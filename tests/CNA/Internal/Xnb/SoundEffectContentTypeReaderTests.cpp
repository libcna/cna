// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-33/XNB-33A + plan_audio.md AUD-06 (2026-07-17 deep audit): SoundEffectReader
// support-matrix survey + reader unit tests. Real, externally-produced fixtures (MonoGame's own
// Tests/Assets/Audio/tone_*.xnb, vendored at tests/assets/xnb/monogame/windows/uncompressed/audio/)
// cover every WaveFormatEx variant this matrix cares about -- never hand-crafted, since a
// hand-authored PCM buffer would trivially "pass" without proving the real WAVEFORMATEX byte
// layout was parsed correctly.
//
// Support matrix (plan_audio.md AUD-06, widened 2026-07-17 from the original PCM16-only baseline):
//   | Format                          | Status                                                |
//   |---------------------------------|--------------------------------------------------------|
//   | PCM 16-bit (mono or stereo)      | Supported (direct SoundEffect raw-buffer constructor)  |
//   | PCM 8-bit                        | Supported (WAV-wrapped, decoded via SDL3's WAV loader) |
//   | IEEE float 32-bit                | Supported (WAV-wrapped, decoded via SDL3's WAV loader) |
//   | MS-ADPCM                         | Supported (WAV-wrapped, decoded via SDL3's WAV loader) |
//   | IMA-ADPCM                        | Supported (WAV-wrapped, decoded via SDL3's WAV loader) |
//   | XMA2 (compressed, Xbox-oriented) | Rejected -- no decode path anywhere in this stack      |
// XMA2 throws a documented ContentLoadException rather than silently constructing a SoundEffect
// that would play back as noise.

#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    std::string ReadWholeFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    class SoundEffectContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterSoundEffectXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        // Reads the fixture's uncompressed body (past the 10-byte header, no size field since
        // none of these fixtures are LZX-compressed) directly into a ContentReader positioned
        // right after where InitializeTypeReaders() would have consumed the type-reader table,
        // mirroring how ContentManager::LoadXnbAsset<T>() itself drives ContentReader::ReadAsset<T>().
        Microsoft::Xna::Framework::Audio::SoundEffect LoadFixture(const std::string& path)
        {
            const std::string bytes = ReadWholeFile(path);
            EXPECT_FALSE(bytes.empty()) << "Real .xnb fixture not found relative to CWD: " << path;

            body_ = std::make_unique<System::IO::MemoryStream>(
                reinterpret_cast<const uint8_t*>(bytes.data()) + 10, static_cast<int32_t>(bytes.size()) - 10);
            reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
            return reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();
        }

        std::string ownedBytes_;
        std::unique_ptr<System::IO::MemoryStream> body_;
        std::unique_ptr<ContentReader> reader_;
        ContentManager cm_;
    };

    constexpr const char* kAudioDir = "tests/assets/xnb/monogame/windows/uncompressed/audio/";
}

TEST_F(SoundEffectContentTypeReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.SoundEffectReader"));
}

