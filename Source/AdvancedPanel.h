#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "StrengthKnob.h"
#include "TransferCurve.h"

namespace ui
{
// Dial pequeno (float ou escolha em passos).
class SmallDial : public DialBase
{
public:
    SmallDial (juce::RangedAudioParameter& p, const juce::String& name) : DialBase (p, 30), label (name) {}
    void paint (juce::Graphics&) override;
private:
    juce::String label;
};

// Seletor de escolhas em texto: CLEAN | TUBE | TAPE | MODERN
class ChoiceStrip : public juce::Component
{
public:
    ChoiceStrip (juce::AudioParameterChoice& p, const juce::String& title);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    int indexAt (juce::Point<float>) const;
    juce::AudioParameterChoice& param;
    juce::String title;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Chave liga/desliga em "pílula" (também usada para ACTIVE, com lógica invertida do bypass).
class ParamToggle : public juce::Component
{
public:
    ParamToggle (juce::RangedAudioParameter& p, const juce::String& name, bool invert = false, bool led = false);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::RangedAudioParameter& param;
    juce::String label;
    bool inverted, showLed, on = false;
    juce::ParameterAttachment attachment;
};

class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    TransferCurve& getCurve() noexcept { return curve; }
private:
    std::vector<std::unique_ptr<SmallDial>> dials;
    std::vector<std::unique_ptr<ChoiceStrip>> strips;
    std::vector<std::unique_ptr<ParamToggle>> toggles;
    TransferCurve curve;
};
} // namespace ui
