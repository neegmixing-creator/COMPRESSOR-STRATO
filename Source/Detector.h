#pragma once
#include "DspCommon.h"

namespace cmp
{
// Detector adaptativo (separado do áudio):
//   entrada -> DC block -> HPF sidechain -> proteção de graves adaptativa
//           -> { pico (fast env) | RMS (slow env) | sustain (150 ms) | transiente | energia de graves }
class Detector
{
public:
    struct Result
    {
        float levelDb, crestDb;                 // mistura pico/RMS fixa (usada pelo caminho legado)
        float peakLin, rmsLin, sustainLin;      // envelopes: rápido, lento, sustentado
        float transient;                        // 0..1: início de evento (energia rápida >> lenta)
        float lfRatio;                          // 0..1: fração da energia abaixo de ~120 Hz
    };

    void prepare (double sampleRate);
    void reset() noexcept;
    void configure (float rmsMix, float peakDecayMs, float hpfHz, float lfProtect = 0.0f) noexcept;
    Result process (float x) noexcept;

private:
    double sr = 48000.0, dcR = 0.999, dcX1 = 0.0, dcY1 = 0.0;
    Biquad hpf;
    bool hpfOn = false;
    float hpfHz = -1.0f, peakMs = -1.0f, lfProtect = 0.0f;
    float rmsMix = 0.25f, peakDecay = 0.99f, rmsCoef = 0.999f;
    float peakEnv = 0.0f, meanSq = 0.0f, sustainSq = 0.0f;
    float lpState = 0.0f, lfE = 0.0f, totE = 0.0f, fastE = 0.0f, slowE = 0.0f, transient = 0.0f;
    float lfCoef = 0.0f, eCoef = 0.0f, fastCoef = 0.0f, slowCoef = 0.0f, sustCoef = 0.0f, transRel = 0.0f;
};
} // namespace cmp
