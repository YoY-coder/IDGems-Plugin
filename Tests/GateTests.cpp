// Tests d'IDGems Gate : motif, synchro tempo, clics, NaN, état. Options :
//   --snapshot fichier.png [0|1]        capture de l'interface (0 = mode PAS, 1 = mode COURBE)
//   --render dossier [boucle.wav] [bpm]  rend des WAV (sec + plusieurs réglages) pour écoute
#include "Plugins/Gate/GateProcessor.h"
#include "Plugins/Gate/GateEditor.h"

using namespace idg::gate;

namespace
{
struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

int failures = 0;
void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::cout << "  [ECHEC] " << what << std::endl; }
    else std::cout << "  ok : " << what << std::endl;
}

void setParam (GateProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

struct Harness
{
    GateProcessor proc;
    FakePlayHead head;
    double sr = 48000.0;
    int block = 480;
    juce::MidiBuffer midi;

    Harness() { proc.setPlayHead (&head); proc.prepareToPlay (sr, block); }

    /** Traite un buffer complet par blocs, en avançant la position du morceau. */
    void process (juce::AudioBuffer<float>& buf)
    {
        for (int s = 0; s < buf.getNumSamples(); s += block)
        {
            const int n = juce::jmin (block, buf.getNumSamples() - s);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, s, n);
            proc.processBlock (view, midi);
            midi.clear();
            if (head.playing) head.ppq += n / sr * head.bpm / 60.0;
        }
    }
};

juce::AudioBuffer<float> constant (int n, float v = 0.5f)
{
    juce::AudioBuffer<float> b (2, n);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (b.getWritePointer (c), v, n);
    return b;
}

/** Boucle de test synthétique : kick + basse sciée + hat, 4 temps. */
juce::AudioBuffer<float> synthLoop (double sr, double bpm, double bars)
{
    const int n = (int) (sr * 60.0 / bpm * 4.0 * bars);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rnd (7);
    double phase = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double beat = std::fmod (t * bpm / 60.0, 1.0);
        const double kick = std::exp (-beat * 25.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 140.0 * std::exp (-beat * 35.0)) * t);
        phase += 55.0 / sr;
        const double saw = 2.0 * (phase - std::floor (phase)) - 1.0;
        const double hat = std::fmod (beat, 0.5) < 0.02 ? (rnd.nextFloat() * 2.0 - 1.0) * 0.25 : 0.0;
        const float x = (float) (0.55 * kick + 0.3 * saw + hat);
        b.setSample (0, i, x);
        b.setSample (1, i, x);
    }
    return b;
}
} // namespace

int snapshotMain (const juce::String& path, int mode)
{
    Harness h;
    h.proc.editMode = mode;
    if (mode == 1) h.proc.setPattern (GatePattern::preset (14));
    auto loop = synthLoop (h.sr, h.head.bpm, 1.0);
    h.process (loop);
    std::unique_ptr<juce::AudioProcessorEditor> ed (h.proc.createEditor());
    ed->setSize (GateEditor::baseW, GateEditor::baseH);
    if (auto* ge = dynamic_cast<GateEditor*> (ed.get())) for (int i = 0; i < 20; ++i) ge->debugTick();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "capture : " << f.getFullPathName() << std::endl;
    return 0;
}

