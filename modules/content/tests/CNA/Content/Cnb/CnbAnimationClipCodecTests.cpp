// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-051/CNBF-052 (Phase C tests): the AnimationClip asset schema -- round trip,
// determinism, the flat-key-array invariant that makes it a compiled representation, and one
// negative test per way the three chunks can disagree with each other.

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

using CNA::Content::Cnb::CnbAnimationKeyStride;
using CNA::Content::Cnb::CnbAnimationTrackStride;
using CNA::Content::Cnb::CnbByteWriter;
using CNA::Content::Cnb::CnbChunkId;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::DecodeAnimationClipFromCnb;
using CNA::Content::Cnb::EncodeAnimationClipToCnb;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CnbAnimationClipChunk = CNA::Content::Cnb::CnbAnimationClipChunk;
namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;

namespace
{
    KeyframeEXT MakeKey(double seconds, float base)
    {
        KeyframeEXT key;
        key.Time = System::TimeSpan::FromSeconds(seconds);
        key.Translation = Vector3(base, base + 1.0f, base + 2.0f);
        key.Rotation = Quaternion(base * 0.1f, base * 0.2f, base * 0.3f, 1.0f);
        key.Scale = Vector3(base + 3.0f, base + 4.0f, base + 5.0f);
        return key;
    }

    AnimationClipEXT MakeSampleClip()
    {
        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::FromSeconds(2.5);
        clip.TargetSpace = ClipTargetSpaceEXT::SceneNode;

        BoneTrackEXT a;
        a.BoneIndex = 3;
        a.Keys = {MakeKey(0.0, 1.0f), MakeKey(1.0, 2.0f), MakeKey(2.5, 3.0f)};

        BoneTrackEXT b;
        b.BoneIndex = -1;
        b.Keys = {MakeKey(0.5, 10.0f)};

        BoneTrackEXT empty;
        empty.BoneIndex = 7;

        clip.Tracks = {a, b, empty};
        return clip;
    }

    CnbDocument ParseBytes(const std::vector<std::uint8_t>& bytes)
    {
        return CnbDocument::Parse(bytes, "clip.cnb");
    }

    std::vector<std::uint8_t> HeaderChunk(double duration, std::uint32_t space,
                                          std::uint32_t trackCount, std::uint32_t keyCount)
    {
        CnbByteWriter w;
        w.WriteF64(duration);
        w.WriteU32(space);
        w.WriteU32(trackCount);
        w.WriteU32(keyCount);
        return w.Take();
    }

    std::vector<std::uint8_t> TrackChunk(
        const std::vector<std::array<std::uint32_t, 3>>& rows)
    {
        CnbByteWriter w;
        for (const auto& row : rows)
        {
            w.WriteI32(static_cast<std::int32_t>(row[0]));
            w.WriteU32(row[1]);
            w.WriteU32(row[2]);
        }
        return w.Take();
    }

    std::vector<std::uint8_t> KeyChunk(std::uint32_t count)
    {
        CnbByteWriter w;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            w.WriteF64(static_cast<double>(i));
            for (int f = 0; f < 10; ++f) { w.WriteF32(static_cast<float>(f)); }
        }
        return w.Take();
    }

    std::vector<std::uint8_t> BuildClipFile(const std::vector<std::uint8_t>& header,
                                            const std::vector<std::uint8_t>& tracks,
                                            const std::vector<std::uint8_t>& keys,
                                            std::uint32_t schemaVersion = 1u)
    {
        CnbWriter writer(CnbAssetTypeId::AnimationClip, schemaVersion);
        writer.AddChunk(CnbAnimationClipChunk::Header, header, CnbChunkFlags::Mandatory, 8u);
        writer.AddChunk(CnbAnimationClipChunk::Tracks, tracks, CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbAnimationClipChunk::Keys, keys, CnbChunkFlags::Mandatory, 8u);
        return writer.Build();
    }

    void ExpectSameClip(const AnimationClipEXT& a, const AnimationClipEXT& b)
    {
        EXPECT_DOUBLE_EQ(b.Duration.getTotalSecondsProperty(), a.Duration.getTotalSecondsProperty());
        EXPECT_EQ(b.TargetSpace, a.TargetSpace);
        ASSERT_EQ(b.Tracks.size(), a.Tracks.size());
        for (std::size_t t = 0; t < a.Tracks.size(); ++t)
        {
            EXPECT_EQ(b.Tracks[t].BoneIndex, a.Tracks[t].BoneIndex) << "track " << t;
            ASSERT_EQ(b.Tracks[t].Keys.size(), a.Tracks[t].Keys.size()) << "track " << t;
            for (std::size_t k = 0; k < a.Tracks[t].Keys.size(); ++k)
            {
                const KeyframeEXT& x = a.Tracks[t].Keys[k];
                const KeyframeEXT& y = b.Tracks[t].Keys[k];
                EXPECT_EQ(y.Time.getTicksProperty(), x.Time.getTicksProperty());
                EXPECT_FLOAT_EQ(y.Translation.X, x.Translation.X);
                EXPECT_FLOAT_EQ(y.Translation.Y, x.Translation.Y);
                EXPECT_FLOAT_EQ(y.Translation.Z, x.Translation.Z);
                EXPECT_FLOAT_EQ(y.Rotation.X, x.Rotation.X);
                EXPECT_FLOAT_EQ(y.Rotation.Y, x.Rotation.Y);
                EXPECT_FLOAT_EQ(y.Rotation.Z, x.Rotation.Z);
                EXPECT_FLOAT_EQ(y.Rotation.W, x.Rotation.W);
                EXPECT_FLOAT_EQ(y.Scale.X, x.Scale.X);
                EXPECT_FLOAT_EQ(y.Scale.Y, x.Scale.Y);
                EXPECT_FLOAT_EQ(y.Scale.Z, x.Scale.Z);
            }
        }
    }
}

