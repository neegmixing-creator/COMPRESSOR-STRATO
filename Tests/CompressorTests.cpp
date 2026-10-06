// Testes do núcleo DSP (sem JUCE). Retorna 0 se tudo passar.
#include "CompressorEngine.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <atomic>
#include <new>

// Contador de alocações: prova de real-time safety (nenhuma alocação dentro de process / setParams)
static std::atomic<long> g_allocs { 0 };
void* operator new (std::size_t n) { ++g_allocs; if (void* p = std::malloc (n)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

using namespace cmp;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("  FAIL %s:%d  %s  -> ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using Buf = std::vector<float>;

static Buf sine (double f, double ampDb, int n, double sr, double phase = 0.0)
{
    Buf b ((size_t) n);
    const double a = std::pow (10.0, ampDb / 20.0);
    for (int i = 0; i < n; ++i) b[(size_t) i] = (float) (a * std::sin (2.0 * kPi * f * i / sr + phase));
    return b;
}
static double rmsDb (const Buf& b, int from, int to)
{
    double s = 0; for (int i = from; i < to; ++i) s += (double) b[(size_t) i] * b[(size_t) i];
    return 10.0 * std::log10 (std::max (s / std::max (1, to - from), 1e-20));
}
static double goertzelDb (const Buf& b, int from, int to, double f, double sr)   // amplitude de pico em dB
{
    double re = 0, im = 0;
    for (int i = from; i < to; ++i) { const double w = 2 * kPi * f * i / sr; re += b[(size_t) i] * std::cos (w); im += b[(size_t) i] * std::sin (w); }
    const double amp = 2.0 * std::sqrt (re * re + im * im) / (to - from);
    return 20.0 * std::log10 (std::max (amp, 1e-12));
}
static bool allFinite (const Buf& b) { for (float v : b) if (! std::isfinite (v)) return false; return true; }

struct Rig
{
    CompressorEngine e; double sr;
    explicit Rig (double rate = 48000.0) : sr (rate) { e.prepare (sr); }
    void setup (const Params& p) { e.setParams (p); e.reset(); e.setParams (p); }
    Buf run (const Buf& in, int block = 256)
    {
        Buf out = in;
        for (size_t i = 0; i < out.size(); i += (size_t) block)
        {
            float* ptr = out.data() + i;
            e.process (&ptr, 1, (int) std::min<size_t> ((size_t) block, out.size() - i));
        }
        return out;
    }
};

static Params base()
{
    Params p; p.thresholdDb = -30.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.attackMs = 1.0f; p.releaseMs = 100.0f;
    p.adaptive = false;                      // suíte original: valida o caminho legado (projetos antigos)
    return p;
}
static double steadyGrDb (Params p, double sr = 48000.0, double inDb = -6.0, double f = 1000.0)
{
    Rig r (sr); r.setup (p);
    const int n = (int) (sr * 1.0);
    Buf in = sine (f, inDb, n, sr), out = r.run (in);
    return rmsDb (in, n / 2, n) - rmsDb (out, n / 2, n);
}

static void testStaticCurve()
{
    std::puts("[threshold / ratio / knee: curva estática]");
    auto gr = [] (float x, float t, float r, float k) { return CompressorEngine::computeGainReductionDb (x, t, 1.0f / r, k); };
    CHECK (std::fabs (gr (-10, -20, 4, 0) - 7.5f) < 1e-4f, "hard knee");
    CHECK (gr (-30, -20, 4, 0) == 0.0f, "abaixo do threshold");
    CHECK (std::fabs (gr (-20, -20, 4, 12) - 0.75f * 12 / 8) < 1e-4f, "no threshold, soft knee 12 dB = slope*W/8");
    float prev = -1e9f; bool mono = true, cont = true; float last = 0;
    for (float x = -80; x <= 0; x += 0.05f)
    {
        const float out = x - gr (x, -24, 8, 12);
        if (out < prev - 1e-5f) mono = false;
        if (x > -80 && std::fabs (gr (x, -24, 8, 12) - last) > 0.2f) cont = false;
        prev = out; last = gr (x, -24, 8, 12);
    }
    CHECK (mono, "saída monotônica crescente"); CHECK (cont, "curva contínua");
    CHECK (std::fabs (gr (0, -20, 1.0f, 6)) < 1e-6f, "ratio 1:1 não comprime");
}

static void testThresholdRatio()
{
    std::puts("[threshold / ratio dinâmicos]");
    Params p = base(); p.ratio = 1000.0f;
    const double grInf = steadyGrDb (p);
    CHECK (grInf > 15.0, "GR com ratio infinito = %.2f", grInf);
    for (float r : { 2.0f, 4.0f, 8.0f })
    {
        p.ratio = r;
        const double expected = (1.0 - 1.0 / r) * grInf, got = steadyGrDb (p);
        CHECK (std::fabs (got - expected) < 0.8, "ratio %.0f: esperado %.2f, obtido %.2f", r, expected, got);
    }
    p.ratio = 4.0f; p.thresholdDb = -30; const double g30 = steadyGrDb (p);
    p.thresholdDb = -24; const double g24 = steadyGrDb (p);
    CHECK (std::fabs ((g30 - g24) - 4.5) < 0.8, "6 dB de threshold -> 4.5 dB de GR (obtido %.2f)", g30 - g24);
    p.thresholdDb = 0; CHECK (std::fabs (steadyGrDb (p)) < 0.2, "threshold 0 dB: sem compressão");
    p.thresholdDb = -30; p.strength = 0.0f; CHECK (std::fabs (steadyGrDb (p)) < 0.1, "strength 0 = sem compressão");
    p.strength = 1.0f; const double gMax = steadyGrDb (p);
    p.strength = 0.5f; CHECK (gMax > steadyGrDb (p) + 3.0, "strength 100%% comprime bem mais que 50%% (%.1f)", gMax);
}

static double stepTime63 (Params p, double sr, bool attackTest)
{
    Rig r (sr); r.setup (p);
    const int pre = (int) (sr * 1.0), len = (int) (sr * 1.5);
    Buf quiet = sine (1000, -50, pre, sr), loud = sine (1000, -6, len, sr);
    Buf lead = quiet; r.run (lead);
    auto grNow = [&] (float v) { float* pv = &v; r.e.process (&pv, 1, 1); return r.e.getMeterData().grDb.load(); };
    std::vector<float> g;
    if (attackTest)
    {
        for (float v : loud) g.push_back (grNow (v));
    }
    else
    {
        for (int i = 0; i < len; ++i) grNow (loud[(size_t) i]);
        for (int i = 0; i < len; ++i) g.push_back (grNow (0.0f));
    }
    const float peak = attackTest ? g.back() : *std::max_element (g.begin(), g.begin() + 4);
    const float level = attackTest ? 0.63f * peak : 0.37f * peak;
    for (size_t i = 0; i < g.size(); ++i)
        if (attackTest ? g[i] >= level : g[i] <= level) return 1000.0 * (double) i / sr;
    return 1e9;
}

static void testAttackRelease()
{
    std::puts("[attack / release / sample rates]");
    Params p = base(); p.ratio = 8.0f; p.releaseMs = 100;
    p.attackMs = 2.0f;  const double a2 = stepTime63 (p, 48000, true);
    p.attackMs = 40.0f; const double a40 = stepTime63 (p, 48000, true);
    CHECK (a40 > 4.0 * a2, "attack 40 ms (%.1f) deve ser >4x o de 2 ms (%.1f)", a40, a2);
    p.attackMs = 1.0f; p.releaseMs = 50;  const double r50 = stepTime63 (p, 48000, false);
    p.releaseMs = 500; const double r500 = stepTime63 (p, 48000, false);
    CHECK (r500 > 4.0 * r50, "release 500 ms (%.1f) deve ser >4x o de 50 ms (%.1f)", r500, r50);
    p.releaseMs = 200;
    const double ref = stepTime63 (p, 48000, false);
    for (double sr : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const double t = stepTime63 (p, sr, false);
        CHECK (std::fabs (t - ref) / ref < 0.15, "release a %.0f Hz: %.1f ms vs %.1f ms @48k", sr, t, ref);
    }
    // estabilidade do release: GR nunca volta a subir após o sinal cair a zero
    Rig r; r.setup (p);
    Buf loud = sine (1000, -6, 48000, 48000);
    r.run (loud);
    float prev = 1e9f; bool mono = true;
    for (int i = 0; i < 48000; ++i) { float v = 0; float* pv = &v; r.e.process (&pv, 1, 1); const float g = r.e.getMeterData().grDb.load(); if (g > prev + 1e-4f) mono = false; prev = g; }
    CHECK (mono, "release monotônico (sem oscilação)");
}

static void testMakeupMixBypass()
{
    std::puts("[makeup / mix / bypass / input / output]");
    Params p = base(); p.ratio = 1.0f; p.makeupDb = 6.0f;
    Rig r; r.setup (p); Buf in = sine (1000, -20, 48000, 48000), out = r.run (in);
    CHECK (std::fabs ((rmsDb (out, 24000, 48000) - rmsDb (in, 24000, 48000)) - 6.0) < 0.05, "makeup +6 dB");
    p.makeupDb = 0; p.inputDb = 6; p.outputDb = -12; r.setup (p); out = r.run (in);
    CHECK (std::fabs ((rmsDb (out, 24000, 48000) - rmsDb (in, 24000, 48000)) + 6.0) < 0.05, "input +6 / output -12 = -6 dB");

    p = base(); p.ratio = 1000.0f; p.mix = 0.0f; p.lookaheadMs = 2.0f; r.setup (p);
    in = sine (440, -3, 24000, 48000); out = r.run (in);
    const int lat = r.e.getLatencySamples(); double maxErr = 0;
    for (int i = lat; i < (int) in.size(); ++i) maxErr = std::max (maxErr, (double) std::fabs (out[(size_t) i] - in[(size_t) (i - lat)]));
    CHECK (lat == 96 && maxErr < 1e-6, "mix 0%% = dry alinhado (lat %d, erro %.2e)", lat, maxErr);
    p.mix = 0.5f; r.setup (p); out = r.run (in);
    const double mixed = rmsDb (out, 12000, 24000);
    p.mix = 1.0f; r.setup (p); const double wet = rmsDb (r.run (in), 12000, 24000);
    CHECK (mixed > wet + 0.5 && mixed < rmsDb (in, 12000, 24000), "mix 50%% entre wet e dry");

    p = base(); p.bypass = true; p.lookaheadMs = 5.0f; p.oversampling = 4; r.setup (p);
    std::mt19937 rng (1); std::uniform_real_distribution<float> d (-1, 1);
    in.assign (20000, 0); for (auto& v : in) v = d (rng); out = r.run (in);
    const int l2 = r.e.getLatencySamples(); maxErr = 0;
    for (int i = l2; i < (int) in.size(); ++i) maxErr = std::max (maxErr, (double) std::fabs (out[(size_t) i] - in[(size_t) (i - l2)]));
    CHECK (maxErr == 0.0, "bypass bit-exato com latência %d (erro %.2e)", l2, maxErr);

    // bypass sem clicks: liga/desliga no meio de uma senoide comprimida
    p = base(); p.ratio = 1000.0f; p.thresholdDb = -40; r.setup (p);
    in = sine (440, -3, 48000, 48000); out.assign (in.size(), 0);
    for (int b = 0; b + 256 <= 48000; b += 256)
    {
        p.bypass = (b / 256) % 40 > 20; r.e.setParams (p);
        float* pp = in.data() + b; Buf tmp (pp, pp + 256); float* t = tmp.data(); r.e.process (&t, 1, 256);
        std::copy (tmp.begin(), tmp.end(), out.begin() + b);
    }
    double maxStep = 0; for (size_t i = 1; i < 47872; ++i) maxStep = std::max (maxStep, (double) std::fabs (out[i] - out[i - 1]));
    CHECK (maxStep < 0.12, "sem click no bypass (maior salto %.3f)", maxStep);
}

static void testSidechain()
{
    std::puts("[sidechain HPF]");
    const double sr = 48000; const int n = 96000;
    Buf low = sine (50, -6, n, sr), high = sine (5000, -34, n, sr), in (n);
    for (int i = 0; i < n; ++i) in[(size_t) i] = low[(size_t) i] + high[(size_t) i];
    Params p = base(); p.thresholdDb = -20; p.ratio = 1000.0f; p.releaseMs = 200; p.mode = Mode::Smooth;
    Rig r; r.setup (p); Buf offOut = r.run (in);
    p.sidechainHpfHz = 250; r.setup (p); Buf hpfOut = r.run (in);
    const double hOff = goertzelDb (offOut, n / 2, n, 5000, sr), hOn = goertzelDb (hpfOut, n / 2, n, 5000, sr);
    CHECK (hOn > hOff + 6.0, "5 kHz atenua menos com HPF (%.1f dB vs %.1f dB)", hOn, hOff);
    CHECK (hOn > -34.0 - 2.0, "com HPF 250 Hz o grave não pumpa os agudos (%.1f dB)", hOn);
}

static void testOversampling()
{
    std::puts("[oversampling]");
    const double sr = 48000; const int n = 48000;
    for (int f : { 2, 4, 8 })
    {
        Params p = base(); p.ratio = 1.0f; p.oversampling = f;
        Rig r; r.setup (p);
        Buf imp (4096, 0.0f); imp[100] = 1.0f; Buf o = r.run (imp);
        const int lat = r.e.getLatencySamples();
        size_t pk = (size_t) (std::max_element (o.begin(), o.end(), [] (float a, float b) { return std::fabs (a) < std::fabs (b); }) - o.begin());
        CHECK ((int) pk == 100 + lat, "latência %dx: pico em %zu (esperado %d)", f, pk, 100 + lat);
        Buf s1 = sine (1000, -6, n, sr), s2 = sine (12000, -6, n, sr);
        const double e1 = goertzelDb (r.run (s1), n / 2, n, 1000, sr), e2 = goertzelDb (r.run (s2), n / 2, n, 12000, sr);
        CHECK (std::fabs (e1 + 6.0) < 0.05 && std::fabs (e2 + 6.0) < 0.3, "resposta plana %dx: %.3f dB @1k, %.3f dB @12k", f, e1, e2);
    }
    // aliasing: 3º harmônico de 15 kHz (45 kHz) dobra para 3 kHz a 48 kHz
    Params p = base(); p.ratio = 1.0f; p.character = 1.0f; p.satMode = SatMode::Modern;
    Buf in = sine (15000, -3, n, sr);
    Rig r; p.oversampling = 1; r.setup (p); const double a1 = goertzelDb (r.run (in), n / 2, n, 3000, sr);
    p.oversampling = 8; r.setup (p); const double a8 = goertzelDb (r.run (in), n / 2, n, 3000, sr);
    CHECK (a8 < a1 - 20.0, "aliasing @3 kHz: 1x = %.1f dB, 8x = %.1f dB", a1, a8);
    // saturação: sem DC e sem NaN em todos os modos
    for (SatMode m : { SatMode::Tube, SatMode::Tape, SatMode::Modern })
        for (int f : { 1, 2, 4, 8 })
        {
            p.satMode = m; p.oversampling = f; r.setup (p);
            Buf o = r.run (sine (200, -3, 96000, sr));
            double mean = 0; for (int i = 48000; i < 96000; ++i) mean += o[(size_t) i]; mean /= 48000;
            CHECK (allFinite (o) && std::fabs (mean) < 2e-3, "sat %d @%dx: DC %.5f", (int) m, f, mean);
        }
}

static void testStereoAndMs()
{
    std::puts("[stereo link / mid-side / mono]");
    const double sr = 48000; const int n = 48000;
    Buf L = sine (1000, -6, n, sr), R = sine (1000, -40, n, sr);
    auto run2 = [&] (const Params& p, Buf& l, Buf& r) { Rig g; g.setup (p); float* ch[2] = { l.data(), r.data() }; for (int i = 0; i < n; i += 256) { float* c[2] = { ch[0] + i, ch[1] + i }; g.e.process (c, 2, std::min (256, n - i)); } };
    Params p = base(); p.stereoLink = 1.0f; Buf l = L, r = R; run2 (p, l, r);
    const double grL = rmsDb (L, n / 2, n) - rmsDb (l, n / 2, n), grR = rmsDb (R, n / 2, n) - rmsDb (r, n / 2, n);
    CHECK (grL > 10 && std::fabs (grL - grR) < 0.2, "link 100%%: GR L %.2f = GR R %.2f", grL, grR);
    p.stereoLink = 0.0f; l = L; r = R; run2 (p, l, r);
    CHECK (std::fabs (rmsDb (R, n / 2, n) - rmsDb (r, n / 2, n)) < 0.1, "link 0%%: canal quieto intacto");
    p.stereoLink = 0.5f; l = L; r = R; run2 (p, l, r);
    const double grR50 = rmsDb (R, n / 2, n) - rmsDb (r, n / 2, n);
    CHECK (grR50 > 3.0 && grR50 < grL - 3.0, "link 50%%: GR R %.2f entre 0 e %.2f", grR50, grL);

    Buf side = sine (1000, -6, n, sr), negSide (n); for (int i = 0; i < n; ++i) negSide[(size_t) i] = -side[(size_t) i];
    p = base(); p.stereoMode = StereoMode::Mid; l = side; r = negSide; run2 (p, l, r);
    CHECK (std::fabs (rmsDb (side, n / 2, n) - rmsDb (l, n / 2, n)) < 0.1, "MID não toca no conteúdo lateral");
    p.stereoMode = StereoMode::Side; l = side; r = negSide; run2 (p, l, r);
    CHECK (rmsDb (side, n / 2, n) - rmsDb (l, n / 2, n) > 10.0, "SIDE comprime o lateral");
    p.stereoMode = StereoMode::Stereo; p.ratio = 1.0f; l = side; r = negSide; run2 (p, l, r);
    CHECK (std::fabs (rmsDb (side, n / 2, n) - rmsDb (r, n / 2, n)) < 0.01, "M/S ida e volta transparente");

    Rig m; m.setup (base()); Buf mono = m.run (sine (1000, -6, n, sr));
    CHECK (allFinite (mono) && rmsDb (mono, n / 2, n) < -9.0, "mono comprime");
}

static void testRobustness()
{
    std::puts("[silêncio / extremos / NaN / clicks]");
    const double sr = 48000; const int n = 96000;
    Params p = base(); p.character = 1.0f; p.satMode = SatMode::Tube; p.oversampling = 4; p.autoGain = true; p.autoRelease = p.autoAttack = true; p.lookaheadMs = 5;
    Rig r; r.setup (p);
    Buf o = r.run (Buf ((size_t) n, 0.0f)); double mx = 0; for (float v : o) mx = std::max (mx, (double) std::fabs (v));
    CHECK (mx == 0.0, "silêncio -> silêncio exato (%.2e)", mx);
    Buf loud = sine (100, 60, n, sr); o = r.run (loud);
    CHECK (allFinite (o), "sinal +60 dBFS finito");
    r.setup (p); o = r.run (sine (1000, -300, n, sr));
    CHECK (allFinite (o), "sinal -300 dB (denormais) finito");
    r.setup (p); Buf bad = sine (1000, -6, n, sr); bad[1000] = NAN; bad[2000] = INFINITY; bad[3000] = -INFINITY;
    o = r.run (bad); CHECK (allFinite (o), "NaN/Inf na entrada não vazam");
    Buf q = sine (1000, -80, n, sr); r.setup (p); o = r.run (q);
    CHECK (rmsDb (o, n / 2, n) < -70.0, "autogain não sobe ruído baixo (%.1f dB)", rmsDb (o, n / 2, n));
    for (int bs : { 1, 7, 64, 480, 1024 })
    {
        Params c = base(); Rig a; a.setup (c); Buf ref = a.run (sine (300, -8, 24000, sr), 24000);
        Rig b; b.setup (c); Buf got = b.run (sine (300, -8, 24000, sr), bs); double e = 0;
        for (size_t i = 0; i < ref.size(); ++i) e = std::max (e, (double) std::fabs (ref[i] - got[i]));
        CHECK (e < 1e-6, "independente do tamanho de bloco (%d): %.2e", bs, e);
    }
    // automação agressiva: sem clicks (salto máximo comparável ao da senoide)
    std::mt19937 rng (7); std::uniform_real_distribution<float> u (0, 1);
    Params a = base(); Rig g; g.setup (a); Buf in = sine (440, -3, 96000, sr), out (in.size());
    for (int b = 0; b < 96000; b += 128)
    {
        a.thresholdDb = -60 + 60 * u (rng); a.ratio = 1 + 19 * u (rng); a.kneeDb = 24 * u (rng); a.makeupDb = -12 + 36 * u (rng) * 0.3f;
        a.mix = u (rng); a.inputDb = -12 + 24 * u (rng); a.outputDb = -12 * u (rng); a.stereoLink = u (rng);
        g.e.setParams (a); Buf t (in.begin() + b, in.begin() + b + 128); float* tp = t.data(); g.e.process (&tp, 1, 128);
        std::copy (t.begin(), t.end(), out.begin() + b);
    }
    double maxStep = 0; for (size_t i = 1; i < out.size(); ++i) maxStep = std::max (maxStep, (double) std::fabs (out[i] - out[i - 1]));
    CHECK (allFinite (out) && maxStep < 0.4, "automação rápida sem clicks (salto %.3f)", maxStep);

    MeterModel mm; mm.configure (24, 1.0f, -60); mm.pushLinear (1.5f, 0.016f);
    CHECK (mm.isClipping () && mm.getDb() > 3.0f, "medidor: CLIP latch"); for (int i = 0; i < 100; ++i) mm.pushLinear (0.001f, 0.016f);
    CHECK (mm.getDb() < 3.0f - 20.0f && mm.getPeakDb() < mm.getDb() + 40, "medidor: queda balística");
}


// ============================== SUÍTE ADAPTATIVA (V2) ==============================
static Params baseA()
{
    Params p; p.adaptive = true; p.thresholdDb = -30.0f; p.ratio = 4.0f; p.kneeDb = 6.0f; p.attackMs = 5.0f; p.releaseMs = 150.0f;
    p.autoAttack = p.autoRelease = true;
    return p;
}
static Buf whiteNoise (int n, float amp, unsigned seed = 3)
{
    std::mt19937 g (seed); std::uniform_real_distribution<float> u (-1, 1); Buf b ((size_t) n);
    for (auto& v : b) v = amp * u (g);
    return b;
}
static Buf pinkNoise (int n, float amp, unsigned seed = 5)
{
    Buf w = whiteNoise (n, 1.0f, seed), b ((size_t) n); float b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i) { b0 = 0.99765f * b0 + w[(size_t) i] * 0.0990460f; b1 = 0.96300f * b1 + w[(size_t) i] * 0.2965164f; b2 = 0.57000f * b2 + w[(size_t) i] * 1.0526913f; b[(size_t) i] = amp * 0.11f * (b0 + b1 + b2 + w[(size_t) i] * 0.1848f); }
    return b;
}
static Buf vocalLike (int n, double sr)      // 200 Hz + harmônicos, envelope silábico ~4 Hz
{
    Buf b ((size_t) n);
    for (int i = 0; i < n; ++i) { const double t = i / sr, env = 0.5 + 0.5 * std::sin (2 * kPi * 4 * t - 1.2); double v = 0;
        for (int h = 1; h <= 5; ++h) v += std::sin (2 * kPi * 200 * h * t) / h; b[(size_t) i] = (float) (0.35 * env * env * v); }
    return b;
}
static Buf kickLike (int n, double sr)       // rajadas de 60 Hz com ataque brusco e decaimento ~80 ms, a cada 400 ms
{
    Buf b ((size_t) n, 0.0f); const int per = (int) (0.4 * sr);
    for (int s0 = (int) (0.1 * sr); s0 < n; s0 += per)
        for (int i = 0; i < (int) (0.5 * sr) && s0 + i < n; ++i) { const double t = i / sr; b[(size_t) (s0 + i)] += (float) (0.9 * std::exp (-t / 0.08) * std::sin (2 * kPi * 60 * t)); }
    return b;
}
static Buf bassLike (int n, double sr)
{
    Buf b ((size_t) n);
    for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (0.6 * std::sin (2 * kPi * 55 * t) + 0.2 * std::sin (2 * kPi * 110 * t)); }
    return b;
}
static double peakDb (const Buf& b, int from, int to) { float m = 0; for (int i = from; i < to; ++i) m = std::max (m, std::fabs (b[(size_t) i])); return 20.0 * std::log10 (std::max (m, 1e-9f)); }

static void testDetectorComponents()
{
    std::puts("[detector: transiente / graves / dual envelope]");
    const double sr = 48000;
    Detector d; d.prepare (sr); d.configure (0.25f, 2.0f, 0.0f, 0.0f);
    float tmax = 0, tSteady = 0; int cnt = 0;
    for (int i = 0; i < (int) sr; ++i) d.process ((float) (0.01 * std::sin (2 * kPi * 1000 * i / sr)));                 // sustain quieto
    for (int i = 0; i < 480; ++i) tmax = std::max (tmax, d.process ((float) (0.8 * std::sin (2 * kPi * 1000 * i / sr))).transient);   // onset forte
    for (int i = 0; i < (int) sr; ++i) { const auto r = d.process ((float) (0.8 * std::sin (2 * kPi * 1000 * i / sr))); if (i > 24000) { tSteady += r.transient; ++cnt; } }
    CHECK (tmax > 0.8f, "onset forte detectado (%.2f)", tmax);
    CHECK (tSteady / cnt < 0.1f, "seno estável não é transiente (%.3f)", tSteady / cnt);

    auto lvl = [&] (double f, float protect) { Detector x; x.prepare (sr); x.configure (0.25f, 2.0f, 0.0f, protect); double acc = 0; int c = 0;
        for (int i = 0; i < (int) sr; ++i) { const float l = x.process ((float) (0.5 * std::sin (2 * kPi * f * i / sr))).levelDb; if (i > 24000) { acc += l; ++c; } }
        return (float) (acc / c); };
    CHECK (lvl (60, 0.0f) - lvl (60, 0.7f) > 3.0f, "proteção de graves reduz o nível do detector em 60 Hz (%.1f dB)", lvl (60, 0.0f) - lvl (60, 0.7f));
    CHECK (std::fabs (lvl (2000, 0.0f) - lvl (2000, 0.7f)) < 0.5f, "proteção não mexe em 2 kHz");
    Detector x; x.prepare (sr); x.configure (0.25f, 2.0f, 0.0f, 0.0f); double lf = 0, rm = 0, su = 0, pkMax = 0; int c = 0;
    for (int i = 0; i < (int) sr; ++i) { const auto r = x.process ((float) (0.5 * std::sin (2 * kPi * 60 * i / sr))); if (i > 36000) { lf += r.lfRatio; rm += r.rmsLin; su += r.sustainLin; pkMax = std::max (pkMax, (double) r.peakLin); ++c; } }
    CHECK (lf / c > 0.6, "lfRatio alto para grave puro (%.2f)", lf / c);
    CHECK (pkMax > 0.45 && pkMax < 0.55 && std::fabs (su / c - 0.5 * 0.7071) < 0.03 && std::fabs (rm / c - 0.5 * 0.7071) < 0.03, "envelopes: pico %.2f, RMS %.2f, sustentado %.2f (esperado 0.5 / 0.35 / 0.35)", pkMax, rm / c, su / c);
}

static void testStrengthSweep()
{
    std::puts("[strength 0/25/50/75/100]");
    const double sr = 48000; const int n = 96000;
    Buf in = sine (1000, -8, n, sr); double prev = -1;
    double gr[5]; int i = 0;
    for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Params p = baseA(); p.strength = s; Rig r; r.setup (p); Buf o = r.run (in);
        gr[i] = rmsDb (in, n / 2, n) - rmsDb (o, n / 2, n);
        CHECK (gr[i] > prev - 1e-3 && allFinite (o), "GR cresce com STRENGTH: %.0f%% = %.2f dB", s * 100, gr[i]);
        prev = gr[i++];
    }
    CHECK (gr[0] < 0.05, "0%% transparente (%.3f dB)", gr[0]);
    CHECK (gr[1] > 0.5 && gr[1] < gr[2] - 1.0, "25%% = controle leve (%.2f dB) < 50%% (%.2f)", gr[1], gr[2]);
    CHECK (gr[4] > gr[2] + 3.0, "100%% bem mais agressivo que 50%% (%.2f vs %.2f)", gr[4], gr[2]);
    // não linear / suave: sem saltos grandes entre passos finos
    double last = 0; bool smooth = true;
    for (int k = 0; k <= 20; ++k) { Params p = baseA(); p.strength = k / 20.0f; Rig r; r.setup (p); Buf o = r.run (sine (1000, -8, 48000, sr)); const double g = rmsDb (in, 24000, 48000) - rmsDb (o, 24000, 48000); if (k > 0 && (g < last - 1e-3 || g - last > 5.0)) smooth = false; last = g; }
    CHECK (smooth, "curva de STRENGTH monotônica e sem degraus (>5 dB por 5%%)");
}

