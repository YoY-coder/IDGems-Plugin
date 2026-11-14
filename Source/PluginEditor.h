#pragma once
#include "PluginProcessor.h"

class IDGemsEditor : public juce::AudioProcessorEditor
{
public:
    explicit IDGemsEditor (IDGemsProcessor& p) : AudioProcessorEditor (p) { setSize (400, 300); }
};
