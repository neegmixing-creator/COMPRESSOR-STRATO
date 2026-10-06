#include "AdvancedPanel.h"
#include "Parameters.h"

namespace ui
{
// ---------------------------------------------------------------- SmallDial
void SmallDial::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat();
    const float labelH = b.getHeight() * 0.18f, valueH = b.getHeight() * 0.18f;
    const auto area = b.withTrimmedTop (labelH).withTrimmedBottom (valueH);
    const auto c = area.getCentre();
    const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.40f;
    const float v = getShown();
    const float a0 = juce::degreesToRadians (-135.0f), span = juce::degreesToRadians (270.0f);

    g.setColour (dim);
    g.setFont (font (labelH * 0.85f));
    g.drawText (spaced (label.toUpperCase()), b.withHeight (labelH), juce::Justification::centred);

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, R, R, 0.0f, a0, a0 + span, true);
    value.addCentredArc (c.x, c.y, R, R, 0.0f, a0, a0 + span * v, true);
    g.setColour (bg2.brighter (0.12f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (bright.withAlpha (0.9f));
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ai = a0 + span * v;
    g.setColour (bg1);
    g.fillEllipse (c.x - R * 0.62f, c.y - R * 0.62f, R * 1.24f, R * 1.24f);
    g.setColour (bright);
    g.drawLine (c.x + R * 0.25f * std::sin (ai), c.y - R * 0.25f * std::cos (ai), c.x + R * 0.55f * std::sin (ai), c.y - R * 0.55f * std::cos (ai), 2.0f);

    juce::String val = param.getCurrentValueAsText();
    if (param.getLabel().isNotEmpty()) val << " " << param.getLabel();
    g.setColour (text.withAlpha (0.65f + 0.35f * getHoldAlpha()));
    g.setFont (font (valueH * 0.85f));
    g.drawText (val, b.withTop (b.getBottom() - valueH), juce::Justification::centred);
}

// ---------------------------------------------------------------- ChoiceStrip
ChoiceStrip::ChoiceStrip (juce::AudioParameterChoice& p, const juce::String& t)
    : param (p), title (t),
      attachment (p, [this] (float v) { current = juce::roundToInt (v); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

int ChoiceStrip::indexAt (juce::Point<float> pt) const
{
    const int n = param.choices.size();
    const auto b = getLocalBounds().toFloat().withTrimmedTop (getHeight() * 0.38f);
    return juce::jlimit (0, juce::jmax (0, n - 1), (int) ((pt.x - b.getX()) / (b.getWidth() / (float) juce::jmax (1, n))));
}

void ChoiceStrip::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() || e.position.y < (float) getHeight() * 0.38f) return;
    attachment.setValueAsCompleteGesture ((float) indexAt (e.position));
}

void ChoiceStrip::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat();
    g.setColour (dim);
    g.setFont (font (b.getHeight() * 0.26f));
    g.drawText (spaced (title.toUpperCase()), b.withHeight (b.getHeight() * 0.38f), juce::Justification::centredLeft);

    const int n = param.choices.size();
    const auto row = b.withTrimmedTop (b.getHeight() * 0.38f);
    const float w = row.getWidth() / (float) juce::jmax (1, n);
    g.setFont (font (row.getHeight() * 0.42f));
    for (int i = 0; i < n; ++i)
    {
        const auto cell = juce::Rectangle<float> (row.getX() + w * (float) i, row.getY(), w, row.getHeight());
        const bool sel = i == current;
        if (sel) { g.setColour (bg2.brighter (0.1f)); g.fillRoundedRectangle (cell.reduced (2.0f), 5.0f); }
        g.setColour (sel ? bright : dim);
        g.drawText (param.choices[i].toUpperCase(), cell, juce::Justification::centred);
    }
}

// ---------------------------------------------------------------- ParamToggle
ParamToggle::ParamToggle (juce::RangedAudioParameter& p, const juce::String& name, bool invert, bool led)
    : param (p), label (name), inverted (invert), showLed (led),
      attachment (p, [this] (float v) { on = (v > 0.5f) != inverted; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

void ParamToggle::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown()) return;
    const bool newOn = ! on;
    attachment.setValueAsCompleteGesture ((newOn != inverted) ? 1.0f : 0.0f);
}

