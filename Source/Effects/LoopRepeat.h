#pragma once
#include "../Core/Dsp.h"

namespace idg
{
/** Beat Repeat (moteur inspiré du « Loop » d'Infiltrator).

    - À chaque pas de TEMPO, l'effet ré-attrape le son : la première passe joue le direct,
      puis la tranche de longueur L se répète en boucle.
    - SPEED règle la longueur de la boucle (de 1 mesure à ~1 ms). Sous ~30 ms la répétition
      devient une note (buzz granulaire) : plus c'est rapide, plus c'est aigu.
    - SMOOTH : fondu à chaque tour de boucle (0 = sec et brillant, 1 = doux).
    - GLIDE : glissement de la longueur quand SPEED bouge.
    - RAMPE : la boucle accélère (+) ou ralentit (-) au fil de chaque pas de tempo.
    - SENS : avant, aller-retour, arrière.

    Moteur autonome (aucune dépendance aux paramètres du rack) : il sera branché dans le rack
    IDGems une fois validé à l'oreille. Aucune allocation dans process(). */
class LoopRepeat
{
public:
    enum Dir { Forward = 0, PingPong, Reverse };

    struct Settings
    {
        bool active = true;
        bool sync = true;
        int tempoIndex = 8;      // voir tempoName()
        float tempoMs = 1000.0f; // mode libre
        float speed = 0.3636f;   // 0..1 (0 = 1 mesure, 1 = ~1 ms)
        bool snap = false;       // sync : aimante la longueur sur la grille (droites, triolets, pointées)
        float smooth = 0.25f;    // 0..1
        float glideMs = 30.0f;
        float ramp = 0.0f;       // -1..1 (= ±3 octaves de vitesse sur un pas de tempo)
        int dir = Forward;
        float mix = 1.0f;
        float gainDb = 0.0f;
    };

    struct Transport
    {
        double bpm = 120.0;
        double ppq = 0.0;            // position au début du bloc (noires)
        bool playing = false;
        double quartersPerBar = 4.0;
    };

    // ------------------------------------------------------------------ tables
    static constexpr int numTempo = 13;
    static juce::StringArray tempoNames()
    {
        return { "Hold", "1/32", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/4.", "1/2", "1/2.", "1 mes.", "2 mes.", "4 mes." };
    }
    /** Durée d'un pas de tempo, en noires (0 = Hold : n'attrape qu'à l'activation). */
    static double tempoBeats (int i, double qpb)
    {
        static const double t[] { 0.0, 0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, -1.0, -2.0, -4.0 };
        const double v = t[juce::jlimit (0, numTempo - 1, i)];
        return v < 0.0 ? -v * qpb : v;
    }

    static constexpr double speedOctaves = 11.0;
    /** Longueur de boucle en noires (sync) : 4 noires à speed 0, 4/2048 à speed 1. */
    static double speedToBeats (float speed) { return 4.0 * std::exp2 (-(double) juce::jlimit (0.0f, 1.0f, speed) * speedOctaves); }
    /** Longueur en ms (libre) : identique au mode sync à 120 BPM. */
    static double speedToMs (float speed) { return 2000.0 * std::exp2 (-(double) juce::jlimit (0.0f, 1.0f, speed) * speedOctaves); }

    /** Valeur de grille la plus proche (droite, triolet, pointée). */
    static double snapBeats (double beats, juce::String* name = nullptr)
    {
        static const double fac[] { 1.0, 2.0 / 3.0, 1.5 };
        static const char* suf[] { "", "T", "." };
        double best = beats, bestErr = 1.0e9;
        int bk = 0, bf = 0;
        for (int k = 0; k <= 12; ++k)
            for (int f = 0; f < 3; ++f)
            {
                const double c = 4.0 * std::exp2 (-(double) k) * fac[f];
                const double e = std::abs (std::log2 (beats / c));
                if (e < bestErr) { bestErr = e; best = c; bk = k; bf = f; }
            }
        if (name != nullptr) *name = "1/" + juce::String (1 << bk) + suf[bf];
        return best;
    }

    // ------------------------------------------------------------------ cycle de vie
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        maxLen = (int) std::ceil (maxLoopSeconds * sr);
        const int capSize = maxLen + maxLen / 2 + 8; // boucle + fondu (≤ L/2)
        for (auto& v : voices)
            for (auto& c : v.cap) c.assign ((size_t) capSize, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) { v.alive = false; v.recorded = 0; v.gain = 0.0f; v.step = 0.0f; }
        cur = 0;
        lastStep = std::numeric_limits<int64_t>::min();
        freeBeat = 0.0; freeCounter = 0.0;
        wasActive = false;
        wetAmt = 0.0f;
        logLen = -1.0e9;
        sinceCapture = 0.0;
    }

