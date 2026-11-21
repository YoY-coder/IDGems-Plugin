#include "SoloWidgets.h"

namespace idg::ui::solo
{
namespace
{
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
}

// ---------------------------------------------------------------------------
// NeonKnob
// ---------------------------------------------------------------------------
NeonKnob::NeonKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::Colour c,
                    const juce::String& lbl, const juce::String& h)
    : param (s.getParameter (id)), colour (c), label (lbl), help (h)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (startAngle, endAngle, true);
    setMouseDragSensitivity (260);
    setPopupMenuEnabled (false);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, *this);
    if (param != nullptr) setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void NeonKnob::tick()
{
    if (param == nullptr) return;
    const float n = param->getValue();
    if (n > 0.15f && rng.nextFloat() < 0.15f + 0.6f * n * n)
    {
        flicker = rng.nextFloat() < 0.2f * n ? 1.0f : rng.nextFloat() * 0.35f;
        repaint();
    }
    else if (flicker > 0.0f) { flicker = 0.0f; repaint(); }
}

void NeonKnob::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    auto bounds = getLocalBounds().toFloat();
    const float labelH = big ? 17.0f : 14.0f, valueH = big ? 17.0f : 14.0f;
    auto lab = bounds.removeFromTop (labelH);
    auto val = bounds.removeFromBottom (valueH);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    auto dial = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
    const auto centre = dial.getCentre();
    const float radius = size * 0.5f;
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    const float norm = param->getValue();
    auto angleOf = [] (float v) { return startAngle + juce::jlimit (0.0f, 1.0f, v) * (endAngle - startAngle); };
    auto arc = [&] (float r, float a0, float a1, juce::Colour c, float thick)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    };

    const float ringR = radius - 3.0f;
    const auto range = param->getNormalisableRange();
    const bool bipolar = range.start < 0.0f && range.end > 0.0f;
    const float zeroN = bipolar ? param->convertTo0to1 (0.0f) : 0.0f;
    arc (ringR, startAngle, endAngle, Theme::line.withAlpha (0.9f * alpha), 2.0f);
    {
        const float amount = bipolar ? std::abs (norm - zeroN) / juce::jmax (zeroN, 1.0f - zeroN) : norm;
        const float intensity = juce::jlimit (0.0f, 1.0f, amount);
        const float flick = 1.0f - flicker * (0.08f + 0.3f * intensity * intensity);
        const float bloom = (0.2f + 0.8f * intensity) * flick * alpha;
        const float a0 = bipolar ? angleOf (zeroN) : startAngle, a1 = angleOf (norm);
        if (std::abs (a1 - a0) > 0.01f)
        {
            const float spread = 2.0f + 7.0f * intensity;
            for (int k = 4; k >= 1; --k)
                arc (ringR, a0, a1, colour.withAlpha (bloom * 0.11f * (float) (5 - k) / 4.0f), 3.0f + spread * (float) k * 0.55f);
            arc (ringR, a0, a1, colour.withAlpha (juce::jmin (1.0f, 0.75f + 0.25f * bloom) * alpha), 3.2f);
            arc (ringR, a0, a1, colour.interpolatedWith (juce::Colours::white, 0.35f + 0.45f * intensity).withAlpha ((0.5f + 0.5f * bloom) * alpha), 1.2f);
            const float ae = a1 - juce::MathConstants<float>::halfPi;
            const juce::Point<float> tip { centre.x + std::cos (ae) * ringR, centre.y + std::sin (ae) * ringR };
            const float gs = 5.0f + 9.0f * intensity;
            g.setColour (colour.withAlpha (bloom * 0.35f));
            g.fillEllipse (juce::Rectangle<float> (gs, gs).withCentre (tip));
            g.setColour (juce::Colours::white.withAlpha ((0.6f + 0.4f * bloom) * alpha));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (tip));
        }
    }
    // corps octogonal
    auto body = dial.reduced (size * 0.25f);
    juce::Path oct;
    for (int i = 0; i < 8; ++i)
    {
        const float a = juce::MathConstants<float>::pi * (0.125f + (float) i * 0.25f);
        juce::Point<float> pt { centre.x + std::cos (a) * body.getWidth() * 0.52f, centre.y + std::sin (a) * body.getWidth() * 0.52f };
        if (i == 0) oct.startNewSubPath (pt); else oct.lineTo (pt);
    }
    oct.closeSubPath();
    g.setColour (Theme::panelHi.withAlpha (alpha));
    g.fillPath (oct);
    g.setColour (Theme::line.brighter (0.35f).withAlpha (alpha));
    g.strokePath (oct, juce::PathStrokeType (1.0f));
    {
        const float a = angleOf (norm) - juce::MathConstants<float>::halfPi;
        const float r0 = body.getWidth() * 0.12f, r1 = body.getWidth() * 0.5f;
        g.setColour (colour.withAlpha (alpha));
        g.drawLine (centre.x + std::cos (a) * r0, centre.y + std::sin (a) * r0, centre.x + std::cos (a) * r1, centre.y + std::sin (a) * r1, 2.2f);
    }

    g.setFont (Theme::mono (big ? 11.5f : 10.0f, true));
    g.setColour (Theme::textDim.withAlpha (alpha));
    g.drawFittedText (label.toUpperCase(), lab.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    g.setFont (Theme::mono (big ? 13.0f : 11.0f, true));
    g.setColour ((isMouseButtonDown() ? colour.brighter (0.4f) : colour.interpolatedWith (Theme::text, 0.35f)).withAlpha (alpha));
    const auto txt = valueText ? valueText() : param->getCurrentValueAsText();
    g.drawFittedText (txt, val.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

// ---------------------------------------------------------------------------
// ChoiceButtons
// ---------------------------------------------------------------------------
ChoiceButtons::ChoiceButtons (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::StringArray l,
                              juce::Colour c, const juce::String& h)
    : param (s.getParameter (id)), labels (std::move (l)), colour (c), help (h)
{
    if (param != nullptr)
        att = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            current = juce::jlimit (0, labels.size() - 1, (int) std::lround (v));
            repaint();
            if (onChange) onChange();
        });
    if (att) att->sendInitialUpdate();
    setTooltip (help);
}

