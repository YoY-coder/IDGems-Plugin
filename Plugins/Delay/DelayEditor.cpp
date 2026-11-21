#include "DelayEditor.h"

namespace idg
{
using ui::Theme;
namespace
{
const juce::Colour space { 0xff33e0c0 };   // famille Espace : turquoise
const juce::Colour hot { 0xffff6a00 };

juce::String msString (double ms)
{
    return ms >= 100.0 ? juce::String (ms, 0) + " ms" : ms >= 10.0 ? juce::String (ms, 1) + " ms" : juce::String (ms, 2) + " ms";
}

double baseSeconds (const GlitchDelay::Settings& s)
{
    return s.sync ? GlitchDelay::noteBeats (s.note, s.feel) * 60.0 / juce::jmax (GlitchDelay::minBpm, s.bpm)
                  : juce::jlimit (GlitchDelay::minTimeMs, GlitchDelay::maxTimeMs, s.timeMs) * 0.001;
}
} // namespace

// ---------------------------------------------------------------------------
// EchoView
// ---------------------------------------------------------------------------
void EchoView::tick()
{
    const float pk = proc.delay.wetPeak.exchange (0.0f);
    glow = juce::jmax (pk, glow * 0.85f);
    repaint();
}

void EchoView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (b);
    g.setColour (Theme::line);
    g.drawRect (b, 1.0f);

    const auto s = proc.currentSettings();
    const double bpm = juce::jmax (GlitchDelay::minBpm, s.bpm);
    const double bar = 4.0 * 60.0 / bpm;                   // fenêtre : une mesure
    const double t = baseSeconds (s);
    const double off = (s.mode == GlitchDelay::Mono ? 0.0 : (double) s.offsetMs * 0.001);
    const double tL = t + juce::jmax (0.0, -off), tR = t + juce::jmax (0.0, off);

    auto area = b.reduced (10.0f, 22.0f);
    const float mid = area.getCentreY();
    auto xOf = [&] (double sec) { return area.getX() + (float) (sec / bar) * area.getWidth(); };

    // grille : noires et doubles-croches
    for (int i = 0; i <= 16; ++i)
    {
        const float x = xOf (bar * i / 16.0);
        g.setColour (Theme::line.withAlpha (i % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }
    g.setColour (Theme::line.brighter (0.2f));
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());
    g.setFont (Theme::mono (9.0f, true));
    g.setColour (Theme::textDim);
    g.drawText ("L", juce::Rectangle<float> (b.getX() + 2, area.getY(), 10, 12), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (b.getX() + 2, area.getBottom() - 12, 10, 12), juce::Justification::centred);

    // perte approximative par passage dans les filtres
    const float lc = juce::jlimit (20.0f, 20000.0f, s.lowCut), hc = juce::jlimit (20.0f, 20000.0f, s.highCut);
    const float band = juce::jlimit (0.0f, 1.0f, (std::log (hc) - std::log (lc)) / std::log (1000.0f));
    const float filt = 0.35f + 0.65f * band;
    const float fb = s.feedback;
    const float halfH = area.getHeight() * 0.5f - 2.0f;
    const float barW = juce::jlimit (1.0f, 6.0f, (float) (t / bar) * area.getWidth() * 0.35f);
    const float act = juce::jlimit (0.0f, 1.0f, glow);

    auto drawEcho = [&] (double when, float level, bool left)
    {
        if (when > bar) return;
        const float x = xOf (when);
        const float lv = juce::jmin (1.3f, level);
        const float h = halfH * juce::jmin (1.0f, lv / 1.3f + 0.04f);
        auto c = lv > 1.0f ? hot.interpolatedWith (juce::Colour (0xffff2d55), juce::jmin (1.0f, (lv - 1.0f) * 3.0f)) : space;
        auto r = left ? juce::Rectangle<float> (x - barW * 0.5f, mid - h, barW, h) : juce::Rectangle<float> (x - barW * 0.5f, mid, barW, h);
        g.setColour (c.withAlpha (0.12f + 0.2f * act));
        g.fillRect (r.expanded (2.5f, 0.0f));
        g.setColour (c.withAlpha (juce::jlimit (0.15f, 1.0f, 0.25f + 0.75f * lv)));
        g.fillRect (r);
        g.setColour (juce::Colours::white.withAlpha (0.5f * juce::jmin (1.0f, lv)));
        g.fillRect (left ? r.removeFromTop (1.5f) : r.removeFromBottom (1.5f));
    };

    // le son sec
    g.setColour (Theme::text.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (xOf (0) - 1.5f, mid - halfH, 3.0f, halfH * 2.0f));

    const int maxEchoes = 400;
    float level = filt;
    if (s.mode == GlitchDelay::PingPong)
    {
        // gauche à T, droite à 2T, gauche à 3T...
        double when = 0.0;
        for (int k = 0; k < maxEchoes && level > 0.01f; ++k)
        {
            const bool left = k % 2 == 0;
            when += left ? tL : tR;
            if (when > bar) break;
            drawEcho (when, level, left);
            level = juce::jmin (1.4f, level * fb * filt);
        }
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const double step = s.mode == GlitchDelay::Mono ? t : (ch == 0 ? tL : tR);
            level = filt;
            for (int k = 1; k < maxEchoes && level > 0.01f; ++k)
            {
                if (step * k > bar) break;
                drawEcho (step * k, level, ch == 0);
                level = juce::jmin (1.4f, level * fb * filt);
            }
        }
    }

