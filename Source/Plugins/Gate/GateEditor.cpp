#include "GateEditor.h"

namespace idg::gate
{
using ui::Theme;

namespace
{
const juce::Colour gateYellow { 0xffffd23f }; // famille Mouvement
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
}

// =====================================================================================================
// Potard néon
// =====================================================================================================
NeonKnob::NeonKnob (GateProcessor& p, const juce::String& id, const juce::String& lbl, const juce::String& help) : label (lbl)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (startAngle, endAngle, true);
    setMouseDragSensitivity (220);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, *this);
    if (auto* prm = p.apvts.getParameter (id)) setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    setTooltip (help + IDG_T ("\nDouble-clic : valeur par défaut."));
}

void NeonKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto labelArea = r.removeFromBottom (16.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight()) - 8.0f;
    auto k = juce::Rectangle<float> (size, size).withCentre (r.getCentre());
    const float norm = (float) valueToProportionOfLength (getValue());
    const float ang = startAngle + norm * (endAngle - startAngle);
    const float rad = size * 0.5f - 5.0f;
    const auto c = isEnabled() ? colour : Theme::textDim;

    // corps
    g.setColour (Theme::bg);
    g.fillEllipse (k.reduced (7.0f));
    g.setColour (Theme::line);
    g.drawEllipse (k.reduced (7.0f), 1.0f);

    juce::Path track;
    track.addCentredArc (k.getCentreX(), k.getCentreY(), rad, rad, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::line.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (norm > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (k.getCentreX(), k.getCentreY(), rad, rad, 0.0f, startAngle, ang, true);
        // halo qui grossit avec la valeur
        const float bloom = 0.25f + 0.75f * norm;
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (c.withAlpha (0.07f * bloom * (float) (4 - i)));
            g.strokePath (arc, juce::PathStrokeType (3.0f + 3.0f * i * bloom, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.55f * norm));
        g.strokePath (arc, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // index
    const auto tip = k.getCentre().getPointOnCircumference (rad - 9.0f, ang);
    const auto base = k.getCentre().getPointOnCircumference (rad * 0.25f, ang);
    g.setColour (Theme::text);
    g.drawLine ({ base, tip }, 2.0f);

    // valeur au centre (dessous), étiquette en bas
    g.setFont (Theme::mono (10.0f, true));
    g.setColour (isMouseOverOrDragging() ? Theme::text : Theme::textDim);
    g.drawText (getTextFromValue (getValue()), k.withY (k.getBottom() - 12.0f).withHeight (12.0f).expanded (20.0f, 0.0f),
                juce::Justification::centred, false);
    Theme::cutText (g, label.toUpperCase(), labelArea, 9.0f, Theme::text.interpolatedWith (c, 0.25f), juce::Justification::centred);
}

// =====================================================================================================
// Graphe du motif
// =====================================================================================================
GateGraph::GateGraph (GateProcessor& p) : proc (p)
{
    pat = proc.getPattern();
    seenVersion = proc.patternVersion();
    setRepaintsOnMouseActivity (false);
    startTimerHz (40);
}

juce::Point<float> GateGraph::toScreen (float x, float y) const
{
    auto r = area();
    return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
}

juce::Point<float> GateGraph::fromScreen (juce::Point<float> p) const
{
    auto r = area();
    return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight()) };
}

float GateGraph::snapX (float x, bool free) const
{
    if (free || ! proc.snap.load()) return x;
    const int g = proc.gridSteps();
    return std::round (x * g) / g;
}

