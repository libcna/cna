// SPDX-License-Identifier: MS-PL

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

namespace
{
    [[nodiscard]] std::uint32_t FloatWord(float value)
    {
        return std::bit_cast<std::uint32_t>(value);
    }

    [[nodiscard]] std::size_t MicrosoftSmartHash(std::initializer_list<std::uint32_t> words)
    {
        std::uint32_t hash = 0;
        for (const auto word : words)
        {
            hash ^= word;
        }
        return hash == 0
            ? static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
            : static_cast<std::size_t>(hash);
    }
}

TEST(VertexValueHashTest, ZeroFieldsUseMicrosoftNonzeroSentinel)
{
    EXPECT_EQ(VertexElement().GetHashCode(), MicrosoftSmartHash({0, 0, 0, 0}));
    EXPECT_EQ(VertexPositionColor().GetHashCode(), MicrosoftSmartHash({0, 0, 0, 0}));
    EXPECT_EQ(VertexPositionTexture().GetHashCode(), MicrosoftSmartHash({0, 0, 0, 0, 0}));
    EXPECT_EQ(VertexPositionNormalTexture().GetHashCode(),
              MicrosoftSmartHash({0, 0, 0, 0, 0, 0, 0, 0}));
    EXPECT_EQ(VertexPositionColorTexture().GetHashCode(),
              MicrosoftSmartHash({0, 0, 0, 0, 0, 0}));
}

TEST(VertexValueHashTest, VertexElementMatchesMicrosoftFieldXor)
{
    const VertexElement value(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 1);
    EXPECT_EQ(value.GetHashCode(),
              MicrosoftSmartHash({12,
                                  static_cast<std::uint32_t>(VertexElementFormat::Vector3),
                                  static_cast<std::uint32_t>(VertexElementUsage::Normal),
                                  1}));
}

TEST(VertexValueHashTest, VertexPositionColorMatchesMicrosoftFieldXor)
{
    const VertexPositionColor value(Vector3(1.0f, -2.0f, 3.5f), Color(10, 20, 30, 40));
    EXPECT_EQ(value.GetHashCode(),
              MicrosoftSmartHash({FloatWord(1.0f), FloatWord(-2.0f), FloatWord(3.5f),
                                  value.Color.getPackedValueProperty()}));
}

TEST(VertexValueHashTest, VertexPositionTextureMatchesMicrosoftFieldXor)
{
    const VertexPositionTexture value(Vector3(1.0f, -2.0f, 3.5f), Vector2(0.25f, -0.75f));
    EXPECT_EQ(value.GetHashCode(),
              MicrosoftSmartHash({FloatWord(1.0f), FloatWord(-2.0f), FloatWord(3.5f),
                                  FloatWord(0.25f), FloatWord(-0.75f)}));
}

TEST(VertexValueHashTest, VertexPositionNormalTextureMatchesMicrosoftFieldXor)
{
    const VertexPositionNormalTexture value(Vector3(1.0f, -2.0f, 3.5f),
                                            Vector3(-0.25f, 0.5f, 0.75f),
                                            Vector2(0.125f, -0.875f));
    EXPECT_EQ(value.GetHashCode(),
              MicrosoftSmartHash({FloatWord(1.0f), FloatWord(-2.0f), FloatWord(3.5f),
                                  FloatWord(-0.25f), FloatWord(0.5f), FloatWord(0.75f),
                                  FloatWord(0.125f), FloatWord(-0.875f)}));
}

TEST(VertexValueHashTest, VertexPositionColorTextureMatchesMicrosoftFieldXor)
{
    const VertexPositionColorTexture value(Vector3(1.0f, -2.0f, 3.5f),
                                           Color(10, 20, 30, 40),
                                           Vector2(0.25f, -0.75f));
    EXPECT_EQ(value.GetHashCode(),
              MicrosoftSmartHash({FloatWord(1.0f), FloatWord(-2.0f), FloatWord(3.5f),
                                  value.Color.getPackedValueProperty(),
                                  FloatWord(0.25f), FloatWord(-0.75f)}));
}
