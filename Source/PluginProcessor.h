#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SatCurves.h"

class VegaSaturatorProcessor : public juce::AudioProcessor
{
public:
    VegaSaturatorProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Saturator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> inL { 0 }, inR { 0 }, outL { 0 }, outR { 0 }, bandLevel[3] {};
    std::atomic<float> uiScale { 1.0f };

private:
    void rebuildOversampling (int factorIdx, int blockSize);
    int osIdx = -1, maxBlock = 512; double sr = 48000;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::dsp::LinkwitzRileyFilter<float> lo1, hi1, lo2, hi2, ap;      // 3-band split at oversampled rate
    juce::AudioBuffer<float> dryBuf;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1024 };
    float hold[3][2] {}; int holdCnt[3][2] {};
    float dcX[2] {}, dcY[2] {};
    juce::dsp::IIR::Filter<float> tiltLo[2], tiltHi[2]; float lastTone = 1e9f; double osRate = 48000;
    juce::SmoothedValue<float> mixS, outS;
    std::atomic<float>* p[20] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaSaturatorProcessor)
};