TEST_F(SoundEffectContentTypeReaderTest, Pcm16BitMonoLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_16bit.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

TEST_F(SoundEffectContentTypeReaderTest, Pcm16BitStereoLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_stereo_44khz_16bit.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

// AUD-06-004 (2026-07-17 deep audit): widened from "rejected" to real support, WAV-wrapped
// through SoundEffect::FromStream -> SDL3's own native 8-bit PCM decoder.
TEST_F(SoundEffectContentTypeReaderTest, Pcm8BitLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_8bit.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

// AUD-06-008: widened from "rejected" to real support, WAV-wrapped through SDL3's native
// IEEE-float decoder.
TEST_F(SoundEffectContentTypeReaderTest, IeeeFloatLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_float.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

// AUD-06-006: widened from "rejected" to real support -- also the first real end-to-end proof
// (independent of WaveBank.cpp's own AUDIO-ADPCM-001 fix) that the shared WavWrapper's MS-ADPCM
// coefficient table lets SDL3 decode a *real*, externally-produced MS-ADPCM XNB fixture, not just
// a hand-built one.
TEST_F(SoundEffectContentTypeReaderTest, MsAdpcmLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_msadpcm.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

// AUD-06-007: widened from "rejected" to real support, WAV-wrapped through SDL3's native
// IMA-ADPCM decoder.
TEST_F(SoundEffectContentTypeReaderTest, ImaAdpcmLoadsSuccessfully)
{
    auto effect = LoadFixture(std::string(kAudioDir) + "tone_mono_44khz_imaadpcm.xnb");
    EXPECT_EQ(effect.getNameProperty(), "test");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}

// XMA2 has no decode path anywhere in this stack (SDL3 doesn't decode XMA2 either) -- still
// correctly rejected. No real XMA2 .xnb fixture is vendored (MonoGame's own test corpus doesn't
// ship one either); this builds a minimal but complete XNB object stream (type-reader table +
// shared-resource count + root type id, matching the exact layout confirmed by hex-dumping a real
// fixture) around the WAVEFORMATEX header FNA's own XMA2 branch parses, since ReadAsset<T>()
// itself parses that preamble via InitializeTypeReaders() before ever reaching SoundEffectReader.
TEST_F(SoundEffectContentTypeReaderTest, Xma2IsRejected)
{
    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";

    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    // Type-reader table: 1 reader, name (7-bit length prefix, fits in one byte since < 128) +
    // bytes, readerVersion (int32) = 0.
    bytes.push_back(1); // typeReaderCount (7-bit int)
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0); // readerVersion

    bytes.push_back(0); // sharedResourceCount (7-bit int)
    bytes.push_back(1); // root object's type id (1-based index into the type-reader table)

    // formatLength(4) + wFormatTag(2)=0x166 + nChannels(2)=1 + nSamplesPerSec(4) +
    // nAvgBytesPerSec(4) + nBlockAlign(2) + wBitsPerSample(2) + cbSize(2)=34 + 34 XMA2-extra
    // bytes + dataLength(4)=0 + loopStart(4) + loopLength(4) + duration(4).
    w32(18 + 34);       // formatLength
    w16(0x166);         // wFormatTag: XMA2
    w16(1);             // nChannels
    w32(44100);         // nSamplesPerSec
    w32(88200);         // nAvgBytesPerSec
    w16(2);             // nBlockAlign
    w16(16);            // wBitsPerSample
    w16(34);            // cbSize
    for (int i = 0; i < 34; ++i) bytes.push_back(0); // XMA2 extra fields, all zero
    w32(0);             // data length
    w32(0);             // loopStart
    w32(0);             // loopLength
    w32(0);             // duration

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
    EXPECT_THROW(reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>(), ContentLoadException);
}

// AUD-06-013 (2026-07-17 deep audit): a malformed XNB claiming nSamplesPerSec=0 for one of the
// newly-supported WAV-wrapped formats (PCM8/float/MS-ADPCM/IMA-ADPCM, AUD-06) must not crash or
// silently construct a garbage SoundEffect -- it must fail deterministically. CNA's reader itself
// doesn't add a new sample-rate check (SoundEffectReader.Read() has none in real FNA either, and
// AUD-05-001's own investigation confirmed CNA deliberately doesn't add C#-level validation FNA
// itself lacks), but SDL3's own WAV loader validates this ("Invalid sample rate") -- confirmed
// here that the failure propagates as a clean, catchable exception all the way through
// SoundEffect::FromStream/BuildViaWavWrapper, not a crash or hang.
TEST_F(SoundEffectContentTypeReaderTest, Pcm8WithZeroSampleRateFailsCleanlyRatherThanCrashing)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(16);            // formatLength (no extension)
    w16(1);              // wFormatTag: PCM
    w16(1);               // nChannels: mono
    w32(0);               // nSamplesPerSec: malformed, zero
    w32(0);               // nAvgBytesPerSec
    w16(1);                // nBlockAlign
    w16(8);                // wBitsPerSample
    w32(16);              // data length
    for (int i = 0; i < 16; ++i) bytes.push_back(static_cast<uint8_t>(0x80)); // 8-bit PCM silence
    w32(0);               // loopStart
    w32(0);               // loopLength
    w32(0);               // duration

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');

    // AUD-06-024: BuildViaWavWrapper now re-throws the root SDL/backend decode failure as
    // ContentLoadException(assetContext, inner) instead of letting the raw NotSupportedException
    // propagate unadorned -- the asset name and format fields are now present, and the original
    // "Invalid sample rate"-style root cause is preserved verbatim via the " ---> " inner-
    // exception chain, not lost.
    try
    {
        reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& ex)
    {
        const std::string what = ex.what();
        EXPECT_NE(what.find("'test'"), std::string::npos) << what;
        EXPECT_NE(what.find("--->"), std::string::npos) << what;
    }
}

// AUD-06-015: Swap16/Swap32 (SoundEffectContentTypeReader.cpp) exist and are already wired to
// `input.getPlatformProperty() == 'x'`, but no fixture -- real or hand-built -- has ever actually
// exercised them; every other test in this file uses platform='w'. This builds a WAVEFORMATEX
// with every swapped field (wFormatTag/nChannels/nSamplesPerSec/nAvgBytesPerSec/nBlockAlign/
// wBitsPerSample) written in REAL big-endian byte order -- matching how a genuine Xbox 360
// (big-endian PowerPC)-produced XNB stores them -- while everything else in the XNB container
// (type-reader table, formatLength, data length, loop points, and the raw PCM sample data itself)
// stays little-endian, exactly matching real FNA's SoundEffectReader.Swap() calls, which only
// ever wrap the WAVEFORMATEX structure's own fields. If the swap were silently disabled, the
// resulting nSamplesPerSec (44100 written big-endian, misread as a little-endian uint32 without
// swapping) would decode as 1,151,051,776 Hz -- wildly different from the correct duration this
// test asserts, giving this real discriminating power, not just "didn't crash".
TEST_F(SoundEffectContentTypeReaderTest, XboxPlatformByteSwapsWaveFormatFieldsCorrectly)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };
    auto w16be = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v >> 8)); bytes.push_back(static_cast<uint8_t>(v)); };
    auto w32be = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v >> 24)); bytes.push_back(static_cast<uint8_t>(v >> 16));
        bytes.push_back(static_cast<uint8_t>(v >> 8)); bytes.push_back(static_cast<uint8_t>(v));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    // PCM16 mono @ 44100 Hz -- every WAVEFORMATEX field big-endian, container fields unchanged.
    w32(16);          // formatLength -- NOT swapped, plain little-endian container field
    w16be(1);         // wFormatTag: PCM
    w16be(1);         // nChannels: mono
    w32be(44100);     // nSamplesPerSec
    w32be(44100 * 2); // nAvgBytesPerSec (mono, 16-bit: rate * 1 channel * 2 bytes)
    w16be(2);         // nBlockAlign (1 channel * 2 bytes)
    w16be(16);        // wBitsPerSample

    std::vector<uint8_t> pcm(2 * 1024, 0); // 1024 mono S16 frames, little-endian samples (never swapped)
    w32(static_cast<uint32_t>(pcm.size())); // data length -- NOT swapped
    bytes.insert(bytes.end(), pcm.begin(), pcm.end());
    w32(0); // loopStart
    w32(0); // loopLength
    w32(0); // duration

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'x'); // platform='x': Xbox 360
    auto effect = reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();

    EXPECT_NEAR(effect.getDurationProperty().getTotalSecondsProperty(), 1024.0 / 44100.0, 1e-6);
}

