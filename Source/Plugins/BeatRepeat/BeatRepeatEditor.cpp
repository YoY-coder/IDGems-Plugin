#include "BeatRepeatEditor.h"

namespace idg
{
using ui::Theme;
namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;

    juce::String noteName (double hz)
    {
        if (hz < 16.0) return {};
        const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
        static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }
}

// ============================================================================ NeonKnob
NeonKnob::NeonKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& l, juce::Colour c, bool b)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      param (s.getParameter (id)), label (l), colour (c), big (b), att (s, id, *this)
{
    setRotaryParameters (startAngle, endAngle, true);
    setMouseDragSensitivity (big ? 320 : 220);
    setVelocityBasedMode (false);
    setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    setPopupDisplayEnabled (false, false, nullptr);
}

void NeonKnob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = big ? 34.0f : 28.0f;
    auto dialArea = b.withTrimmedBottom (labelH);
    const float size = juce::jmin (dialArea.getWidth(), dialArea.getHeight()) - 6.0f;
    auto dial = juce::Rectangle<float> (size, size).withCentre (dialArea.getCentre());
    const auto c = dial.getCentre();
    const float radius = size * 0.5f, ringR = radius - 4.0f;
    const float norm = param->getValue();
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    auto angleOf = [] (float v) { return startAngle + juce::jlimit (0.0f, 1.0f, v) * (endAngle - startAngle); };
    auto arc = [&] (float r, float a0, float a1, juce::Colour col, float thick)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    };

    arc (ringR, startAngle, endAngle, Theme::line.withAlpha (0.9f * alpha), 2.0f);
    const auto* rp = dynamic_cast<juce::AudioParameterFloat*> (param);
    const bool bipolar = rp != nullptr && rp->range.start < 0.0f && rp->range.end > 0.0f && rp->range.start == -rp->range.end;
    const float zero = bipolar ? 0.5f : 0.0f;
    const float intensity = juce::jlimit (0.0f, 1.0f, bipolar ? std::abs (norm - 0.5f) * 2.0f : norm);
    const float a0 = angleOf (zero), a1 = angleOf (norm);
    if (std::abs (a1 - a0) > 0.01f)
    {
        const float lo = juce::jmin (a0, a1), hi = juce::jmax (a0, a1);
        const float bloom = (0.25f + 0.75f * intensity) * alpha;
        const float spread = (big ? 3.0f : 2.0f) + 7.0f * intensity;
        for (int k = 4; k >= 1; --k)
            arc (ringR, lo, hi, colour.withAlpha (bloom * 0.11f * (float) (5 - k) / 4.0f), 3.0f + spread * (float) k * 0.55f);
        arc (ringR, lo, hi, colour.withAlpha (alpha), big ? 4.5f : 3.2f);
        arc (ringR, lo, hi, colour.interpolatedWith (juce::Colours::white, 0.35f + 0.45f * intensity).withAlpha ((0.5f + 0.5f * bloom) * alpha), 1.3f);
        const float ae = a1 - juce::MathConstants<float>::halfPi;
        const juce::Point<float> tip { c.x + std::cos (ae) * ringR, c.y + std::sin (ae) * ringR };
        g.setColour (colour.withAlpha (bloom * 0.35f));
        g.fillEllipse (juce::Rectangle<float> (6.0f + 9.0f * intensity, 6.0f + 9.0f * intensity).withCentre (tip));
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (tip));
    }

    // corps : roche noire, reflet froid
    auto body = dial.reduced (size * (big ? 0.17f : 0.2f));
    juce::ColourGradient grad (juce::Colour (0xff2a2a31), body.getX(), body.getY(), juce::Colour (0xff050506), body.getRight(), body.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (body);
    g.setColour (Theme::line);
    g.drawEllipse (body, 1.0f);
    const float pa = a1 - juce::MathConstants<float>::halfPi;
    const float r0 = body.getWidth() * 0.18f, r1 = body.getWidth() * 0.46f;
    g.setColour (colour.interpolatedWith (juce::Colours::white, 0.5f).withAlpha (alpha));
    g.drawLine (c.x + std::cos (pa) * r0, c.y + std::sin (pa) * r0, c.x + std::cos (pa) * r1, c.y + std::sin (pa) * r1, big ? 3.0f : 2.0f);

    // étiquette + valeur
    auto lab = b.removeFromBottom (labelH);
    Theme::cutText (g, label.toUpperCase(), lab.removeFromTop (labelH * 0.5f), big ? 12.0f : 9.5f,
                    Theme::text.withAlpha (alpha), juce::Justification::centred, 0.0f, true);
    g.setFont (Theme::mono (big ? 12.0f : 10.5f));
    g.setColour (colour.interpolatedWith (Theme::text, 0.4f).withAlpha (alpha));
    g.drawText (readout.isNotEmpty() ? readout : param->getCurrentValueAsText(), lab, juce::Justification::centred, false);
}

// ============================================================================ ParamButton
ParamButton::ParamButton (juce::RangedAudioParameter& p, const juce::String& text, int cv, juce::Colour c)
    : juce::Button (text), param (p), choiceValue (cv), colour (c) {}

