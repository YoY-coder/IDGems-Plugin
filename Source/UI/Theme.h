#pragma once
#include "../Core/ParamDefs.h"

namespace idg::ui
{
/** Direction artistique : noir texturé, accent orange, touches de bleu, aberration chromatique. */
struct Theme
{
    static inline juce::Colour accent { 0xffff6a00 };
    static inline juce::Colour blue { 0xff2fa8ff };
    static inline juce::Colour bg { 0xff0a0a0c };
    static inline juce::Colour panel { 0xff121216 };
    static inline juce::Colour panelHi { 0xff1b1b21 };
    static inline juce::Colour line { 0xff2a2a33 };
    static inline juce::Colour text { 0xffe8e6e3 };
    static inline juce::Colour textDim { 0xff8a8892 };
    static inline juce::Colour red { 0xffff2d55 };
    static inline bool tooltips = true;
    static inline float glitchAmount = 0.5f;

    static juce::Font mono (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
    }
    static juce::Font sans (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    /** Texte avec aberration chromatique (rouge / bleu décalés). */
    static void glitchText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
                            juce::Colour c, float offset = 1.2f)
    {
        g.setColour (juce::Colour (0xffff1e3c).withAlpha (0.55f));
        g.drawText (s, r.translated (-offset, 0), j, false);
        g.setColour (blue.withAlpha (0.55f));
        g.drawText (s, r.translated (offset, 0.3f), j, false);
        g.setColour (c);
        g.drawText (s, r, j, false);
    }

    /** Cadre de panneau avec coins « fracturés ». */
    static void panelFrame (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edge, float alpha = 1.0f)
    {
        juce::Path p;
        const float c = 7.0f;
        p.startNewSubPath (r.getX() + c, r.getY());
        p.lineTo (r.getRight(), r.getY());
        p.lineTo (r.getRight(), r.getBottom() - c);
        p.lineTo (r.getRight() - c, r.getBottom());
        p.lineTo (r.getX(), r.getBottom());
        p.lineTo (r.getX(), r.getY() + c);
        p.closeSubPath();
        g.setColour (panel.withAlpha (0.82f * alpha));
        g.fillPath (p);
        g.setColour (edge.withAlpha (0.35f * alpha));
        g.strokePath (p, juce::PathStrokeType (1.0f));
        // petites marques de coin
        g.setColour (edge.withAlpha (0.9f * alpha));
        g.fillRect (r.getX() + c, r.getY() - 1.0f, 14.0f, 2.0f);
        g.fillRect (r.getRight() - c - 14.0f, r.getBottom() - 1.0f, 14.0f, 2.0f);
    }

    static void sectionTitle (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setFont (mono (11.0f, true));
        glitchText (g, s, r, juce::Justification::centredLeft, c, 0.8f);
    }
};
} // namespace idg::ui
