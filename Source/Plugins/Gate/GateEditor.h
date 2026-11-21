#pragma once
#include "GateProcessor.h"
#include "../../UI/LookAndFeel.h"

namespace idg::gate
{
/** Potard néon (arc plein qui s'illumine avec la valeur), lié à un paramètre. */
class NeonKnob : public juce::Slider
{
public:
    NeonKnob (GateProcessor&, const juce::String& paramId, const juce::String& label, const juce::String& help);
    void paint (juce::Graphics&) override;
    juce::Colour colour = ui::Theme::accent;

private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

/** Grand graphe du motif : mode PAS (on/off façon Transgate) et mode COURBE (points + courbures façon ShaperBox). */
class GateGraph : public juce::Component, private juce::Timer
{
public:
    explicit GateGraph (GateProcessor&);
    ~GateGraph() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverCell = hoverPt = hoverHandle = -1; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // outils
    void applyPreset (int idx);
    void invert();
    void shiftSteps (int dir);
    void randomise();
    void clearAll (bool open);
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    std::function<void()> onHistoryChanged;
    void tick() { timerCallback(); }

private:
    void timerCallback() override;
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10.0f, 8.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float>) const;
    float snapX (float x, bool free) const;
    int hitPoint (juce::Point<float>) const;
    int hitHandle (juce::Point<float>) const;
    juce::Point<float> handlePos (int seg) const;
    void pushUndo();
    void publish();
    void paintCell (int cell, float value);
    bool stepsMode() const { return proc.editMode.load() == 0; }

    GateProcessor& proc;
    GatePattern pat;
    int seenVersion = -1;
    std::vector<juce::String> undoStack, redoStack;

    int hoverCell = -1, hoverPt = -1, hoverHandle = -1;
    int dragPt = -1, dragHandle = -1, lastCell = -1;
    float paintValue = 1.0f, handleStart = 0.0f;
    bool levelPaint = false;
    float shownPhase = 0.0f;
    std::array<float, GateProcessor::numBins> inView {}, outView {};
};

class GateEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GateEditor (GateProcessor&);
    ~GateEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int baseW = 960, baseH = 600;
    void debugTick() { graph.tick(); timerCallback(); }

private:
    struct Content : public juce::Component
    {
        explicit Content (GateEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        GateEditor& ed;
        juce::Image bg;
    };

    void timerCallback() override;
    void updateModeButtons();
    juce::TextButton& addButton (const juce::String& text, const juce::String& tip, std::function<void()> fn);
    juce::ComboBox& addCombo (const juce::String& paramId, const juce::String& tip);

    GateProcessor& proc;
    ui::IDGLookAndFeel lnf;
    Content content { *this };
    GateGraph graph;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::TextButton *stepsBtn = nullptr, *curveBtn = nullptr, *snapBtn = nullptr, *undoBtn = nullptr, *redoBtn = nullptr;
    juce::ComboBox presetBox;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtts;
    juce::ComboBox *modeBox = nullptr, *lengthBox = nullptr, *gridBox = nullptr, *retrigBox = nullptr;
    NeonKnob hzKnob, swingKnob, offsetKnob, attackKnob, releaseKnob, depthKnob;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    bool sizeReady = false;

    friend struct Content;
};
} // namespace idg::gate
