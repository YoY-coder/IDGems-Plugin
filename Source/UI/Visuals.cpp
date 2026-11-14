#include "Visuals.h"

namespace idg::ui
{
juce::Image makeBackgroundTexture (int w, int h, juce::Colour accent)
{
    juce::Image img (juce::Image::RGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Random r (1337);
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                float n = r.nextFloat();
                int v = 8 + (int) (n * n * 12.0f);
                // trame de balayage
                if (y % 3 == 0) v -= 2;
                bd.setPixelColour (x, y, juce::Colour::fromRGB ((juce::uint8) v, (juce::uint8) v, (juce::uint8) (v + 2)));
            }
    }
    juce::Graphics g (img);
    // rayures et fractures
    for (int i = 0; i < 70; ++i)
    {
        float x = r.nextFloat() * (float) w, y = r.nextFloat() * (float) h;
        float len = 20.0f + r.nextFloat() * 220.0f;
        g.setColour (juce::Colours::white.withAlpha (0.015f + r.nextFloat() * 0.03f));
        g.drawLine (x, y, x + len, y + (r.nextFloat() - 0.5f) * 6.0f, 0.6f);
    }
    for (int i = 0; i < 14; ++i)
    {
        float y = r.nextFloat() * (float) h;
        float hh = 1.0f + r.nextFloat() * 5.0f;
        g.setColour ((r.nextBool() ? accent : Theme::blue).withAlpha (0.03f + r.nextFloat() * 0.04f));
        g.fillRect (r.nextFloat() * (float) w * 0.5f, y, (float) w * (0.2f + r.nextFloat() * 0.6f), hh);
    }
    // grille fine
    g.setColour (juce::Colours::white.withAlpha (0.018f));
    for (int x = 0; x < w; x += 32) g.drawVerticalLine (x, 0.0f, (float) h);
    for (int y = 0; y < h; y += 32) g.drawHorizontalLine (y, 0.0f, (float) w);
    // vignettage
    juce::ColourGradient vg (juce::Colours::transparentBlack, (float) w * 0.5f, (float) h * 0.45f,
                             juce::Colours::black.withAlpha (0.6f), 0.0f, 0.0f, true);
    g.setGradientFill (vg);
    g.fillRect (0, 0, w, h);
    return img;
}

// ---------------------------------------------------------------------------
void CorruptScope::push (const float* data, int n)
{
    for (int i = 0; i < n; ++i)
    {
        wave[(size_t) writePos] = data[i];
        writePos = (writePos + 1) % (int) wave.size();
    }
    // spectre grossier
    for (int i = 0; i < 512; ++i)
    {
        float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 512.0f);
        fftData[(size_t) i] = wave[(size_t) ((writePos + 512 + i) % (int) wave.size())] * w;
    }
    std::fill (fftData.begin() + 512, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int b = 0; b < 64; ++b)
    {
        int lo = (int) std::pow (2.0, b / 64.0 * 8.0), hi = juce::jmax (lo + 1, (int) std::pow (2.0, (b + 1) / 64.0 * 8.0));
        float m = 0;
        for (int k = lo; k < hi && k < 256; ++k) m = juce::jmax (m, fftData[(size_t) k]);
        float db = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (m / 64.0f) + 60.0f) / 60.0f);
        bands[(size_t) b] = juce::jmax (db, bands[(size_t) b] * 0.85f);
    }
}

void CorruptScope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight(), w = r.getWidth();
    // spectre en barres fracturées
    const float bw = w / 64.0f;
    for (int b = 0; b < 64; ++b)
    {
        float v = bands[(size_t) b];
        float bh = v * h * 0.8f;
        float x = r.getX() + b * bw;
        float shift = (rng.nextFloat() < intensity * 0.15f) ? (rng.nextFloat() - 0.5f) * 20.0f : 0.0f;
        g.setColour (Theme::accent.withAlpha (alpha * (0.12f + v * 0.25f)));
        g.fillRect (x + shift, r.getBottom() - bh, bw - 1.5f, bh);
        if (v > 0.7f) { g.setColour (Theme::blue.withAlpha (alpha * 0.3f)); g.fillRect (x + shift + 1.5f, r.getBottom() - bh, bw - 2.5f, 2.0f); }
    }
    // forme d'onde, tranches décalées, aberration chromatique
    const int n = (int) wave.size();
    auto drawWave = [&] (juce::Colour c, float dx)
    {
        juce::Path p;
        int slice = 0;
        float off = 0;
        for (int i = 0; i < n; i += 2)
        {
            if (i / 128 != slice)
            {
                slice = i / 128;
                off = rng.nextFloat() < intensity * 0.35f ? (rng.nextFloat() - 0.5f) * h * 0.3f : 0.0f;
            }
            float x = r.getX() + (float) i / (float) n * w + dx;
            float y = r.getCentreY() - wave[(size_t) ((writePos + i) % n)] * h * 0.42f + off;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    };
    drawWave (juce::Colour (0xffff1e3c).withAlpha (alpha * 0.35f), -2.0f - intensity * 3.0f);
    drawWave (Theme::blue.withAlpha (alpha * 0.35f), 2.0f + intensity * 3.0f);
    drawWave (Theme::accent.withAlpha (alpha * 0.8f), 0.0f);
}

// ---------------------------------------------------------------------------
void GlitchOverlay::setLevel (float l)
{
    level = l;
    ++frame;
    if (level > 0.02f || frame % 15 == 0) repaint();
}

