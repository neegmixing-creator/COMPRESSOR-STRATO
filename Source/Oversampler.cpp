#include "Oversampler.h"

namespace cmp
{
static double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) { term *= (x * 0.5) / k; sum += term * term; }
    return sum;
}

void Oversampler::designStage (Stage& s, int D)
{
    s.D = D;
    const int N = 2 * D + 1;
    const double beta = 8.0;
    std::vector<double> h ((size_t) N);
    double sum = 0.0;
    for (int j = 0; j < N; ++j)
    {
        const int n = j - D;
        double v = (n == 0) ? 0.5 : std::sin (kPi * n * 0.5) / (kPi * n);
        if (n != 0 && (n % 2) == 0) v = 0.0;                         // propriedade half-band
        const double r = (double) n / D;
        v *= besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        h[(size_t) j] = v;
        sum += v;
    }
    s.phase0.clear(); s.phase1.clear(); s.down.clear();
    for (int j = 0; j < N; ++j)
    {
        const double c = h[(size_t) j] / sum;
        if (c == 0.0) continue;
        s.down.push_back ({ j, (float) c });
        if ((j & 1) == 0) s.phase0.push_back ({ j / 2, (float) (2.0 * c) });
        else              s.phase1.push_back ({ (j - 1) / 2, (float) (2.0 * c) });
    }
    int len = 1; while (len < N + 2) len <<= 1;
    s.upMask = s.downMask = len - 1;
    for (auto& st : s.ch) { st.upHist.assign ((size_t) len, 0.0f); st.downHist.assign ((size_t) len, 0.0f); st.upPos = st.downPos = 0; }
}

void Oversampler::prepare (int)
{
    static constexpr int kD[kMaxStages] = { 32, 12, 8 };    // latência na taxa base: 32, 6, 2
    for (int i = 0; i < kMaxStages; ++i) designStage (stages[(size_t) i], kD[i]);
    numStages = 0;
}

void Oversampler::reset() noexcept
{
    for (auto& s : stages)
        for (auto& st : s.ch)
        {
            std::fill (st.upHist.begin(), st.upHist.end(), 0.0f);
            std::fill (st.downHist.begin(), st.downHist.end(), 0.0f);
            st.upPos = st.downPos = 0;
        }
}

void Oversampler::setFactor (int factor)
{
    const int n = factor >= 8 ? 3 : factor >= 4 ? 2 : factor >= 2 ? 1 : 0;
    if (n != numStages) { numStages = n; reset(); }
}

int Oversampler::getLatency() const noexcept
{
    static constexpr int kLat[kMaxStages] = { 32, 6, 2 };
    int total = 0;
    for (int i = 0; i < numStages; ++i) total += kLat[i];
    return total;
}
} // namespace cmp
