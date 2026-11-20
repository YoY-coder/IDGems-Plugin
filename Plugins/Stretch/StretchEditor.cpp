#include "StretchEditor.h"
#include "UI/Theme.h"

namespace idg
{
using ui::Theme;

namespace
{
const juce::Colour kGlitch { 0xffff6a00 }; // famille Glitch : orange

struct KnobInfo { const char* id; const char* label; const char* help; };
const KnobInfo kKnobs[] {
    { "grain",   "GRAIN",   "Taille de la frame figée. Spectral : petit = plus granuleux et transitoire, grand = nappe lisse et précise. Grains : petit = buzz métallique." },
    { "texture", "TEXTURE", "0 = gel net. Plus haut : la frame analysée bouge dans la zone figée et les phases sont bousculées : nappe vivante, granuleuse." },
    { "speed",   "VITESSE", "Avance pendant le gel. 0 = gelé net, 25 % = étiré x4, 100 % = temps réel granulaire." },
    { "hold",    "HOLD",    "Part du pas qui reste figée. Le reste repasse le son direct (gel rythmique haché)." },
    { "mix",     "MIX",     "Dosage sec / traité (100 % par défaut)." },
};
} // namespace

// ---------------------------------------------------------------------------
StretchLookAndFeel::StretchLookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, Theme::panelHi);
    setColour (juce::TooltipWindow::textColourId, Theme::text);
    setColour (juce::TooltipWindow::outlineColourId, kGlitch.withAlpha (0.6f));
}

void StretchLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    // place pour le halo et la valeur affichée dessous
    r = r.withTrimmedBottom (24.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight()) * 0.84f;
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f - 4.0f;
    const float thick = juce::jmax (3.0f, size * 0.07f);
    const float ang = a0 + pos * (a1 - a0);

    // corps
    g.setColour (Theme::panelHi);
    g.fillEllipse (r.reduced (thick + 3.0f));
    g.setColour (Theme::line);
    g.drawEllipse (r.reduced (thick + 3.0f), 1.0f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
    g.setColour (Theme::line);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, ang, true);
        // halo qui grossit avec la valeur
        g.setColour (kGlitch.withAlpha (0.10f + 0.20f * pos));
        g.strokePath (arc, juce::PathStrokeType (thick * (2.2f + 1.5f * pos), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kGlitch);
        g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.35f + 0.4f * pos));
        g.strokePath (arc, juce::PathStrokeType (thick * 0.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // index
    const juce::Point<float> p1 = c.getPointOnCircumference (rad * 0.25f, ang), p2 = c.getPointOnCircumference (rad * 0.62f, ang);
    g.setColour (Theme::text);
    g.drawLine ({ p1, p2 }, 2.0f);

    // valeur au centre
    g.setColour (Theme::text);
    g.setFont (Theme::mono (14.0f, true));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> (c.x - 60.0f, r.getBottom() + 2.0f, 120.0f, 22.0f),
                juce::Justification::centred, false);
}

void StretchLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    if (on)
    {
        g.setColour (kGlitch.withAlpha (0.25f));
        g.fillRect (r.expanded (1.5f));
        g.setColour (kGlitch);
        g.fillRect (r);
    }
    else
    {
        g.setColour (over ? Theme::panelHi.brighter (0.1f) : Theme::panelHi);
        g.fillRect (r);
        g.setColour (Theme::line);
        g.drawRect (r, 1.0f);
    }
}

void StretchLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? juce::Colours::black : Theme::text);
    g.setFont (Theme::mono (juce::jmin (13.0f, b.getHeight() * 0.42f), true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
void StepStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bg.brighter (0.03f));
    g.fillRect (r);

    constexpr int steps = 4;
    const float hold = proc.apvts.getRawParameterValue ("hold")->load();
    const float cap = juce::jlimit (0.0f, 1.0f, proc.uiCapture.load());
    const float phase = proc.uiPhase.load();
    const int cur = ((proc.uiStep.load() % steps) + steps) % steps;
    const float sw = r.getWidth() / steps;

    for (int i = 0; i < steps; ++i)
    {
        auto s = juce::Rectangle<float> (r.getX() + i * sw, r.getY(), sw, r.getHeight()).reduced (3.0f, 6.0f);
        const bool active = i == cur;
        // partie jouée (la frame s'enregistre)
        auto play = s.withWidth (s.getWidth() * cap);
        g.setColour (Theme::text.withAlpha (active ? 0.35f : 0.15f));
        g.fillRect (play);
        // partie figée
        if (hold > cap)
        {
            auto fr = juce::Rectangle<float> (s.getX() + s.getWidth() * cap, s.getY(), s.getWidth() * (hold - cap), s.getHeight());
            g.setColour (kGlitch.withAlpha (active ? 0.85f : 0.35f));
            g.fillRect (fr);
            // hachures « frame figée »
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            for (float xx = fr.getX() + 4.0f; xx < fr.getRight(); xx += 7.0f)
                g.fillRect (xx, fr.getY(), 1.5f, fr.getHeight());
        }
        // reste : son direct
        if (hold < 1.0f)
        {
            const float x0 = s.getX() + s.getWidth() * juce::jmax (hold, cap);
            g.setColour (Theme::text.withAlpha (active ? 0.25f : 0.10f));
            g.fillRect (juce::Rectangle<float> (x0, s.getY(), s.getRight() - x0, s.getHeight()));
        }
        g.setColour (active ? kGlitch : Theme::line);
        g.drawRect (s, 1.0f);
        if (active)
        {
            const float px = s.getX() + s.getWidth() * phase;
            g.setColour (juce::Colours::white);
            g.fillRect (px - 1.0f, s.getY() - 4.0f, 2.0f, s.getHeight() + 8.0f);
        }
    }

}

// ---------------------------------------------------------------------------
void StretchEditor::Content::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.fillAll (Theme::bg);
    // braises / fissure discrète en fond
    juce::ColourGradient glow (kGlitch.withAlpha (0.16f), r.getRight(), r.getBottom(), Theme::bg.withAlpha (0.0f), r.getCentreX(), r.getY(), true);
    g.setGradientFill (glow);
    g.fillRect (r);
    juce::Path crack;
    crack.startNewSubPath (r.getRight() - 250.0f, r.getBottom());
    crack.lineTo (r.getRight() - 205.0f, r.getBottom() - 9.0f);
    crack.lineTo (r.getRight() - 150.0f, r.getBottom() - 4.0f);
    crack.lineTo (r.getRight() - 90.0f, r.getBottom() - 14.0f);
    crack.lineTo (r.getRight(), r.getBottom() - 8.0f);
    g.setColour (kGlitch.withAlpha (0.12f));
    g.strokePath (crack, juce::PathStrokeType (5.0f));
    g.setColour (kGlitch.withAlpha (0.45f));
    g.strokePath (crack, juce::PathStrokeType (1.2f));

    Theme::cutText (g, "STRETCH", { 22.0f, 14.0f, 360.0f, 40.0f }, 30.0f, Theme::text, juce::Justification::centredLeft, 0.5f, true);
    g.setFont (Theme::mono (11.0f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("IDGEMS · GEL RYTHMIQUE DE LA FRAME"), juce::Rectangle<float> (24.0f, 54.0f, 400.0f, 14.0f), juce::Justification::centredLeft, false);

    g.setColour (Theme::textDim);
    g.drawText ("RATE", juce::Rectangle<float> (24.0f, 78.0f, 80.0f, 14.0f), juce::Justification::centredLeft, false);
    // légende de la frise
    struct Leg { const char* t; juce::Colour c; };
    const Leg legs[] { { "JOUÉ", Theme::text.withAlpha (0.35f) }, { "FIGÉ", kGlitch }, { "DIRECT", Theme::text.withAlpha (0.18f) } };
    float lx = r.getRight() - 24.0f - 3 * 74.0f;
    for (auto& l : legs)
    {
        g.setColour (l.c);
        g.fillRect (lx, 81.0f, 9.0f, 9.0f);
        g.setColour (Theme::textDim);
        g.drawText (IDG_T (l.t), juce::Rectangle<float> (lx + 14.0f, 78.0f, 58.0f, 14.0f), juce::Justification::centredLeft, false);
        lx += 74.0f;
    }
}

