// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-34: "the one piece of Phase 2 genuinely build-verifiable on this Linux
// machine today". MetalPipelineKey.hpp has zero Objective-C/Apple-framework dependency, so unlike
// the rest of the Metal renderer (Objective-C++, only ever compiled on a real Mac), this test
// compiles and runs on every platform CnaTests already builds for -- no #if defined(CNA_RENDERER_*)
// gate, deliberately, since gating it would defeat the point.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalPipelineKey.hpp"
#include <unordered_map>

using namespace CNA::Internal::Renderers::Metal;

TEST(MetalBlendKey, DefaultMatchesXnaOpaquePreset)
{
    // plans/plan_metal.md METAL-24's own comment claims the default MetalBlendKey matches
    // BlendState.Opaque's real values (Blend::One=0/Blend::Zero=1, BlendFunction::Add=0 for both
    // channels) -- verified against the real enum ordinals here rather than re-trusted.
    MetalBlendKey k;
    EXPECT_EQ(k.colorSrc, 0);
    EXPECT_EQ(k.colorDst, 1);
    EXPECT_EQ(k.alphaSrc, 0);
    EXPECT_EQ(k.alphaDst, 1);
    EXPECT_EQ(k.colorFunc, 0);
    EXPECT_EQ(k.alphaFunc, 0);
    EXPECT_FALSE(k.enabled);
}

TEST(MetalBlendKey, EqualWhenEveryFieldMatches)
{
    MetalBlendKey a{5, 6, 7, 8, 2, 3, true};
    MetalBlendKey b{5, 6, 7, 8, 2, 3, true};
    EXPECT_TRUE(a == b);
}

TEST(MetalBlendKey, UnequalWhenAnySingleFieldDiffers)
{
    MetalBlendKey base{5, 6, 7, 8, 2, 3, true};
    EXPECT_FALSE(base == (MetalBlendKey{9, 6, 7, 8, 2, 3, true}));   // colorSrc
    EXPECT_FALSE(base == (MetalBlendKey{5, 9, 7, 8, 2, 3, true}));   // colorDst
    EXPECT_FALSE(base == (MetalBlendKey{5, 6, 9, 8, 2, 3, true}));   // alphaSrc
    EXPECT_FALSE(base == (MetalBlendKey{5, 6, 7, 9, 2, 3, true}));   // alphaDst
    EXPECT_FALSE(base == (MetalBlendKey{5, 6, 7, 8, 9, 3, true}));   // colorFunc
    EXPECT_FALSE(base == (MetalBlendKey{5, 6, 7, 8, 2, 9, true}));   // alphaFunc
    EXPECT_FALSE(base == (MetalBlendKey{5, 6, 7, 8, 2, 3, false}));  // enabled
}

TEST(MetalBlendKey, SetBlendEnabledUsesLastWriterStraightAlphaOrOpaqueState)
{
    MetalBlendKey current{};
    current = MetalBlendKeyForSetBlendEnabled(true);
    EXPECT_TRUE(current == (MetalBlendKey{4, 5, 4, 5, 0, 0, true}));

    current = MetalBlendKey{4, 5, 4, 5, 0, 0, true};
    current = MetalBlendKeyForSetBlendEnabled(false);
    EXPECT_TRUE(current == MetalBlendKey{});

    current = MetalBlendKeyForSetBlendEnabled(false);
    current = MetalBlendKey{4, 5, 4, 5, 0, 0, true};
    EXPECT_TRUE(current.enabled);
}

TEST(MetalPipelineCacheKey, EqualOnlyWhenKindAndBlendBothMatch)
{
    MetalPipelineCacheKey a{MetalPipelineKind::LitTex32, MetalBlendKey{}};
    MetalPipelineCacheKey b{MetalPipelineKind::LitTex32, MetalBlendKey{}};
    MetalPipelineCacheKey differentKind{MetalPipelineKind::Pbr48, MetalBlendKey{}};
    MetalPipelineCacheKey differentBlend{MetalPipelineKind::LitTex32, MetalBlendKey{1, 2, 3, 4, 5, 6, true}};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == differentKind);
    EXPECT_FALSE(a == differentBlend);
}

