#include "MeterViews.h"

namespace ui
{
void GainReductionView::push (float grDb, float dtSec)
{
    model.pushDb (grDb, dtSec);
    hist[(size_t) head] = model.getDb();
    head = (head + 1) % kBars;
    repaint();
}

void GainReductionView::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat();
    const float midY = b.getCentreY() - 6.0f, maxH = b.getHeight() * 0.42f;
    const float step = b.getWidth() / (float) kBars;

    for (int i = 0; i < kBars; ++i)
    {
        const float gr = hist[(size_t) ((head + i) % kBars)];
        const float x = b.getX() + ((float) i + 0.5f) * step;
        const float age = (float) i / (float) kBars;                       // 0 = antigo, 1 = recente
        const float h = juce::jlimit (0.0f, 1.0f, gr / 24.0f) * maxH;      // escala 0..-24 dB
        g.setColour (dim.withAlpha (0.10f + 0.25f * age));
        g.fillRect (x - 0.5f, midY - 1.0f, 1.0f, 2.0f);                    // linha de base
        if (h > 0.4f)
        {
            g.setColour (accent.withAlpha (0.20f + 0.65f * age));
            g.fillRect (x - 0.6f, midY, 1.2f, h);
            g.setColour (dim.withAlpha (0.18f + 0.25f * age));
            g.fillRect (x - 0.6f, midY - h * 0.55f, 1.2f, h * 0.55f);
        }
    }
    g.setColour (dim);
    g.setFont (font (b.getHeight() * 0.17f));
    g.drawText (spaced ("GR") + "  " + juce::String (model.getDb(), 1) + " dB",
                juce::Rectangle<float> (b.getX(), b.getBottom() - b.getHeight() * 0.26f, b.getWidth(), b.getHeight() * 0.26f), juce::Justification::centredLeft);
}

void IoMeterView::push (float inLin, float outLin, float dtSec)
{
    in.pushLinear (inLin, dtSec);
    out.pushLinear (outLin, dtSec);
    repaint();
}

void IoMeterView::drawBar (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const cmp::MeterModel& m)
{
    using namespace col;
    auto toX = [&] (float db) { return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f); };
    const float barY = r.getCentreY() - 2.0f;
    g.setColour (dim);
    g.setFont (font (r.getHeight() * 0.42f));
    g.drawText (spaced (label), r.withWidth (r.getWidth() * 0.12f).translated (-r.getWidth() * 0.14f, 0), juce::Justification::centredRight);

    g.setColour (bg2);
    g.fillRoundedRectangle (r.getX(), barY, r.getWidth(), 4.0f, 2.0f);
    for (float t : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f })              // escala discreta
    { g.setColour (line); g.fillRect (toX (t) - 0.5f, barY - 3.0f, 1.0f, 3.0f); }

    const float x1 = toX (m.getDb());
    g.setColour (m.getDb() > -6.0f ? warn.interpolatedWith (bright, 0.35f) : text.withAlpha (0.85f));
    g.fillRoundedRectangle (r.getX(), barY, juce::jmax (0.0f, x1 - r.getX()), 4.0f, 2.0f);
    g.setColour (bright.withAlpha (0.8f));
    g.fillRect (toX (m.getPeakDb()) - 0.5f, barY - 2.0f, 1.5f, 8.0f);              // peak hold

    g.setColour (m.isClipping() ? warn : dim.withAlpha (0.4f));
    g.setFont (font (r.getHeight() * 0.36f));
    g.drawText ("CLIP", r.withX (r.getRight() + 6.0f).withWidth (r.getWidth() * 0.16f), juce::Justification::centredLeft);
}

void IoMeterView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float rowH = b.getHeight() * 0.5f, left = b.getWidth() * 0.18f, w = b.getWidth() * 0.64f;
    drawBar (g, { b.getX() + left, b.getY(), w, rowH }, "IN", in);
    drawBar (g, { b.getX() + left, b.getY() + rowH, w, rowH }, "OUT", out);
}
} // namespace ui