static void testProgramDependence()
{
    std::puts("[dependência do programa: transiente / release multi-estágio]");
    const double sr = 48000; const int n = 96000;
    Buf kick = kickLike (n, sr);
    Params p = baseA(); p.strength = 0.6f; p.thresholdDb = -24.0f; p.ratio = 8.0f;
    p.mode = Mode::Punch; Rig a; a.setup (p); Buf oP = a.run (kick);
    p.mode = Mode::Aggressive; Rig b; b.setup (p); Buf oA = b.run (kick);
    const double pkIn = peakDb (kick, 0, n), pkP = peakDb (oP, 0, n), pkA = peakDb (oA, 0, n);
    CHECK (pkP > pkA + 2.0, "PUNCH preserva o impacto: pico %.1f dB vs AGGRESSIVE %.1f dB (entrada %.1f)", pkP, pkA, pkIn);
    CHECK (pkP > pkIn - 6.0, "PUNCH não esmaga o transiente (perda %.1f dB)", pkIn - pkP);

    // release depende da duração do evento: evento curto recupera mais rápido que evento longo
    auto recov = [&] (double loudSec) {
        Params q = baseA(); q.strength = 0.7f; q.thresholdDb = -30; q.ratio = 8; q.releaseMs = 300; q.mode = Mode::Clean; Rig r; r.setup (q);
        Buf quiet = sine (1000, -60, 24000, sr); r.run (quiet);
        Buf loud = sine (1000, -6, (int) (loudSec * sr), sr); r.run (loud);
        std::vector<float> g; for (int i = 0; i < 96000; ++i) { float v = 0; float* pv = &v; r.e.process (&pv, 1, 1); g.push_back (r.e.getMeterData().grDb.load()); }
        const float lvl = 0.37f * g.front(); for (size_t i = 0; i < g.size(); ++i) if (g[i] <= lvl) return 1000.0 * (double) i / sr; return 1e9; };
    const double tShort = recov (0.03), tLong = recov (1.5);
    CHECK (tShort < 0.75 * tLong, "release: evento curto %.0f ms vs longo %.0f ms", tShort, tLong);

    // bass: BUS (HPF 60 Hz + proteção + energia sustentada) comprime muito menos um grave puro que CLEAN
    const int N = 96000; Buf bass = bassLike (N, sr);
    auto grBass = [&] (Mode m) { Params q = baseA(); q.strength = 0.6f; q.thresholdDb = -20; q.mode = m; Rig r; r.setup (q); Buf o = r.run (bass); return rmsDb (bass, N / 2, N) - rmsDb (o, N / 2, N); };
    const double gBus = grBass (Mode::Bus), gClean = grBass (Mode::Clean);
    CHECK (gBus < gClean - 2.0, "grave puro: BUS %.1f dB de GR vs CLEAN %.1f dB", gBus, gClean);
}

