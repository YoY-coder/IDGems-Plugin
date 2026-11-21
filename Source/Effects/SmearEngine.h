#pragma once
#include "Core/JuceIncludes.h"

namespace idg
{
/** Smear : dilue le son dans le temps, sans aucune constante rythmique.

    Principe (vocodeur de phase, STFT 4096 pts à 44,1/48 kHz, recouvrement 8x) :
    - la puissance de chaque bande de fréquence est moyennée dans le temps (lissage exponentiel
      de constante SMEAR, montée réglable) : attaques, trous et rythme se fondent en une nappe ;
    - la fréquence instantanée de chaque bande est mesurée (sur le mid) puis moyennée, pondérée par
      l'énergie : les notes d'origine sont gardées, juste « coulées » ;
    - resynthèse à phase verrouillée par pic (pas de son « phasy ») : la phase de chaque pic avance
      à sa fréquence lissée, les bandes voisines suivent ;
    - FOCUS creuse ce qui n'est pas un pic (bruit, inharmonique) : lignes harmoniques pures ;
    - DRIFT ajoute un mouvement lent (désaccord, niveau, largeur stéréo au-dessus de 200 Hz) ;
    - phase commune aux deux canaux : image stable, compatible mono.
    Latence fixe = taille de la FFT. Aucune allocation dans process(). */
class SmearEngine
{
public:
    struct Settings
    {
        float smearSec = 2.0f;  // constante de temps de la traînée
        float rise = 1.0f;      // montée en part de SMEAR (1 = symétrique)
        float focus = 0.6f;     // 0..1 : pureté tonale
        float drift = 0.25f;    // 0..1 : mouvement
        float tone = 0.0f;      // -1..1 : inclinaison ±3 dB/octave autour de 1 kHz
        float mix = 1.0f;       // 0..1
        float outDb = 0.0f;
        bool freeze = false;
    };

    static constexpr int dispBins = 160; // spectre affiché (échelle log 20 Hz - 20 kHz)
    std::array<std::atomic<float>, dispBins> dispIn {}, dispOut {};

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = sr <= 50000.0 ? 12 : (sr <= 100000.0 ? 13 : 14);
        N = 1 << order;
        H = N / 8;
        K = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);

