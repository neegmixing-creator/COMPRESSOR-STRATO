#include "CompressorEngine.h"

namespace cmp
{
// ======================================================================================
//  STRATA ADAPTIVE ENGINE
//
//  Fluxo por amostra, por caminho (L/R ou M/S):
//    entrada -> Detector (pico, RMS, sustain, transiente, graves) -> nível adaptativo (dB)
//            -> curva estática (threshold/ratio/knee EFETIVOS, dependentes de STRENGTH e do programa)
//            -> stereo link -> balística (attack adaptativo + release em 3 estágios) -> ganho
//            -> Character (amount = f(CHARACTER, GR real)) em oversampling -> Auto Gain por energia
//
//  STRENGTH (s, 0..1) é interpretado UMA vez, em computeStrengthIntent() (StrengthIntent.h), e todos os
//  estágios consultam essa intenção: profundidade, sensibilidade do detector, preservação de transiente,
//  adaptação de attack/release, peso do sustain, resposta à densidade e acoplamento do Character.
//  O PROGRAMA (transiente / sustain / graves) e o MODO modulam o que cada estágio faz com a intenção.
// ======================================================================================
namespace
{
struct ModeConfig
{
    // --- caminho legado ---
    float rmsMix, peakDecayMs;
    float attackScale, attackFloorMs, attackCeilMs;    // 0 = sem teto
    float releaseScale;
    float kneeAdd, kneeScale;
    float hpfFloorHz, linkFloor;
    bool  forceAutoRelease;
    // --- motor adaptativo ---
    float sustAttackMul, transAttackMul;   // attack em material sustentado / em transientes
    float transRelief;                     // dB de alívio do threshold durante transientes (impacto)
    float lfProtect;                       // proteção de graves do detector (0..1)
    float relBias;                         // >0 favorece cauda lenta, <0 recuperação rápida
    float sustainW;                        // peso da energia sustentada no nível (glue)
};

const ModeConfig kModes[] = {
    // rms  pkDec atkSc flr  ceil  relSc kAdd kSc  hpf   link  autoR | sustA transA relief lfP  relBias sustW
    { 0.25f, 2.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.00f, false,  1.0f, 1.0f, 1.0f, 0.30f,  0.00f, 0.0f }, // Clean
    { 0.00f, 1.0f, 1.8f, 2.0f, 0.0f, 0.8f, 0.0f, 0.8f,  0.0f, 0.00f, false,  1.0f, 2.2f, 6.0f, 0.50f, -0.15f, 0.0f }, // Punch
    { 0.85f, 5.0f, 1.6f, 0.0f, 0.0f, 1.4f, 6.0f, 1.0f,  0.0f, 0.00f, true,   1.0f, 1.2f, 1.0f, 0.40f,  0.15f, 0.3f }, // Smooth
    { 0.00f, 0.5f, 0.25f,0.0f, 3.0f, 0.6f, 0.0f, 0.3f,  0.0f, 0.00f, false,  0.8f, 0.5f, 0.0f, 0.20f, -0.20f, 0.0f }, // Aggressive
    { 0.60f, 3.0f, 2.5f, 1.0f, 0.0f, 1.2f, 4.0f, 1.0f, 60.0f, 0.75f, true,   1.0f, 1.6f, 2.0f, 0.60f,  0.10f, 0.4f }, // Bus
};
} // namespace

float CompressorEngine::computeGainReductionDb (float x, float thr, float invR, float knee) noexcept
{
    const float over = x - thr;
    const float slope = 1.0f - invR;
    if (knee <= 0.01f)  return over > 0.0f ? over * slope : 0.0f;
    if (2.0f * over < -knee) return 0.0f;
    if (2.0f * over >  knee) return over * slope;
    const float t = over + knee * 0.5f;
    return slope * t * t / (2.0f * knee);
}

void CompressorEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    const int maxDelay = (int) std::ceil (0.0051 * sr) + 64;
    for (int c = 0; c < 2; ++c) { path[c].det.prepare (sr); path[c].line.prepare (maxDelay); rawLine[c].prepare (maxDelay); }
    os.prepare (2);
    os.setFactor (1);
    sat.setSampleRate (sr);