int GateGraph::hitPoint (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 8.0f;
    for (int i = 0; i < pat.n; ++i)
    {
        const float d = toScreen (pat.pts[i].x, pat.pts[i].y).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

juce::Point<float> GateGraph::handlePos (int seg) const
{
    const float xm = 0.5f * (pat.pts[seg].x + pat.pts[seg + 1].x);
    return toScreen (xm, pat.segValue (seg, xm));
}

int GateGraph::hitHandle (juce::Point<float> p) const
{
    for (int i = 0; i + 1 < pat.n; ++i)
    {
        const auto& a = pat.pts[i]; const auto& b = pat.pts[i + 1];
        if (b.x - a.x < 1.0e-5f || std::abs (b.y - a.y) < 1.0e-4f) continue;
        if (handlePos (i).getDistanceFrom (p) < 7.0f) return i;
    }
    return -1;
}

void GateGraph::pushUndo()
{
    undoStack.push_back (pat.toString());
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
    redoStack.clear();
    if (onHistoryChanged) onHistoryChanged();
}

void GateGraph::publish()
{
    proc.setPattern (pat);
    seenVersion = proc.patternVersion();
    repaint();
}

void GateGraph::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (pat.toString());
    pat = GatePattern::fromString (undoStack.back());
    undoStack.pop_back();
    publish();
    if (onHistoryChanged) onHistoryChanged();
}

void GateGraph::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (pat.toString());
    pat = GatePattern::fromString (redoStack.back());
    redoStack.pop_back();
    publish();
    if (onHistoryChanged) onHistoryChanged();
}

void GateGraph::applyPreset (int idx) { pushUndo(); pat = GatePattern::preset (idx); publish(); }
void GateGraph::invert() { pushUndo(); pat.invert(); publish(); }
void GateGraph::shiftSteps (int dir) { pushUndo(); pat.shift ((float) dir / (float) proc.gridSteps()); publish(); }
void GateGraph::clearAll (bool open) { pushUndo(); pat = GatePattern::steps ({ open ? 1.0f : 0.0f }); publish(); }

void GateGraph::randomise()
{
    pushUndo();
    juce::Random r;
    const int g = proc.gridSteps();
    std::vector<float> lv ((size_t) g);
    for (int i = 0; i < g; ++i)
    {
        float v = r.nextFloat() < (i % 4 == 0 ? 0.85f : 0.5f) ? 1.0f : 0.0f;
        if (i > 0 && r.nextFloat() < 0.25f) v = lv[(size_t) i - 1]; // notes liées
        lv[(size_t) i] = v;
    }
    pat = GatePattern::steps (lv);
    publish();
}

void GateGraph::paintCell (int cell, float value)
{
    const int g = proc.gridSteps();
    cell = juce::jlimit (0, g - 1, cell);
    pat.setRange ((float) cell / g, (float) (cell + 1) / g, value);
}

void GateGraph::timerCallback()
{
    if (proc.patternVersion() != seenVersion && dragPt < 0 && dragHandle < 0 && lastCell < 0)
    {
        pat = proc.getPattern();
        seenVersion = proc.patternVersion();
    }
    // lissage visuel des crêtes (retombée douce)
    for (int i = 0; i < GateProcessor::numBins; ++i)
    {
        const float a = proc.inPeak[(size_t) i].load (std::memory_order_relaxed);
        const float b = proc.outPeak[(size_t) i].load (std::memory_order_relaxed);
        inView[(size_t) i] = juce::jmax (a, inView[(size_t) i] * 0.93f);
        outView[(size_t) i] = juce::jmax (b, outView[(size_t) i] * 0.93f);
    }
    shownPhase = proc.uiPhase.load (std::memory_order_relaxed);
    repaint();
}