static void testCharacter()
{
    std::puts("[character: ligado à compressão]");
    const double sr = 48000; const int n = 96000; Buf in = sine (1000, -8, n, sr);
    for (SatMode m : { SatMode::Clean, SatMode::Tube, SatMode::Tape, SatMode::Modern })
    {
        Params p = baseA(); p.strength = 0.8f; p.satMode = m; p.character = 0.0f; Rig r; r.setup (p);
        Params q = p; q.character = 0.0f; q.satMode = SatMode::Clean; Rig r0; r0.setup (q);
        Buf a = r.run (in), b = r0.run (in); double e = 0; for (size_t i = 0; i < a.size(); ++i) e = std::max (e, (double) std::fabs (a[i] - b[i]));
        CHECK (e < 1e-6, "Character 0%% transparente no modo %d (dif %.2e)", (int) m, e);
        p.character = 1.0f; Rig r1; r1.setup (p); Buf c = r1.run (in);
        const double h2 = goertzelDb (c, n / 2, n, 2000, sr), h3 = goertzelDb (c, n / 2, n, 3000, sr), h5 = goertzelDb (c, n / 2, n, 5000, sr), h0 = goertzelDb (a, n / 2, n, 3000, sr);
        CHECK (allFinite (c) && std::max ({ h2, h3, h5 }) > h0 + 6.0, "Character 100%% gera harmônicos no modo %d (2ª %.1f, 3ª %.1f, 5ª %.1f dB)", (int) m, h2, h3, h5);
        if (m == SatMode::Tube) CHECK (h2 > h3, "TUBE: 2ª harmônica (assimétrica) > 3ª (%.1f vs %.1f)", h2, h3);
        if (m == SatMode::Modern) CHECK (h5 > goertzelDb (a, n / 2, n, 5000, sr) + 6.0, "MODERN: joelho fechado -> 5ª harmônica nasce (%.1f dB)", h5);
        if (m == SatMode::Tape) CHECK (h3 > h2 + 10.0, "modo %d simétrico: 3ª domina (%.1f vs %.1f)", (int) m, h3, h2);
    }
    // caráter cresce com a GR
    Params p = baseA(); p.satMode = SatMode::Tape; p.character = 1.0f;
    auto harm = [&] (float th) { p.thresholdDb = th; p.strength = 0.5f; Rig r; r.setup (p); Buf o = r.run (in); return goertzelDb (o, n / 2, n, 3000, sr) - goertzelDb (o, n / 2, n, 1000, sr); };
    CHECK (harm (-30) > harm (0) + 3.0, "mais compressão -> mais caráter (%.1f vs %.1f dB)", harm (-30), harm (0));
}