void GlitchOverlay::paint (juce::Graphics& g)
{
    const float amt = Theme::glitchAmount;
    if (amt <= 0.0f) return;
    const float k = juce::jlimit (0.0f, 1.0f, level * amt * 1.6f);
    const int w = getWidth(), h = getHeight();
    const int count = (int) (k * 14.0f) + (rng.nextFloat() < 0.04f * amt ? 1 : 0);
    for (int i = 0; i < count; ++i)
    {
        float y = rng.nextFloat() * (float) h;
        float hh = 1.0f + rng.nextFloat() * (2.0f + k * 10.0f);
        float x = rng.nextFloat() * (float) w * 0.7f;
        float ww = (float) w * (0.05f + rng.nextFloat() * 0.4f);
        auto c = rng.nextFloat() < 0.6f ? Theme::accent : (rng.nextBool() ? Theme::blue : juce::Colour (0xffff1e3c));
        g.setColour (c.withAlpha (0.05f + 0.25f * k * rng.nextFloat()));
        g.fillRect (x, y, ww, hh);
        // pixels morts
        if (rng.nextFloat() < k)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f * k));
            g.fillRect (x + rng.nextFloat() * ww, y, 2.0f + rng.nextFloat() * 6.0f, 1.0f);
        }
    }
    if (k > 0.5f && rng.nextFloat() < k * 0.3f)
    {
        // déchirure horizontale plein écran
        float y = rng.nextFloat() * (float) h;
        g.setColour (Theme::accent.withAlpha (0.08f * k));
        g.fillRect (0.0f, y, (float) w, 2.0f + 6.0f * k);
    }
}

// ---------------------------------------------------------------------------
void Meters::tick()
{
    auto fall = [] (float& v, float target) { v = target > v ? target : v * 0.86f; };
    for (int k = 0; k < 2; ++k)
    {
        fall (in[k], proc.meterIn[k].load());
        fall (out[k], proc.meterOut[k].load());
    }
    float g = 1.0f - proc.meterGR.load();
    gr = g > gr ? g : gr * 0.9f;
    float pk = juce::jmax (out[0], out[1]);
    if (pk >= peakOut) { peakOut = pk; peakHold = 45; }
    else if (--peakHold < 0) peakOut *= 0.95f;
    repaint();
}

void Meters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    Theme::sectionTitle (g, "// NIVEAUX", r.removeFromTop (16), Theme::accent);
    r.removeFromTop (4);
    auto toY = [] (float lin) { float db = juce::Decibels::gainToDecibels (lin, -60.0f); return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    auto bar = [&] (juce::Rectangle<float> b, float v, juce::Colour c)
    {
        g.setColour (Theme::line);
        g.fillRect (b);
        float f = toY (v);
        auto fill = b.withTrimmedTop (b.getHeight() * (1.0f - f));
        g.setColour (c);
        g.fillRect (fill);
        if (v > 0.98f) { g.setColour (Theme::red); g.fillRect (b.withHeight (3.0f)); }
    };
    auto labels = r.removeFromBottom (14);
    const float bw = 9.0f;
    auto area = r.reduced (4.0f, 0.0f);
    float x = area.getX();
    bar ({ x, area.getY(), bw, area.getHeight() }, in[0], Theme::blue); x += bw + 2;
    bar ({ x, area.getY(), bw, area.getHeight() }, in[1], Theme::blue); x += bw + 16;
    bar ({ x, area.getY(), bw, area.getHeight() }, out[0], Theme::accent); x += bw + 2;
    bar ({ x, area.getY(), bw, area.getHeight() }, out[1], Theme::accent); x += bw + 16;
    // réduction de gain (vers le bas)
    {
        juce::Rectangle<float> b { x, area.getY(), bw, area.getHeight() };
        g.setColour (Theme::line);
        g.fillRect (b);
        float db = juce::jlimit (0.0f, 24.0f, -juce::Decibels::gainToDecibels (1.0f - gr, -60.0f));
        g.setColour (Theme::red);
        g.fillRect (b.withHeight (b.getHeight() * db / 24.0f));
    }
    // échelle
    g.setFont (Theme::mono (9.0f));
    g.setColour (Theme::textDim);
    float sx = x + bw + 6;
    for (int db : { 0, -6, -12, -24, -48 })
    {
        float y = area.getY() + area.getHeight() * (1.0f - (db + 60.0f) / 60.0f);
        g.drawText (juce::String (db), juce::Rectangle<float> (sx, y - 5, 30, 10), juce::Justification::centredLeft);
    }
    g.drawText ("IN", juce::Rectangle<float> (area.getX(), labels.getY(), 2 * bw + 2, 12), juce::Justification::centred);
    g.drawText ("OUT", juce::Rectangle<float> (area.getX() + 2 * bw + 18, labels.getY(), 2 * bw + 2, 12), juce::Justification::centred);
    g.drawText ("GR", juce::Rectangle<float> (x - 2, labels.getY(), bw + 4, 12), juce::Justification::centred);
    g.setColour (peakOut > 0.98f ? Theme::red : Theme::text);
    g.drawText (juce::String (juce::Decibels::gainToDecibels (peakOut, -60.0f), 1) + " dB", juce::Rectangle<float> (sx + 30, area.getY(), 70, 12), juce::Justification::centredLeft);
}
} // namespace idg::ui