// ---------------------------------------------------------------------------
// AUD-06-012: SoundEffectReader::Read() passes its declared audio-data length straight to
// input.ReadBytesExactOrThrow() with no length check of its own -- this is safe because that
// shared helper (ContentReader.cpp) already rejects a negative count outright, and
// BinaryReader::ReadBytes() (sharp-runtime) already clamps its eager allocation to the stream's
// own real remaining length before ever allocating, so a corrupt/adversarial declared length can
// never force an allocation for bytes that could never be read anyway. These tests exercise that
// existing protection specifically through the SoundEffectReader's own data-length field, not
// just the shared helper in isolation.
// ---------------------------------------------------------------------------

TEST_F(SoundEffectContentTypeReaderTest, NegativeDataLengthFailsCleanlyRatherThanCrashing)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(16);   // formatLength (no extension)
    w16(1);    // wFormatTag: PCM
    w16(1);    // nChannels: mono
    w32(44100); // nSamplesPerSec
    w32(88200); // nAvgBytesPerSec
    w16(2);    // nBlockAlign
    w16(16);   // wBitsPerSample
    w32(static_cast<uint32_t>(-100)); // data length: negative
    w32(0);    // loopStart
    w32(0);    // loopLength
    w32(0);    // duration

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
    EXPECT_THROW(
        reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>(),
        ContentLoadException);
}

