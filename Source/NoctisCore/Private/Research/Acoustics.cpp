#include "Noctis/Research/Acoustics.h"

#include "Noctis/Core/Random.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace noctis
{
namespace acousticsimpl
{
constexpr float kRefPa = 20e-6f;

float pascalsPeakAt1m(float levelDb) { return kRefPa * std::pow(10.0f, levelDb / 20.0f) * 1.41421356f; }

// One-pole filters.
struct OnePole
{
    float a = 0.0f;
    float z = 0.0f;
    void lowpass(float cutoffHz, int sr) { a = std::exp(-kTwoPi * cutoffHz / static_cast<float>(sr)); }
    float lp(float x)
    {
        z = (1.0f - a) * x + a * z;
        return z;
    }
};

float envelope(float t, float dur, float attack, float release)
{
    if (t < 0.0f || t > dur)
    {
        return 0.0f;
    }
    const float a = std::max(1e-3f, attack * dur);
    const float r = std::max(1e-3f, release * dur);
    if (t < a)
    {
        return t / a;
    }
    if (t > dur - r)
    {
        return std::max(0.0f, (dur - t) / r);
    }
    return 1.0f;
}

// Frequency contour of hypothetical call types (relative to f0). These shapes are game hypotheses
// built on closed-mouth vocalisation in extant archosaurs; see species vocal claims.
float contour(CallContext c, float u)
{
    switch (c)
    {
    case CallContext::Alarm: return 1.0f + 0.25f * std::sin(kPi * u); // short rise–fall
    case CallContext::Distress: return 1.3f + 0.15f * std::sin(kTwoPi * 3.0f * u);
    case CallContext::Threat: return 0.85f - 0.1f * u;
    case CallContext::Courtship: return 1.0f + 0.08f * std::sin(kTwoPi * 0.7f * u);
    case CallContext::Begging: return 1.6f + 0.2f * u;
    case CallContext::Territorial: return 0.9f + 0.05f * std::sin(kPi * u);
    case CallContext::Contact:
    case CallContext::Other: return 1.0f + 0.06f * std::sin(kPi * u);
    }
    return 1.0f;
}

size_t nextPow2(size_t n)
{
    size_t p = 1;
    while (p < n)
    {
        p <<= 1u;
    }
    return p;
}
} // namespace acousticsimpl

void SignalSynth::render(const SoundSource& src, const EvidenceDatabase* db, double bufferStart, int sampleRate, float distanceM, float gainExtraDb,
                         const PropagationEnv& env, std::vector<float>& buffer)
{
    using namespace acousticsimpl;
    (void)db;
    const float c = AcousticAnalysis::speedOfSound(env.temperatureC);
    const float d = std::max(1.0f, distanceM);
    const double arrival = src.startTime + static_cast<double>(d / c);
    const long n0 = static_cast<long>(std::llround((arrival - bufferStart) * sampleRate));
    const float dur = std::max(0.02f, src.durationS);
    const long nDur = static_cast<long>(dur * static_cast<float>(sampleRate));
    const long len = static_cast<long>(buffer.size());
    if (n0 + nDur < 0 || n0 >= len)
    {
        return;
    }
    const float amp = pascalsPeakAt1m(src.levelDb) / d * std::pow(10.0f, gainExtraDb / 20.0f);
    Rng rng(src.seed ? src.seed : static_cast<u64>(src.id) * 7919u, 0x5A11);
    const float sr = static_cast<float>(sampleRate);
    const float nyquist = 0.45f * sr;
    auto absorb = [&](float f) { return std::pow(10.0f, -SoundField::absorptionDbPerM(f, env.temperatureC, env.humidity) * d / 20.0f); };
    const long iStart = std::max(0L, n0);
    const long iEnd = std::min(len, n0 + nDur);

    switch (src.kind)
    {
    case SoundKind::Vocalization:
    case SoundKind::Voice:
    {
        const bool voice = src.kind == SoundKind::Voice;
        const float f0 = std::max(5.0f, src.f0Hz * src.pitchShift);
        const CallContext ctxCall = static_cast<CallContext>(src.callContext);
        const int harmonics = voice ? 12 : 8;
        float weights[16];
        float phases[16];
        float absorption[16];
        for (int h = 1; h <= harmonics; ++h)
        {
            // Closed-mouth calls are close to sinusoidal: steep harmonic roll-off.
            weights[h - 1] = voice ? 1.0f / static_cast<float>(h) : std::pow(static_cast<float>(h), -2.5f);
            phases[h - 1] = rng.range(0.0f, kTwoPi);
            absorption[h - 1] = absorb(f0 * static_cast<float>(h));
        }
        float phase = 0.0f;
        const float jitter = ctxCall == CallContext::Distress ? 0.04f : 0.008f;
        const float amHz = ctxCall == CallContext::Threat ? 12.0f : 0.0f;
        for (long i = iStart; i < iEnd; ++i)
        {
            const float t = static_cast<float>(i - n0) / sr;
            const float u = t / dur;
            const float f = f0 * (voice ? 1.0f + 0.1f * std::sin(kTwoPi * 2.0f * u) : contour(ctxCall, u)) * (1.0f + jitter * rng.normal());
            phase += kTwoPi * f / sr;
            float s = 0.0f;
            for (int h = 1; h <= harmonics; ++h)
            {
                if (f * static_cast<float>(h) > nyquist)
                {
                    break;
                }
                s += weights[h - 1] * absorption[h - 1] * std::sin(static_cast<float>(h) * phase + phases[h - 1]);
            }
            float e = envelope(t, dur, 0.12f, 0.25f);
            if (amHz > 0.0f)
            {
                e *= 0.65f + 0.35f * std::sin(kTwoPi * amHz * t);
            }
            buffer[static_cast<size_t>(i)] += amp * e * s;
        }
        break;
    }
    case SoundKind::Footfall:
    case SoundKind::Impact:
    {
        const float f0 = std::max(10.0f, src.f0Hz);
        OnePole lp;
        lp.lowpass(f0 * 2.5f, sampleRate);
        const float ab = absorb(f0);
        for (long i = iStart; i < iEnd; ++i)
        {
            const float t = static_cast<float>(i - n0) / sr;
            const float decay = std::exp(-t / 0.06f);
            const float thump = std::sin(kTwoPi * f0 * t) * decay;
            const float grit = lp.lp(rng.normal()) * std::exp(-t / 0.03f) * 0.5f;
            buffer[static_cast<size_t>(i)] += amp * ab * (thump + grit);
        }
        break;
    }
    case SoundKind::Splash:
    case SoundKind::VegetationCrash:
    case SoundKind::Mechanical:
    {
        OnePole lp;
        lp.lowpass(src.kind == SoundKind::Mechanical ? 6000.0f : 2500.0f, sampleRate);
        OnePole hp;
        hp.lowpass(src.kind == SoundKind::Mechanical ? 1500.0f : 250.0f, sampleRate);
        const float ab = absorb(src.kind == SoundKind::Mechanical ? 3000.0f : 1000.0f);
        for (long i = iStart; i < iEnd; ++i)
        {
            const float t = static_cast<float>(i - n0) / sr;
            const float x = rng.normal();
            const float band = lp.lp(x) - hp.lp(x);
            buffer[static_cast<size_t>(i)] += amp * ab * band * envelope(t, dur, 0.02f, 0.7f) * 2.0f;
        }
        break;
    }
    case SoundKind::Engine:
    case SoundKind::Rotor:
    {
        const float f0 = std::max(10.0f, src.f0Hz);
        const int harmonics = src.kind == SoundKind::Rotor ? 6 : 4;
        OnePole lp;
        lp.lowpass(src.kind == SoundKind::Rotor ? 3000.0f : 600.0f, sampleRate);
        float absorption[8];
        for (int h = 1; h <= harmonics; ++h)
        {
            absorption[h - 1] = absorb(f0 * static_cast<float>(h));
        }
        for (long i = iStart; i < iEnd; ++i)
        {
            const float t = static_cast<float>(i - n0) / sr;
            float s = 0.0f;
            for (int h = 1; h <= harmonics; ++h)
            {
                s += absorption[h - 1] / static_cast<float>(h) * std::sin(kTwoPi * f0 * static_cast<float>(h) * t);
            }
            s += 0.35f * lp.lp(rng.normal());
            buffer[static_cast<size_t>(i)] += amp * 0.6f * s * envelope(t, dur, 0.02f, 0.02f);
        }
        break;
    }
    case SoundKind::Thunder:
    {
        OnePole lp;
        lp.lowpass(180.0f, sampleRate);
        const float ab = absorb(80.0f);
        for (long i = iStart; i < iEnd; ++i)
        {
            const float t = static_cast<float>(i - n0) / sr;
            const float e = (t < 0.05f ? t / 0.05f : std::exp(-(t - 0.05f) / (0.35f * dur))) * (0.7f + 0.3f * std::sin(kTwoPi * 2.3f * t));
            buffer[static_cast<size_t>(i)] += amp * ab * lp.lp(rng.normal()) * 3.0f * e;
        }
        break;
    }
    }
}

void SignalSynth::addNoise(std::vector<float>& buffer, int sampleRate, float levelDb, float lowpassHz, float highpassHz, u64 seed)
{
    using namespace acousticsimpl;
    if (levelDb <= 0.0f || buffer.empty())
    {
        return;
    }
    std::vector<float> n(buffer.size());
    Rng rng(seed, 0x0153);
    OnePole lp;
    lp.lowpass(std::max(10.0f, lowpassHz), sampleRate);
    OnePole hp;
    hp.lowpass(std::max(1.0f, highpassHz), sampleRate);
    double sum = 0.0;
    for (size_t i = 0; i < n.size(); ++i)
    {
        const float x = rng.normal();
        const float y = lp.lp(x) - (highpassHz > 0.0f ? hp.lp(x) : 0.0f);
        n[i] = y;
        sum += static_cast<double>(y) * static_cast<double>(y);
    }
    const float rms = static_cast<float>(std::sqrt(sum / static_cast<double>(n.size())));
    const float target = kRefPa * std::pow(10.0f, levelDb / 20.0f);
    const float g = rms > 0.0f ? target / rms : 0.0f;
    for (size_t i = 0; i < n.size(); ++i)
    {
        buffer[i] += n[i] * g;
    }
}

Recording AcousticRecorder::record(const WorldContext& ctx, const MicSpec& mic, const Vec3& micPosition, float micHeading, double t0, float durationS,
                                   u32 id, u64 seed, const std::vector<SoundSource>* capturedSources)
{
    Recording r;
    r.id = id;
    r.startTime = t0;
    r.durationS = durationS;
    r.sampleRate = mic.sampleRate;
    r.micPosition = micPosition;
    r.micHeading = micHeading;
    r.mic = mic;
    r.samples.assign(static_cast<size_t>(durationS * static_cast<float>(mic.sampleRate)), 0.0f);
    const PropagationEnv env = ctx.propagation();
    std::vector<SoundSource> collected;
    if (!capturedSources)
    {
        ctx.sound->collect(t0 - 18.0, t0 + durationS, collected);
    }
    const std::vector<SoundSource>& sources = capturedSources ? *capturedSources : collected;
    for (const SoundSource& s : sources)
    {
        const float d = distance(s.position, micPosition);
        if (d > 7000.0f)
        {
            continue;
        }
        const float received = SoundField::receivedLevelDb(s, micPosition, env);
        if (received < -15.0f)
        {
            continue;
        }
        // Directivity: on-axis gain, off-axis rejection.
        float dirGain = 0.0f;
        if (mic.directivityGainDb > 0.0f)
        {
            const float off = std::fabs(angleDelta(micHeading, (s.position.xy() - micPosition.xy()).heading()));
            const float halfBeam = 0.5f * mic.beamWidthDeg * kDegToRad;
            dirGain = off < halfBeam ? mic.directivityGainDb * std::cos(off / halfBeam * kHalfPi * 0.5f)
                                     : -std::min(15.0f, mic.directivityGainDb * (off - halfBeam) / kHalfPi);
        }
        const float spreadingAndF0 = s.levelDb - 20.0f * std::log10(std::max(1.0f, d)) -
                                     SoundField::absorptionDbPerM(s.f0Hz * s.pitchShift, env.temperatureC, env.humidity) * d;
        const float extra = received - spreadingAndF0 + dirGain;
        SignalSynth::render(s, ctx.db, t0, r.sampleRate, d, extra, env, r.samples);
        r.truthSourceIds.push_back(s.id);
    }
    // Ambient noise: wind (pink-ish, low frequency), rain (broadband), moving water, self-noise.
    const WeatherState& w = ctx.weather->state();
    SignalSynth::addNoise(r.samples, r.sampleRate, w.windNoiseDb + 4.0f, 700.0f, 0.0f, seed ^ 0x11);
    SignalSynth::addNoise(r.samples, r.sampleRate, w.rainNoiseDb, 6000.0f, 400.0f, seed ^ 0x22);
    if (ctx.water)
    {
        SignalSynth::addNoise(r.samples, r.sampleRate, ctx.water->noiseLevelDb(micPosition.xy()), 2000.0f, 120.0f, seed ^ 0x33);
    }
    SignalSynth::addNoise(r.samples, r.sampleRate, mic.selfNoiseDb, 0.45f * static_cast<float>(r.sampleRate), 20.0f, seed ^ 0x44);
    return r;
}

void AcousticAnalysis::fft(std::vector<std::complex<float>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1u;
        for (; j & bit; bit >>= 1u)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1u)
    {
        const float ang = kTwoPi / static_cast<float>(len) * (inverse ? 1.0f : -1.0f);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
    {
        for (auto& x : a)
        {
            x /= static_cast<float>(n);
        }
    }
}

Spectrogram AcousticAnalysis::spectrogram(const Recording& r, int fftSize, int hop)
{
    Spectrogram s;
    const int n = static_cast<int>(r.samples.size());
    if (n < fftSize)
    {
        return s;
    }
    s.frames = (n - fftSize) / hop + 1;
    s.bins = fftSize / 2;
    s.hopS = static_cast<float>(hop) / static_cast<float>(r.sampleRate);
    s.binHz = static_cast<float>(r.sampleRate) / static_cast<float>(fftSize);
    s.db.assign(static_cast<size_t>(s.frames) * static_cast<size_t>(s.bins), 0.0f);
    std::vector<float> window(static_cast<size_t>(fftSize));
    float wsum = 0.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fftSize - 1));
        wsum += window[static_cast<size_t>(i)];
    }
    std::vector<std::complex<float>> buf(static_cast<size_t>(fftSize));
    for (int f = 0; f < s.frames; ++f)
    {
        const int off = f * hop;
        for (int i = 0; i < fftSize; ++i)
        {
            buf[static_cast<size_t>(i)] = {r.samples[static_cast<size_t>(off + i)] * window[static_cast<size_t>(i)], 0.0f};
        }
        fft(buf, false);
        for (int b = 0; b < s.bins; ++b)
        {
            const float ampPeak = 2.0f * std::abs(buf[static_cast<size_t>(b)]) / wsum;
            const float rms = ampPeak / 1.41421356f;
            s.db[static_cast<size_t>(f) * static_cast<size_t>(s.bins) + static_cast<size_t>(b)] = 20.0f * std::log10(std::max(1e-9f, rms / acousticsimpl::kRefPa));
        }
    }
    return s;
}

