#include "PluginEditor.h"

namespace
{
class TransferView : public juce::Component
{
public:
    explicit TransferView (VegaSaturatorProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto& a = proc.apvts;
        auto r = getLocalBounds().toFloat().reduced (12.0f);
        g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("TRANSFER", r.removeFromTop (14.0f), juce::Justification::centredLeft);
        r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
        g.setColour (juce::Colour (0xff262626));
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight()); g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());
        g.drawRect (r);
        g.setColour (juce::Colour (0xff333333));
        g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);
        const int type = (int) a.getRawParameterValue ("type")->load();
        const float drive = a.getRawParameterValue ("drive")->load() * 0.01f, bias = a.getRawParameterValue ("bias")->load() * 0.004f;
        const float gn = vsat::driveToGain (drive);
        const bool autoG = a.getRawParameterValue ("autoGain")->load() > 0.5f;
        const float comp = autoG ? vsat::autoGain (type, gn, bias) : 1.0f;
        const float crush = a.getRawParameterValue ("crush")->load() * 0.01f;
        const int bits = juce::roundToInt (16.0f - crush * 13.0f); const float q = 1.0f / (float) (1 << (bits - 1));
        juce::Path path;
        for (int i = 0; i <= 300; ++i)
        {
            const float x = -1.0f + 2.0f * i / 300.0f;
            float y = type == 6 ? std::round (juce::jlimit (-1.0f, 1.0f, x * gn) / q) * q * comp : vsat::shape (type, x * gn, bias) * comp;
            y = juce::jlimit (-1.2f, 1.2f, y);
            const juce::Point<float> pt (r.getX() + (x + 1) * 0.5f * r.getWidth(), r.getCentreY() - y * 0.5f * r.getHeight());
            if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        g.setColour (VegaColours::accent.withAlpha (0.25f)); g.strokePath (path, juce::PathStrokeType (5.0f));
        g.setColour (VegaColours::accent); g.strokePath (path, juce::PathStrokeType (2.0f));
    }
private:
    VegaSaturatorProcessor& proc;
};

class BandView : public juce::Component
{
public:
    explicit BandView (VegaSaturatorProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto& a = proc.apvts;
        auto r = getLocalBounds().toFloat().reduced (12.0f);
        g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("BANDS", r.removeFromTop (14.0f), juce::Justification::centredLeft);
        auto freqX = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
        g.setFont (VegaFonts::ui (9.0f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (juce::Colour (0xff242424)); g.drawVerticalLine ((int) freqX (f), r.getY(), r.getBottom() - 12);
            g.setColour (VegaColours::textDim.withAlpha (0.5f));
            g.drawText (f >= 1000 ? juce::String ((int) f / 1000) + "k" : juce::String ((int) f), (int) freqX (f) - 15, (int) r.getBottom() - 11, 30, 11, juce::Justification::centred);
        }
        const bool multi = a.getRawParameterValue ("mode")->load() > 0.5f;
        const float x1 = freqX (a.getRawParameterValue ("xLow")->load()), x2 = freqX (a.getRawParameterValue ("xHigh")->load());
        const float drive = a.getRawParameterValue ("drive")->load() * 0.01f;
        const char* dIds[] { "lowDrive", "midDrive", "highDrive" }; const char* onIds[] { "lowOn", "midOn", "highOn" };
        const char* names[] { "LOW", "MID", "HIGH" };
        auto plot = r.withTrimmedBottom (14.0f);
        for (int b = 0; b < 3; ++b)
        {
            const float l = b == 0 ? plot.getX() : b == 1 ? x1 : x2, rr = b == 0 ? x1 : b == 1 ? x2 : plot.getRight();
            const float d = multi ? juce::jlimit (0.0f, 1.0f, drive * a.getRawParameterValue (dIds[b])->load() * 0.01f) : (b == 1 ? drive : drive);
            const bool on = ! multi || a.getRawParameterValue (onIds[b])->load() > 0.5f;
            auto col = juce::Rectangle<float> (l + 2, plot.getBottom() - d * plot.getHeight(), rr - l - 4, d * plot.getHeight());
            g.setColour ((on ? VegaColours::accent : VegaColours::textDim).withAlpha (0.18f)); g.fillRect (col);
            g.setColour (on ? VegaColours::accent : VegaColours::textDim); g.fillRect (col.removeFromTop (2.0f));
            // live level
            const float lvl = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (levels[b], -60.0f) + 60.0f) / 60.0f);
            g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillRect (rr - 8, plot.getBottom() - lvl * plot.getHeight(), 4.0f, lvl * plot.getHeight());
            if (multi) { g.setColour (VegaColours::text); g.setFont (VegaFonts::ui (10.0f, true)); g.drawText (names[b], (int) l, (int) plot.getY() + 2, (int) (rr - l), 12, juce::Justification::centred); }
        }
        if (multi) { g.setColour (VegaColours::gold); g.fillRect (x1 - 1, plot.getY(), 2.0f, plot.getHeight()); g.fillRect (x2 - 1, plot.getY(), 2.0f, plot.getHeight()); }
        else { g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (11.0f)); g.drawText ("single band - switch Mode to Multiband to drive lows, mids and highs separately", plot.toNearestInt().removeFromTop (20), juce::Justification::centred); }
    }
    float levels[3] {};