TEST(CnbAnimationClipCodecTest, RoundTripsEveryFieldExactly)
{
    const AnimationClipEXT original = MakeSampleClip();
    const AnimationClipEXT decoded =
        DecodeAnimationClipFromCnb(ParseBytes(EncodeAnimationClipToCnb(original, "Clips/walk")));
    ExpectSameClip(original, decoded);
}

TEST(CnbAnimationClipCodecTest, EmptyClipRoundTrips)
{
    AnimationClipEXT empty;
    const AnimationClipEXT decoded =
        DecodeAnimationClipFromCnb(ParseBytes(EncodeAnimationClipToCnb(empty)));
    EXPECT_TRUE(decoded.Tracks.empty());
    EXPECT_EQ(decoded.TargetSpace, ClipTargetSpaceEXT::JointPalette);
    EXPECT_EQ(decoded.Duration.getTicksProperty(), 0);
}

TEST(CnbAnimationClipCodecTest, EncodingIsDeterministic)
{
    const AnimationClipEXT clip = MakeSampleClip();
    EXPECT_EQ(EncodeAnimationClipToCnb(clip, "Clips/walk"),
              EncodeAnimationClipToCnb(clip, "Clips/walk"));
}

TEST(CnbAnimationClipCodecTest, EveryKeyframeLivesInOneFlatArrayAddressedByTrackRanges)
{
    // This is the whole point of the compiled shape: three tracks, one contiguous key run, whose
    // length is exactly totalKeys * stride -- not three separately-located nested arrays.
    const CnbDocument doc = ParseBytes(EncodeAnimationClipToCnb(MakeSampleClip()));
    EXPECT_EQ(doc.ChunkData(doc.RequireSingle(CnbAnimationClipChunk::Keys)).size(),
              4u * CnbAnimationKeyStride);
    EXPECT_EQ(doc.ChunkData(doc.RequireSingle(CnbAnimationClipChunk::Tracks)).size(),
              3u * CnbAnimationTrackStride);
}

TEST(CnbAnimationClipCodecTest, TracksMayShareAnIdenticalKeyRange)
{
    // Two tracks pointing at the same key range is an unusual but perfectly valid encoding; the
    // reader must not assume the ranges partition the array.
    const std::vector<std::uint8_t> bytes = BuildClipFile(
        HeaderChunk(1.0, 0u, 2u, 2u),
        TrackChunk({{0u, 0u, 2u}, {1u, 0u, 2u}}),
        KeyChunk(2u));
    const AnimationClipEXT clip = DecodeAnimationClipFromCnb(ParseBytes(bytes));
    ASSERT_EQ(clip.Tracks.size(), 2u);
    EXPECT_EQ(clip.Tracks[0].Keys.size(), 2u);
    EXPECT_EQ(clip.Tracks[1].Keys.size(), 2u);
    EXPECT_EQ(clip.Tracks[1].Keys[0].Time.getTicksProperty(),
              clip.Tracks[0].Keys[0].Time.getTicksProperty());
}

