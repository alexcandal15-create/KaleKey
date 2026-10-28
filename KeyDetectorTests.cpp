// Tests for the key-detection core using synthetic audio of KNOWN key.
// Build & run:  g++ -O2 -std=c++17 -I Source Tests/KeyDetectorTests.cpp Source/KeyDetector.cpp -o keytests && ./keytests
#include "KeyDetector.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct Chord { int root; bool minor; };

// Additive tone with 6 harmonics (1/h rolloff) so harmonic suppression is really exercised.
void addTone (std::vector<float>& out, double sr, double f, double t0, double dur, double amp)
{
    const size_t s0 = (size_t) (t0 * sr), n = (size_t) (dur * sr);
    for (size_t i = 0; i < n && s0 + i < out.size(); ++i)
    {
        const double t = (double) i / sr;
        const double env = std::exp (-1.2 * t) * std::min (1.0, t * 200.0);
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            if (f * h < sr * 0.45) v += std::sin (2.0 * kPi * f * h * t) / h;
        out[s0 + i] += (float) (amp * env * v);
    }
}

double midiToHz (double m, double cents) { return 440.0 * std::pow (2.0, (m - 69.0 + cents / 100.0) / 12.0); }

enum class Flavour { Clean, DrumsNoise, Detuned };

std::vector<float> makeProgression (int tonic, bool minor, double sr, double seconds, Flavour fl, unsigned seed)
{
    std::vector<float> x ((size_t) (seconds * sr), 0.0f);
    const std::vector<Chord> prog = minor
        ? std::vector<Chord>{ { 0, true }, { 5, true }, { 7, false }, { 0, true } }      // i iv V i  (harmonic minor)
        : std::vector<Chord>{ { 0, false }, { 5, false }, { 7, false }, { 0, false } };  // I IV V I
    // NOTE: I-vi-IV-V (C Am F G) and i-VI-III-VII (Am F C G) are the SAME chords, so they are
    // inherently ambiguous between relative major/minor. Do not use them as ground truth.
    const double cents = fl == Flavour::Detuned ? 28.0 : 0.0;

    const double chordLen = 2.0;
    for (double t = 0.0; t < seconds; t += 0.5)   // re-trigger every half second
    {
        const Chord& c = prog[(size_t) ((int) (t / chordLen) % (int) prog.size())];
        const int root = 48 + tonic + c.root;      // around C3..B3
        const int third = c.minor ? 3 : 4;
        addTone (x, sr, midiToHz (root - 12, cents), t, 0.5, 0.18);          // bass
        addTone (x, sr, midiToHz (root, cents), t, 0.5, 0.12);
        addTone (x, sr, midiToHz (root + third, cents), t, 0.5, 0.12);
        addTone (x, sr, midiToHz (root + 7, cents), t, 0.5, 0.12);
    }

    if (fl == Flavour::DrumsNoise)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<float> nd (0.0f, 1.0f);
        for (double t = 0.0; t < seconds; t += 0.5)   // kick on every beat, hats on off-beats
        {
            const size_t s0 = (size_t) (t * sr);
            for (size_t i = 0; i < (size_t) (0.18 * sr) && s0 + i < x.size(); ++i)
            {
                const double tt = (double) i / sr;
                const double f = 50.0 + 90.0 * std::exp (-tt * 30.0);
                x[s0 + i] += (float) (0.5 * std::exp (-tt * 14.0) * std::sin (2.0 * kPi * f * tt));
            }
            const size_t h0 = (size_t) ((t + 0.25) * sr);
            for (size_t i = 0; i < (size_t) (0.05 * sr) && h0 + i < x.size(); ++i)
                x[h0 + i] += 0.15f * nd (rng) * (float) std::exp (-(double) i / sr * 80.0);
        }
        for (auto& s : x) s += 0.01f * nd (rng);   // noise floor around -40 dB
    }
    return x;
}

sk::Result analyse (const std::vector<float>& x, double sr, float sens = 0.5f)
{
    sk::KeyDetector d;
    d.prepare (sr);
    d.setSensitivity (sens);
    // feed in odd-sized blocks like a host would
    size_t pos = 0;
    const int blocks[] = { 64, 480, 512, 1024, 333 };
    int b = 0;
    while (pos < x.size())
    {
        const size_t n = std::min ((size_t) blocks[b++ % 5], x.size() - pos);
        d.pushSamples (x.data() + pos, (int) n);
        pos += n;
    }
    return d.result();
}

int failures = 0;
void check (bool ok, const char* msg)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
    if (! ok) ++failures;
}

void runSet (const char* name, Flavour fl, double sr, int minExact)
{
    int exact = 0, relative = 0, fifth = 0, other = 0;
    for (int m = 0; m < 2; ++m)
        for (int t = 0; t < 12; ++t)
        {
            auto x = makeProgression (t, m == 1, sr, 24.0, fl, (unsigned) (t + 100 * m));
            auto r = analyse (x, sr);
            if (r.hasKey && r.tonic == t && r.minor == (m == 1)) ++exact;
            else if (r.hasKey && ((m == 0 && r.minor && r.tonic == (t + 9) % 12) || (m == 1 && ! r.minor && r.tonic == (t + 3) % 12))) ++relative;
            else if (r.hasKey && r.tonic == (t + 7) % 12) ++fifth;
            else
            {
                ++other;
                std::printf ("    miss: truth %s -> %s\n", sk::KeyDetector::keyName (t, m == 1).c_str(),
                             r.hasKey ? sk::KeyDetector::keyName (r.tonic, r.minor).c_str() : "(none)");
            }
        }
    std::printf ("%s @ %.0f Hz: exact %d/24, relative-key %d, fifth-off %d, other %d\n", name, sr, exact, relative, fifth, other);
    char buf[128];
    std::snprintf (buf, sizeof buf, "%s @ %.0f Hz: at least %d/24 exact", name, sr, minExact);
    check (exact >= minExact, buf);
}
} // namespace

int main()
{
    std::printf ("== 24-key detection on synthetic progressions ==\n");
    runSet ("Clean", Flavour::Clean, 44100.0, 22);
    runSet ("Clean", Flavour::Clean, 48000.0, 22);
    runSet ("Drums+noise", Flavour::DrumsNoise, 44100.0, 20);
    runSet ("Detuned +28c", Flavour::Detuned, 44100.0, 20);

    std::printf ("== Behaviour on non-musical / insufficient input ==\n");
    {
        std::vector<float> silence (44100 * 12, 0.0f);
        auto r = analyse (silence, 44100.0);
        check (! r.hasKey && r.status == sk::Status::Listening, "silence -> Listening, no key");
    }
    {
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        std::vector<float> noise (44100 * 20);
        for (auto& s : noise) s = nd (rng);
        auto r = analyse (noise, 44100.0);
        check (! r.hasKey, "white noise -> no key reported");
        check (r.status == sk::Status::Insufficient || r.status == sk::Status::Analyzing, "white noise -> not Stable");
    }
    {
        auto x = makeProgression (6, true, 44100.0, 0.8, Flavour::Clean, 1);
        auto r = analyse (x, 44100.0);
        check (! r.hasKey, "0.8 s sample -> not enough information");
    }
    {
        auto x = makeProgression (6, true, 44100.0, 24.0, Flavour::Clean, 1);
        auto r = analyse (x, 44100.0);
        check (r.hasKey && r.tonic == 6 && r.minor && sk::KeyDetector::keyName (r.tonic, r.minor) == "F# Minor",
               "F# minor progression is named 'F# Minor' (sharps, not Gb)");
        check (r.status == sk::Status::Stable, "long clean sample reaches Stable");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
