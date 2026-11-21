#pragma once
#include "../Core/Dsp.h"

namespace idg
{
/** Stretch (v3, calqué sur l'analyse d'un rendu Infiltrator « speed 0, grain 40 Hz, 1/16 ») :
    à chaque pas rythmique (Rate), le son direct passe le temps d'un grain (1/f), puis ce grain
    est rejoué en boucle exacte jusqu'au pas suivant. La boucle a une période de 1/f : à 40 Hz,
    25 ms rejouées 40 fois par seconde (forme d'onde intacte, pas de nuage de grains superposés).
    - Raccord de boucle sans clic : les X premiers échantillons de la boucle sont fondus avec les
      X échantillons qui suivent la fenêtre, donc la fin de la boucle enchaîne sur son début comme
      dans le son d'origine, et le premier passage prolonge le son direct sans rupture.
    - Bascule boucle précédente -> son direct : fondu à puissance constante de quelques ms.
    - Sortie : coupe-bas et coupe-haut 24 dB/oct (deux SVF en cascade chacun), résonance commune.
    Pur DSP, sans allocation dans process(). */
class StretchEngine
{
public:
    struct Settings
    {
        double rateBeats = 0.25; // durée d'un pas en noires (0.25 = 1/16)
        float grainHz = 40.0f;   // fréquence de répétition du grain
        float hpHz = 20.0f;      // coupe-bas
        float lpHz = 20000.0f;   // coupe-haut
        float reso = 0.0f;       // résonance des deux filtres (0..1)
    };

    static constexpr float minGrainHz = 1.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // un grain de 1 s + marge (fenêtre + fondu)
        buf.allocate ((int) (sr * 2.5) + 1024);
        maxXf = juce::jmax (8.0, sr * 0.003);
        const auto cap = (size_t) (sr / minGrainHz + maxXf + 64);
        for (auto* b : { &cur, &old }) { b->d[0].assign (cap, 0.0f); b->d[1].assign (cap, 0.0f); }
        reset();
    }

    void reset()
    {
        buf.clear();
        t = 0;
        lastStep = std::numeric_limits<int64_t>::min();
        looping = false;
        oldActive = false;
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        hpSm = 20.0f; lpSm = 20000.0f; resoSm = 0.0f;
        filtCount = 0;
    }

    /** beatPos : position (en noires) au début du bloc ; spb : échantillons par noire. */
    void process (float* L, float* R, int n, double beatPos, double spb, const Settings& s)
    {
        const double rate = juce::jmax (1.0 / 64.0, s.rateBeats);
        const double G = juce::jlimit (16.0, sr / (double) minGrainHz, sr / (double) juce::jmax (minGrainHz, s.grainHz));

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            buf.push (inL, inR);

            const double beat = beatPos + (double) i / spb;
            const auto step = (int64_t) std::floor (beat / rate);

            if (step != lastStep)
            {
                // nouveau pas : la boucle en cours s'efface vers le son direct, la nouvelle frame s'enregistre
                lastStep = step;
                const double stepLen = rate * spb;
                const double capture = juce::jmin (G, stepLen * 0.5);
                xf = juce::jmax (1.0, juce::jmin (maxXf, capture * 0.5, G * 0.25));
                if (looping)
                {
                    std::swap (cur, old); // l'ancienne boucle garde son tampon pendant le fondu
                    oldActive = true;
                }
                looping = false;
                fadePos = 0.0;
                loopStart = t + (int64_t) std::llround (capture);
                pendingG = G;
            }

            if (! looping && t >= loopStart && lastStep != std::numeric_limits<int64_t>::min())
            {
                // la fenêtre figée finit ici : [t - G, t). La boucle démarre en phase 0, dont le
                // raccord lit l'échantillon courant (= son direct) : aucune rupture.
                looping = true;
                oldActive = false;
                startLoop (cur, (double) t - pendingG, pendingG, xf);
            }
            feed (cur);

            float l, r;
            if (looping)
            {
                loopSample (cur, l, r);
                advance (cur);
            }
            else
            {
                // son direct (enregistrement de la frame), avec fondu depuis l'ancienne boucle
                l = inL; r = inR;
                if (oldActive)
                {
                    fadePos += 1.0;
                    const float a = (float) juce::jmin (1.0, fadePos / xf);
                    const float gIn = std::sin (0.5f * dsp::pi * a), gOld = std::cos (0.5f * dsp::pi * a);
                    float ol, orr;
                    loopSample (old, ol, orr);
                    advance (old);
                    l = inL * gIn + ol * gOld;
                    r = inR * gIn + orr * gOld;
                    if (a >= 1.0f) oldActive = false;
                }
            }

            // filtres (coefficients lissés, recalculés tous les 16 échantillons)
            if (filtCount-- <= 0)
            {
                filtCount = 15;
                updateFilters (s);
            }
            for (int c = 0; c < 2; ++c)
            {
                float& v = c == 0 ? l : r;
                if (hpOn) v = hp[1].highpass (c, hp[0].highpass (c, v));
                if (lpOn) v = lp[1].lowpass (c, lp[0].lowpass (c, v));
            }

            L[i] = safety (l);
            R[i] = safety (r);
            ++t;
        }
        for (auto& f : hp) f.sanitize();
        for (auto& f : lp) f.sanitize();
    }

    /** Réponse du filtre (dB) à une fréquence, pour l'affichage. */
    static float responseDb (float f, float hpHz, float lpHz, float reso, double sampleRate = 48000.0)
    {
        const double q1 = 0.5412, q2 = qFor (reso);
        auto mag2 = [&] (double fc, double q, bool high)
        {
            // SVF analogique 2e ordre : |H|² pour s = j·f/fc
            const double w = f / fc, w2 = w * w;
            const double num = high ? w2 * w2 : 1.0;
            const double den = (1.0 - w2) * (1.0 - w2) + w2 / (q * q);
            return num / juce::jmax (1.0e-12, den);
        };
        double h = 1.0;
        if (hpHz > 20.5f) h *= mag2 (hpHz, q1, true) * mag2 (hpHz, q2, true);
        if (lpHz < 19500.0f && lpHz < sampleRate * 0.49) h *= mag2 (lpHz, q1, false) * mag2 (lpHz, q2, false);
        return (float) (10.0 * std::log10 (juce::jmax (1.0e-12, h)));
    }