TEST_F(SoundEffectContentTypeReaderTest, OversizedDataLengthAgainstTruncatedStreamFailsCleanlyNotWithHugeAllocation)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(16);   // formatLength (no extension)
    w16(1);    // wFormatTag: PCM
    w16(1);    // nChannels: mono
    w32(44100); // nSamplesPerSec
    w32(88200); // nAvgBytesPerSec
    w16(2);    // nBlockAlign
    w16(16);   // wBitsPerSample
    // Claims 100 MB of audio data, but the stream actually ends here -- BinaryReader::ReadBytes's
    // allocation-clamp (sharp-runtime) must never attempt a 100 MB allocation for bytes that
    // provably cannot exist; the fixture simply ends, no loop points/duration trailer follow.
    w32(100u * 1024u * 1024u);

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
    EXPECT_THROW(
        reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>(),
        System::IO::EndOfStreamException);
}

// ---------------------------------------------------------------------------
// AUD-06-011: formatLength drives two separate `int64_t skip = formatLength - 18[- 34]` /
// `static_cast<int32_t>(skip)` computations (SoundEffectContentTypeReader.cpp) that must never
// desynchronize the reader (silently read the wrong number of extension bytes) or read
// out-of-bounds, however corrupt the declared value is. These tests exercise the boundary and
// the pathological-large-value case specifically.
// ---------------------------------------------------------------------------

// formatLength=17 is one byte short of even covering its own cbSize field (16-byte base + 2-byte
// cbSize = 18 minimum for any formatLength > 16) -- `skip = 17 - 18 = -1` must be caught by the
// existing negative-count guard, not silently truncated/misread.
TEST_F(SoundEffectContentTypeReaderTest, FormatLengthOneByteTooSmallForCbSizeThrowsCleanly)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(17);   // formatLength: > 16, but too small to contain even a cbSize field
    w16(1);    // wFormatTag: PCM
    w16(1);    // nChannels: mono
    w32(44100); // nSamplesPerSec
    w32(88200); // nAvgBytesPerSec
    w16(2);    // nBlockAlign
    w16(16);   // wBitsPerSample
    w16(0);    // cbSize (read since formatLength > 16)
    // No further bytes -- the reader should throw before ever trying to read a negative-length
    // extension, so nothing past cbSize needs to exist for this fixture to be a valid test.

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
    EXPECT_THROW(
        reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>(),
        ContentLoadException);
}

// A pathologically large formatLength (near uint32_t's own ceiling) must not desync the reader
// into misreading downstream fields -- `skip` (int64) stays bounded by formatLength's own
// uint32_t range, so casting to int32_t always either produces the correct positive value or
// wraps to negative (caught by the existing guard), never a small-but-wrong positive value that
// would silently consume the wrong number of bytes.
TEST_F(SoundEffectContentTypeReaderTest, PathologicallyLargeFormatLengthThrowsCleanlyNotDesync)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(0xFFFFFFF0u); // formatLength: near uint32_t's ceiling
    w16(1);            // wFormatTag: PCM
    w16(1);            // nChannels: mono
    w32(44100);        // nSamplesPerSec
    w32(88200);        // nAvgBytesPerSec
    w16(2);            // nBlockAlign
    w16(16);           // wBitsPerSample
    w16(0);            // cbSize

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
    EXPECT_THROW(
        reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>(),
        ContentLoadException);
}