TEST(MetalPipelineCacheKey, ColorAttachmentCountDefaultsToOne)
{
    // plans/plan_metal.md METAL-113: every pre-MRT call site aggregate-initializes only {kind, blend},
    // relying on this default so it's unaffected by the new field.
    MetalPipelineCacheKey k{MetalPipelineKind::Colored16, MetalBlendKey{}};
    EXPECT_EQ(k.colorAttachmentCount, 1);
}

TEST(MetalPipelineCacheKey, UnequalWhenOnlyColorAttachmentCountDiffers)
{
    MetalPipelineCacheKey single{MetalPipelineKind::Colored16, MetalBlendKey{}, 1};
    MetalPipelineCacheKey mrt{MetalPipelineKind::Colored16, MetalBlendKey{}, 3};
    EXPECT_FALSE(single == mrt);
}

TEST(MetalPipelineCacheKey, SampleCountDefaultsToOne)
{
    MetalPipelineCacheKey k{MetalPipelineKind::Colored16, MetalBlendKey{}};
    EXPECT_EQ(k.sampleCount, 1);
}

TEST(MetalPipelineCacheKey, UnequalWhenOnlySampleCountDiffers)
{
    MetalPipelineCacheKey noMsaa{MetalPipelineKind::Colored16, MetalBlendKey{}, 1, 1};
    MetalPipelineCacheKey msaa4x{MetalPipelineKind::Colored16, MetalBlendKey{}, 1, 4};
    EXPECT_FALSE(noMsaa == msaa4x);
}

TEST(MetalPipelineCacheKeyHash, EqualKeysProduceEqualHashes)
{
    // A hard requirement, not a nicety: std::unordered_map's own contract is undefined behavior if
    // this doesn't hold.
    MetalPipelineCacheKeyHash hash;
    MetalPipelineCacheKey a{MetalPipelineKind::Skinned56VertexLit, MetalBlendKey{4, 5, 6, 7, 1, 1, true}};
    MetalPipelineCacheKey b{MetalPipelineKind::Skinned56VertexLit, MetalBlendKey{4, 5, 6, 7, 1, 1, true}};
    EXPECT_EQ(hash(a), hash(b));
}

TEST(MetalPipelineCacheKeyHash, DifferentColorAttachmentCountUsuallyProducesDifferentHash)
{
    MetalPipelineCacheKeyHash hash;
    MetalPipelineCacheKey single{MetalPipelineKind::Colored16, MetalBlendKey{}, 1};
    MetalPipelineCacheKey mrt{MetalPipelineKind::Colored16, MetalBlendKey{}, 8};
    EXPECT_NE(hash(single), hash(mrt));
}

TEST(MetalPipelineCacheKeyHash, DifferentSampleCountUsuallyProducesDifferentHash)
{
    MetalPipelineCacheKeyHash hash;
    MetalPipelineCacheKey noMsaa{MetalPipelineKind::Colored16, MetalBlendKey{}, 1, 1};
    MetalPipelineCacheKey msaa4x{MetalPipelineKind::Colored16, MetalBlendKey{}, 1, 4};
    EXPECT_NE(hash(noMsaa), hash(msaa4x));
}

TEST(MetalPipelineCacheKeyHash, ColorAttachmentCountAndSampleCountBothVaryingProducesDifferentHash)
{
    // plans/plan_metal.md METAL-104: a real regression guard for the hash_combine fix -- if sampleCount
    // were still packed into the same 64-bit word as colorAttachmentCount, a (count=1,samples=8)
    // key could collide with a (count=?,samples=?) key via bit overflow into colorAttachmentCount's
    // own range. XOR-combining avoids this; this test locks in that distinctness for a
    // representative pair that would be most at risk of exactly that kind of overflow collision.
    MetalPipelineCacheKeyHash hash;
    MetalPipelineCacheKey a{MetalPipelineKind::Colored16, MetalBlendKey{}, 1, 8};
    MetalPipelineCacheKey b{MetalPipelineKind::Colored16, MetalBlendKey{}, 2, 4};
    EXPECT_NE(hash(a), hash(b));
}