private:
    /** Boucle figée, copiée hors du tampon circulaire (elle peut tenir aussi longtemps que le pas). */
    struct Loop
    {
        std::vector<float> d[2];
        int64_t base = 0;     // index absolu de d[.][0]
        int64_t filled = 0;   // nombre d'échantillons copiés
        int64_t needed = 0;
        double start = 0, len = 1, phase = 0, xf = 1;

        float at (int c, int64_t abs) const
        {
            const auto i = juce::jlimit ((int64_t) 0, juce::jmax ((int64_t) 0, filled - 1), abs - base);
            return d[c][(size_t) i];
        }
        float read (int c, double pos) const
        {
            const auto i = (int64_t) std::floor (pos);
            const float f = (float) (pos - (double) i);
            const float xm1 = at (c, i - 1), x0 = at (c, i), x1 = at (c, i + 1), x2 = at (c, i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
    };

    /** Copie la fenêtre [start, start + len + xf] : la partie déjà enregistrée tout de suite,
        la suite (le raccord) au fil des échantillons via feed(). */
    void startLoop (Loop& lp_, double start, double len, double fade)
    {
        lp_.start = start; lp_.len = len; lp_.phase = 0.0; lp_.xf = fade;
        lp_.base = (int64_t) std::floor (start) - 2;
        lp_.needed = juce::jmin ((int64_t) lp_.d[0].size(), (int64_t) std::ceil (len + fade) + 6);
        lp_.filled = 0;
        while (lp_.filled < lp_.needed && lp_.base + lp_.filled <= t)
        {
            const auto abs = lp_.base + lp_.filled;
            lp_.d[0][(size_t) lp_.filled] = buf.at (0, (int) abs);
            lp_.d[1][(size_t) lp_.filled] = buf.at (1, (int) abs);
            ++lp_.filled;
        }
    }

    void feed (Loop& lp_)
    {
        if (! looping || lp_.filled >= lp_.needed || lp_.base + lp_.filled != t) return;
        lp_.d[0][(size_t) lp_.filled] = buf.at (0, (int) t);
        lp_.d[1][(size_t) lp_.filled] = buf.at (1, (int) t);
        ++lp_.filled;
    }

    static double qFor (float reso) { return 1.3065 + (double) juce::jlimit (0.0f, 1.0f, reso) * 4.0; }

    /** Échantillon de la boucle à sa phase courante (raccord fondu sur les xf premiers échantillons). */
    void loopSample (const Loop& lp_, float& l, float& r) const
    {
        const double pos = lp_.start + lp_.phase;
        l = lp_.read (0, pos);
        r = lp_.read (1, pos);
        if (lp_.phase < lp_.xf)
        {
            // au début de la boucle : on part de ce qui suivait la fenêtre (continuité avec la fin)
            const float a = (float) (lp_.phase / lp_.xf);
            const float w = 0.5f - 0.5f * std::cos (dsp::pi * a); // 0 -> 1
            const float al = lp_.read (0, pos + lp_.len), ar = lp_.read (1, pos + lp_.len);
            l = al + (l - al) * w;
            r = ar + (r - ar) * w;
        }
    }

    static void advance (Loop& lp_)
    {
        lp_.phase += 1.0;
        if (lp_.phase >= lp_.len) lp_.phase -= lp_.len;
    }

    void updateFilters (const Settings& s)
    {
        const float hpT = juce::jlimit (20.0f, 20000.0f, s.hpHz);
        const float lpT = juce::jlimit (20.0f, 20000.0f, s.lpHz);
        // lissage exponentiel en log-fréquence (pas de zipper)
        hpSm = std::exp (std::log (hpSm) + 0.15f * (std::log (hpT) - std::log (hpSm)));
        lpSm = std::exp (std::log (lpSm) + 0.15f * (std::log (lpT) - std::log (lpSm)));
        resoSm += 0.15f * (juce::jlimit (0.0f, 1.0f, s.reso) - resoSm);
        hpOn = hpSm > 20.5f;
        lpOn = lpSm < 19500.0f && lpSm < sr * 0.45;
        const float q2 = (float) qFor (resoSm);
        if (hpOn) { hp[0].set (sr, hpSm, 0.5412f); hp[1].set (sr, hpSm, q2); }
        if (lpOn) { lp[0].set (sr, lpSm, 0.5412f); lp[1].set (sr, lpSm, q2); }
    }

    static float safety (float x)
    {
        if (! std::isfinite (x)) return 0.0f;
        const float a = std::abs (x);
        if (a <= 1.0f) return x;
        return std::copysign (1.0f + dsp::fastTanh (a - 1.0f) * 0.5f, x); // plafond doux à ~1.5
    }

    dsp::RingBuffer buf;
    double sr = 44100.0, maxXf = 132.0, xf = 64.0, fadePos = 0.0, pendingG = 1102.5;
    int64_t t = 0, lastStep = 0, loopStart = 0;
    Loop cur, old;
    bool looping = false, oldActive = false;

    dsp::SVF hp[2], lp[2];
    float hpSm = 20.0f, lpSm = 20000.0f, resoSm = 0.0f;
    bool hpOn = false, lpOn = false;
    int filtCount = 0;
};
} // namespace idg
