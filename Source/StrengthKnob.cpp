#include "StrengthKnob.h"

namespace ui
{
DialBase::DialBase (juce::RangedAudioParameter& p, int hz)
    : param (p),
      attachment (p, [this] (float v) { norm = param.convertTo0to1 (v); }, nullptr)
{
    attachment.sendInitialUpdate();
    shown = norm;
    startTimerHz (hz);
}

void DialBase::timerCallback()
{
    const float d = norm - shown;
    if (std::abs (d) > 0.0004f) { shown += d * 0.28f; repaint(); }
    else if (shown != norm)     { shown = norm; repaint(); }
    if (holdFrames > 0 && ! dragging) { --holdFrames; repaint(); }
}

void DialBase::setNormGesture (float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    const float v = param.getNormalisableRange().snapToLegalValue (param.convertFrom0to1 (n));
    attachment.setValueAsPartOfGesture (v);
}

void DialBase::mouseDown (const juce::MouseEvent& e)
{
    dragging = true; holdFrames = 40;
    lastX = (float) e.x; lastY = (float) e.y; accum = norm;
    attachment.beginGesture();
}

void DialBase::mouseDrag (const juce::MouseEvent& e)
{
    const float dy = lastY - (float) e.y, dx = (float) e.x - lastX;
    lastX = (float) e.x; lastY = (float) e.y;
    float pxPerFull = isStepped() ? 14.0f * (float) (param.getNumSteps() - 1) : 220.0f;
    if (! isStepped())
    {
        if (e.mods.isShiftDown())        pxPerFull *= 20.0f;     // ultrafino
        else if (e.mods.isCommandDown()) pxPerFull *= 4.0f;      // fino
    }
    accum = juce::jlimit (0.0f, 1.0f, accum + (dy + 0.5f * dx) / pxPerFull);
    setNormGesture (accum);
}

void DialBase::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
    dragging = false;
}

void DialBase::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    holdFrames = 40;
}

void DialBase::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    float n = norm;
    if (isStepped())
    {
        if (std::abs (w.deltaY) < 0.001f) return;
        n += (w.deltaY > 0.0f ? 1.0f : -1.0f) / (float) (param.getNumSteps() - 1);
    }
    else n += w.deltaY * (e.mods.isShiftDown() ? 0.04f : 0.4f);
    const float v = param.getNormalisableRange().snapToLegalValue (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
    attachment.setValueAsCompleteGesture (v);
    holdFrames = 40;
}

void StrengthKnob::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float v = getShown();
    const float dotRadius = R * 0.94f, discR = R * 0.70f;
    constexpr int N = 29;
    const float a0 = juce::degreesToRadians (-135.0f), span = juce::degreesToRadians (270.0f);

    // halo muito sutil, proporcional à intensidade
    {
        juce::ColourGradient halo (accent.withAlpha (0.08f * v), c.x, c.y, accent.withAlpha (0.0f), c.x, c.y - R, true);
        g.setGradientFill (halo);
        g.fillEllipse (b);
    }

    // pontos: 0% = nenhum aceso, 50% = metade, 100% = todos
    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / (float) (N - 1), a = a0 + span * t;
        const float lit = juce::jlimit (0.0f, 1.0f, v * (float) N - (float) i);
        const juce::Point<float> p (c.x + dotRadius * std::sin (a), c.y - dotRadius * std::cos (a));
        const float r = 1.6f + 1.0f * lit;
        if (lit > 0.0f)
        {
            g.setColour (bright.withAlpha (0.10f * lit));
            g.fillEllipse (p.x - r * 2.4f, p.y - r * 2.4f, r * 4.8f, r * 4.8f);
        }
        g.setColour (dim.darker (0.5f).interpolatedWith (bright, lit));
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
    }

    // corpo do knob: aro externo e face escura
    {
        juce::ColourGradient ring (bg2.brighter (0.18f), c.x, c.y - discR, bg0, c.x, c.y + discR, false);
        g.setGradientFill (ring);
        g.fillEllipse (c.x - discR, c.y - discR, discR * 2.0f, discR * 2.0f);
        const float fr = discR * 0.93f;
        g.setColour (bg1.brighter (0.05f));
        g.fillEllipse (c.x - fr, c.y - fr, fr * 2.0f, fr * 2.0f);
        g.setColour (line);
        g.drawEllipse (c.x - fr, c.y - fr, fr * 2.0f, fr * 2.0f, 1.0f);

        // indicador luminoso
        const float ai = a0 + span * v;
        const juce::Line<float> ind (c.x + fr * 0.62f * std::sin (ai), c.y - fr * 0.62f * std::cos (ai),
                                     c.x + fr * 0.88f * std::sin (ai), c.y - fr * 0.88f * std::cos (ai));
        g.setColour (bright.withAlpha (0.16f));
        g.drawLine (ind, 8.0f);
        g.setColour (bright);
        g.drawLine (ind, 2.8f);

        // valor numérico
        const int pct = juce::roundToInt (param.convertFrom0to1 (v));
        g.setColour (text.withAlpha (0.55f + 0.45f * juce::jmax (v, getHoldAlpha())));
        g.setFont (font (R * 0.30f));
        g.drawText (juce::String (pct) + "%", juce::Rectangle<float> (c.x - fr, c.y - fr * 0.28f, fr * 2.0f, fr * 0.56f), juce::Justification::centred);
    }
}
} // namespace ui