int ChoiceButtons::indexAt (juce::Point<int> p) const
{
    if (labels.isEmpty() || getWidth() <= 0) return -1;
    return juce::jlimit (0, labels.size() - 1, p.x * labels.size() / getWidth());
}

void ChoiceButtons::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i < 0 || att == nullptr || ! isEnabled()) return;
    if (isDisabled && isDisabled (i)) return;
    att->setValueAsCompleteGesture ((float) i);
}

void ChoiceButtons::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hover) { hover = i; repaint(); }
}

void ChoiceButtons::paint (juce::Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float w = b.getWidth() / (float) n;
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    g.setFont (Theme::mono (juce::jmin (11.0f, b.getHeight() * 0.48f), true));
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (1.5f, 0.0f);
        const bool sel = i == current;
        const bool dis = isDisabled && isDisabled (i);
        if (sel)
        {
            g.setColour (colour.withAlpha (0.22f * alpha));
            g.fillRect (r.expanded (2.0f, 2.0f));
            g.setColour (colour.withAlpha (alpha));
            g.fillRect (r);
        }
        else
        {
            g.setColour (Theme::panelHi.withAlpha (alpha));
            g.fillRect (r);
            g.setColour ((i == hover && ! dis ? colour.withAlpha (0.8f) : Theme::line.brighter (0.2f)).withAlpha (alpha));
            g.drawRect (r, 1.0f);
        }
        g.setColour (sel ? juce::Colours::black : (dis ? Theme::textDim.withAlpha (0.3f) : Theme::text.withAlpha (0.85f * alpha)));
        g.drawFittedText (labels[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.6f);
    }
}

// ---------------------------------------------------------------------------
// FlatButton
// ---------------------------------------------------------------------------
void FlatButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    if (over && isEnabled())
    {
        g.setColour (colour.withAlpha (0.2f));
        g.fillRect (r.expanded (2.0f));
    }
    g.setColour ((down ? colour.darker (0.3f) : Theme::panelHi).withAlpha (alpha));
    g.fillRect (r);
    g.setColour (colour.withAlpha (alpha));
    g.drawRect (r, 1.2f);
    g.fillRect (r.getX(), r.getBottom() - 3.0f, r.getWidth() * 0.25f, 3.0f);
    g.setFont (Theme::mono (juce::jmin (13.0f, r.getHeight() * 0.45f), true));
    g.setColour ((down ? juce::Colours::black : Theme::text).withAlpha (alpha));
    g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

// ---------------------------------------------------------------------------
// Fond
// ---------------------------------------------------------------------------
juce::Image makeSoloBackground (int w, int h, uint32_t seed)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (Theme::bg);
    juce::Random rng ((juce::int64) seed);

    // grain de roche
    for (int i = 0; i < w * h / 6; ++i)
    {
        const float v = rng.nextFloat();
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.03f * v * v));
        g.fillRect ((float) rng.nextInt (w), (float) rng.nextInt (h), 1.0f, 1.0f);
    }
    // taches sombres / lumière froide en haut à gauche
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x22c8d8ff), 0.0f, 0.0f, juce::Colours::transparentBlack,
                                             (float) w * 0.6f, (float) h * 0.7f, true));
    g.fillRect (0, 0, w, h);

    // fissures incandescentes
    auto crack = [&] (juce::Point<float> p, float angle, int segs, float thick)
    {
        juce::Path path;
        path.startNewSubPath (p);
        for (int s = 0; s < segs; ++s)
        {
            angle += (rng.nextFloat() - 0.5f) * 0.9f;
            const float len = 6.0f + rng.nextFloat() * 16.0f;
            p += { std::cos (angle) * len, std::sin (angle) * len };
            path.lineTo (p);
        }
        g.setColour (Theme::accent.withAlpha (0.05f));
        g.strokePath (path, juce::PathStrokeType (thick * 7.0f));
        g.setColour (Theme::accent.withAlpha (0.16f));
        g.strokePath (path, juce::PathStrokeType (thick * 2.5f));
        g.setColour (juce::Colour (0xffffb070).withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (thick * 0.7f));
    };
    for (int i = 0; i < 4; ++i)
    {
        const bool bottom = i % 2 == 0;
        crack ({ rng.nextFloat() * (float) w, bottom ? (float) h + 2.0f : -2.0f },
               bottom ? -juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi, 6 + rng.nextInt (7),
               0.8f + rng.nextFloat() * 0.8f);
    }
    // vignette
    juce::ColourGradient vig (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.45f,
                              juce::Colours::black.withAlpha (0.65f), 0.0f, 0.0f, true);
    g.setGradientFill (vig);
    g.fillRect (0, 0, w, h);
    return img;
}
} // namespace idg::ui::solo
