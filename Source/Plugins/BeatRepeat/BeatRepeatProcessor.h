#pragma once
#include "../../Effects/LoopRepeat.h"

namespace idg
{
/** Plugin autonome « IDGems Beat Repeat » : banc d'essai du moteur LoopRepeat avant son retour dans le rack. */
class BeatRepeatProcessor : public juce::AudioProcessor
{
public:
    BeatRepeatProcessor();

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "IDGems Beat Repeat"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    /** Lit les paramètres courants (utilisé aussi par l'outil de rendu). */
    LoopRepeat::Settings readSettings() const;

    juce::AudioProcessorValueTreeState apvts;
    LoopRepeat engine;
    std::atomic<double> uiBpm { 120.0 };

private:
    std::vector<float> scratch;
    double bpm = 120.0, ppq = 0.0, qpb = 4.0;
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeatRepeatProcessor)
};
} // namespace idg
