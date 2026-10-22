// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SDL_GPU) || defined(CNA_RENDERER_PRESENT_SDL_GPU)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuSpirvBindings.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
// Instruction fixtures isolate descriptor reflection; they are not submitted to a driver.
std::vector<std::uint32_t> SamplerModule(std::uint32_t dimension, std::uint32_t set, std::uint32_t binding,
                                         std::uint32_t stage = 4, bool arrayed = false,
                                         bool multisampled = false, bool descriptors = false)
{
    std::vector<std::uint32_t> words{0x07230203,
                                     0x00010000,
                                     0,
                                     13,
                                     0,
                                     (2u << 16) | 17,
                                     1,
                                     (3u << 16) | 14,
                                     0,
                                     1,
                                     (5u << 16) | 15,
                                     stage,
                                     1,
                                     0x6e69616d,
                                     0,
                                     (4u << 16) | 71,
                                     9,
                                     34,
                                     set,
                                     (4u << 16) | 71,
                                     9,
                                     33,
                                     binding,
                                     (3u << 16) | 22,
                                     2,
                                     32,
                                     (9u << 16) | 25,
                                     3,
                                     2,
                                     dimension,
                                     0,
                                     arrayed ? 1u : 0u,
                                     multisampled ? 1u : 0u,
                                     1,
                                     0,
                                     (3u << 16) | 27,
                                     4,
                                     3};
    if (descriptors)
        words.insert(words.end(),
                     {(4u << 16) | 21, 11, 32, 0, (4u << 16) | 43, 11, 12, 2, (4u << 16) | 28, 10, 4, 12});
    words.insert(words.end(), {(4u << 16) | 32,
                               5,
                               0,
                               descriptors ? 10u : 4u,
                               (4u << 16) | 59,
                               5,
                               9,
                               0,
                               (2u << 16) | 19,
                               6,
                               (3u << 16) | 33,
                               7,
                               6,
                               (5u << 16) | 54,
                               6,
                               1,
                               0,
                               7,
                               (2u << 16) | 248,
                               8,
                               (1u << 16) | 253,
                               (1u << 16) | 56});
    return words;
}

SpirvRemapResultEXT Reflect(const std::vector<std::uint32_t> &words)
{
    return RemapSpirvForSdlGpuEXT(words.data(), words.size(), false);
}
} // namespace

TEST(SdlGpuSpirvBindingsTest, NativeVolumeRetainsDimensionAndUnitWhenCompacted)
{
    const auto result = Reflect(SamplerModule(2, 2, 3));
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.resources.size(), 1u);
    const auto &resource = result.resources[0];
    EXPECT_EQ(resource.textureDimension, SpirvTextureDimensionEXT::Texture3D);
    EXPECT_EQ(resource.textureUnit, 3u);
    EXPECT_EQ(resource.assignedSet, 2u);
    EXPECT_EQ(resource.assignedBinding, 0u);
    EXPECT_EQ(resource.slot, 0u);
    EXPECT_FALSE(resource.usesSpriteTexture);
    EXPECT_EQ(result.samplerCount, 1u);
    EXPECT_EQ(result.uniformBufferCount, 0u);
}

TEST(SdlGpuSpirvBindingsTest, PortableTypedUnitsRetainTheirOriginalMeaning)
{
    for (const auto dimension : {1u, 2u, 3u})
    {
        const auto base = dimension == 2 ? 8u : dimension == 3 ? 4u : 0u;
        for (std::uint32_t unit = 0; unit != 4; ++unit)
        {
            const auto result = Reflect(SamplerModule(dimension, 1, base + unit));
            ASSERT_TRUE(result.error.empty()) << result.error;
            ASSERT_EQ(result.resources.size(), 1u);
            EXPECT_EQ(result.resources[0].textureUnit, unit);
            EXPECT_EQ(result.resources[0].textureDimension, static_cast<SpirvTextureDimensionEXT>(dimension));
            EXPECT_FALSE(result.resources[0].usesSpriteTexture);
        }
    }
}

TEST(SdlGpuSpirvBindingsTest, NativeCubeAndTwoDimensionalUnitsAreDimensionAware)
{
    for (const auto dimension : {1u, 3u})
    {
        const auto result = Reflect(SamplerModule(dimension, 2, 2));
        ASSERT_TRUE(result.error.empty()) << result.error;
        EXPECT_EQ(result.resources[0].textureUnit, 2u);
        EXPECT_EQ(result.resources[0].textureDimension, static_cast<SpirvTextureDimensionEXT>(dimension));
        EXPECT_FALSE(result.resources[0].usesSpriteTexture);
    }
}

TEST(SdlGpuSpirvBindingsTest, PortableAndNativeSpriteInputsKeepTheirOwnTexture)
{
    for (const auto set : {0u, 2u})
    {
        const auto result = Reflect(SamplerModule(1, set, 0));
        ASSERT_TRUE(result.error.empty()) << result.error;
        EXPECT_TRUE(result.resources[0].usesSpriteTexture);
        EXPECT_EQ(result.resources[0].textureUnit, 0u);
    }
}

TEST(SdlGpuSpirvBindingsTest, RefusesMismatchedPortableDimensions)
{
    for (const auto binding : {0u, 4u, 12u})
        EXPECT_FALSE(Reflect(SamplerModule(2, 1, binding)).error.empty());
    EXPECT_FALSE(Reflect(SamplerModule(1, 1, 8)).error.empty());
}