    // ------------------------------------------------------------------ télémétrie (UI)
    std::atomic<float> uiLoopMs { 0.0f }, uiPhase { 0.0f }, uiStepProgress { 0.0f };
    std::atomic<int> uiCycle { 0 };

    // ------------------------------------------------------------------ traitement
    void process (float* L, float* R, int n, const Settings& s, const Transport& t)
    {
        const double bpm = juce::jlimit (20.0, 999.0, t.bpm);
        const double spb = sr * 60.0 / bpm;               // samples par noire
        const double qpb = juce::jmax (0.25, t.quartersPerBar);
        const double glideA = s.glideMs <= 0.01f ? 0.0 : std::exp (-1.0 / (s.glideMs * 0.001 * sr));
        const float wetA = (float) std::exp (-1.0 / (0.006 * sr));
        const float gain = dsp::dbToGain (s.gainDb);

        // longueur visée (avant rampe)
        double baseLen;
        if (s.sync)
        {
            double b = speedToBeats (s.speed);
            if (s.snap) b = snapBeats (b);
            baseLen = b * spb;
        }
        else baseLen = speedToMs (s.speed) * 0.001 * sr;

        // pas de tempo (en noires si sync, en samples si libre)
        const double stepBeats = s.sync ? tempoBeats (s.tempoIndex, qpb) : 0.0;
        const double stepSamples = s.sync ? (stepBeats > 0.0 ? stepBeats * spb : qpb * spb)
                                          : juce::jmax (1.0, (double) s.tempoMs * 0.001 * sr);
        const bool hold = s.sync && stepBeats <= 0.0;

        const double hostBeat0 = t.playing ? t.ppq : freeBeat;
        const float minXf = (float) juce::jmax (8.0, 0.0007 * sr);
        const float recFade = (float) juce::jlimit (32.0, 0.02 * sr, 0.004 * sr);

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];

            // --- activation / désactivation
            if (s.active && ! wasActive) startCapture (recFade);
            wasActive = s.active;

            // --- horloge : nouvelle capture à chaque pas de tempo
            if (s.active && ! hold)
            {
                int64_t stepIdx;
                if (s.sync) stepIdx = (int64_t) std::floor ((hostBeat0 + (double) i / spb) / stepBeats + 1.0e-9);
                else
                {
                    freeCounter += 1.0;
                    if (freeCounter >= stepSamples) freeCounter -= stepSamples * std::floor (freeCounter / stepSamples);
                    stepIdx = freeCounter < 1.0 ? lastStep + 1 : lastStep;
                }
                if (stepIdx != lastStep)
                {
                    const bool first = lastStep == std::numeric_limits<int64_t>::min();
                    lastStep = stepIdx;
                    if (! first || ! voices[cur].alive) startCapture (recFade);
                }
            }

            // --- longueur de boucle (rampe + glide dans le domaine log)
            const double progress = juce::jlimit (0.0, 1.0, sinceCapture / stepSamples);
            const double target = std::log2 (juce::jlimit (16.0, (double) maxLen, baseLen * std::exp2 (-3.0 * s.ramp * progress)));
            if (logLen < -1.0e8) logLen = target;
            logLen = target + (logLen - target) * glideA;
            const double len = juce::jlimit (16.0, (double) maxLen, std::exp2 (logLen));
            const double xf = juce::jlimit ((double) juce::jmin (minXf, (float) (len * 0.5)), len * 0.5, (double) s.smooth * 0.5 * len);

            // --- enregistrement + lecture
            float wl = 0.0f, wr = 0.0f;
            for (auto& v : voices)
            {
                if (! v.alive) continue;
                v.record (inL, inR);
                float a, b;
                v.render (len, xf, s.dir, a, b);
                v.gain += v.step;
                if (v.gain >= 1.0f) { v.gain = 1.0f; v.step = 0.0f; }
                if (v.gain <= 0.0f && v.step < 0.0f) { v.alive = false; continue; }
                wl += a * v.gain; wr += b * v.gain;
            }
            sinceCapture += 1.0;

            // --- dosage sec / traité (fondu court à l'activation)
            const float wetTarget = s.active ? juce::jlimit (0.0f, 1.0f, s.mix) : 0.0f;
            wetAmt = wetTarget + (wetAmt - wetTarget) * wetA;
            if (! s.active && wetAmt < 1.0e-4f) for (auto& v : voices) v.alive = false;

