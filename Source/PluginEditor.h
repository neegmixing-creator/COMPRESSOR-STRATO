#pragma once
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "StrengthKnob.h"
#include "MeterViews.h"
#include "AdvancedPanel.h"

// STRATA — SIMPLES POR FORA. COMPLEXO POR DENTRO.
// Tela principal: GAIN REDUCTION, STRENGTH, ACTIVE, MODE, I/O. Tudo o mais fica atrás de ADVANCED.
// A UI só lê/escreve parâmetros e lê os medidores atômicos: nunca toca no audio thread.
class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void layout();

    PluginProcessor& processor;
    ui::CustomLookAndFeel laf;

    ui::StrengthKnob strength;
    ui::GainReductionView grView;
    ui::IoMeterView ioView;
    ui::ParamToggle active;
    ui::ChoiceStrip modeStrip;
    ui::AdvancedPanel advanced;

    float progress = 0.0f;          // 0 = Advanced fechado, 1 = aberto (animado)
    bool advancedOpen = false;
    juce::Rectangle<float> advancedButton, knobLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