static void testAutoGain()
{
    std::puts("[auto gain por energia]");
    const double sr = 48000; const int n = 48000 * 6;
    for (float lvl : { -30.0f, -12.0f })
    {
        Params p = baseA(); p.strength = 0.6f; p.ratio = 4; p.thresholdDb = -30; p.autoGain = true; Rig r; r.setup (p);
        Buf in = pinkNoise (n, std::pow (10.0f, lvl / 20.0f) * 3.0f), o = r.run (in);
        const double dIn = rmsDb (in, n * 3 / 4, n), dOut = rmsDb (o, n * 3 / 4, n);
        Params q = p; q.autoGain = false; Rig r2; r2.setup (q); Buf o2 = r2.run (in);
        const double dOff = rmsDb (o2, n * 3 / 4, n);
        CHECK (std::fabs (dOut - dIn) < std::fabs (dOff - dIn) * 0.45 + 0.3, "auto gain aproxima o volume: in %.1f, off %.1f, on %.1f dB", dIn, dOff, dOut);
    }
    Params p = baseA(); p.autoGain = true; p.strength = 1.0f; Rig r; r.setup (p);
    Buf sil (n, 0.0f); Buf o = r.run (sil); double mx = 0; for (float v : o) mx = std::max (mx, (double) std::fabs (v));
    CHECK (mx == 0.0, "silêncio -> silêncio");
    Buf quiet = whiteNoise (n, 1e-5f); r.setup (p); o = r.run (quiet);
    CHECK (rmsDb (o, n / 2, n) < rmsDb (quiet, n / 2, n) + 0.5, "ruído -100 dB não é amplificado (%.1f vs %.1f)", rmsDb (o, n / 2, n), rmsDb (quiet, n / 2, n));
    // tom ligado/desligado: depois do silêncio o ganho não explode
    Buf burst (n, 0.0f); Buf t = sine (1000, -50, 12000, sr); std::copy (t.begin(), t.end(), burst.begin() + 24000);
    r.setup (p); o = r.run (burst); CHECK (allFinite (o) && peakDb (o, 0, n) < -50 + 20.0, "auto gain limitado em sinais fracos");
}