TEST(SdlGpuSpirvBindingsTest, RefusesUnmappableSetsAndUnits)
{
    for (const auto set : {3u, 6u})
        EXPECT_FALSE(Reflect(SamplerModule(2, set, 0)).error.empty());
    EXPECT_FALSE(Reflect(SamplerModule(2, 2, 4)).error.empty());
    EXPECT_FALSE(Reflect(SamplerModule(1, 0, 1)).error.empty());
    EXPECT_FALSE(Reflect(SamplerModule(2, 0, 0)).error.empty());
}

TEST(SdlGpuSpirvBindingsTest, RefusesUnboundVertexStageSampling)
{
    EXPECT_FALSE(Reflect(SamplerModule(1, 0, 0, 0)).error.empty());
}

TEST(SdlGpuSpirvBindingsTest, RefusesUnsupportedImageShapes)
{
    EXPECT_FALSE(Reflect(SamplerModule(1, 2, 0, 4, true)).error.empty());
    EXPECT_FALSE(Reflect(SamplerModule(1, 2, 0, 4, false, true)).error.empty());
    for (const auto dimension : {0u, 4u, 5u})
        EXPECT_FALSE(Reflect(SamplerModule(dimension, 2, 0)).error.empty());
}

TEST(SdlGpuSpirvBindingsTest, RefusesSamplerDescriptorArrays)
{
    const auto result = Reflect(SamplerModule(2, 2, 0, 4, false, false, true));
    EXPECT_NE(result.error.find("descriptor arrays"), std::string::npos);
}

TEST(SdlGpuSpirvBindingsTest, RefusesMoreThanSixteenSamplersBeforeNativeShaderCreation)
{
    auto words = SamplerModule(1, 0, 0);
    // Add the twelve typed portable units and four native units, all with distinct
    // source bindings. SDL has sixteen slots even when the source has multiple sets.
    for (std::uint32_t index = 0; index != 16; ++index)
    {
        const auto id = 20u + index * 4u;
        const auto dimension = index < 4 || index >= 12 ? 1u : index < 8 ? 3u : 2u;
        const std::vector<std::uint32_t> decorations{
            (4u << 16) | 71, id + 3, 34, index < 12 ? 1u : 2u,
            (4u << 16) | 71, id + 3, 33, index < 12 ? index : index - 12};
        words.insert(words.begin() + 23, decorations.begin(), decorations.end());
        const std::vector<std::uint32_t> types{
            (9u << 16) | 25, id, 2, dimension, 0, 0, 0, 1, 0,
            (3u << 16) | 27, id + 1, id,
            (4u << 16) | 32, id + 2, 0, id + 1,
            (4u << 16) | 59, id + 2, id + 3, 0};
        auto function = words.begin() + 5;
        while ((*function & 0xffffu) != 54) function += *function >> 16;
        words.insert(function, types.begin(), types.end());
        words[3] = id + 4;
        const auto result = Reflect(words);
        if (index < 15)
        {
            ASSERT_TRUE(result.error.empty()) << result.error;
            EXPECT_EQ(result.samplerCount, index + 2);
        }
        else EXPECT_NE(result.error.find("sixteen"), std::string::npos);
    }
}

TEST(SdlGpuSpirvBindingsTest, NativeVertexCoordinatesStayUnchangedAndPortableOnesAreCorrected)
{
    const std::vector<std::uint32_t> words{0x07230203,
                                           0x00010000,
                                           0,
                                           15,
                                           0,
                                           (2u << 16) | 17,
                                           1,
                                           (3u << 16) | 14,
                                           0,
                                           1,
                                           (6u << 16) | 15,
                                           0,
                                           1,
                                           0x6e69616d,
                                           0,
                                           9,
                                           (4u << 16) | 71,
                                           9,
                                           11,
                                           0,
                                           (2u << 16) | 19,
                                           2,
                                           (3u << 16) | 33,
                                           3,
                                           2,
                                           (3u << 16) | 22,
                                           4,
                                           32,
                                           (4u << 16) | 23,
                                           5,
                                           4,
                                           4,
                                           (4u << 16) | 32,
                                           6,
                                           3,
                                           5,
                                           (4u << 16) | 59,
                                           6,
                                           9,
                                           3,
                                           (4u << 16) | 43,
                                           4,
                                           7,
                                           0x3f800000,
                                           (7u << 16) | 44,
                                           5,
                                           8,
                                           7,
                                           7,
                                           7,
                                           7,
                                           (5u << 16) | 54,
                                           2,
                                           1,
                                           0,
                                           3,
                                           (2u << 16) | 248,
                                           10,
                                           (3u << 16) | 62,
                                           9,
                                           8,
                                           (1u << 16) | 253,
                                           (1u << 16) | 56};
    const auto native = Reflect(words);
    ASSERT_TRUE(native.error.empty()) << native.error;
    EXPECT_EQ(native.words, words);
    EXPECT_FALSE(native.changed);
    EXPECT_TRUE(native.resources.empty());
    const auto portable = RemapSpirvForSdlGpuEXT(words.data(), words.size());
    ASSERT_TRUE(portable.error.empty()) << portable.error;
    EXPECT_NE(portable.words, words);
    EXPECT_TRUE(portable.changed);
    const auto bytes =
        RemapSpirvForSdlGpuEXT(static_cast<const void *>(words.data()), words.size() * 4, false);
    EXPECT_EQ(bytes.words, words);
    EXPECT_TRUE(bytes.error.empty());
}
#endif
