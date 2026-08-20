// SPDX-License-Identifier: MS-PL
// plans/plan_wicked.md WICKED-70: device-independent coverage of the WICKED renderer's pipeline cache key.
//
// Everything else in this renderer needs a real Vulkan device, a window and a GPU, so it is covered
// by the smoke test registered in cmake/Tests/WickedTests.cmake instead. The cache key is the one
// piece of genuinely device-independent logic, and it is also the piece where a mistake is silent:
// a key that hashes or compares two different render states as equal makes the cache hand back a
// pipeline built for the wrong state, which renders without any error at all.

#include "CNA/Internal/Renderers/Wicked/WickedRenderer.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

using CNA::Internal::Renderers::Wicked::WickedPipelineKey;
using CNA::Internal::Renderers::Wicked::WickedPipelineKeyHash;
using CNA::Internal::Renderers::Wicked::WickedShaderVariant;

namespace
{
    WickedPipelineKey MakeKey()
    {
        WickedPipelineKey key{};
        key.variant = static_cast<std::uint32_t>(WickedShaderVariant::Basic24);
        key.topology = 1;
        key.blendEnabled = 1;
        key.colorSrcBlend = 4;
        key.alphaSrcBlend = 4;
        key.colorDstBlend = 5;
        key.alphaDstBlend = 5;
        key.depthEnable = 1;
        key.depthWriteEnable = 1;
        key.depthFunc = 3;
        return key;
    }
}

TEST(WickedPipelineKey, DefaultConstructedKeysAreEqual)
{
    const WickedPipelineKey a{};
    const WickedPipelineKey b{};
    EXPECT_TRUE(a == b);
}

TEST(WickedPipelineKey, DefaultsMatchXnaDefaultWriteMasks)
{
    const WickedPipelineKey key{};
    for (int slot = 0; slot < 4; ++slot)
        EXPECT_EQ(15, key.colorWriteChannels[slot]);
    EXPECT_EQ(0xFFFFFFFFu, key.multiSampleMask);
    EXPECT_EQ(0xFF, key.stencilMask);
    EXPECT_EQ(0xFF, key.stencilWriteMask);
}

TEST(WickedPipelineKey, IdenticallyPopulatedKeysAreEqual)
{
    EXPECT_TRUE(MakeKey() == MakeKey());
}

TEST(WickedPipelineKey, ShaderVariantParticipatesInEquality)
{
    WickedPipelineKey other = MakeKey();
    other.variant = static_cast<std::uint32_t>(WickedShaderVariant::Basic32);
    EXPECT_FALSE(MakeKey() == other);
}

TEST(WickedPipelineKey, InstancedFlagParticipatesInEquality)
{
    // The instanced flag selects a different vertex shader AND a different input layout, so a key
    // that ignored it would hand an instanced draw the non-instanced pipeline -- which binds no
    // per-instance stream at all and would render every instance on top of the first.
    WickedPipelineKey other = MakeKey();
    other.instanced = 1;
    EXPECT_FALSE(MakeKey() == other);

    const WickedPipelineKeyHash hash;
    EXPECT_NE(hash(MakeKey()), hash(other));
}

TEST(WickedPipelineKey, EnvMapFlagParticipatesInEquality)
{
    // The env-map flag selects a different vertex AND pixel shader, so confusing it with the
    // ordinary program would silently render EnvironmentMapEffect geometry through BasicPS.
    WickedPipelineKey other = MakeKey();
    other.envMap = 1;
    EXPECT_FALSE(MakeKey() == other);

    const WickedPipelineKeyHash hash;
    EXPECT_NE(hash(MakeKey()), hash(other));
}

TEST(WickedPipelineKey, SkinnedFlagParticipatesInEquality)
{
    // The skinned flag selects a different vertex program that consumes the bone palette. A key
    // that ignored it would draw a skinned mesh through the bind-pose program -- a plausible
    // looking render of the wrong thing.
    WickedPipelineKey other = MakeKey();
    other.skinned = 1;
    EXPECT_FALSE(MakeKey() == other);

    const WickedPipelineKeyHash hash;
    EXPECT_NE(hash(MakeKey()), hash(other));
}