void GateGraph::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060607));
    g.fillRect (full);
    auto r = area();
    const int grid = proc.gridSteps();
    const bool steps = stepsMode();

    // cases alternées + survol
    for (int i = 0; i < grid; ++i)
    {
        const float x0 = r.getX() + r.getWidth() * i / grid, x1 = r.getX() + r.getWidth() * (i + 1) / grid;
        if ((i / juce::jmax (1, grid / 4)) % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.018f)); g.fillRect (x0, r.getY(), x1 - x0, r.getHeight()); }
        if (steps && i == hoverCell) { g.setColour (Theme::accent.withAlpha (0.08f)); g.fillRect (x0, r.getY(), x1 - x0, r.getHeight()); }
    }
    // grille
    for (int i = 0; i <= grid; ++i)
    {
        const float x = r.getX() + r.getWidth() * i / grid;
        const bool major = grid >= 8 ? (i % (grid / 4) == 0) : true;
        g.setColour (Theme::line.withAlpha (major ? 1.0f : 0.45f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        g.setColour (Theme::line.withAlpha (i == 2 ? 0.6f : 0.25f));
        g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * i / 4.0f), r.getX(), r.getRight());
    }

    // forme d'onde : entrée (gris) et sortie (orange), en miroir autour du centre
    {
        const float bw = r.getWidth() / GateProcessor::numBins;
        const float cy = r.getCentreY(), hh = r.getHeight() * 0.48f;
        for (int i = 0; i < GateProcessor::numBins; ++i)
        {
            const float x = r.getX() + i * bw;
            const float a = std::sqrt (juce::jlimit (0.0f, 1.0f, inView[(size_t) i])) * hh;
            const float b = std::sqrt (juce::jlimit (0.0f, 1.0f, outView[(size_t) i])) * hh;
            g.setColour (Theme::textDim.withAlpha (0.16f));
            g.fillRect (x, cy - a, bw * 0.8f, 2.0f * a);
            g.setColour (Theme::accent.withAlpha (0.28f));
            g.fillRect (x, cy - b, bw * 0.8f, 2.0f * b);
        }
    }

    // motif
    juce::Path curve, fill;
    const int N = juce::jmax (400, (int) r.getWidth());
    fill.startNewSubPath (toScreen (0, 0));
    for (int seg = 0; seg + 1 < pat.n; ++seg)
    {
        const auto& a = pat.pts[seg]; const auto& b = pat.pts[seg + 1];
        auto pa = toScreen (a.x, a.y);
        if (seg == 0) curve.startNewSubPath (pa); else curve.lineTo (pa);
        fill.lineTo (pa);
        const int steps2 = std::abs (a.curve) > 0.001f ? juce::jmax (2, (int) ((b.x - a.x) * N)) : 1;
        for (int k = 1; k <= steps2; ++k)
        {
            const float x = a.x + (b.x - a.x) * k / steps2;
            const auto p = toScreen (x, k == steps2 ? b.y : pat.segValue (seg, x));
            curve.lineTo (p); fill.lineTo (p);
        }
    }
    fill.lineTo (toScreen (1, 0));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Theme::accent.withAlpha (0.34f), 0, r.getY(), Theme::accent.withAlpha (0.04f), 0, r.getBottom(), false));
    g.fillPath (fill);
    // halo néon + aberration rouge/bleu discrète
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (Theme::accent.withAlpha (0.06f * (4 - i)));
        g.strokePath (curve, juce::PathStrokeType (2.0f + 3.0f * i, juce::PathStrokeType::mitered));
    }
    g.setColour (juce::Colour (0xffff1e3c).withAlpha (0.35f));
    g.strokePath (curve, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (-1.5f, 0.5f));
    g.setColour (Theme::blue.withAlpha (0.3f));
    g.strokePath (curve, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (1.5f, -0.5f));
    g.setColour (Theme::accent.brighter (0.35f));
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered));

    // points et poignées de courbure (mode courbe)
    if (! steps)
    {
        for (int i = 0; i + 1 < pat.n; ++i)
        {
            const auto& a = pat.pts[i]; const auto& b = pat.pts[i + 1];
            if (b.x - a.x < 1.0e-5f || std::abs (b.y - a.y) < 1.0e-4f) continue;
            const auto hp = handlePos (i);
            juce::Path d;
            const float s = (i == hoverHandle || i == dragHandle) ? 5.5f : 4.0f;
            d.addQuadrilateral (hp.x, hp.y - s, hp.x + s, hp.y, hp.x, hp.y + s, hp.x - s, hp.y);
            g.setColour (Theme::bg);
            g.fillPath (d);
            g.setColour (Theme::blue.withAlpha (std::abs (a.curve) > 0.001f ? 1.0f : 0.7f));
            g.strokePath (d, juce::PathStrokeType (1.3f));
        }
        const float ps = pat.n > 48 ? 6.0f : 9.0f;
        for (int i = 0; i < pat.n; ++i)
        {
            const auto sp = toScreen (pat.pts[i].x, pat.pts[i].y);
            const bool hot = i == hoverPt || i == dragPt;
            auto e = juce::Rectangle<float> (hot ? ps + 3.0f : ps, hot ? ps + 3.0f : ps).withCentre (sp);
            g.setColour (Theme::bg);
            g.fillEllipse (e);
            g.setColour (hot ? juce::Colours::white : gateYellow);
            g.drawEllipse (e, 1.5f);
        }
    }

    // tête de lecture
    {
        int hh2 = 0;
        const float ph = shownPhase;
        const auto hp = toScreen (ph, pat.valueAt (ph, hh2));
        g.setColour (Theme::text.withAlpha (0.28f));
        g.drawVerticalLine (juce::roundToInt (hp.x), r.getY(), r.getBottom());
        const float gv = proc.uiGain.load();
        const auto gp = toScreen (ph, gv);
        g.setColour (Theme::accent.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (gp));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (gp));
    }

    g.setColour (Theme::line.brighter (0.2f));
    g.drawRect (full, 1.0f);
}

