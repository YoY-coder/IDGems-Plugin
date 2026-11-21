// Tests du module solo IDGems Delay : timing exact, règles du random, feedback 110 %, NaN, clics, plafond.
// Modes annexes :
//   IDGemsDelayTests --snapshot capture.png           capture de l'interface
//   IDGemsDelayTests --render boucle_140bpm.wav dossier/   rend plusieurs réglages en WAV pour écoute
//   IDGemsDelayTests --render-one source.wav sortie.wav sync=0 time=43.2 mode=1 ...   un réglage précis
#include "../Plugins/Delay/DelayProcessor.h"
#include "../Plugins/Delay/DelayEditor.h"

using namespace idg;

namespace
{
struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setIsPlaying (true);
        return p;
    }
};

int failures = 0;
void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::cout << "  [ECHEC] " << what << std::endl; }
}

void set (DelayProcessor& p, const juce::String& id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Harness
{
    DelayProcessor proc;
    FakePlayHead head;
    double sr = 48000.0;
    int block = 480;
    Harness() { proc.setPlayHead (&head); proc.prepareToPlay (sr, block); }

    /** Traite un signal (L, R) en place, bloc par bloc. */
    void process (std::vector<float>& L, std::vector<float>& R)
    {
        juce::MidiBuffer midi;
        for (size_t i = 0; i < L.size(); i += (size_t) block)
        {
            const int n = (int) juce::jmin ((size_t) block, L.size() - i);
            float* chans[2] { L.data() + i, R.data() + i };
            juce::AudioBuffer<float> b (chans, 2, n);
            proc.processBlock (b, midi);
        }
    }
};

/** Position du premier pic au-dessus de `thr` après `from`. */
int firstPeak (const std::vector<float>& x, int from, float thr)
{
    for (int i = from; i < (int) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > thr) return i;
    return -1;
}
} // namespace

