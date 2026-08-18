// SPDX-License-Identifier: MS-PL
//
// plan_fx.md FX-091: the SDL_GPU sampler cache's key identity.
//
// The renderer keeps ONE native sampler object per distinct sampler description and looks it up by
// key on every draw. A key that cannot tell two descriptions apart therefore does not merely lose a
// cache entry -- it hands the second description the first one's sampler, and the draw samples with
// the wrong filter, wrap or LOD without any diagnostic at all. This file pins the property that
// makes that impossible: distinct descriptions have distinct keys, one field at a time.
//
// The regression that motivated it: the key used to be a hand-packed uint64 with the 32-bit LOD
// bias shifted to bit 40, so its top eight bits -- the float's sign and seven of its eight exponent
// bits -- fell off the end. 0.0, +/-0.5, +/-2.0 and +/-8.0 all collapsed onto one key.

#if defined(CNA_RENDERER_SDL_GPU)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace
{
    using CNA::Internal::Renderers::SdlGpu::SamplerCacheKeyEXT;
    using CNA::Internal::Renderers::SdlGpu::SamplerCacheKeyHashEXT;

    /// The renderer's own defaults, so each test varies exactly one field away from them.
    SamplerCacheKeyEXT BaseKey(float lodBias = 0.0f)
    {
        return SamplerCacheKeyEXT::Make(/*filter=*/0, /*u=*/1, /*v=*/1, /*w=*/1,
                                        /*anisotropy=*/4, /*mipLevel=*/0, lodBias);
    }
}

// The exact list the follow-up audit named, plus the neighbours that share an exponent with them.
// Under the old packing these produced two keys in total; each must now be its own.
TEST(SdlGpuSamplerCacheKeyTest, DistinctLodBiasesNeverAlias)
{
    const float biases[] = {
        0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f,
        4.0f, -4.0f, 8.0f, -8.0f, 0.25f, -0.25f, 15.0f, -15.0f, 0.125f,
    };

    std::vector<SamplerCacheKeyEXT> keys;
    for (const float bias : biases) keys.push_back(BaseKey(bias));

    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        for (std::size_t j = i + 1; j < keys.size(); ++j)
        {
            EXPECT_FALSE(keys[i] == keys[j])
                << "LOD biases " << biases[i] << " and " << biases[j]
                << " must not share a native sampler";
        }
    }

    // Equal keys must also hash equally, or the unordered_map would miss its own entry.
    for (const float bias : biases)
    {
        const SamplerCacheKeyEXT a = BaseKey(bias);
        const SamplerCacheKeyEXT b = BaseKey(bias);
        EXPECT_TRUE(a == b);
        EXPECT_EQ(SamplerCacheKeyHashEXT{}(a), SamplerCacheKeyHashEXT{}(b))
            << "bias " << bias;
    }

    // And the hash must actually separate them, or every lookup degrades to a bucket walk that
    // only equality saves. Not a correctness requirement, but a cheap check that the mix reaches
    // the bias bits at all -- the exact defect class the packed key had.
    std::set<std::size_t> hashes;
    for (const SamplerCacheKeyEXT& key : keys) hashes.insert(SamplerCacheKeyHashEXT{}(key));
    EXPECT_EQ(hashes.size(), keys.size())
        << "the hash must mix the LOD bias, not just the integer fields";
}

// -0.0 and +0.0 compare equal as floats but are different sampler requests to no hardware alive;
// what matters is that the key is deterministic either way and that NaN finds itself, since a key
// that never equals itself would leak a new native sampler on every single draw.
TEST(SdlGpuSamplerCacheKeyTest, SignedZeroAndNaNBiasesAreDeterministic)
{
    EXPECT_TRUE(BaseKey(0.0f) == BaseKey(0.0f));
    EXPECT_TRUE(BaseKey(-0.0f) == BaseKey(-0.0f));
    EXPECT_FALSE(BaseKey(0.0f) == BaseKey(-0.0f))
        << "the key compares bit patterns, so signed zeroes stay distinguishable";

    const float nan = std::nanf("");
    EXPECT_TRUE(BaseKey(nan) == BaseKey(nan))
        << "a NaN bias must still match itself, or the cache would never hit and would leak";
}

// Every other field of the description participates too. A field left out of the key is the same
// defect as a truncated one: the second request silently gets the first request's sampler.
TEST(SdlGpuSamplerCacheKeyTest, EveryFieldParticipatesInIdentity)
{
    const SamplerCacheKeyEXT base = BaseKey();

    struct Variant { const char* name; SamplerCacheKeyEXT key; };
    const Variant variants[] = {
        {"filter", SamplerCacheKeyEXT::Make(1, 1, 1, 1, 4, 0, 0.0f)},
        {"addressU", SamplerCacheKeyEXT::Make(0, 0, 1, 1, 4, 0, 0.0f)},
        {"addressV", SamplerCacheKeyEXT::Make(0, 1, 0, 1, 4, 0, 0.0f)},
        {"addressW", SamplerCacheKeyEXT::Make(0, 1, 1, 0, 4, 0, 0.0f)},
        {"maxAnisotropy", SamplerCacheKeyEXT::Make(0, 1, 1, 1, 8, 0, 0.0f)},
        {"maxMipLevel", SamplerCacheKeyEXT::Make(0, 1, 1, 1, 4, 3, 0.0f)},
        {"lodBias", SamplerCacheKeyEXT::Make(0, 1, 1, 1, 4, 0, -1.5f)},
    };
    for (const Variant& variant : variants)
    {
        EXPECT_FALSE(base == variant.key)
            << "changing " << variant.name << " must change the sampler's identity";
    }

    // All nine TextureFilter ordinals and all three TextureAddressMode ordinals on each axis are
    // separate samplers -- REMED-GFX-170's own finding, re-pinned here against the new key.
    std::set<std::size_t> hashes;
    std::vector<SamplerCacheKeyEXT> keys;
    for (int filter = 0; filter <= 8; ++filter)
    {
        for (int u = 0; u <= 2; ++u)
        {
            for (int v = 0; v <= 2; ++v)
            {
                keys.push_back(SamplerCacheKeyEXT::Make(filter, u, v, 1, 4, 0, 0.0f));
                hashes.insert(SamplerCacheKeyHashEXT{}(keys.back()));
            }
        }
    }
    for (std::size_t i = 0; i < keys.size(); ++i)
        for (std::size_t j = i + 1; j < keys.size(); ++j)
            ASSERT_FALSE(keys[i] == keys[j]) << "filter/address triples collapsed at " << i << '/' << j;
    EXPECT_EQ(hashes.size(), keys.size());
}

// MaxMipLevel is a signed public property. The packed key masked it to eight bits, so -1 and 255
// were the same request; nothing clamps it before the key is built, so the struct must carry it
// whole.
TEST(SdlGpuSamplerCacheKeyTest, MaxMipLevelKeepsItsFullSignedRange)
{
    const int levels[] = {-1, 0, 1, 3, 15, 255, 256, 1024, -256};
    std::vector<SamplerCacheKeyEXT> keys;
    for (const int level : levels)
        keys.push_back(SamplerCacheKeyEXT::Make(0, 1, 1, 1, 4, level, 0.0f));
    for (std::size_t i = 0; i < keys.size(); ++i)
        for (std::size_t j = i + 1; j < keys.size(); ++j)
            EXPECT_FALSE(keys[i] == keys[j])
                << "MaxMipLevel " << levels[i] << " and " << levels[j] << " aliased";
}

#endif  // CNA_RENDERER_SDL_GPU
