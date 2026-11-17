#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaSaturatorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaSaturatorEditor (VegaSaturatorProcessor&);
    ~VegaSaturatorEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 1000, H = 560;
    class Main;
private:
    void timerCallback() override;
    VegaSaturatorProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
