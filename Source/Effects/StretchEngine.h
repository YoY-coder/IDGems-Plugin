#pragma once
#include "../Core/Dsp.h"

namespace idg
{
/** Stretch : à chaque pas rythmique (Rate), le son joue le temps d'un grain puis reste « coincé »
    sur cette frame.
    Deux moteurs :
    - Spectral (défaut) : vocodeur de phase à verrouillage de phase (Laroche-Dolson). Chaque partiel
      continue à sa vraie fréquence : les notes d'origine restent, étirées à l'infini.
    - Grains : nuage de grains (Hann x4). Les grains courts imposent leur propre hauteur (buzz métallique).
    - Grain   : taille de la frame figée (= fenêtre d'analyse en spectral)
    - Texture : spectral = la frame analysée bouge dans la zone figée + phases bousculées (nappe vivante,
                granuleuse) ; grains = aléa de position / d'espacement
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
        int mode = 0;            // 0 = spectral, 1 = grains
    };

    static constexpr int numGrains = 24;
    static constexpr double maxGrainMs = 1000.0;
    static constexpr int maxOrder = 16;              // FFT jusqu'à 65536 points
    static constexpr int maxW = 1 << maxOrder;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // retour en arrière max : grain * (1 + 1.5 * texture) + marge
        buf.allocate (juce::jmax ((int) (sr * (maxGrainMs * 0.001 * 3.0 + 0.5)), 3 * maxW + 4096));
        for (int o = 5; o <= maxOrder; ++o) ffts[(size_t) o] = std::make_unique<juce::dsp::FFT> (o);
        fftBuf.assign ((size_t) (2 * maxW), 0.0f);
        win.assign ((size_t) maxW, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            ch[c].mag.assign ((size_t) (maxW / 2 + 1), 0.0f);
            ch[c].ph.assign ((size_t) (maxW / 2 + 1), 0.0f);
            ch[c].omega.assign ((size_t) (maxW / 2 + 1), 0.0f);
            ch[c].syn.assign ((size_t) (maxW / 2 + 1), 0.0f);
            ch[c].peakOf.assign ((size_t) (maxW / 2 + 1), 0);
        }
        olaSize = juce::nextPowerOfTwo (2 * maxW + 1024);
        olaMask = olaSize - 1;
        ola[0].assign ((size_t) olaSize, 0.0f);
        ola[1].assign ((size_t) olaSize, 0.0f);
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
        for (auto& o : ola) std::fill (o.begin(), o.end(), 0.0f);
        spectral = false;
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
        const bool specMode = s.mode == 0;
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
                spectral = specMode;
                if (spectral)
                    startSpectral (G, tex, speed);
                else
                {
                    for (int k = 3; k >= 0; --k)
                        spawn (G, tex, 0.0f, hop * k);
                    spawnAcc = hop * (1.0 + 0.45 * tex * rng.bi());
                }
            }

            const bool wantFrozen = cloud && phase < (double) hold;
            wet = juce::jlimit (0.0f, 1.0f, wet + (wantFrozen ? wetStep : -wetStep));

            if (cloud && spectral)
            {
                if (t >= nextFrame)
                {
                    spectralFrame (nextFrame, tex, speed);
                    nextFrame += sH;
                }
            }
            else if (cloud)
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

            {
                // sortie du vocodeur (les traînes restent lisibles pendant le fondu de sortie)
                const auto oi = (size_t) (t & olaMask);
                l += ola[0][oi] * specNorm / norm;
                r += ola[1][oi] * specNorm / norm;
                ola[0][oi] = ola[1][oi] = 0.0f;
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

    // ------------------------------------------------------------------ spectral
    static float princarg (float x) { return x - dsp::twoPi * std::round (x / dsp::twoPi); }

    /** Analyse : spectre de la frame [pos, pos + W) + fréquence instantanée par bin
        (écart de phase avec la frame [pos - H, pos - H + W)). */
    void analyse (int c, int64_t pos)
    {
        auto& C = ch[c];
        const int half = sN / 2;
        auto* fft = ffts[(size_t) sOrder].get();
        // frame précédente : phases seulement
        std::fill (fftBuf.begin(), fftBuf.begin() + 2 * sN, 0.0f);
        for (int n = 0; n < sW; ++n) fftBuf[(size_t) n] = buf.at (c, (int) (pos - sH + n)) * win[(size_t) n];
        fft->performRealOnlyForwardTransform (fftBuf.data(), true);
        for (int k = 0; k <= half; ++k)
            C.omega[(size_t) k] = std::atan2 (fftBuf[(size_t) (2 * k + 1)], fftBuf[(size_t) (2 * k)]);
        // frame courante
        std::fill (fftBuf.begin(), fftBuf.begin() + 2 * sN, 0.0f);
        for (int n = 0; n < sW; ++n) fftBuf[(size_t) n] = buf.at (c, (int) (pos + n)) * win[(size_t) n];
        fft->performRealOnlyForwardTransform (fftBuf.data(), true);
        float maxMag = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const float re = fftBuf[(size_t) (2 * k)], im = fftBuf[(size_t) (2 * k + 1)];
            const float m = std::sqrt (re * re + im * im);
            const float phB = std::atan2 (im, re);
            const float expected = dsp::twoPi * (float) k * (float) sH / (float) sN;
            const float dev = princarg (phB - C.omega[(size_t) k] - expected);
            C.omega[(size_t) k] = (dsp::twoPi * (float) k / (float) sN) + dev / (float) sH; // rad / échantillon
            C.mag[(size_t) k] = m;
            C.ph[(size_t) k] = phB;
            maxMag = juce::jmax (maxMag, m);
        }
        // pics (maxima locaux sur ±2 bins) et région d'influence de chaque pic
        const float thr = maxMag * 1.0e-4f;
        int lastPeak = -1;
        for (int k = 0; k <= half; ++k)
        {
            const float m = C.mag[(size_t) k];
            bool peak = m > thr;
            for (int d = -2; peak && d <= 2; ++d)
            {
                const int j = k + d;
                if (d == 0 || j < 0 || j > half) continue;
                if (C.mag[(size_t) j] > m) peak = false;
            }
            if (peak)
            {
                // les bins entre deux pics vont au plus proche
                const int from = lastPeak < 0 ? 0 : (lastPeak + k) / 2 + 1;
                for (int j = from; j <= k; ++j) C.peakOf[(size_t) j] = k;
                lastPeak = k;
            }
            else if (lastPeak >= 0)
                C.peakOf[(size_t) k] = lastPeak;
            else
                C.peakOf[(size_t) k] = k;
        }
    }

    int64_t analysisPos (int64_t frameStart, float tex, float speed)
    {
        const double elapsed = (double) juce::jmax ((int64_t) 0, frameStart - cloudStart);
        double pos = (double) (anchor - sW) + (double) speed * elapsed - (double) rng.uni() * tex * (double) sW;
        // la frame analysée doit être entièrement écrite
        pos = juce::jmin (pos, (double) (t - sW - 1));
        return (int64_t) std::llround (pos);
    }

    /** Synthèse d'une frame démarrant à l'instant absolu frameStart (ajout dans le tampon OLA). */
    void synth (int c, int64_t frameStart, bool first, float tex)
    {
        auto& C = ch[c];
        const int half = sN / 2;
        const float advance = (float) sH;
        const float jitter = tex * tex * dsp::pi;
        if (first)
            for (int k = 0; k <= half; ++k) C.syn[(size_t) k] = C.ph[(size_t) k];
        else
        {
            // les pics avancent à leur vraie fréquence, les autres bins suivent leur pic (verrouillage)
            for (int k = 0; k <= half; ++k)
                if (C.peakOf[(size_t) k] == k)
                    C.syn[(size_t) k] = princarg (C.syn[(size_t) k] + C.omega[(size_t) k] * advance + jitter * rng.bi());
            for (int k = 0; k <= half; ++k)
            {
                const int p = C.peakOf[(size_t) k];
                if (p != k) C.syn[(size_t) k] = C.syn[(size_t) p] + (C.ph[(size_t) k] - C.ph[(size_t) p]);
            }
        }
        for (int k = 0; k <= half; ++k)
        {
            const float m = C.mag[(size_t) k] * (1.0f + 0.5f * tex * rng.bi());
            fftBuf[(size_t) (2 * k)] = m * std::cos (C.syn[(size_t) k]);
            fftBuf[(size_t) (2 * k + 1)] = m * std::sin (C.syn[(size_t) k]);
        }
        std::fill (fftBuf.begin() + 2 * (half + 1), fftBuf.begin() + 2 * sN, 0.0f);
        ffts[(size_t) sOrder]->performRealOnlyInverseTransform (fftBuf.data());
        auto& o = ola[c];
        for (int n = 0; n < sW; ++n)
        {
            const int64_t idx = frameStart + n;
            if (idx < cloudStart) continue; // le passé (frames pré-calculées) n'est jamais relu
            o[(size_t) (idx & olaMask)] += fftBuf[(size_t) n] * win[(size_t) n];
        }
    }

    void startSpectral (double G, float tex, float speed)
    {
        sW = juce::jlimit (64, maxW, (int) std::lround (G)) & ~3;
        sH = sW / 4;
        sOrder = juce::jlimit (5, maxOrder, (int) std::ceil (std::log2 ((double) sW)));
        sN = 1 << sOrder;
        for (int n = 0; n < sW; ++n) win[(size_t) n] = 0.5f - 0.5f * std::cos (dsp::twoPi * (float) n / (float) sW);
        // analyse x synthèse Hann à recouvrement 4 : somme = 1.5
        specNorm = 1.0f / 1.5f;
        for (auto& o : ola) std::fill (o.begin(), o.end(), 0.0f);
        // 4 frames pré-calculées : le gel démarre déjà en régime (pas de montée)
        for (int k = 3; k >= 0; --k)
            spectralFrame (cloudStart - (int64_t) k * sH, tex, speed, k == 3);
        nextFrame = cloudStart + sH;
    }

    void spectralFrame (int64_t frameStart, float tex, float speed, bool first = false)
    {
        // gel pur (texture 0, vitesse 0) : une seule analyse suffit
        const bool reanalyse = first || tex > 0.0f || speed > 0.0f;
        if (reanalyse)
        {
            const auto pos = analysisPos (frameStart, tex, speed);
            for (int c = 0; c < 2; ++c) analyse (c, pos);
        }
        for (int c = 0; c < 2; ++c) synth (c, frameStart, first, tex);
    }

    // ------------------------------------------------------------------ grains
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

    // vocodeur
    struct Chan { std::vector<float> mag, ph, omega, syn; std::vector<int> peakOf; };
    std::array<std::unique_ptr<juce::dsp::FFT>, maxOrder + 1> ffts;
    std::vector<float> fftBuf, win;
    std::array<Chan, 2> ch;
    std::array<std::vector<float>, 2> ola;
    int olaSize = 0;
    int64_t olaMask = 0, nextFrame = 0;
    int sW = 1024, sH = 256, sN = 1024, sOrder = 10;
    float specNorm = 1.0f;
    bool spectral = false;
};
} // namespace idg
