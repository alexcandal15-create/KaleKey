#include "KeyDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sk
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Krumhansl-Kessler probe-tone profiles (index 0 = tonic)
const double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
const double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

double pearson (const double* x, const double* y)
{
    double mx = 0, my = 0;
    for (int i = 0; i < 12; ++i) { mx += x[i]; my += y[i]; }
    mx /= 12; my /= 12;
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double a = x[i] - mx, b = y[i] - my;
        sxy += a * b; sxx += a * a; syy += b * b;
    }
    if (sxx < 1e-12 || syy < 1e-12) return 0.0;
    return sxy / std::sqrt (sxx * syy);
}

inline int mod12 (long v) { return (int) (((v % 12) + 12) % 12); }

struct Peak { int bin; double freq; double amp; };
} // namespace

KeyDetector::KeyDetector()
{
    const int N = kFftSize;
    ring.assign ((size_t) N, 0.0f);
    window.resize ((size_t) N);
    re.resize ((size_t) N);
    im.resize ((size_t) N);
    mag.resize ((size_t) N / 2 + 1);
    cosT.resize ((size_t) N / 2);
    sinT.resize ((size_t) N / 2);
    bitrev.resize ((size_t) N);

    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N);   // Hann

    for (int i = 0; i < N / 2; ++i)
    {
        cosT[(size_t) i] = std::cos (-2.0 * kPi * i / N);
        sinT[(size_t) i] = std::sin (-2.0 * kPi * i / N);
    }

    int bits = 0;
    while ((1 << bits) < N) ++bits;
    for (int i = 0; i < N; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev[(size_t) i] = r;
    }

    updateDecay();
    reset();
}

void KeyDetector::prepare (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
    updateDecay();
    reset();
}

void KeyDetector::setSensitivity (float s01)
{
    sensitivity = std::min (1.0f, std::max (0.0f, s01));
    updateDecay();
}

void KeyDetector::updateDecay()
{
    const double tau = 30.0 - 26.0 * (double) sensitivity;   // seconds of memory
    decay = std::exp (-((double) kHop / sr) / tau);
}

void KeyDetector::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = sinceLast = filled = 0;
    acc.fill (0.0);
    accWeight = tonalSecondsAcc = 0.0;
    tuneRe = tuneIm = tuneWeight = tuningSemi = 0.0;
    silentRun = 0;
    audioFrames = 0;
    haveKey = false;
    curIdx = 0; candIdx = -1; candStreak = 0; stableFrames = 0;
    res = Result{};
}

void KeyDetector::pushSamples (const float* x, int n)
{
    const int N = kFftSize;
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) writePos] = x[i];
        writePos = (writePos + 1) % N;
        if (filled < N) ++filled;
        if (++sinceLast >= kHop && filled >= N)
        {
            sinceLast = 0;
            processFrame();
        }
    }
}

void KeyDetector::fft()
{
    const int N = kFftSize;
    for (int i = 0; i < N; ++i)
    {
        const int j = bitrev[(size_t) i];
        if (j > i) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
    }
    for (int len = 2; len <= N; len <<= 1)
    {
        const int half = len >> 1, step = N / len;
        for (int i = 0; i < N; i += len)
            for (int k = 0; k < half; ++k)
            {
                const double wr = cosT[(size_t) (k * step)], wi = sinT[(size_t) (k * step)];
                const size_t a = (size_t) (i + k), b = (size_t) (i + k + half);
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
            }
    }
}

