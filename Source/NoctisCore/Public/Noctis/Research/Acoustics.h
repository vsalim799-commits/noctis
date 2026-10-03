// AcousticAnalysisSystem — recording, synthesis and analysis of the soundscape.
//
// Recordings are physical: every SoundSource active during the recording window is synthesised,
// delayed by its travel time, attenuated harmonic by harmonic (spreading + ISO 9613-1 absorption +
// foliage), weighted by microphone directivity, and mixed with wind, rain, river and microphone
// self-noise. Analysis works on that signal only (FFT spectrogram, event detection, fundamental
// frequency, TDOA localisation with several microphones, clustering into call types).
// It never states what a call "means" — interpretation is the player's hypothesis.
#pragma once

#include "Noctis/Sim/WorldContext.h"

#include <complex>
#include <string>
#include <vector>

namespace noctis
{
struct MicSpec
{
    std::string id = "omni";
    float selfNoiseDb = 18.0f;       // equivalent noise level, dB SPL
    float directivityGainDb = 0.0f;  // on-axis gain (shotgun ~6, parabolic ~15)
    float beamWidthDeg = 360.0f;
    int sampleRate = 16000;
};

struct Recording
{
    u32 id = 0;
    double startTime = 0.0;
    float durationS = 0.0f;
    int sampleRate = 16000;
    std::vector<float> samples; // pascals
    Vec3 micPosition;
    float micHeading = 0.0f;
    MicSpec mic;
    u32 arrayId = 0;            // recordings sharing an arrayId are time-synchronised
    u32 expeditionId = 0;
    std::string label;
    // Hidden truth, kept for validation/debug only.
    std::vector<u32> truthSourceIds;
};

struct Spectrogram
{
    int frames = 0;
    int bins = 0;
    float hopS = 0.0f;
    float binHz = 0.0f;
    std::vector<float> db; // frames * bins, dB SPL per bin
    float at(int frame, int bin) const { return db[static_cast<size_t>(frame) * static_cast<size_t>(bins) + static_cast<size_t>(bin)]; }
};

struct AcousticEvent
{
    float startS = 0.0f;
    float endS = 0.0f;
    float durationS = 0.0f;
    float peakFreqHz = 0.0f;
    float f0Hz = 0.0f;
    float bandwidthHz = 0.0f;
    float peakLevelDb = 0.0f; // dB SPL at the microphone
    float rmsLevelDb = 0.0f;
    float snrDb = 0.0f;
    float harmonicity = 0.0f; // 0 noise-like .. 1 harmonic
    float fmSlopeHzPerS = 0.0f;
    int cluster = -1;
};

struct LocalizationResult
{
    bool valid = false;
    Vec2 position;
    float errorRadiusM = 0.0f;
    float residualMs = 0.0f;
    int microphones = 0;
};

class NOCTIS_API SignalSynth
{
public:
    // Adds the signal of 'src' as received at distanceM into buffer (pascals) starting at bufferStart.
    // Per-harmonic absorption uses env; gainExtraDb includes foliage, wind and directivity terms.
    static void render(const SoundSource& src, const EvidenceDatabase* db, double bufferStart, int sampleRate, float distanceM, float gainExtraDb,
                       const PropagationEnv& env, std::vector<float>& buffer);
    // Coloured noise of a given broadband level (dB SPL): 'pink' for wind, 'white' for rain/self-noise.
    static void addNoise(std::vector<float>& buffer, int sampleRate, float levelDb, float lowpassHz, float highpassHz, u64 seed);
};

class NOCTIS_API AcousticRecorder
{
public:
    // Synthesises what the microphone captured during [t0, t0 + durationS]. When 'capturedSources' is null the
    // sources still held by the sound field are used; long recordings pass the sources captured while recording.
    static Recording record(const WorldContext& ctx, const MicSpec& mic, const Vec3& micPosition, float micHeading, double t0, float durationS, u32 id,
                            u64 seed, const std::vector<SoundSource>* capturedSources = nullptr);
};

class NOCTIS_API AcousticAnalysis
{
public:
    static void fft(std::vector<std::complex<float>>& a, bool inverse);
    static Spectrogram spectrogram(const Recording& r, int fftSize = 2048, int hop = 512);
    static std::vector<AcousticEvent> detectEvents(const Recording& r, const Spectrogram& s, float thresholdDb = 8.0f, float minHz = 10.0f,
                                                   float maxHz = 4000.0f);
    static float similarity(const AcousticEvent& a, const AcousticEvent& b);
    // k-means on (log f0, log duration, log bandwidth, harmonicity); assigns AcousticEvent::cluster.
    static void cluster(std::vector<AcousticEvent>& events, int k, u64 seed);
    // TDOA localisation (GCC-PHAT pairwise delays + grid search). Recordings must share start time.
    static LocalizationResult localize(const std::vector<const Recording*>& recs, float tStartS, float tEndS, float searchRadiusM, float temperatureC);
    static float speedOfSound(float temperatureC) { return 331.3f + 0.606f * temperatureC; }
    // 16-bit PCM WAV encoding (for export to the lab and to Unreal), normalised to the peak or to a fixed full-scale Pa.
    static std::vector<u8> toWav(const Recording& r, float fullScalePa = 0.0f);
};
} // namespace noctis
