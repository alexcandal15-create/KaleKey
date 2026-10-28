#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include "KeyDetector.h"

class SampleKeyProcessor : public juce::AudioProcessor
{
public:
    SampleKeyProcessor();
    ~SampleKeyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SampleKey Analyzer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- UI-side API (message thread) ----
    bool getResult (sk::Result& out);          // false if the analysis thread was busy; keep old value
    float getInputLevel() const noexcept { return inputLevel.load(); }
    void requestReset() noexcept { resetRequested.store (true); }
    void setFrozen (bool f) noexcept { frozen.store (f); }
    bool isFrozen() const noexcept { return frozen.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (SampleKeyProcessor& o) : juce::Thread ("SampleKey analysis"), owner (o) {}
        void run() override;
    private:
        SampleKeyProcessor& owner;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int kFifoSize = 1 << 17;     // ~3 s at 44.1 kHz, plenty for a 15 ms analysis loop
    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> fifoData;

    std::atomic<float> inputLevel { 0.0f };
    std::atomic<bool> frozen { false }, resetRequested { false };
    std::atomic<double> pendingSampleRate { 0.0 };
    std::atomic<float>* sensitivityParam = nullptr;

    juce::SpinLock resultLock;
    sk::Result sharedResult;

    AnalysisThread analysisThread { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleKeyProcessor)
};