    if (proc.apvts.getRawParameterValue ("bypass")->load() > 0.5f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (b);
        Theme::cutText (g, "BYPASS", b, 22.0f, juce::Colour (0xffff2d55), juce::Justification::centred, 0.4f, true);
    }

    // lecture du temps
    juce::String timeTxt = s.sync ? GlitchDelay::divisionText (s.note, s.feel) + "  ·  " + msString (t * 1000.0)
                                  : "FREE  ·  " + msString (t * 1000.0);
    Theme::cutText (g, timeTxt, b.reduced (10.0f, 3.0f).withHeight (18.0f), 12.0f, space.interpolatedWith (Theme::text, 0.5f),
                    juce::Justification::centredLeft, 0.25f * act, true);
    g.setFont (Theme::mono (10.0f, true));
    g.setColour (fb > 1.0f ? hot : Theme::textDim);
    static const char* modes[] { "STEREO", "PING-PONG", "MONO" };
    g.drawText (juce::String (modes[juce::jlimit (0, 2, s.mode)]) + "   FB " + juce::String (juce::roundToInt (fb * 100.0f)) + " %"
                    + (fb > 1.0f ? IDG_T ("  ▲ EMBALLEMENT") : juce::String())
                    + "   " + juce::String (bpm, 1) + " BPM",
                b.reduced (10.0f, 3.0f).withHeight (18.0f), juce::Justification::centredRight);
}

// ---------------------------------------------------------------------------
// Contenu
// ---------------------------------------------------------------------------
DelayEditor::Content::Content (DelayProcessor& p)
    : proc (p), echoes (p),
      sync (p.apvts, "sync", { "FREE", "SYNC" }, space, IDG_T ("SYNC : temps calé sur le tempo (jamais plus d'une noire). FREE : temps libre en ms, jusqu'aux temps ultra-courts.")),
      feel (p.apvts, "feel", { "DROIT", IDG_T ("POINTÉ"), "TERNAIRE" }, space, IDG_T ("Type de division. Le random ne choisit jamais le ternaire. Pas de pointé sur 1/4 (on ne dépasse jamais la noire).")),
      mode (p.apvts, "mode", { IDG_T ("STÉRÉO"), "PING-PONG", "MONO" }, space, IDG_T ("STÉRÉO : gauche et droite séparées. PING-PONG : les répétitions rebondissent G/D. MONO : tout au centre.")),
      note (p.apvts, "note", space, "Division", IDG_T ("Division rythmique : de 1/128 à 1/4.")),
      time (p.apvts, "time", space, "Temps", IDG_T ("Temps libre : 0,3 ms (effet de filtre en peigne) à 500 ms.")),
      feedback (p.apvts, "feedback", hot, "Feedback", IDG_T ("Réinjection. Au-delà de 100 % la boucle s'emballe et sature (le random reste sous 100 %).")),
      offset (p.apvts, "offset", space, IDG_T ("DÉCALAGE"), IDG_T ("Décalage stéréo : retarde un côté de quelques ms (élargit l'image).")),
      mix (p.apvts, "mix", Theme::text, "Mix", IDG_T ("Dosage sec / traité. Jamais touché par le random.")),
      filter (p.apvts, "lowcut", "highcut", space),
      bypass (p.apvts, "bypass", "BYPASS", juce::Colour (0xffff2d55), IDG_T ("Coupe le delay (le son sec passe). Relié au bouton on/off d'Ableton."))
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &echoes, &sync, &feel, &mode, &note, &time, &feedback,
                                                                       &filter, &offset, &mix, &random, &undo, &bypass })
        addAndMakeVisible (c);
    note.setBig (true); time.setBig (true); feedback.setBig (true);
    note.valueText = [this]
    {
        const auto s = proc.currentSettings();
        return GlitchDelay::divisionText (s.note, s.feel);
    };
    feel.isDisabled = [this] (int i) { return i == GlitchDelay::Dotted && ! GlitchDelay::dottedAvailable (proc.currentSettings().note); };
    sync.onChange = [this] { refresh(); };
    random.onClick = [this] { proc.randomize (rng); refresh(); };
    undo.onClick = [this] { proc.undoRandom(); refresh(); };
    refresh();
}