static void testTransparencyAndStability()
{
    std::puts("[transparência / estabilidade / sinais]");
    const double sr = 48000; const int n = 48000;
    Params p = baseA(); p.strength = 0.0f; p.character = 0.0f; p.mix = 1.0f; p.lookaheadMs = 1.0f;
    Rig r; r.setup (p);
    Buf in = pinkNoise (n, 0.5f), o = r.run (in); const int lat = r.e.getLatencySamples(); double num = 0, den = 0;
    for (int i = lat; i < n; ++i) { const double d = o[(size_t) i] - in[(size_t) (i - lat)]; num += d * d; den += (double) in[(size_t) (i - lat)] * in[(size_t) (i - lat)]; }
    CHECK (10 * std::log10 (num / den + 1e-30) < -100.0, "STRENGTH 0 + CHARACTER 0 vs bypass: diferença %.1f dB", 10 * std::log10 (num / den + 1e-30));
    Buf tones (n); for (int i = 0; i < n; ++i) tones[(size_t) i] = (float) (0.2 * std::sin (2 * kPi * 500 * i / sr) + 0.2 * std::sin (2 * kPi * 3000 * i / sr) + 0.2 * std::sin (2 * kPi * 9000 * i / sr));
    p.oversampling = 4; r.setup (p); o = r.run (tones); const int l4 = r.e.getLatencySamples(); num = den = 0;
    for (int i = l4 + 2000; i < n; ++i) { const double d = o[(size_t) i] - tones[(size_t) (i - l4)]; num += d * d; den += (double) tones[(size_t) (i - l4)] * tones[(size_t) (i - l4)]; }
    CHECK (10 * std::log10 (num / den) < -50.0, "mesma coisa com OS 4x, tons até 9 kHz (filtro de fase linear): %.1f dB", 10 * std::log10 (num / den));

    // varredura de níveis x strength x modos x sinais
    const float levels[] = { -120, -100, -80, -60, -20, 0, 6, 12 };
    int bad = 0; double maxOut = 0;
    for (int sig = 0; sig < 6; ++sig)
        for (float L : levels)
            for (Mode m : { Mode::Clean, Mode::Punch, Mode::Smooth, Mode::Aggressive, Mode::Bus })
                for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    Params q = baseA(); q.mode = m; q.strength = s; q.character = 0.7f; q.satMode = SatMode::Tape; q.autoGain = true; q.oversampling = (sig % 2) ? 2 : 1;
                    Buf x = sig == 0 ? sine (1000, L, 12000, sr) : sig == 1 ? whiteNoise (12000, std::pow (10.0f, L / 20.0f)) : sig == 2 ? pinkNoise (12000, std::pow (10.0f, L / 20.0f))
                          : sig == 3 ? vocalLike (12000, sr) : sig == 4 ? kickLike (12000, sr) : bassLike (12000, sr);
                    if (sig >= 3) for (auto& v : x) v *= std::pow (10.0f, L / 20.0f);
                    Rig g; g.setup (q); Buf y = g.run (x);
                    if (! allFinite (y)) ++bad;
                    for (float v : y) maxOut = std::max (maxOut, (double) std::fabs (v));
                }
    CHECK (bad == 0, "%d combinações com NaN/Inf", bad);
    CHECK (maxOut < 60.0, "saída limitada (pico máx %.1f)", maxOut);

    // ratio e tempos extremos
    for (float ratio : { 1.0f, 1000.0f })
        for (float att : { 0.05f, 100.0f })
            for (float rel : { 10.0f, 2000.0f })
            { Params q = baseA(); q.ratio = ratio; q.attackMs = att; q.releaseMs = rel; q.strength = 0.9f; Rig g; g.setup (q); Buf y = g.run (pinkNoise (24000, 0.7f)); CHECK (allFinite (y), "extremos ratio %.0f att %.2f rel %.0f", ratio, att, rel); }
    // ratio 1:1 sem compressão (STRENGTH moderado)
    { Params q = baseA(); q.ratio = 1.0f; q.strength = 0.5f; q.autoAttack = q.autoRelease = false; Rig g; g.setup (q); Buf x = sine (1000, -6, 48000, sr), y = g.run (x);
      CHECK (std::fabs (rmsDb (x, 24000, 48000) - rmsDb (y, 24000, 48000)) < 0.1, "ratio 1:1 não comprime"); }
}