int renderMain (const juce::String& outDir, const juce::String& inPath, double bpm)
{
    juce::File dir (outDir);
    dir.createDirectory();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    double sr = 48000.0;
    juce::AudioBuffer<float> dry;
    if (inPath.isNotEmpty())
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (inPath)));
        if (r == nullptr) { std::cout << "lecture impossible : " << inPath << std::endl; return 1; }
        sr = r->sampleRate;
        dry.setSize (2, (int) r->lengthInSamples);
        r->read (&dry, 0, (int) r->lengthInSamples, 0, true, true);
    }
    else dry = synthLoop (sr, bpm, 4.0);

    auto write = [&] (const juce::String& name, const juce::AudioBuffer<float>& b)
    {
        auto f = dir.getChildFile (name + ".wav");
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        auto w = juce::WavAudioFormat().createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                 .withNumChannels (2).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        std::cout << "  " << f.getFileName() << std::endl;
    };
    write ("00_sec", dry);

    struct Variant { const char* name; int preset; float attack, release; int length; int grid; float swing; };
    const Variant vars[] {
        { "01_trance1_tendu", 5, 0.3f, 1.5f, 10, 6, 0.0f },
        { "02_trance1_doux", 5, 8.0f, 40.0f, 10, 6, 0.0f },
        { "03_doubles_swing", 2, 0.3f, 2.0f, 10, 8, 50.0f },
        { "04_stutter_roll", 12, 0.2f, 1.0f, 10, 10, 0.0f },
        { "05_sidechain", 14, 0.5f, 3.0f, 10, 6, 0.0f },
        { "06_rampes_tendues", 16, 0.3f, 1.0f, 10, 6, 0.0f },
        { "07_glitch_2mesures", 17, 0.2f, 1.0f, 11, 8, 0.0f },
    };
    for (const auto& v : vars)
    {
        Harness h;
        h.sr = sr;
        h.head.bpm = bpm;
        h.proc.prepareToPlay (sr, h.block);
        h.proc.setPattern (GatePattern::preset (v.preset));
        setParam (h.proc, "attack", v.attack);
        setParam (h.proc, "release", v.release);
        setParam (h.proc, "length", (float) v.length);
        setParam (h.proc, "grid", (float) v.grid);
        setParam (h.proc, "swing", v.swing);
        auto b = dry;
        h.process (b);
        write (v.name, b);
    }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return snapshotMain (argv[2], argc >= 4 ? juce::String (argv[3]).getIntValue() : 0);
    if (argc >= 3 && juce::String (argv[1]) == "--render")
        return renderMain (argv[2], argc >= 4 ? juce::String (argv[3]) : juce::String(), argc >= 5 ? juce::String (argv[4]).getDoubleValue() : 140.0);

    std::cout << "=== Tests IDGems Gate ===" << std::endl;

    // 1. Motif : lecture, fronts verticaux, sauvegarde texte
    {
        std::cout << "1. Motif" << std::endl;
        auto p = GatePattern::fromBits ("x-x-");
        check (p.valueAt (0.1f) > 0.99f && p.valueAt (0.3f) < 0.01f && p.valueAt (0.6f) > 0.99f, "marches x-x- lues correctement");
        auto q = GatePattern::fromString (p.toString());
        bool same = q.n == p.n;
        for (float x = 0; x < 1.0f; x += 0.01f) same = same && std::abs (q.valueAt (x) - p.valueAt (x)) < 1.0e-3f;
        check (same, "aller-retour texte identique");
        p.setRange (0.25f, 0.5f, 1.0f);
        check (p.valueAt (0.3f) > 0.99f && p.valueAt (0.6f) > 0.99f && p.valueAt (0.8f) < 0.01f, "mode PAS : allume une case sans toucher les autres");
        auto s = GatePattern::fromBits ("x---");
        s.shift (0.25f);
        check (s.valueAt (0.1f) < 0.01f && s.valueAt (0.3f) > 0.99f && s.valueAt (0.6f) < 0.01f, "décalage d'un pas vers la droite");
        s.invert();
        check (s.valueAt (0.1f) > 0.99f && s.valueAt (0.3f) < 0.01f, "inversion");
        GatePattern c;
        c.fromVector ({ { 0, 1, 0.8f }, { 0.5f, 0, 0 }, { 1, 0, 0 } });
        check (c.valueAt (0.25f) > 0.75f, "courbure positive : la chute reste haute puis tombe vite (" + juce::String (c.valueAt (0.25f)) + ")");
        for (int i = 0; i < GatePattern::presetNames().size(); ++i)
        {
            auto pr = GatePattern::preset (i);
            bool ok = pr.n >= 2 && pr.pts[0].x == 0.0f && pr.pts[pr.n - 1].x == 1.0f;
            for (int k = 1; k < pr.n; ++k) ok = ok && pr.pts[k].x >= pr.pts[k - 1].x;
            if (! ok) check (false, "motif prédéfini invalide : " + GatePattern::presetNames()[i]);
        }
    }

    // 2. Synchro tempo : 1 mesure, croches x-x-... -> 8 fenêtres ouvertes / 8 fermées
    {
        std::cout << "2. Synchro tempo" << std::endl;
        Harness h;
        h.proc.setPattern (GatePattern::preset (1)); // croches (16 pas, un sur deux)
        h.proc.prepareToPlay (h.sr, h.block);
        auto b = constant ((int) (h.sr * 2.0)); // 1 mesure à 120 bpm
        h.process (b);
        const double stepLen = h.sr * 2.0 / 16.0;
        auto at = [&] (double step, double frac) { return b.getSample (0, (int) ((step + frac) * stepLen)); };
        check (at (0, 0.5) > 0.49f && at (1, 0.5) < 0.001f && at (2, 0.5) > 0.49f && at (15, 0.5) < 0.001f,
               "pas ouverts / fermés aux bons endroits");
        // grille de 16 en mode Hz : 2 cycles par seconde
        Harness hz;
        setParam (hz.proc, "mode", 1.0f);
        setParam (hz.proc, "hz", 2.0f);
        hz.proc.setPattern (GatePattern::fromBits ("x-"));
        hz.head.playing = false;
        auto c = constant ((int) hz.sr);
        hz.process (c);
        check (c.getSample (0, (int) (hz.sr * 0.1)) > 0.49f && c.getSample (0, (int) (hz.sr * 0.35)) < 0.001f
                   && c.getSample (0, (int) (hz.sr * 0.6)) > 0.49f, "mode Hz (2 Hz) sans transport");
    }

    // 3. Note MIDI : relance le motif au début
    {
        std::cout << "3. Redemarrage MIDI" << std::endl;
        Harness h;
        setParam (h.proc, "retrig", 1.0f);
        h.proc.setPattern (GatePattern::fromBits ("x---"));
        auto b = constant (h.block * 30);
        h.process (b); // laisse tourner
        auto c = constant (h.block);
        h.midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        h.proc.processBlock (c, h.midi);
        h.midi.clear();
        check (c.getSample (0, h.block - 1) > 0.49f, "note MIDI -> début du motif (ouvert)");
    }

    // 4. Clics : sinus à travers des marches, attaque / relâche par défaut
    {
        std::cout << "4. Clics" << std::endl;
        Harness h;
        h.proc.setPattern (GatePattern::preset (2));
        juce::AudioBuffer<float> b (2, (int) h.sr * 2);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = 0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / h.sr);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        h.process (b);
        float maxJump = 0.0f;
        const float sineStep = 0.8f * (float) (2.0 * juce::MathConstants<double>::pi * 60.0 / h.sr);
        for (int i = 1; i < b.getNumSamples(); ++i) maxJump = juce::jmax (maxJump, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
        check (maxJump < 0.8f / (0.5f * 0.001f * (float) h.sr) + sineStep * 1.5f + 0.01f,
               "saut max entre deux échantillons : " + juce::String (maxJump, 4));
    }

    // 5. NaN, plafond, réglages extrêmes
    {
        std::cout << "5. Robustesse" << std::endl;
        Harness h;
        juce::Random r (3);
        bool ok = true;
        float peak = 0.0f;
        for (int k = 0; k < 40; ++k)
        {
            for (auto* prm : h.proc.getParameters()) prm->setValueNotifyingHost (r.nextFloat());
            h.proc.setPattern (GatePattern::preset (k % GatePattern::presetNames().size()));
            auto b = constant (h.block * 8, 0.9f);
            h.process (b);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = b.getSample (0, i);
                ok = ok && std::isfinite (v);
                peak = juce::jmax (peak, std::abs (v));
            }
        }
        check (ok, "aucun NaN");
        check (peak <= 0.9f + 1.0e-5f, "le gate n'amplifie jamais (crête " + juce::String (peak) + ")");
    }

    // 6. État : motif + paramètres sauvegardés avec le projet
    {
        std::cout << "6. Sauvegarde" << std::endl;
        Harness a;
        a.proc.setPattern (GatePattern::preset (9));
        setParam (a.proc, "swing", 40.0f);
        a.proc.editorScale = 1.3f;
        juce::MemoryBlock mb;
        a.proc.getStateInformation (mb);
        Harness b;
        b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
        check (b.proc.getPattern().toString() == a.proc.getPattern().toString(), "motif restauré");
        check (std::abs (b.proc.apvts.getRawParameterValue ("swing")->load() - 40.0f) < 0.01f, "paramètres restaurés");
        check (std::abs (b.proc.editorScale.load() - 1.3f) < 0.001f, "taille de fenêtre restaurée");
    }

    // 7. CPU
    {
        std::cout << "7. CPU" << std::endl;
        Harness h;
        h.proc.setPattern (GatePattern::preset (17));
        auto b = synthLoop (h.sr, 120.0, 8.0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        h.process (b);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double audioMs = b.getNumSamples() / h.sr * 1000.0;
        check (ms < audioMs * 0.05, juce::String (ms, 2) + " ms pour " + juce::String (audioMs, 0) + " ms d'audio");
    }

    std::cout << (failures == 0 ? "=== TOUT EST OK ===" : "=== " + std::to_string (failures) + " ECHEC(S) ===") << std::endl;
    return failures == 0 ? 0 : 1;
}
