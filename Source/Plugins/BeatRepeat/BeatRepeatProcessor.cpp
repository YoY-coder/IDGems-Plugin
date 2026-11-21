#include "BeatRepeatProcessor.h"
#include "BeatRepeatEditor.h"

namespace idg
{
using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout BeatRepeatProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });

    l.add (std::make_unique<AudioParameterBool> (ParameterID { "active", 1 }, "Repeat", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "tempo", 1 }, "Tempo", LoopRepeat::tempoNames(), 8));
    {
        NormalisableRange<float> r (10.0f, 8000.0f); r.setSkewForCentre (500.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempoMs", 1 }, "Tempo (libre)", r, 1000.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed", NormalisableRange<float> (0.0f, 1.0f), 4.0f / 11.0f,
           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
           { return String (LoopRepeat::speedToMs (v), 1) + " ms @120"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "snap", 1 }, "Snap", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Smooth", NormalisableRange<float> (0.0f, 1.0f), 0.25f, pct));
    {
        NormalisableRange<float> r (0.0f, 2000.0f); r.setSkewForCentre (150.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide", r, 30.0f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ramp", 1 }, "Rampe", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
           { return (v > 0 ? "+" : "") + String (v * 3.0f, 2) + " oct"; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dir", 1 }, "Sens", StringArray { "Avant", "Aller-retour", IDG_T ("Arrière") }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

BeatRepeatProcessor::BeatRepeatProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsBeatRepeat", createLayout())
{
}

bool BeatRepeatProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    const auto in = l.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BeatRepeatProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    engine.prepare (sampleRate);
    scratch.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    setLatencySamples (0);
}

LoopRepeat::Settings BeatRepeatProcessor::readSettings() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    LoopRepeat::Settings s;
    s.active = v ("active") > 0.5f;
    s.sync = v ("sync") > 0.5f;
    s.tempoIndex = juce::roundToInt (v ("tempo"));
    s.tempoMs = v ("tempoMs");
    s.speed = v ("speed");
    s.snap = v ("snap") > 0.5f;
    s.smooth = v ("smooth");
    s.glideMs = v ("glide");
    s.ramp = v ("ramp");
    s.dir = juce::roundToInt (v ("dir"));
    s.mix = v ("mix");
    s.gainDb = v ("gain");
    return s;
}

void BeatRepeatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 1) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (auto ts = pos->getTimeSignature())
                qpb = juce::jmax (0.25, (double) ts->numerator * 4.0 / (double) juce::jmax (1, ts->denominator));
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
        }
    uiBpm = bpm;

    float* L = buffer.getWritePointer (0);
    float* R;
    if (buffer.getNumChannels() >= 2) R = buffer.getWritePointer (1);
    else
    {
        if ((int) scratch.size() < n) return; // ne devrait pas arriver (bloc plus grand qu'annoncé)
        std::copy (L, L + n, scratch.begin());
        R = scratch.data();
    }

    LoopRepeat::Transport t;
    t.bpm = bpm; t.ppq = ppq; t.playing = playing; t.quartersPerBar = qpb;
    engine.process (L, R, n, readSettings(), t);
}

void BeatRepeatProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, mb);
}

void BeatRepeatProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BeatRepeatProcessor::createEditor() { return new BeatRepeatEditor (*this); }
} // namespace idg

#if ! IDGEMS_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new idg::BeatRepeatProcessor(); }
#endif
