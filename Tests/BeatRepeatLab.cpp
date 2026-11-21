// Labo du Beat Repeat :
//   BeatRepeatLab                      -> tests de stabilité (NaN, plafond, clics, réglages aléatoires)
//   BeatRepeatLab --render <dossier>   -> boucle sèche de test + rendus traités (WAV) pour écoute
//   BeatRepeatLab --snapshot <png>     -> capture de l'interface
#include "../Source/Plugins/BeatRepeat/BeatRepeatProcessor.h"
#include "../Source/Plugins/BeatRepeat/BeatRepeatEditor.h"
#include <iostream>
#include <functional>

using namespace idg;

namespace
{
constexpr double kSr = 44100.0, kBpm = 120.0;
constexpr int kBars = 4;
const double kSpb = kSr * 60.0 / kBpm;
const int kLen = (int) (kBars * 4 * kSpb);

struct Stereo { std::vector<float> l, r; explicit Stereo (int n = 0) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {} };

// ---------------------------------------------------------------- boucle sèche de synthèse
Stereo makeDrums()
{
    Stereo s (kLen);
    juce::Random rnd (7);
    auto addKick = [&] (double beat)
    {
        const int st = (int) (beat * kSpb);
        double ph = 0.0;
        for (int i = 0; i < (int) (0.45 * kSr) && st + i < kLen; ++i)
        {
            const double t = i / kSr;
            const double f = 45.0 + 140.0 * std::exp (-t * 28.0);
            ph += f / kSr;
            const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * ph) * std::exp (-t * 6.0) * 0.9);
            s.l[(size_t) (st + i)] += v; s.r[(size_t) (st + i)] += v;
        }
    };
    auto addSnare = [&] (double beat, float amp)
    {
        const int st = (int) (beat * kSpb);
        dsp::SVF bp; bp.set (kSr, 1900.0f, 0.8f);
        double ph = 0.0;
        for (int i = 0; i < (int) (0.3 * kSr) && st + i < kLen; ++i)
        {
            const double t = i / kSr;
            ph += 190.0 / kSr;
            const float noise = bp.bandpass (0, rnd.nextFloat() * 2.0f - 1.0f) * 2.2f;
            const float tone = (float) std::sin (juce::MathConstants<double>::twoPi * ph) * (float) std::exp (-t * 30.0);
            const float v = amp * (noise * (float) std::exp (-t * 14.0) + 0.6f * tone);
            s.l[(size_t) (st + i)] += v * 0.95f; s.r[(size_t) (st + i)] += v;
        }
    };
    auto addHat = [&] (double beat, float amp)
    {
        const int st = (int) (beat * kSpb);
        dsp::SVF hp; hp.set (kSr, 8000.0f, 0.7f);
        for (int i = 0; i < (int) (0.06 * kSr) && st + i < kLen; ++i)
        {
            const float v = amp * hp.highpass (0, rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-i / kSr * 70.0);
            s.l[(size_t) (st + i)] += v; s.r[(size_t) (st + i)] += v * 0.8f;
        }
    };
    for (int bar = 0; bar < kBars; ++bar)
    {
        const double b0 = bar * 4.0;
        addKick (b0); addKick (b0 + 2.5);
        if (bar % 2 == 1) addKick (b0 + 3.25);
        addSnare (b0 + 1.0, 0.7f); addSnare (b0 + 3.0, 0.7f);
        if (bar == kBars - 1) addSnare (b0 + 3.75, 0.35f);
        for (int k = 0; k < 16; ++k) addHat (b0 + k * 0.25, (k % 2 == 0) ? 0.22f : 0.12f);
    }
    return s;
}

