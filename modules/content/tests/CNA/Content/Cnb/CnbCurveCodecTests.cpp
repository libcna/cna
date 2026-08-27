// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-041/CNBF-042 (Phase B tests): the Curve asset schema. Round trip and
// determinism first, then one negative test per way a Curve .cnb can be malformed -- each built
// by taking a valid file and breaking exactly one thing.

#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

using CNA::Content::Cnb::CnbByteWriter;
using CNA::Content::Cnb::CnbCurveKeyStride;
using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::DecodeCurveFromCnb;
using CNA::Content::Cnb::EncodeCurveToCnb;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;
namespace CnbCurveChunk = CNA::Content::Cnb::CnbCurveChunk;

namespace
{
    Curve MakeSampleCurve()
    {
        Curve curve;
        curve.setPreLoopProperty(CurveLoopType::Oscillate);
        curve.setPostLoopProperty(CurveLoopType::CycleOffset);
        curve.getKeysProperty().Add(CurveKey(0.0f, 1.5f, 0.25f, -0.25f, CurveContinuity::Smooth));
        curve.getKeysProperty().Add(CurveKey(1.0f, -3.0f, 0.0f, 2.0f, CurveContinuity::Step));
        curve.getKeysProperty().Add(CurveKey(2.5f, 7.25f, -1.0f, 1.0f, CurveContinuity::Smooth));
        return curve;
    }

    CnbDocument ParseBytes(const std::vector<std::uint8_t>& bytes)
    {
        return CnbDocument::Parse(bytes, "curve.cnb");
    }

