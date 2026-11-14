#pragma once
#include "Knob.h"
#include "SeqEditor.h"
#include "Visuals.h"

namespace idg::ui
{
/** Panneau central : l'effet sélectionné (en-tête, potards, séquenceur), oscilloscope corrompu en fond. */
class EffectPanel : public juce::Component
{
public:
    explicit EffectPanel (EditorContext&);
    ~EffectPanel() override;
    void setEffect (int fx);
    void refresh() { updateHeader(); seq.refresh(); repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    CorruptScope scope;

private:
    struct ScratchPad;
    void rebuild();
    void updateHeader();

    EditorContext& ctx;
    int fx = 0;
    juce::OwnedArray<juce::Component> paramComps;
    std::unique_ptr<Knob> mixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, parAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    juce::TextButton onBtn { "ON" }, holdBtn { "HOLD" }, soloBtn { "SOLO" }, parBtn { IDG_T ("PARALLÈLE") },
        rndBtn { "RANDOM" }, lockBtn { "LOCK" }, initBtn { "INIT" }, copyBtn { "COPIER" }, pasteBtn { "COLLER" }, transBtn { "TRANS" };
    juce::ComboBox modeBox;
    std::unique_ptr<juce::Component> scratchPad;
    SeqEditor seq;
    juce::Rectangle<int> header, body;
    juce::String warning;
};
} // namespace idg::ui
