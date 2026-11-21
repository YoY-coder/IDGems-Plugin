#pragma once
#include "DelayProcessor.h"
#include "UI/Solo/SoloWidgets.h"
#include "UI/LookAndFeel.h"

namespace idg
{
/** Vue des échos : positions des répétitions sur une mesure, voies L / R, hauteur = niveau. */
class EchoView : public juce::Component
{
public:
    explicit EchoView (DelayProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();

private:
    DelayProcessor& proc;
    float glow = 0.0f;
};

class DelayEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DelayEditor (DelayProcessor&);
    ~DelayEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;

    static constexpr int baseW = 920, baseH = 470;

private:
    struct Content : public juce::Component
    {
        explicit Content (DelayProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        DelayProcessor& proc;
        juce::Image bg;
        EchoView echoes;
        ui::solo::ChoiceButtons sync, feel, mode;
        ui::solo::NeonKnob note, time, feedback, offset, mix;
        ui::solo::FilterGraph filter;
        ui::solo::ToggleParam bypass;
        ui::solo::FlatButton random { "RANDOM", ui::Theme::accent }, undo { IDG_T ("◀ RETOUR"), ui::Theme::blue };
        juce::Random rng;
        juce::Rectangle<float> pTime, pFb, pFilter, pStereo, pMix;
    };

    void timerCallback() override;

    DelayProcessor& proc;
    ui::IDGLookAndFeel lnf;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    bool sizeReady = false;
};
} // namespace idg
