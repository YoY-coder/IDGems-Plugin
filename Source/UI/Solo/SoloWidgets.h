#pragma once
#include "../Theme.h"

namespace idg::ui::solo
{
/** Potard néon (même rendu que le rack IDGems) lié directement à un paramètre de l'APVTS.
    Libellé au-dessus, valeur en dessous (toujours visible). Double-clic : valeur par défaut. */
class NeonKnob : public juce::Slider
{
public:
    NeonKnob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, juce::Colour colour,
              const juce::String& label, const juce::String& help = {});
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override { return help; }
    /** Timer de l'éditeur : scintillement du néon. */
    void tick();
    void setBig (bool b) { big = b; }
    /** Texte de valeur personnalisé (sinon celui du paramètre). */
    std::function<juce::String()> valueText;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    juce::Colour colour;
    juce::String label, help;
    bool big = false;
    float flicker = 0.0f;
    juce::Random rng;
};

/** Rangée de boutons exclusifs liée à un paramètre Choice ou Bool (ex. STÉRÉO | PING-PONG | MONO). */
class ChoiceButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceButtons (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, juce::StringArray labels,
                   juce::Colour colour, const juce::String& help = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int selected() const { return current; }
    /** Option grisée (ex. pointé indisponible sur 1/4). */
    std::function<bool (int)> isDisabled;
    std::function<void()> onChange;

private:
    int indexAt (juce::Point<int> p) const;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att;
    juce::StringArray labels;
    juce::Colour colour;
    juce::String help;
    int current = 0, hover = -1;
};

/** Bouton texte simple au style IDGems (RANDOM, RETOUR...). */
class FlatButton : public juce::Component
{
public:
    FlatButton (const juce::String& t, juce::Colour c) : text (t), colour (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        down = false; repaint();
        if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    std::function<void()> onClick;
    juce::String text;

private:
    juce::Colour colour;
    bool over = false, down = false;
};

/** Fond noir texturé + fissures incandescentes (DA Altered Brother), généré une fois. */
juce::Image makeSoloBackground (int w, int h, uint32_t seed);
} // namespace idg::ui::solo