static void testLookaheadStereoMs()
{
    std::puts("[lookahead / consistência de canais / M/S adaptativo]");
    const double sr = 48000; const int n = 48000;
    for (float la : { 0.5f, 1.0f, 2.0f, 5.0f })
    {
        Params p = baseA(); p.lookaheadMs = la; p.strength = 0.0f; Rig r; r.setup (p);
        Buf imp (4096, 0.0f); imp[300] = 0.5f; Buf o = r.run (imp);
        CHECK (std::fabs (o[(size_t) (300 + r.e.getLatencySamples())] - 0.5f) < 1e-6f, "lookahead %.1f ms: impulso na latência declarada (%d)", la, r.e.getLatencySamples());
    }
    Params p = baseA(); p.lookaheadMs = 5.0f; p.stereoLink = 0.0f; p.strength = 0.8f; Buf l = pinkNoise (n, 0.5f), r2 = l;
    Rig g; g.setup (p); float* ch[2]; double diff = 0;
    for (int i = 0; i + 256 <= n; i += 256) { ch[0] = l.data() + i; ch[1] = r2.data() + i; g.e.process (ch, 2, 256); }
    for (int i = 0; i < n - 256; ++i) diff = std::max (diff, (double) std::fabs (l[(size_t) i] - r2[(size_t) i]));
    CHECK (diff == 0.0, "L==R em entrada idêntica, link 0%%, lookahead 5 ms: diferença %.2e", diff);
    // M/S com adaptativo: ida e volta transparente com strength 0
    Buf a = sine (700, -6, n, sr), b = sine (1300, -9, n, sr), a0 = a, b0 = b;
    p = baseA(); p.strength = 0.0f; p.stereoMode = StereoMode::Mid; Rig m; m.setup (p);
    for (int i = 0; i + 256 <= n; i += 256) { ch[0] = a.data() + i; ch[1] = b.data() + i; m.e.process (ch, 2, 256); }
    double e = 0; for (int i = 0; i < n - 256; ++i) e = std::max ({ e, (double) std::fabs (a[(size_t) i] - a0[(size_t) i]), (double) std::fabs (b[(size_t) i] - b0[(size_t) i]) });
    CHECK (e < 1e-6, "M/S ida e volta (adaptativo): erro %.2e", e);
}

// ============================== SUÍTE V3 ==============================
static double meanGrDb (Params p, const Buf& x, double sr = 48000.0)       // GR médio real do motor (medidor), blocos de 64
{
    Rig r (sr); r.setup (p); double acc = 0; int cnt = 0; const int B = 64;
    Buf y = x;
    for (int i = 0; i + B <= (int) y.size(); i += B)
    {
        float* ptr = y.data() + i; r.e.process (&ptr, 1, B);
        if (i > (int) y.size() / 4) { acc += r.e.getMeterData().grDb.load(); ++cnt; }
    }
    return acc / std::max (cnt, 1);
}
static Buf drumLike (int n, double sr)       // kick (60 Hz) + snare-ish (ruído em banda) + hat
{
    Buf b = kickLike (n, sr), w = whiteNoise (n, 1.0f, 11); const int per = (int) (0.4 * sr);
    for (int s0 = (int) (0.3 * sr); s0 < n; s0 += per)
        for (int i = 0; i < (int) (0.12 * sr) && s0 + i < n; ++i) b[(size_t) (s0 + i)] += 0.5f * std::exp (-(float) i / (0.03f * (float) sr)) * w[(size_t) (s0 + i)];
    return b;
}
static Buf transientHeavy (int n, double sr) // cliques curtos (5 ms) a cada 150 ms sobre silêncio
{
    Buf b ((size_t) n, 0.0f), w = whiteNoise (n, 1.0f, 17); const int per = (int) (0.15 * sr);
    for (int s0 = 1000; s0 < n; s0 += per) for (int i = 0; i < (int) (0.005 * sr) && s0 + i < n; ++i) b[(size_t) (s0 + i)] = 0.8f * w[(size_t) (s0 + i)] * (1.0f - (float) i / (0.005f * (float) sr));
    return b;
}
static Buf sustained (int n, double sr)
{
    Buf b ((size_t) n);
    for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (0.25 * std::sin (2 * kPi * 220 * t) + 0.2 * std::sin (2 * kPi * 330 * t + 1) + 0.15 * std::sin (2 * kPi * 495 * t + 2)); }
    return b;
}
static Buf mixLike (int n, double sr)
{
    Buf a = pinkNoise (n, 0.25f, 23), d = drumLike (n, sr), v = vocalLike (n, sr), b = bassLike (n, sr), m ((size_t) n);
    for (int i = 0; i < n; ++i) m[(size_t) i] = 0.5f * (a[(size_t) i] + d[(size_t) i] + v[(size_t) i] + 0.5f * b[(size_t) i]);
    return m;
}

static void testStrengthIntentStruct()
{
    std::puts("[V3: StrengthIntent]");
    StrengthIntent prev = computeStrengthIntent (0.0f); bool mono = true;
    for (int k = 1; k <= 100; ++k)
    {
        const StrengthIntent i = computeStrengthIntent (k / 100.0f);
        if (i.compressionDepth < prev.compressionDepth || i.detectorSensitivity < prev.detectorSensitivity || i.sustainWeight < prev.sustainWeight
            || i.densityResponse < prev.densityResponse || i.characterCoupling < prev.characterCoupling || i.thresholdDeepenDb < prev.thresholdDeepenDb
            || i.transientPreservation > prev.transientPreservation || i.attackAdaptation > prev.attackAdaptation) mono = false;
        prev = i;
    }
    CHECK (mono, "campos da intenção evoluem de forma contínua e monotônica");
    const StrengthIntent z = computeStrengthIntent (0.0f), h = computeStrengthIntent (0.5f), f = computeStrengthIntent (1.0f);
    CHECK (z.compressionDepth == 0.0f && z.characterCoupling == 0.0f, "0%% = PRESERVAR");
    CHECK (h.compressionDepth == 1.0f && h.thresholdDeepenDb == 0.0f && h.ratioPush == 0.0f, "50%% = THRESHOLD/RATIO do usuário valem como ajustados");
    CHECK (f.thresholdDeepenDb <= 6.0f && f.attackAdaptation >= 0.7f - 1e-5f && f.characterCoupling == 1.0f, "100%% = deslocamentos limitados (thr %.1f dB, attack x%.2f)", f.thresholdDeepenDb, f.attackAdaptation);
}

static void testStrengthMaterials()
{
    std::puts("[V3: STRENGTH 0/25/50/75/100 em 6 materiais]");
    const double sr = 48000; const int n = 96000;
    struct M { const char* name; Buf x; Mode mode; } mats[] = {
        { "vocal-like", vocalLike (n, sr), Mode::Smooth }, { "bass-like", bassLike (n, sr), Mode::Clean }, { "drum-like", drumLike (n, sr), Mode::Punch },
        { "transient-heavy", transientHeavy (n, sr), Mode::Punch }, { "sustained", sustained (n, sr), Mode::Clean }, { "mix-like", mixLike (n, sr), Mode::Bus } };
    for (auto& m : mats)
    {
        double g[5]; int i = 0;
        for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            Params p = baseA(); p.mode = m.mode; p.strength = s; p.thresholdDb = -24.0f; p.ratio = 4.0f; g[i++] = meanGrDb (p, m.x);
        }
        CHECK (g[0] < 0.05, "%s: STRENGTH 0%% preserva (GR %.3f dB)", m.name, g[0]);
        bool mono = true; for (int k = 1; k < 5; ++k) if (g[k] < g[k - 1] - 0.2) mono = false;
        CHECK (mono, "%s: GR média cresce 0/25/50/75/100 = %.2f %.2f %.2f %.2f %.2f dB", m.name, g[0], g[1], g[2], g[3], g[4]);
        CHECK (g[2] >= g[1] && g[3] >= g[2] - 0.2 && g[4] >= g[3] - 0.2, "%s: 75%% nunca com menos intenção que 50%%", m.name);
    }
}