void GateGraph::mouseMove (const juce::MouseEvent& e)
{
    const auto v = fromScreen (e.position);
    hoverCell = juce::jlimit (0, proc.gridSteps() - 1, (int) (v.x * proc.gridSteps()));
    if (! stepsMode())
    {
        hoverPt = hitPoint (e.position);
        hoverHandle = hoverPt < 0 ? hitHandle (e.position) : -1;
        setMouseCursor (hoverHandle >= 0 ? juce::MouseCursor::UpDownResizeCursor
                        : hoverPt >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    }
    else setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void GateGraph::mouseDown (const juce::MouseEvent& e)
{
    const auto v = fromScreen (e.position);
    if (stepsMode())
    {
        pushUndo();
        const int g = proc.gridSteps();
        const int cell = juce::jlimit (0, g - 1, (int) (v.x * g));
        levelPaint = e.mods.isShiftDown();
        if (levelPaint) paintValue = v.y;
        else
        {
            const float cur = pat.valueAt (((float) cell + 0.5f) / g);
            paintValue = e.mods.isRightButtonDown() ? 0.0f : (cur > 0.5f ? 0.0f : 1.0f);
        }
        paintCell (cell, paintValue);
        lastCell = cell;
        publish();
        return;
    }

    dragPt = hitPoint (e.position);
    dragHandle = dragPt < 0 ? hitHandle (e.position) : -1;
    if (e.mods.isRightButtonDown())
    {
        if (dragPt > 0 && dragPt < pat.n - 1)
        {
            pushUndo();
            auto vv = pat.toVector();
            vv.erase (vv.begin() + dragPt);
            pat.fromVector (vv);
            publish();
        }
        else if (dragHandle >= 0) { pushUndo(); pat.pts[dragHandle].curve = 0.0f; publish(); }
        dragPt = dragHandle = -1;
        return;
    }
    pushUndo();
    if (dragHandle >= 0) { handleStart = pat.pts[dragHandle].curve; return; }
    if (dragPt < 0 && pat.n < GatePattern::maxPts - 2)
    {
        // nouveau point posé sur la grille
        auto vv = pat.toVector();
        const float x = juce::jlimit (0.0f, 1.0f, snapX (v.x, e.mods.isCommandDown()));
        GatePattern::Pt np { x, v.y, 0.0f };
        // insertion après les points de même x
        auto it = std::upper_bound (vv.begin(), vv.end(), x, [] (float xx, const GatePattern::Pt& p) { return xx < p.x; });
        // la courbure du segment coupé est conservée des deux côtés
        if (it != vv.begin()) np.curve = (it - 1)->curve;
        const int idx = (int) (it - vv.begin());
        vv.insert (it, np);
        pat.n = 0;
        for (auto& p : vv) pat.pts[pat.n++] = p; // pas de nettoyage pendant le geste
        dragPt = idx;
        publish();
    }
}

void GateGraph::mouseDrag (const juce::MouseEvent& e)
{
    const auto v = fromScreen (e.position);
    if (stepsMode())
    {
        if (lastCell < 0) return;
        const int g = proc.gridSteps();
        const int cell = juce::jlimit (0, g - 1, (int) (v.x * g));
        if (levelPaint) paintValue = v.y;
        const int a = juce::jmin (cell, lastCell), b = juce::jmax (cell, lastCell);
        for (int c = a; c <= b; ++c) if (c != lastCell || levelPaint) paintCell (c, paintValue);
        lastCell = cell;
        hoverCell = cell;
        publish();
        return;
    }
    if (dragHandle >= 0)
    {
        const auto& a = pat.pts[dragHandle]; const auto& b = pat.pts[dragHandle + 1];
        const float dir = b.y < a.y ? 1.0f : -1.0f;
        const float dy = -(float) e.getDistanceFromDragStartY() / 120.0f;
        pat.pts[dragHandle].curve = juce::jlimit (-1.0f, 1.0f, handleStart + dir * dy);
        publish();
        return;
    }
    if (dragPt < 0) return;
    float x = snapX (v.x, e.mods.isCommandDown());
    const int n = pat.n;
    if (dragPt == 0) x = 0.0f;
    else if (dragPt == n - 1) x = 1.0f;
    else x = juce::jlimit (pat.pts[dragPt - 1].x, pat.pts[dragPt + 1].x, x);
    pat.pts[dragPt].x = x;
    pat.pts[dragPt].y = v.y;
    publish();
}

void GateGraph::mouseUp (const juce::MouseEvent&)
{
    if (! stepsMode() && (dragPt >= 0 || dragHandle >= 0))
    {
        pat.fromVector (pat.toVector()); // nettoyage (points doublés, plats inutiles)
        publish();
    }
    dragPt = dragHandle = lastCell = -1;
}

void GateGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (stepsMode()) return;
    const int i = hitPoint (e.position);
    if (i > 0 && i < pat.n - 1)
    {
        pushUndo();
        auto vv = pat.toVector();
        vv.erase (vv.begin() + i);
        pat.fromVector (vv);
        publish();
        return;
    }
    const int hnd = hitHandle (e.position);
    if (hnd >= 0) { pushUndo(); pat.pts[hnd].curve = 0.0f; publish(); }
}

// =====================================================================================================
// Éditeur
// =====================================================================================================
GateEditor::GateEditor (GateProcessor& p)
    : AudioProcessorEditor (p), proc (p), graph (p),
      hzKnob (p, "hz", "Vitesse", IDG_T ("Vitesse libre en Hz (mode Hz) : nombre de cycles du motif par seconde.")),
      swingKnob (p, "swing", "Swing", IDG_T ("Décale un pas sur deux (selon la grille) : 0 % = droit, 100 % = shuffle très marqué.")),
      offsetKnob (p, "offset", IDG_T ("Décalage"), IDG_T ("Décale la lecture du motif dans le temps (phase).")),
      attackKnob (p, "attack", "Attaque", IDG_T ("Temps d'ouverture du gate. Court = attaque sèche et tendue ; long = fondu.")),
      releaseKnob (p, "release", IDG_T ("Relâche"), IDG_T ("Temps de fermeture du gate. Court = coupe nette ; long = queue douce.")),
      depthKnob (p, "depth", "Profondeur", IDG_T ("Profondeur du gate : 100 % = silence complet sur les pas fermés."))
{
    // taille mémorisée lue avant setResizable (qui déclenche resized())
    float remembered = proc.editorScale.load();
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.addAndMakeVisible (graph);

    stepsBtn = &addButton ("PAS", IDG_T ("Mode PAS : on/off par case, façon Transgate."), [this] { proc.editMode = 0; updateModeButtons(); });
    curveBtn = &addButton ("COURBE", IDG_T ("Mode COURBE : points libres et courbures, façon ShaperBox / Filtrator."), [this] { proc.editMode = 1; updateModeButtons(); });
    snapBtn = &addButton ("AIMANT", IDG_T ("Les points se calent sur la grille (Ctrl pour poser hors grille)."), [this] { proc.snap = ! proc.snap.load(); updateModeButtons(); });
    addButton (IDG_T ("◄"), IDG_T ("Décale le motif d'un pas vers la gauche."), [this] { graph.shiftSteps (-1); });
    addButton (IDG_T ("►"), IDG_T ("Décale le motif d'un pas vers la droite."), [this] { graph.shiftSteps (1); });
    addButton ("INVERSER", IDG_T ("Ouvert <-> fermé."), [this] { graph.invert(); });
    addButton (IDG_T ("ALÉA"), IDG_T ("Motif aléatoire sur la grille actuelle."), [this] { graph.randomise(); });
    addButton ("VIDER", IDG_T ("Remet le motif à plat (tout ouvert)."), [this] { graph.clearAll (true); });
    undoBtn = &addButton ("ANNULER", "Ctrl+Z", [this] { graph.undo(); });
    redoBtn = &addButton (IDG_T ("RÉTABLIR"), "Ctrl+Maj+Z", [this] { graph.redo(); });
    graph.onHistoryChanged = [this] { undoBtn->setEnabled (graph.canUndo()); redoBtn->setEnabled (graph.canRedo()); };
    graph.onHistoryChanged();

    presetBox.setTextWhenNothingSelected ("MOTIFS...");
    presetBox.addItemList (GatePattern::presetNames(), 1);
    presetBox.setTooltip (IDG_T ("Motifs de départ (on peut ensuite les retoucher)."));
    presetBox.onChange = [this]
    {
        const int i = presetBox.getSelectedItemIndex();
        if (i >= 0) graph.applyPreset (i);
        presetBox.setSelectedId (0, juce::dontSendNotification);
    };
    content.addAndMakeVisible (presetBox);

    modeBox = &addCombo ("mode", IDG_T ("Tempo : calé sur le morceau. Hz : vitesse libre."));
    lengthBox = &addCombo ("length", IDG_T ("Durée d'un cycle du motif (T = triolet, . = pointé)."));
    gridBox = &addCombo ("grid", IDG_T ("Nombre de pas de la grille (édition, aimant, swing, décalage ◄ ►)."));
    retrigBox = &addCombo ("retrig", IDG_T ("Hôte : suit la position du morceau. Note MIDI : chaque note relance le motif au début."));

    for (auto* k : { &hzKnob, &swingKnob, &offsetKnob }) content.addAndMakeVisible (k);
    for (auto* k : { &attackKnob, &releaseKnob, &depthKnob }) { k->colour = gateYellow; content.addAndMakeVisible (k); }
    modeBox->onChange = [this] { updateModeButtons(); };

    tooltip = std::make_unique<juce::TooltipWindow> (this, 600);
    content.setSize (baseW, baseH); // après la création de tous les éléments (resized() les place)
    updateModeButtons();

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) baseW / baseH);
        c->setSizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    }
    const float scale = juce::jlimit (0.6f, 2.0f, remembered > 0.0f ? remembered : 0.9f);
    setSize ((int) (baseW * scale), (int) (baseH * scale));
    sizeReady = true;
    setWantsKeyboardFocus (true);
    startTimerHz (10);
}

