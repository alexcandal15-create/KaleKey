#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
const juce::Colour kBg      { 0xff0e1014 };
const juce::Colour kPanel   { 0xff171a21 };
const juce::Colour kText    { 0xffe8eaf0 };
const juce::Colour kDim     { 0xff7b8294 };
const juce::Colour kAccent  { 0xff4de0b4 };
const juce::Colour kWarn    { 0xffffb454 };

const char* statusText (sk::Status s)
{
    switch (s)
    {
        case sk::Status::Listening:    return "Listening";
        case sk::Status::Analyzing:    return "Analyzing";
        case sk::Status::Stable:       return "Stable Signal";
        case sk::Status::Insufficient: return "Insufficient Audio";
    }
    return "";
}
}

juce::Font SampleKeyEditor::makeFont (float h, bool bold)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

SampleKeyEditor::SampleKeyEditor (SampleKeyProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (560, 400);

    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { processor.requestReset(); };

    addAndMakeVisible (holdButton);
    holdButton.setClickingTogglesState (true);
    holdButton.setToggleState (processor.isFrozen(), juce::dontSendNotification);
    holdButton.onClick = [this] { processor.setFrozen (holdButton.getToggleState()); };

    for (auto* b : { &resetButton, &holdButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::buttonOnColourId, kAccent.darker (0.4f));
        b->setColour (juce::TextButton::textColourOffId, kText);
        b->setColour (juce::TextButton::textColourOnId, kText);
    }

    addAndMakeVisible (sensitivitySlider);
    sensitivitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensitivitySlider.setColour (juce::Slider::trackColourId, kAccent);
    sensitivitySlider.setColour (juce::Slider::thumbColourId, kText);
    sensitivitySlider.setColour (juce::Slider::backgroundColourId, kPanel);
    sensitivityAttachment = std::make_unique<juce::SliderParameterAttachment> (
        *processor.apvts.getParameter ("sensitivity"), sensitivitySlider, nullptr);

    addAndMakeVisible (sensitivityLabel);
    sensitivityLabel.setText ("Sensitivity  (slow & stable  <->  fast)", juce::dontSendNotification);
    sensitivityLabel.setColour (juce::Label::textColourId, kDim);
    sensitivityLabel.setFont (makeFont (12.0f, false));

    startTimerHz (30);
}

SampleKeyEditor::~SampleKeyEditor() { stopTimer(); }

void SampleKeyEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    r.removeFromTop (30);                       // title row
    keyArea = r.removeFromTop (150);
    notesArea = r.removeFromTop (100);
    auto bottom = r.removeFromBottom (36);
    resetButton.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (8);
    holdButton.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (20);
    auto sl = bottom;
    sensitivityLabel.setBounds (sl.removeFromTop (14));
    sensitivitySlider.setBounds (sl);
    meterArea = juce::Rectangle<int> (20, 12, 120, 8).withRightX (getWidth() - 20);
}

void SampleKeyEditor::timerCallback()
{
    sk::Result r;
    if (processor.getResult (r)) shown = r;       // never blocks: keep old value if analysis thread is publishing
    level = std::max (processor.getInputLevel(), level * 0.85f);
    repaint();
}

void SampleKeyEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // title + input meter (shows that audio is reaching the plugin)
    g.setColour (kDim);
    g.setFont (makeFont (13.0f, true));
    g.drawText ("SAMPLEKEY ANALYZER", 20, 10, 250, 18, juce::Justification::left);

    g.setFont (makeFont (11.0f, false));
    g.drawText ("INPUT", meterArea.getX() - 44, 8, 40, 14, juce::Justification::right);
    g.setColour (kPanel);
    g.fillRoundedRectangle (meterArea.toFloat(), 4.0f);
    const float lv = juce::jlimit (0.0f, 1.0f, std::pow (level, 0.5f));
    g.setColour (level > 0.95f ? kWarn : kAccent);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth (meterArea.getWidth() * lv), 4.0f);

    // main panel
    g.setColour (kPanel);
    g.fillRoundedRectangle (keyArea.toFloat(), 12.0f);

    const bool has = shown.hasKey;
    auto k = keyArea.reduced (18, 12);

    g.setColour (kDim);
    g.setFont (makeFont (11.0f, false));
    g.drawText ("DETECTED KEY", k.removeFromTop (14), juce::Justification::left);

    auto left = k.removeFromLeft (300);
    g.setColour (has ? kText : kDim);
    g.setFont (makeFont (has ? 52.0f : 36.0f, true));
    g.drawText (has ? juce::String (sk::KeyDetector::keyName (shown.tonic, shown.minor)) : juce::String ("--"),
                left.removeFromTop (70), juce::Justification::centredLeft);

    g.setColour (kDim);
    g.setFont (makeFont (13.0f, false));
    g.drawText ("Scale:  " + juce::String (has ? (shown.minor ? "Minor" : "Major") : "--"),
                left.removeFromTop (20), juce::Justification::left);

    // status pill
    const bool warn = shown.status == sk::Status::Insufficient;
    g.setColour (warn ? kWarn : (shown.status == sk::Status::Stable ? kAccent : kDim));
    g.setFont (makeFont (13.0f, true));
    g.drawText (juce::String ("Analysis Status:  ") + statusText (shown.status), left.removeFromTop (22),
                juce::Justification::left);

    // right column: confidence + alternatives
    k.removeFromLeft (10);
    g.setColour (kDim);
    g.setFont (makeFont (11.0f, false));
    g.drawText ("CONFIDENCE (heuristic)", k.removeFromTop (14), juce::Justification::left);
    auto bar = k.removeFromTop (12).toFloat();
    g.setColour (kBg);
    g.fillRoundedRectangle (bar, 6.0f);
    g.setColour (shown.confidence < 0.3f ? kWarn : kAccent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * shown.confidence), 6.0f);
    g.setColour (kText);
    g.setFont (makeFont (13.0f, true));
    g.drawText (has ? juce::String (juce::roundToInt (shown.confidence * 100.0f)) + " %" : juce::String ("--"),
                k.removeFromTop (22), juce::Justification::left);

    g.setColour (kDim);
    g.setFont (makeFont (11.0f, false));
    g.drawText ("ALTERNATIVE KEYS", k.removeFromTop (16), juce::Justification::left);
    g.setColour (kText);
    g.setFont (makeFont (13.0f, false));
    if (has && shown.numAlternatives > 0)
        for (int i = 0; i < shown.numAlternatives; ++i)
            g.drawText (juce::String (sk::KeyDetector::keyName (shown.alternatives[(size_t) i].tonic,
                                                                shown.alternatives[(size_t) i].minor)),
                        k.removeFromTop (17), juce::Justification::left);
    else
    {
        g.setColour (kDim);
        g.drawText (has ? "none close" : "--", k.removeFromTop (17), juce::Justification::left);
    }

    // detected notes: 12 chroma bars
    auto n = notesArea.reduced (0, 6);
    g.setColour (kDim);
    g.setFont (makeFont (11.0f, false));
    g.drawText ("DETECTED NOTES", n.removeFromTop (16), juce::Justification::left);
    const float colW = (float) n.getWidth() / 12.0f;
    auto labels = n.removeFromBottom (18);
    for (int i = 0; i < 12; ++i)
    {
        const float x = (float) n.getX() + colW * (float) i;
        const float h = (float) n.getHeight() * shown.chroma[(size_t) i];
        const bool present = shown.notePresent[(size_t) i];
        g.setColour (present ? kAccent : kPanel.brighter (0.15f));
        g.fillRoundedRectangle (x + 4.0f, (float) n.getBottom() - std::max (3.0f, h), colW - 8.0f, std::max (3.0f, h), 3.0f);
        g.setColour (present ? kText : kDim);
        g.setFont (makeFont (12.0f, present));
        g.drawText (sk::KeyDetector::noteName (i), juce::Rectangle<float> (x, (float) labels.getY(), colW, 18.0f).toNearestInt(),
                    juce::Justification::centred);
    }
}
