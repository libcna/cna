// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Graphics::TextureCollection;
using Microsoft::Xna::Framework::Graphics::Texture2D;

// =============================================================================
// TextureCollection — index bounds
// =============================================================================

TEST(TextureCollectionValidationTest, SetNullTexture_DoesNotThrow)
{
    TextureCollection col;
    EXPECT_NO_THROW(col(0, nullptr));
}

TEST(TextureCollectionValidationTest, GetNullSlot_ReturnsNull)
{
    TextureCollection col;
    EXPECT_EQ(col[0], nullptr);
}

TEST(TextureCollectionValidationTest, NegativeIndex_ThrowsOutOfRange)
{
    TextureCollection col;
    EXPECT_THROW(col(-1, nullptr), std::out_of_range);
}

TEST(TextureCollectionValidationTest, IndexAtMax_ThrowsOutOfRange)
{
    TextureCollection col;
    EXPECT_THROW(col(TextureCollection::MaxTextures, nullptr), std::out_of_range);
}

TEST(TextureCollectionValidationTest, IndexAtLastSlot_DoesNotThrow)
{
    TextureCollection col;
    EXPECT_NO_THROW(col(TextureCollection::MaxTextures - 1, nullptr));
}

// =============================================================================
// TextureCollection — disposed texture rejected
// =============================================================================

TEST(TextureCollectionValidationTest, DisposedTexture_ThrowsObjectDisposedException)
{
    TextureCollection col;
    Texture2D tex; // default constructor — no device, no GPU resource
    tex.Dispose();
    EXPECT_THROW(col(0, &tex), System::ObjectDisposedException);
}

TEST(TextureCollectionValidationTest, DisposedTexture_CatchableAsInvalidOperationException)
{
    TextureCollection col;
    Texture2D tex;
    tex.Dispose();
    EXPECT_THROW(col(0, &tex), System::InvalidOperationException);
}

TEST(TextureCollectionValidationTest, LiveTexture_DoesNotThrowForDisposedCheck)
{
    TextureCollection col;
    Texture2D tex; // not disposed
    EXPECT_NO_THROW(col(0, &tex));
}