GateEditor::~GateEditor()
{
    stopTimer();
    tooltip.reset();
    setLookAndFeel (nullptr);
}

juce::TextButton& GateEditor::addButton (const juce::String& text, const juce::String& tip, std::function<void()> fn)
{
    auto* b = buttons.add (new juce::TextButton (text));
    b->setTooltip (tip);
    b->onClick = std::move (fn);
    content.addAndMakeVisible (b);
    return *b;
}

juce::ComboBox& GateEditor::addCombo (const juce::String& id, const juce::String& tip)
{
    auto* c = combos.add (new juce::ComboBox());
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->addItemList (prm->choices, 1);
    c->setTooltip (tip);
    content.addAndMakeVisible (c);
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, id, *c));
    return *c;
}

void GateEditor::updateModeButtons()
{
    const bool steps = proc.editMode.load() == 0;
    stepsBtn->setToggleState (steps, juce::dontSendNotification);
    curveBtn->setToggleState (! steps, juce::dontSendNotification);
    snapBtn->setToggleState (proc.snap.load(), juce::dontSendNotification);
    snapBtn->setEnabled (! steps);
    const bool hz = modeBox->getSelectedItemIndex() == GateProcessor::ModeHz;
    hzKnob.setEnabled (hz);
    lengthBox->setEnabled (! hz);
    graph.repaint();
    content.repaint();
}

