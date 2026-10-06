#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorEngine.h"
#include "Parameters.h"

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Strata"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (params::id::bypass); }

    cmp::MeterData& getMeterData() noexcept { return engine.getMeterData(); }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

private:
    cmp::Params readParams() const noexcept;

    cmp::CompressorEngine engine;
    // Ponteiros para os valores atômicos dos parâmetros (obtidos no construtor; lidos no audio thread sem locks)
    std::atomic<float> *pStrength, *pThreshold, *pRatio, *pAttack, *pRelease, *pKnee, *pMakeup, *pMix, *pInput, *pOutput,
                       *pMode, *pAutoAttack, *pAutoRelease, *pLookahead, *pScHpf, *pCharacter, *pSatMode, *pOversampling,
                       *pStereoLink, *pStereoMode, *pAutoGain, *pBypass, *pAdaptive;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
