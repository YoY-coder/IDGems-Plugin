#pragma once
#include "GatePattern.h"

namespace idg::gate
{
/** IDGems Gate : gate rythmique à motif dessinable (façon ShaperBox / Filtrator / Transgate). */
class GateProcessor : public juce::AudioProcessor
{
public:
    enum RateMode { ModeTempo = 0, ModeHz };
    enum Retrig { RetrigHost = 0, RetrigMidi };

    GateProcessor();
    ~GateProcessor() override = default;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "IDGems Gate"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- motif (thread interface : lecture / écriture ; thread audio : copie non bloquante)
    GatePattern getPattern() const;
    void setPattern (const GatePattern&);
    int patternVersion() const { return version.load(); }

    // --- réglages des listes
    static const juce::StringArray& lengthNames();
    static double lengthQuarters (int idx, double quartersPerBar);
    static const int gridSizes[];
    static constexpr int numGridSizes = 11;
    int gridSteps() const;

    juce::AudioProcessorValueTreeState apvts;

    // --- affichage (écrit par le thread audio, lu par l'interface)
    static constexpr int numBins = 256;
    std::array<std::atomic<float>, numBins> inPeak {}, outPeak {};
    std::atomic<float> uiPhase { 0.0f }, uiGain { 1.0f };

    // --- interface (sauvegardé avec le projet)
    std::atomic<float> editorScale { 0.0f };
    std::atomic<int> editMode { 0 };   // 0 = pas, 1 = courbe
    std::atomic<bool> snap { true };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void pullPattern();
    float phaseToGate (double phase, int grid, float swing);

    mutable juce::SpinLock lock;
    GatePattern pattern;              // protégé par lock
    std::atomic<int> version { 1 };

    GatePattern audioPattern;         // copie du thread audio
    int audioVersion = 0;
    int hint = 0;

    std::atomic<float>* pMode = nullptr; std::atomic<float>* pLength = nullptr; std::atomic<float>* pHz = nullptr;
    std::atomic<float>* pGrid = nullptr; std::atomic<float>* pSwing = nullptr; std::atomic<float>* pOffset = nullptr;
    std::atomic<float>* pRetrig = nullptr; std::atomic<float>* pAttack = nullptr; std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pDepth = nullptr;

    double sr = 44100.0;
    double freePhase = 0.0;           // horloge interne (transport arrêté, note MIDI, mode Hz)
    bool wasPlaying = false;
    float gain = 1.0f;
    juce::SmoothedValue<float> depthSmooth;
    int lastBin = -1;
    float accIn = 0.0f, accOut = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateProcessor)
};
} // namespace idg::gate
