// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2070: KHR_materials_clearcoat, carried beside a PbrMaterial rather than inside
// one.
//
// The placement is the decision worth testing around. PbrMaterial's defining property is that it is
// lossless against PbrEffect, and a field PbrEffect has no state for would break that quietly. So
// these live in their own value type -- which then needs the same value semantics PbrMaterial has,
// because a material description that cannot be compared or hashed is not a description.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <string>
#include <unordered_set>

namespace {

using CNA::Graphics::PbrMaterialExtensions;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

TEST(PbrMaterialExtensionsTest, TheDefaultSetChangesNothing)
{
    const PbrMaterialExtensions extensions;
    EXPECT_TRUE(extensions.isNeutral());
    EXPECT_FLOAT_EQ(extensions.getClearcoatFactor(), 0.0f)
        << "every lobe has to be off by default, or a material that names none of them is not the "
           "material it was before";
    EXPECT_FLOAT_EQ(extensions.getClearcoatRoughness(), 0.0f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatNormalScale(), 1.0f);
    EXPECT_EQ(extensions.getClearcoatTexture(), nullptr);
    EXPECT_EQ(extensions.getClearcoatRoughnessTexture(), nullptr);
    EXPECT_EQ(extensions.getClearcoatNormalTexture(), nullptr);
    EXPECT_EQ(extensions.ToString(), "{}") << "a set with nothing on should not cost a line of zeros";
}

TEST(PbrMaterialExtensionsTest, EveryFieldRoundTripsAndIsValidated)
{
    GraphicsDevice gd;
    Texture2D strength(gd, 1, 1);
    Texture2D roughness(gd, 1, 1);
    Texture2D normal(gd, 1, 1);

    PbrMaterialExtensions extensions;
    extensions.setClearcoatFactor(0.75f);
    extensions.setClearcoatRoughness(0.25f);
    extensions.setClearcoatNormalScale(2.5f);
    extensions.setClearcoatTexture(&strength);
    extensions.setClearcoatRoughnessTexture(&roughness);
    extensions.setClearcoatNormalTexture(&normal);

    EXPECT_FLOAT_EQ(extensions.getClearcoatFactor(), 0.75f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatRoughness(), 0.25f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatNormalScale(), 2.5f);
    EXPECT_EQ(extensions.getClearcoatTexture(), &strength);
    EXPECT_EQ(extensions.getClearcoatRoughnessTexture(), &roughness);
    EXPECT_EQ(extensions.getClearcoatNormalTexture(), &normal);
    EXPECT_FALSE(extensions.isNeutral());

    extensions.setClearcoatFactor(5.0f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatFactor(), 1.0f);
    extensions.setClearcoatFactor(-1.0f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatFactor(), 0.0f) << "zero is how the lobe is turned off";
    extensions.setClearcoatRoughness(2.0f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatRoughness(), 1.0f);
    extensions.setClearcoatNormalScale(-2.0f);
    EXPECT_FLOAT_EQ(extensions.getClearcoatNormalScale(), 2.5f)
        << "a negative normal scale is nonsense and must be ignored, not stored";
}

TEST(PbrMaterialExtensionsTest, EqualityComparesEveryFieldIncludingTheTextures)
{
    GraphicsDevice gd;
    Texture2D first(gd, 1, 1);
    Texture2D second(gd, 1, 1);

    PbrMaterialExtensions a;
    PbrMaterialExtensions b;
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);

    a.setClearcoatFactor(0.5f);
    EXPECT_NE(a, b);
    b.setClearcoatFactor(0.5f);
    EXPECT_EQ(a, b);

    a.setClearcoatRoughness(0.3f);
    EXPECT_NE(a, b);
    b.setClearcoatRoughness(0.3f);

    a.setClearcoatNormalScale(1.5f);
    EXPECT_NE(a, b);
    b.setClearcoatNormalScale(1.5f);

    a.setClearcoatTexture(&first);
    EXPECT_NE(a, b) << "a bound texture must make two sets differ";
    b.setClearcoatTexture(&second);
    EXPECT_NE(a, b) << "two different textures must not compare equal";
    b.setClearcoatTexture(&first);
    EXPECT_EQ(a, b);

    a.setClearcoatRoughnessTexture(&first);
    EXPECT_NE(a, b);
    b.setClearcoatRoughnessTexture(&first);

    a.setClearcoatNormalTexture(&second);
    EXPECT_NE(a, b);
    b.setClearcoatNormalTexture(&second);
    EXPECT_EQ(a, b) << "every field has now been set on both, so they must agree again";
}

TEST(PbrMaterialExtensionsTest, EqualSetsHashEqually)
{
    GraphicsDevice gd;
    Texture2D texture(gd, 1, 1);

    PbrMaterialExtensions a;
    a.setClearcoatFactor(0.4f);
    a.setClearcoatRoughness(0.2f);
    a.setClearcoatTexture(&texture);

    PbrMaterialExtensions b = a;
    ASSERT_EQ(a, b);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());

    // Not a requirement that unequal sets hash differently -- that is not what a hash promises --
    // but a hash that ignored a field would be useless, so a few obvious changes are checked.
    std::unordered_set<std::size_t> hashes{a.GetHashCode()};
    b.setClearcoatFactor(0.9f);
    hashes.insert(b.GetHashCode());
    b.setClearcoatRoughness(0.9f);
    hashes.insert(b.GetHashCode());
    b.setClearcoatTexture(nullptr);
    hashes.insert(b.GetHashCode());
    EXPECT_EQ(hashes.size(), 4u) << "a field is missing from the hash";
}

TEST(PbrMaterialExtensionsTest, ToStringNamesOnlyTheLobesThatAreOn)
{
    GraphicsDevice gd;
    Texture2D texture(gd, 1, 1);

    PbrMaterialExtensions extensions;
    EXPECT_EQ(extensions.ToString(), "{}");

    extensions.setClearcoatFactor(0.5f);
    extensions.setClearcoatRoughness(0.25f);
    EXPECT_EQ(extensions.ToString(), "{Clearcoat:{Factor:0.5 Roughness:0.25 Textures:0}}");

    extensions.setClearcoatTexture(&texture);
    extensions.setClearcoatNormalTexture(&texture);
    EXPECT_EQ(extensions.ToString(), "{Clearcoat:{Factor:0.5 Roughness:0.25 Textures:2}}");

    extensions.setClearcoatFactor(0.0f);
    EXPECT_EQ(extensions.ToString(), "{}")
        << "a lobe turned off should disappear from the summary even with its maps still bound";
}

} // namespace

#endif // CNA_CNAEXT
