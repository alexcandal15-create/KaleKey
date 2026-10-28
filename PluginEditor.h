#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class SampleKeyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SampleKeyEditor (SampleKeyProcessor&);
    ~SampleKeyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::Font makeFont (float height, bool bold);

    SampleKeyProcessor& processor;
    sk::Result shown;
    float level = 0.0f;

    juce::TextButton resetButton { "Reset" };
    juce::TextButton holdButton { "Hold" };
    juce::Slider sensitivitySlider;
    juce::Label sensitivityLabel;
    std::unique_ptr<juce::SliderParameterAttachment> sensitivityAttachment;

    juce::Rectangle<int> keyArea, notesArea, meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleKeyEditor)
};