    /// Rebuilds a Curve .cnb from raw chunk payloads, so a test can put deliberately inconsistent
    /// bytes in one chunk while the container around them stays perfectly valid.
    std::vector<std::uint8_t> BuildCurveFile(const std::vector<std::uint8_t>& headerChunk,
                                             const std::vector<std::uint8_t>& keyChunk,
                                             std::uint32_t schemaVersion = 1u)
    {
        CnbWriter writer(CnbAssetTypeId::Curve, schemaVersion);
        writer.AddChunk(CnbCurveChunk::Header, headerChunk, CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbCurveChunk::Keys, keyChunk, CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }

    std::vector<std::uint8_t> ValidHeaderChunk(std::uint32_t keyCount)
    {
        CnbByteWriter w;
        w.WriteU32(static_cast<std::uint32_t>(CurveLoopType::Constant));
        w.WriteU32(static_cast<std::uint32_t>(CurveLoopType::Linear));
        w.WriteU32(keyCount);
        return w.Take();
    }

    std::vector<std::uint8_t> ValidKeyChunk(std::uint32_t keyCount)
    {
        CnbByteWriter w;
        for (std::uint32_t i = 0; i < keyCount; ++i)
        {
            w.WriteF32(static_cast<float>(i));
            w.WriteF32(static_cast<float>(i) * 2.0f);
            w.WriteF32(0.0f);
            w.WriteF32(0.0f);
            w.WriteU32(0u);
        }
        return w.Take();
    }
}

TEST(CnbCurveCodecTest, RoundTripsEveryFieldExactly)
{
    const Curve original = MakeSampleCurve();
    const CnbDocument doc = ParseBytes(EncodeCurveToCnb(original, "Curves/wobble"));
    const Curve decoded = DecodeCurveFromCnb(doc);

    EXPECT_EQ(decoded.getPreLoopProperty(), CurveLoopType::Oscillate);
    EXPECT_EQ(decoded.getPostLoopProperty(), CurveLoopType::CycleOffset);
    ASSERT_EQ(decoded.getKeysProperty().getCountProperty(), 3);

    for (int i = 0; i < 3; ++i)
    {
        const CurveKey& a = original.getKeysProperty()[i];
        const CurveKey& b = decoded.getKeysProperty()[i];
        EXPECT_FLOAT_EQ(b.getPositionProperty(), a.getPositionProperty()) << "key " << i;
        EXPECT_FLOAT_EQ(b.getValueProperty(), a.getValueProperty()) << "key " << i;
        EXPECT_FLOAT_EQ(b.getTangentInProperty(), a.getTangentInProperty()) << "key " << i;
        EXPECT_FLOAT_EQ(b.getTangentOutProperty(), a.getTangentOutProperty()) << "key " << i;
        EXPECT_EQ(b.getContinuityProperty(), a.getContinuityProperty()) << "key " << i;
    }

    // Evaluating both curves at the same positions is the observable behaviour that actually
    // matters; matching fields would not prove it on its own.
    for (float t = -1.0f; t <= 4.0f; t += 0.25f)
    {
        EXPECT_FLOAT_EQ(decoded.Evaluate(t), original.Evaluate(t)) << "t=" << t;
    }
}

TEST(CnbCurveCodecTest, RecordsItsDebugMetadataAndAssetType)
{
    const CnbDocument doc = ParseBytes(EncodeCurveToCnb(MakeSampleCurve(), "Curves/wobble"));
    EXPECT_EQ(doc.AssetTypeId(), CnbAssetTypeId::Curve);
    EXPECT_EQ(doc.AssetSchemaVersion(), 1u);
    ASSERT_TRUE(doc.Metadata().present);
    EXPECT_EQ(doc.Metadata().assetTypeName, "Microsoft.Xna.Framework.Curve");
    EXPECT_EQ(doc.Metadata().contentName, "Curves/wobble");
}

TEST(CnbCurveCodecTest, EmptyCurveRoundTrips)
{
    Curve empty;
    const CnbDocument doc = ParseBytes(EncodeCurveToCnb(empty));
    const Curve decoded = DecodeCurveFromCnb(doc);
    EXPECT_EQ(decoded.getKeysProperty().getCountProperty(), 0);
    EXPECT_EQ(decoded.getPreLoopProperty(), empty.getPreLoopProperty());
    EXPECT_EQ(decoded.getPostLoopProperty(), empty.getPostLoopProperty());
}

TEST(CnbCurveCodecTest, EncodingIsDeterministic)
{
    const Curve curve = MakeSampleCurve();
    EXPECT_EQ(EncodeCurveToCnb(curve, "Curves/wobble"), EncodeCurveToCnb(curve, "Curves/wobble"));
}

TEST(CnbCurveCodecTest, KeysAreStoredAsOneFlatFixedStrideArray)
{
    // The compiled property this schema exists for: the whole key set is one contiguous run whose
    // length is exactly count * stride, addressable without walking anything.
    const CnbDocument doc = ParseBytes(EncodeCurveToCnb(MakeSampleCurve()));
    const std::size_t keys = doc.RequireSingle(CnbCurveChunk::Keys);
    EXPECT_EQ(doc.ChunkData(keys).size(), 3u * CnbCurveKeyStride);
}

TEST(CnbCurveCodecTest, RejectsAWrongAssetType)
{
    CnbWriter writer(CnbAssetTypeId::Model, 1u);
    writer.AddChunk(CnbCurveChunk::Header, ValidHeaderChunk(0u), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CnbCurveChunk::Keys, {}, CnbChunkFlags::Mandatory, 4u);
    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "notacurve.cnb");
    EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
}

TEST(CnbCurveCodecTest, RejectsAFutureSchemaVersion)
{
    const CnbDocument doc = ParseBytes(BuildCurveFile(ValidHeaderChunk(0u), {}, 2u));
    EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
}

TEST(CnbCurveCodecTest, RejectsAMissingMandatoryChunk)
{
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(CnbCurveChunk::Keys, {}, CnbChunkFlags::Mandatory, 4u);
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "noheader.cnb");
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
    {
        CnbWriter writer(CnbAssetTypeId::Curve, 1u);
        writer.AddChunk(CnbCurveChunk::Header, ValidHeaderChunk(0u), CnbChunkFlags::Mandatory, 4u);
        const CnbDocument doc = CnbDocument::Parse(writer.Build(), "nokeys.cnb");
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbCurveCodecTest, RejectsAnUnknownMandatoryChunk)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(CnbCurveChunk::Header, ValidHeaderChunk(0u), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CnbCurveChunk::Keys, {}, CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::MakeChunkId('c', 'r', 'v', 'x'), {1, 2, 3},
                    CnbChunkFlags::Mandatory, 4u);
    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "future.cnb");
    EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
}