void DelayEditor::Content::refresh()
{
    const bool s = sync.selected() == 1;
    note.setVisible (s);
    time.setVisible (! s);
    feel.setEnabled (s);
    undo.setEnabled (proc.canUndo());
    feel.repaint();
}

void DelayEditor::Content::resized()
{
    random.setBounds (baseW - 150, 16, 130, 34);
    undo.setBounds (baseW - 270, 16, 110, 34);
    bypass.setBounds (baseW - 390, 16, 110, 34);
    echoes.setBounds (20, 66, baseW - 40, 124);

    const float top = 202.0f, h = (float) baseH - top - 16.0f;
    pTime = { 20, top, 240, h };
    pFb = { 270, top, 120, h };
    pFilter = { 400, top, 250, h };
    pStereo = { 660, top, 150, h };
    pMix = { 820, top, 80, h };
    auto inner = [] (juce::Rectangle<float> r) { return r.reduced (10.0f).withTrimmedTop (16.0f).toNearestInt(); };

    auto t = inner (pTime);
    sync.setBounds (t.removeFromTop (24));
    feel.setBounds (t.removeFromBottom (24));
    t.reduce (0, 4);
    note.setBounds (t.withSizeKeepingCentre (150, t.getHeight()));
    time.setBounds (note.getBounds());

    feedback.setBounds (inner (pFb).withTrimmedTop (30).withTrimmedBottom (30));
    filter.setBounds (inner (pFilter).withTrimmedTop (4));
    auto st = inner (pStereo);
    mode.setBounds (st.removeFromTop (24));
    offset.setBounds (st.withTrimmedTop (16).withTrimmedBottom (8));
    mix.setBounds (inner (pMix).withTrimmedTop (40).withTrimmedBottom (40));
}

void DelayEditor::Content::paint (juce::Graphics& g)
{
    if (bg.isNull()) bg = ui::solo::makeSoloBackground (baseW, baseH, 0xde1a7);
    g.drawImageAt (bg, 0, 0);

    Theme::cutText (g, "DELAY", { 22.0f, 10.0f, 300.0f, 40.0f }, 30.0f, Theme::text, juce::Justification::centredLeft, 0.55f, true);
    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::accent);
    g.drawText (IDG_T ("IDGEMS  //  MODULE 01  //  GLITCH DELAY"), juce::Rectangle<float> (26.0f, 46.0f, 400.0f, 14.0f),
                juce::Justification::centredLeft);

    auto panel = [&] (juce::Rectangle<float> r, const juce::String& title, juce::Colour c)
    {
        Theme::panelFrame (g, r, c);
        Theme::sectionTitle (g, title, r.reduced (10.0f, 6.0f), c);
    };
    panel (pTime, "Temps", space);
    panel (pFb, "Feedback", hot);
    panel (pFilter, "Filtre", space);
    panel (pStereo, IDG_T ("STÉRÉO"), space);
    panel (pMix, "Mix", Theme::text);
}

// ---------------------------------------------------------------------------
// Éditeur (fenêtre redimensionnable, taille mémorisée dans le projet)
// ---------------------------------------------------------------------------
DelayEditor::DelayEditor (DelayProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);
    content->setSize (baseW, baseH);
    tooltip = std::make_unique<juce::TooltipWindow> (this, 600);
    tooltip->setLookAndFeel (&lnf);

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) baseW / baseH);
        c->setSizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    }
    const float remembered = proc.editorScale.load();
    const float scale = juce::jlimit (0.6f, 2.0f, remembered > 0.0f ? remembered : 1.0f);
    setSize ((int) (baseW * scale), (int) (baseH * scale));
    sizeReady = true;
    startTimerHz (30);
}

DelayEditor::~DelayEditor()
{
    stopTimer();
    tooltip.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void DelayEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content->setTransform (juce::AffineTransform::scale (s));
    if (sizeReady) proc.editorScale = s;
}

void DelayEditor::timerCallback()
{
    content->refresh();
    content->echoes.tick();
    for (auto* k : { &content->note, &content->time, &content->feedback, &content->offset, &content->mix })
        k->tick();
}
} // namespace idg
