#pragma once
#include "../Theme.h"

namespace idg::ui::solo
{
/** Potard néon (même rendu que le rack IDGems) lié directement à un paramètre de l'APVTS.
    Corps rond (raccord avec IDGems Beat Repeat), libellé tranché + valeur dessous. Double-clic : valeur par défaut. */
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

/** Interrupteur lié à un paramètre Bool (ex. BYPASS). */
class ToggleParam : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleParam (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text,
                 juce::Colour colour, const juce::String& help = {});
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    bool isOn() const { return on; }
    std::function<void()> onChange;

private:
    std::unique_ptr<juce::ParameterAttachment> att;
    juce::String text;
    juce::Colour colour;
    bool on = false, over = false;
};

/** Filtre graphique : courbe coupe-bas + coupe-haut (12 dB/oct), poignées à glisser.
    Courbe pâle = couleur après 4 passages (le filtre est dans la boucle du delay). */
class FilterGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    FilterGraph (juce::AudioProcessorValueTreeState& s, const juce::String& lowId, const juce::String& highId, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverHandle = -1; repaint(); }

private:
    juce::Rectangle<float> plotArea() const;
    float xOfHz (float hz) const;
    float hzOfX (float x) const;
    float value (int k) const;
    float responseDb (float hz) const;
    int handleAt (juce::Point<float> p) const;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    juce::RangedAudioParameter* params[2] {};
    std::unique_ptr<juce::ParameterAttachment> atts[2];
    juce::Colour colour;
    int dragging = -1, hoverHandle = -1;
};

/** Fond noir texturé + fissures incandescentes (DA Altered Brother), généré une fois. */
juce::Image makeSoloBackground (int w, int h, uint32_t seed);
} // namespace idg::ui::solo
