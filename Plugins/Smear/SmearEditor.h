#pragma once
#include "SmearProcessor.h"

namespace idg
{
/** Potards « arc néon » (même esprit que le rack IDGems). */
class SmearLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SmearLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

/** Sonogramme défilant : entrée (bleu, en haut) et sortie (orange, en bas).
    On y voit les attaques et les trous de l'entrée se dissoudre en lignes continues. */
class SmearScope : public juce::Component
{
public:
    explicit SmearScope (SmearProcessor& p);
    void paint (juce::Graphics&) override;
    void advance();

private:
    static juce::Colour heat (float v, bool out);
    SmearProcessor& proc;
    static constexpr int cols = 300;
    juce::Image imgIn { juce::Image::RGB, cols, SmearEngine::dispBins, true };
    juce::Image imgOut { juce::Image::RGB, cols, SmearEngine::dispBins, true };
};

class SmearEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SmearEditor (SmearProcessor&);
    ~SmearEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() { timerCallback(); } // capture d'interface hors ligne

private:
    void timerCallback() override;

    static constexpr int baseW = 780, baseH = 470;

    SmearProcessor& proc;
    SmearLookAndFeel lnf;

    struct Content : public juce::Component
    {
        void paint (juce::Graphics&) override;
        void resized() override;
        struct K { const char* id; const char* label; std::unique_ptr<juce::Slider> s; juce::Rectangle<int> area; };
        std::vector<K> knobs;
        juce::TextButton freeze { "FREEZE" };
        std::unique_ptr<SmearScope> scope;
    } content;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    bool sizeReady = false;
};
} // namespace idg
