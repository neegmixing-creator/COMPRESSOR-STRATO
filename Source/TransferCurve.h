#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CustomLookAndFeel.h"

namespace ui
{
// Curva de transferência (entrada x saída) com threshold/ratio/knee ajustados pelo usuário, em tempo real,
// e um ponto vivo com o nível atual. (O STRENGTH e o programa adaptam essa curva dentro do motor.)
class TransferCurve : public juce::Component, private juce::Timer
{
public:
    explicit TransferCurve (juce::AudioProcessorValueTreeState&);
    ~TransferCurve() override { stopTimer(); }
    void setLive (float inDb, float grDb) noexcept { liveIn = inDb; liveGr = grDb; }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isShowing()) repaint(); }
    std::atomic<float>* threshold; std::atomic<float>* ratio; std::atomic<float>* knee;
    float liveIn = -100.0f, liveGr = 0.0f;
};
} // namespace ui
