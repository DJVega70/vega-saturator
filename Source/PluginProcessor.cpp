#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kMode, kType, kDrive, kBias, kTone, kLowDrive, kMidDrive, kHighDrive, kXLow, kXHigh, kCrush, kOS, kAuto, kMix, kOut, kLowOn, kMidOn, kHighOn };
const char* ids[] = { "mode", "type", "drive", "bias", "tone", "lowDrive", "midDrive", "highDrive", "xLow", "xHigh", "crush", "oversampling", "autoGain", "mix", "output", "lowOn", "midOn", "highOn" };
constexpr int numP = 18;
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaSaturatorProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return String (roundToInt (x)) + "%"; };
    auto hz = [] (float x, int) { return x >= 1000 ? String (x / 1000, 1) + " kHz" : String (roundToInt (x)) + " Hz"; };
    auto db = [] (float x, int) { return String (x, 1) + " dB"; };
    auto F = [&] (int id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (f))); };
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kMode], 1 }, "Mode", StringArray { "Single Band", "Multiband" }, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kType], 1 }, "Type", vsat::typeNames(), 0));
    F (kDrive, "Drive", NormalisableRange<float> (0, 100), 30, [] (float x, int) { return String (x * 0.36f, 1) + " dB"; });
    F (kBias, "Asymmetry", NormalisableRange<float> (-100, 100), 0, pct);
    F (kTone, "Tone", NormalisableRange<float> (-100, 100), 0, [] (float x, int) { return x == 0 ? String ("Flat") : (x < 0 ? "Dark " : "Bright ") + String (roundToInt (std::abs (x))); });
    F (kLowDrive, "Low Drive", NormalisableRange<float> (0, 200), 100, pct);
    F (kMidDrive, "Mid Drive", NormalisableRange<float> (0, 200), 100, pct);
    F (kHighDrive, "High Drive", NormalisableRange<float> (0, 200), 60, pct);
    F (kXLow, "Low / Mid", NormalisableRange<float> (40, 1000, 0, 0.4f), 180, hz);
    F (kXHigh, "Mid / High", NormalisableRange<float> (1000, 12000, 0, 0.4f), 3500, hz);
    F (kCrush, "Crush", NormalisableRange<float> (0, 100), 40, pct);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kOS], 1 }, "Oversampling", StringArray { "Off", "2x", "4x", "8x" }, 2));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kAuto], 1 }, "Auto Gain", true));
    F (kMix, "Mix", NormalisableRange<float> (0, 100), 100, pct);
    F (kOut, "Output", NormalisableRange<float> (-24, 12), 0, db);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kLowOn], 1 }, "Low On", true));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kMidOn], 1 }, "Mid On", true));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kHighOn], 1 }, "High On", true));
    return { v.begin(), v.end() };
}

VegaSaturatorProcessor::VegaSaturatorProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

bool VegaSaturatorProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return l.getMainInputChannelSet() == o && (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono());
}

void VegaSaturatorProcessor::rebuildOversampling (int idx, int block)
{
    osIdx = idx;
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) idx, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    if (idx > 0) os->initProcessing ((size_t) block);
    osRate = sr * (1 << idx);
    juce::dsp::ProcessSpec spec { osRate, (juce::uint32) (block << idx), 2 };
    for (auto* f : { &lo1, &hi1, &lo2, &hi2, &ap }) { f->prepare (spec); f->reset(); }
    lo1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); hi1.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lo2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); hi2.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    ap.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    const int lat = idx > 0 ? (int) std::round (os->getLatencyInSamples()) : 0;
    setLatencySamples (lat);
    dryDelay.setDelay ((float) lat);
    lastTone = 1e9f;
}

void VegaSaturatorProcessor::prepareToPlay (double s, int block)
{
    sr = s; maxBlock = block;
    dryDelay.prepare ({ s, (juce::uint32) block, 2 }); dryDelay.reset();
    dryBuf.setSize (2, block);
    rebuildOversampling ((int) p[kOS]->load(), block);
    mixS.reset (s, 0.03); outS.reset (s, 0.03);
    mixS.setCurrentAndTargetValue (p[kMix]->load() * 0.01f); outS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p[kOut]->load()));
    for (auto& a : hold) for (auto& v : a) v = 0;
    for (auto& a : holdCnt) for (auto& v : a) v = 0;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0;
}

void VegaSaturatorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), nch = std::min (2, buffer.getNumChannels());
    if (n == 0 || nch == 0) return;
    if ((int) p[kOS]->load() != osIdx || n > maxBlock) { maxBlock = std::max (maxBlock, n); dryBuf.setSize (2, maxBlock); rebuildOversampling ((int) p[kOS]->load(), maxBlock); }

    float pin[2] {};
    for (int c = 0; c < nch; ++c) pin[c] = buffer.getMagnitude (c, 0, n);

    // latency-aligned dry copy for the mix
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < n; ++i) { dryDelay.pushSample (c, buffer.getSample (c, i)); dryBuf.setSample (c, i, dryDelay.popSample (c)); }

    const int type = (int) p[kType]->load();
    const bool multi = p[kMode]->load() > 0.5f, autoG = p[kAuto]->load() > 0.5f;
    const float drive = p[kDrive]->load() * 0.01f, bias = p[kBias]->load() * 0.004f;
    const float bandDrive[3] { p[kLowDrive]->load() * 0.01f, p[kMidDrive]->load() * 0.01f, p[kHighDrive]->load() * 0.01f };
    const bool bandOn[3] { p[kLowOn]->load() > 0.5f, p[kMidOn]->load() > 0.5f, p[kHighOn]->load() > 0.5f };
    const float crush = p[kCrush]->load() * 0.01f;

    float g[3], comp[3];
    for (int b = 0; b < 3; ++b)
    {
        const float d = juce::jlimit (0.0f, 1.0f, drive * (multi ? bandDrive[b] : 1.0f));
        g[b] = vsat::driveToGain (d);
        comp[b] = autoG ? vsat::autoGain (type, g[b], bias) : 1.0f;
    }
    const int bits = juce::roundToInt (16.0f - crush * 13.0f);
    const float q = 1.0f / (float) (1 << (bits - 1));
    const int holdLen = 1 + juce::roundToInt (crush * crush * 24.0f) * (1 << osIdx);

    auto sat = [&] (int b, int c, float x)
    {
        if (type == 6)
        {   // bitcrush: drive into a clipper, then reduce bits and sample rate
            float y = juce::jlimit (-1.0f, 1.0f, x * g[b]);
            y = std::round (y / q) * q;
            if (--holdCnt[b][c] <= 0) { hold[b][c] = y; holdCnt[b][c] = holdLen; }
            return hold[b][c] * comp[b];
        }
        return vsat::shape (type, x * g[b], bias) * comp[b];
    };

    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) nch, (size_t) n);
    auto up = osIdx > 0 ? os->processSamplesUp (block) : block;
    const int un = (int) up.getNumSamples();
    if (multi) { lo1.setCutoffFrequency (p[kXLow]->load()); hi1.setCutoffFrequency (p[kXLow]->load()); lo2.setCutoffFrequency (p[kXHigh]->load()); hi2.setCutoffFrequency (p[kXHigh]->load()); ap.setCutoffFrequency (p[kXHigh]->load()); }

    float bl[3] {};
    for (int c = 0; c < nch; ++c)
    {
        float* d = up.getChannelPointer ((size_t) c);
        for (int i = 0; i < un; ++i)
        {
            const float x = d[i];
            if (! multi) { d[i] = sat (1, c, x); continue; }
            float low = lo1.processSample (c, x), rest = hi1.processSample (c, x);
            low = ap.processSample (c, low);                         // keep the low band phase-aligned with mid+high
            float mid = lo2.processSample (c, rest), high = hi2.processSample (c, rest);
            bl[0] = std::max (bl[0], std::abs (low)); bl[1] = std::max (bl[1], std::abs (mid)); bl[2] = std::max (bl[2], std::abs (high));
            d[i] = (bandOn[0] ? sat (0, c, low) : low) + (bandOn[1] ? sat (1, c, mid) : mid) + (bandOn[2] ? sat (2, c, high) : high);
        }
    }
    for (int b = 0; b < 3; ++b) bandLevel[b].store (std::max (bandLevel[b].load(), bl[b]));
    if (osIdx > 0) os->processSamplesDown (block);

    // DC blocker + tone tilt
    const float tone = p[kTone]->load() * 0.01f;
    if (std::abs (tone - lastTone) > 1e-4f)
    {
        lastTone = tone;
        auto lo = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, 400.0, 0.7f, juce::Decibels::decibelsToGain (-6.0f * tone));
        auto hi = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0, 0.7f, juce::Decibels::decibelsToGain (6.0f * tone));
        for (int c = 0; c < 2; ++c) { tiltLo[c].coefficients = lo; tiltHi[c].coefficients = hi; }
    }
    mixS.setTargetValue (p[kMix]->load() * 0.01f); outS.setTargetValue (juce::Decibels::decibelsToGain (p[kOut]->load()));
    float pout[2] {};
    for (int i = 0; i < n; ++i)
    {
        const float m = mixS.getNextValue(), o = outS.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            float y = buffer.getSample (c, i);
            const float dc = y - dcX[c] + 0.9995f * dcY[c]; dcX[c] = y; dcY[c] = dc; y = dc;
            if (tone != 0.0f) y = tiltHi[c].processSample (tiltLo[c].processSample (y));
            y = (dryBuf.getSample (c, i) * (1.0f - m) + y * m) * o;
            buffer.setSample (c, i, y);
            pout[c] = std::max (pout[c], std::abs (y));
        }
    }
    inL.store (std::max (inL.load(), pin[0])); inR.store (std::max (inR.load(), nch > 1 ? pin[1] : pin[0]));
    outL.store (std::max (outL.load(), pout[0])); outR.store (std::max (outR.load(), nch > 1 ? pout[1] : pout[0]));
}

void VegaSaturatorProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaSaturatorProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        apvts.replaceState (s);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaSaturatorProcessor(); }
