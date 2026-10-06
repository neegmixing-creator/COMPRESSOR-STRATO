#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace cmp
{
// Dados escritos pelo audio thread (atômicos lock-free) e lidos pela UI.
struct MeterData
{
    std::atomic<float> inPeak { 0.0f };    // pico linear do bloco (após INPUT gain)
    std::atomic<float> outPeak { 0.0f };   // pico linear do bloco (saída final)
    std::atomic<float> grDb { 0.0f };      // gain reduction máximo do bloco (dB, positivo)

    // Máximos ACUMULADOS desde a última leitura da UI (a UI zera com exchange(0)): nenhum pico entre dois
    // quadros de 30 Hz se perde, e quando o áudio para os medidores caem a zero.
    std::atomic<float> inPeakUi { 0.0f }, outPeakUi { 0.0f }, grUi { 0.0f };

    static void atomicMax (std::atomic<float>& a, float v) noexcept   // lock-free, seguro no audio thread
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
};

// Balística do lado da UI: ataque instantâneo, queda em dB/s, peak-hold e latch de CLIP.
class MeterModel
{
public:
    void configure (float fallDbPerSec, float holdSeconds, float floorDb) noexcept
    { fall = fallDbPerSec; hold = holdSeconds; floor = floorDb; reset(); }

    void reset() noexcept { value = peak = floor; holdLeft = 0.0f; clip = false; }

    void pushDb (float db, float dtSec) noexcept
    {
        db = std::max (db, floor);
        value = (db >= value) ? db : std::max (db, value - fall * dtSec);
        if (db >= peak) { peak = db; holdLeft = hold; }
        else if ((holdLeft -= dtSec) <= 0.0f) peak = std::max (value, peak - fall * dtSec);
    }
    void pushLinear (float lin, float dtSec) noexcept
    {
        pushDb (lin > 1.0e-6f ? 20.0f * std::log10 (lin) : floor, dtSec);
        if (lin > 1.0f) clip = true;
    }
    float getDb() const noexcept      { return value; }
    float getPeakDb() const noexcept  { return peak; }
    bool  isClipping() const noexcept { return clip; }
    void  clearClip() noexcept        { clip = false; }
private:
    float fall = 24.0f, hold = 1.5f, floor = -60.0f;
    float value = -60.0f, peak = -60.0f, holdLeft = 0.0f;
    bool clip = false;
};
} // namespace cmp
