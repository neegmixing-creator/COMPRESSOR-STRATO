#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorEngine.h"

namespace params
{
namespace id
{
    inline constexpr const char* strength = "strength";    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio = "ratio";          inline constexpr const char* attack = "attack";
    inline constexpr const char* release = "release";      inline constexpr const char* knee = "knee";
    inline constexpr const char* makeup = "makeup";        inline constexpr const char* mix = "mix";
    inline constexpr const char* input = "input";          inline constexpr const char* output = "output";
    inline constexpr const char* mode = "mode";            inline constexpr const char* autoAttack = "autoAttack";
    inline constexpr const char* autoRelease = "autoRelease"; inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* scHpf = "scHpf";          inline constexpr const char* character = "character";
    inline constexpr const char* satMode = "satMode";      inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* stereoLink = "stereoLink"; inline constexpr const char* stereoMode = "stereoMode";
    inline constexpr const char* autoGain = "autoGain";    inline constexpr const char* bypass = "bypass";
    inline constexpr const char* adaptive = "adaptive";    // V2: motor adaptativo (ausente em projetos antigos => migrado para OFF)
}

inline constexpr float kRatios[]    = { 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 20.f, 1000.f };   // último = ∞
inline constexpr float kLookahead[] = { 0.f, 0.5f, 1.f, 2.f, 5.f };
inline constexpr float kScHpf[]     = { 0.f, 30.f, 60.f, 90.f, 120.f, 180.f, 250.f, 400.f };
inline constexpr int   kOversampling[] = { 1, 2, 4, 8 };
inline constexpr float kLink[]      = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;
    using NR  = juce::NormalisableRange<float>;
    using PID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto fl = [&] (const char* i, const char* n, NR r, float def, const char* unit)
    { l.add (std::make_unique<APF> (PID { i, 1 }, n, r, def, juce::AudioParameterFloatAttributes().withLabel (unit))); };
    auto ch = [&] (const char* i, const char* n, juce::StringArray s, int def)
    { l.add (std::make_unique<APC> (PID { i, 1 }, n, s, def)); };
    auto bl = [&] (const char* i, const char* n, bool def) { l.add (std::make_unique<APB> (PID { i, 1 }, n, def)); };

    fl (id::strength,  "Strength",  NR (0.f, 100.f, 0.1f), 50.f, "%");
    fl (id::threshold, "Threshold", NR (-60.f, 0.f, 0.1f), -18.f, "dB");
    ch (id::ratio,     "Ratio", { "1:1", "1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1", "inf:1" }, 4);
    fl (id::attack,    "Attack",    NR (0.05f, 100.f, 0.01f, 0.3f), 10.f, "ms");
    fl (id::release,   "Release",   NR (10.f, 2000.f, 1.f, 0.35f), 120.f, "ms");
    fl (id::knee,      "Knee",      NR (0.f, 24.f, 0.1f), 6.f, "dB");
    fl (id::makeup,    "Makeup",    NR (-12.f, 24.f, 0.1f), 0.f, "dB");
    fl (id::mix,       "Mix",       NR (0.f, 100.f, 0.1f), 100.f, "%");
    fl (id::input,     "Input",     NR (-24.f, 24.f, 0.1f), 0.f, "dB");
    fl (id::output,    "Output",    NR (-24.f, 24.f, 0.1f), 0.f, "dB");
    ch (id::mode,      "Mode", { "Clean", "Punch", "Smooth", "Aggressive", "Bus" }, 0);
    bl (id::autoAttack,  "Auto Attack", true);     // V2: padrão ligado (o motor adaptativo é a identidade do plugin)
    bl (id::autoRelease, "Auto Release", true);
    ch (id::lookahead, "Lookahead", { "0 ms", "0.5 ms", "1 ms", "2 ms", "5 ms" }, 0);
    ch (id::scHpf,     "Sidechain HPF", { "Off", "30 Hz", "60 Hz", "90 Hz", "120 Hz", "180 Hz", "250 Hz", "400 Hz" }, 0);
    fl (id::character, "Character", NR (0.f, 100.f, 0.1f), 0.f, "%");
    ch (id::satMode,   "Character Mode", { "Clean", "Tube", "Tape", "Modern" }, 0);
    ch (id::oversampling, "Oversampling", { "Off", "2x", "4x", "8x" }, 0);
    ch (id::stereoLink, "Stereo Link", { "0%", "25%", "50%", "75%", "100%" }, 4);
    ch (id::stereoMode, "Stereo Mode", { "Stereo", "Mid", "Side" }, 0);
    bl (id::autoGain, "Auto Gain", false);
    bl (id::bypass,   "Bypass", false);
    bl (id::adaptive, "Adaptive Engine", true);
    return l;
}
} // namespace params