            float ol = (inL + (wl - inL) * wetAmt) * gain;
            float orr = (inR + (wr - inR) * wetAmt) * gain;
            // sécurité : anti-NaN + plafond dur (+6 dBFS)
            if (! dsp::finite (ol)) ol = 0.0f;
            if (! dsp::finite (orr)) orr = 0.0f;
            L[i] = juce::jlimit (-2.0f, 2.0f, ol);
            R[i] = juce::jlimit (-2.0f, 2.0f, orr);

            if (i == n - 1)
            {
                const auto& v = voices[cur];
                uiLoopMs.store ((float) (len * 1000.0 / sr), std::memory_order_relaxed);
                uiPhase.store ((float) juce::jlimit (0.0, 1.0, v.p / len), std::memory_order_relaxed);
                uiCycle.store (v.cycle, std::memory_order_relaxed);
                uiStepProgress.store ((float) progress, std::memory_order_relaxed);
            }
        }
        freeBeat += (double) n / spb;
    }

private:
    static constexpr double maxLoopSeconds = 4.0;

    struct Voice
    {
        std::vector<float> cap[2];
        int recorded = 0;
        double p = 0.0;   // position dans le cycle (samples)
        int cycle = 0;    // 0 = première passe (direct)
        bool alive = false;
        float gain = 0.0f, step = 0.0f;

        void start (float fadeIn)
        {
            recorded = 0; p = 0.0; cycle = 0; alive = true;
            gain = fadeIn > 0.0f ? 0.0f : 1.0f;
            step = fadeIn > 0.0f ? 1.0f / fadeIn : 0.0f;
        }

        inline void record (float l, float r)
        {
            if (recorded < (int) cap[0].size())
            {
                cap[0][(size_t) recorded] = l;
                cap[1][(size_t) recorded] = r;
                ++recorded;
            }
        }

        /** Lecture Hermite bornée aux échantillons déjà enregistrés. */
        inline float rd (int ch, double x) const
        {
            const int last = recorded - 1;
            if (last <= 0) return last == 0 ? cap[ch][0] : 0.0f;
            x = juce::jlimit (0.0, (double) last, x);
            const int i = (int) x;
            const float f = (float) (x - (double) i);
            const auto& c = cap[ch];
            auto at = [&] (int k) { return c[(size_t) juce::jlimit (0, last, k)]; };
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        static inline float fadeIn (double w) // 0..1 -> 0..1, sin² (somme des deux fondus = 1)
        {
            const float s = (float) std::sin (juce::jlimit (0.0, 1.0, w) * juce::MathConstants<double>::halfPi);
            return s * s;
        }

        inline void render (double len, double xf, int dir, float& outL, float& outR)
        {
            if (cycle == 0)
            {
                // première passe : le direct (ce qui vient d'être enregistré)
                outL = rd (0, p); outR = rd (1, p);
            }
            else
            {
                const bool backwards = dir == Reverse || (dir == PingPong && (cycle & 1) == 1);
                if (! backwards)
                {
                    outL = rd (0, p); outR = rd (1, p);
                    // boucle avant : la fin du cycle précédent continue en fondu sortant
                    if (dir == Forward && p < xf)
                    {
                        const float fi = fadeIn (p / xf);
                        outL = outL * fi + rd (0, p + len) * (1.0f - fi);
                        outR = outR * fi + rd (1, p + len) * (1.0f - fi);
                    }
                }
                else
                {
                    const double q = len - p;
                    outL = rd (0, q); outR = rd (1, q);
                    // boucle arrière : le cycle suivant entre en fondu avant la fin
                    if (dir == Reverse && p > len - xf)
                    {
                        const float fi = fadeIn ((p - (len - xf)) / xf);
                        const double nq = 2.0 * len - p;
                        outL = outL * (1.0f - fi) + rd (0, nq) * fi;
                        outR = outR * (1.0f - fi) + rd (1, nq) * fi;
                    }
                }
            }
            p += 1.0;
            if (p >= len)
            {
                p -= len * std::floor (p / len);
                cycle = cycle >= (1 << 29) ? 2 : cycle + 1;
            }
        }
    };

    void startCapture (float fade)
    {
        // l'ancienne boucle s'éteint pendant que la nouvelle (le direct) monte
        auto& old = voices[cur];
        if (old.alive) old.step = -1.0f / fade;
        cur = 1 - cur;
        voices[cur].start (old.alive ? fade : 0.0f);
        sinceCapture = 0.0;
    }

    double sr = 44100.0;
    int maxLen = 1;
    Voice voices[2];
    int cur = 0;
    int64_t lastStep = std::numeric_limits<int64_t>::min();
    double freeBeat = 0.0, freeCounter = 0.0, sinceCapture = 0.0;
    bool wasActive = false;
    float wetAmt = 0.0f;
    double logLen = -1.0e9;
};
} // namespace idg
