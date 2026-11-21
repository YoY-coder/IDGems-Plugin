#include "GateProcessor.h"
#include "GateEditor.h"

namespace idg::gate
{
const int GateProcessor::gridSizes[] { 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };

const juce::StringArray& GateProcessor::lengthNames()
{
    static const juce::StringArray n { "1/16", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.",
                                       "1 mesure", "2 mesures", "4 mesures", "8 mesures" };
    return n;
}

double GateProcessor::lengthQuarters (int idx, double qpb)
{
    static const double q[] { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0 };
    if (idx >= 0 && idx < 10) return q[idx];
    static const double bars[] { 1.0, 2.0, 4.0, 8.0 };
    return qpb * bars[juce::jlimit (0, 3, idx - 10)];
}

juce::AudioProcessorValueTreeState::ParameterLayout GateProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    auto ms = [] (float v, int) { return v < 10.0f ? String (v, 2) + " ms" : String (v, 1) + " ms"; };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Tempo", "Hz" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, IDG_T ("Durée"), lengthNames(), 10));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "hz", 1 }, "Vitesse Hz",
                                                  NormalisableRange<float> (0.05f, 40.0f, 0.0f, 0.3f), 2.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (
                                                      [] (float v, int) { return String (v, v < 1.0f ? 3 : 2) + " Hz"; })));
    StringArray grids;
    for (int g : gridSizes) grids.add (String (g) + " pas");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "grid", 1 }, "Grille", grids, 6));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", NormalisableRange<float> (0.0f, 100.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "offset", 1 }, IDG_T ("Décalage"), NormalisableRange<float> (0.0f, 100.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "retrig", 1 }, IDG_T ("Redémarrage"),
                                                   StringArray { IDG_T ("Hôte"), "Note MIDI" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attaque",
                                                  NormalisableRange<float> (0.0f, 50.0f, 0.0f, 0.35f), 0.5f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, IDG_T ("Relâche"),
                                                  NormalisableRange<float> (0.0f, 200.0f, 0.0f, 0.35f), 3.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "depth", 1 }, "Profondeur", NormalisableRange<float> (0.0f, 100.0f), 100.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    return l;
}

GateProcessor::GateProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "IDGemsGate", createLayout())
{
    pMode = apvts.getRawParameterValue ("mode");
    pLength = apvts.getRawParameterValue ("length");
    pHz = apvts.getRawParameterValue ("hz");
    pGrid = apvts.getRawParameterValue ("grid");
    pSwing = apvts.getRawParameterValue ("swing");
    pOffset = apvts.getRawParameterValue ("offset");
    pRetrig = apvts.getRawParameterValue ("retrig");
    pAttack = apvts.getRawParameterValue ("attack");
    pRelease = apvts.getRawParameterValue ("release");
    pDepth = apvts.getRawParameterValue ("depth");
    pattern = GatePattern::preset (5);
    audioPattern = pattern;
    for (auto& a : inPeak) a = 0.0f;
    for (auto& a : outPeak) a = 0.0f;
}

bool GateProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return l.getMainInputChannelSet() == out;
}

void GateProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    gain = 1.0f;
    freePhase = 0.0;
    wasPlaying = false;
    lastBin = -1;
    depthSmooth.reset (sampleRate, 0.02);
    depthSmooth.setCurrentAndTargetValue (pDepth->load() * 0.01f);
}

int GateProcessor::gridSteps() const { return gridSizes[juce::jlimit (0, numGridSizes - 1, (int) pGrid->load())]; }

GatePattern GateProcessor::getPattern() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return pattern;
}

void GateProcessor::setPattern (const GatePattern& p)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pattern = p;
    }
    ++version;
}

void GateProcessor::pullPattern()
{
    const int v = version.load();
    if (v == audioVersion) return;
    // jamais d'attente dans le thread audio : si l'interface écrit en ce moment, on réessaie au bloc suivant
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked()) return;
    audioPattern = pattern;
    audioVersion = v;
    hint = 0;
}

float GateProcessor::phaseToGate (double phase, int grid, float swing)
{
    double x = phase - std::floor (phase);
    if (swing > 0.0f && grid >= 2)
    {
        // swing : le 1er pas de chaque paire dure plus longtemps que le 2e (50 % -> 75 %)
        const double u = x * grid;
        const double pair = std::floor (u * 0.5);
        if (2.0 * pair + 1.0 < (double) grid)
        {
            const double local = u - 2.0 * pair;                  // 0..2
            const double r = 0.5 + 0.25 * (double) swing;         // part du 1er pas
            const double w = local < 2.0 * r ? local / (2.0 * r) : 1.0 + (local - 2.0 * r) / (2.0 * (1.0 - r));
            x = (2.0 * pair + w) / grid;
        }
    }
    const float xf = (float) juce::jlimit (0.0, 1.0, x);
    uiPhase.store (xf, std::memory_order_relaxed);
    return xf;
}

void GateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels());
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    pullPattern();

    double bpm = 120.0, ppq = 0.0, qpb = 4.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (auto q = pos->getPpqPosition()) ppq = *q;
            if (auto ts = pos->getTimeSignature()) if (ts->numerator > 0 && ts->denominator > 0) qpb = ts->numerator * 4.0 / ts->denominator;
            playing = pos->getIsPlaying();
        }

    const bool tempo = (int) pMode->load() == ModeTempo;
    const bool midiRetrig = (int) pRetrig->load() == RetrigMidi;
    const double cycleQ = juce::jmax (1.0 / 16.0, lengthQuarters ((int) pLength->load(), qpb));
    const double hz = (double) pHz->load();
    const double qps = bpm / 60.0 / sr;                       // noires par échantillon
    const double cyclePerSample = tempo ? qps / cycleQ : hz / sr;
    const double offset = (double) pOffset->load() * 0.01;
    const int grid = gridSteps();
    const float swing = pSwing->load() * 0.01f;
    const float up = 1.0f / (float) juce::jmax (1.0, juce::jmax (0.1, (double) pAttack->load()) * 0.001 * sr);
    const float down = 1.0f / (float) juce::jmax (1.0, juce::jmax (0.1, (double) pRelease->load()) * 0.001 * sr);
    depthSmooth.setTargetValue (pDepth->load() * 0.01f);

    if (playing && ! wasPlaying) freePhase = 0.0;
    wasPlaying = playing;
    const bool hostClock = playing && ! midiRetrig;

    float* L = buffer.getWritePointer (0);
    float* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    auto mi = midi.begin();
    for (int i = 0; i < n; ++i)
    {
        for (; mi != midi.end() && (*mi).samplePosition <= i; ++mi)
            if (midiRetrig && (*mi).getMessage().isNoteOn()) freePhase = 0.0;

        double cyc;
        if (hostClock)
        {
            cyc = tempo ? (ppq + i * qps) / cycleQ : (ppq / bpm * 60.0 + i / sr) * hz;
            freePhase = cyc - std::floor (cyc);   // reprise sans saut si le transport s'arrête
        }
        else
        {
            cyc = freePhase;
            freePhase += cyclePerSample;
            freePhase -= std::floor (freePhase);
        }

        const float x = phaseToGate (cyc + offset, grid, swing);
        const float target = audioPattern.valueAt (x, hint);
        if (target > gain) gain = juce::jmin (target, gain + up);
        else gain = juce::jmax (target, gain - down);

        const float d = depthSmooth.getNextValue();
        const float g = 1.0f - d * (1.0f - gain);
        const float inL = L[i], inR = R != nullptr ? R[i] : inL;
        float oL = inL * g, oR = inR * g;
        if (! std::isfinite (oL)) oL = 0.0f;
        if (! std::isfinite (oR)) oR = 0.0f;
        L[i] = oL;
        if (R != nullptr) R[i] = oR;

        // affichage : crête d'entrée / sortie par case de la grille d'affichage
        const int bin = juce::jlimit (0, numBins - 1, (int) (x * numBins));
        if (bin != lastBin)
        {
            if (lastBin >= 0)
            {
                inPeak[(size_t) lastBin].store (accIn, std::memory_order_relaxed);
                outPeak[(size_t) lastBin].store (accOut, std::memory_order_relaxed);
            }
            lastBin = bin;
            accIn = accOut = 0.0f;
        }
        accIn = juce::jmax (accIn, std::abs (inL), std::abs (inR));
        accOut = juce::jmax (accOut, std::abs (oL), std::abs (oR));
    }
    uiGain.store (gain, std::memory_order_relaxed);
}

void GateProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("pattern", getPattern().toString(), nullptr);
    st.setProperty ("uiScale", (double) editorScale.load(), nullptr);
    st.setProperty ("editMode", editMode.load(), nullptr);
    st.setProperty ("snap", snap.load(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void GateProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (st.hasProperty ("pattern")) setPattern (GatePattern::fromString (st["pattern"].toString()));
    if (st.hasProperty ("uiScale")) editorScale = (float) (double) st["uiScale"];
    if (st.hasProperty ("editMode")) editMode = (int) st["editMode"];
    if (st.hasProperty ("snap")) snap = (bool) st["snap"];
    apvts.replaceState (st);
}

juce::AudioProcessorEditor* GateProcessor::createEditor() { return new GateEditor (*this); }
} // namespace idg::gate

#if ! IDGEMS_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new idg::gate::GateProcessor(); }
#endif
