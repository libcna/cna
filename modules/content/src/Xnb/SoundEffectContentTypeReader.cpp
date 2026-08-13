// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>

#include "CNA/Internal/Audio/WavWrapper.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Audio::AudioChannels;
    using Microsoft::Xna::Framework::Audio::SoundEffect;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

    namespace
    {
        // FNA's SoundEffectReader.Swap(bool, ushort/uint) -- only ever exercised on the Xbox
        // ('x') platform byte-order, which real fixtures used by this port never target, but
        // ported verbatim rather than dropped (matches this plan's precedent for FNA's own
        // unreachable-in-practice code, e.g. the LZX decoder's Intel E8 path).
        uint16_t Swap16(bool swap, uint16_t x)
        {
            if (!swap) return x;
            return static_cast<uint16_t>(((x >> 8) & 0x00FF) | ((x << 8) & 0xFF00));
        }

        uint32_t Swap32(bool swap, uint32_t x)
        {
            if (!swap) return x;
            return ((x >> 24) & 0x000000FFu) | ((x >> 8) & 0x0000FF00u) |
                   ((x << 8) & 0x00FF0000u) | ((x << 24) & 0xFF000000u);
        }

        constexpr uint16_t kWaveFormatPcm = 0x0001;
        constexpr uint16_t kWaveFormatMsAdpcm = 0x0002;
        constexpr uint16_t kWaveFormatIeeeFloat = 0x0003;
        constexpr uint16_t kWaveFormatImaAdpcm = 0x0011;
        constexpr uint16_t kWaveFormatXma2 = 0x0166;

        // AUDIO-XNB-ADPCM-001 (2026-07-17 deep audit follow-up): empirically confirmed against a
        // *real*, externally-produced MonoGame XNB fixture
        // (tests/assets/xnb/monogame/windows/uncompressed/audio/tone_mono_44khz_msadpcm.xnb) that
        // the content pipeline writes cbSize=0 for MS-ADPCM -- no wSamplesPerBlock, no
        // coefficient table at all in the embedded format block (verified via a direct hex dump
        // of the file's fmt data: bytes at offset 87-88 are 0x00 0x00). This matches FNA's own
        // SoundEffectReader.cs, which never captures the extension for any non-XMA2 format either
        // -- so a real XNA/MonoGame-produced MS-ADPCM asset genuinely has no extension to forward
        // verbatim. Unlike IMA-ADPCM (SDL3's decoder auto-derives wSamplesPerBlock from
        // nBlockAlign when absent), SDL3's MS-ADPCM decoder has no such fallback and requires an
        // explicit, valid coefficient table (SDL_wave.c's MS_ADPCM_Init) -- so this synthesizes
        // one using the same standard preset coefficients as WaveBank.cpp's ADPCM path
        // (CNA::Internal::Audio::BuildStandardMsAdpcmExtension) and a samplesPerBlock computed
        // from nBlockAlign via the MS-ADPCM "Standards Update" formula every encoder (including
        // XNA's) follows, only when the XNB didn't already supply a real, usable extension.
        uint16_t ComputeMsAdpcmSamplesPerBlock(uint16_t blockAlign, uint16_t channels)
        {
            const uint32_t ch = channels == 0 ? 1u : channels;
            const uint32_t blockHeaderBits = ch * 7u * 8u;
            const uint32_t blockBits = static_cast<uint32_t>(blockAlign) * 8u;
            const uint32_t availableBits = blockBits > blockHeaderBits ? blockBits - blockHeaderBits : 0u;
            const uint32_t blockDataSamples = availableBits / (4u * ch);
            return static_cast<uint16_t>(blockDataSamples + 2u);
        }

        // A real MS-ADPCM extension needs at least wSamplesPerBlock(2) + wNumCoef(2) +
        // 7 coefficient pairs(28) = 32 bytes.
        constexpr std::size_t kMinRealMsAdpcmExtensionSize = 32;

        // AUD-06-010: uses the .xnb's own stored duration field as a validation oracle instead of
        // discarding it -- a drastic disagreement with the actually-decoded duration is a strong
        // signal of a real structural misinterpretation (wrong sample rate, wrong channel count,
        // a doubled/halved frame count from a format mixup), not something a legitimately-authored
        // asset would ever produce. Empirically calibrated against real MonoGame fixtures
        // (tests/assets/xnb/monogame/...): uncompressed PCM/float match the stored duration
        // exactly (to whole-millisecond rounding); ADPCM formats showed a few percent of
        // legitimate block-rounding drift. `storedDurationMs == 0` is treated as "not meaningfully
        // set" and skipped entirely -- many real assets across the ecosystem leave this field at
        // its default, and it's not something FNA itself ever populates/validates either, so
        // treating an unset value as an error would be a real compatibility regression. The 2x/0.5x
        // threshold is deliberately generous: comfortably wider than any legitimate rounding drift
        // observed, while still catching an order-of-magnitude-class misinterpretation.
        void ValidateDecodedDurationAgainstStoredOracle(
            const std::string& assetName, uint32_t storedDurationMs, const SoundEffect& effect)
        {
            if (storedDurationMs == 0) return;

            const double decodedMs = effect.getDurationProperty().getTotalMillisecondsProperty();
            if (decodedMs <= 0.0) return; // nothing meaningful to compare against

            const double ratio = decodedMs / static_cast<double>(storedDurationMs);
            if (ratio > 2.0 || ratio < 0.5)
            {
                throw ContentLoadException(
                    "'" + assetName + "': decoded audio duration (" + std::to_string(decodedMs) +
                    "ms) disagrees drastically with the .xnb's own stored duration (" +
                    std::to_string(storedDurationMs) + "ms) -- likely a sample rate, channel "
                    "count, or format misinterpretation.");
            }
        }

        // AUD-06-023 (fuzzing prep): found via property-based testing that the 16-bit PCM direct
        // fast path had never received the same treatment AUD-06-024 gave BuildViaWavWrapper --
        // an invalid sample rate (e.g. 0) makes SoundEffect's raw-buffer constructor throw a raw
        // System::NotSupportedException with no asset context at all, unlike every other failure
        // path in this reader. Isolating the construction call in its own helper keeps that fix
        // symmetric with BuildViaWavWrapper's, and keeps ValidateDecodedDurationAgainstStoredOracle
        // (called separately, after this returns) from being caught and needlessly re-wrapped.
        SoundEffect BuildDirectPcm16(
            const std::string& assetName,
            const std::vector<uint8_t>& data,
            uint32_t nSamplesPerSec, uint16_t nChannels,
            int32_t loopStart, int32_t loopLength)
        {
            try
            {
                SoundEffect effect(
                    data, 0, static_cast<int32_t>(data.size()),
                    static_cast<int32_t>(nSamplesPerSec),
                    static_cast<AudioChannels>(nChannels),
                    loopStart, loopLength);
                effect.setNameProperty(assetName);
                return effect;
            }
            catch (const std::exception& inner)
            {
                throw ContentLoadException(
                    "'" + assetName + "': SoundEffectReader failed to construct 16-bit PCM audio "
                    "(channels=" + std::to_string(nChannels) +
                    ", sampleRate=" + std::to_string(nSamplesPerSec) + ").",
                    inner);
            }
        }

        // Builds a SoundEffect for any format SDL3's own WAV decoder understands natively (PCM
        // 8/16-bit, IEEE float, MS/IMA ADPCM) by wrapping the raw bytes in a minimal in-memory WAV
        // file and going through SoundEffect::FromStream -- the same technique WaveBank.cpp uses
        // for its own compressed/8-bit entries (shared via CNA::Internal::Audio::WavWrapper).
        SoundEffect BuildViaWavWrapper(
            const std::string& assetName,
            const std::vector<uint8_t>& data,
            uint16_t wFormatTag, uint16_t nChannels, uint32_t nSamplesPerSec,
            uint32_t nAvgBytesPerSec, uint16_t nBlockAlign, uint16_t wBitsPerSample,
            const std::vector<uint8_t>& extensionData,
            int32_t loopStart, int32_t loopLength)
        {
            const bool needsSynthesizedAdpcmExtension =
                wFormatTag == kWaveFormatMsAdpcm && extensionData.size() < kMinRealMsAdpcmExtensionSize;

            auto wav = CNA::Internal::Audio::BuildWavFromWaveFormatEx(
                data.data(), static_cast<uint32_t>(data.size()),
                wFormatTag, nChannels, nSamplesPerSec, nAvgBytesPerSec, nBlockAlign, wBitsPerSample,
                needsSynthesizedAdpcmExtension
                    ? CNA::Internal::Audio::BuildStandardMsAdpcmExtension(
                          ComputeMsAdpcmSamplesPerBlock(nBlockAlign, nChannels))
                    : extensionData,
                /*factSampleFrames=*/0);
            CNA::Internal::Audio::AppendSmplChunkIfLooped(wav, loopStart, loopLength);

            std::string s(reinterpret_cast<const char*>(wav.data()), wav.size());
            std::istringstream ss(s);

            // AUD-06-024: SoundEffect::FromStream decodes the synthetic WAV via SDL3's own
            // decoder, which can reject it (e.g. an invalid sample rate -- AUD-06-013) with a
            // raw System::NotSupportedException that names nothing about the .xnb asset it came
            // from. Re-throwing as ContentLoadException(assetContext, inner) preserves the root
            // decoder error verbatim (ContentLoadException's inner-exception constructor appends
            // `" ---> " + inner.what()`) while adding the asset name and format fields a caller
            // actually needs to find the offending file.
            try
            {
                // The old direct decoder rejected a zero WAVEFORMATEX rate. The mixer seam may
                // instead normalize the malformed header to its output rate, which turns corrupt
                // content into plausible but wrongly-timed audio. Preserve the established XNB
                // failure contract at the boundary that still owns the original metadata.
                if (nSamplesPerSec == 0)
                {
                    throw std::invalid_argument("Invalid sample rate: zero");
                }

                std::unique_ptr<SoundEffect> loaded(SoundEffect::FromStream(ss));
                loaded->setNameProperty(assetName);
                return std::move(*loaded);
            }
            catch (const std::exception& inner)
            {
                throw ContentLoadException(
                    "'" + assetName + "': SoundEffectReader failed to decode WAV-wrapped audio "
                    "(formatTag=" + std::to_string(wFormatTag) + ", bitsPerSample=" +
                    std::to_string(wBitsPerSample) + ", channels=" + std::to_string(nChannels) +
                    ", sampleRate=" + std::to_string(nSamplesPerSec) + ").",
                    inner);
            }
        }
    }

    SoundEffect SoundEffectReader::Read(ContentReader& input, std::optional<SoundEffect> existingInstance)
    {
        (void)existingInstance; // never provided: getCanDeserializeIntoExistingObjectProperty() defaults false, matching FNA

        const bool se = input.getPlatformProperty() == 'x';

        const uint32_t formatLength = input.ReadUInt32();
        const uint16_t wFormatTag = Swap16(se, input.ReadUInt16());
        const uint16_t nChannels = Swap16(se, input.ReadUInt16());
        const uint32_t nSamplesPerSec = Swap32(se, input.ReadUInt32());
        // AUD-06-002: previously read-and-discarded ("unused, matches FNA" -- true for FNA's own
        // FAudio-backed constructor, which recomputes these itself, but CNA's SDL3_mixer-backed
        // WAV-wrapper path for non-16-bit-PCM formats below genuinely needs them).
        const uint32_t nAvgBytesPerSec = Swap32(se, input.ReadUInt32());
        const uint16_t nBlockAlign = Swap16(se, input.ReadUInt16());
        const uint16_t wBitsPerSample = Swap16(se, input.ReadUInt16());

        std::vector<uint8_t> extensionData;
        if (formatLength > 16)
        {
            const uint16_t cbSize = Swap16(se, input.ReadUInt16());
            if (wFormatTag == kWaveFormatXma2 && cbSize == 34)
            {
                // XMA2's extra FAudioXMA2WaveFormatEx fields -- consumed only for stream-position
                // correctness; CNA's SoundEffect has no XMA2 decode path, so XMA2 is rejected
                // below regardless of these values.
                input.ReadUInt16();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadUInt32();
                input.ReadByte();
                input.ReadByte();
                input.ReadUInt16();
                const int64_t skip = static_cast<int64_t>(formatLength) - 18 - 34;
                if (skip < 0)
                {
                    throw ContentLoadException(
                        "'" + input.getAssetNameProperty() + "': SoundEffectReader formatLength too "
                        "small for its XMA2 extension.");
                }
                (void)input.ReadBytesExactOrThrow(static_cast<int32_t>(skip), "SoundEffectReader");
            }
            else
            {
                const int64_t skip = static_cast<int64_t>(formatLength) - 18;
                if (skip < 0)
                {
                    throw ContentLoadException(
                        "'" + input.getAssetNameProperty() + "': SoundEffectReader formatLength too "
                        "small for its format extension.");
                }
                // AUD-06-002: captured verbatim (not discarded) -- for MS-ADPCM/IMA-ADPCM this is
                // exactly the WAVEFORMATEX extension (wSamplesPerBlock, and for MS-ADPCM the
                // coefficient table) a synthetic WAV's fmt chunk needs to decode correctly via
                // SDL3's own WAV/ADPCM decoder below. Not byte-swapped for platform=='x': like the
                // XMA2 branch above, this is unreachable in practice for any fixture this port
                // targets, and these bytes are only ever re-embedded verbatim into another WAV
                // fmt chunk, never individually interpreted as multi-byte integers here.
                extensionData = input.ReadBytesExactOrThrow(static_cast<int32_t>(skip), "SoundEffectReader");
            }
        }

        std::vector<uint8_t> data = input.ReadBytesExactOrThrow(input.ReadInt32(), "SoundEffectReader");

        const int32_t loopStart = input.ReadInt32();
        const int32_t loopLength = input.ReadInt32();
        const uint32_t storedDurationMs = input.ReadUInt32(); // duration in milliseconds, per the
        // .xnb format; FNA itself never reads this into anything (matches this reader's own prior
        // "unused" behavior for every field except duration) -- AUD-06-010 uses it as an oracle
        // instead of discarding it outright, see the check below.

        if (nChannels != 1 && nChannels != 2)
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() + "': unsupported SoundEffect channel count (" +
                std::to_string(nChannels) + "); only mono and stereo are supported.");
        }

        // AUD-06-004/006/007/008 (2026-07-17 deep audit, A-01): CNA's SoundEffect raw-buffer
        // constructors are S16-PCM-only, but SDL3's own WAV loader (used via
        // SoundEffect::FromStream/MIX_LoadAudio_IO) natively decodes PCM 8/16-bit, IEEE float
        // 32-bit, and MS/IMA ADPCM (SDL_audio.h's own documented SDL_LoadWAV coverage) -- wrapping
        // the raw bytes in a synthetic WAV file reaches that decoder instead of requiring CNA to
        // write its own float/ADPCM decode path. XMA2 remains rejected: no decode path exists
        // anywhere in this stack (SDL3 doesn't decode XMA2 either).
        if (wFormatTag == kWaveFormatPcm && wBitsPerSample == 16)
        {
            // Unchanged fast path: direct construction, no WAV-wrapping overhead.
            SoundEffect effect = BuildDirectPcm16(
                input.getAssetNameProperty(), data, nSamplesPerSec, nChannels, loopStart, loopLength);
            ValidateDecodedDurationAgainstStoredOracle(
                input.getAssetNameProperty(), storedDurationMs, effect);
            return effect;
        }
        if ((wFormatTag == kWaveFormatPcm && wBitsPerSample == 8) ||
            (wFormatTag == kWaveFormatIeeeFloat && wBitsPerSample == 32) ||
            (wFormatTag == kWaveFormatMsAdpcm && wBitsPerSample == 4) ||
            (wFormatTag == kWaveFormatImaAdpcm && wBitsPerSample == 4))
        {
            SoundEffect effect = BuildViaWavWrapper(
                input.getAssetNameProperty(), data,
                wFormatTag, nChannels, nSamplesPerSec, nAvgBytesPerSec, nBlockAlign, wBitsPerSample,
                extensionData, loopStart, loopLength);
            ValidateDecodedDurationAgainstStoredOracle(
                input.getAssetNameProperty(), storedDurationMs, effect);
            return effect;
        }

        // AUD-06-017: the diagnostic names every field useful for triaging an unsupported-format
        // report against a real asset -- tag, bit depth, channel count, sample rate, and the
        // asset name itself -- not just tag/bits, so a caller doesn't have to reproduce the
        // failure with extra instrumentation just to see what channels/rate the file declared.
        throw ContentLoadException(
            "'" + input.getAssetNameProperty() + "': unsupported SoundEffect wave format "
            "(formatTag=" + std::to_string(wFormatTag) + ", bitsPerSample=" +
            std::to_string(wBitsPerSample) + ", channels=" + std::to_string(nChannels) +
            ", sampleRate=" + std::to_string(nSamplesPerSec) + "). CNA's .xnb SoundEffectReader "
            "supports 8/16-bit PCM, 32-bit IEEE float, and MS/IMA ADPCM; XMA2 has no decode path "
            "anywhere in this stack (plan_audio.md AUD-06 support matrix).");
    }

    void RegisterSoundEffectXnbReader()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SoundEffectReader",
            [] { return std::make_unique<SoundEffectReader>(); });
    }
}
