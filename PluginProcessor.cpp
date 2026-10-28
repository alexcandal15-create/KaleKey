#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout SampleKeyProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sensitivity", 1 }, "Sensitivity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    return layout;
}

SampleKeyProcessor::SampleKeyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      fifoData ((size_t) kFifoSize, 0.0f)
{
    sensitivityParam = apvts.getRawParameterValue ("sensitivity");
    pendingSampleRate.store (44100.0);
    analysisThread.startThread();
}

SampleKeyProcessor::~SampleKeyProcessor()
{
    analysisThread.stopThread (2000);
}

bool SampleKeyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void SampleKeyProcessor::prepareToPlay (double sampleRate, int)
{
    pendingSampleRate.store (sampleRate);   // applied by the analysis thread
    resetRequested.store (true);
}

// AUDIO THREAD: no allocation, no locks, no waiting. The audio passes through untouched;
// a mono sum is copied into a lock-free FIFO for the analysis thread.
void SampleKeyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    float peak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        peak = std::max (peak, buffer.getMagnitude (ch, 0, n));
    inputLevel.store (std::max (peak, inputLevel.load() * 0.9f));

    if (frozen.load()) return;

    int s1, z1, s2, z2;
    fifo.prepareToWrite (n, s1, z1, s2, z2);   // if full, we just drop samples instead of blocking

    const float inv = 1.0f / (float) numCh;
    int i = 0;
    auto mixInto = [&] (int start, int size)
    {
        for (int k = 0; k < size; ++k, ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                sum += buffer.getReadPointer (ch)[i];
            fifoData[(size_t) (start + k)] = sum * inv;
        }
    };
    mixInto (s1, z1);
    mixInto (s2, z2);
    fifo.finishedWrite (z1 + z2);
}

bool SampleKeyProcessor::getResult (sk::Result& out)
{
    const juce::SpinLock::ScopedTryLockType lock (resultLock);
    if (! lock.isLocked()) return false;
    out = sharedResult;
    return true;
}

// ANALYSIS THREAD: owns the detector, allocation and FFTs are fine here.
void SampleKeyProcessor::AnalysisThread::run()
{
    sk::KeyDetector detector;
    detector.prepare (44100.0);
    std::vector<float> chunk (4096);

    while (! threadShouldExit())
    {
        const double sr = owner.pendingSampleRate.exchange (0.0);
        if (sr > 0.0) detector.prepare (sr);
        if (owner.resetRequested.exchange (false)) detector.reset();
        detector.setSensitivity (owner.sensitivityParam != nullptr ? owner.sensitivityParam->load() : 0.5f);

        const int ready = owner.fifo.getNumReady();
        if (ready <= 0)
        {
            wait (15);
            continue;
        }

        const int n = std::min (ready, (int) chunk.size());
        int s1, z1, s2, z2;
        owner.fifo.prepareToRead (n, s1, z1, s2, z2);
        std::copy_n (owner.fifoData.begin() + s1, z1, chunk.begin());
        std::copy_n (owner.fifoData.begin() + s2, z2, chunk.begin() + z1);
        owner.fifo.finishedRead (z1 + z2);

        detector.pushSamples (chunk.data(), z1 + z2);

        const juce::SpinLock::ScopedLockType lock (owner.resultLock);
        owner.sharedResult = detector.result();
    }
}

juce::AudioProcessorEditor* SampleKeyProcessor::createEditor() { return new SampleKeyEditor (*this); }

void SampleKeyProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SampleKeyProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SampleKeyProcessor(); }