static void testRatioAndLimiting()
{
    std::puts("[V3: ratio 1:1 / infinito]");
    const double sr = 48000; const int n = 96000; Buf in = sine (1000, -6, n, sr);
    for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        for (Mode m : { Mode::Clean, Mode::Punch, Mode::Smooth, Mode::Aggressive, Mode::Bus })
        { Params p = baseA(); p.ratio = 1.0f; p.strength = s; p.mode = m; p.thresholdDb = -40.0f; Rig r; r.setup (p); Buf o = r.run (in);
          CHECK (std::fabs (rmsDb (in, n / 2, n) - rmsDb (o, n / 2, n)) < 0.1, "1:1 sem compressão (strength %.0f%%, modo %d)", s * 100, (int) m); }
    for (float s : { 0.5f, 1.0f })
    { Params p = baseA(); p.ratio = 1000.0f; p.strength = s; p.thresholdDb = -20.0f; p.attackMs = 0.05f; p.kneeDb = 0.0f; p.lookaheadMs = 2.0f; p.autoAttack = p.autoRelease = false;
      Rig r; r.setup (p); Buf o = r.run (in);
      CHECK (peakDb (o, n / 2, n) < -20.0 + 2.5, "inf:1 limita: pico %.1f dB com threshold -20 dB (strength %.0f%%)", peakDb (o, n / 2, n), s * 100); }
    { Params p = baseA(); p.ratio = 1000.0f; p.strength = 0.5f; p.thresholdDb = -20.0f; p.kneeDb = 0; p.autoAttack = p.autoRelease = false; p.attackMs = 0.05f; p.lookaheadMs = 2.0f;
      Buf burst = sine (1000, 0.0, n, sr); Rig r; r.setup (p); Buf o = r.run (burst);
      CHECK (peakDb (o, n / 2, n) < -20.0 + 2.5 && allFinite (o), "inf:1 limita também sinal em 0 dBFS (%.1f dB)", peakDb (o, n / 2, n)); }
}

static void testTransientAndSustain()
{
    std::puts("[V3: silêncio -> transiente -> sustain]");
    const double sr = 48000; const int n = 48000;
    Buf in ((size_t) n, 0.0f); const int t0 = 9600;                     // 200 ms de silêncio
    for (int i = 0; i < (int) (0.004 * sr); ++i) in[(size_t) (t0 + i)] = (float) (0.7 * std::sin (2 * kPi * 120 * i / sr) * std::exp (-i / (0.002 * sr)) + 0.2 * std::sin (2 * kPi * 3000 * i / sr) * std::exp (-i / (0.001 * sr)));
    for (int i = 0; i < n - t0; ++i) in[(size_t) (t0 + i)] += (float) (0.3 * std::sin (2 * kPi * 220 * i / sr) * (1.0 - std::exp (-i / (0.002 * sr))));   // sustain a -10 dB
    const int tr0 = t0, tr1 = t0 + (int) (0.012 * sr), s0 = t0 + (int) (0.15 * sr), s1 = n;
    double loss[5]; int k = 0;
    for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Params p = baseA(); p.mode = Mode::Punch; p.strength = s; p.thresholdDb = -24.0f; p.ratio = 4.0f; Rig r; r.setup (p); Buf o = r.run (in);
        loss[k++] = peakDb (in, tr0, tr1) - peakDb (o, tr0, tr1);
        if (s == 0.5f)
        {
            const double gTail = rmsDb (in, s0, s1) - rmsDb (o, s0, s1);
            CHECK (loss[k - 1] < 6.0, "50%% PUNCH: ataque preservado (perda %.1f dB)", loss[k - 1]);
            CHECK (gTail > 3.0 && gTail > loss[k - 1] + 1.0, "50%%: sustain controlado (%.1f dB) mais que o ataque (%.1f dB)", gTail, loss[k - 1]);
        }
        if (s == 0.75f) { const double gTail = rmsDb (in, s0, s1) - rmsDb (o, s0, s1); CHECK (gTail > 4.0 && gTail > loss[k - 1] + 0.5, "75%%: corpo densificado (%.1f dB) sem destruir o ataque (perda %.1f dB)", gTail, loss[k - 1]); }
    }
    CHECK (loss[0] < 0.1, "0%% preserva o transiente (perda %.2f dB)", loss[0]);
    CHECK (loss[1] <= loss[3] + 0.3 && loss[3] <= loss[4] + 0.3, "perda de transiente cresce com STRENGTH: %.1f %.1f %.1f %.1f %.1f dB", loss[0], loss[1], loss[2], loss[3], loss[4]);
    // PUNCH preserva mais que AGGRESSIVE no mesmo STRENGTH
    Params p = baseA(); p.strength = 0.5f; p.thresholdDb = -24.0f; p.mode = Mode::Punch; Rig a; a.setup (p); const double lp = peakDb (in, tr0, tr1) - peakDb (a.run (in), tr0, tr1);
    p.mode = Mode::Aggressive; Rig b; b.setup (p); const double la = peakDb (in, tr0, tr1) - peakDb (b.run (in), tr0, tr1);
    CHECK (lp < la - 1.0, "PUNCH preserva mais impacto que AGGRESSIVE (%.1f vs %.1f dB de perda)", lp, la);
}

static void testCharacterMatrix()
{
    std::puts("[V3: character x strength]");
    const double sr = 48000; const int n = 96000; Buf in = pinkNoise (n, 0.9f);
    for (SatMode m : { SatMode::Clean, SatMode::Tube, SatMode::Tape, SatMode::Modern })
    {
        auto run = [&] (float s, float c, float gainDb, int os) { Params p = baseA(); p.strength = s; p.character = c; p.satMode = m; p.inputDb = gainDb; p.oversampling = os; p.lookaheadMs = 1.0f; Rig r; r.setup (p); Buf o = r.run (in); return std::make_pair (o, r.e.getLatencySamples()); };
        auto diffDb = [&] (const std::pair<Buf, int>& r) { double d = 0, e = 0; for (int i = r.second + 4000; i < n; ++i) { const double x = in[(size_t) (i - r.second)]; d += (r.first[(size_t) i] - x) * (r.first[(size_t) i] - x); e += x * x; } return 10 * std::log10 (d / e + 1e-30); };
        CHECK (diffDb (run (0.0f, 0.0f, 0, 1)) < -100.0, "modo %d: S0/C0 = bypass (%.1f dB)", (int) m, diffDb (run (0.0f, 0.0f, 0, 1)));
        CHECK (diffDb (run (0.0f, 1.0f, 0, 1)) < -100.0, "modo %d: S0/C100 transparente (%.1f dB)", (int) m, diffDb (run (0.0f, 1.0f, 0, 1)));
        for (float s : { 0.5f, 1.0f })
            for (float g : { 0.0f, 12.0f })
            { auto r = run (s, 1.0f, g, 4); double mx = 0, mean = 0; for (int i = 48000; i < n; ++i) { mx = std::max (mx, (double) std::fabs (r.first[(size_t) i])); mean += r.first[(size_t) i]; } mean /= 48000;
              CHECK (allFinite (r.first) && mx < 8.0 && std::fabs (mean) < 0.02, "modo %d S%.0f/C100 +%.0f dB: pico %.2f, DC %.4f", (int) m, s * 100, g, mx, mean); }
    }
    // Character só aparece com compressão: sinal abaixo do threshold => igual a Character 0
    Params p = baseA(); p.strength = 1.0f; p.thresholdDb = 0.0f; p.satMode = SatMode::Tape; p.character = 1.0f; Rig r; r.setup (p); Buf q = sine (1000, -30, 48000, sr), o = r.run (q);
    Params p0 = p; p0.character = 0.0f; Rig r0; r0.setup (p0); Buf o0 = r0.run (q); double e = 0; for (size_t i = 0; i < o.size(); ++i) e = std::max (e, (double) std::fabs (o[i] - o0[i]));
    CHECK (e < 1e-5, "sem GR não há caráter excessivo (dif %.2e)", e);
}

