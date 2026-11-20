#pragma once
#include "StretchProcessor.h"

namespace idg
{
/** Potards « arc néon » (même esprit que le rack IDGems). */
class StretchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StretchLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

/** Frise des pas : partie jouée (enregistrement de la frame), partie figée, partie rendue au son direct. */
class StepStrip : public juce::Component
{
public:
    explicit StepStrip (StretchProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    StretchProcessor& proc;
};

class StretchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StretchEditor (StretchProcessor&);
    ~StretchEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateRateButtons();

    static constexpr int baseW = 720, baseH = 400;

    StretchProcessor& proc;
    StretchLookAndFeel lnf;

    struct Content : public juce::Component
    {
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::OwnedArray<juce::TextButton> rateButtons, modeButtons;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label> labels;
        std::unique_ptr<StepStrip> strip;
    } content;

    std::unique_ptr<juce::ParameterAttachment> rateAtt, modeAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtts;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    int currentRate = -1, currentMode = -1;
    bool sizeReady = false;
};
} // namespace idg
