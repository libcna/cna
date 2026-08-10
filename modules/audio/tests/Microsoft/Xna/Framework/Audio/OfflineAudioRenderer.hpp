// SPDX-License-Identifier: MS-PL
#pragma once

// AUD-03-001: deterministic offline audio render/measurement harness.
//
// Renders real SDL3_mixer decode/resample/mix output to memory via MIX_CreateMixer() +
// MIX_Generate() -- NOT MIX_CreateMixerDevice(). This means no physical audio device, no
// SDL_AUDIODRIVER environment variable, and no wall-clock timing are involved at all: every test
// using this harness runs identically whether or not real/dummy audio hardware is present, and
// produces the exact same samples on every run. This is the missing "prove what reaches the
// speakers" capability the 2026-07-17 deep audit identified as the central process gap (A-02).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

namespace CNA::Test::Audio
{
    /** @brief Result of an offline render: interleaved float32 PCM plus the real (non-appended-silence) byte count. */
    struct OfflineRenderResult
    {
        std::vector<float> samples; // interleaved float32, `channels` channels
        int channels = 0;
        int sampleRate = 0;
        int realBytesRendered = 0; // sum of MIX_Generate's return values (excludes appended silence)
        bool ok = false;           // false if mixer/audio/track setup failed
    };

    /**
     * Renders `framesToRender` frames of `renderChannels`-channel float32 PCM at
     * `renderSampleRate` Hz by playing `sourcePcm` (raw bytes in `sourceFormat`,
     * `sourceChannels` channels, `sourceSampleRate` Hz) once through a brand-new, fully offline
     * SDL3_mixer pipeline. `pitchRatio` is applied via MIX_SetTrackFrequencyRatio before playing
     * (1.0 = neutral). `looped` selects infinite looping (-1) vs play-once (0) for
     * MIX_PROP_PLAY_LOOPS_NUMBER. `trackGain`/`mixerGain` (AUD-04-014) are applied via
     * MIX_SetTrackGain/MIX_SetMixerGain before playing, mirroring the two independent gain
     * stages CNA's own SoundEffectInstance::Volume and SoundEffect::MasterVolume compose through
     * (1.0 = neutral for both, matching every existing call site's unchanged behavior).
     */
    inline OfflineRenderResult RenderRawPcmOffline(
        const std::vector<uint8_t>& sourcePcm,
        SDL_AudioFormat sourceFormat,
        int sourceChannels,
        int sourceSampleRate,
        int renderSampleRate,
        int renderChannels,
        int framesToRender,
        float pitchRatio = 1.0f,
        bool looped = false,
        float trackGain = 1.0f,
        float mixerGain = 1.0f)
    {
        OfflineRenderResult result;
        result.channels = renderChannels;
        result.sampleRate = renderSampleRate;

        // MIX_Init()/MIX_Quit() are refcounted (like SDL_Init), so this nested Init/Quit pair is
        // safe even while CNA::Internal::Audio::GetMixer()'s own shared device mixer is alive
        // elsewhere -- the underlying resources aren't released until the last matching MIX_Quit().
        if (!MIX_Init()) return result;

        SDL_AudioSpec renderSpec{};
        renderSpec.format = SDL_AUDIO_F32;
        renderSpec.channels = renderChannels;
        renderSpec.freq = renderSampleRate;

        MIX_Mixer* mixer = MIX_CreateMixer(&renderSpec);
        if (!mixer)
        {
            MIX_Quit();
            return result;
        }

        SDL_AudioSpec sourceSpec{};
        sourceSpec.format = sourceFormat;
        sourceSpec.channels = sourceChannels;
        sourceSpec.freq = sourceSampleRate;

        SDL_IOStream* io = SDL_IOFromConstMem(sourcePcm.data(), sourcePcm.size());
        if (!io)
        {
            MIX_DestroyMixer(mixer);
            MIX_Quit();
            return result;
        }

        // MIX_LoadRawAudio_IO copies the raw bytes into its own buffer immediately, so sourcePcm
        // only needs to stay valid for the duration of this call, not the mixer's lifetime.
        MIX_Audio* audio = MIX_LoadRawAudio_IO(mixer, io, &sourceSpec, true /* closeio */);
        if (!audio)
        {
            MIX_DestroyMixer(mixer);
            MIX_Quit();
            return result;
        }

        MIX_Track* track = MIX_CreateTrack(mixer);
        if (!track)
        {
            MIX_DestroyAudio(audio);
            MIX_DestroyMixer(mixer);
            MIX_Quit();
            return result;
        }

        if (!MIX_SetTrackAudio(track, audio) ||
            !MIX_SetTrackFrequencyRatio(track, pitchRatio) ||
            !MIX_SetTrackGain(track, trackGain) ||
            !MIX_SetMixerGain(mixer, mixerGain))
        {
            MIX_DestroyTrack(track);
            MIX_DestroyAudio(audio);
            MIX_DestroyMixer(mixer);
            MIX_Quit();
            return result;
        }

        SDL_PropertiesID props = SDL_CreateProperties();
        bool played;
        if (props != 0)
        {
            SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, looped ? -1 : 0);
            played = MIX_PlayTrack(track, props);
            SDL_DestroyProperties(props);
        }
        else
        {
            played = MIX_PlayTrack(track, 0);
        }

