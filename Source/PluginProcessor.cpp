#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", params::createLayout())
{
    namespace id = params::id;
    auto p = [this] (const char* i) { return apvts.getRawParameterValue (i); };
    pStrength = p (id::strength);   pThreshold = p (id::threshold); pRatio = p (id::ratio);
    pAttack = p (id::attack);       pRelease = p (id::release);     pKnee = p (id::knee);
    pMakeup = p (id::makeup);       pMix = p (id::mix);             pInput = p (id::input);
    pOutput = p (id::output);       pMode = p (id::mode);           pAutoAttack = p (id::autoAttack);
    pAutoRelease = p (id::autoRelease); pLookahead = p (id::lookahead); pScHpf = p (id::scHpf);
    pCharacter = p (id::character); pSatMode = p (id::satMode);     pOversampling = p (id::oversampling);
    pStereoLink = p (id::stereoLink); pStereoMode = p (id::stereoMode);
    pAutoGain = p (id::autoGain);   pBypass = p (id::bypass);   pAdaptive = p (id::adaptive);
}

cmp::Params PluginProcessor::readParams() const noexcept
{
    auto idx = [] (const std::atomic<float>* a, int maxIdx) { return juce::jlimit (0, maxIdx, (int) std::lround (a->load())); };
    cmp::Params q;
    q.strength    = pStrength->load() * 0.01f;
    q.thresholdDb = pThreshold->load();
    q.ratio       = params::kRatios[idx (pRatio, 9)];
    q.attackMs    = pAttack->load();
    q.releaseMs   = pRelease->load();
    q.kneeDb      = pKnee->load();
    q.makeupDb    = pMakeup->load();
    q.mix         = pMix->load() * 0.01f;
    q.inputDb     = pInput->load();
    q.outputDb    = pOutput->load();
    q.mode        = (cmp::Mode) idx (pMode, 4);
    q.autoAttack  = pAutoAttack->load() > 0.5f;
    q.autoRelease = pAutoRelease->load() > 0.5f;
    q.lookaheadMs = params::kLookahead[idx (pLookahead, 4)];
    q.sidechainHpfHz = params::kScHpf[idx (pScHpf, 7)];
    q.character   = pCharacter->load() * 0.01f;
    q.satMode     = (cmp::SatMode) idx (pSatMode, 3);
    q.oversampling = params::kOversampling[idx (pOversampling, 3)];
    q.stereoLink  = params::kLink[idx (pStereoLink, 4)];
    q.stereoMode  = (cmp::StereoMode) idx (pStereoMode, 2);
    q.autoGain    = pAutoGain->load() > 0.5f;
    q.bypass      = pBypass->load() > 0.5f;
    q.adaptive    = pAdaptive->load() > 0.5f;
    return q;
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setParams (readParams());
    engine.reset();
    engine.setParams (readParams());
    setLatencySamples (engine.getLatencySamples());
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());

    const int lat = engine.getLatencySamples();       // muda só com Lookahead / Oversampling
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor (*this); }

void PluginProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PluginProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);
            // Migração V1 -> V2: projetos salvos antes do Adaptive Engine não têm o parâmetro "adaptive".
            // Eles voltam com o motor legado (STRENGTH 50% neutro), então o som do projeto antigo não muda.
            if (! state.getChildWithProperty ("id", params::id::adaptive).isValid())
                if (auto* a = apvts.getParameter (params::id::adaptive))
                    a->setValueNotifyingHost (0.0f);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PluginProcessor(); }
