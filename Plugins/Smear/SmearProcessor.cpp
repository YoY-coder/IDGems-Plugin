#include "SmearProcessor.h"
#include "SmearEditor.h"

namespace idg
{
static juce::String timeText (float v)
{
    return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, v < 10.0f ? 2 : 1) + " s";
}

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });
    auto tm = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return timeText (v); });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
    auto tone = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        if (std::abs (v) < 0.02f) return String ("neutre");
        return (v > 0 ? "+" : "") + String (v * 3.0f, 1) + " dB/oct";
    });
    auto logRange = [] (float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
            [] (float a, float b, float v) { return a * std::pow (b / a, v); },
            [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); });
    };

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "smear", 1 }, "Smear", logRange (0.02f, 20.0f), 2.0f, tm));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "rise", 1 }, "Montee", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "focus", 1 }, "Focus", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drift", 1 }, "Drift", NormalisableRange<float> (0.0f, 1.0f), 0.25f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, tone));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Sortie", NormalisableRange<float> (-24.0f, 12.0f), 0.0f, db));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
    return l;
}

SmearProcessor::SmearProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsSmear", makeLayout())
{
    pSmear = apvts.getRawParameterValue ("smear");
    pRise = apvts.getRawParameterValue ("rise");
    pFocus = apvts.getRawParameterValue ("focus");
    pDrift = apvts.getRawParameterValue ("drift");
    pTone = apvts.getRawParameterValue ("tone");
    pMix = apvts.getRawParameterValue ("mix");
    pOut = apvts.getRawParameterValue ("out");
    pFreeze = apvts.getRawParameterValue ("freeze");
    engine.prepare (44100.0);
    setLatencySamples (engine.getLatency());
}

bool SmearProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void SmearProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());
}

SmearEngine::Settings SmearProcessor::currentSettings() const
{
    SmearEngine::Settings s;
    s.smearSec = pSmear->load();
    s.rise = pRise->load();
    s.focus = pFocus->load();
    s.drift = pDrift->load();
    s.tone = pTone->load();
    s.mix = pMix->load();
    s.outDb = pOut->load();
    s.freeze = pFreeze->load() > 0.5f;
    return s;
}

void SmearProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    if (n == 0 || numCh == 0) return;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    const auto s = currentSettings();
    float* L = buffer.getWritePointer (0);
    if (numCh > 1)
        engine.process (L, buffer.getWritePointer (1), n, s);
    else
    {
        float tmp[256];
        for (int pos = 0; pos < n; pos += 256)
        {
            const int m = juce::jmin (256, n - pos);
            std::copy (L + pos, L + pos + m, tmp);
            engine.process (L + pos, tmp, m, s);
        }
    }
}

juce::AudioProcessorEditor* SmearProcessor::createEditor() { return new SmearEditor (*this); }

void SmearProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", editorScale.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SmearProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        if (t.isValid() && t.hasType (apvts.state.getType()))
        {
            if (t.hasProperty ("uiScale")) editorScale = (float) t.getProperty ("uiScale");
            apvts.replaceState (t);
        }
    }
}
} // namespace idg

#if ! IDGEMS_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new idg::SmearProcessor(); }
#endif
