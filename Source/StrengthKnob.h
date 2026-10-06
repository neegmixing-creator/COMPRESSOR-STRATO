#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CustomLookAndFeel.h"

namespace ui
{
// Base de interação dos dials. A UI NUNCA processa áudio: só lê/escreve parâmetros pelo ParameterAttachment
// (thread-safe, com gestos corretos para automação/undo do host).
//   arrastar vertical (e horizontal, opcional) | Ctrl/Cmd = fino | Shift = ultrafino | roda = preciso | duplo clique = reset
class DialBase : public juce::Component, protected juce::Timer
{
public:
    explicit DialBase (juce::RangedAudioParameter& p, int timerHz = 60);
    ~DialBase() override { stopTimer(); }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

protected:
    float getNorm() const noexcept   { return norm; }
    float getShown() const noexcept  { return shown; }                 // valor animado (suave)
    float getHoldAlpha() const noexcept { return juce::jlimit (0.0f, 1.0f, (float) holdFrames / 20.0f); }
    bool  isStepped() const noexcept { return param.getNumSteps() > 1 && param.getNumSteps() < 64; }
    juce::RangedAudioParameter& param;

private:
    void timerCallback() override;
    void setNormGesture (float n);

    float norm = 0.0f, shown = 0.0f, lastY = 0.0f, lastX = 0.0f, accum = 0.0f;
    int holdFrames = 0;
    bool dragging = false;
    juce::ParameterAttachment attachment;
};

// Knob central gigante do STRENGTH: pontos ao redor, indicador luminoso, glow sutil, valor em %.
class StrengthKnob : public DialBase
{
public:
    explicit StrengthKnob (juce::RangedAudioParameter& p) : DialBase (p, 60) {}
    void paint (juce::Graphics&) override;
};
} // namespace ui
