#pragma once
#include "BeatRepeatProcessor.h"
#include "../../UI/LookAndFeel.h"

namespace idg
{
/** Potard néon (arc plein qui bloome avec la valeur) lié à un paramètre. */
class NeonKnob : public juce::Slider
{
public:
    NeonKnob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label,
              juce::Colour colour, bool big = false);
    void paint (juce::Graphics&) override;
    void setReadout (const juce::String& s) { if (s != readout) { readout = s; repaint(); } }

private:
    juce::RangedAudioParameter* param;
    juce::String label, readout;
    juce::Colour colour;
    bool big;
    juce::AudioProcessorValueTreeState::SliderAttachment att;
};

/** Bouton texte lié à un paramètre booléen ou à une valeur d'un choix. */
class ParamButton : public juce::Button
{
public:
    ParamButton (juce::RangedAudioParameter& p, const juce::String& text, int choiceValue, juce::Colour c);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void clicked() override;
    bool isLit() const;

private:
    juce::RangedAudioParameter& param;
    int choiceValue; // -1 = booléen (bascule)
    juce::Colour colour;
};

class BeatRepeatEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeatRepeatEditor (BeatRepeatProcessor&);
    ~BeatRepeatEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    juce::String speedText() const;

    BeatRepeatProcessor& proc;
    ui::IDGLookAndFeel laf;
    NeonKnob speed, tempo, tempoMs, smooth, glide, ramp, mix, gain;
    ParamButton active, sync, snap, fwd, pingpong, rev;
    juce::Rectangle<float> stripArea;
    float stepProgress = 0.0f, phase = 0.0f, loopMs = 0.0f;
    int cycle = 0;
};
} // namespace idg