    smIn.prepare (sr, 15); smOut.prepare (sr, 15); smMakeup.prepare (sr, 15); smMix.prepare (sr, 15);
    smByp.prepare (sr, 10); smThr.prepare (sr, 10); smInvR.prepare (sr, 10); smKnee.prepare (sr, 10);
    smStrength.prepare (sr, 20); smChar.prepare (sr, 20);
    smAttack.prepare (sr / kCtl, 20); smRelease.prepare (sr / kCtl, 20);

    twCoef     = 1.0f - timeToCoef (5.0f, sr);
    grSlowCoef = 1.0f - timeToCoef (300.0f, sr);
    gateCoef   = 1.0f - timeToCoef (50.0f, sr);
    avgCoef    = 1.0f - timeToCoef (1500.0f, sr);
    autoCoef   = 1.0f - timeToCoef (400.0f, sr);
    densCoef   = 1.0f - timeToCoef (700.0f, sr);
    medCoef    = 1.0f - timeToCoef (60.0f, sr);
    eAgCoef    = 1.0f - timeToCoef (400.0f, sr);
    tubeDcR    = (float) (1.0 - 2.0 * kPi * 3.0 / sr);

    needSnap = true;
    reset();
}

void CompressorEngine::resetDsp() noexcept
{
    for (auto& p : path)
    {
        p.det.reset(); p.line.reset();
        p.y1 = p.y = p.grSlow = p.transient = p.density = p.gMed = p.gLong = 0; p.r[0] = p.r[1] = p.r[2] = 0;
        p.dcX = p.dcY = 0;
    }
    os.reset();
    sat.reset();
    gate = avgGr = autoDb = eIn = eOut = agGate = 0.0f; agOpen = false;
}

void CompressorEngine::reset() noexcept
{
    resetDsp();
    for (auto& r : rawLine) r.reset();
    wasBypassed = false;
    needSnap = true;
}

void CompressorEngine::setParams (const Params& in) noexcept
{
    const ModeConfig& m = kModes[(int) in.mode];
    mode = in.mode; modeIdx = (int) in.mode; satMode = in.satMode; stereoMode = in.stereoMode;
    autoGain = in.autoGain; bypassTarget = in.bypass; adaptive = in.adaptive;

    smIn.setTarget (dbToLin (std::clamp (in.inputDb, -24.0f, 24.0f)));
    smOut.setTarget (dbToLin (std::clamp (in.outputDb, -24.0f, 24.0f)));
    smMakeup.setTarget (dbToLin (std::clamp (in.makeupDb, -12.0f, 24.0f)));
    smMix.setTarget (std::clamp (in.mix, 0.0f, 1.0f));
    smByp.setTarget (in.bypass ? 1.0f : 0.0f);
    smThr.setTarget (std::clamp (in.thresholdDb, -60.0f, 0.0f));
    smInvR.setTarget (1.0f / std::clamp (in.ratio, 1.0f, 1000.0f));
    smKnee.setTarget (std::clamp (in.kneeDb, 0.0f, 24.0f) * m.kneeScale + m.kneeAdd);
    smStrength.setTarget (std::clamp (in.strength, 0.0f, 1.0f));
    smChar.setTarget (std::clamp (in.character, 0.0f, 1.0f));
    smAttack.setTarget (std::clamp (in.attackMs, 0.05f, 100.0f));
    smRelease.setTarget (std::clamp (in.releaseMs, 10.0f, 2000.0f));
    autoAtt = in.autoAttack;
    autoRel = in.autoRelease || m.forceAutoRelease;

    // Coeficientes do caminho legado (o adaptativo calcula os seus em updateControl)
    float a = std::clamp (in.attackMs, 0.05f, 100.0f) * m.attackScale;
    a = std::max (a, m.attackFloorMs);
    if (m.attackCeilMs > 0.0f) a = std::min (a, m.attackCeilMs);
    const float r = std::clamp (in.releaseMs, 10.0f, 2000.0f) * m.releaseScale;
    attA = timeToCoef (autoAtt ? a * 2.0f : a, sr);
    attB = timeToCoef (autoAtt ? a * 0.35f : a, sr);
    relA = timeToCoef (autoRel ? r * 2.5f : r, sr);
    relB = timeToCoef (autoRel ? r * 0.3f : r, sr);

    const float hpf = std::max (in.sidechainHpfHz, m.hpfFloorHz);
    for (auto& p : path) p.det.configure (m.rmsMix, m.peakDecayMs, hpf, in.adaptive ? m.lfProtect : 0.0f);
    linkEff = std::max (std::clamp (in.stereoLink, 0.0f, 1.0f), m.linkFloor);

    lookaheadSamples = (int) std::lround (std::clamp (in.lookaheadMs, 0.0f, 5.0f) * 0.001 * sr);

    const int f = in.oversampling >= 8 ? 8 : in.oversampling >= 4 ? 4 : in.oversampling >= 2 ? 2 : 1;
    if (f != os.getFactor()) { os.setFactor (f); sat.setSampleRate (sr * f); }
    sat.setMode (in.satMode);

    if (needSnap)
    {
        for (Smoother* s : { &smIn, &smOut, &smMakeup, &smMix, &smByp, &smThr, &smInvR, &smKnee, &smStrength, &smChar, &smAttack, &smRelease })
            s->snapToTarget();
        needSnap = false;
        ctl = 0;
        updateControl();
    }
}