int snapshotMain (const juce::String& path);
int renderMain (const juce::String& wav, const juce::String& dir);
int renderOneMain (int argc, char** argv);

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot") return snapshotMain (argv[2]);
    if (argc >= 4 && juce::String (argv[1]) == "--render") return renderMain (argv[2], argv[3]);
    if (argc >= 4 && juce::String (argv[1]) == "--render-one") return renderOneMain (argc, argv);
    std::cout << "=== Tests IDGems Delay ===" << std::endl;

    // 1. Timing : une impulsion revient exactement au bon endroit (sync et free), latence comprise
    {
        std::cout << "1. Timing des répétitions..." << std::endl;
        struct Case { bool sync; int note, feel; float ms; double expectSec; };
        const Case cases[] {
            { true, 3, GlitchDelay::Straight, 0, 0.125 },         // 1/16 à 120 BPM
            { true, 4, GlitchDelay::Dotted, 0, 0.375 },           // 1/8D
            { true, 2, GlitchDelay::Triplet, 0, 0.0625 * 2 / 3 }, // 1/32T
            { true, 5, GlitchDelay::Dotted, 0, 0.5 },             // 1/4 pointée -> plafonnée à 1/4
            { false, 0, 0, 7.5f, 0.0075 },
            { false, 0, 0, 0.5f, 0.0005 } };
        for (const auto& c : cases)
        {
            Harness h;
            set (h.proc, "sync", c.sync ? 1.0f : 0.0f);
            set (h.proc, "note", (float) c.note);
            set (h.proc, "feel", (float) c.feel);
            set (h.proc, "time", c.ms);
            set (h.proc, "feedback", 0.0f);
            set (h.proc, "mix", 1.0f);
            std::vector<float> L ((size_t) (h.sr * 1.2), 0.0f), R = L;
            const int at = 4800;
            L[(size_t) at] = R[(size_t) at] = 0.5f;
            h.process (L, R);
            const int lat = h.proc.getLatencySamples();
            const int expect = at + lat + (int) std::lround (c.expectSec * h.sr);
            const int got = firstPeak (L, at, 0.05f);
            check (std::abs (got - expect) <= 2, "écho attendu à " + juce::String (expect) + ", obtenu " + juce::String (got));
        }
        std::cout << "   latence déclarée : " << Harness().proc.getLatencySamples() << " échantillons" << std::endl;
    }

    // 2. Ping-pong : 1re répétition à gauche, 2e à droite
    {
        std::cout << "2. Ping-pong..." << std::endl;
        Harness h;
        set (h.proc, "mode", 1.0f);
        set (h.proc, "note", 3.0f);
        set (h.proc, "feedback", 0.8f);
        set (h.proc, "mix", 1.0f);
        std::vector<float> L ((size_t) h.sr, 0.0f), R = L;
        L[4800] = R[4800] = 0.5f;
        h.process (L, R);
        const int lat = h.proc.getLatencySamples();
        const int t = 6000; // 1/16 à 120 BPM
        const int e1 = 4800 + lat + t, e2 = 4800 + lat + 2 * t;
        std::cout << "   e1 L " << L[(size_t) e1] << " R " << R[(size_t) e1] << " / e2 L " << L[(size_t) e2] << " R " << R[(size_t) e2]
                  << " / pic L " << firstPeak (L, 4900, 0.05f) << " R " << firstPeak (R, 4900, 0.03f) << " attendu " << e1 << std::endl;
        check (std::abs (L[(size_t) e1]) > 0.05f && std::abs (R[(size_t) e1]) < 0.01f, "ping-pong : 1re répétition pas à gauche seule");
        check (std::abs (R[(size_t) e2]) > 0.03f && std::abs (L[(size_t) e2]) < 0.01f, "ping-pong : 2e répétition pas à droite seule");
    }

    // 3. Règles du random
    {
        std::cout << "3. Random (jamais de ternaire, feedback <= 100 %, mix intact)..." << std::endl;
        Harness h;
        set (h.proc, "mix", 0.37f);
        juce::Random rng (7);
        int dotted = 0, syncs = 0;
        for (int i = 0; i < 2000; ++i)
        {
            h.proc.randomize (rng);
            const auto s = h.proc.currentSettings();
            check (s.feel != GlitchDelay::Triplet, "random a choisi du ternaire");
            check (s.feedback <= 1.0f + 1.0e-4f, "random a dépassé 100 % de feedback : " + juce::String (s.feedback));
            check (std::abs (s.mix - 0.37f) < 1.0e-3f, "random a touché au mix");
            check (GlitchDelay::noteBeats (s.note, s.feel) <= 1.0, "random au-delà d'une noire");
            check (s.lowCut < s.highCut, "random : coupe-bas au-dessus du coupe-haut");
            dotted += s.feel == GlitchDelay::Dotted;
            syncs += s.sync;
        }
        std::cout << "   sur 2000 : " << syncs << " sync, " << dotted << " pointés, 0 ternaire" << std::endl;
        const float before = h.proc.currentSettings().feedback;
        h.proc.randomize (rng);
        h.proc.undoRandom();
        check (std::abs (h.proc.currentSettings().feedback - before) < 1.0e-3f, "RETOUR ne restaure pas le réglage précédent");
    }

    // 4. Feedback 110 % : la boucle s'emballe mais reste bornée, sans NaN, sous le plafond
    {
        std::cout << "4. Feedback 110 % (emballement contrôlé)..." << std::endl;
        for (int mode = 0; mode < 3; ++mode)
        {
            Harness h;
            set (h.proc, "mode", (float) mode);
            set (h.proc, "feedback", 1.1f);
            set (h.proc, "mix", 1.0f);
            set (h.proc, "note", 1.0f);
            std::vector<float> L ((size_t) (h.sr * 20), 0.0f), R = L;
            juce::Random rnd (3);
            for (int i = 0; i < 4800; ++i) L[(size_t) i] = R[(size_t) i] = (rnd.nextFloat() * 2 - 1) * 0.8f;
            h.process (L, R);
            float pk = 0, tail = 0; bool nan = false;
            for (size_t i = 0; i < L.size(); ++i)
            {
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) nan = true;
                pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
                if (i > L.size() - 48000) tail = juce::jmax (tail, std::abs (L[i]));
            }
            check (! nan, "NaN en feedback 110 %");
            check (pk <= dsp::dbToGain (-0.3f) + 1.0e-4f, "plafond dépassé : " + juce::String (pk));
            check (tail > 0.05f, "à 110 % la boucle devrait encore sonner après 20 s");
            std::cout << "   mode " << mode << " : crête " << pk << ", queue " << tail << std::endl;
        }
    }

    // 5. Feedback < 100 % : la queue s'éteint
    {
        std::cout << "5. Extinction de la queue..." << std::endl;
        Harness h;
        set (h.proc, "feedback", 0.9f);
        set (h.proc, "mix", 1.0f);
        std::vector<float> L ((size_t) (h.sr * 12), 0.0f), R = L;
        for (int i = 0; i < 2400; ++i) L[(size_t) i] = R[(size_t) i] = std::sin ((float) i * 0.05f) * 0.7f;
        h.process (L, R);
        float tail = 0;
        for (size_t i = L.size() - 4800; i < L.size(); ++i) tail = juce::jmax (tail, std::abs (L[i]));
        check (tail < 0.001f, "la queue ne s'éteint pas à 90 % : " + juce::String (tail));
    }

    // 6. Clics : changement brutal de division / mode / sync sur un sinus continu
    {
        std::cout << "6. Clics aux changements de temps et de mode..." << std::endl;
        Harness h;
        set (h.proc, "feedback", 0.6f);
        set (h.proc, "mix", 1.0f);
        const int n = (int) h.sr * 4;
        std::vector<float> L ((size_t) n), R;
        for (int i = 0; i < n; ++i) L[(size_t) i] = 0.4f * std::sin (2.0f * dsp::pi * 110.0f * (float) i / (float) h.sr);
        R = L;
        juce::MidiBuffer midi;
        juce::Random rng (11);
        float maxJump = 0, prev = 0;
        for (int i = 0; i < n; i += h.block)
        {
            if ((i / h.block) % 20 == 10)
            {
                set (h.proc, "note", (float) rng.nextInt (6));
                set (h.proc, "mode", (float) rng.nextInt (3));
                set (h.proc, "sync", rng.nextBool() ? 1.0f : 0.0f);
                set (h.proc, "time", 5.0f + 200.0f * rng.nextFloat());
            }
            float* chans[2] { L.data() + i, R.data() + i };
            juce::AudioBuffer<float> b (chans, 2, h.block);
            h.proc.processBlock (b, midi);
            if (i > h.sr * 0.6)
                for (int k = 0; k < h.block; ++k)
                {
                    const float j = std::abs (L[(size_t) (i + k)] - prev);
                    if (j > maxJump) { maxJump = j; if (j > 0.12f) std::cout << "   saut " << j << " bloc " << (i / h.block) << " +" << k << std::endl; }
                    prev = L[(size_t) (i + k)];
                }
            else prev = L[(size_t) (i + h.block - 1)];
        }
        // sinus 110 Hz : la somme de quelques répétitions varie de < 0.06 par échantillon
        check (maxJump < 0.12f, "saut (clic) détecté : " + juce::String (maxJump));
        std::cout << "   saut max " << maxJump << std::endl;
    }

    // 7. État : sauvegarde / rechargement, taille de fenêtre comprise
    {
        std::cout << "7. Sauvegarde de l'état..." << std::endl;
        Harness a, b;
        set (a.proc, "feedback", 0.77f);
        set (a.proc, "mode", 2.0f);
        a.proc.editorScale = 1.3f;
        juce::MemoryBlock mb;
        a.proc.getStateInformation (mb);
        b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.proc.currentSettings().feedback - 0.77f) < 1.0e-3f, "feedback non restauré");
        check (b.proc.currentSettings().mode == 2, "mode non restauré");
        check (std::abs (b.proc.editorScale.load() - 1.3f) < 1.0e-3f, "taille de fenêtre non restaurée");
    }

    // 7b. Bypass : le sec ressort intact (latence comprise), sans clic, et RETOUR n'y touche pas
    {
        std::cout << "7b. Bypass..." << std::endl;
        Harness h;
        set (h.proc, "feedback", 0.8f);
        set (h.proc, "mix", 0.7f);
        const int n = (int) h.sr * 2;
        std::vector<float> L ((size_t) n), R, src;
        for (int i = 0; i < n; ++i) L[(size_t) i] = 0.3f * std::sin (2.0f * dsp::pi * 220.0f * (float) i / (float) h.sr);
        R = L; src = L;
        juce::MidiBuffer midi;
        float maxJump = 0, prev = 0;
        for (int i = 0; i < n; i += h.block)
        {
            if (i == h.block * 50) set (h.proc, "bypass", 1.0f);
            float* chans[2] { L.data() + i, R.data() + i };
            juce::AudioBuffer<float> b (chans, 2, h.block);
            h.proc.processBlock (b, midi);
            for (int k = 0; k < h.block; ++k) { if (i + k > 2000) maxJump = juce::jmax (maxJump, std::abs (L[(size_t) (i + k)] - prev)); prev = L[(size_t) (i + k)]; }
        }
        const int lat = h.proc.getLatencySamples();
        float err = 0;
        for (int i = n - 4800; i < n; ++i) err = juce::jmax (err, std::abs (L[(size_t) i] - src[(size_t) (i - lat)]));
        check (err < 1.0e-3f, "bypass : le sec n'est pas intact (" + juce::String (err) + ")");
        check (maxJump < 0.08f, "bypass : clic (" + juce::String (maxJump) + ")");
        juce::Random rng (2);
        h.proc.randomize (rng);
        h.proc.undoRandom();
        check (h.proc.apvts.getRawParameterValue ("bypass")->load() > 0.5f, "RETOUR a modifié le bypass");
    }

    // 8. CPU
    {
        Harness h;
        set (h.proc, "feedback", 0.9f);
        std::vector<float> L ((size_t) (h.sr * 10)), R;
        juce::Random rnd (5);
        for (auto& v : L) v = rnd.nextFloat() * 0.5f - 0.25f;
        R = L;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        h.process (L, R);
        auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "8. CPU : " << juce::String (ms / 100.0, 2) << " % d'un cœur" << std::endl;
    }

    std::cout << (failures == 0 ? "=== TOUS LES TESTS PASSENT ===" : "=== " + juce::String (failures).toStdString() + " ECHEC(S) ===") << std::endl;
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
int snapshotMain (const juce::String& path)
{
    Harness h;
    set (h.proc, "mode", 1.0f);
    set (h.proc, "feedback", 0.7f);
    set (h.proc, "note", 3.0f);
    set (h.proc, "feel", 1.0f);
    set (h.proc, "highcut", 6000.0f);
    set (h.proc, "offset", 4.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (h.proc.createEditor());
    ed->setSize (DelayEditor::baseW, DelayEditor::baseH);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::cout << "capture : " << f.getFullPathName() << std::endl;
    return 0;
}

// ---------------------------------------------------------------------------
// Rendu de WAV d'écoute : boucle sèche -> plusieurs réglages
// ---------------------------------------------------------------------------
namespace
{
bool loadWav (const juce::String& path, juce::AudioBuffer<float>& src, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (path)));
    if (reader == nullptr) { std::cout << "impossible de lire " << path << std::endl; return false; }
    src.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&src, 0, (int) reader->lengthInSamples, 0, true, true);
    sr = reader->sampleRate;
    return true;
}

