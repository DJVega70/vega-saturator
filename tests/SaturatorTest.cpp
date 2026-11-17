#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaSaturatorProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

// Harmonic magnitudes of a processed sine via DFT bins (exact bin frequencies)
struct Result { double h[8] {}; double rms = 0; double alias = 0; };
static Result analyse (VegaSaturatorProcessor& p, float freq, float amp, int n = 48000)
{
    const double fs = 48000; const int blk = 480;
    std::vector<float> out ((size_t) n);
    juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < n; pos += blk)
    {
        for (int i = 0; i < blk; ++i) { const float x = amp * (float) std::sin (2 * juce::MathConstants<double>::pi * freq * (pos + i) / fs); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        for (int i = 0; i < blk; ++i) out[(size_t) (pos + i)] = b.getSample (0, i);
    }
    Result r; const int start = n / 2, len = n / 2;   // analyse second half (settled), 0.5 s -> 2 Hz bins
    auto bin = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < len; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / len); re += out[(size_t) (start + i)] * w * std::cos (2 * juce::MathConstants<double>::pi * f * i / fs); im += out[(size_t) (start + i)] * w * std::sin (2 * juce::MathConstants<double>::pi * f * i / fs); } return std::sqrt (re * re + im * im) * 4.0 / len; };
    for (int k = 1; k <= 7; ++k) r.h[k] = freq * k < 23000 ? bin (freq * k) : 0;
    for (int i = start; i < n; ++i) r.rms += out[(size_t) i] * out[(size_t) i];
    r.rms = std::sqrt (r.rms / len);
    // alias probe: energy at a frequency that is not a harmonic (folded 3rd harmonic of 15 kHz = 45k -> 3 kHz)
    r.alias = bin (3000);
    return r;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000;
    auto make = [&] { auto* p = new VegaSaturatorProcessor(); p->setPlayConfigDetails (2, 2, fs, 480); p->prepareToPlay (fs, 480); return std::unique_ptr<VegaSaturatorProcessor> (p); };
    for (int t = 0; t < (int) vsat::typeNames().size(); ++t)
    {
        bool ok = true; for (int i = -200; i <= 200; ++i) ok &= std::isfinite (vsat::shape (t, i * 0.05f, 0.3f)) && std::abs (vsat::shape (t, 0, 0.3f)) < 1e-5f;
        check (ok, "shape " + vsat::typeNames()[t] + " passes 0 and stays finite");
    }
    {
        auto lo = make(), hi = make();
        set (*lo, "drive", 5); set (*hi, "drive", 80); set (*lo, "type", 3); set (*hi, "type", 3);
        auto a = analyse (*lo, 200, 0.5f), b = analyse (*hi, 200, 0.5f);
        const double thdA = std::sqrt (a.h[3] * a.h[3] + a.h[5] * a.h[5]) / a.h[1], thdB = std::sqrt (b.h[3] * b.h[3] + b.h[5] * b.h[5]) / b.h[1];
        check (thdB > thdA * 5 && thdB > 0.05, "more drive -> more harmonics (THD " + juce::String (thdA * 100, 2) + "% -> " + juce::String (thdB * 100, 1) + "%)");
    }
    {
        auto sym = make(), asym = make();
        for (auto* p : { sym.get(), asym.get() }) { set (*p, "type", 3); set (*p, "drive", 60); }
        set (*asym, "bias", 80);
        auto a = analyse (*sym, 200, 0.5f), b = analyse (*asym, 200, 0.5f);
        check (a.h[2] / a.h[1] < 0.003 && b.h[2] / b.h[1] > 0.02, "asymmetry adds even (2nd) harmonics (" + juce::String (a.h[2] / a.h[1] * 100, 2) + "% -> " + juce::String (b.h[2] / b.h[1] * 100, 1) + "%)");
    }
    {   // auto gain keeps level steady across drive
        auto a = make(), b = make();
        set (*a, "drive", 10); set (*b, "drive", 90); set (*a, "type", 0); set (*b, "type", 0);
        auto ra = analyse (*a, 300, 0.25f), rb = analyse (*b, 300, 0.25f);
        const double diff = 20 * std::log10 (rb.rms / ra.rms);
        check (std::abs (diff) < 2.5, "auto gain: 10% vs 90% drive within " + juce::String (diff, 2) + " dB");
    }
    {   // oversampling reduces aliasing on a hard clipper with a 15 kHz tone
        auto off = make(), x8 = make();
        for (auto* p : { off.get(), x8.get() }) { set (*p, "type", 4); set (*p, "drive", 70); }
        set (*off, "oversampling", 0); set (*x8, "oversampling", 3);
        off->prepareToPlay (fs, 480); x8->prepareToPlay (fs, 480);
        auto a = analyse (*off, 15000, 0.5f), b = analyse (*x8, 15000, 0.5f);
        check (b.alias < a.alias * 0.2, "8x oversampling cuts aliasing (" + juce::String (20 * std::log10 (b.alias / a.alias + 1e-12), 1) + " dB)");
        check (x8->getLatencySamples() > 0 && off->getLatencySamples() == 0, "latency reported (" + juce::String (x8->getLatencySamples()) + " samples at 8x)");
    }
    {   // multiband: low band off -> 60 Hz stays clean while mids distort
        auto p = make(); set (*p, "mode", 1); set (*p, "type", 4); set (*p, "drive", 80); set (*p, "lowOn", 0); set (*p, "oversampling", 0); p->prepareToPlay (fs, 480);
        auto lo = analyse (*p, 60, 0.4f);
        auto p2 = make(); set (*p2, "mode", 1); set (*p2, "type", 4); set (*p2, "drive", 80); set (*p2, "oversampling", 0); p2->prepareToPlay (fs, 480);
        auto mid = analyse (*p2, 1000, 0.4f);
        check (lo.h[3] / lo.h[1] < 0.02 && mid.h[3] / mid.h[1] > 0.1, "multiband: bypassed low band stays clean (" + juce::String (lo.h[3] / lo.h[1] * 100, 2) + "% H3), mids distort (" + juce::String (mid.h[3] / mid.h[1] * 100, 1) + "%)");
    }
    {   // mix 0 = dry (latency compensated)
        auto p = make(); set (*p, "mix", 0); set (*p, "drive", 100);
        auto r = analyse (*p, 440, 0.5f);
        check (r.h[3] / r.h[1] < 0.001 && std::abs (r.h[1] - 0.5) < 0.02, "mix 0% is clean dry (" + juce::String (r.h[1], 3) + ")");
    }
    {
        auto p = make(); set (*p, "type", 6); set (*p, "crush", 100); auto r = analyse (*p, 440, 0.5f);
        check (std::isfinite (r.rms) && r.rms > 0.05, "bitcrush produces sound");
    }
    {
        auto p = make(); set (*p, "drive", 61); juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q->apvts.getRawParameterValue ("drive")->load() - 61) < 0.5f, "state round trip");
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