void StretchEditor::Content::resized()
{
    auto r = getLocalBounds().reduced (24, 0);
    r.removeFromTop (94);
    auto rateRow = r.removeFromTop (30);
    // choix du moteur, en haut à droite
    for (int i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setBounds (getWidth() - 24 - (modeButtons.size() - i) * 112, 24, 106, 28);
    const int bw = rateRow.getWidth() / juce::jmax (1, rateButtons.size());
    for (auto* b : rateButtons) b->setBounds (rateRow.removeFromLeft (bw));
    r.removeFromTop (8);
    if (strip) strip->setBounds (r.removeFromTop (54));
    r.removeFromTop (10);
    auto knobRow = r.removeFromTop (r.getHeight() - 14);
    const int kw = knobRow.getWidth() / juce::jmax (1, knobs.size());
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto k = knobRow.removeFromLeft (kw);
        labels[i]->setBounds (k.removeFromTop (18));
        // le Grain (le réglage principal) est plus gros
        knobs[i]->setBounds (i == 0 ? k : k.reduced (8, 10));
    }
}

StretchEditor::StretchEditor (StretchProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content.strip = std::make_unique<StepStrip> (p);
    content.addAndMakeVisible (*content.strip);

    const auto& names = StretchProcessor::rateNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = content.rateButtons.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { if (rateAtt) rateAtt->setValueAsCompleteGesture ((float) i); };
        b->setTooltip (IDG_T ("Fréquence du gel : à chaque pas, le son joue le temps d'un grain puis se fige."));
        content.addAndMakeVisible (b);
    }
    rateAtt = std::make_unique<juce::ParameterAttachment> (
        *proc.apvts.getParameter ("rate"), [this] (float v) { currentRate = (int) std::lround (v); updateRateButtons(); });
    rateAtt->sendInitialUpdate();

    const juce::StringArray modes { "SPECTRAL", "GRAINS" };
    const char* modeHelp[] { "Spectral : chaque partiel continue à sa vraie hauteur, les notes d'origine restent, étirées.",
                             "Grains : boucle de grains. Les grains courts imposent leur propre hauteur (buzz métallique)." };
    for (int i = 0; i < modes.size(); ++i)
    {
        auto* b = content.modeButtons.add (new juce::TextButton (modes[i]));
        b->onClick = [this, i] { if (modeAtt) modeAtt->setValueAsCompleteGesture ((float) i); };
        b->setTooltip (IDG_T (modeHelp[i]));
        content.addAndMakeVisible (b);
    }
    modeAtt = std::make_unique<juce::ParameterAttachment> (
        *proc.apvts.getParameter ("mode"), [this] (float v) { currentMode = (int) std::lround (v); updateRateButtons(); });
    modeAtt->sendInitialUpdate();

    for (auto& k : kKnobs)
    {
        auto* s = content.knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        s->setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        s->setTooltip (IDG_T (k.help));
        content.addAndMakeVisible (s);
        knobAtts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, k.id, *s));
        s->setDoubleClickReturnValue (true, proc.apvts.getParameterRange (k.id).convertFrom0to1 (
                                                proc.apvts.getParameter (k.id)->getDefaultValue()));
        auto* l = content.labels.add (new juce::Label ({}, k.label));
        l->setJustificationType (juce::Justification::centred);
        l->setFont (Theme::mono (12.0f, true));
        l->setColour (juce::Label::textColourId, Theme::textDim);
        content.addAndMakeVisible (l);
    }

    addAndMakeVisible (content);
    content.setSize (baseW, baseH);
    tooltip = std::make_unique<juce::TooltipWindow> (this, 600);

    // taille mémorisée lue AVANT setResizable (qui déclenche resized())
    const float remembered = proc.editorScale.load();
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) baseW / baseH);
        c->setSizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    }
    const float scale = juce::jlimit (0.5f, 2.0f, remembered > 0.0f ? remembered : 1.0f);
    setSize ((int) (baseW * scale), (int) (baseH * scale));
    sizeReady = true;
    startTimerHz (30);
}

StretchEditor::~StretchEditor()
{
    stopTimer();
    tooltip.reset();
    setLookAndFeel (nullptr);
}

void StretchEditor::updateRateButtons()
{
    for (int i = 0; i < content.rateButtons.size(); ++i)
        content.rateButtons[i]->setToggleState (i == currentRate, juce::dontSendNotification);
    for (int i = 0; i < content.modeButtons.size(); ++i)
        content.modeButtons[i]->setToggleState (i == currentMode, juce::dontSendNotification);
}

void StretchEditor::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void StretchEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    if (sizeReady) proc.editorScale = s;
}

void StretchEditor::timerCallback() { content.strip->repaint(); }
} // namespace idg
