#include "SmearEditor.h"
#include "UI/Theme.h"

namespace idg
{
using ui::Theme;

namespace
{
const juce::Colour kAcc { 0xffff6a00 };  // orange IDGems
const juce::Colour kBlue { 0xff2fa8ff };
} // namespace

// ---------------------------------------------------------------------------
SmearLookAndFeel::SmearLookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, Theme::panelHi);
    setColour (juce::TooltipWindow::textColourId, Theme::text);
    setColour (juce::TooltipWindow::outlineColourId, kAcc.withAlpha (0.6f));
}

void SmearLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    r = r.withTrimmedBottom (22.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight()) * 0.86f;
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f - 4.0f;
    const float thick = juce::jmax (3.0f, size * 0.07f);
    const float ang = a0 + pos * (a1 - a0);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    // réglages qui passent par zéro (TONE, SORTIE) : l'arc part de la valeur neutre
    const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
    const float aStart = a0 + zero * (a1 - a0);
    const float amount = bipolar ? juce::jmin (1.0f, std::abs (pos - zero) * 2.0f) : pos;

    g.setColour (Theme::panelHi);
    g.fillEllipse (r.reduced (thick + 3.0f));
    g.setColour (Theme::line);
    g.drawEllipse (r.reduced (thick + 3.0f), 1.0f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
    g.setColour (Theme::line);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (amount > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (aStart, ang), juce::jmax (aStart, ang), true);
        g.setColour (kAcc.withAlpha (0.10f + 0.20f * amount));
        g.strokePath (arc, juce::PathStrokeType (thick * (2.2f + 1.5f * amount), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kAcc);
        g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.35f + 0.4f * amount));
        g.strokePath (arc, juce::PathStrokeType (thick * 0.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const juce::Point<float> p1 = c.getPointOnCircumference (rad * 0.25f, ang), p2 = c.getPointOnCircumference (rad * 0.62f, ang);
    g.setColour (Theme::text);
    g.drawLine ({ p1, p2 }, 2.0f);

    g.setColour (Theme::text);
    g.setFont (Theme::mono (juce::jlimit (11.0f, 15.0f, size * 0.16f), true));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> (c.x - 70.0f, r.getBottom() + 2.0f, 140.0f, 20.0f),
                juce::Justification::centred, false);
}

void SmearLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    if (b.getToggleState())
    {
        g.setColour (kBlue.withAlpha (0.25f));
        g.fillRect (r.expanded (1.5f));
        g.setColour (kBlue);
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

void SmearLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? juce::Colours::black : Theme::text);
    g.setFont (Theme::mono (juce::jmin (13.0f, b.getHeight() * 0.42f), true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
SmearScope::SmearScope (SmearProcessor& p) : proc (p)
{
    imgIn.clear (imgIn.getBounds(), Theme::bg);
    imgOut.clear (imgOut.getBounds(), Theme::bg);
}

juce::Colour SmearScope::heat (float v, bool out)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (out)
    {
        // noir → rouge braise → orange → blanc
        if (v < 0.45f) return Theme::bg.interpolatedWith (juce::Colour (0xff8a1600), v / 0.45f);
        if (v < 0.8f) return juce::Colour (0xff8a1600).interpolatedWith (kAcc, (v - 0.45f) / 0.35f);
        return kAcc.interpolatedWith (juce::Colour (0xfffff0dc), (v - 0.8f) / 0.2f);
    }
    if (v < 0.5f) return Theme::bg.interpolatedWith (juce::Colour (0xff0b3a66), v / 0.5f);
    if (v < 0.85f) return juce::Colour (0xff0b3a66).interpolatedWith (kBlue, (v - 0.5f) / 0.35f);
    return kBlue.interpolatedWith (juce::Colour (0xffe0f4ff), (v - 0.85f) / 0.15f);
}

void SmearScope::advance()
{
    for (auto* img : { &imgIn, &imgOut })
        img->moveImageSection (0, 0, 1, 0, cols - 1, SmearEngine::dispBins);
    const int nb = SmearEngine::dispBins;
    for (int b = 0; b < nb; ++b)
    {
        const float din = proc.engine.dispIn[(size_t) b].load (std::memory_order_relaxed);
        const float dout = proc.engine.dispOut[(size_t) b].load (std::memory_order_relaxed);
        // 0 dB en haut de l'échelle, -78 dB = noir
        imgIn.setPixelAt (cols - 1, nb - 1 - b, heat ((din + 78.0f) / 72.0f, false));
        imgOut.setPixelAt (cols - 1, nb - 1 - b, heat ((dout + 78.0f) / 72.0f, true));
    }
    repaint();
}

void SmearScope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float gap = 6.0f;
    auto top = r.removeFromTop ((r.getHeight() - gap) * 0.5f);
    r.removeFromTop (gap);
    auto bot = r;
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (imgIn, top, juce::RectanglePlacement::stretchToFit);
    g.drawImage (imgOut, bot, juce::RectanglePlacement::stretchToFit);

    g.setFont (Theme::mono (9.5f));
    for (auto* area : { &top, &bot })
    {
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float y = area->getBottom() - area->getHeight() * std::log (f / 20.0f) / std::log (1000.0f);
            g.setColour (Theme::line.withAlpha (0.5f));
            g.drawHorizontalLine ((int) y, area->getX(), area->getRight());
            g.setColour (Theme::textDim.withAlpha (0.8f));
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (area->getX() + 3.0f, y - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
        }
        g.setColour (Theme::line);
        g.drawRect (*area, 1.0f);
    }
    g.setFont (Theme::mono (10.5f, true));
    g.setColour (kBlue);
    g.drawText (IDG_T ("ENTRÉE"), top.reduced (6.0f, 4.0f), juce::Justification::topRight, false);
    g.setColour (kAcc);
    g.drawText ("SORTIE", bot.reduced (6.0f, 4.0f), juce::Justification::topRight, false);
}