// AUD-06-017: an unknown/unsupported format tag's diagnostic must name every field useful for
// triaging against a real asset -- tag, bit depth, channels, sample rate, and the asset name --
// not just tag/bits, matching the acceptance criterion exactly.
TEST_F(SoundEffectContentTypeReaderTest, UnknownFormatTagDiagnosticIncludesAllRelevantFields)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(16);       // formatLength (no extension)
    w16(0x270B);   // wFormatTag: a genuinely unknown/unassigned tag
    w16(2);        // nChannels: stereo
    w32(22050);    // nSamplesPerSec
    w32(88200);    // nAvgBytesPerSec
    w16(4);        // nBlockAlign
    w16(24);       // wBitsPerSample: not a bit depth CNA supports for any known tag
    w32(0);        // data length
    w32(0);        // loopStart
    w32(0);        // loopLength
    w32(0);        // duration

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');

    try
    {
        reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();
        FAIL() << "expected ContentLoadException";
    }
    catch (const ContentLoadException& ex)
    {
        const std::string what = ex.what();
        EXPECT_NE(what.find("9995"), std::string::npos) << what;   // formatTag=0x270B decimal
        EXPECT_NE(what.find("24"), std::string::npos) << what;     // bitsPerSample
        EXPECT_NE(what.find("channels=2"), std::string::npos) << what;
        EXPECT_NE(what.find("22050"), std::string::npos) << what;  // sampleRate
        EXPECT_NE(what.find("'test'"), std::string::npos) << what; // asset name
    }
}

// AUD-06-013: PCM8 stereo with a wildly wrong nBlockAlign (declared 100, the coherent value for
// nChannels=2/wBitsPerSample=8 is 2). Empirically confirmed SDL3's own WAV loader does NOT trust
// the file's declared nBlockAlign for PCM -- it recomputes the correct block size from
// nChannels*wBitsPerSample/8 internally, so a corrupt/incoherent nBlockAlign field cannot produce
// a wrong decoded frame count or duration; it's silently (and correctly) ignored rather than
// either rejected or trusted. With the correct block size (2 bytes/frame) 20 bytes decodes as
// exactly 10 frames (confirmed: 20/2=10 frames, 10/44100 ~= 0.000227s, matching the value
// observed here before this comment was written from a raw debug probe). No CNA-side coherence
// check is needed on top of this -- the backend already can't be confused by this specific class
// of incoherent WAVEFORMATEX.
TEST_F(SoundEffectContentTypeReaderTest, IncoherentBlockAlignForPcm8IsIgnoredNotTrustedBySdlDecoder)
{
    std::vector<uint8_t> bytes;
    auto w16 = [&bytes](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    auto w32 = [&bytes](uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16)); bytes.push_back(static_cast<uint8_t>(v >> 24));
    };

    const std::string readerName = "Microsoft.Xna.Framework.Content.SoundEffectReader";
    bytes.push_back(1);
    bytes.push_back(static_cast<uint8_t>(readerName.size()));
    bytes.insert(bytes.end(), readerName.begin(), readerName.end());
    w32(0);
    bytes.push_back(0);
    bytes.push_back(1);

    w32(16);      // formatLength
    w16(1);       // wFormatTag: PCM
    w16(2);       // nChannels: stereo
    w32(44100);   // nSamplesPerSec
    w32(44100);   // nAvgBytesPerSec
    w16(100);     // nBlockAlign: incoherent with nChannels/wBitsPerSample (correct value is 2)
    w16(8);       // wBitsPerSample
    w32(20);      // data length -- 10 real stereo 8-bit frames if the coherent block size is used
    for (int i = 0; i < 20; ++i) bytes.push_back(static_cast<uint8_t>(0x80 + i));
    w32(0); w32(0); w32(0);

    body_ = std::make_unique<System::IO::MemoryStream>(bytes.data(), static_cast<int32_t>(bytes.size()));
    reader_ = std::make_unique<ContentReader>(&cm_, body_.get(), "test", 5, 'w');
    auto effect = reader_->ReadAsset<Microsoft::Xna::Framework::Audio::SoundEffect>();

    EXPECT_NEAR(effect.getDurationProperty().getTotalSecondsProperty(), 10.0 / 44100.0, 1e-6);
}