bool ParamButton::isLit() const
{
    const float v = param.convertFrom0to1 (param.getValue());
    return choiceValue < 0 ? v > 0.5f : juce::roundToInt (v) == choiceValue;
}

void ParamButton::clicked()
{
    float target;
    if (choiceValue < 0) target = isLit() ? 0.0f : 1.0f;
    else target = param.convertTo0to1 ((float) choiceValue);
    param.beginChangeGesture();
    param.setValueNotifyingHost (target);
    param.endChangeGesture();
}

void ParamButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = isLit();
    if (lit)
    {
        g.setColour (colour.withAlpha (0.18f));
        g.fillRect (r.expanded (2.0f));
        g.setColour (colour.withAlpha (0.85f));
        g.fillRect (r);
    }
    else
    {
        g.setColour (Theme::panelHi.withAlpha (over ? 1.0f : 0.8f));
        g.fillRect (r);
    }
    g.setColour (lit ? colour.brighter (0.4f) : Theme::line.brighter (over ? 0.5f : 0.0f));
    g.drawRect (r, 1.0f);
    g.setColour (lit ? juce::Colours::black : Theme::textDim.brighter (over ? 0.4f : 0.0f));
    g.setFont (Theme::mono (juce::jmin (13.0f, r.getHeight() * 0.5f), true));
    g.drawText (getButtonText(), r, juce::Justification::centred, false);
}

// ============================================================================ Editor
static juce::RangedAudioParameter& prm (BeatRepeatProcessor& p, const char* id) { return *p.apvts.getParameter (id); }

BeatRepeatEditor::BeatRepeatEditor (BeatRepeatProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p),
      speed (p.apvts, "speed", "Speed", Theme::accent, true),
      tempo (p.apvts, "tempo", "Tempo", Theme::accent),
      tempoMs (p.apvts, "tempoMs", "Tempo", Theme::accent),
      smooth (p.apvts, "smooth", "Smooth", Theme::blue),
      glide (p.apvts, "glide", "Glide", Theme::blue),
      ramp (p.apvts, "ramp", "Rampe", Theme::accent),
      mix (p.apvts, "mix", "Mix", Theme::textDim),
      gain (p.apvts, "gain", "Gain", Theme::textDim),
      active (prm (p, "active"), "REPEAT", -1, Theme::accent),
      sync (prm (p, "sync"), "SYNC", -1, Theme::accent),
      snap (prm (p, "snap"), "SNAP", -1, Theme::blue),
      fwd (prm (p, "dir"), juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), 0, Theme::blue),
      pingpong (prm (p, "dir"), juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x94")), 1, Theme::blue),
      rev (prm (p, "dir"), juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90")), 2, Theme::blue)
{
    setLookAndFeel (&laf);
    for (auto* k : { &speed, &tempo, &tempoMs, &smooth, &glide, &ramp, &mix, &gain }) addAndMakeVisible (k);
    for (auto* b : { &active, &sync, &snap, &fwd, &pingpong, &rev }) addAndMakeVisible (b);
    fwd.setTooltip ("Avant");
    pingpong.setTooltip ("Aller-retour");
    rev.setTooltip (IDG_T ("Arrière"));
    setSize (640, 400);
    startTimerHz (30);
}

BeatRepeatEditor::~BeatRepeatEditor() { setLookAndFeel (nullptr); }

juce::String BeatRepeatEditor::speedText() const
{
    const auto s = proc.readSettings();
    const double bpm = proc.uiBpm.load();
    double ms;
    juce::String div;
    if (s.sync)
    {
        double beats = LoopRepeat::speedToBeats (s.speed);
        juce::String nearest;
        const double snapped = LoopRepeat::snapBeats (beats, &nearest);
        if (s.snap) { beats = snapped; div = nearest; }
        else div = (std::abs (std::log2 (beats / snapped)) < 0.03 ? "" : "~") + nearest;
        ms = beats * 60000.0 / bpm;
    }
    else ms = LoopRepeat::speedToMs (s.speed);
    const double hz = 1000.0 / ms;
    juce::String t = ms >= 100.0 ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms, 1) + " ms";
    if (div.isNotEmpty()) t << "  " << div;
    if (hz >= 20.0) t << "  " << juce::String (juce::roundToInt (hz)) << " Hz " << noteName (hz);
    return t;
}

void BeatRepeatEditor::timerCallback()
{
    const bool isSync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    tempo.setVisible (isSync);
    tempoMs.setVisible (! isSync);
    snap.setEnabled (isSync);
    speed.setReadout (speedText());
    for (auto* b : { &active, &sync, &snap, &fwd, &pingpong, &rev }) b->repaint();
    stepProgress = proc.engine.uiStepProgress.load();
    phase = proc.engine.uiPhase.load();
    cycle = proc.engine.uiCycle.load();
    loopMs = proc.engine.uiLoopMs.load();
    repaint (stripArea.toNearestInt().expanded (4));
}

void BeatRepeatEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.fillAll (Theme::bg);
    // texture : grain + fissures incandescentes discrètes
    juce::Random rnd (0x1D6E);
    for (int i = 0; i < 1400; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.035f));
        g.fillRect (rnd.nextFloat() * b.getWidth(), rnd.nextFloat() * b.getHeight(), 1.0f, 1.0f);
    }
    for (int k = 0; k < 3; ++k)
    {
        juce::Path crack;
        float x = rnd.nextFloat() * b.getWidth(), y = b.getBottom();
        crack.startNewSubPath (x, y);
        for (int s = 0; s < 9; ++s) { x += (rnd.nextFloat() - 0.5f) * 40.0f; y -= 10.0f + rnd.nextFloat() * 18.0f; crack.lineTo (x, y); }
        g.setColour (Theme::accent.withAlpha (0.05f));
        g.strokePath (crack, juce::PathStrokeType (5.0f));
        g.setColour (Theme::accent.withAlpha (0.22f));
        g.strokePath (crack, juce::PathStrokeType (1.0f));
    }

    auto top = b.removeFromTop (46.0f).reduced (16.0f, 8.0f);
    Theme::cutText (g, "BEAT REPEAT", top.withWidth (330.0f), 20.0f, Theme::text, juce::Justification::centredLeft, 0.55f, true);
    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::accent);
    g.drawText ("IDGEMS // LAB", top.withTrimmedLeft (318.0f).withWidth (110.0f), juce::Justification::centredLeft, false);
    g.setColour (Theme::line);
    g.fillRect (16.0f, 46.0f, b.getWidth() - 32.0f, 1.0f);

    Theme::panelFrame (g, juce::Rectangle<float> (16.0f, 56.0f, (float) getWidth() - 32.0f, 262.0f), Theme::accent, 0.8f);
}

