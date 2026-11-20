#pragma once
#include "../Core/Dsp.h"

namespace idg
{
/** Stretch : à chaque pas rythmique (Rate), le son joue le temps d'un grain puis reste « coincé »
    sur cette frame, tenue par un nuage de grains (fenêtres de Hann, recouvrement x4).
    - Grain   : taille du fragment figé (petit = buzz métallique, grand = nappe étirée)
    - Texture : aléa de position / d'espacement des grains (0 = buzz net, 1 = granuleux)
    - Vitesse : avance de la tête de lecture (0 = gel total, 0.25 = étiré x4, 1 = temps réel)
    - Hold    : part du pas qui reste figée (le reste repasse le son direct)
    Pur DSP, sans allocation dans process(), utilisable seul (plugin Stretch) ou dans le rack. */
class StretchEngine
{
public:
    struct Settings
    {
        double rateBeats = 0.5;  // durée d'un pas en noires
        float grainMs = 60.0f;
        float texture = 0.25f;
        float speed = 0.0f;
        float hold = 1.0f;
        float mix = 1.0f;
    };

    static constexpr int numGrains = 24;
    static constexpr double maxGrainMs = 1000.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // retour en arrière max : grain * (1 + 1.5 * texture) + marge
        buf.allocate ((int) (sr * (maxGrainMs * 0.001 * 3.0 + 0.5)));
        xfLen = juce::jmax (8.0, sr * 0.003);
        reset();
    }

    void reset()
    {
        buf.clear();
        t = 0;
        lastStep = std::numeric_limits<int64_t>::min();
        for (auto& g : grains) g.active = false;
        anchor = 0; tickTime = 0; spawnAcc = 0;
        wet = 0.0f;
        cloud = false;
        captureEnd = cloudStart = 0;
        rng.seed (0x57E7C4u);
    }

    /** beatPos : position (en noires) au début du bloc ; spb : échantillons par noire. */
    void process (float* L, float* R, int n, double beatPos, double spb, const Settings& s)
    {
        const double G = juce::jlimit (16.0, maxGrainMs * 0.001 * sr, (double) s.grainMs * 0.001 * sr);
        const double hop = G * 0.25;
        const float tex = juce::jlimit (0.0f, 1.0f, s.texture);
        const float speed = juce::jlimit (0.0f, 1.0f, s.speed);
        const double rate = juce::jmax (1.0 / 64.0, s.rateBeats);
        const float hold = juce::jlimit (0.02f, 1.0f, s.hold);
        const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
        // grains cohérents (texture 0) : somme de Hann x4 = 2 ; décorrélés : ~1.22
        const float norm = 0.5f * (1.0f + 0.63f * tex);
        const float wetStep = (float) (1.0 / xfLen);

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            buf.push (inL, inR);

            const double beat = beatPos + (double) i / spb;
            const double stepPos = beat / rate;
            const auto step = (int64_t) std::floor (stepPos);
            const double phase = stepPos - (double) step;

            if (step != lastStep)
            {
                // nouveau pas : l'ancien gel s'éteint, le son direct repasse et la nouvelle frame s'enregistre
                lastStep = step;
                for (auto& g : grains)
                    if (g.active) g.fadeDir = -1;
                cloud = false;
                tickTime = t;
                const double stepLen = rate * spb;
                captureEnd = t + (int64_t) std::llround (juce::jmin (G, stepLen * 0.5));
            }

            if (! cloud && t >= captureEnd)
            {
                // frame enregistrée : la fenêtre figée est [fin - G, fin], le nuage démarre déjà en régime
                cloud = true;
                anchor = t;
                cloudStart = t;
                for (int k = 3; k >= 0; --k)
                    spawn (G, tex, 0.0f, hop * k);
                spawnAcc = hop * (1.0 + 0.45 * tex * rng.bi());
            }

            const bool wantFrozen = cloud && phase < (double) hold;
            wet = juce::jlimit (0.0f, 1.0f, wet + (wantFrozen ? wetStep : -wetStep));

            if (cloud)
            {
                spawnAcc -= 1.0;
                if (spawnAcc <= 0.0)
                {
                    spawn (G, tex, speed, 0.0);
                    spawnAcc += hop * (1.0 + 0.45 * tex * rng.bi());
                }
            }

            float l = 0.0f, r = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const double ph = g.age / g.len;
                const float w = 0.5f - 0.5f * std::cos (dsp::twoPi * (float) ph);
                // fondu de bascule (puissance constante)
                if (g.fadeDir != 0)
                {
                    g.fade -= wetStep;
                    if (g.fade <= 0.0f) { g.active = false; continue; }
                }
                const float fg = g.fade >= 1.0f ? 1.0f : std::sin (0.5f * dsp::pi * g.fade);
                const double pos = g.start + g.age;
                l += buf.read (0, pos) * w * fg;
                r += buf.read (1, pos) * w * fg;
                g.age += 1.0;
                if (g.age >= g.len) g.active = false;
            }

            const float ew = std::sin (0.5f * dsp::pi * wet) * mix;
            const float ed = std::cos (0.5f * dsp::pi * wet) + std::sin (0.5f * dsp::pi * wet) * (1.0f - mix);
            L[i] = safety (inL * ed + l * norm * ew);
            R[i] = safety (inR * ed + r * norm * ew);
            ++t;
        }
    }

    /** Pas courant (pour l'affichage). */
    int64_t currentStep() const { return lastStep; }

private:
    struct Grain
    {
        bool active = false;
        double start = 0, age = 0, len = 1;
        float fade = 1.0f;
        int fadeDir = 0;
    };

    static float safety (float x)
    {
        if (! std::isfinite (x)) return 0.0f;
        const float a = std::abs (x);
        if (a <= 1.0f) return x;
        return std::copysign (1.0f + dsp::fastTanh (a - 1.0f) * 0.5f, x); // plafond doux à ~1.5
    }

    void spawn (double G, float tex, float speed, double preAge)
    {
        for (auto& g : grains)
        {
            if (g.active) continue;
            const double elapsed = (double) (t - cloudStart);
            // fenêtre figée : les G derniers échantillons avant le gel, un peu plus loin si texture
            const double back = G + (double) rng.uni() * tex * 1.5 * G;
            g.active = true;
            g.len = G;
            g.age = preAge;
            // les grains pré-vieillis lisent la même fenêtre, simplement déjà entamée
            g.start = (double) anchor - back + (double) speed * elapsed;
            // garde-fou : la lecture (Hermite, +2) ne doit jamais dépasser la tête d'écriture
            g.start = juce::jmin (g.start, (double) t - preAge - 3.0);
            g.fade = 1.0f;
            g.fadeDir = 0;
            return;
        }
    }

    dsp::RingBuffer buf;
    std::array<Grain, numGrains> grains;
    dsp::Rng rng;
    double sr = 44100.0, xfLen = 132.0;
    int64_t t = 0, anchor = 0, tickTime = 0, lastStep = 0, captureEnd = 0, cloudStart = 0;
    double spawnAcc = 0.0;
    float wet = 0.0f;
    bool cloud = false;
};
} // namespace idg