TEST(CnbCurveCodecTest, IgnoresAnUnknownOptionalChunk)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(CnbCurveChunk::Header, ValidHeaderChunk(1u), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CnbCurveChunk::Keys, ValidKeyChunk(1u), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CNA::Content::Cnb::MakeChunkId('c', 'r', 'v', 'x'), {1, 2, 3},
                    CnbChunkFlags::None, 4u);
    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "future.cnb");

    const Curve decoded = DecodeCurveFromCnb(doc);
    EXPECT_EQ(decoded.getKeysProperty().getCountProperty(), 1);
}

TEST(CnbCurveCodecTest, RejectsADuplicateSchemaChunk)
{
    CnbWriter writer(CnbAssetTypeId::Curve, 1u);
    writer.AddChunk(CnbCurveChunk::Header, ValidHeaderChunk(0u), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CnbCurveChunk::Header, ValidHeaderChunk(0u), CnbChunkFlags::Mandatory, 4u);
    writer.AddChunk(CnbCurveChunk::Keys, {}, CnbChunkFlags::Mandatory, 4u);
    const CnbDocument doc = CnbDocument::Parse(writer.Build(), "dup.cnb");
    EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
}

TEST(CnbCurveCodecTest, RejectsAnOutOfRangeLoopType)
{
    for (const std::uint32_t badValue : {5u, 0xFFFFFFFFu})
    {
        {
            CnbByteWriter w;
            w.WriteU32(badValue);
            w.WriteU32(0u);
            w.WriteU32(0u);
            const CnbDocument doc = ParseBytes(BuildCurveFile(w.Take(), {}));
            EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException) << "preLoop " << badValue;
        }
        {
            CnbByteWriter w;
            w.WriteU32(0u);
            w.WriteU32(badValue);
            w.WriteU32(0u);
            const CnbDocument doc = ParseBytes(BuildCurveFile(w.Take(), {}));
            EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException) << "postLoop " << badValue;
        }
    }
}

TEST(CnbCurveCodecTest, RejectsAnOutOfRangeContinuity)
{
    CnbByteWriter keys;
    keys.WriteF32(0.0f);
    keys.WriteF32(0.0f);
    keys.WriteF32(0.0f);
    keys.WriteF32(0.0f);
    keys.WriteU32(2u);
    const CnbDocument doc = ParseBytes(BuildCurveFile(ValidHeaderChunk(1u), keys.Take()));
    EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
}

TEST(CnbCurveCodecTest, RejectsAKeyChunkWhoseLengthDisagreesWithTheDeclaredCount)
{
    // Too short.
    {
        const CnbDocument doc = ParseBytes(BuildCurveFile(ValidHeaderChunk(4u), ValidKeyChunk(3u)));
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
    // Too long: extra bytes mean the two chunks disagree, which is just as wrong as too few.
    {
        const CnbDocument doc = ParseBytes(BuildCurveFile(ValidHeaderChunk(2u), ValidKeyChunk(3u)));
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
    // Off by a single byte.
    {
        std::vector<std::uint8_t> keys = ValidKeyChunk(2u);
        keys.push_back(0u);
        const CnbDocument doc = ParseBytes(BuildCurveFile(ValidHeaderChunk(2u), keys));
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
}

TEST(CnbCurveCodecTest, RejectsAnEnormousDeclaredKeyCountWithoutAllocating)
{
    const CnbDocument doc = ParseBytes(BuildCurveFile(ValidHeaderChunk(0xFFFFFFFFu), {}));
    EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
}

TEST(CnbCurveCodecTest, RejectsATruncatedOrOverlongHeaderChunk)
{
    {
        std::vector<std::uint8_t> header = ValidHeaderChunk(0u);
        header.resize(header.size() - 1u);
        const CnbDocument doc = ParseBytes(BuildCurveFile(header, {}));
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
    {
        std::vector<std::uint8_t> header = ValidHeaderChunk(0u);
        header.push_back(0u);
        const CnbDocument doc = ParseBytes(BuildCurveFile(header, {}));
        EXPECT_THROW((void)DecodeCurveFromCnb(doc), ContentLoadException);
    }
}
