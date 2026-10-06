#pragma once
#include "DspCommon.h"
#include <array>

namespace cmp
{
// Oversampler em cascata de estágios half-band FIR (fase linear, janela Kaiser ~80 dB).
// Fator 1/2/4/8. Latência sempre em número INTEIRO de amostras na taxa base
// (32 / 38 / 40 amostras para 2x / 4x / 8x), então o caminho dry pode ser compensado exatamente.
class Oversampler
{
public:
    static constexpr int kMaxStages = 3;

    void prepare (int numChannels);
    void reset() noexcept;
    void setFactor (int factor);                 // 1, 2, 4 ou 8
    int  getFactor() const noexcept { return 1 << numStages; }
    int  getLatency() const noexcept;            // em amostras da taxa base

    // Sobe 1 amostra para a taxa oversampled, aplica fn a cada amostra, desce de volta.
    template <class Fn>
    float process (float x, int ch, Fn&& fn) noexcept { return run (0, x, ch, fn); }

private:
    struct Tap { int k; float c; };
    struct ChState { std::vector<float> upHist, downHist; int upPos = 0, downPos = 0; };
    struct Stage
    {
        int D = 0, upMask = 0, downMask = 0;
        std::vector<Tap> phase0, phase1, down;
        std::array<ChState, 2> ch;
    };

    void designStage (Stage& s, int D);

    template <class Fn>
    float run (int stage, float in, int ch, Fn& fn) noexcept
    {
        if (stage >= numStages) return fn (in);
        Stage& s = stages[(size_t) stage];
        ChState& st = s.ch[(size_t) ch];

        // --- upsample x2 (polifásico) ---
        st.upHist[(size_t) st.upPos] = in;
        float y0 = 0.0f, y1 = 0.0f;
        for (const Tap& t : s.phase0) y0 += t.c * st.upHist[(size_t) ((st.upPos - t.k) & s.upMask)];
        for (const Tap& t : s.phase1) y1 += t.c * st.upHist[(size_t) ((st.upPos - t.k) & s.upMask)];
        st.upPos = (st.upPos + 1) & s.upMask;

        const float r0 = run (stage + 1, y0, ch, fn);
        const float r1 = run (stage + 1, y1, ch, fn);

        // --- downsample x2 ---
        // A saída é avaliada alinhada à amostra r0 (índice par): latência total inteira.
        st.downHist[(size_t) st.downPos] = r0;  st.downPos = (st.downPos + 1) & s.downMask;
        float out = 0.0f;
        for (const Tap& t : s.down) out += t.c * st.downHist[(size_t) ((st.downPos - 1 - t.k) & s.downMask)];
        st.downHist[(size_t) st.downPos] = r1;  st.downPos = (st.downPos + 1) & s.downMask;
        return out;
    }

    std::array<Stage, kMaxStages> stages;
    int numStages = 0;
};
} // namespace cmp
