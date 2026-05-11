#include "MusicAnalysis.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace stemmerizer::dsp
{

namespace
{
    // ---- shared helpers ----------------------------------------------------

    // Downmix to mono into `out`. Optionally restrict to the middle `maxSeconds`
    // of audio (BPM-only optimization; long files don't benefit from more).
    // Returns the actual frame count written to `out`.
    long long downmixToMono (const float* interleaved,
                             long long    numFrames,
                             int          numChannels,
                             int          sampleRate,
                             double       maxSeconds,
                             std::vector<float>& out)
    {
        if (interleaved == nullptr || numFrames <= 0 || numChannels <= 0)
        {
            out.clear();
            return 0;
        }

        long long startFrame = 0;
        long long endFrame   = numFrames;
        if (maxSeconds > 0.0)
        {
            const long long maxFrames = (long long) (maxSeconds * (double) sampleRate);
            if (numFrames > maxFrames)
            {
                startFrame = (numFrames - maxFrames) / 2;
                endFrame   = startFrame + maxFrames;
            }
        }

        const long long n = endFrame - startFrame;
        out.resize ((size_t) n);

        const float scale = 1.f / (float) numChannels;
        for (long long i = 0; i < n; ++i)
        {
            const float* sampleStart = interleaved + (size_t) ((startFrame + i) * numChannels);
            float acc = 0.f;
            for (int c = 0; c < numChannels; ++c)
                acc += sampleStart[c];
            out[(size_t) i] = acc * scale;
        }
        return n;
    }

    // Hann window cached lazily for a given size.
    void fillHann (std::vector<float>& w, int n)
    {
        if ((int) w.size() == n) return;
        w.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            w[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * juce::MathConstants<float>::pi
                                                   * (float) i / (float) (n - 1));
    }

    // ---- BPM ---------------------------------------------------------------

    std::vector<float> computeOnsetEnvelope (const float* mono,
                                             long long    nFrames,
                                             int          fftOrder,
                                             int          hopSize)
    {
        const int fftSize = 1 << fftOrder;
        if (nFrames < fftSize) return {};

        juce::dsp::FFT fft (fftOrder);

        std::vector<float> window;
        fillHann (window, fftSize);

        // performRealOnlyForwardTransform requires a buffer of 2*fftSize.
        std::vector<float> fftBuf ((size_t) fftSize * 2, 0.f);

        const int numBins = fftSize / 2 + 1;
        std::vector<float> prevMag ((size_t) numBins, 0.f);
        std::vector<float> curMag  ((size_t) numBins, 0.f);

        const long long numFramesHop = (nFrames - fftSize) / hopSize + 1;
        if (numFramesHop <= 1) return {};

        std::vector<float> env ((size_t) numFramesHop, 0.f);

        for (long long f = 0; f < numFramesHop; ++f)
        {
            const long long start = f * hopSize;
            std::fill (fftBuf.begin(), fftBuf.end(), 0.f);
            for (int i = 0; i < fftSize; ++i)
                fftBuf[(size_t) i] = mono[(size_t) (start + i)] * window[(size_t) i];

            fft.performRealOnlyForwardTransform (fftBuf.data(), true);

            // After real-only FFT, bins 0..numBins-1 are interleaved (re, im).
            float flux = 0.f;
            for (int b = 0; b < numBins; ++b)
            {
                const float re = fftBuf[(size_t) (2 * b)];
                const float im = fftBuf[(size_t) (2 * b + 1)];
                const float mag = std::sqrt (re * re + im * im);
                curMag[(size_t) b] = mag;
                const float diff = mag - prevMag[(size_t) b];
                if (diff > 0.f) flux += diff;
            }
            env[(size_t) f] = flux;
            std::swap (prevMag, curMag);
        }
        return env;
    }

    // Subtract running mean (window = halfWindow*2+1) and half-wave rectify.
    void detrend (std::vector<float>& env, int halfWindow)
    {
        const int n = (int) env.size();
        if (n == 0) return;

        std::vector<float> out ((size_t) n, 0.f);
        for (int i = 0; i < n; ++i)
        {
            const int lo = std::max (0, i - halfWindow);
            const int hi = std::min (n - 1, i + halfWindow);
            float sum = 0.f;
            for (int j = lo; j <= hi; ++j) sum += env[(size_t) j];
            const float mean = sum / (float) (hi - lo + 1);
            const float v = env[(size_t) i] - mean;
            out[(size_t) i] = v > 0.f ? v : 0.f;
        }
        env.swap (out);
    }
}

std::optional<BpmResult> analyzeBpm (const float* interleaved,
                                     long long   numFrames,
                                     int         numChannels,
                                     int         sampleRate)
{
    if (interleaved == nullptr || numFrames <= 0 || numChannels <= 0 || sampleRate <= 0)
        return std::nullopt;

    // Limit analysis to the middle 60 seconds for speed.
    std::vector<float> mono;
    const long long nMono = downmixToMono (interleaved, numFrames, numChannels,
                                           sampleRate, 60.0, mono);
    if (nMono < (long long) sampleRate) // < 1 second of audio
        return std::nullopt;

    // Onset envelope at native rate. Hop=512 / FFT=1024 gives ~46 ms hop at
    // ~22 kHz, ~12 ms at 44.1 kHz — fine resolution for the 60..200 BPM range.
    constexpr int kFftOrder = 10;          // 1024
    constexpr int kHopSize  = 512;
    auto env = computeOnsetEnvelope (mono.data(), nMono, kFftOrder, kHopSize);
    if (env.size() < 32) return std::nullopt;

    detrend (env, 6);

    // Autocorrelation (one-sided) of the onset envelope. The envelope is
    // ~zero-mean and non-negative after detrend; we want raw autocorrelation
    // over the BPM-relevant lag range.
    const float envHz = (float) sampleRate / (float) kHopSize;
    const int lagMin = std::max (1, (int) std::floor (envHz * 60.f / 200.f)); // 200 BPM
    const int lagMax = std::min ((int) env.size() - 1,
                                 (int) std::ceil  (envHz * 60.f / 60.f));     // 60 BPM
    if (lagMax <= lagMin + 2) return std::nullopt;

    std::vector<float> acf ((size_t) (lagMax - lagMin + 1), 0.f);
    const int N = (int) env.size();
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        float s = 0.f;
        const int top = N - lag;
        for (int i = 0; i < top; ++i)
            s += env[(size_t) i] * env[(size_t) (i + lag)];
        acf[(size_t) (lag - lagMin)] = s;
    }

    // Find peak.
    int bestIdx = 0;
    float bestVal = acf[0];
    for (int i = 1; i < (int) acf.size(); ++i)
    {
        if (acf[(size_t) i] > bestVal) { bestVal = acf[(size_t) i]; bestIdx = i; }
    }

    if (bestVal <= 0.f) return std::nullopt;

    const float meanVal = std::accumulate (acf.begin(), acf.end(), 0.f) / (float) acf.size();
    if (meanVal <= 0.f) return std::nullopt;
    const float conf = bestVal / meanVal;

    // Conservative threshold: peak must be ~1.8x the mean of the lag range.
    constexpr float kBpmConfThreshold = 1.8f;
    if (conf < kBpmConfThreshold) return std::nullopt;

    // Parabolic refinement around the peak for sub-sample lag accuracy.
    float lagF = (float) (bestIdx + lagMin);
    if (bestIdx > 0 && bestIdx < (int) acf.size() - 1)
    {
        const float y0 = acf[(size_t) (bestIdx - 1)];
        const float y1 = acf[(size_t)  bestIdx     ];
        const float y2 = acf[(size_t) (bestIdx + 1)];
        const float denom = (y0 - 2.f * y1 + y2);
        if (std::abs (denom) > 1e-9f)
        {
            const float delta = 0.5f * (y0 - y2) / denom;
            if (delta > -1.f && delta < 1.f)
                lagF += delta;
        }
    }

    const float bpm = 60.f * envHz / lagF;
    if (bpm < 55.f || bpm > 210.f) return std::nullopt;

    BpmResult r;
    r.bpm        = bpm;
    r.confidence = juce::jlimit (0.f, 1.f, (conf - kBpmConfThreshold) / 4.f);
    return r;
}