std::vector<AcousticEvent> AcousticAnalysis::detectEvents(const Recording& r, const Spectrogram& s, float thresholdDb, float minHz, float maxHz)
{
    std::vector<AcousticEvent> events;
    if (s.frames == 0)
    {
        return events;
    }
    const int b0 = std::max(1, static_cast<int>(minHz / s.binHz));
    const int b1 = std::min(s.bins - 1, static_cast<int>(maxHz / s.binHz));
    // Per-frame band level (power sum).
    std::vector<float> level(static_cast<size_t>(s.frames));
    for (int f = 0; f < s.frames; ++f)
    {
        double p = 0.0;
        for (int b = b0; b <= b1; ++b)
        {
            p += std::pow(10.0, s.at(f, b) / 10.0);
        }
        level[static_cast<size_t>(f)] = static_cast<float>(10.0 * std::log10(std::max(1e-12, p)));
    }
    std::vector<float> sorted = level;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const float noise = sorted[sorted.size() / 2];
    // Per-bin noise floor (median over frames) for spectral peak picking.
    std::vector<float> binNoise(static_cast<size_t>(s.bins), 0.0f);
    {
        std::vector<float> col(static_cast<size_t>(s.frames));
        for (int b = b0; b <= b1; ++b)
        {
            for (int f = 0; f < s.frames; ++f)
            {
                col[static_cast<size_t>(f)] = s.at(f, b);
            }
            std::nth_element(col.begin(), col.begin() + static_cast<std::ptrdiff_t>(col.size() / 2), col.end());
            binNoise[static_cast<size_t>(b)] = col[col.size() / 2];
        }
    }
    const float on = noise + thresholdDb;
    const float off = noise + thresholdDb - 3.0f;
    int start = -1;
    std::vector<std::pair<int, int>> segs;
    for (int f = 0; f < s.frames; ++f)
    {
        const float l = level[static_cast<size_t>(f)];
        if (start < 0 && l > on)
        {
            start = f;
        }
        else if (start >= 0 && l < off)
        {
            segs.push_back({start, f - 1});
            start = -1;
        }
    }
    if (start >= 0)
    {
        segs.push_back({start, s.frames - 1});
    }
    // Merge short gaps.
    std::vector<std::pair<int, int>> merged;
    const int gapFrames = std::max(1, static_cast<int>(0.15f / s.hopS));
    for (const auto& sg : segs)
    {
        if (!merged.empty() && sg.first - merged.back().second <= gapFrames)
        {
            merged.back().second = sg.second;
        }
        else
        {
            merged.push_back(sg);
        }
    }
    for (const auto& sg : merged)
    {
        const float dur = static_cast<float>(sg.second - sg.first + 1) * s.hopS;
        if (dur < 0.06f)
        {
            continue;
        }
        AcousticEvent e;
        e.startS = static_cast<float>(sg.first) * s.hopS;
        e.endS = static_cast<float>(sg.second + 1) * s.hopS;
        e.durationS = e.endS - e.startS;
        // Mean excess spectrum over the event.
        std::vector<double> spec(static_cast<size_t>(s.bins), 0.0);
        float peakL = -1e9f;
        double rmsP = 0.0;
        std::vector<float> framePeakHz;
        for (int f = sg.first; f <= sg.second; ++f)
        {
            peakL = std::max(peakL, level[static_cast<size_t>(f)]);
            rmsP += std::pow(10.0, level[static_cast<size_t>(f)] / 10.0);
            int best = b0;
            for (int b = b0; b <= b1; ++b)
            {
                const double p = std::pow(10.0, s.at(f, b) / 10.0);
                spec[static_cast<size_t>(b)] += p;
                if (s.at(f, b) - binNoise[static_cast<size_t>(b)] > s.at(f, best) - binNoise[static_cast<size_t>(best)])
                {
                    best = b;
                }
            }
            framePeakHz.push_back(static_cast<float>(best) * s.binHz);
        }
        int peakBin = b0;
        double total = 0.0;
        for (int b = b0; b <= b1; ++b)
        {
            const double excess = spec[static_cast<size_t>(b)] / (sg.second - sg.first + 1) / std::pow(10.0, binNoise[static_cast<size_t>(b)] / 10.0);
            if (excess > spec[static_cast<size_t>(peakBin)] / (sg.second - sg.first + 1) / std::pow(10.0, binNoise[static_cast<size_t>(peakBin)] / 10.0))
            {
                peakBin = b;
            }
            total += spec[static_cast<size_t>(b)];
        }
        e.peakFreqHz = static_cast<float>(peakBin) * s.binHz;
        // Harmonic product spectrum for the fundamental (3 harmonics).
        double bestH = -1.0;
        int f0Bin = peakBin;
        for (int b = b0; b * 3 <= b1; ++b)
        {
            const double h = std::log(spec[static_cast<size_t>(b)] + 1e-30) + std::log(spec[static_cast<size_t>(2 * b)] + 1e-30) * 0.6 +
                             std::log(spec[static_cast<size_t>(3 * b)] + 1e-30) * 0.3;
            if (bestH < -0.5 || h > bestH)
            {
                bestH = h;
                f0Bin = b;
            }
        }
        // Prefer the spectral peak when the HPS candidate is much weaker (near-sinusoidal calls).
        if (spec[static_cast<size_t>(f0Bin)] < spec[static_cast<size_t>(peakBin)] * 0.1)
        {
            f0Bin = peakBin;
        }
        e.f0Hz = static_cast<float>(f0Bin) * s.binHz;
        // -10 dB bandwidth around the peak.
        const double thr = spec[static_cast<size_t>(peakBin)] * 0.1;
        int lo = peakBin;
        int hi = peakBin;
        while (lo > b0 && spec[static_cast<size_t>(lo - 1)] > thr)
        {
            --lo;
        }
        while (hi < b1 && spec[static_cast<size_t>(hi + 1)] > thr)
        {
            ++hi;
        }
        e.bandwidthHz = static_cast<float>(hi - lo + 1) * s.binHz;
        e.peakLevelDb = peakL;
        e.rmsLevelDb = static_cast<float>(10.0 * std::log10(rmsP / (sg.second - sg.first + 1)));
        e.snrDb = peakL - noise;
        double harm = 0.0;
        for (int h = 1; h <= 6; ++h)
        {
            const int hb = f0Bin * h;
            for (int b = std::max(b0, hb - 1); b <= std::min(b1, hb + 1); ++b)
            {
                harm += spec[static_cast<size_t>(b)];
            }
        }
        e.harmonicity = total > 0.0 ? static_cast<float>(std::min(1.0, harm / total)) : 0.0f;
        // FM slope by least squares on per-frame peaks.
        if (framePeakHz.size() > 2)
        {
            const float nF = static_cast<float>(framePeakHz.size());
            float st = 0.0f;
            float sf = 0.0f;
            float stt = 0.0f;
            float stf = 0.0f;
            for (size_t i = 0; i < framePeakHz.size(); ++i)
            {
                const float t = static_cast<float>(i) * s.hopS;
                st += t;
                sf += framePeakHz[i];
                stt += t * t;
                stf += t * framePeakHz[i];
            }
            const float den = nF * stt - st * st;
            e.fmSlopeHzPerS = std::fabs(den) > 1e-9f ? (nF * stf - st * sf) / den : 0.0f;
        }
        events.push_back(e);
    }
    (void)r;
    return events;
}

