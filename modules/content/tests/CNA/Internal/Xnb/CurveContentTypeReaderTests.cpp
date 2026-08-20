// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-20: unit tests for CurveReader.

#include <any>
#include <gtest/gtest.h>

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