// ---- Key -------------------------------------------------------------------

namespace
{
    // Krumhansl-Kessler tonal hierarchy profiles.
    constexpr std::array<float, 12> kMajorProfile = {
        6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
        2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr std::array<float, 12> kMinorProfile = {
        6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
        2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float pearson (const std::array<float, 12>& a, const std::array<float, 12>& b)
    {
        const float meanA = std::accumulate (a.begin(), a.end(), 0.f) / 12.f;
        const float meanB = std::accumulate (b.begin(), b.end(), 0.f) / 12.f;
        float num = 0.f, dA = 0.f, dB = 0.f;
        for (size_t i = 0; i < 12; ++i)
        {
            const float xa = a[i] - meanA;
            const float xb = b[i] - meanB;
            num += xa * xb;
            dA  += xa * xa;
            dB  += xb * xb;
        }
        const float denom = std::sqrt (dA * dB);
        return denom > 1e-12f ? num / denom : 0.f;
    }
}

std::optional<KeyResult> analyzeKey (const float* interleaved,
                                     long long   numFrames,
                                     int         numChannels,
                                     int         sampleRate)
{
    if (interleaved == nullptr || numFrames <= 0 || numChannels <= 0 || sampleRate <= 0)
        return std::nullopt;

    // Use the whole file (or limit if huge — 6 minutes is plenty).
    std::vector<float> mono;
    const long long nMono = downmixToMono (interleaved, numFrames, numChannels,
                                           sampleRate, 360.0, mono);
    if (nMono < (long long) sampleRate)
        return std::nullopt;

    constexpr int kFftOrder = 13;       // 8192
    const int fftSize = 1 << kFftOrder;
    constexpr int kHop = 4096;
    if (nMono < fftSize) return std::nullopt;

    juce::dsp::FFT fft (kFftOrder);

    std::vector<float> window;
    fillHann (window, fftSize);

    std::vector<float> fftBuf ((size_t) fftSize * 2, 0.f);
    std::array<float, 12> chroma { 0,0,0,0,0,0,0,0,0,0,0,0 };

    const int numBins = fftSize / 2 + 1;
    const float binHz = (float) sampleRate / (float) fftSize;
    const int binLo = std::max (1, (int) std::ceil  (50.f   / binHz));
    const int binHi = std::min (numBins - 1, (int) std::floor (5000.f / binHz));

    const long long numFramesHop = (nMono - fftSize) / kHop + 1;
    if (numFramesHop <= 0) return std::nullopt;

    for (long long f = 0; f < numFramesHop; ++f)
    {
        const long long start = f * kHop;
        std::fill (fftBuf.begin(), fftBuf.end(), 0.f);
        for (int i = 0; i < fftSize; ++i)
            fftBuf[(size_t) i] = mono[(size_t) (start + i)] * window[(size_t) i];

        fft.performRealOnlyForwardTransform (fftBuf.data(), true);

        for (int b = binLo; b <= binHi; ++b)
        {
            const float re = fftBuf[(size_t) (2 * b)];
            const float im = fftBuf[(size_t) (2 * b + 1)];
            const float mag = std::sqrt (re * re + im * im);
            if (mag <= 0.f) continue;

            const float freq = (float) b * binHz;
            // pc = round(12 * log2(freq / 440)) mod 12, with A=9.
            const float midi = 12.f * std::log2 (freq / 440.f);
            int pc = (int) std::lround (midi) % 12;
            pc = ((pc % 12) + 12) % 12;  // ensure non-negative
            pc = (pc + 9) % 12;          // shift so A->9, A+1->10, ... C->0
            chroma[(size_t) pc] += mag;
        }
    }

    // Normalize to sum=1.
    const float sum = std::accumulate (chroma.begin(), chroma.end(), 0.f);
    if (sum <= 0.f) return std::nullopt;
    for (auto& v : chroma) v /= sum;

    // Correlate against 24 rotated templates.
    float bestCorr = -2.f, secondCorr = -2.f;
    int bestRoot = 0; bool bestMinor = false;

    for (int root = 0; root < 12; ++root)
    {
        std::array<float, 12> majR{}, minR{};
        for (int i = 0; i < 12; ++i)
        {
            majR[(size_t) i] = kMajorProfile[(size_t) ((i - root + 12) % 12)];
            minR[(size_t) i] = kMinorProfile[(size_t) ((i - root + 12) % 12)];
        }
        const float cMaj = pearson (chroma, majR);
        const float cMin = pearson (chroma, minR);

        if (cMaj > bestCorr) { secondCorr = bestCorr; bestCorr = cMaj; bestRoot = root; bestMinor = false; }
        else if (cMaj > secondCorr) { secondCorr = cMaj; }

        if (cMin > bestCorr) { secondCorr = bestCorr; bestCorr = cMin; bestRoot = root; bestMinor = true; }
        else if (cMin > secondCorr) { secondCorr = cMin; }
    }

    const float gap = bestCorr - secondCorr;
    constexpr float kKeyGapThreshold = 0.05f;
    if (gap < kKeyGapThreshold) return std::nullopt;

    KeyResult k;
    k.rootPitchClass = bestRoot;
    k.isMinor        = bestMinor;
    k.confidence     = juce::jlimit (0.f, 1.f, gap);
    return k;
}

juce::String keyName (const std::optional<KeyResult>& k)
{
    if (! k.has_value()) return {};
    static const char* names[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B" };
    const int pc = juce::jlimit (0, 11, k->rootPitchClass);
    return juce::String (names[pc]) + (k->isMinor ? " minor" : " major");
}

} // namespace stemmerizer::dsp
