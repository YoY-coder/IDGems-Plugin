#pragma once
#include "Core/JuceIncludes.h"
#include "Effects/SmearEngine.h"

namespace idg
{
/** Petit VST3 autonome « IDGems Smear » : l'effet seul, pour le régler à l'oreille
    avant de le réintégrer dans le rack IDGems. */
class SmearProcessor : public juce::AudioProcessor
{
public:
    SmearProcessor();

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "IDGems Smear"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    SmearEngine::Settings currentSettings() const;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> editorScale { 0.0f };
    SmearEngine engine;

private:
    std::atomic<float>* pSmear = nullptr, *pRise = nullptr, *pFocus = nullptr, *pDrift = nullptr,
                      *pTone = nullptr, *pMix = nullptr, *pOut = nullptr, *pFreeze = nullptr;
};
} // namespace idg