float AcousticAnalysis::similarity(const AcousticEvent& a, const AcousticEvent& b)
{
    const float df0 = std::fabs(std::log2(std::max(1.0f, a.f0Hz) / std::max(1.0f, b.f0Hz)));
    const float ddur = std::fabs(std::log2(std::max(0.01f, a.durationS) / std::max(0.01f, b.durationS)));
    const float dbw = std::fabs(std::log2(std::max(1.0f, a.bandwidthHz) / std::max(1.0f, b.bandwidthHz)));
    const float dh = std::fabs(a.harmonicity - b.harmonicity);
    const float dist = 1.5f * df0 + 1.0f * ddur + 0.5f * dbw + 1.0f * dh;
    return 1.0f / (1.0f + dist);
}

void AcousticAnalysis::cluster(std::vector<AcousticEvent>& events, int k, u64 seed)
{
    if (events.empty() || k <= 0)
    {
        return;
    }
    k = std::min<int>(k, static_cast<int>(events.size()));
    auto feat = [](const AcousticEvent& e) {
        return std::array<float, 4>{std::log2(std::max(1.0f, e.f0Hz)), 2.0f * std::log2(std::max(0.01f, e.durationS)),
                                    0.5f * std::log2(std::max(1.0f, e.bandwidthHz)), 2.0f * e.harmonicity};
    };
    std::vector<std::array<float, 4>> centers;
    Rng rng(seed, 0xC1);
    for (int i = 0; i < k; ++i)
    {
        centers.push_back(feat(events[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(events.size()) - 1))]));
    }
    for (int iter = 0; iter < 30; ++iter)
    {
        for (AcousticEvent& e : events)
        {
            const auto f = feat(e);
            float best = 1e30f;
            for (int c = 0; c < k; ++c)
            {
                float d = 0.0f;
                for (int j = 0; j < 4; ++j)
                {
                    d += square(f[static_cast<size_t>(j)] - centers[static_cast<size_t>(c)][static_cast<size_t>(j)]);
                }
                if (d < best)
                {
                    best = d;
                    e.cluster = c;
                }
            }
        }
        std::vector<std::array<float, 4>> sums(static_cast<size_t>(k), {0.0f, 0.0f, 0.0f, 0.0f});
        std::vector<int> counts(static_cast<size_t>(k), 0);
        for (const AcousticEvent& e : events)
        {
            const auto f = feat(e);
            for (int j = 0; j < 4; ++j)
            {
                sums[static_cast<size_t>(e.cluster)][static_cast<size_t>(j)] += f[static_cast<size_t>(j)];
            }
            ++counts[static_cast<size_t>(e.cluster)];
        }
        for (int c = 0; c < k; ++c)
        {
            if (counts[static_cast<size_t>(c)] > 0)
            {
                for (int j = 0; j < 4; ++j)
                {
                    centers[static_cast<size_t>(c)][static_cast<size_t>(j)] = sums[static_cast<size_t>(c)][static_cast<size_t>(j)] / static_cast<float>(counts[static_cast<size_t>(c)]);
                }
            }
        }
    }
}

