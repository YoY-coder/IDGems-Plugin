#pragma once
#include "EffectBase.h"
#include "GlitchDelay.h"

namespace idg
{
/** Delay du rack = module solo 01 (GlitchDelay), + mode throw propre au rack.
    Le dosage sec / traité est fait par le rack : ici on ne rend que le signal traité. */
class Delay : public EffectModule
{
public:
    enum { Time, Sync, Note, Feel, Fb, Mode, Offset, LowCut, HighCut, Throw };
    void prepare (double sr, int) override { dl.prepare (sr); sampleRate = sr; reset(); }
    void reset() override { dl.reset(); sendGain = 0.0f; }
    bool throwMode() const override { return B (Throw); }

    void process (float* L, float* R, int n, const ProcCtx& ctx) override
    {
        GlitchDelay::Settings s;
        s.sync = B (Sync);
        s.note = I (Note);
        s.feel = I (Feel);
        s.timeMs = P (Time);
        s.feedback = P (Fb);
        s.mode = I (Mode);
        s.offsetMs = P (Offset);
        s.lowCut = P (LowCut);
        s.highCut = juce::jmax (s.lowCut, P (HighCut));
        s.mix = 1.0f;
        s.bpm = ctx.bpm;
        // throw : seule la porte (pad / séquenceur / MIDI) envoie dans la boucle, la queue reste
        const float sendTarget = (! B (Throw) || ctx.gate) ? 1.0f : 0.0f;
        const float step = 1.0f / (float) (0.004 * sampleRate);
        for (int i = 0; i < n; ++i)
        {
            sendGain += juce::jlimit (-step, step, sendTarget - sendGain);
            L[i] *= sendGain;
            R[i] *= sendGain;
        }
        dl.process (L, R, n, s);
    }

private:
    GlitchDelay dl;
    double sampleRate = 44100.0;
    float sendGain = 0.0f;
};
} // namespace idg
