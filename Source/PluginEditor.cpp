#include "PluginEditor.h"

namespace
{
juce::RangedAudioParameter& par (PluginProcessor& p, const char* id) { return *p.apvts.getParameter (id); }
juce::AudioParameterChoice& choice (PluginProcessor& p, const char* id) { return *dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)); }
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      strength (par (p, params::id::strength)),
      active (par (p, params::id::bypass), "Active", /*invert*/ true, /*led*/ true),
      modeStrip (choice (p, params::id::mode), "Mode"),
      advanced (p.apvts)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (strength);
    addAndMakeVisible (grView);
    addAndMakeVisible (ioView);
    addAndMakeVisible (active);
    addAndMakeVisible (modeStrip);
    addChildComponent (advanced);

    setResizable (true, true);
    setResizeLimits (600, 600, 1800, 1800);
    getConstrainer()->setFixedAspectRatio (1.0);
    setSize (1000, 1000);
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PluginEditor::mouseUp (const juce::MouseEvent& e)
{
    if (advancedButton.contains (e.position)) { advancedOpen = ! advancedOpen; if (advancedOpen) advanced.setVisible (true); repaint(); }
}

void PluginEditor::timerCallback()
{
    // Medidores: máximos acumulados desde a última leitura (exchange zera; o audio thread só faz max atômico)
    auto& m = processor.getMeterData();
    const float inP = m.inPeakUi.exchange (0.0f), outP = m.outPeakUi.exchange (0.0f), gr = m.grUi.exchange (0.0f);
    constexpr float dt = 1.0f / 30.0f;
    grView.push (gr, dt);
    ioView.push (inP, outP, dt);
    advanced.getCurve().setLive (inP > 1.0e-6f ? 20.0f * std::log10 (inP) : -100.0f, gr);

    const float target = advancedOpen ? 1.0f : 0.0f;
    if (std::abs (progress - target) > 0.002f)
    {
        progress += (target - progress) * 0.25f;
        layout();
        repaint();
    }
    else if (progress != target)
    {
        progress = target;
        advanced.setVisible (advancedOpen);
        layout();
        repaint();
    }
}

void PluginEditor::layout()
{
    const float W = (float) getWidth(), H = (float) getHeight(), p = progress;

    grView.setBounds (juce::Rectangle<float> (W * 0.20f, H * 0.055f, W * 0.60f, H * 0.12f).toNearestInt());
    ioView.setBounds (juce::Rectangle<float> (W * 0.06f, H * 0.185f, W * 0.30f, H * 0.04f).toNearestInt());
    active.setBounds (juce::Rectangle<float> (W * 0.76f, H * 0.19f, W * 0.18f, H * 0.035f).toNearestInt());

    // o grupo do knob encolhe e sobe quando ADVANCED abre
    const float knobD = H * (0.50f - 0.22f * p);
    const float cy    = H * (0.50f - 0.14f * p);
    strength.setBounds (juce::Rectangle<float> (W * 0.5f - knobD * 0.5f, cy - knobD * 0.5f, knobD, knobD).toNearestInt());

    const float labelY = cy + knobD * 0.5f + H * 0.012f;
    knobLabelArea = { W * 0.25f, labelY, W * 0.5f, H * 0.045f };
    modeStrip.setBounds (juce::Rectangle<float> (W * 0.22f, labelY + H * 0.05f, W * 0.56f, H * 0.055f).toNearestInt());

    // painel Advanced desliza de baixo para cima (fica visível só quando p > 0)
    const float panelH = H * 0.30f, panelTop = H * 0.925f - panelH;
    advanced.setBounds (juce::Rectangle<float> (W * 0.04f, juce::jmap (p, H, panelTop), W * 0.92f, panelH).toNearestInt());

    advancedButton = { W * 0.38f, H * 0.935f, W * 0.24f, H * 0.045f };
}

void PluginEditor::paint (juce::Graphics& g)
{
    using namespace ui::col;
    const auto b = getLocalBounds().toFloat();
    const float W = b.getWidth(), H = b.getHeight();

    g.setColour (bg0);
    g.fillAll();
    juce::ColourGradient bg (bg2, W * 0.5f, H * 0.45f, bg0, W * 0.5f, H * 1.05f, true);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (b.reduced (3.0f), 26.0f);
    g.setColour (line);
    g.drawRoundedRectangle (b.reduced (3.5f), 26.0f, 1.0f);

    g.setColour (text.withAlpha (0.9f));
    g.setFont (ui::font (H * 0.022f, true));
    g.drawText (ui::spaced ("STRATA"), juce::Rectangle<float> (W * 0.06f, H * 0.05f, W * 0.2f, H * 0.04f), juce::Justification::centredLeft);

    g.setColour (bright);
    g.setFont (ui::font (H * 0.026f));
    g.drawText (ui::spaced ("STRENGTH"), knobLabelArea, juce::Justification::centred);

    // ADVANCED: rótulo discreto (clique abre/fecha o painel)
    g.setColour (advancedOpen ? text : dim);
    g.setFont (ui::font (H * 0.016f));
    g.drawText (ui::spaced ("ADVANCED") + (advancedOpen ? "   v" : "   ^"), advancedButton, juce::Justification::centred);
    if (progress < 0.5f)
    {
        g.setColour (dim.withAlpha (0.5f * (1.0f - progress * 2.0f)));
        g.setFont (ui::font (H * 0.012f));
        g.drawText (ui::spaced ("ADAPTIVE COMPRESSION"), juce::Rectangle<float> (0.0f, H * 0.972f, W, H * 0.02f), juce::Justification::centred);
    }
}