static void testAutomationNoJumps()
{
    std::puts("[V3: automação de STRENGTH sem clicks / jumps de GR]");
    const double sr = 48000; const int n = 96000; Buf in = sine (440, -8, n, sr);
    for (int block : { 1, 64 })
    {
        Params p = baseA(); p.thresholdDb = -30; Rig r; r.setup (p); double maxStep = 0, maxGrStep = 0, prevGr = 0, prevO = 0; bool first = true;
        for (int i = 0; i + block <= n; i += block)
        {
            p.strength = (float) i / n; r.e.setParams (p);
            Buf t (in.begin() + i, in.begin() + i + block); float* tp = t.data(); r.e.process (&tp, 1, block);
            for (float v : t) { if (! first) maxStep = std::max (maxStep, (double) std::fabs (v - prevO)); prevO = v; first = false; }
            const double gr = r.e.getMeterData().grDb.load(); if (i > 4800) maxGrStep = std::max (maxGrStep, std::fabs (gr - prevGr) / block); prevGr = gr;
        }
        CHECK (maxStep < 0.12, "bloco %d: sem clicks (salto %.3f)", block, maxStep);
        CHECK (maxGrStep < 0.02, "bloco %d: GR sem jumps (%.4f dB/amostra)", block, maxGrStep);
    }
}

static void testBlockSizesAndRates()
{
    std::puts("[V3: block sizes / sample rates / níveis]");
    const double sr = 48000;
    Params p = baseA(); p.strength = 0.7f; p.character = 0.5f; p.satMode = SatMode::Tube; p.oversampling = 2; p.lookaheadMs = 2.0f; p.autoGain = true; p.mode = Mode::Smooth;
    Buf x = mixLike (24000, sr); Rig ref; ref.setup (p); Buf y0 = ref.run (x, 24000);
    for (int bs : { 1, 16, 32, 64, 128, 256, 512, 1024 })
    { Rig g; g.setup (p); Buf y = g.run (x, bs); double e = 0; for (size_t i = 0; i < y.size(); ++i) e = std::max (e, (double) std::fabs (y[i] - y0[i])); CHECK (e < 1e-6, "bloco %d idêntico ao bloco único (%.2e)", bs, e); }
    // taxa de amostragem: mesmo comportamento (GR médio) em todas as taxas
    double ref48 = 0;
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Params q = baseA(); q.strength = 0.7f; q.thresholdDb = -24; Buf s2 = vocalLike ((int) (rate * 1.5), rate);
        const double g = meanGrDb (q, s2, rate); if (rate == 48000.0) ref48 = g;
        Rig r (rate); r.setup (q); Buf o = r.run (s2); CHECK (allFinite (o), "%.0f Hz finito", rate);
        if (ref48 > 0) CHECK (std::fabs (g - ref48) < 1.2, "GR média a %.0f Hz = %.2f dB (48k: %.2f)", rate, g, ref48);
    }
    // níveis x strength x modos x caráter x OS, com +6 dB incluso
    int bad = 0; const float levels[] = { -120, -100, -80, -60, -20, 0, 6 };
    for (float L : levels) for (Mode m : { Mode::Clean, Mode::Punch, Mode::Smooth, Mode::Aggressive, Mode::Bus }) for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    { Params q = baseA(); q.mode = m; q.strength = s; q.character = 1.0f; q.satMode = SatMode::Tube; q.autoGain = true; q.oversampling = 8; q.stereoMode = StereoMode::Mid;
      Buf a = mixLike (6000, sr), b = a; for (auto& v : a) v *= std::pow (10.0f, L / 20.0f) * 2; for (size_t i = 0; i < b.size(); ++i) b[i] = -a[i] * 0.5f;
      Rig g; g.setup (q); float* ch[2] = { a.data(), b.data() }; g.e.process (ch, 2, 6000); if (! allFinite (a) || ! allFinite (b)) ++bad; }
    CHECK (bad == 0, "%d combinações com NaN/Inf (estéreo M/S, OS 8x, Character 100%%)", bad);
}

static void testStereoLinkSmooth()
{
    std::puts("[V3: stereo link interpolado]");
    const double sr = 48000; const int n = 48000; Buf L = sine (1000, -6, n, sr), R = sine (1000, -30, n, sr); double prev = -1;
    for (float link : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    { Params p = baseA(); p.stereoLink = link; p.thresholdDb = -28; Rig g; g.setup (p); Buf l = L, r = R; float* ch[2] = { l.data(), r.data() }; for (int i = 0; i + 256 <= n; i += 256) { float* c[2] = { ch[0] + i, ch[1] + i }; g.e.process (c, 2, 256); }
      const double grR = rmsDb (R, n / 2, n - 256) - rmsDb (r, n / 2, n - 256); CHECK (grR >= prev - 0.05, "link %.0f%%: GR do canal quieto %.2f dB (cresce com o link)", link * 100, grR); prev = grR; }
}

static void testAutoGainV3()
{
    std::puts("[V3: auto gain estável]");
    const double sr = 48000; const int n = 48000 * 8;
    Params p = baseA(); p.autoGain = true; p.strength = 1.0f; p.thresholdDb = -40; Rig r; r.setup (p);
    // sinal fraco -> sem boost explosivo; ruído quase silencioso; silêncio longo
    for (float L : { -110.0f, -90.0f, -70.0f })
    { Buf q = whiteNoise (n, std::pow (10.0f, L / 20.0f)); r.setup (p); Buf o = r.run (q); CHECK (rmsDb (o, n / 2, n) < rmsDb (q, n / 2, n) + 1.0, "ruído %.0f dB não é amplificado (%.1f vs %.1f dB)", L, rmsDb (o, n / 2, n), rmsDb (q, n / 2, n)); }
    // passagem silêncio -> música -> silêncio: ganho nunca passa dos limites
    Buf mus = mixLike (n, sr); for (int i = 0; i < 48000; ++i) mus[(size_t) i] = 0; for (int i = n - 96000; i < n; ++i) mus[(size_t) i] = 0;
    r.setup (p); Buf o = r.run (mus); double mx = 0; for (float v : o) mx = std::max (mx, (double) std::fabs (v));
    CHECK (allFinite (o) && mx < 8.0, "sem runaway gain (pico %.2f)", mx);
    for (int i = n - 48000; i < n; ++i) if (o[(size_t) i] != 0.0f) { CHECK (false, "silêncio final deve ser exatamente zero"); break; }
    // variação máxima de ganho por segundo (sem saltos de nível): compara energia por blocos de 50 ms
    Buf s2 = sustained (n, sr); for (auto& v : s2) v *= 0.3f; r.setup (p); o = r.run (s2); double worst = 0; double prevL = 0; bool f = true;
    for (int i = 48000; i + 2400 <= n; i += 2400) { const double l = rmsDb (o, i, i + 2400) - rmsDb (s2, i, i + 2400); if (! f) worst = std::max (worst, std::fabs (l - prevL)); prevL = l; f = false; }
    CHECK (worst < 1.0, "auto gain (após 1 s de ataque do compressor) varia no máximo %.2f dB a cada 50 ms", worst);
}

static void testRealTimeSafety()
{
    std::puts("[V3: real-time safety: zero alocações no audio thread]");
    const double sr = 48000;
    Params p = baseA(); p.character = 1.0f; p.satMode = SatMode::Tape; p.oversampling = 8; p.autoGain = true; p.lookaheadMs = 5.0f; p.stereoMode = StereoMode::Mid;
    Rig r; r.setup (p); Buf a = mixLike (48000, sr), b = a; float* ch[2] = { a.data(), b.data() };
    const long before = g_allocs.load();
    for (int i = 0; i + 128 <= 48000; i += 128)
    {
        p.strength = (float) i / 48000; p.mode = (Mode) ((i / 128) % 5); p.oversampling = (i / 128 % 4 == 0) ? 2 : 8; p.bypass = (i / 128) % 50 == 49;
        r.e.setParams (p); float* c[2] = { ch[0] + i, ch[1] + i }; r.e.process (c, 2, 128);
    }
    CHECK (g_allocs.load() == before, "%ld alocações durante setParams/process", g_allocs.load() - before);
}

int main()
{
    testStaticCurve(); testThresholdRatio(); testAttackRelease(); testMakeupMixBypass();
    testSidechain(); testOversampling(); testStereoAndMs(); testRobustness();
    testDetectorComponents(); testStrengthSweep(); testProgramDependence(); testCharacter(); testAutoGain(); testTransparencyAndStability(); testLookaheadStereoMs();
    testStrengthIntentStruct(); testStrengthMaterials(); testRatioAndLimiting(); testTransientAndSustain(); testCharacterMatrix(); testAutomationNoJumps(); testBlockSizesAndRates(); testStereoLinkSmooth(); testAutoGainV3(); testRealTimeSafety();
    std::printf ("\n%d verificações, %d falhas\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
