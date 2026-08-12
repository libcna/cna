// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-202/GLTF-203: glTF sampler state, and the §14.2 mapping table.
//
// `cgltf_sampler`, `mag_filter` and `wrap_s` had ZERO occurrences in CNA. Every imported texture
// was drawn with whatever `SamplerState` the device happened to have, which defaults to
// `LinearWrap` -- so an asset authored `CLAMP_TO_EDGE` with UVs outside [0,1] tiled instead of
// clamping, which is a large, obvious error rather than a subtle one.
//
// The whole table is tested here rather than through fixtures because `MapGltfSamplerEXT` takes raw
// glTF enum values: every one of the 6 x 2 x 3 x 3 combinations can be stated directly, which no
// realistic number of fixtures could cover.

#include <string>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using CNA::Internal::GltfImport::MapGltfSamplerEXT;
using CNA::Internal::GltfImport::SamplerOut;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

namespace
{
    // glTF's own constants, spelled out so a row of the table below reads like the specification.
    constexpr int kNearest              = 9728;
    constexpr int kLinear               = 9729;
    constexpr int kNearestMipmapNearest = 9984;
    constexpr int kLinearMipmapNearest  = 9985;
    constexpr int kNearestMipmapLinear  = 9986;
    constexpr int kLinearMipmapLinear   = 9987;
    constexpr int kRepeat               = 10497;
    constexpr int kClampToEdge          = 33071;
    constexpr int kMirroredRepeat       = 33648;
    constexpr int kUndefined            = 0;
}

// §14.2's filter table, every row. XNA turns out to cover glTF's filter space EXACTLY -- its nine
// TextureFilter values express all eight min x mag x mip combinations -- so none of these is an
// approximation, which is worth asserting because the plan originally assumed otherwise.
TEST(GltfSamplerMapping, EveryMinMagCombinationMapsToItsExactXnaFilter)
{
    struct Row
    {
        int minFilter;
        int magFilter;
        TextureFilter expected;
        const char* what;
    };

    const Row rows[] = {
        // Both linear, both mip modes.
        {kLinearMipmapLinear,   kLinear,  TextureFilter::Linear,        "trilinear"},
        {kLinearMipmapNearest,  kLinear,  TextureFilter::LinearMipPoint, "linear min/mag, point mip"},
        // Both point, both mip modes.
        {kNearestMipmapNearest, kNearest, TextureFilter::Point,          "point everywhere"},
        {kNearestMipmapLinear,  kNearest, TextureFilter::PointMipLinear, "point min/mag, linear mip"},
        // Mixed: linear minification, point magnification.
        {kLinearMipmapLinear,   kNearest, TextureFilter::MinLinearMagPointMipLinear, "mixed, linear mip"},
        {kLinearMipmapNearest,  kNearest, TextureFilter::MinLinearMagPointMipPoint,  "mixed, point mip"},
        // Mixed the other way: point minification, linear magnification.
        {kNearestMipmapLinear,  kLinear,  TextureFilter::MinPointMagLinearMipLinear, "mixed, linear mip"},
        {kNearestMipmapNearest, kLinear,  TextureFilter::MinPointMagLinearMipPoint,  "mixed, point mip"},
    };

    for (const Row& row : rows)
    {
        SCOPED_TRACE(std::string(row.what) + " (min=" + std::to_string(row.minFilter) +
                     ", mag=" + std::to_string(row.magFilter) + ")");
        const SamplerOut mapped =
            MapGltfSamplerEXT(row.magFilter, row.minFilter, kRepeat, kRepeat);
        EXPECT_EQ(row.expected, mapped.filter);
        EXPECT_FALSE(mapped.minFilterHasNoMipStage) << "this minFilter does have a mip stage";
    }
}

