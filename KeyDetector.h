#pragma once
// SampleKey Analyzer - key detection core.
// Pure C++17, no JUCE dependency, so it can be unit-tested on its own.
// NOT real-time safe on purpose (it allocates in prepare/processFrame):
// it must run on an analysis thread, never on the audio thread.

#include <array>
#include <string>
#include <vector>

namespace sk
{
enum class Status { Listening, Analyzing, Stable, Insufficient };

struct KeyCandidate
{
    int tonic = 0;       // 0 = C, 1 = C#, ... 11 = B
    bool minor = false;
    float score = 0.0f;  // Pearson correlation with the key profile (-1..1)
};

struct Result
{
    Status status = Status::Listening;
    bool signalPresent = false;
    bool hasKey = false;
    int tonic = 0;
    bool minor = false;
    float confidence = 0.0f;                 // heuristic 0..1, NOT a calibrated probability
    std::array<KeyCandidate, 3> alternatives{};
    int numAlternatives = 0;
    std::array<float, 12> chroma{};          // accumulated chroma, normalised to max = 1
    std::array<bool, 12> notePresent{};
    float tuningCents = 0.0f;                // estimated deviation from A440
    float tonalSeconds = 0.0f;               // amount of usable tonal audio analysed so far
};

class KeyDetector
{
public:
    static constexpr int kFftSize = 16384;
    static constexpr int kHop = 4096;

    KeyDetector();

    void prepare (double sampleRate);   // also resets
    void reset();

    // 0 = slow / very stable (30 s memory), 1 = fast reacting (4 s memory)
    void setSensitivity (float s01);

    // Feed mono samples. Frames are analysed internally every kHop samples.
    void pushSamples (const float* mono, int numSamples);

    const Result& result() const noexcept { return res; }

    static const char* noteName (int pitchClass);                 // sharps only
    static std::string keyName (int tonic, bool minor);           // e.g. "F# Minor"

private:
    void processFrame();
    void updateKey (bool audioFrame);
    void fft();
    void updateDecay();

    double sr = 44100.0;
    float sensitivity = 0.5f;
    double decay = 0.9;

    std::vector<float> ring;
    int writePos = 0, sinceLast = 0, filled = 0;

    std::vector<double> window, re, im, mag, cosT, sinT;
    std::vector<int> bitrev;

    std::array<double, 12> acc{};
    double accWeight = 0.0;
    double tonalSecondsAcc = 0.0;
    double tuneRe = 0.0, tuneIm = 0.0, tuneWeight = 0.0;
    double tuningSemi = 0.0;

    int silentRun = 0;
    long audioFrames = 0;

    bool haveKey = false;
    int curIdx = 0, candIdx = -1, candStreak = 0, stableFrames = 0;

    Result res;
};
} // namespace sk
