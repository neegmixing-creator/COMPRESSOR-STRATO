#pragma once
#include "DspCommon.h"

namespace cmp
{
enum class SatMode { Clean = 0, Tube, Tape, Modern };

// Saturação original, dependente do nível. Ganho de pequenos sinais = 1 (só atua
// quando o sinal "esquenta"). Deve rodar na taxa oversampled.
class Saturator
{
public:
    void setSampleRate (double osRate) noexcept;
    void setMode (SatMode m) noexcept { mode = m; }
    void reset() noexcept { lp[0] = lp[1] = 0.0f; }
    float process (float x, float amount, int ch) noexcept;

    static float fastTanh (float x) noexcept;   // Padé contínuo em C1, satura em |x|>=3
private:
    SatMode mode = SatMode::Clean;
    float lpCoef = 0.5f;
    float lp[2] = { 0.0f, 0.0f };
};
} // namespace cmp
