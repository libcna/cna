// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-31: unit tests for SpriteFontReader's own behavior (registration, the
// existing-instance error path) -- see ContentManagerSpriteFontXnbTests.cpp for the full
// end-to-end M3 milestone test through ContentManager.

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class SpriteFontContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
            CNA::Internal::Xnb::RegisterSpriteFontXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };
}

TEST_F(SpriteFontContentTypeReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.SpriteFontReader"));
}

TEST_F(SpriteFontContentTypeReaderTest, CollectionDependenciesAreRegisteredUnderRealFnaCanonicalNames)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.ListReader`1[[System.Char]]"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(
        "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]"));
}

TEST_F(SpriteFontContentTypeReaderTest, CanDeserializeIntoExistingObjectDefaultsFalseMatchingFna)
{
    // FNA's real SpriteFontReader never overrides CanDeserializeIntoExistingObject, so
    // ContentManager itself never supplies an existingInstance -- the reader's own
    // NotImplementedException for that case (see SpriteFontContentTypeReader.cpp) is
    // consequently unreachable via normal dispatch, exactly like this property says it should be.
    auto reader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.SpriteFontReader");
    ASSERT_NE(reader, nullptr);

    EXPECT_FALSE(reader->getCanDeserializeIntoExistingObjectProperty());
}