void GateEditor::timerCallback()
{
    // un état rechargé par l'hôte peut changer le mode d'édition
    if (stepsBtn->getToggleState() != (proc.editMode.load() == 0) || snapBtn->getToggleState() != proc.snap.load())
        updateModeButtons();
    // ligne d'état sous le graphe (durée du cycle, nombre de pas)
    const auto gb = graph.getBounds();
    content.repaint (gb.getX(), gb.getBottom(), gb.getWidth(), 22);
}

bool GateEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (k.getModifiers().isShiftDown()) graph.redo(); else graph.undo();
        return true;
    }
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y')) { graph.redo(); return true; }
    return false;
}

void GateEditor::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void GateEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    if (sizeReady) proc.editorScale = s;
}

void GateEditor::Content::resized()
{
    // fond texturé : roche noire, fissures incandescentes, poussière (calculé une fois)
    bg = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (bg);
    g.fillAll (Theme::bg);
    juce::Random r (1337);
    for (int i = 0; i < 9000; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (r.nextFloat() * 0.035f));
        g.fillRect ((float) r.nextInt (getWidth()), (float) r.nextInt (getHeight()), 1.0f + r.nextFloat(), 1.0f + r.nextFloat());
    }
    for (int c = 0; c < 5; ++c)
    {
        juce::Path crack;
        float x = (float) r.nextInt (getWidth()), y = c % 2 == 0 ? (float) getHeight() : 0.0f;
        crack.startNewSubPath (x, y);
        for (int k = 0; k < 9; ++k)
        {
            x += (r.nextFloat() - 0.5f) * 60.0f;
            y += (c % 2 == 0 ? -1.0f : 1.0f) * (8.0f + r.nextFloat() * 18.0f);
            crack.lineTo (x, y);
        }
        g.setColour (Theme::accent.withAlpha (0.05f));
        g.strokePath (crack, juce::PathStrokeType (6.0f));
        g.setColour (Theme::accent.withAlpha (0.22f));
        g.strokePath (crack, juce::PathStrokeType (1.0f));
    }
    for (int i = 0; i < 40; ++i)
    {
        const float s = 1.0f + r.nextFloat() * 2.5f;
        g.setColour ((r.nextFloat() < 0.8f ? Theme::accent : Theme::blue).withAlpha (0.1f + r.nextFloat() * 0.25f));
        g.fillEllipse ((float) r.nextInt (getWidth()), (float) r.nextInt (getHeight()), s, s);
    }
    juce::ColourGradient vig (juce::Colours::transparentBlack, getWidth() * 0.5f, getHeight() * 0.45f,
                              juce::Colours::black.withAlpha (0.6f), 0.0f, 0.0f, true);
    g.setGradientFill (vig);
    g.fillAll();

    // placement
    auto& e = ed;
    auto area = getLocalBounds().reduced (18, 0);
    area.removeFromTop (58);
    auto bar = area.removeFromTop (28);
    e.stepsBtn->setBounds (bar.removeFromLeft (64)); bar.removeFromLeft (2);
    e.curveBtn->setBounds (bar.removeFromLeft (76)); bar.removeFromLeft (8);
    e.snapBtn->setBounds (bar.removeFromLeft (72)); bar.removeFromLeft (14);
    for (int i = 3; i < e.buttons.size(); ++i)
    {
        auto* b = e.buttons[i];
        const int w = b->getButtonText().length() <= 2 ? 30 : juce::jmax (60, b->getButtonText().length() * 9 + 10);
        if (b == e.undoBtn) bar.removeFromLeft (14);
        b->setBounds (bar.removeFromLeft (w));
        bar.removeFromLeft (4);
    }
    e.presetBox.setBounds (bar.removeFromRight (180));
    area.removeFromTop (8);
    e.graph.setBounds (area.removeFromTop (300));
    area.removeFromTop (24);
    auto bottom = area.removeFromTop (166);
    auto timeBox = bottom.removeFromLeft (590);
    bottom.removeFromLeft (16);
    auto shapeBox = bottom;

    auto tb = timeBox.reduced (12, 10);
    tb.removeFromTop (16);
    auto col1 = tb.removeFromLeft (150);
    tb.removeFromLeft (12);
    auto col2 = tb.removeFromLeft (150);
    tb.removeFromLeft (8);
    auto placeCombo = [] (juce::Rectangle<int>& col, juce::ComboBox* c) { col.removeFromTop (16); c->setBounds (col.removeFromTop (26)); col.removeFromTop (14); };
    placeCombo (col1, e.modeBox);
    placeCombo (col1, e.lengthBox);
    placeCombo (col2, e.gridBox);
    placeCombo (col2, e.retrigBox);
    const int kw = tb.getWidth() / 3;
    for (auto* k : { &e.hzKnob, &e.swingKnob, &e.offsetKnob }) k->setBounds (tb.removeFromLeft (kw).withTrimmedTop (4));
    auto sb = shapeBox.reduced (12, 10);
    sb.removeFromTop (16);
    const int kw2 = sb.getWidth() / 3;
    for (auto* k : { &e.attackKnob, &e.releaseKnob, &e.depthKnob }) k->setBounds (sb.removeFromLeft (kw2).withTrimmedTop (4));
}