LocalizationResult AcousticAnalysis::localize(const std::vector<const Recording*>& recs, float tStartS, float tEndS, float searchRadiusM, float temperatureC)
{
    LocalizationResult res;
    if (recs.size() < 3)
    {
        return res;
    }
    const float c = speedOfSound(temperatureC);
    const int sr = recs[0]->sampleRate;
    float baseline = 0.0f;
    for (const Recording* r : recs)
    {
        baseline = std::max(baseline, distance(r->micPosition.xy(), recs[0]->micPosition.xy()));
    }
    const float maxLagS = baseline / c + 0.01f;
    const int i0 = std::max(0, static_cast<int>((tStartS - maxLagS) * static_cast<float>(sr)));
    const int i1 = std::min(static_cast<int>(recs[0]->samples.size()), static_cast<int>((tEndS + maxLagS) * static_cast<float>(sr)));
    if (i1 - i0 < 64)
    {
        return res;
    }
    const size_t n = acousticsimpl::nextPow2(static_cast<size_t>(i1 - i0) * 2u);
    auto spectrumOf = [&](const Recording& r) {
        std::vector<std::complex<float>> a(n, {0.0f, 0.0f});
        for (int i = i0; i < i1 && i < static_cast<int>(r.samples.size()); ++i)
        {
            a[static_cast<size_t>(i - i0)] = {r.samples[static_cast<size_t>(i)], 0.0f};
        }
        fft(a, false);
        return a;
    };
    const std::vector<std::complex<float>> ref = spectrumOf(*recs[0]);
    std::vector<float> tdoa(recs.size(), 0.0f);
    const int maxLag = static_cast<int>(maxLagS * static_cast<float>(sr));
    for (size_t k = 1; k < recs.size(); ++k)
    {
        std::vector<std::complex<float>> x = spectrumOf(*recs[k]);
        for (size_t i = 0; i < n; ++i)
        {
            std::complex<float> g = x[i] * std::conj(ref[i]);
            const float mag = std::abs(g);
            x[i] = mag > 1e-20f ? g / mag : std::complex<float>(0.0f, 0.0f); // PHAT weighting
        }
        fft(x, true);
        int bestLag = 0;
        float bestVal = -1e30f;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            const size_t idx = lag >= 0 ? static_cast<size_t>(lag) : n - static_cast<size_t>(-lag);
            const float v = x[idx].real();
            if (v > bestVal)
            {
                bestVal = v;
                bestLag = lag;
            }
        }
        tdoa[k] = static_cast<float>(bestLag) / static_cast<float>(sr); // arrival(k) - arrival(0)
    }
    Vec2 centre;
    for (const Recording* r : recs)
    {
        centre += r->micPosition.xy();
    }
    centre = centre / static_cast<float>(recs.size());
    auto cost = [&](const Vec2& p) {
        const float d0 = distance(p, recs[0]->micPosition.xy());
        float sum = 0.0f;
        for (size_t k = 1; k < recs.size(); ++k)
        {
            const float pred = (distance(p, recs[k]->micPosition.xy()) - d0) / c;
            sum += square(pred - tdoa[k]);
        }
        return sum;
    };
    Vec2 best = centre;
    float bestCost = 1e30f;
    const float coarse = std::max(5.0f, searchRadiusM / 150.0f);
    for (float y = -searchRadiusM; y <= searchRadiusM; y += coarse)
    {
        for (float x = -searchRadiusM; x <= searchRadiusM; x += coarse)
        {
            const Vec2 p = centre + Vec2{x, y};
            const float cst = cost(p);
            if (cst < bestCost)
            {
                bestCost = cst;
                best = p;
            }
        }
    }
    const Vec2 coarseBest = best;
    for (float y = -coarse * 2.0f; y <= coarse * 2.0f; y += 0.5f)
    {
        for (float x = -coarse * 2.0f; x <= coarse * 2.0f; x += 0.5f)
        {
            const Vec2 p = coarseBest + Vec2{x, y};
            const float cst = cost(p);
            if (cst < bestCost)
            {
                bestCost = cst;
                best = p;
            }
        }
    }
    res.valid = true;
    res.position = best;
    res.microphones = static_cast<int>(recs.size());
    res.residualMs = std::sqrt(bestCost / static_cast<float>(recs.size() - 1)) * 1000.0f;
    // Error grows with timing residual, sample quantisation and distance outside the array.
    const float outside = std::max(0.0f, distance(best, centre) - baseline);
    res.errorRadiusM = res.residualMs * 0.001f * c * 3.0f + c / static_cast<float>(sr) * 2.0f + outside * 0.15f;
    return res;
}