// The two minFilters with no mip stage at all. XNA's TextureFilter cannot say "base level only" --
// that is a property of the texture's level count, not of the sampler -- so the mip mode carried
// here is arbitrary, and the flag is what stops that from being an unrecorded guess.
TEST(GltfSamplerMapping, ANonMipmappedMinFilterIsFlaggedRatherThanSilentlyApproximated)
{
    for (const int magFilter : {kNearest, kLinear})
    {
        SCOPED_TRACE("mag=" + std::to_string(magFilter));

        const SamplerOut nearest = MapGltfSamplerEXT(magFilter, kNearest, kRepeat, kRepeat);
        EXPECT_TRUE(nearest.minFilterHasNoMipStage);
        const SamplerOut linear = MapGltfSamplerEXT(magFilter, kLinear, kRepeat, kRepeat);
        EXPECT_TRUE(linear.minFilterHasNoMipStage);

        // Point is chosen as the least-blending mip mode, so minification still behaves as the
        // file asked even once GLTF-206 starts generating levels.
        EXPECT_EQ(magFilter == kNearest ? TextureFilter::Point
                                        : TextureFilter::MinPointMagLinearMipPoint,
                  nearest.filter);
        EXPECT_EQ(magFilter == kNearest ? TextureFilter::MinLinearMagPointMipPoint
                                        : TextureFilter::LinearMipPoint,
                  linear.filter);
    }
}

// §14.2's wrap table. The two axes are independent, which is the point: a texture may repeat in U
// and clamp in V, and a mapping that read one field for both would pass every symmetric test.
TEST(GltfSamplerMapping, WrapModesMapPerAxisIndependently)
{
    struct Row { int wrap; TextureAddressMode expected; };
    const Row rows[] = {
        {kRepeat,         TextureAddressMode::Wrap},
        {kClampToEdge,    TextureAddressMode::Clamp},
        {kMirroredRepeat, TextureAddressMode::Mirror},
        {kUndefined,      TextureAddressMode::Wrap},   // §3.8.4's default
    };

    for (const Row& u : rows)
    {
        for (const Row& v : rows)
        {
            SCOPED_TRACE("wrapS=" + std::to_string(u.wrap) + " wrapT=" + std::to_string(v.wrap));
            const SamplerOut mapped =
                MapGltfSamplerEXT(kLinear, kLinearMipmapLinear, u.wrap, v.wrap);
            EXPECT_EQ(u.expected, mapped.addressU);
            EXPECT_EQ(v.expected, mapped.addressV);
        }
    }
}

// The case that motivated the whole task: CLAMP_TO_EDGE must not come out as Wrap. Called out
// separately from the table above because it is the failure an asset actually shows.
TEST(GltfSamplerMapping, ClampToEdgeDoesNotTile)
{
    const SamplerOut mapped =
        MapGltfSamplerEXT(kLinear, kLinearMipmapLinear, kClampToEdge, kClampToEdge);
    EXPECT_EQ(TextureAddressMode::Clamp, mapped.addressU);
    EXPECT_EQ(TextureAddressMode::Clamp, mapped.addressV);
    EXPECT_NE(TextureAddressMode::Wrap, mapped.addressU)
        << "a clamped texture still tiles -- UVs outside [0,1] will repeat the image";
}

// §3.8.4: an absent sampler means repeat with an implementation-chosen filter. CNA reads that as
// LinearWrap -- which is what the device already defaulted to, so this is the one case where the
// old behaviour was accidentally right. `declared` is what keeps the two distinguishable.
TEST(GltfSamplerMapping, AnUndefinedSamplerIsGltfsOwnDefaultAndSaysSo)
{
    const SamplerOut mapped = MapGltfSamplerEXT(kUndefined, kUndefined, kUndefined, kUndefined);
    EXPECT_EQ(TextureFilter::Linear, mapped.filter);
    EXPECT_EQ(TextureAddressMode::Wrap, mapped.addressU);
    EXPECT_EQ(TextureAddressMode::Wrap, mapped.addressV);
    EXPECT_FALSE(mapped.declared)
        << "an undefined sampler claims to have been declared -- an import report could not then "
           "tell 'the author chose repeat' from 'the author said nothing'";
    EXPECT_FALSE(mapped.minFilterHasNoMipStage)
        << "the default filter is the auto one, which does mipmap";
}
