#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "CustomLookAndFeel.h"
#include "Meter.h"

namespace ui
{
// GAIN REDUCTION: histórico discreto (barras finas, como um equipamento premium) + leitura "GR 4.7 dB".
// Representa o GR REAL do motor; o suavizamento aqui é só visual (independente do áudio).
class GainReductionView : public juce::Component
{
public:
    GainReductionView() { model.configure (40.0f, 0.0f, 0.0f); hist.fill (0.0f); }
    void push (float grDb, float dtSec);          // chamado a 30 Hz pelo editor
    void paint (juce::Graphics&) override;
private:
    static constexpr int kBars = 160;
    std::array<float, kBars> hist;
    int head = 0;
    cmp::MeterModel model;
};

// INPUT / OUTPUT: barras finas com escala (-60 .. +3 dB), peak hold e CLIP (clique para limpar).
class IoMeterView : public juce::Component
{
public:
    IoMeterView() { in.configure (24.0f, 1.5f, -60.0f); out.configure (24.0f, 1.5f, -60.0f); }
    void push (float inLin, float outLin, float dtSec);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { in.clearClip(); out.clearClip(); repaint(); }
private:
    void drawBar (juce::Graphics&, juce::Rectangle<float>, const juce::String&, const cmp::MeterModel&);
    cmp::MeterModel in, out;
};
} // namespace ui
