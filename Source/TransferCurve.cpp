#include "TransferCurve.h"
#include "CompressorEngine.h"
#include "Parameters.h"

namespace ui
{
TransferCurve::TransferCurve (juce::AudioProcessorValueTreeState& apvts)
    : threshold (apvts.getRawParameterValue (params::id::threshold)),
      ratio (apvts.getRawParameterValue (params::id::ratio)),
      knee (apvts.getRawParameterValue (params::id::knee))
{
    startTimerHz (20);
}

void TransferCurve::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat().reduced (4.0f);
    auto X = [&] (float db) { return b.getX() + b.getWidth() * (db + 60.0f) / 60.0f; };
    auto Y = [&] (float db) { return b.getBottom() - b.getHeight() * (db + 60.0f) / 60.0f; };

    g.setColour (bg1);  g.fillRoundedRectangle (b, 6.0f);
    g.setColour (line);
    for (float t : { -48.0f, -36.0f, -24.0f, -12.0f }) { g.drawVerticalLine ((int) X (t), b.getY(), b.getBottom()); g.drawHorizontalLine ((int) Y (t), b.getX(), b.getRight()); }
    g.setColour (dim.withAlpha (0.5f));
    g.drawLine (X (-60.0f), Y (-60.0f), X (0.0f), Y (0.0f), 1.0f);                       // 1:1

    const float thr = threshold->load(), kn = knee->load();
    const int idx = juce::jlimit (0, 9, (int) std::lround (ratio->load()));
    const float invR = 1.0f / params::kRatios[idx];
    juce::Path p;
    for (int i = 0; i <= 120; ++i)
    {
        const float in = -60.0f + 60.0f * (float) i / 120.0f;
        const float out = in - cmp::CompressorEngine::computeGainReductionDb (in, thr, invR, kn);
        if (i == 0) p.startNewSubPath (X (in), Y (out)); else p.lineTo (X (in), Y (out));
    }
    g.setColour (bright.withAlpha (0.9f));
    g.strokePath (p, juce::PathStrokeType (1.8f));

    if (liveIn > -60.0f)                                                                    // ponto vivo
    {
        g.setColour (active);
        g.fillEllipse (X (liveIn) - 3.0f, Y (juce::jmin (0.0f, liveIn - liveGr)) - 3.0f, 6.0f, 6.0f);
    }
    g.setColour (dim);
    g.setFont (font (9.5f));
    g.drawText (spaced ("IN") + "  ->  " + spaced ("OUT"), b.reduced (6.0f), juce::Justification::topLeft);
}
} // namespace ui