        win.assign ((size_t) N, 0.0f);
        double sumSq = 0.0;
        for (int i = 0; i < N; ++i)
        {
            win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N));
            sumSq += (double) win[(size_t) i] * win[(size_t) i];
        }
        olaNorm = (float) (1.0 / (sumSq / H));

        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) N, 0.0f);
            outBuf[c].assign ((size_t) (N + H), 0.0f);
            work[c].assign ((size_t) (2 * N), 0.0f);
            P[c].assign ((size_t) K, 0.0f);
            dryDelay[c].assign ((size_t) N, 0.0f);
        }
        W.assign ((size_t) K, 0.0f);
        F.assign ((size_t) K, 0.0f);
        prevPh.assign ((size_t) K, 0.0f);
        phOut.assign ((size_t) K, 0.0f);
        refP.assign ((size_t) K, 0.0f);
        gainK.assign ((size_t) K, 1.0f);
        tilt.assign ((size_t) K, 1.0f);
        peaks.assign ((size_t) K, 0);
        bandPos.assign ((size_t) K, 0.0f);
        binW.assign ((size_t) K, 0.0f);
        dispMap.assign ((size_t) K, -1);
        for (int k = 0; k < K; ++k)
        {
            binW[(size_t) k] = juce::MathConstants<float>::twoPi * (float) k / (float) N;
            const double f = juce::jmax (1.0, k * sr / N);
            // bandes du mouvement : 1/6 d'octave à partir de 20 Hz
            bandPos[(size_t) k] = (float) juce::jlimit (0.0, (double) (numBands - 1.001), std::log2 (f / 20.0) * 6.0);
            if (f >= 20.0 && f <= 20000.0)
                dispMap[(size_t) k] = juce::jlimit (0, dispBins - 1, (int) (std::log (f / 20.0) / std::log (1000.0) * dispBins));
        }
        for (int b = 0; b < dispBins; ++b) { dispIn[(size_t) b] = -120.0f; dispOut[(size_t) b] = -120.0f; }
        lastTone = 1000.0f;
        limRel = (float) (1.0 - std::exp (-1.0 / (0.15 * sr)));
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inBuf[c].begin(), inBuf[c].end(), 0.0f);
            std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f);
            std::fill (P[c].begin(), P[c].end(), 0.0f);
            std::fill (dryDelay[c].begin(), dryDelay[c].end(), 0.0f);
        }
        std::fill (W.begin(), W.end(), 0.0f);
        std::fill (F.begin(), F.end(), 0.0f);
        std::fill (prevPh.begin(), prevPh.end(), 0.0f);
        std::fill (phOut.begin(), phOut.end(), 0.0f);
        for (auto* a : { &rwDet, &rwAmp, &rwWid, &tgDet, &tgAmp, &tgWid }) a->fill (0.0f);
        cnt = 0;
        dPos = 0;
        comp = 1.0f;
        limGain = 1.0f;
        mixS.reset (sr, 0.05);
        outS.reset (sr, 0.05);
        mixS.setCurrentAndTargetValue (-1.0f);
        outS.setCurrentAndTargetValue (1.0f);
    }

    int getLatency() const { return N; }
    double getSampleRate() const { return sr; }

    /** Traitement en place (stéréo). */
    void process (float* L, float* R, int n, const Settings& s)
    {
        if (mixS.getCurrentValue() < 0.0f) mixS.setCurrentAndTargetValue (s.mix);
        mixS.setTargetValue (juce::jlimit (0.0f, 1.0f, s.mix));
        outS.setTargetValue (juce::Decibels::decibelsToGain (s.outDb));
        float* io[2] { L, R };

        for (int i = 0; i < n; ++i)
        {
            float wet[2], dry[2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = std::isfinite (io[c][i]) ? io[c][i] : 0.0f;
                inBuf[c][(size_t) (N - H + cnt)] = x;
                wet[c] = outBuf[c][(size_t) cnt];
                dry[c] = dryDelay[c][(size_t) dPos]; // entrée retardée de N : alignée sur le traité
                dryDelay[c][(size_t) dPos] = x;
            }
            if (++dPos >= N) dPos = 0;

            const float m = mixS.getNextValue();
            const float g = outS.getNextValue();
            float l = (dry[0] + (wet[0] - dry[0]) * m) * g;
            float r = (dry[1] + (wet[1] - dry[1]) * m) * g;

            // sécurité : plafond à -0,3 dBFS (attaque immédiate, relâche 150 ms)
            const float pk = juce::jmax (std::abs (l), std::abs (r));
            limGain += (1.0f - limGain) * limRel;
            if (pk * limGain > kCeil) limGain = kCeil / pk;
            io[0][i] = l * limGain;
            io[1][i] = r * limGain;

            if (++cnt >= H)
            {
                cnt = 0;
                processFrame (s);
            }
        }
    }

