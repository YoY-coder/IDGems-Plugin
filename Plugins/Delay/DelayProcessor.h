#pragma once
#include "Effects/GlitchDelay.h"
#include "Engine/OutputChain.h"

namespace idg
{
/** IDGems Delay : module solo (un effet = un petit VST3). */
class DelayProcessor : public juce::AudioProcessor
{
public:
    DelayProcessor();

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "IDGems Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Random « glitch » : jamais de ternaire, feedback ≤ 100 %, mix jamais touché. */
    void randomize (juce::Random& rng);
    /** Revient au réglage d'avant le dernier random. */
    bool undoRandom();
    bool canUndo() const { return ! history.empty(); }

    /** Réglages actuels lus depuis les paramètres (n'importe quel thread). */
    GlitchDelay::Settings currentSettings() const;
    double currentBpm() const { return bpm.load(); }

    juce::AudioProcessorValueTreeState apvts;
    GlitchDelay delay;
    std::atomic<float> editorScale { 0.0f };   // taille de la fenêtre, sauvée avec le projet
    std::atomic<float> outPeak { 0.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void setParam (const juce::String& id, float plainValue);

    OutputChain out;
    std::atomic<double> bpm { 120.0 };
    std::vector<std::vector<float>> history;   // valeurs normalisées avant chaque random

    std::atomic<float>* pSync = nullptr, * pNote = nullptr, * pFeel = nullptr, * pTime = nullptr, * pFb = nullptr,
                      * pMode = nullptr, * pOffset = nullptr, * pLow = nullptr, * pHigh = nullptr, * pMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayProcessor)
};
} // namespace idg