// Taxa de controle: tudo que usa pow/exp e não precisa de resolução por amostra.
void CompressorEngine::updateControl() noexcept
{
    const ModeConfig& m = kModes[modeIdx];
    const float s = smStrength.getCurrent();
    intent = computeStrengthIntent (s);
    gScale = intent.compressionDepth;
    if (! adaptive) { gScale = std::pow (std::min (s * 2.0f, 1.0f), 1.3f); return; }   // legado: curva original

    const float uA = smAttack.getCurrent(), uR = smRelease.getCurrent();   // avançam 1 passo por tick (suavização 20 ms)
    smAttack.next(); smRelease.next();

    // ATTACK: STRENGTH baixo -> mais lento (preserva transientes); alto -> mais rápido.
    const float strMul = intent.attackAdaptation;
    float sustMs = uA * strMul * m.sustAttackMul;
    float transMs = autoAtt ? uA * strMul * m.transAttackMul : sustMs;
    auto lim = [&] (float ms) { ms = std::max (ms, m.attackFloorMs); if (m.attackCeilMs > 0.0f) ms = std::min (ms, m.attackCeilMs); return std::max (ms, 0.02f); };
    aS = timeToCoef (lim (sustMs), sr);
    aT = timeToCoef (lim (transMs), sr);

    // RELEASE: três constantes (recuperação rápida / média / cauda lenta); mais rápido com STRENGTH alto.
    const float R = uR * m.releaseScale * intent.releaseAdaptation;
    if (autoRel) { rc[0] = timeToCoef (R * 0.2f, sr); rc[1] = timeToCoef (R, sr); rc[2] = timeToCoef (R * 3.5f, sr); }
    else         { rc[0] = rc[1] = rc[2] = timeToCoef (R, sr); }
}