TEST(CnbAnimationClipCodecTest, RejectsAWrongAssetTypeOrFutureSchemaVersion)
{
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(CnbAnimationClipChunk::Header, HeaderChunk(0.0, 0u, 0u, 0u),
                        CnbChunkFlags::Mandatory, 8u);
        writer.AddChunk(CnbAnimationClipChunk::Tracks, {}, CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbAnimationClipChunk::Keys, {}, CnbChunkFlags::Mandatory, 8u);
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "wrong.cnb");
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(HeaderChunk(0.0, 0u, 0u, 0u), {}, {}, 2u));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbAnimationClipCodecTest, RejectsAMissingMandatoryChunk)
{
    const CnbChunkId all[] = {CnbAnimationClipChunk::Header, CnbAnimationClipChunk::Tracks,
                              CnbAnimationClipChunk::Keys};
    for (std::size_t omit = 0; omit < 3u; ++omit)
    {
        CnbWriter writer(CnbAssetTypeId::AnimationClip, 1u);
        for (std::size_t i = 0; i < 3u; ++i)
        {
            if (i == omit) { continue; }
            std::vector<std::uint8_t> payload;
            if (i == 0) { payload = HeaderChunk(0.0, 0u, 0u, 0u); }
            writer.AddChunk(all[i], payload, CnbChunkFlags::Mandatory, 4u);
        }
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "missing.cnb");
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException)
            << "omitting chunk " << omit;
    }
}

TEST(CnbAnimationClipCodecTest, RejectsAnOutOfRangeTargetSpace)
{
    for (const std::uint32_t space : {2u, 0xFFFFFFFFu})
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(HeaderChunk(0.0, space, 0u, 0u), {}, {}));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException) << "space " << space;
    }
}

TEST(CnbAnimationClipCodecTest, RejectsATrackRangeThatLeavesTheKeyArray)
{
    // firstKey inside, but firstKey+count past the end.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 2u), TrackChunk({{0u, 1u, 5u}}), KeyChunk(2u)));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
    // firstKey already past the end.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 2u), TrackChunk({{0u, 9u, 0u}}), KeyChunk(2u)));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
    // firstKey + count chosen to wrap a 32-bit sum; the checked arithmetic must catch it.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 2u), TrackChunk({{0u, 0xFFFFFFF0u, 0x20u}}), KeyChunk(2u)));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbAnimationClipCodecTest, RejectsChunkLengthsThatDisagreeWithTheDeclaredCounts)
{
    // Track chunk too short.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 3u, 0u), TrackChunk({{0u, 0u, 0u}}), {}));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
    // Track chunk too long.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 0u), TrackChunk({{0u, 0u, 0u}, {1u, 0u, 0u}}), {}));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
    // Key chunk too short.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 4u), TrackChunk({{0u, 0u, 4u}}), KeyChunk(3u)));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
    // Key chunk too long.
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 2u), TrackChunk({{0u, 0u, 2u}}), KeyChunk(3u)));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbAnimationClipCodecTest, RejectsATimeThatCannotBeATimeSpan)
{
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // A malformed duration must surface as a ContentLoadException naming the file, not as the
    // System::ArgumentException TimeSpan::FromSeconds would raise from deep inside the decoder.
    for (const double bad : {infinity, -infinity, nan, 1.0e18})
    {
        const CnbDocument doc = ParseBytes(BuildClipFile(HeaderChunk(bad, 0u, 0u, 0u), {}, {}));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException) << "duration " << bad;
    }

    // ... and the same for a keyframe time.
    {
        CnbByteWriter keys;
        keys.WriteF64(nan);
        for (int f = 0; f < 10; ++f) { keys.WriteF32(0.0f); }
        const CnbDocument doc = ParseBytes(BuildClipFile(
            HeaderChunk(1.0, 0u, 1u, 1u), TrackChunk({{0u, 0u, 1u}}), keys.Take()));
        EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbAnimationClipCodecTest, RejectsAnEnormousDeclaredCountWithoutAllocating)
{
    const CnbDocument doc =
        ParseBytes(BuildClipFile(HeaderChunk(1.0, 0u, 0xFFFFFFFFu, 0xFFFFFFFFu), {}, {}));
    EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
}

TEST(CnbAnimationClipCodecTest, RejectsATruncatedHeaderChunk)
{
    std::vector<std::uint8_t> header = HeaderChunk(1.0, 0u, 0u, 0u);
    header.resize(header.size() - 2u);
    const CnbDocument doc = ParseBytes(BuildClipFile(header, {}, {}));
    EXPECT_THROW((void)DecodeAnimationClipFromCnb(doc), ContentLoadException);
}