/** Basse façon Vital : deux dents de scie désaccordées + passe-bas à enveloppe (growl). */
Stereo makeBass()
{
    Stereo s (kLen);
    // (début en noires, durée, note MIDI)
    struct N { double b, d; int m; };
    std::vector<N> notes;
    const int pat[] { 37, 37, 49, 37, 40, 37, 44, 42 }; // autour de C#
    for (int bar = 0; bar < kBars; ++bar)
        for (int k = 0; k < 8; ++k)
            if (! (k == 3 && bar % 2 == 0)) notes.push_back ({ bar * 4.0 + k * 0.5, k == 7 ? 0.5 : 0.42, pat[k] + (bar == 2 ? -2 : 0) });
    dsp::SVF f; f.reset();
    std::vector<double> ph1 (1, 0.0), ph2 (1, 0.0);
    double p1 = 0.0, p2 = 0.0, p3 = 0.0;
    for (const auto& n : notes)
    {
        const int st = (int) (n.b * kSpb), len = (int) (n.d * kSpb);
        const double hz = 440.0 * std::pow (2.0, (n.m - 69) / 12.0);
        for (int i = 0; i < len + (int) (0.01 * kSr) && st + i < kLen; ++i)
        {
            const double t = i / kSr;
            p1 += hz * 1.004 / kSr; p2 += hz * 0.996 / kSr; p3 += hz * 0.5 / kSr;
            p1 -= std::floor (p1); p2 -= std::floor (p2); p3 -= std::floor (p3);
            const float saw = (float) ((2.0 * p1 - 1.0) + (2.0 * p2 - 1.0)) * 0.5f;
            const float sub = (float) std::sin (juce::MathConstants<double>::twoPi * p3);
            const float env = (float) std::min (1.0, t / 0.003) * (i > len ? (float) std::max (0.0, 1.0 - (i - len) / (0.01 * kSr)) : 1.0f);
            const float cut = 180.0f + 2600.0f * (float) std::exp (-t * 9.0) + 400.0f * (float) (0.5 + 0.5 * std::sin (t * 37.0));
            f.set (kSr, cut, 2.2f);
            const float v = (dsp::fastTanh (f.lowpass (0, saw) * 2.2f) * 0.42f + sub * 0.3f) * env;
            s.l[(size_t) (st + i)] += v; s.r[(size_t) (st + i)] += v;
        }
    }
    return s;
}

Stereo mixOf (const Stereo& a, const Stereo& b)
{
    Stereo s (kLen);
    float peak = 1.0e-6f;
    for (size_t i = 0; i < (size_t) kLen; ++i)
    {
        s.l[i] = a.l[i] * 0.8f + b.l[i]; s.r[i] = a.r[i] * 0.8f + b.r[i];
        peak = std::max ({ peak, std::abs (s.l[i]), std::abs (s.r[i]) });
    }
    for (size_t i = 0; i < (size_t) kLen; ++i) { s.l[i] *= 0.7f / peak; s.r[i] *= 0.7f / peak; } // crête à -3 dBFS
    return s;
}

void writeWav (const juce::File& f, const Stereo& s)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto os = std::make_unique<juce::FileOutputStream> (f);
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kSr, 2, 24, {}, 0));
    if (w == nullptr) { std::cerr << "écriture impossible : " << f.getFullPathName() << std::endl; return; }
    os.release();
    const float* ch[] { s.l.data(), s.r.data() };
    w->writeFromFloatArrays (ch, 2, (int) s.l.size());
}

/** Rend `src` à travers le moteur ; `automate` modifie les réglages selon la position (en noires). */
Stereo render (const Stereo& src, LoopRepeat::Settings base, std::function<void (LoopRepeat::Settings&, double)> automate,
               double sr = kSr, int block = 64)
{
    LoopRepeat e;
    e.prepare (sr);
    Stereo out = src;
    const double spb = sr * 60.0 / kBpm;
    for (int i = 0; i < kLen; i += block)
    {
        const int n = std::min (block, kLen - i);
        LoopRepeat::Settings s = base;
        const double beat = i / spb;
        if (automate) automate (s, beat);
        LoopRepeat::Transport t;
        t.bpm = kBpm; t.ppq = beat; t.playing = true; t.quartersPerBar = 4.0;
        e.process (out.l.data() + i, out.r.data() + i, n, s, t);
    }
    return out;
}

float speedForMs (double ms) { return (float) (-std::log2 (ms / 2000.0) / LoopRepeat::speedOctaves); }
float speedForBeats (double beats) { return (float) (-std::log2 (beats / 4.0) / LoopRepeat::speedOctaves); }