// ---------------------------------------------------------------------------
void SmearEditor::Content::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.fillAll (Theme::bg);
    juce::ColourGradient glow (kAcc.withAlpha (0.15f), r.getRight(), r.getBottom(), Theme::bg.withAlpha (0.0f), r.getCentreX(), r.getY(), true);
    g.setGradientFill (glow);
    g.fillRect (r);

    // fissure incandescente qui « coule » (courbes douces : pas de rythme)
    juce::Path crack;
    crack.startNewSubPath (r.getRight() - 300.0f, r.getBottom());
    crack.cubicTo (r.getRight() - 230.0f, r.getBottom() - 14.0f, r.getRight() - 160.0f, r.getBottom() + 2.0f, r.getRight() - 90.0f, r.getBottom() - 9.0f);
    crack.cubicTo (r.getRight() - 50.0f, r.getBottom() - 14.0f, r.getRight() - 20.0f, r.getBottom() - 4.0f, r.getRight(), r.getBottom() - 7.0f);
    g.setColour (kAcc.withAlpha (0.12f));
    g.strokePath (crack, juce::PathStrokeType (6.0f));
    g.setColour (kAcc.withAlpha (0.45f));
    g.strokePath (crack, juce::PathStrokeType (1.2f));

    Theme::cutText (g, "SMEAR", { 22.0f, 14.0f, 300.0f, 40.0f }, 30.0f, Theme::text, juce::Justification::centredLeft, 0.5f, true);
    g.setFont (Theme::mono (11.0f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("IDGEMS · DILUTION SPECTRALE, SANS RYTHME"), juce::Rectangle<float> (24.0f, 54.0f, 420.0f, 14.0f),
                juce::Justification::centredLeft, false);

    g.setFont (Theme::mono (12.0f, true));
    for (auto& k : knobs)
    {
        g.setColour (Theme::textDim);
        g.drawText (IDG_T (k.label), k.area.withHeight (16).toFloat(), juce::Justification::centred, false);
    }
}

void SmearEditor::Content::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    r.removeFromTop (78);
    r.removeFromBottom (18);

    // gauche : grand potard SMEAR + FREEZE ; droite : sonogramme ; bas : réglages
    auto bottom = r.removeFromBottom (128);
    auto left = r.removeFromLeft (200);
    r.removeFromLeft (16);
    if (scope) scope->setBounds (r);

    freeze.setBounds (left.removeFromBottom (32).reduced (24, 0));
    left.removeFromBottom (6);
    knobs[0].area = left;
    knobs[0].s->setBounds (left.withTrimmedTop (16));

    bottom.removeFromTop (12);
    const int nSmall = (int) knobs.size() - 1;
    const int w = bottom.getWidth() / nSmall;
    for (int i = 1; i <= nSmall; ++i)
    {
        auto a = bottom.removeFromLeft (w);
        knobs[(size_t) i].area = a;
        knobs[(size_t) i].s->setBounds (a.withTrimmedTop (16).withSizeKeepingCentre (juce::jmin (w, 110), a.getHeight() - 16));
    }
}

SmearEditor::SmearEditor (SmearProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content.scope = std::make_unique<SmearScope> (p);
    content.addAndMakeVisible (*content.scope);

    struct Def { const char* id; const char* label; const char* help; };
    const Def defs[] {
        { "smear", "SMEAR", "Durée de la traînée : combien de temps chaque fréquence met à monter et à s'éteindre. "
                            "Court = le son se lisse ; long = tout se fond en une nappe qui garde les notes." },
        { "rise", "MONTÉE", "Vitesse d'apparition, en part du SMEAR. 100 % = symétrique (coulant, le son arrive en fondu) ; "
                            "bas = les nouvelles notes arrivent vite et seule la queue est diluée." },
        { "focus", "FOCUS", "Pureté tonale : creuse tout ce qui n'est pas une harmonique (bruit, inharmonique). "
                            "0 % = tout le spectre est dilué ; 100 % = lignes harmoniques pures." },
        { "drift", "DRIFT", "Mouvement lent : léger désaccord, ondulations de niveau et largeur stéréo "
                            "(au-dessus de 200 Hz, la basse reste mono)." },
        { "tone", "TONE", "Inclinaison du spectre traité : à gauche plus sombre, à droite plus brillant (±3 dB/octave)." },
        { "mix", "MIX", "Dosage sec / traité (100 % par défaut)." },
        { "out", "SORTIE", "Volume de sortie (un limiteur de sécurité reste actif à -0,3 dBFS)." },
    };
    for (auto& d : defs)
    {
        Content::K k { d.id, d.label, std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), {} };
        k.s->setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        k.s->setTooltip (IDG_T (d.help));
        content.addAndMakeVisible (*k.s);
        atts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, *k.s));
        if (auto* prm = proc.apvts.getParameter (d.id))
            k.s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        content.knobs.push_back (std::move (k));
    }

    content.freeze.setClickingTogglesState (true);
    content.freeze.setTooltip (IDG_T ("Fige la nappe telle qu'elle est : le son tient indéfiniment, l'entrée est ignorée."));
    content.addAndMakeVisible (content.freeze);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", content.freeze);

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

SmearEditor::~SmearEditor()
{
    stopTimer();
    tooltip.reset();
    setLookAndFeel (nullptr);
}

void SmearEditor::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void SmearEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    if (sizeReady) proc.editorScale = s;
}

void SmearEditor::timerCallback()
{
    content.scope->advance();
}
} // namespace idg