private:
    VegaSaturatorProcessor& proc;
};
}

class VegaSaturatorEditor::Main : public juce::Component
{
public:
    explicit Main (VegaSaturatorProcessor& p) : proc (p), a (p.apvts), transfer (p), bands (p),
        type (a, "type", "Type"), mode (a, "mode", "Mode"), drive (a, "drive", "DRIVE", true), bias (a, "bias", "Asymmetry"), tone (a, "tone", "Tone"), crush (a, "crush", "Crush"),
        lowDrive (a, "lowDrive", "Low Drive"), midDrive (a, "midDrive", "Mid Drive"), highDrive (a, "highDrive", "High Drive"), xLow (a, "xLow", "Low / Mid"), xHigh (a, "xHigh", "Mid / High"),
        lowOn (a, "lowOn", "Low"), midOn (a, "midOn", "Mid"), highOn (a, "highOn", "High"),
        osSel (a, "oversampling", "Oversampling"), autoG (a, "autoGain", "Auto Gain"), mix (a, "mix", "Mix"), output (a, "output", "Output")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &transfer, &bands, &type, &mode, &drive, &bias, &tone, &crush, &lowDrive, &midDrive, &highDrive, &xLow, &xHigh,
                                                                 &lowOn, &midOn, &highOn, &osSel, &autoG, &mix, &output, &inMeter, &outMeter })
            addAndMakeVisible (c);
    }
    void refresh()
    {
        auto take = [] (std::atomic<float>& v) { return v.exchange (0.0f); };
        inMeter.setLevels (take (proc.inL), take (proc.inR)); outMeter.setLevels (take (proc.outL), take (proc.outR));
        for (int b = 0; b < 3; ++b) { const float v = take (proc.bandLevel[b]); bands.levels[b] = std::max (v, bands.levels[b] * 0.85f); }
        transfer.repaint(); bands.repaint();
        const bool multi = a.getRawParameterValue ("mode")->load() > 0.5f;
        for (juce::Component* c : std::initializer_list<juce::Component*> { &lowDrive, &midDrive, &highDrive, &xLow, &xHigh, &lowOn, &midOn, &highOn }) c->setEnabled (multi);
        crush.setEnabled ((int) a.getRawParameterValue ("type")->load() == 6);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "SATURATOR  VP-12");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        g.setColour (VegaColours::line); g.drawHorizontalLine (252, 0, (float) W);
        vegaui::drawSection (g, { 0, 256, 430, 270 }, "SATURATION", false);
        vegaui::drawSection (g, { 430, 256, 400, 270 }, "MULTIBAND");
        vegaui::drawSection (g, { 830, 256, W - 830, 270 }, "OUTPUT");
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("IN", 812, 40, 60, 12, juce::Justification::centred); g.drawText ("OUT", 900, 40, 60, 12, juce::Justification::centred);
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "AUTO GAIN KEEPS THE LEVEL STEADY SO YOU HEAR THE COLOUR, NOT JUST LOUDER");
    }
    void resized()
    {
        transfer.setBounds (12, 44, 250, 202); bands.setBounds (270, 44, 520, 202);
        inMeter.setBounds (812, 52, 60, 194); outMeter.setBounds (900, 52, 60, 194);
        type.setBounds (14, 276, 140, 46); mode.setBounds (14, 340, 140, 46);
        drive.setBounds (160, 268, 130, 160); bias.setBounds (296, 268, 64, 112); tone.setBounds (360, 268, 66, 112); crush.setBounds (296, 396, 64, 112);
        lowDrive.setBounds (440, 268, 80, 112); midDrive.setBounds (520, 268, 80, 112); highDrive.setBounds (600, 268, 80, 112);
        xLow.setBounds (680, 268, 74, 112); xHigh.setBounds (754, 268, 74, 112);
        lowOn.setBounds (450, 400, 60, 80); midOn.setBounds (530, 400, 60, 80); highOn.setBounds (610, 400, 60, 80);
        osSel.setBounds (840, 276, 150, 46); autoG.setBounds (840, 334, 70, 76); mix.setBounds (840, 414, 74, 108); output.setBounds (916, 414, 74, 108);
    }
private:
    VegaSaturatorProcessor& proc; juce::AudioProcessorValueTreeState& a;
    TransferView transfer; BandView bands;
    vegaui::Choice type, mode; vegaui::Knob drive, bias, tone, crush, lowDrive, midDrive, highDrive, xLow, xHigh; vegaui::Toggle lowOn, midOn, highOn;
    vegaui::Choice osSel; vegaui::Toggle autoG; vegaui::Knob mix, output; vegaui::StereoMeter inMeter, outMeter;
};

VegaSaturatorEditor::VegaSaturatorEditor (VegaSaturatorProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaSaturatorEditor::~VegaSaturatorEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaSaturatorEditor::timerCallback() { main->refresh(); }
void VegaSaturatorEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaSaturatorProcessor::createEditor() { return new VegaSaturatorEditor (*this); }
