#include "DelayProcessor.h"
#include "DelayEditor.h"

namespace idg
{
namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;

juce::NormalisableRange<float> centred (float mn, float mx, float centre)
{
    juce::NormalisableRange<float> r (mn, mx);
    r.setSkewForCentre (centre);
    return r;
}

juce::String msText (float v, int)
{
    return v >= 100.0f ? juce::String (v, 0) + " ms" : v >= 10.0f ? juce::String (v, 1) + " ms" : juce::String (v, 2) + " ms";
}
juce::String hzText (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}
juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
float pctValue (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; }
float numValue (const juce::String& t)
{
    float v = t.retainCharacters ("0123456789.-").getFloatValue();
    return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DelayProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto attrF = [] (auto toText, auto fromText)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText).withValueFromStringFunction (fromText);
    };
    juce::StringArray notes;
    for (int i = 0; i < GlitchDelay::numNotes; ++i) notes.add (GlitchDelay::noteName (i));

    l.add (std::make_unique<APB> (juce::ParameterID { "sync", 1 }, "Sync", true,
                                  juce::AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool b, int) { return juce::String (b ? "SYNC" : "FREE"); })));
    l.add (std::make_unique<APC> (juce::ParameterID { "note", 1 }, "Division", notes, 3));
    l.add (std::make_unique<APC> (juce::ParameterID { "feel", 1 }, "Type", juce::StringArray { "Droit", IDG_T ("Pointé"), "Ternaire" }, 0));
    l.add (std::make_unique<APF> (juce::ParameterID { "time", 1 }, "Temps (free)", centred (GlitchDelay::minTimeMs, GlitchDelay::maxTimeMs, 35.0f), 60.0f,
                                  attrF (msText, [] (const juce::String& t) { return numValue (t); })));
    l.add (std::make_unique<APF> (juce::ParameterID { "feedback", 1 }, "Feedback", juce::NormalisableRange<float> (0.0f, 1.1f), 0.45f,
                                  attrF (pctText, pctValue)));
    l.add (std::make_unique<APC> (juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { IDG_T ("Stéréo"), "Ping-pong", "Mono" }, 0));
    l.add (std::make_unique<APF> (juce::ParameterID { "offset", 1 }, IDG_T ("Décalage stéréo"),
                                  juce::NormalisableRange<float> (-GlitchDelay::maxOffsetMs, GlitchDelay::maxOffsetMs, 0.0f, 0.5f, true), 0.0f,
                                  attrF ([] (float v, int) { return std::abs (v) < 0.05f ? juce::String ("0 ms") : (v > 0 ? "R +" : "L +") + juce::String (std::abs (v), 1) + " ms"; },
                                         [] (const juce::String& t) { return numValue (t); })));
    l.add (std::make_unique<APF> (juce::ParameterID { "lowcut", 1 }, "Coupe-bas", centred (20.0f, 5000.0f, 300.0f), 20.0f,
                                  attrF (hzText, [] (const juce::String& t) { return numValue (t); })));
    l.add (std::make_unique<APF> (juce::ParameterID { "highcut", 1 }, "Coupe-haut", centred (200.0f, 20000.0f, 2500.0f), 20000.0f,
                                  attrF (hzText, [] (const juce::String& t) { return numValue (t); })));
    l.add (std::make_unique<APF> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                  attrF (pctText, pctValue)));
    return l;
}

DelayProcessor::DelayProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsDelay", createLayout())
{
    pSync = apvts.getRawParameterValue ("sync");
    pNote = apvts.getRawParameterValue ("note");
    pFeel = apvts.getRawParameterValue ("feel");
    pTime = apvts.getRawParameterValue ("time");
    pFb = apvts.getRawParameterValue ("feedback");
    pMode = apvts.getRawParameterValue ("mode");
    pOffset = apvts.getRawParameterValue ("offset");
    pLow = apvts.getRawParameterValue ("lowcut");
    pHigh = apvts.getRawParameterValue ("highcut");
    pMix = apvts.getRawParameterValue ("mix");
}

bool DelayProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), o = l.getMainOutputChannelSet();
    if (o != juce::AudioChannelSet::stereo() && o != juce::AudioChannelSet::mono()) return false;
    return in == o || in == juce::AudioChannelSet::mono();
}

void DelayProcessor::prepareToPlay (double sampleRate, int)
{
    delay.prepare (sampleRate);
    out.prepare (sampleRate);
    setLatencySamples (out.latency (false));
}

