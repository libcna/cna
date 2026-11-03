// SPDX-License-Identifier: MS-PL
//
// XNB-20: unit tests for CurveReader.

#include <any>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include "CNA/Internal/Xnb/CurveContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class CurveReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterCurveXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

TEST_F(CurveReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.CurveReader"));
}

TEST_F(CurveReaderTest, ReadsLoopTypesAndTwoKeysInOrder)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)CurveLoopType::Linear);   // PreLoop
    writer.Write((int32_t)CurveLoopType::Constant);  // PostLoop
    writer.Write((int32_t)2); // key count
    // Key 0
    writer.Write(0.0f); writer.Write(1.0f); writer.Write(0.0f); writer.Write(0.0f);
    writer.Write((int32_t)CurveContinuity::Smooth);
    // Key 1
    writer.Write(1.0f); writer.Write(2.0f); writer.Write(0.5f); writer.Write(0.5f);
    writer.Write((int32_t)CurveContinuity::Step);
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(nullptr, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.CurveReader");
    ASSERT_NE(typeReader, nullptr);
    const Curve curve = std::any_cast<Curve>(typeReader->ReadUntyped(reader, std::any{}));

    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Linear);
    EXPECT_EQ(curve.getPostLoopProperty(), CurveLoopType::Constant);
    ASSERT_EQ(curve.getKeysProperty().getCountProperty(), 2);

    const auto& key0 = curve.getKeysProperty()[0];
    EXPECT_FLOAT_EQ(key0.getPositionProperty(), 0.0f);
    EXPECT_FLOAT_EQ(key0.getValueProperty(), 1.0f);
    EXPECT_EQ(key0.getContinuityProperty(), CurveContinuity::Smooth);

    const auto& key1 = curve.getKeysProperty()[1];
    EXPECT_FLOAT_EQ(key1.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(key1.getValueProperty(), 2.0f);
    EXPECT_FLOAT_EQ(key1.getTangentInProperty(), 0.5f);
    EXPECT_FLOAT_EQ(key1.getTangentOutProperty(), 0.5f);
    EXPECT_EQ(key1.getContinuityProperty(), CurveContinuity::Step);
}

namespace
{
    // Hand-encoded little-endian binary32 values used below: 1.0f = 0x3f800000,
    // 2.0f = 0x40000000, 3.0f = 0x40400000, 4.0f = 0x40800000, -0.5f = 0xbf000000.
    void PushFloatBits(std::vector<uint8_t>& bytes, uint32_t bits)
    {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<uint8_t>(bits >> shift));
    }

    Curve ReadCurve(const std::vector<uint8_t>& bytes, std::optional<Curve> existing,
                    int64_t* consumed = nullptr)
    {
        System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader reader(nullptr, &stream, "curve", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.CurveReader");
        std::any existingAny = existing ? std::any(*existing) : std::any{};
        Curve curve = std::any_cast<Curve>(typeReader->ReadUntyped(reader, std::move(existingAny)));
        if (consumed) *consumed = stream.getPositionProperty();
        return curve;
    }
}

// TQ-004: XNA CurveReader.get_CanDeserializeIntoExistingObject is `ldc.i4.1; ret` -- it reads into
// an existing Curve. (FNA's CurveReader leaves the base class's false; XNA wins.)
TEST_F(CurveReaderTest, ReportsThatItCanDeserializeIntoAnExistingCurve)
{
    const auto reader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.CurveReader");
    ASSERT_NE(reader, nullptr);
    EXPECT_TRUE(reader->getCanDeserializeIntoExistingObjectProperty());
}

// TQ-004: XNA CurveReader.Read IL_002f-IL_0063 reads position, value, tangentIn, tangentOut, then
// continuity, and builds CurveKey(position, value, tangentIn, tangentOut, continuity). Every field
// differs here, so a swapped pair (notably the two tangents) cannot pass unnoticed.
TEST_F(CurveReaderTest, ReadsEveryKeyFieldInXnaOrderFromLiteralBytes)
{
    std::vector<uint8_t> bytes{0x03, 0, 0, 0,  // PreLoop = Oscillate (3)
                               0x04, 0, 0, 0,  // PostLoop = Linear (4)
                               0x01, 0, 0, 0}; // one key
    PushFloatBits(bytes, 0x3f800000); // position 1
    PushFloatBits(bytes, 0x40000000); // value 2
    PushFloatBits(bytes, 0x40400000); // tangentIn 3
    PushFloatBits(bytes, 0xbf000000); // tangentOut -0.5
    bytes.insert(bytes.end(), {0x01, 0, 0, 0}); // continuity = Step (1)
    int64_t consumed = 0;

    const Curve curve = ReadCurve(bytes, std::nullopt, &consumed);

    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Oscillate);
    EXPECT_EQ(curve.getPostLoopProperty(), CurveLoopType::Linear);
    ASSERT_EQ(curve.getKeysProperty().getCountProperty(), 1);
    const auto& key = curve.getKeysProperty()[0];
    EXPECT_EQ(key.getPositionProperty(), 1.0f);
    EXPECT_EQ(key.getValueProperty(), 2.0f);
    EXPECT_EQ(key.getTangentInProperty(), 3.0f);
    EXPECT_EQ(key.getTangentOutProperty(), -0.5f);
    EXPECT_EQ(key.getContinuityProperty(), CurveContinuity::Step);
    EXPECT_EQ(consumed, static_cast<int64_t>(bytes.size()));
}

// TQ-004: XNA CurveReader.Read reuses a non-null existing Curve (IL_0000-IL_000b), overwrites both
// loop types and Add()s each key (IL_0068) without clearing the keys it already had; Add keeps the
// collection ordered by position.
TEST_F(CurveReaderTest, ReadingIntoAnExistingCurveKeepsItsKeysAndOverwritesLoopTypes)
{
    Curve existing;
    existing.setPreLoopProperty(CurveLoopType::Constant);
    existing.setPostLoopProperty(CurveLoopType::Constant);
    existing.getKeysProperty().Add(Microsoft::Xna::Framework::CurveKey(4.0f, 40.0f));

    std::vector<uint8_t> bytes{0x00, 0, 0, 0,  // PreLoop = Constant (0)
                               0x01, 0, 0, 0,  // PostLoop = Cycle (1)
                               0x01, 0, 0, 0}; // one key
    PushFloatBits(bytes, 0x3f800000); // position 1
    PushFloatBits(bytes, 0x40000000); // value 2
    PushFloatBits(bytes, 0x00000000); // tangentIn 0
    PushFloatBits(bytes, 0x00000000); // tangentOut 0
    bytes.insert(bytes.end(), {0x00, 0, 0, 0}); // continuity = Smooth (0)

    const Curve curve = ReadCurve(bytes, existing);

    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Constant);
    EXPECT_EQ(curve.getPostLoopProperty(), CurveLoopType::Cycle);
    ASSERT_EQ(curve.getKeysProperty().getCountProperty(), 2);
    EXPECT_EQ(curve.getKeysProperty()[0].getPositionProperty(), 1.0f);
    EXPECT_EQ(curve.getKeysProperty()[0].getValueProperty(), 2.0f);
    EXPECT_EQ(curve.getKeysProperty()[1].getPositionProperty(), 4.0f);
    EXPECT_EQ(curve.getKeysProperty()[1].getValueProperty(), 40.0f);
}
