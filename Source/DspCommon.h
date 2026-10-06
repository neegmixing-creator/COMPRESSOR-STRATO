#pragma once
// Utilidades DSP sem dependência de JUCE (compilável e testável isoladamente).
#include <algorithm>
#include <cmath>
#include <vector>

namespace cmp
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db) noexcept   { return std::exp (db * 0.11512925464970229f); }
inline float linToDb (float lin) noexcept  { return 8.685889638065035f * std::log (std::max (lin, 1.0e-6f)); }
inline float flushDenorm (float x) noexcept { return std::fabs (x) < 1.0e-20f ? 0.0f : x; }
inline double flushDenorm (double x) noexcept { return std::fabs (x) < 1.0e-30 ? 0.0 : x; }

// Coeficiente de um polo: y = c*y + (1-c)*x, com constante de tempo em ms.
inline float timeToCoef (float ms, double sampleRate) noexcept
{
    const double n = std::max (0.001 * (double) ms * sampleRate, 1.0e-3);
    return (float) std::exp (-1.0 / n);
}

// Suavizador exponencial de um polo (para parâmetros: evita zipper noise).
class Smoother
{
public:
    void prepare (double sampleRate, float ms) noexcept { coef = timeToCoef (ms, sampleRate); }
    void setTarget (float t) noexcept   { target = t; }
    void snap (float v) noexcept        { target = current = v; }
    void snapToTarget() noexcept        { current = target; }
    float next() noexcept
    {
        current = target + coef * (current - target);
        if (std::fabs (current - target) < 1.0e-7f) current = target;
        return current;
    }
    float getTarget() const noexcept    { return target; }
    float getCurrent() const noexcept   { return current; }
    bool  atTarget() const noexcept     { return current == target; }
private:
    float coef = 0.0f, target = 0.0f, current = 0.0f;
};

// Biquad em double (precisão necessária para HPFs graves em sample rates altos).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setHighpass (double sampleRate, double freq, double q = 0.70710678118) noexcept
    {
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + cw) * 0.5 / a0;  b1 = -(1.0 + cw) / a0;  b2 = b0;
        a1 = -2.0 * cw / a0;         a2 = (1.0 - alpha) / a0;
    }
    float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = flushDenorm (b1 * x - a1 * y + z2);
        z2 = flushDenorm (b2 * x - a2 * y);
        return (float) y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

// Linha de atraso circular (tamanho potência de 2, sem alocação no audio thread).
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        size = 1;
        while (size < maxDelay + 2) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        pos = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    void push (float x) noexcept { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
    // d = 0 -> amostra mais recente; d = n -> n amostras atrás.
    float read (int d) const noexcept { return buf[(size_t) ((pos + size - 1 - d) & mask)]; }
private:
    std::vector<float> buf;
    int size = 0, mask = 0, pos = 0;
};
} // namespace cmp