        if (played)
        {
            const std::size_t bytesPerFrame = static_cast<std::size_t>(renderChannels) * sizeof(float);
            const std::size_t totalBytes = static_cast<std::size_t>(framesToRender) * bytesPerFrame;
            std::vector<uint8_t> raw(totalBytes);

            int generated = MIX_Generate(mixer, raw.data(), static_cast<int>(totalBytes));
            if (generated >= 0)
            {
                result.realBytesRendered = generated;
                result.samples.resize(totalBytes / sizeof(float));
                std::memcpy(result.samples.data(), raw.data(), totalBytes);
                result.ok = true;
            }
        }

        MIX_DestroyTrack(track);
        MIX_DestroyAudio(audio);
        MIX_DestroyMixer(mixer);
        MIX_Quit();
        return result;
    }

    // --- fixture generation ---

    /** @brief Generates a full-scale-relative sine wave as raw interleaved S16LE PCM bytes. */
    inline std::vector<uint8_t> GenerateSineWaveS16(
        double frequencyHz, int sampleRate, int channels, double durationSeconds,
        float amplitude = 0.8f)
    {
        const int frames = static_cast<int>(durationSeconds * sampleRate);
        std::vector<uint8_t> bytes(static_cast<std::size_t>(frames) * channels * sizeof(int16_t));
        auto* samples = reinterpret_cast<int16_t*>(bytes.data());
        for (int i = 0; i < frames; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            const double value = amplitude * std::sin(2.0 * std::numbers::pi * frequencyHz * t);
            const auto sample = static_cast<int16_t>(std::lround(value * 32767.0));
            for (int c = 0; c < channels; ++c)
            {
                samples[static_cast<std::size_t>(i) * channels + c] = sample;
            }
        }
        return bytes;
    }

    /** @brief Generates raw interleaved S16LE PCM bytes containing only silence. */
    inline std::vector<uint8_t> GenerateSilenceS16(int sampleRate, int channels, double durationSeconds)
    {
        const int frames = static_cast<int>(durationSeconds * sampleRate);
        return std::vector<uint8_t>(static_cast<std::size_t>(frames) * channels * sizeof(int16_t), 0);
    }

    // --- measurement ---

    /** @brief Root-mean-square level of one channel of interleaved float32 PCM. */
    inline double MeasureRms(const std::vector<float>& interleaved, int channels, int channelIndex = 0)
    {
        if (interleaved.empty() || channels <= 0) return 0.0;
        double sumSquares = 0.0;
        std::size_t count = 0;
        for (std::size_t i = static_cast<std::size_t>(channelIndex); i < interleaved.size(); i += channels)
        {
            sumSquares += static_cast<double>(interleaved[i]) * interleaved[i];
            ++count;
        }
        return count == 0 ? 0.0 : std::sqrt(sumSquares / static_cast<double>(count));
    }

    /** @brief Peak absolute amplitude of one channel of interleaved float32 PCM. */
    inline double MeasurePeak(const std::vector<float>& interleaved, int channels, int channelIndex = 0)
    {
        double peak = 0.0;
        for (std::size_t i = static_cast<std::size_t>(channelIndex); i < interleaved.size(); i += channels)
        {
            peak = std::max(peak, static_cast<double>(std::fabs(interleaved[i])));
        }
        return peak;
    }

    /** @brief True if any sample is NaN or infinite. */
    inline bool ContainsNaNOrInf(const std::vector<float>& interleaved)
    {
        for (float s : interleaved)
        {
            if (std::isnan(s) || std::isinf(s)) return true;
        }
        return false;
    }

    /**
     * Goertzel single-bin magnitude at `targetHz` for one channel -- exact (no FFT bin-width
     * uncertainty) for detecting whether a specific expected tone is present, unlike a full FFT
     * which would require picking a bin close to the target frequency.
     */
    inline double GoertzelMagnitude(
        const std::vector<float>& interleaved, int channels, int channelIndex,
        int sampleRate, double targetHz)
    {
        std::vector<float> mono;
        for (std::size_t i = static_cast<std::size_t>(channelIndex); i < interleaved.size(); i += channels)
        {
            mono.push_back(interleaved[i]);
        }
        const std::size_t n = mono.size();
        if (n == 0) return 0.0;

        const double k = std::round((static_cast<double>(n) * targetHz) / sampleRate);
        const double omega = (2.0 * std::numbers::pi * k) / static_cast<double>(n);
        const double coeff = 2.0 * std::cos(omega);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (float sample : mono)
        {
            s0 = sample + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double real = s1 - s2 * std::cos(omega);
        const double imag = s2 * std::sin(omega);
        return std::sqrt(real * real + imag * imag) / static_cast<double>(n);
    }

    /**
     * Coarse blind dominant-frequency search: a GoertzelMagnitude sweep (`stepHz` resolution)
     * over [searchMinHz, searchMaxHz] locates the peak bin. Precision is fundamentally limited
     * by the analysis window's own frequency resolution (~1/duration, e.g. ~5 Hz for a 0.2 s
     * window) no matter how fine `stepHz` is -- use this only to find the right neighborhood
     * (or to catch a gross error like an accidental octave/2x shift) when the expected frequency
     * isn't known ahead of time, then refine with RefineFrequencyEstimateHz below for a tight
     * numerical gate.
     */
    inline double EstimateDominantFrequencyHz(
        const std::vector<float>& interleaved, int channels, int channelIndex, int sampleRate,
        double searchMinHz, double searchMaxHz, double stepHz = 1.0)
    {
        double bestHz = searchMinHz;
        double bestMag = -1.0;
        for (double hz = searchMinHz; hz <= searchMaxHz; hz += stepHz)
        {
            double mag = GoertzelMagnitude(interleaved, channels, channelIndex, sampleRate, hz);
            if (mag > bestMag)
            {
                bestMag = mag;
                bestHz = hz;
            }
        }
        return bestHz;
    }

    /**
     * Precisely refines a known-approximate frequency (`nominalHz`) using a phase-difference
     * estimator: correlates the first half and second half of the buffer against a complex
     * exponential at `nominalHz`, then converts the phase drift between the two halves' centers
     * into a frequency offset. Unlike Goertzel-peak-search/FFT-bin methods, this is NOT limited
     * by the window's basic bin-width resolution (~1/duration) -- it is limited only by how close
     * `nominalHz` already is to the true frequency (the phase difference must not wrap by more
     * than +/- half a cycle between the two half-buffer centers, i.e.
     * |trueHz - nominalHz| * (n/2/sampleRate) < 0.5). This is the precise primary measurement for
     * golden tests where the expected frequency is already known to within a few percent (e.g.
     * from the authored source tone and an intended pitch/resample ratio); use
     * EstimateDominantFrequencyHz above first if the expected frequency is not already known.
     */
    inline double RefineFrequencyEstimateHz(
        const std::vector<float>& interleaved, int channels, int channelIndex,
        int sampleRate, double nominalHz)
    {
        std::vector<float> mono;
        for (std::size_t i = static_cast<std::size_t>(channelIndex); i < interleaved.size(); i += channels)
        {
            mono.push_back(interleaved[i]);
        }
        const std::size_t n = mono.size();
        if (n < 4) return nominalHz;

        const std::size_t half = n / 2;
        auto correlate = [&](std::size_t start, std::size_t count) {
            double real = 0.0, imag = 0.0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const double t = static_cast<double>(start + i) / sampleRate;
                const double angle = 2.0 * std::numbers::pi * nominalHz * t;
                real += static_cast<double>(mono[start + i]) * std::cos(angle);
                imag -= static_cast<double>(mono[start + i]) * std::sin(angle);
            }
            return std::pair<double, double>(real, imag);
        };

        auto [real1, imag1] = correlate(0, half);
        auto [real2, imag2] = correlate(n - half, half);

        const double phase1 = std::atan2(imag1, real1);
        const double phase2 = std::atan2(imag2, real2);
        const double centerT1 = (static_cast<double>(half) / 2.0) / sampleRate;
        const double centerT2 = (static_cast<double>(n - half) + static_cast<double>(half) / 2.0) / sampleRate;
        const double dt = centerT2 - centerT1;

        double dPhase = phase2 - phase1;
        while (dPhase > std::numbers::pi) dPhase -= 2.0 * std::numbers::pi;
        while (dPhase < -std::numbers::pi) dPhase += 2.0 * std::numbers::pi;

        if (dt <= 0.0) return nominalHz;
        return nominalHz + dPhase / (2.0 * std::numbers::pi * dt);
    }

    /**
     * Zero-crossing-rate dominant-frequency estimate for one channel -- an independent
     * measurement method from GoertzelMagnitude, useful when only a ratio (not an absolute
     * expected frequency) is known ahead of time. Coarser than EstimateDominantFrequencyHz
     * (inherent +/-1 crossing quantization over the analysis window) -- use as a sanity
     * cross-check, not a tight precision gate.
     */
    inline double MeasureZeroCrossingFrequencyHz(
        const std::vector<float>& interleaved, int channels, int channelIndex, int sampleRate)
    {
        std::vector<float> mono;
        for (std::size_t i = static_cast<std::size_t>(channelIndex); i < interleaved.size(); i += channels)
        {
            mono.push_back(interleaved[i]);
        }
        if (mono.size() < 2) return 0.0;

        int crossings = 0;
        for (std::size_t i = 1; i < mono.size(); ++i)
        {
            if ((mono[i - 1] < 0.0f && mono[i] >= 0.0f) || (mono[i - 1] >= 0.0f && mono[i] < 0.0f))
            {
                ++crossings;
            }
        }
        const double durationSeconds = static_cast<double>(mono.size()) / sampleRate;
        // Each full cycle produces exactly 2 zero crossings.
        return (crossings / 2.0) / durationSeconds;
    }
}