int renderMain (const juce::File& dir)
{
    dir.createDirectory();
    const auto drums = makeDrums(), bass = makeBass(), full = mixOf (drums, bass);
    writeWav (dir.getChildFile ("00_sec_basse.wav"), bass);
    writeWav (dir.getChildFile ("00_sec_batterie_basse.wav"), full);

    // 1. Le buzz du Loop d'Infiltrator : recapture toutes les demi-mesures, Speed jouée à la main
    //    (mêmes longueurs que mesurées sur la boucle de référence).
    auto buzzSpeed = [] (LoopRepeat::Settings& s, double beat)
    {
        static const double ms[] { 83.3, 31.25, 11.45, 9.64, 20.84, 20.84, 31.25, 15.6 };
        s.speed = speedForMs (ms[juce::jlimit (0, 7, (int) (beat / 2.0))]);
    };
    LoopRepeat::Settings buzz;
    buzz.tempoIndex = 8; // 1/2
    buzz.dir = LoopRepeat::PingPong;
    buzz.smooth = 0.3f;
    buzz.glideMs = 40.0f;
    writeWav (dir.getChildFile ("01_buzz_aller-retour_smooth30.wav"), render (bass, buzz, buzzSpeed));
    {
        auto b = buzz; b.dir = LoopRepeat::Forward; b.smooth = 0.0f;
        writeWav (dir.getChildFile ("02_buzz_avant_smooth0_sec.wav"), render (bass, b, buzzSpeed));
        b.smooth = 1.0f;
        writeWav (dir.getChildFile ("03_buzz_avant_smooth100_doux.wav"), render (bass, b, buzzSpeed));
    }
    writeWav (dir.getChildFile ("04_buzz_sur_batterie_basse.wav"), render (full, buzz, buzzSpeed));

    // 5. Beat repeat classique : 1 mesure sur 2, recapture à chaque temps, boucle 1/16.
    {
        LoopRepeat::Settings s;
        s.tempoIndex = 6; s.speed = speedForBeats (0.25); s.snap = true; s.smooth = 0.1f;
        writeWav (dir.getChildFile ("05_classique_1-16_mesures_2_et_4.wav"),
                  render (full, s, [] (LoopRepeat::Settings& x, double beat) { x.active = ((int) (beat / 4.0)) % 2 == 1; }));
    }
    // 6. Roulement de fin de mesure : Hold, 1/8 qui accélère (rampe +), sur les 2 derniers temps.
    {
        LoopRepeat::Settings s;
        s.tempoIndex = 0; s.speed = speedForBeats (0.5); s.ramp = 1.0f; s.smooth = 0.15f; s.glideMs = 0.0f;
        writeWav (dir.getChildFile ("06_roulement_rampe_fin_de_mesure.wav"),
                  render (full, s, [] (LoopRepeat::Settings& x, double beat) { x.active = std::fmod (beat, 4.0) >= 2.0; }));
    }
    // 7. Arrière, 1/8, recapture toutes les demi-mesures.
    {
        LoopRepeat::Settings s;
        s.tempoIndex = 8; s.speed = speedForBeats (0.5); s.snap = true; s.dir = LoopRepeat::Reverse; s.smooth = 0.2f;
        writeWav (dir.getChildFile ("07_arriere_1-8.wav"), render (full, s, nullptr));
    }
    // 8. Montée : 1 mesure de capture, Speed balayée de 1/16 jusqu'au buzz aigu avec un long glide.
    {
        LoopRepeat::Settings s;
        s.tempoIndex = 10; s.glideMs = 250.0f; s.smooth = 0.2f; s.dir = LoopRepeat::PingPong;
        writeWav (dir.getChildFile ("08_montee_speed_glide.wav"),
                  render (bass, s, [] (LoopRepeat::Settings& x, double beat)
                  {
                      const double ph = std::fmod (beat, 4.0) / 4.0;
                      x.speed = speedForBeats (0.25) + (float) ph * (speedForMs (3.0) - speedForBeats (0.25));
                  }));
    }
    std::cout << "rendus écrits dans " << dir.getFullPathName() << std::endl;
    return 0;
}

// ---------------------------------------------------------------- tests
juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [OK]    " : "  [ECHEC] ") << what << std::endl;
    if (! ok) ++failures;
}