/** Rend `src` à travers le delay réglé avec `values` (identifiant, valeur réelle), + 3 s de queue. */
void renderOne (const juce::AudioBuffer<float>& src, double sr, double bpm,
                const std::vector<std::pair<juce::String, float>>& values, const juce::File& f)
{
    Harness h;
    h.sr = sr;
    h.head.bpm = bpm > 40.0 ? bpm : 140.0;
    h.proc.prepareToPlay (h.sr, h.block);
    for (auto& [id, v] : values) set (h.proc, id, v);
    const int n = src.getNumSamples() + (int) (h.sr * 3.0);
    std::vector<float> L ((size_t) n, 0.0f), R = L;
    for (int i = 0; i < src.getNumSamples(); ++i)
    {
        L[(size_t) i] = src.getSample (0, i);
        R[(size_t) i] = src.getSample (juce::jmin (1, src.getNumChannels() - 1), i);
    }
    h.process (L, R);
    // on retire la latence du limiteur pour que le rendu soit calé sur la source
    const int lat = h.proc.getLatencySamples();
    juce::AudioBuffer<float> outBuf (2, n - lat);
    outBuf.copyFrom (0, 0, L.data() + lat, n - lat);
    outBuf.copyFrom (1, 0, R.data() + lat, n - lat);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (h.sr).withNumChannels (2).withBitsPerSample (24));
    if (w) w->writeFromAudioSampleBuffer (outBuf, 0, outBuf.getNumSamples());
    std::cout << "rendu : " << f.getFullPathName() << std::endl;
}