void CompressorEngine::process (float* const* ch, int numCh, int numSamples) noexcept
{
    const int nc = std::min (numCh, 2);
    if (nc <= 0) return;
    const bool stereo = nc == 2;
    const bool msActive = stereo && stereoMode != StereoMode::Stereo;
    const bool act[2] = { ! (stereo && stereoMode == StereoMode::Side), stereo && stereoMode != StereoMode::Mid };
    const ModeConfig& mc = kModes[modeIdx];

    float inPk = 0.0f, outPk = 0.0f, grPk = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float raw[2] = { 0.0f, 0.0f }, rawD[2] = { 0.0f, 0.0f };
        const int total = getLatencySamples();
        for (int c = 0; c < nc; ++c)
        {
            const float v = ch[c][n];
            raw[c] = std::isfinite (v) ? v : 0.0f;
            rawLine[c].push (raw[c]);
            rawD[c] = rawLine[c].read (total);
        }

        const float byp = smByp.next();

        // ---- Bypass total: Input -> Output (compensado em latência), DSP parado ----
        if (bypassTarget && byp >= 1.0f)
        {
            if (! wasBypassed) { resetDsp(); wasBypassed = true; }
            for (int c = 0; c < nc; ++c)
            {
                ch[c][n] = rawD[c];
                inPk = std::max (inPk, std::fabs (raw[c]));
                outPk = std::max (outPk, std::fabs (rawD[c]));
            }
            continue;
        }
        wasBypassed = false;

        const float inG = smIn.next(), outG = smOut.next(), mkG = smMakeup.next(), mixV = smMix.next();
        const float thr = smThr.next(), invR = smInvR.next(), knee = smKnee.next();
        const float str = smStrength.next(), chr = smChar.next();
        if (++ctl >= kCtl) { ctl = 0; updateControl(); }

        float xg[2] = { 0, 0 }, p[2] = { 0, 0 };
        for (int c = 0; c < nc; ++c) { xg[c] = raw[c] * inG; inPk = std::max (inPk, std::fabs (xg[c])); }
        if (msActive) { p[0] = (xg[0] + xg[1]) * 0.5f; p[1] = (xg[0] - xg[1]) * 0.5f; }
        else          { p[0] = xg[0]; p[1] = xg[1]; }
        for (int c = 0; c < nc; ++c) path[c].line.push (p[c]);

        // ---- Detector + curva estática ----
        float gs[2] = { 0, 0 }, crest[2] = { 0, 0 }, tr[2] = { 0, 0 }, lvlMax = -120.0f;
        if (adaptive)
        {
            const StrengthIntent& I = intent;
            const float sensDb = 3.0f * I.detectorSensitivity;       // sensibilidade do detector sobe no extremo
            const float sustW = std::clamp (mc.sustainW + 0.3f * I.sustainWeight, 0.0f, 0.6f);   // densificar
            for (int c = 0; c < nc; ++c)
            {
                if (! act[c]) continue;
                const Detector::Result d = path[c].det.process (p[c]);
                tr[c] = d.transient;
                // Peso entre envelope rápido (pico) e lento (RMS): transiente -> pico; sensibilidade alta -> mais pico.
                const float rmsW = mc.rmsMix * (1.0f - 0.7f * d.transient) * (1.0f - 0.5f * I.detectorSensitivity);
                float lvl = (1.0f - rmsW) * d.peakLin + rmsW * d.rmsLin;
                if (sustW > 0.0f) lvl += sustW * (d.sustainLin - lvl);                      // energia sustentada
                const float lvlDb = linToDb (lvl) + sensDb;
                // Threshold efetivo: o do usuário continua marcando "onde começa". Aprofundamento limitado (<= 6 dB)
                // e maior em material sustentado; transientes ganham alívio proporcional à preservação.
                const float thrE = thr - I.thresholdDeepenDb * (0.6f + 0.4f * (1.0f - d.transient))
                                       + mc.transRelief * I.transientPreservation * d.transient * (1.0f - I.detectorSensitivity);
                // Ratio efetivo: continua sendo o ratio do usuário (1:1 = nunca comprime; inf = limiting); o empurrão
                // é proporcional à inclinação do usuário e menor em transientes.
                const float slope = 1.0f - invR;
                const float slopeE = std::min (1.0f, slope + (1.0f - slope) * slope * I.ratioPush * (1.0f - 0.5f * d.transient));
                const float kneeE = knee * (1.0f - I.kneeFirmness);
                gs[c] = computeGainReductionDb (lvlDb, thrE, 1.0f - slopeE, kneeE) * I.compressionDepth;
                lvlMax = std::max (lvlMax, lvlDb);
            }
        }
        else   // caminho legado: STRENGTH como macro (0.5 neutro)
        {
            const float a = 2.0f * str;
            const float bst = std::max (0.0f, a - 1.0f);
            const float effThr = thr - bst * 18.0f;
            const float effInvR = invR / (1.0f + bst * 1.5f);
            const float grScale = std::min (a, 1.0f);
            for (int c = 0; c < nc; ++c)
            {
                if (! act[c]) continue;
                const Detector::Result d = path[c].det.process (p[c]);
                gs[c] = computeGainReductionDb (d.levelDb, effThr, effInvR, knee) * grScale;
                crest[c] = d.crestDb;
                lvlMax = std::max (lvlMax, d.levelDb);
            }
        }
        if (act[0] && act[1])                                  // STEREO LINK: 0 = independente, 1 = linkado
        {
            const float mx = std::max (gs[0], gs[1]);
            gs[0] += linkEff * (mx - gs[0]);
            gs[1] += linkEff * (mx - gs[1]);
        }

        // ---- Balística ----
        float gr[2] = { 0, 0 };
        for (int c = 0; c < nc; ++c)
        {
            Path& P = path[c];
            if (! act[c]) { P.y1 = P.y = P.gMed = P.gLong = 0.0f; P.r[0] = P.r[1] = P.r[2] = 0.0f; continue; }

            if (adaptive)
            {
                P.density += (tr[c] - P.density) * densCoef;
                const float att = aS + tr[c] * (aT - aS);                       // transiente -> attack de transiente
                // Pesos dos 3 estágios: GR pequeno -> recuperação rápida; GR grande -> controlada/lenta.
                const float gm  = std::clamp (P.y * (1.0f / 12.0f), 0.0f, 1.0f);
                // Duração do evento: o estágio médio é alimentado pela GR média de 60 ms e o lento pela de 300 ms.
                // Evento curto -> estágios lentos mal carregam -> recuperação rápida; evento longo -> cauda controlada.
                P.gMed  += (gs[c] - P.gMed)  * medCoef;
                P.gLong += (gs[c] - P.gLong) * grSlowCoef;
                float wF = 0.6f - 0.4f * gm, wM = 0.3f + 0.1f * gm, wS = 0.1f + 0.3f * gm;
                const float shift = mc.relBias;                                   // personalidade do modo
                if (shift > 0.0f) { const float d = shift * wF; wF -= d; wS += d; }
                else              { const float d = -shift * wS; wS -= d; wF += d; }
                { const float d = (0.25f + 0.5f * intent.densityResponse) * P.density * wS; wS -= d; wM += d; }    // eventos densos: cauda mais curta (anti-pumping)
                const float in3[3] = { gs[c], P.gMed, P.gLong };
                for (int i = 0; i < 3; ++i)
                    P.r[i] = flushDenorm (std::max (in3[i], rc[i] * P.r[i] + (1.0f - rc[i]) * in3[i]));
                P.y1 = std::max (gs[c], wF * P.r[0] + wM * P.r[1] + wS * P.r[2]);
                P.y  = flushDenorm (att * P.y + (1.0f - att) * P.y1);
            }
            else
            {
                float tw = std::clamp ((crest[c] - 4.0f) / 10.0f, 0.0f, 1.0f);
                P.transient += (tw - P.transient) * twCoef;
                const float att = autoAtt ? attA + P.transient * (attB - attA) : attA;
                const float wr = std::clamp (P.grSlow / std::max (P.y1, 0.5f), 0.0f, 1.0f);
                const float rel = autoRel ? relB + wr * (relA - relB) : relA;
                P.y1 = flushDenorm (std::max (gs[c], rel * P.y1 + (1.0f - rel) * gs[c]));
                P.y  = flushDenorm (att * P.y + (1.0f - att) * P.y1);
            }
            P.grSlow = flushDenorm (P.grSlow + (P.y - P.grSlow) * grSlowCoef);
            gr[c] = P.y;
            grPk = std::max (grPk, P.y);
        }

        // ---- Ganho, Character (amount ligado à GR real), saturação em oversampling ----
        // Character 0 => transparente. Com adaptativo, o caráter cresce com a compressão efetiva.
        const float grMax = std::max (gr[0], gr[1]);
        // GR ~ 0 => caráter 0 (transparente); cresce com a GR real e com o acoplamento do STRENGTH.
        const float charAmt = adaptive ? chr * intent.characterCoupling * smoothStep (0.2f, 10.0f, grMax) : chr;
        // Drive acoplado à GR: o sinal comprimido é "reaquecido" antes do saturador (e normalizado depois),
        // então mais compressão => mais caráter, com ganho de pequenos sinais = 1. CHARACTER 0 => drive 1.
        const float driveLin = adaptive && charAmt > 0.0f ? dbToLin (std::min (12.0f, 0.6f * grMax) * chr) : 1.0f;   // drive interno limitado a +12 dB
        const float invDrive = 1.0f / driveLin;
        const bool charActive = charAmt > 1.0e-4f;
        float w[2] = { 0, 0 };
        for (int c = 0; c < nc; ++c)
        {
            float s = path[c].line.read (lookaheadSamples) * std::exp (-gr[c] * 0.11512925f);
            if (os.getFactor() > 1)
                s = os.process (s, c, [&] (float v) noexcept { return sat.process (v * driveLin, charAmt, c) * invDrive; });
            else
                s = sat.process (s * driveLin, charAmt, c) * invDrive;
            Path& P = path[c];
            if (satMode == SatMode::Tube && charActive)          // sem DC offset vindo da assimetria
            {
                const double y = (double) s - P.dcX + tubeDcR * P.dcY;
                P.dcX = s; P.dcY = flushDenorm (y); s = (float) y;
            }
            else { P.dcX = s; P.dcY = s; }                       // acompanha o sinal: entrada suave no bloqueador
            w[c] = s;
        }
        if (msActive) { const float m = w[0], sd = w[1]; w[0] = m + sd; w[1] = m - sd; }

        // ---- Auto Gain por ENERGIA: compara energia de entrada vs. saída (janela ~400 ms) ----
        float sIn = 0.0f, sOut = 0.0f;
        for (int c = 0; c < nc; ++c) { sIn += xg[c] * xg[c]; sOut += w[c] * w[c]; }
        eIn  = flushDenorm (eIn  + (sIn  - eIn)  * eAgCoef);
        eOut = flushDenorm (eOut + (sOut - eOut) * eAgCoef);
        if (++agCtl >= 32)
        {
            agCtl = 0;
            // Gate com histerese (abre > -52 dB, fecha < -60 dB RMS por canal): silêncio e sinais muito baixos => 0 dB.
            const float eNorm = eIn / (float) nc;
            if (! agOpen && eNorm > 6.3e-6f) agOpen = true;
            else if (agOpen && eNorm < 1.0e-6f) agOpen = false;
            agGate += ((agOpen && autoGain ? 1.0f : 0.0f) - agGate) * 0.05f;
            float target = 0.0f;
            if (autoGain && eOut > 1.0e-9f)
                target = std::clamp (0.85f * 10.0f * std::log10 (std::max (eIn, 1.0e-12f) / eOut), -6.0f, 18.0f) * agGate;
            const float k = 1.0f - std::pow (1.0f - autoCoef, 32.0f);
            const float maxStep = 8.0f * 32.0f / (float) sr;                          // <= 8 dB/s, independente do sample rate
            const float step = std::clamp ((target - autoDb) * k, -maxStep, maxStep);
            autoDb = flushDenorm (autoDb + step);
        }
        const float autoLin = std::fabs (autoDb) > 1.0e-4f ? dbToLin (autoDb) : 1.0f;
        const float wetG = mkG * autoLin;

        // ---- Mix, output, bypass com crossfade ----
        for (int c = 0; c < nc; ++c)
        {
            const float dry = rawD[c] * inG;
            const float mixed = dry + (w[c] * wetG - dry) * mixV;
            float o = mixed * outG;
            if (byp > 0.0f) o = o + (rawD[c] - o) * byp;
            if (! std::isfinite (o)) o = 0.0f;
            ch[c][n] = o;
            outPk = std::max (outPk, std::fabs (o));
        }
    }

    meters.inPeak.store (inPk, std::memory_order_relaxed);
    meters.outPeak.store (outPk, std::memory_order_relaxed);
    meters.grDb.store (grPk, std::memory_order_relaxed);
    MeterData::atomicMax (meters.inPeakUi, inPk);
    MeterData::atomicMax (meters.outPeakUi, outPk);
    MeterData::atomicMax (meters.grUi, grPk);
}
} // namespace cmp