private:
    static constexpr int numBands = 64;
    static constexpr float kCeil = 0.966f;

    float rnd() // -1..1, générateur sans allocation
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) ((seed >> 8) & 0xffffff) / 8388608.0f - 1.0f;
    }

    void processFrame (const Settings& s)
    {
        const double hopSec = H / sr;
        const float smear = juce::jlimit (0.01f, 60.0f, s.smearSec);
        const float aDown = s.freeze ? 1.0f : (float) std::exp (-hopSec / smear);
        const float aUp = s.freeze ? 1.0f : (float) std::exp (-hopSec / juce::jmax (0.005f, smear * juce::jlimit (0.0f, 1.0f, s.rise)));
        const float twoPi = juce::MathConstants<float>::twoPi;

        // mouvement lent par bandes (1/6 d'octave), cibles qui changent toutes les ~1,5 s
        {
            const float pChange = (float) (hopSec / 1.5);
            const float k = (float) (1.0 - std::exp (-hopSec / 0.7));
            for (int b = 0; b < numBands; ++b)
            {
                if (0.5f * (rnd() + 1.0f) < pChange) tgDet[(size_t) b] = rnd();
                if (0.5f * (rnd() + 1.0f) < pChange) tgAmp[(size_t) b] = rnd();
                if (0.5f * (rnd() + 1.0f) < pChange) tgWid[(size_t) b] = rnd();
                rwDet[(size_t) b] += (tgDet[(size_t) b] - rwDet[(size_t) b]) * k;
                rwAmp[(size_t) b] += (tgAmp[(size_t) b] - rwAmp[(size_t) b]) * k;
                rwWid[(size_t) b] += (tgWid[(size_t) b] - rwWid[(size_t) b]) * k;
            }
        }

        // analyse : fenêtre + rotation de N/2 (trame à phase nulle)
        for (int c = 0; c < 2; ++c)
        {
            auto& w = work[c];
            const auto& in = inBuf[c];
            for (int i = 0; i < N; ++i)
            {
                const int j = (i + N / 2) & (N - 1);
                w[(size_t) i] = in[(size_t) j] * win[(size_t) j];
            }
            std::fill (w.begin() + N, w.end(), 0.0f);
            fft->performRealOnlyForwardTransform (w.data(), true);
        }

        float maxP = 0.0f;
        double eIn = 0.0;
        const float binNorm = 4.0f / (float) N; // pour l'affichage (≈ 0 dB pour un sinus plein)
        for (auto& d : dispTmpIn) d = 0.0f;
        for (int k = 0; k < K; ++k)
        {
            const float lr = work[0][(size_t) (2 * k)], li = work[0][(size_t) (2 * k + 1)];
            const float rr = work[1][(size_t) (2 * k)], ri = work[1][(size_t) (2 * k + 1)];
            const float pin[2] { lr * lr + li * li, rr * rr + ri * ri };
            for (int c = 0; c < 2; ++c)
            {
                float& p = P[c][(size_t) k];
                const float a = pin[c] > p ? aUp : aDown;
                p = a * p + (1.0f - a) * pin[c];
                if (p < 1e-24f) p = 0.0f;
            }
            const float mr = 0.5f * (lr + rr), mi = 0.5f * (li + ri);
            const float pm = mr * mr + mi * mi;
            const float ph = std::atan2 (mi, mr);
            const float d = std::remainder (ph - prevPh[(size_t) k] - (float) H * binW[(size_t) k], twoPi);
            prevPh[(size_t) k] = ph;
            const float om = binW[(size_t) k] + d / (float) H;
            W[(size_t) k] = aDown * W[(size_t) k] + (1.0f - aDown) * pm;
            F[(size_t) k] = aDown * F[(size_t) k] + (1.0f - aDown) * pm * om;
            if (W[(size_t) k] < 1e-24f) { W[(size_t) k] = 0.0f; F[(size_t) k] = 0.0f; }

            const float ps = 0.5f * (P[0][(size_t) k] + P[1][(size_t) k]);
            refP[(size_t) k] = ps; // provisoire : puissance lissée
            maxP = juce::jmax (maxP, ps);
            eIn += (double) P[0][(size_t) k] + P[1][(size_t) k];
            if (const int b = dispMap[(size_t) k]; b >= 0)
                dispTmpIn[(size_t) b] = juce::jmax (dispTmpIn[(size_t) b], pm);
        }

        // pics de la puissance lissée (maximum local sur ±2 bandes, au-dessus de -100 dB du max)
        int np = 0;
        const float floorP = maxP * 1e-10f;
        for (int k = 2; k < K - 2; ++k)
        {
            const float v = refP[(size_t) k];
            if (v > floorP && v >= refP[(size_t) k - 1] && v > refP[(size_t) k + 1]
                && v >= refP[(size_t) k - 2] && v > refP[(size_t) k + 2])
                peaks[(size_t) np++] = k;
        }

        // verrouillage de phase par région (frontière = minimum entre deux pics)
        const float detAmt = s.drift * 0.012f; // ±20 cents à 100 %
        if (np == 0)
        {
            for (int k = 0; k < K; ++k)
            {
                phOut[(size_t) k] = std::remainder (phOut[(size_t) k] + (float) H * binW[(size_t) k], twoPi);
                gainK[(size_t) k] = 1.0f;
            }
        }
        else
        {
            int lo = 0;
            for (int i = 0; i < np; ++i)
            {
                const int p = peaks[(size_t) i];
                int hi = K;
                if (i + 1 < np)
                {
                    const int q = peaks[(size_t) i + 1];
                    hi = p + 1;
                    for (int k = p + 1; k <= q; ++k)
                        if (refP[(size_t) k] < refP[(size_t) hi]) hi = k;
                }
                float om = W[(size_t) p] > 0.0f ? F[(size_t) p] / W[(size_t) p] : binW[(size_t) p];
                if (! std::isfinite (om)) om = binW[(size_t) p];
                om *= 1.0f + detAmt * band (rwDet, p);
                const float phase = std::remainder (phOut[(size_t) p] + (float) H * om, twoPi);
                const float pp = refP[(size_t) p];
                for (int k = lo; k < hi; ++k)
                {
                    phOut[(size_t) k] = phase;
                    const float ratio = pp > 0.0f ? juce::jmin (1.0f, refP[(size_t) k] / pp) : 0.0f;
                    gainK[(size_t) k] = s.focus > 0.001f ? std::pow (ratio, s.focus) : 1.0f;
                }
                lo = hi;
            }
        }

        // énergie : on compense ce que FOCUS retire (au plus +12 dB)
        double eOut = 0.0;
        for (int k = 0; k < K; ++k)
            eOut += ((double) P[0][(size_t) k] + P[1][(size_t) k]) * gainK[(size_t) k] * gainK[(size_t) k];
        const float target = eOut > 1e-30 ? (float) juce::jlimit (1.0, 4.0, std::sqrt (eIn / eOut)) : 1.0f;
        comp += (target - comp) * 0.3f;

        if (std::abs (s.tone - lastTone) > 1e-4f)
        {
            lastTone = s.tone;
            for (int k = 0; k < K; ++k)
            {
                const double f = juce::jlimit (20.0, 20000.0, k * sr / N);
                tilt[(size_t) k] = (float) std::pow (f / 1000.0, 0.5 * s.tone);
            }
        }

        // resynthèse
        const float ampAmt = s.drift * 0.35f, widAmt = s.drift * 0.7f;
        const int widFrom = (int) std::ceil (200.0 * N / sr); // la basse reste mono
        for (auto& d : dispTmpOut) d = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            auto& w = work[c];
            const float side = c == 0 ? 1.0f : -1.0f;
            for (int k = 0; k < K; ++k)
            {
                float mag = std::sqrt (P[c][(size_t) k]) * gainK[(size_t) k] * comp * tilt[(size_t) k];
                float ph = phOut[(size_t) k];
                if (ampAmt > 0.0f) mag *= 1.0f + ampAmt * band (rwAmp, k);
                if (widAmt > 0.0f && k >= widFrom) ph += side * widAmt * band (rwWid, k);
                if (k == 0 || k == K - 1) mag = 0.0f;
                w[(size_t) (2 * k)] = mag * std::cos (ph);
                w[(size_t) (2 * k + 1)] = mag * std::sin (ph);
                if (const int b = dispMap[(size_t) k]; b >= 0)
                    dispTmpOut[(size_t) b] = juce::jmax (dispTmpOut[(size_t) b], 0.5f * mag * mag);
            }
            fft->performRealOnlyInverseTransform (w.data());

            auto& o = outBuf[c];
            std::copy (o.begin() + H, o.end(), o.begin());
            std::fill (o.end() - H, o.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                const int j = (i + N / 2) & (N - 1); // on annule la rotation
                o[(size_t) j] += w[(size_t) i] * win[(size_t) j] * olaNorm;
            }
        }

        for (int c = 0; c < 2; ++c)
            std::copy (inBuf[c].begin() + H, inBuf[c].end(), inBuf[c].begin());

        for (int b = 0; b < dispBins; ++b)
        {
            dispIn[(size_t) b].store (juce::Decibels::gainToDecibels (std::sqrt (dispTmpIn[(size_t) b]) * binNorm, -120.0f), std::memory_order_relaxed);
            dispOut[(size_t) b].store (juce::Decibels::gainToDecibels (std::sqrt (dispTmpOut[(size_t) b]) * binNorm, -120.0f), std::memory_order_relaxed);
        }
    }

    float band (const std::array<float, numBands>& a, int k) const
    {
        const float pos = bandPos[(size_t) k];
        const int i = (int) pos;
        const float f = pos - (float) i;
        return a[(size_t) i] + (a[(size_t) i + 1] - a[(size_t) i]) * f;
    }

    double sr = 44100.0;
    int order = 12, N = 4096, H = 512, K = 2049;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, W, F, prevPh, phOut, refP, gainK, tilt, bandPos, binW;
    std::vector<float> inBuf[2], outBuf[2], work[2], P[2], dryDelay[2];
    std::vector<int> peaks, dispMap;
    std::array<float, numBands> rwDet {}, rwAmp {}, rwWid {}, tgDet {}, tgAmp {}, tgWid {};
    std::array<float, dispBins> dispTmpIn {}, dispTmpOut {};
    float olaNorm = 1.0f, comp = 1.0f, lastTone = 1000.0f, limGain = 1.0f;
    float limRel = 0.0002f;
    int cnt = 0, dPos = 0;
    uint32_t seed = 0x1234567u;
    juce::SmoothedValue<float> mixS { 1.0f }, outS { 1.0f };
};
} // namespace idg