int testsMain()
{
    std::cout << "=== Tests Beat Repeat ===" << std::endl;
    const auto full = mixOf (makeDrums(), makeBass());

    // 1. réglages aléatoires, blocs de tailles variables, plusieurs fréquences d'échantillonnage
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        juce::Random rnd (42);
        LoopRepeat e;
        e.prepare (sr);
        Stereo x = full;
        bool finiteOk = true;
        float peak = 0.0f;
        LoopRepeat::Settings s;
        double beat = 0.0;
        for (int i = 0; i < kLen;)
        {
            const int n = std::min (1 + rnd.nextInt (1024), kLen - i);
            if (rnd.nextFloat() < 0.15f)
            {
                s.active = rnd.nextFloat() < 0.8f; s.sync = rnd.nextBool(); s.tempoIndex = rnd.nextInt (LoopRepeat::numTempo);
                s.tempoMs = 10.0f + rnd.nextFloat() * 2000.0f; s.speed = rnd.nextFloat(); s.snap = rnd.nextBool();
                s.smooth = rnd.nextFloat(); s.glideMs = rnd.nextFloat() * 500.0f; s.ramp = rnd.nextFloat() * 2.0f - 1.0f;
                s.dir = rnd.nextInt (3); s.mix = rnd.nextFloat(); s.gainDb = rnd.nextFloat() * 12.0f - 6.0f;
            }
            LoopRepeat::Transport t;
            t.bpm = 60.0 + rnd.nextFloat() * 120.0; t.ppq = beat; t.playing = rnd.nextFloat() < 0.9f;
            e.process (x.l.data() + i, x.r.data() + i, n, s, t);
            for (int k = i; k < i + n; ++k)
            {
                finiteOk &= std::isfinite (x.l[(size_t) k]) && std::isfinite (x.r[(size_t) k]);
                peak = std::max ({ peak, std::abs (x.l[(size_t) k]), std::abs (x.r[(size_t) k]) });
            }
            beat += n / (sr * 60.0 / t.bpm);
            i += n;
        }
        check (finiteOk, "aucun NaN/inf (" + juce::String (sr, 0) + U (" Hz, réglages aléatoires)"));
        check (peak <= 2.0f, U ("plafond de sécurité respecté (crête ") + juce::String (peak, 3) + ")");
    }

    // 2. clics : sinus pur, toutes les longueurs et tous les sens, smooth au minimum
    {
        float worst = 0.0f;
        juce::String worstCase;
        for (int dir = 0; dir < 3; ++dir)
            for (float sp : { 0.2f, 0.45f, 0.62f, 0.8f })
                for (int tempo : { 2, 6, 8 })
                {
                    Stereo sine (kLen);
                    for (int i = 0; i < kLen; ++i)
                        sine.l[(size_t) i] = sine.r[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * i / kSr);
                    LoopRepeat::Settings s;
                    s.dir = dir; s.speed = sp; s.tempoIndex = tempo; s.smooth = 0.0f; s.glideMs = 0.0f;
                    auto out = render (sine, s, [] (LoopRepeat::Settings& x, double beat) { x.active = std::fmod (beat, 3.0) < 2.2; });
                    for (int i = 1; i < kLen; ++i)
                    {
                        const float d = std::abs (out.l[(size_t) i] - out.l[(size_t) i - 1]);
                        if (d > worst) { worst = d; worstCase = "sens " + juce::String (dir) + ", speed " + juce::String (sp) + ", tempo " + juce::String (tempo); }
                    }
                }
        // pente max d'un sinus 110 Hz à 0.5 : 0.0078 / échantillon. Un clic franc dépasse 0.2.
        check (worst < 0.06f, "pas de clic (saut max " + juce::String (worst, 4) + ", pire cas : " + worstCase + ")");
    }

    // 3. bypass exact quand REPEAT est éteint
    {
        LoopRepeat::Settings s; s.active = false;
        auto out = render (full, s, nullptr);
        float diff = 0.0f;
        for (size_t i = 0; i < (size_t) kLen; ++i) diff = std::max (diff, std::abs (out.l[i] - full.l[i]));
        check (diff < 1.0e-6f, U ("REPEAT éteint = signal intact"));
    }

    // 4. la boucle se répète bien à la longueur demandée (autocorrélation sur un buzz 1/64)
    {
        LoopRepeat::Settings s; s.tempoIndex = 10; s.speed = speedForBeats (1.0 / 16.0); s.snap = true; s.smooth = 0.0f; s.glideMs = 0.0f;
        auto out = render (full, s, nullptr);
        const int L = (int) std::round (kSpb / 16.0); // 1/64 = 1/16 de noire
        const int a = (int) (1.5 * kSpb);
        double num = 0.0, den = 0.0;
        for (int i = a; i < a + 4000; ++i) { num += out.l[(size_t) i] * out.l[(size_t) (i + L)]; den += out.l[(size_t) i] * out.l[(size_t) i]; }
        check (den > 0.0 && num / den > 0.97, U ("période de la boucle = 1/64 (corrélation ") + juce::String (num / juce::jmax (1.0e-9, den), 3) + ")");
    }

    // 5. coût CPU
    {
        LoopRepeat::Settings s; s.dir = LoopRepeat::Reverse; s.smooth = 0.5f;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 10; ++k) render (full, s, nullptr);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double rt = (10.0 * kLen / kSr * 1000.0) / ms;
        check (rt > 50.0, "CPU : " + juce::String (rt, 0) + U ("x plus rapide que le temps réel"));
    }

    std::cout << (failures == 0 ? "TOUT EST OK" : juce::String (failures) + " ECHEC(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}

int snapshotMain (const juce::String& path)
{
    BeatRepeatProcessor p;
    p.setPlayConfigDetails (2, 2, kSr, 512);
    p.prepareToPlay (kSr, 512);
    if (auto* sp = p.apvts.getParameter ("speed")) sp->setValueNotifyingHost (speedForMs (20.84));
    if (auto* d = p.apvts.getParameter ("dir")) d->setValueNotifyingHost (d->convertTo0to1 (1.0f));
    if (auto* r = p.apvts.getParameter ("smooth")) r->setValueNotifyingHost (0.3f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int k = 0; k < 40; ++k) { buf.clear(); p.processBlock (buf, midi); }
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (auto* be = dynamic_cast<BeatRepeatEditor*> (ed.get())) be->refreshNow();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "capture : " << f.getFullPathName() << std::endl;
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--render")
        return renderMain (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return snapshotMain (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).getFullPathName());
    return testsMain();
}
