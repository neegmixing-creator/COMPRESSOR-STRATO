#include "Detector.h"

namespace cmp
{
void Detector::prepare (double sampleRate)
{
    sr = sampleRate;
    dcR = 1.0 - 2.0 * kPi * 8.0 / sr;
    rmsCoef   = timeToCoef (15.0f, sr);                          // envelope lento "curto": ~15 ms
    sustCoef  = timeToCoef (150.0f, sr);                         // energia sustentada
    lfCoef    = 1.0f - (float) std::exp (-2.0 * kPi * 120.0 / sr);
    eCoef     = 1.0f - timeToCoef (30.0f, sr);
    fastCoef  = 1.0f - timeToCoef (2.0f, sr);
    slowCoef  = 1.0f - timeToCoef (40.0f, sr);
    transRel  = timeToCoef (40.0f, sr);
    hpfHz = -1.0f; peakMs = -1.0f;
    reset();
}

void Detector::reset() noexcept
{
    dcX1 = dcY1 = 0.0;
    hpf.reset();
    peakEnv = meanSq = sustainSq = 0.0f;
    lpState = lfE = totE = fastE = slowE = transient = 0.0f;
}

void Detector::configure (float newRmsMix, float peakDecayMs, float newHpfHz, float newLfProtect) noexcept
{
    rmsMix = std::clamp (newRmsMix, 0.0f, 1.0f);
    lfProtect = std::clamp (newLfProtect, 0.0f, 1.0f);
    if (peakDecayMs != peakMs) { peakMs = peakDecayMs; peakDecay = timeToCoef (peakDecayMs, sr); }
    if (newHpfHz != hpfHz)
    {
        hpfHz = newHpfHz;
        hpfOn = hpfHz >= 10.0f;
        if (hpfOn) hpf.setHighpass (sr, hpfHz);
        else hpf.reset();
    }
}

Detector::Result Detector::process (float in) noexcept
{
    // 1) Bloqueio de DC (8 Hz) e HPF de sidechain do usuário
    const double yd = (double) in - dcX1 + dcR * dcY1;
    dcX1 = in;
    dcY1 = flushDenorm (yd);
    float x = (float) yd;
    if (hpfOn) x = hpf.process (x);

    // 2) Detector de baixa frequência + proteção adaptativa:
    //    só remove graves do detector quando eles DOMINAM a energia (vocal/agudos ficam intactos).
    lpState = flushDenorm (lpState + lfCoef * (x - lpState));
    lfE  = flushDenorm (lfE  + (lpState * lpState - lfE) * eCoef);
    totE = flushDenorm (totE + (x * x - totE) * eCoef);
    const float lfRatio = totE > 1.0e-12f ? std::clamp (lfE / totE, 0.0f, 1.0f) : 0.0f;
    if (lfProtect > 0.0f) x -= lfProtect * lfRatio * lpState;

    // 3) Envelopes: rápido (pico), lento (RMS ~15 ms) e sustentado (RMS ~150 ms)
    const float a = std::fabs (x);
    peakEnv   = flushDenorm (std::max (a, peakEnv * peakDecay));
    meanSq    = flushDenorm (rmsCoef  * meanSq    + (1.0f - rmsCoef)  * a * a);
    sustainSq = flushDenorm (sustCoef * sustainSq + (1.0f - sustCoef) * a * a);
    const float rms = std::sqrt (meanSq);

    // 4) Detector de transientes: energia de 2 ms vs. energia de 40 ms (sem logaritmos).
    //    Ataque instantâneo, liberação de 40 ms -> o "evento" cobre toda a região do ataque.
    fastE = flushDenorm (fastE + (x * x - fastE) * fastCoef);
    slowE = flushDenorm (slowE + (x * x - slowE) * slowCoef);
    const float onset = slowE > 1.0e-10f ? std::clamp ((fastE / slowE - 3.0f) / 12.0f, 0.0f, 1.0f) : 0.0f;
    transient = onset > transient ? onset : flushDenorm (transient * transRel);

    Result r;
    const float level = (1.0f - rmsMix) * peakEnv + rmsMix * rms;
    r.levelDb = linToDb (level);
    r.crestDb = rms > 1.0e-5f ? linToDb (peakEnv) - linToDb (rms) : 0.0f;
    r.peakLin = peakEnv; r.rmsLin = rms; r.sustainLin = std::sqrt (sustainSq);
    r.transient = transient; r.lfRatio = lfRatio;
    return r;
}
} // namespace cmp