void ParamToggle::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float h = b.getHeight();
    if (showLed)
    {
        const float d = h * 0.30f;
        const float cx = b.getX() + d, cy = b.getCentreY();
        if (on) { g.setColour (active.withAlpha (0.25f)); g.fillEllipse (cx - d, cy - d, d * 2.0f, d * 2.0f); }
        g.setColour (on ? active : dim.darker (0.4f));
        g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
        g.setColour (on ? text : dim);
        g.setFont (font (h * 0.50f));
        g.drawText (spaced (label.toUpperCase()), b.withTrimmedLeft (d * 2.6f), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (on ? bg2.brighter (0.15f) : bg1);
        g.fillRoundedRectangle (b, h * 0.5f);
        g.setColour (on ? bright.withAlpha (0.6f) : line);
        g.drawRoundedRectangle (b, h * 0.5f, 1.0f);
        g.setColour (on ? bright : dim);
        g.setFont (font (h * 0.40f));
        g.drawText (label.toUpperCase(), b, juce::Justification::centred);
    }
}

// ---------------------------------------------------------------- AdvancedPanel
AdvancedPanel::AdvancedPanel (juce::AudioProcessorValueTreeState& apvts) : curve (apvts)
{
    namespace id = params::id;
    auto par = [&] (const char* i) -> juce::RangedAudioParameter& { return *apvts.getParameter (i); };
    auto choice = [&] (const char* i) -> juce::AudioParameterChoice& { return *dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (i)); };

    const std::pair<const char*, const char*> d[] = {
        { id::threshold, "Threshold" }, { id::ratio, "Ratio" }, { id::attack, "Attack" }, { id::release, "Release" }, { id::knee, "Knee" }, { id::character, "Character" },
        { id::scHpf, "SC HPF" }, { id::mix, "Mix" }, { id::input, "Input" }, { id::output, "Output" }, { id::lookahead, "Lookahead" }, { id::stereoLink, "Link" } };
    for (auto& e : d) { dials.push_back (std::make_unique<SmallDial> (par (e.first), e.second)); addAndMakeVisible (*dials.back()); }

    const std::pair<const char*, const char*> s[] = {
        { id::satMode, "Character Mode" }, { id::stereoMode, "Stereo Mode" }, { id::oversampling, "Oversampling" } };
    for (auto& e : s) { strips.push_back (std::make_unique<ChoiceStrip> (choice (e.first), e.second)); addAndMakeVisible (*strips.back()); }

    const std::pair<const char*, const char*> t[] = {
        { id::autoAttack, "Auto Attack" }, { id::autoRelease, "Auto Release" }, { id::autoGain, "Auto Gain" }, { id::adaptive, "Adaptive" } };
    for (auto& e : t) { toggles.push_back (std::make_unique<ParamToggle> (par (e.first), e.second)); addAndMakeVisible (*toggles.back()); }

    addAndMakeVisible (curve);
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    using namespace col;
    const auto b = getLocalBounds().toFloat();
    g.setColour (bg1);
    g.fillRoundedRectangle (b, 18.0f);
    g.setColour (line);
    g.drawRoundedRectangle (b.reduced (0.5f), 18.0f, 1.0f);
}

void AdvancedPanel::resized()
{
    auto b = getLocalBounds().reduced (getWidth() / 40, getHeight() / 14);
    auto top = b.removeFromTop (b.getHeight() * 0.62f);
    const int curveW = top.getHeight();
    curve.setBounds (top.removeFromRight (curveW).reduced (4));
    top.removeFromRight (getWidth() / 60);

    // 12 dials em 2 linhas de 6
    const int cols = 6, cw = top.getWidth() / cols, ch = top.getHeight() / 2;
    for (size_t i = 0; i < dials.size(); ++i)
        dials[i]->setBounds (top.getX() + (int) (i % cols) * cw, top.getY() + (int) (i / cols) * ch, cw, ch);

    b.removeFromTop (b.getHeight() / 8);
    auto row = b;
    const int stripW = (int) ((float) row.getWidth() * 0.62f / (float) strips.size());
    for (auto& s : strips) s->setBounds (row.removeFromLeft (stripW).reduced (6, 0));
    const int tw = row.getWidth() / (int) toggles.size();
    for (auto& t : toggles) t->setBounds (row.removeFromLeft (tw).reduced (4, row.getHeight() / 5));
}
} // namespace ui
