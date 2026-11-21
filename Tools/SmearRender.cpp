// Rendu hors ligne de l'effet Smear : écrit le sec + un WAV par réglage, pour écoute.
// Usage : IDGemsSmearRender <dossier_sortie> <boucle_seche.wav>
//         IDGemsSmearRender --check                (contrôles : NaN, latence, plafond)
//         IDGemsSmearRender --snapshot image.png   (capture de l'interface)
#include "Core/JuceIncludes.h"
#include "Effects/SmearEngine.h"
#include "SmearProcessor.h"
#include "SmearEditor.h"

namespace
{
bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Stats { float peak = 0, jump = 0; bool nan = false; };

/** Traite la boucle en compensant la latence (le WAV traité est calé sur le sec). */
juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& dry, double sr, const idg::SmearEngine::Settings& s, Stats& st)
{
    idg::SmearEngine e;
    e.prepare (sr);
    const int lat = e.getLatency(), n = dry.getNumSamples(), total = n + lat;
    juce::AudioBuffer<float> b (2, total);
    b.clear();
    for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, dry, juce::jmin (c, dry.getNumChannels() - 1), 0, n);
    for (int pos = 0; pos < total; pos += 256)
    {
        const int m = juce::jmin (256, total - pos);
        e.process (b.getWritePointer (0, pos), b.getWritePointer (1, pos), m, s);
    }
    juce::AudioBuffer<float> out (2, n);
    for (int c = 0; c < 2; ++c)
    {
        out.copyFrom (c, 0, b, c, lat, n);
        auto* d = out.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (d[i])) st.nan = true;
            st.peak = juce::jmax (st.peak, std::abs (d[i]));
            if (i) st.jump = juce::jmax (st.jump, std::abs (d[i] - d[i - 1]));
        }
    }
    return out;
}

int check()
{
    bool ok = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // MIX à 0 : la sortie doit être l'entrée retardée exactement de la latence déclarée
        idg::SmearEngine e;
        e.prepare (sr);
        const int lat = e.getLatency(), n = lat * 3;
        std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
        L[100] = 0.5f; R[100] = -0.5f;
        idg::SmearEngine::Settings s;
        s.mix = 0.0f;
        e.process (L.data(), R.data(), n, s);
        const bool latOk = std::abs (L[(size_t) (100 + lat)] - 0.5f) < 1e-5f && std::abs (R[(size_t) (100 + lat)] + 0.5f) < 1e-5f;
        std::printf ("sr %.0f : latence %d %s\n", sr, lat, latOk ? "OK" : "FAUSSE !");
        ok = ok && latOk;

        // bruit fort + réglages extrêmes : pas de NaN, plafond tenu
        juce::Random rnd (7);
        juce::AudioBuffer<float> noise (2, (int) sr * 4);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (c, i, (rnd.nextFloat() * 2 - 1) * 2.0f);
        for (float focus : { 0.0f, 1.0f })
            for (float smear : { 0.02f, 20.0f })
            {
                idg::SmearEngine::Settings x;
                x.focus = focus; x.smearSec = smear; x.drift = 1.0f; x.tone = 1.0f; x.outDb = 12.0f;
                Stats st;
                render (noise, sr, x, st);
                const bool good = ! st.nan && st.peak <= 0.97f;
                std::printf ("   bruit focus %.0f smear %.2f : crete %.3f %s\n", focus, smear, st.peak, good ? "OK" : "ECHEC");
                ok = ok && good;
            }
    }
    return ok ? 0 : 1;
}

struct Preset { const char* file; const char* label; idg::SmearEngine::Settings s; };

idg::SmearEngine::Settings mk (float smear, float rise, float focus, float drift, float tone = 0.0f)
{
    idg::SmearEngine::Settings s;
    s.smearSec = smear; s.rise = rise; s.focus = focus; s.drift = drift; s.tone = tone;
    return s;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: %s <dossier_sortie> <boucle.wav> | --check | --snapshot image.png\n", argv[0]); return 1; }
    const juce::String cmd (argv[1]);
    if (cmd == "--check") return check();
    if (cmd == "--snapshot" && argc > 2)
    {
        idg::SmearProcessor proc;
        proc.prepareToPlay (44100.0, 512);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        // signal de test : notes de basse hachées, pour voir la coulée sur le sonogramme
        double ph = 0;
        for (int k = 0; k < 600; ++k)
        {
            for (int i = 0; i < 512; ++i)
            {
                const int t = k * 512 + i;
                const double f = (t / 22050) % 2 ? 49.0 : 65.4;
                ph += f / 44100.0;
                const float gate = ((t / 5512) % 2) ? 1.0f : 0.0f;
                const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * ph) + 0.3 * std::sin (juce::MathConstants<double>::twoPi * ph * 3.0)
                                         + 0.15 * std::sin (juce::MathConstants<double>::twoPi * ph * 7.0)) * 0.4f * gate;
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            proc.processBlock (b, m);
            if (k % 2 == 0)
                if (auto* e = dynamic_cast<idg::SmearEditor*> (ed.get())) e->tick();
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
    if (argc < 3) { std::printf ("il faut une boucle seche\n"); return 1; }

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.createDirectory();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])));
    if (rd == nullptr) { std::printf ("boucle illisible\n"); return 1; }
    const double sr = rd->sampleRate;
    juce::AudioBuffer<float> dry (2, (int) rd->lengthInSamples);
    rd->read (&dry, 0, (int) rd->lengthInSamples, 0, true, true);
    writeWav (out.getChildFile ("00_sec.wav"), dry, sr);

    const Preset presets[] {
        { "01_defaut_2s.wav",           "défaut : 2 s, montée 100 %, focus 60 %, drift 25 %", mk (2.0f, 1.0f, 0.6f, 0.25f) },
        { "02_court_300ms.wav",         "300 ms (lissé, garde un peu de mouvement)",          mk (0.3f, 1.0f, 0.6f, 0.25f) },
        { "03_long_8s.wav",             "8 s (nappe quasi figée)",                             mk (8.0f, 1.0f, 0.6f, 0.25f) },
        { "04_focus0.wav",              "2 s, focus 0 % (tout le spectre, plus granuleux)",    mk (2.0f, 1.0f, 0.0f, 0.25f) },
        { "05_focus100.wav",            "2 s, focus 100 % (harmoniques pures)",                mk (2.0f, 1.0f, 1.0f, 0.25f) },
        { "06_montee_rapide.wav",       "2 s, montée 5 % (notes nettes, queue diluée)",        mk (2.0f, 0.05f, 0.6f, 0.25f) },
        { "07_drift0.wav",              "2 s, drift 0 % (statique, mono)",                     mk (2.0f, 1.0f, 0.6f, 0.0f) },
        { "08_drift100.wav",            "2 s, drift 100 % (mouvant, large)",                   mk (2.0f, 1.0f, 0.6f, 1.0f) },
    };

    bool ok = true;
    for (auto& p : presets)
    {
        Stats st;
        auto b = render (dry, sr, p.s, st);
        ok = ok && ! st.nan;
        std::printf ("%-24s %-55s crete %.2f  saut max %.3f %s\n", p.file, p.label, st.peak, st.jump, st.nan ? "NaN !" : "");
        writeWav (out.getChildFile (p.file), b, sr);
    }
    return ok ? 0 : 1;
}