void KeyDetector::processFrame()
{
    const int N = kFftSize;

    double energy = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double s = ring[(size_t) ((writePos + i) % N)];
        energy += s * s;
        re[(size_t) i] = s * window[(size_t) i];
        im[(size_t) i] = 0.0;
    }

    const double rms = std::sqrt (energy / N);
    if (rms < 1.0e-3)   // below about -60 dBFS: treat as silence
    {
        ++silentRun;
        updateKey (false);
        return;
    }
    silentRun = 0;
    ++audioFrames;

    fft();
    for (int k = 0; k <= N / 2; ++k)
        mag[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);

    const double binHz = sr / N;
    const int kLo = std::max (2, (int) std::ceil (65.0 / binHz));
    const int kHi = std::min (N / 2 - 2, (int) std::floor (5000.0 / binHz));

    double bandEnergy = 0.0, maxMag = 0.0, sumMag = 0.0;
    for (int k = kLo; k <= kHi; ++k)
    {
        const double m = mag[(size_t) k];
        bandEnergy += m * m; sumMag += m; maxMag = std::max (maxMag, m);
    }
    if (bandEnergy <= 0.0 || maxMag <= 0.0) { updateKey (true); return; }

    const double meanMag = sumMag / (kHi - kLo + 1);
    const double thresh = std::max (maxMag * 0.03, meanMag * 3.0);

    // --- spectral peak picking with parabolic interpolation --------------
    std::vector<Peak> peaks;
    peaks.reserve (256);
    double tonalEnergy = 0.0;
    for (int k = kLo; k <= kHi; ++k)
    {
        const double m = mag[(size_t) k];
        if (m > thresh && m > mag[(size_t) k - 1] && m >= mag[(size_t) k + 1])
        {
            const double a0 = std::log (mag[(size_t) k - 1] + 1e-12);
            const double a1 = std::log (m + 1e-12);
            const double a2 = std::log (mag[(size_t) k + 1] + 1e-12);
            const double den = a0 - 2.0 * a1 + a2;
            double delta = std::fabs (den) > 1e-12 ? 0.5 * (a0 - a2) / den : 0.0;
            delta = std::max (-0.5, std::min (0.5, delta));
            peaks.push_back ({ k, (k + delta) * binHz, m });
            tonalEnergy += mag[(size_t) k - 1] * mag[(size_t) k - 1] + m * m
                         + mag[(size_t) k + 1] * mag[(size_t) k + 1];
        }
    }

    if (peaks.size() > 150)
    {
        std::nth_element (peaks.begin(), peaks.begin() + 150, peaks.end(),
                          [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
        peaks.resize (150);
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.freq < b.freq; });
    }

    // --- how tonal is this frame? (noise / percussion rejection) ---------
    const double tonalness = std::min (1.0, tonalEnergy / bandEnergy);
    const double gate = 0.15 - 0.07 * (double) sensitivity;
    const double weight = std::max (0.0, std::min (1.0, (tonalness - gate) / (0.45 - gate)));

    if (weight > 0.0 && ! peaks.empty())
    {
        double maxAmp = 0.0;
        for (auto& p : peaks) maxAmp = std::max (maxAmp, p.amp);

        std::array<double, 12> fc{};
        std::vector<int> fundamentals;
        double tre = 0.0, tim = 0.0, tw = 0.0;

        for (int i = 0; i < (int) peaks.size(); ++i)
        {
            const auto& p = peaks[(size_t) i];

            // Harmonic suppression: a peak sitting at 3x, 5x, 6x or 7x a lower,
            // comparable peak is most likely that peak's overtone, not a new note.
            // (2x/4x/8x land on the same pitch class, so they are harmless.)
            bool harmonic = false;
            for (int j : fundamentals)
            {
                const double r = p.freq / peaks[(size_t) j].freq;
                const int kk = (int) std::lround (r);
                if (kk >= 3 && kk <= 7 && kk != 4
                    && std::fabs (r - kk) < 0.02 * kk
                    && p.amp <= 2.0 * peaks[(size_t) j].amp)
                { harmonic = true; break; }
            }
            if (! harmonic) fundamentals.push_back (i);

            double c = std::sqrt (p.amp / maxAmp);
            if (harmonic) c *= 0.3;
            if (p.freq < 100.0) c *= 0.6;   // kicks / sub rumble are rarely harmonic content

            const double midi = 69.0 + 12.0 * std::log2 (p.freq / 440.0);
            fc[(size_t) mod12 (std::lround (midi - tuningSemi))] += c;

            const double dev = midi - std::round (midi);
            tre += c * std::cos (2.0 * kPi * dev);
            tim += c * std::sin (2.0 * kPi * dev);
            tw += c;
        }

        const double fsum = std::accumulate (fc.begin(), fc.end(), 0.0);
        if (fsum > 0.0)
        {
            for (size_t p = 0; p < 12; ++p)
                acc[p] = acc[p] * decay + fc[p] / fsum * weight;
            accWeight = accWeight * decay + weight;
            tonalSecondsAcc += weight * kHop / sr;

            // running tuning estimate (circular mean of the cents deviation)
            tuneRe = tuneRe * decay + tre / fsum * weight;
            tuneIm = tuneIm * decay + tim / fsum * weight;
            tuneWeight = tuneWeight * decay + tw / fsum * weight;
            if (tuneWeight > 1e-6 && std::hypot (tuneRe, tuneIm) / tuneWeight > 0.3)
                tuningSemi = std::atan2 (tuneIm, tuneRe) / (2.0 * kPi);
            else
                tuningSemi = 0.0;
        }
    }
    else
    {
        // keep memory fading only when we actually accumulate; skipped frames leave it untouched
    }

    updateKey (true);
}

