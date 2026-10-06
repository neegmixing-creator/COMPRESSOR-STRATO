#include "Saturation.h"

namespace cmp
{
float Saturator::fastTanh (float x) noexcept
{
    if (x >= 3.0f) return 1.0f;
    if (x <= -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void Saturator::setSampleRate (double osRate) noexcept
{
    lpCoef = 1.0f - (float) std::exp (-2.0 * kPi * 6000.0 / osRate);
    reset();
}

float Saturator::process (float x, float amount, int ch) noexcept
{
    if (amount <= 1.0e-5f) return x;

    const float k = (mode == SatMode::Modern ? 1.0f + 4.0f * amount : 1.0f + 2.5f * amount);
    float y;
    switch (mode)
    {
        case SatMode::Clean:    // poucos harmônicos: apenas 3ª harmônica leve (curva algébrica suave)
        {
            y = x / std::sqrt (1.0f + x * x);
            break;
        }
        case SatMode::Tube:     // assimétrica (harmônicos pares + ímpares), viés cresce com o drive
        {
            const float b = 0.4f * amount;
            const float tb = fastTanh (b);
            y = (fastTanh (k * x + b) - tb) / (k * (1.0f - tb * tb));
            break;
        }
        case SatMode::Tape:     // simétrica, curva suave tipo atan algébrico
        {
            const float kx = k * x;
            y = kx / std::sqrt (1.0f + kx * kx) / k;
            break;
        }
        case SatMode::Modern:   // joelho fechado (p = 4), drive reduzido: saturação limpa e controlada
        default:
        {
            const float kx = k * x, q = kx * kx;
            y = kx / std::sqrt (std::sqrt (1.0f + q * q)) / k;
            break;
        }
    }

    float delta = y - x;                          // só a parte "harmônica" é filtrada/misturada
    if (mode == SatMode::Tape)                    // harmônicos de fita: suavizados em ~6 kHz
    {
        // dependente do nível: sinais fortes perdem mais "ar" nos harmônicos (comportamento de fita)
        const float c = lpCoef * (1.0f - 0.5f * std::min (1.0f, std::fabs (x)));
        lp[ch] = flushDenorm (lp[ch] + c * (delta - lp[ch]));
        delta = lp[ch];
    }
    return x + amount * delta;
}
} // namespace cmp