GlitchDelay::Settings DelayProcessor::currentSettings() const
{
    GlitchDelay::Settings s;
    s.sync = pSync->load() > 0.5f;
    s.note = (int) std::lround (pNote->load());
    s.feel = (int) std::lround (pFeel->load());
    s.timeMs = pTime->load();
    s.feedback = pFb->load();
    s.mode = (int) std::lround (pMode->load());
    s.offsetMs = pOffset->load();
    s.lowCut = pLow->load();
    s.highCut = juce::jmax (s.lowCut, pHigh->load());
    s.mix = pMix->load();
    s.bpm = bpm.load();
    return s;
}

void DelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = buffer.getNumChannels();
    if (nOut == 0 || n == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) if (*b > 1.0) bpm = *b;

    float* L = buffer.getWritePointer (0);
    float* R = nOut > 1 ? buffer.getWritePointer (1) : nullptr;
    // entrée mono : on duplique ; sortie mono : on traite dans un tampon temporaire de 32 échantillons
    if (R != nullptr && nIn == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = nIn; c < nOut; ++c) if (c > 1) buffer.clear (c, 0, n);

    const auto s = currentSettings();
    OutputChain::Settings os;
    os.hpFreq = 5.0f; os.monoOn = false; os.softClip = false; os.ceilingDb = -0.3f; os.releaseMs = 60.0f;

    float tmpR[32];
    float pk = 0.0f;
    for (int i = 0; i < n; i += 32)
    {
        const int m = juce::jmin (32, n - i);
        float* l = L + i;
        float* r = R != nullptr ? R + i : tmpR;
        if (R == nullptr) std::copy (l, l + m, tmpR);
        delay.process (l, r, m, s);
        out.process (l, r, m, os);
        for (int k = 0; k < m; ++k) pk = juce::jmax (pk, std::abs (l[k]), std::abs (r[k]));
        if (R == nullptr) for (int k = 0; k < m; ++k) l[k] = 0.5f * (l[k] + r[k]);
    }
    if (pk > outPeak.load()) outPeak = pk;
}

// ---------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------
void DelayProcessor::setParam (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void DelayProcessor::randomize (juce::Random& rng)
{
    std::vector<float> snap;
    for (auto* p : getParameters()) snap.push_back (p->getValue());
    history.push_back (std::move (snap));
    if (history.size() > 32) history.erase (history.begin());

    auto between = [&] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
    auto logBetween = [&] (float a, float b) { return std::exp (between (std::log (a), std::log (b))); };

    const bool sync = rng.nextFloat() < 0.7f;
    setParam ("sync", sync ? 1.0f : 0.0f);
    // division : 1/64 à 1/4, plus souvent courte ; jamais de ternaire
    static const float noteWeights[GlitchDelay::numNotes] { 0.05f, 0.15f, 0.25f, 0.27f, 0.18f, 0.10f };
    float pick = rng.nextFloat(), acc = 0.0f;
    int note = GlitchDelay::numNotes - 1;
    for (int i = 0; i < GlitchDelay::numNotes; ++i) { acc += noteWeights[i]; if (pick < acc) { note = i; break; } }
    setParam ("note", (float) note);
    const bool dotted = GlitchDelay::dottedAvailable (note) && rng.nextFloat() < 0.25f;
    setParam ("feel", dotted ? (float) GlitchDelay::Dotted : (float) GlitchDelay::Straight);
    setParam ("time", logBetween (1.0f, 180.0f));
    setParam ("feedback", between (0.0f, 1.0f));          // jamais au-dessus de 100 %
    setParam ("mode", (float) rng.nextInt (3));
    setParam ("offset", rng.nextFloat() < 0.5f ? 0.0f : between (-8.0f, 8.0f));
    const float low = rng.nextFloat() < 0.35f ? 20.0f : logBetween (40.0f, 1500.0f);
    const float high = rng.nextFloat() < 0.35f ? 20000.0f : logBetween (juce::jmax (1200.0f, low * 3.0f), 20000.0f);
    setParam ("lowcut", low);
    setParam ("highcut", high);
    // mix : jamais randomisé
}

bool DelayProcessor::undoRandom()
{
    if (history.empty()) return false;
    auto snap = history.back();
    history.pop_back();
    auto params = getParameters();
    for (int i = 0; i < params.size() && i < (int) snap.size(); ++i)
    {
        params[i]->beginChangeGesture();
        params[i]->setValueNotifyingHost (snap[(size_t) i]);
        params[i]->endChangeGesture();
    }
    return true;
}

// ---------------------------------------------------------------------------
// État
// ---------------------------------------------------------------------------
void DelayProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", editorScale.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void DelayProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            if (t.hasProperty ("uiScale")) editorScale = (float) t.getProperty ("uiScale");
            apvts.replaceState (t);
        }
}

juce::AudioProcessorEditor* DelayProcessor::createEditor() { return new DelayEditor (*this); }
} // namespace idg

#if ! defined (IDGEMS_TESTS)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new idg::DelayProcessor(); }
#endif