double bpmFromName (const juce::String& path)
{
    // « boucle_140bpm.wav » -> 140
    auto name = juce::File (path).getFileNameWithoutExtension().toLowerCase();
    auto i = name.indexOf ("bpm");
    if (i <= 0) return 0.0;
    int s = i;
    while (s > 0 && (juce::CharacterFunctions::isDigit (name[s - 1]) || name[s - 1] == '.')) --s;
    return name.substring (s, i).getDoubleValue();
}
} // namespace

/** --render-one source.wav sortie.wav id=valeur id=valeur ... */
int renderOneMain (int argc, char** argv)
{
    juce::AudioBuffer<float> src;
    double sr = 44100.0;
    if (! loadWav (argv[2], src, sr)) return 1;
    std::vector<std::pair<juce::String, float>> values;
    double bpm = bpmFromName (argv[2]);
    for (int a = 4; a < argc; ++a)
    {
        juce::String kv (argv[a]);
        auto key = kv.upToFirstOccurrenceOf ("=", false, false);
        auto val = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (key == "bpm") bpm = val; else values.push_back ({ key, val });
    }
    renderOne (src, sr, bpm, values, juce::File (argv[3]));
    return 0;
}

int renderMain (const juce::String& wavPath, const juce::String& dirPath)
{
    juce::AudioBuffer<float> src;
    double sr = 44100.0;
    if (! loadWav (wavPath, src, sr)) return 1;
    const double bpm = bpmFromName (wavPath);
    juce::File dir (dirPath);
    dir.createDirectory();

    struct Preset { const char* name; std::vector<std::pair<juce::String, float>> values; };
    const std::vector<Preset> presets {
        { "01_sec", { { "mix", 0.0f } } },
        { "02_1-16_pingpong_fb60", { { "note", 3 }, { "mode", 1 }, { "feedback", 0.6f }, { "mix", 0.5f } } },
        { "03_1-32_stereo_fb85_filtre", { { "note", 2 }, { "feedback", 0.85f }, { "lowcut", 400 }, { "highcut", 4000 }, { "mix", 0.6f } } },
        { "04_1-8D_pingpong_fb50", { { "note", 4 }, { "feel", 1 }, { "mode", 1 }, { "feedback", 0.5f }, { "mix", 0.45f } } },
        { "05_1-16T_mono_fb70", { { "note", 3 }, { "feel", 2 }, { "mode", 2 }, { "feedback", 0.7f }, { "mix", 0.5f } } },
        { "06_free_3ms_peigne_fb90", { { "sync", 0 }, { "time", 3.0f }, { "feedback", 0.9f }, { "mix", 0.6f } } },
        { "07_free_25ms_offset_fb75", { { "sync", 0 }, { "time", 25.0f }, { "offset", 6.0f }, { "feedback", 0.75f }, { "mix", 0.5f } } },
        { "08_1-64_fb110_emballement", { { "note", 1 }, { "feedback", 1.1f }, { "highcut", 3000 }, { "mix", 0.5f } } },
    };
    for (const auto& pr : presets)
        renderOne (src, sr, bpm, pr.values, dir.getChildFile (juce::String (pr.name) + ".wav"));
    return 0;
}
