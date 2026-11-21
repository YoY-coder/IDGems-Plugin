#pragma once
#include "../Core/Dsp.h"

namespace idg
{
/** Delay « glitch » (module 01 de la série un-effet-à-la-fois).
    - temps courts : jamais plus d'une noire (1/4) en sync, 0,3 à 500 ms en free ;
    - sous-divisions droites / pointées / ternaires ;
    - modes Stéréo, Ping-pong, Mono ; décalage stéréo en ms ;
    - coupe-bas + coupe-haut dans la boucle (chaque répétition est filtrée à nouveau) ;
    - feedback jusqu'à 110 % : au-delà de 100 % la boucle s'emballe et vient buter
      sur une saturation douce (auto-oscillation contrôlée) ;
    - changement de temps par fondu entre deux têtes de lecture (pas de glissement de pitch, pas de clic).
    Aucune allocation dans process(). */
class GlitchDelay
{
public:
    enum Mode { Stereo, PingPong, Mono };
    enum Feel { Straight, Dotted, Triplet };

    static constexpr int numNotes = 6;
    static const char* noteName (int i)
    {
        static const char* n[] { "1/128", "1/64", "1/32", "1/16", "1/8", "1/4" };
        return n[juce::jlimit (0, numNotes - 1, i)];
    }
    /** Durée en noires (1/4 = 1). Jamais plus d'une noire : 1/4 pointée retombe sur 1/4. */
    static double noteBeats (int note, int feel)
    {
        double b = std::ldexp (1.0, juce::jlimit (0, numNotes - 1, note) - (numNotes - 1)); // 1/128 = 1/32 noire
        if (feel == Dotted) b *= 1.5;
        else if (feel == Triplet) b *= 2.0 / 3.0;
        return juce::jmin (1.0, b);
    }
    static bool dottedAvailable (int note) { return note < numNotes - 1; }
    static juce::String divisionText (int note, int feel)
    {
        juce::String s (noteName (note));
        if (feel == Dotted && dottedAvailable (note)) s << "D";
        else if (feel == Triplet) s << "T";
        return s;
    }

    struct Settings
    {
        bool sync = true;
        int note = 3, feel = Straight;     // 1/16 droit
        float timeMs = 60.0f;              // mode free
        float feedback = 0.45f;            // 0..1.1
        int mode = Stereo;
        float offsetMs = 0.0f;             // > 0 : droite en retard, < 0 : gauche en retard
        float lowCut = 20.0f, highCut = 20000.0f;
        float mix = 0.5f;
        double bpm = 120.0;
    };

    static constexpr float minTimeMs = 0.3f, maxTimeMs = 500.0f, maxOffsetMs = 20.0f;
    static constexpr double minBpm = 20.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // une noire à 20 BPM (3 s) + décalage max + marge
        buf.allocate ((int) (sr * (60.0 / minBpm + maxOffsetMs * 0.001)) + 64);
        fadeLen = juce::jmax (16, (int) (0.012 * sr));
        reset();
    }

    void reset()
    {
        buf.clear();
        lp.reset(); hp.reset();
        for (auto& t : taps) { t.cur = t.next = -1.0; t.fade = 0; }
        fbS = mixS = -1.0f;
        lcS = hcS = -1.0f;
        xf = 1.0f;
        activeMode = prevMode = -1;
        wetPeak = 0.0f;
    }

    /** Retard (samples) visé pour chaque canal. */
    void targetDelays (const Settings& s, double out[2]) const
    {
        double base = s.sync ? noteBeats (s.note, s.feel) * 60.0 / juce::jmax (minBpm, s.bpm) * sr
                             : juce::jlimit (minTimeMs, maxTimeMs, s.timeMs) * 0.001 * sr;
        const double off = juce::jlimit (-maxOffsetMs, maxOffsetMs, s.offsetMs) * 0.001 * sr;
        out[0] = base + juce::jmax (0.0, -off);
        out[1] = base + juce::jmax (0.0, off);
        if (s.mode == Mono) out[1] = out[0] = base;
        for (int k = 0; k < 2; ++k) out[k] = juce::jlimit (2.0, (double) buf.size - 8.0, out[k]);
    }