void KeyDetector::updateKey (bool audioFrame)
{
    res.signalPresent = silentRun < 3;
    res.tonalSeconds = (float) tonalSecondsAcc;
    res.tuningCents = (float) (tuningSemi * 100.0);

    if (! audioFrame && silentRun >= 3)
    {
        res.status = Status::Listening;
        return;
    }

    const double audioSeconds = (double) audioFrames * kHop / sr;

    // normalised chroma for display and note presence
    double mx = 0.0;
    for (double v : acc) mx = std::max (mx, v);
    for (size_t p = 0; p < 12; ++p)
    {
        res.chroma[p] = mx > 0.0 ? (float) (acc[p] / mx) : 0.0f;
        res.notePresent[p] = res.chroma[p] >= 0.35f;
    }

    const bool enough = tonalSecondsAcc >= 2.0 && mx > 0.0;
    if (! enough)
    {
        haveKey = false;
        res.hasKey = false;
        res.numAlternatives = 0;
        res.confidence = 0.0f;
        res.status = (audioSeconds > 8.0 && tonalSecondsAcc < 2.0) ? Status::Insufficient
                   : (audioFrames > 0 ? Status::Analyzing : Status::Listening);
        return;
    }

    double r[24];
    double prof[12];
    for (int t = 0; t < 12; ++t)
    {
        for (int i = 0; i < 12; ++i) prof[i] = kMajorProfile[mod12 (i - t)];
        r[t] = pearson (acc.data(), prof);
        for (int i = 0; i < 12; ++i) prof[i] = kMinorProfile[mod12 (i - t)];
        r[12 + t] = pearson (acc.data(), prof);
    }

    int order[24];
    std::iota (order, order + 24, 0);
    std::sort (order, order + 24, [&] (int a, int b) { return r[a] > r[b]; });
    const int best = order[0];

    if (! haveKey)
    {
        haveKey = true; curIdx = best; candIdx = -1; candStreak = 0; stableFrames = 0;
    }
    else if (best == curIdx)
    {
        candIdx = -1; candStreak = 0; ++stableFrames;
    }
    else
    {
        if (best == candIdx) ++candStreak; else { candIdx = best; candStreak = 1; }
        stableFrames = 0;
        if (candStreak >= 10 || (r[best] - r[curIdx] > 0.10 && candStreak >= 3))
        {
            curIdx = best; candIdx = -1; candStreak = 0;
        }
    }

    // best competitor to the current key
    int second = (order[0] == curIdx) ? order[1] : order[0];

    const double r1 = r[curIdx], r2 = r[second];
    const double sharp = std::max (0.0, std::min (1.0, (r1 - 0.4) / 0.5));
    const double margin = std::max (0.0, std::min (1.0, (r1 - r2) / 0.12));
    const double maturity = std::max (0.3, std::min (1.0, tonalSecondsAcc / 8.0));
    const double conf = (0.6 * sharp + 0.4 * margin) * maturity;

    res.hasKey = true;
    res.tonic = curIdx % 12;
    res.minor = curIdx >= 12;
    res.confidence = (float) conf;

    res.numAlternatives = 0;
    for (int i = 0; i < 24 && res.numAlternatives < 3; ++i)
    {
        const int idx = order[i];
        if (idx == curIdx) continue;
        if (r[idx] < r1 - 0.20) break;
        res.alternatives[(size_t) res.numAlternatives++] = { idx % 12, idx >= 12, (float) r[idx] };
    }

    if (conf < 0.15)
        res.status = Status::Insufficient;
    else if (stableFrames >= 8 && tonalSecondsAcc >= 4.0 && conf >= 0.3)
        res.status = Status::Stable;
    else
        res.status = Status::Analyzing;
}

const char* KeyDetector::noteName (int pc) { return kNames[mod12 (pc)]; }

std::string KeyDetector::keyName (int tonic, bool minor)
{
    return std::string (noteName (tonic)) + (minor ? " Minor" : " Major");
}
} // namespace sk