void GateEditor::Content::paint (juce::Graphics& g)
{
    g.drawImageAt (bg, 0, 0);
    auto& e = ed;
    // titre
    Theme::cutText (g, "IDGEMS", { 18.0f, 12.0f, 200.0f, 34.0f }, 26.0f, Theme::text, juce::Justification::centredLeft, 0.35f);
    Theme::cutText (g, "GATE", { 206.0f, 12.0f, 160.0f, 34.0f }, 26.0f, Theme::accent, juce::Justification::centredLeft, 0.8f, false);
    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::textDim);
    g.drawText (IDG_T ("gate rythmique dessinable  //  stutter on/off"), juce::Rectangle<float> (370.0f, 22.0f, 300.0f, 16.0f),
                juce::Justification::centredLeft, false);

    // cadres du bas
    const auto gb = e.graph.getBounds();
    auto boxTime = juce::Rectangle<float> ((float) gb.getX(), (float) gb.getBottom() + 24.0f, 590.0f, 166.0f);
    auto boxShape = juce::Rectangle<float> (boxTime.getRight() + 16.0f, boxTime.getY(), (float) gb.getRight() - boxTime.getRight() - 16.0f, 166.0f);
    Theme::panelFrame (g, boxTime, Theme::accent);
    Theme::panelFrame (g, boxShape, gateYellow);
    Theme::sectionTitle (g, "TEMPS", boxTime.reduced (12.0f, 8.0f), Theme::accent);
    Theme::sectionTitle (g, IDG_T ("FORME DU GATE"), boxShape.reduced (12.0f, 8.0f), gateYellow);

    // étiquettes des listes
    g.setFont (Theme::mono (9.5f, true));
    g.setColour (Theme::textDim);
    for (auto [c, t] : { std::pair<juce::ComboBox*, const char*> { e.modeBox, "MODE" }, { e.lengthBox, "DUR\xc3\x89" "E DU CYCLE" },
                         { e.gridBox, "GRILLE" }, { e.retrigBox, "RED\xc3\x89MARRAGE" } })
        g.drawText (juce::String (juce::CharPointer_UTF8 (t)), c->getBounds().toFloat().translated (0, -15.0f).withHeight (14.0f),
                    juce::Justification::centredLeft, false);

    // ligne d'état sous le graphe
    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::textDim);
    const auto len = e.modeBox->getSelectedItemIndex() == GateProcessor::ModeHz
                         ? e.hzKnob.getTextFromValue (e.hzKnob.getValue())
                         : e.lengthBox->getText();
    const bool stepsMode = e.proc.editMode.load() == 0;
    g.drawText (stepsMode ? IDG_T ("PAS : clic / glisser = allumer-éteindre  •  clic droit = éteindre  •  Maj + glisser = niveau du pas")
                          : IDG_T ("COURBE : clic = point  •  glisser = déplacer  •  losange bleu = courbure  •  double-clic / clic droit = supprimer  •  Ctrl = hors grille"),
                juce::Rectangle<float> ((float) gb.getX(), (float) gb.getBottom() + 4.0f, (float) gb.getWidth() - 190.0f, 14.0f),
                juce::Justification::centredLeft, true);
    g.drawText (IDG_T ("cycle : ") + len + "   //   " + juce::String (e.proc.gridSteps()) + " pas",
                juce::Rectangle<float> ((float) gb.getX(), (float) gb.getBottom() + 4.0f, (float) gb.getWidth(), 14.0f),
                juce::Justification::centredRight, false);
}
} // namespace idg::gate