    /** Traite en place. */
    void process (float* L, float* R, int n, const Settings& s)
    {
        double target[2];
        targetDelays (s, target);
        const int mode = juce::jlimit (0, 2, s.mode);
        if (activeMode < 0) activeMode = prevMode = mode;

        // lissage par bloc (cibles atteintes en ~30 ms)
        const float a = (float) std::exp (-(double) n / (0.03 * sr));
        auto smooth = [a] (float& v, float t) { v = v < -0.5f ? t : t + (v - t) * a; };
        smooth (fbS, juce::jlimit (0.0f, 1.1f, s.feedback));
        smooth (mixS, juce::jlimit (0.0f, 1.0f, s.mix));
        // fréquences lissées en log (évite les sauts de filtre)
        auto smoothHz = [a] (float& v, float t) { t = std::log (t); v = v < -0.5f ? t : t + (v - t) * a; };
        smoothHz (lcS, juce::jlimit (20.0f, 20000.0f, s.lowCut));
        smoothHz (hcS, juce::jlimit (20.0f, 20000.0f, s.highCut));
        hp.set (sr, std::exp (lcS), 0.7071f);
        lp.set (sr, juce::jmin ((float) (sr * 0.45), std::exp (hcS)), 0.7071f);

        for (int k = 0; k < 2; ++k)
        {
            auto& t = taps[k];
            if (t.cur < 0.0) t.cur = target[k];
            // nouveau temps : on prépare un fondu vers la nouvelle tête (si aucun n'est en cours)
            if (t.fade == 0 && std::abs (target[k] - t.cur) > 0.5) { t.next = target[k]; t.fade = 1; }
        }

        const float fb = fbS;
        const float mix = mixS;
        const float xfStep = 1.0f / (0.01f * (float) sr);
        float pk = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // changement de mode : fondu de 10 ms entre l'ancien et le nouveau routage
            // (sortie ET réinjection, sinon le saut resterait gravé dans la boucle)
            if (xf >= 1.0f && mode != activeMode) { prevMode = activeMode; activeMode = mode; xf = 0.0f; }
            if (xf < 1.0f) xf = juce::jmin (1.0f, xf + xfStep);

            float d[2];
            for (int k = 0; k < 2; ++k)
            {
                auto& t = taps[k];
                float v = buf.delay (k, t.cur - 1.0);
                if (t.fade > 0)
                {
                    const float x = (float) t.fade / (float) fadeLen;
                    const float w = 0.5f - 0.5f * std::cos (dsp::pi * x);
                    v += (buf.delay (k, t.next - 1.0) - v) * w;
                    if (++t.fade > fadeLen) { t.cur = t.next; t.fade = 0; }
                }
                d[k] = v;
            }

            // filtres dans la boucle : ils colorent la 1re répétition et chaque suivante
            float f[2];
            for (int k = 0; k < 2; ++k) f[k] = lp.lowpass (k, hp.highpass (k, d[k]));

            const float inL = L[i], inR = R[i];
            float wl, wr, oL, oR;
            route (activeMode, inL, inR, f, fb, wl, wr, oL, oR);
            if (xf < 1.0f)
            {
                float pwl, pwr, poL, poR;
                route (prevMode, inL, inR, f, fb, pwl, pwr, poL, poR);
                const float g = 0.5f - 0.5f * std::cos (dsp::pi * xf);
                wl = pwl + (wl - pwl) * g; wr = pwr + (wr - pwr) * g;
                oL = poL + (oL - poL) * g; oR = poR + (oR - poR) * g;
            }
            buf.push (loopSat (wl), loopSat (wr));

            pk = juce::jmax (pk, std::abs (oL), std::abs (oR));
            L[i] = inL + (oL - inL) * mix;
            R[i] = inR + (oR - inR) * mix;
        }
        lp.sanitize(); hp.sanitize();
        if (pk > wetPeak.load (std::memory_order_relaxed)) wetPeak.store (pk, std::memory_order_relaxed);
    }

    /** Routage d'un mode : ce qu'on écrit dans la boucle (w) et ce qu'on entend (o). */
    static inline void route (int m, float inL, float inR, const float* f, float fb, float& wl, float& wr, float& oL, float& oR)
    {
        switch (m)
        {
            case PingPong: wl = 0.5f * (inL + inR) + fb * f[1]; wr = fb * f[0]; oL = f[0]; oR = f[1]; break;
            case Mono:     wl = wr = 0.5f * (inL + inR) + fb * f[0]; oL = oR = f[0]; break;
            default:       wl = inL + fb * f[0]; wr = inR + fb * f[1]; oL = f[0]; oR = f[1]; break;
        }
    }

    /** Saturation de boucle : transparente sous 0 dBFS, genou doux au-dessus, plafond ~ +3,5 dB.
        C'est elle qui tient l'auto-oscillation quand le feedback dépasse 100 %. */
    static inline float loopSat (float x)
    {
        if (! dsp::finite (x)) return 0.0f;
        const float ax = std::abs (x);
        if (ax <= 1.0f) return x;
        return std::copysign (1.0f + 0.5f * dsp::fastTanh ((ax - 1.0f) * 2.0f), x);
    }

    /** Crête du signal traité depuis la dernière lecture (pour l'interface). */
    std::atomic<float> wetPeak { 0.0f };

private:
    struct Tap { double cur = -1.0, next = -1.0; int fade = 0; };
    double sr = 44100.0;
    dsp::RingBuffer buf;
    dsp::SVF lp, hp;
    Tap taps[2];
    int fadeLen = 512;
    float fbS = -1.0f, mixS = -1.0f, lcS = -1.0f, hcS = -1.0f;
    float xf = 1.0f;
    int activeMode = -1, prevMode = -1;
};
} // namespace idg