void BeatRepeatEditor::paintOverChildren (juce::Graphics& g)
{
    // bande de lecture : progression du pas de tempo + position dans la boucle
    auto r = stripArea;
    g.setColour (Theme::panel);
    g.fillRect (r);
    g.setColour (Theme::line);
    g.drawRect (r, 1.0f);
    auto stepBar = r.removeFromTop (r.getHeight() * 0.5f).reduced (2.0f, 2.0f);
    g.setColour (Theme::blue.withAlpha (0.65f));
    g.fillRect (stepBar.withWidth (stepBar.getWidth() * juce::jlimit (0.0f, 1.0f, stepProgress)));
    auto loopBar = r.reduced (2.0f, 2.0f);
    const bool on = proc.apvts.getRawParameterValue ("active")->load() > 0.5f;
    if (on && cycle > 0)
    {
        g.setColour (Theme::accent.withAlpha (0.35f));
        g.fillRect (loopBar);
        const float x = loopBar.getX() + loopBar.getWidth() * phase;
        g.setColour (juce::Colours::white);
        g.fillRect (x - 1.0f, loopBar.getY(), 2.0f, loopBar.getHeight());
    }
    else if (on)
    {
        g.setColour (Theme::accent.withAlpha (0.6f));
        g.fillRect (loopBar.withWidth (loopBar.getWidth() * phase));
    }
    g.setFont (Theme::mono (10.0f, true));
    g.setColour (Theme::textDim);
    g.drawText (! on ? juce::String ("BYPASS") : cycle == 0 ? juce::String ("CAPTURE") : "LOOP x" + juce::String (cycle),
                stripArea.withTrimmedRight (6.0f), juce::Justification::centredRight, false);
}

void BeatRepeatEditor::resized()
{
    const int panelX = 16, panelY = 56, panelW = getWidth() - 32;
    const int colW = 110;
    const int knobH = 100;
    // colonne gauche : Tempo / Rampe
    tempo.setBounds (panelX + 18, panelY + 16, colW, knobH);
    tempoMs.setBounds (tempo.getBounds());
    ramp.setBounds (panelX + 18, panelY + 16 + knobH + 22, colW, knobH);
    // centre : Speed
    const int cw = 230;
    speed.setBounds (panelX + (panelW - cw) / 2, panelY + 10, cw, 200);
    const int bx = panelX + (panelW - 200) / 2, by = panelY + 216;
    fwd.setBounds (bx, by, 44, 30);
    pingpong.setBounds (bx + 48, by, 44, 30);
    rev.setBounds (bx + 96, by, 44, 30);
    snap.setBounds (bx + 146, by, 56, 30);
    // colonne droite : Smooth / Glide
    smooth.setBounds (panelX + panelW - colW - 18, panelY + 16, colW, knobH);
    glide.setBounds (panelX + panelW - colW - 18, panelY + 16 + knobH + 22, colW, knobH);
    sync.setBounds (getWidth() - 16 - 72, 10, 72, 26);

    // bas : REPEAT, bande de lecture, Mix, Gain
    const int y = 330;
    active.setBounds (16, y + 8, 120, 46);
    stripArea = juce::Rectangle<float> (150.0f, (float) y + 12.0f, (float) getWidth() - 150.0f - 190.0f, 38.0f);
    mix.setBounds (getWidth() - 180, y - 6, 80, 70);
    gain.setBounds (getWidth() - 96, y - 6, 80, 70);
}
} // namespace idg