TEST(WickedPipelineKey, PbrFlagParticipatesInEquality)
{
    // PbrEffect has its own vertex and pixel programs and its own texture slots, so confusing it
    // with the ordinary program would render a PBR material through the Lambert path.
    WickedPipelineKey other = MakeKey();
    other.pbr = 1;
    EXPECT_FALSE(MakeKey() == other);

    const WickedPipelineKeyHash hash;
    EXPECT_NE(hash(MakeKey()), hash(other));
}

TEST(WickedPipelineKey, SkinnedPbrIsDistinctFromUnskinnedPbr)
{
    // Both flags together select PbrSkinned68VS; either alone selects a different program.
    WickedPipelineKey pbrOnly = MakeKey();
    pbrOnly.pbr = 1;
    WickedPipelineKey skinnedPbr = pbrOnly;
    skinnedPbr.skinned = 1;
    EXPECT_FALSE(pbrOnly == skinnedPbr);
}

TEST(WickedPipelineKey, VertexStreamSplitParticipatesInEquality)
{
    // A split declaration gets its own re-slotted input layout, so two draws that differ only in
    // how their elements are spread across buffers must not share a pipeline.
    WickedPipelineKey single = MakeKey();
    EXPECT_EQ(0u, single.streamCount);

    WickedPipelineKey split = MakeKey();
    split.streamCount = 2;
    split.streamBase[0] = 0;
    split.streamStride[0] = 12;
    split.streamBase[1] = 12;
    split.streamStride[1] = 12;
    EXPECT_FALSE(single == split);

    WickedPipelineKey differentSplit = split;
    differentSplit.streamStride[0] = 16;
    differentSplit.streamBase[1] = 16;
    EXPECT_FALSE(split == differentSplit);

    const WickedPipelineKeyHash hash;
    EXPECT_NE(hash(single), hash(split));
    EXPECT_NE(hash(split), hash(differentSplit));
}

TEST(WickedPipelineKey, EveryStateFieldParticipatesInEquality)
{
    // Byte-wise comparison is what makes this hold for a field added later without touching
    // operator==; the point of the test is that the property is checked, not assumed.
    const WickedPipelineKey base = MakeKey();

    WickedPipelineKey blend = base;
    blend.colorDstBlend = 1;
    EXPECT_FALSE(base == blend);

    WickedPipelineKey depth = base;
    depth.depthFunc = 2;
    EXPECT_FALSE(base == depth);

    WickedPipelineKey stencil = base;
    stencil.stencilEnable = 1;
    EXPECT_FALSE(base == stencil);

    WickedPipelineKey raster = base;
    raster.cullMode = 2;
    EXPECT_FALSE(base == raster);

    WickedPipelineKey writeMask = base;
    writeMask.colorWriteChannels[2] = 0;
    EXPECT_FALSE(base == writeMask);

    WickedPipelineKey bias = base;
    bias.slopeScaleDepthBias = 0.5f;
    EXPECT_FALSE(base == bias);
}

TEST(WickedPipelineKeyHash, EqualKeysHashEqually)
{
    const WickedPipelineKeyHash hash;
    EXPECT_EQ(hash(MakeKey()), hash(MakeKey()));
}

TEST(WickedPipelineKeyHash, DistinctStatesGetDistinctHashesInPractice)
{
    const WickedPipelineKeyHash hash;
    std::unordered_set<std::size_t> seen;

    WickedPipelineKey key = MakeKey();
    for (std::uint32_t variant = 0;
         variant < static_cast<std::uint32_t>(WickedShaderVariant::Count); ++variant)
    {
        key.variant = variant;
        for (std::int32_t cull = 0; cull < 3; ++cull)
        {
            key.cullMode = cull;
            seen.insert(hash(key));
        }
    }

    EXPECT_EQ(static_cast<std::size_t>(WickedShaderVariant::Count) * 3u, seen.size());
}

TEST(WickedPipelineKeyHash, WorksAsAnUnorderedMapKey)
{
    std::unordered_set<WickedPipelineKey, WickedPipelineKeyHash> keys;
    keys.insert(MakeKey());
    keys.insert(MakeKey());
    EXPECT_EQ(1u, keys.size());

    WickedPipelineKey other = MakeKey();
    other.topology = 4;
    keys.insert(other);
    EXPECT_EQ(2u, keys.size());
}