std::vector<u8> AcousticAnalysis::toWav(const Recording& r, float fullScalePa)
{
    float peak = fullScalePa;
    if (peak <= 0.0f)
    {
        for (const float s : r.samples)
        {
            peak = std::max(peak, std::fabs(s));
        }
    }
    peak = std::max(peak, 1e-9f);
    const u32 dataBytes = static_cast<u32>(r.samples.size() * 2u);
    std::vector<u8> out;
    out.reserve(44u + dataBytes);
    auto put32 = [&](u32 v) {
        for (int i = 0; i < 4; ++i)
        {
            out.push_back(static_cast<u8>((v >> (8 * i)) & 0xFFu));
        }
    };
    auto put16 = [&](u16 v) {
        out.push_back(static_cast<u8>(v & 0xFFu));
        out.push_back(static_cast<u8>(v >> 8));
    };
    const char* riff = "RIFF";
    out.insert(out.end(), riff, riff + 4);
    put32(36u + dataBytes);
    const char* wavefmt = "WAVEfmt ";
    out.insert(out.end(), wavefmt, wavefmt + 8);
    put32(16u);
    put16(1u);
    put16(1u);
    put32(static_cast<u32>(r.sampleRate));
    put32(static_cast<u32>(r.sampleRate) * 2u);
    put16(2u);
    put16(16u);
    const char* data = "data";
    out.insert(out.end(), data, data + 4);
    put32(dataBytes);
    for (const float s : r.samples)
    {
        const float v = clampf(s / peak, -1.0f, 1.0f) * 32767.0f;
        put16(static_cast<u16>(static_cast<i16>(v)));
    }
    return out;
}
} // namespace noctis