TEST(MetalPipelineCacheKeyHash, DifferentKeysUsuallyProduceDifferentHashes)
{
    // Not a correctness requirement (hash collisions are always legal), but a real, practical
    // sanity check on the bit-packing scheme -- every field lives in its own non-overlapping byte
    // range (see MetalPipelineKey.hpp's own comment), so distinct field combinations across the 15
    // real PipelineKind values should not collide in practice.
    MetalPipelineCacheKeyHash hash;
    std::unordered_map<uint64_t, MetalPipelineKind> seen;
    for (uint8_t kindValue = 0; kindValue <= static_cast<uint8_t>(MetalPipelineKind::Sprite2D); ++kindValue)
    {
        MetalPipelineCacheKey k{static_cast<MetalPipelineKind>(kindValue), MetalBlendKey{}};
        const uint64_t h = static_cast<uint64_t>(hash(k));
        auto it = seen.find(h);
        EXPECT_TRUE(it == seen.end())
            << "Hash collision between PipelineKind " << static_cast<int>(kindValue)
            << " and " << static_cast<int>(it != seen.end() ? it->second : MetalPipelineKind::Colored16);
        seen[h] = static_cast<MetalPipelineKind>(kindValue);
    }
}

// plans/plan_metal.md METAL-22/23: the actual real-world usage pattern -- getOrCreatePipeline()'s own
// std::unordered_map<PipelineCacheKey, id<MTLRenderPipelineState>, PipelineCacheKeyHash>. This is
// the property that matters in practice: does the cache correctly distinguish every PipelineKind,
// and does it correctly treat two draws with the SAME shader but a DIFFERENT BlendState as two
// separate cache entries (rather than silently reusing the wrong pipeline's baked-in blend state)?
TEST(MetalPipelineCacheKey, UnorderedMapRoundTripDistinguishesEveryPipelineKind)
{
    std::unordered_map<MetalPipelineCacheKey, int, MetalPipelineCacheKeyHash> cache;
    const MetalPipelineKind kinds[] = {
        MetalPipelineKind::Colored16, MetalPipelineKind::Textured20, MetalPipelineKind::ColorTex24,
        MetalPipelineKind::LitTex32, MetalPipelineKind::LitTex32VertexLit, MetalPipelineKind::DualTex20,
        MetalPipelineKind::DualTex24Colored, MetalPipelineKind::EnvMap32, MetalPipelineKind::Skinned52,
        MetalPipelineKind::Skinned56, MetalPipelineKind::Skinned52VertexLit, MetalPipelineKind::Skinned56VertexLit,
        MetalPipelineKind::Pbr48, MetalPipelineKind::SkinnedPbr68, MetalPipelineKind::Sprite2D,
    };
    int nextId = 0;
    for (auto kind : kinds)
        cache[MetalPipelineCacheKey{kind, MetalBlendKey{}}] = nextId++;

    ASSERT_EQ(cache.size(), std::size(kinds));
    nextId = 0;
    for (auto kind : kinds)
    {
        auto it = cache.find(MetalPipelineCacheKey{kind, MetalBlendKey{}});
        ASSERT_NE(it, cache.end());
        EXPECT_EQ(it->second, nextId++);
    }
}

TEST(MetalPipelineCacheKey, UnorderedMapTreatsSameKindDifferentBlendAsDistinctEntries)
{
    std::unordered_map<MetalPipelineCacheKey, int, MetalPipelineCacheKeyHash> cache;
    const MetalPipelineCacheKey opaque{MetalPipelineKind::Textured20, MetalBlendKey{}};
    // Blend::SourceAlpha=4 / InverseSourceAlpha=5, BlendFunction::Add=0, real AlphaBlend preset shape.
    const MetalPipelineCacheKey alphaBlend{MetalPipelineKind::Textured20, MetalBlendKey{4, 5, 4, 5, 0, 0, true}};

    cache[opaque] = 1;
    cache[alphaBlend] = 2;

    ASSERT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.at(opaque), 1);
    EXPECT_EQ(cache.at(alphaBlend), 2);

    // Re-binding Opaque again (a fresh key with identical field values, not the same key instance)
    // must still find the original cached entry -- this is exactly what getOrCreatePipeline() relies
    // on to avoid rebuilding an MTLRenderPipelineState it already has.
    const MetalPipelineCacheKey opaqueAgain{MetalPipelineKind::Textured20, MetalBlendKey{}};
    EXPECT_EQ(cache.at(opaqueAgain), 1);
}
