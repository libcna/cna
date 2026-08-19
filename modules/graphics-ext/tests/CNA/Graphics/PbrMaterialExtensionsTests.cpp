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
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <string>
#include <unordered_set>

namespace {

using CNA::Graphics::PbrMaterialExtensions;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Vector3;

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
    EXPECT_FALSE(extensions.isSubsurfaceEnabled());
    EXPECT_FLOAT_EQ(extensions.getSubsurfaceWrap(), 0.5f);
    EXPECT_FALSE(extensions.isSheenEnabled());
    EXPECT_FLOAT_EQ(extensions.getSheenColorFactor().X, 0.0f);
    EXPECT_FLOAT_EQ(extensions.getSheenRoughness(), 0.0f);
    EXPECT_EQ(extensions.getSheenColorTexture(), nullptr);
    EXPECT_EQ(extensions.getSheenRoughnessTexture(), nullptr);
    EXPECT_EQ(extensions.ToString(), "{}") << "a set with nothing on should not cost a line of zeros";
}

TEST(PbrMaterialExtensionsTest, TheSheenFieldsRoundTripAndAreValidated)
{
    GraphicsDevice gd;
    Texture2D colour(gd, 1, 1);
    Texture2D roughness(gd, 1, 1);

    PbrMaterialExtensions extensions;
    extensions.setSheenColorFactor(Vector3(0.2f, 0.4f, 0.8f));
    extensions.setSheenRoughness(0.6f);
    extensions.setSheenColorTexture(&colour);
    extensions.setSheenRoughnessTexture(&roughness);

    EXPECT_FLOAT_EQ(extensions.getSheenColorFactor().Z, 0.8f);
    EXPECT_FLOAT_EQ(extensions.getSheenRoughness(), 0.6f);
    EXPECT_EQ(extensions.getSheenColorTexture(), &colour);
    EXPECT_EQ(extensions.getSheenRoughnessTexture(), &roughness);
    EXPECT_TRUE(extensions.isSheenEnabled());
    EXPECT_FALSE(extensions.isNeutral());

    extensions.setSheenColorFactor(Vector3(-1.0f, 5.0f, 0.5f));
    EXPECT_FLOAT_EQ(extensions.getSheenColorFactor().X, 0.0f);
    EXPECT_FLOAT_EQ(extensions.getSheenColorFactor().Y, 1.0f);
    extensions.setSheenRoughness(9.0f);
    EXPECT_FLOAT_EQ(extensions.getSheenRoughness(), 1.0f);

    // Black is how the lobe is turned off, and it has to turn off even with its maps still bound.
    extensions.setSheenColorFactor(Vector3(0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(extensions.isSubsurfaceEnabled());
    EXPECT_FLOAT_EQ(extensions.getSubsurfaceWrap(), 0.5f);
    EXPECT_FALSE(extensions.isSheenEnabled());
    EXPECT_TRUE(extensions.isNeutral());
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

TEST(PbrMaterialExtensionsTest, TheTransmissionAndVolumeFieldsRoundTripAndAreValidated)
{
    GraphicsDevice gd;
    Texture2D transmission(gd, 1, 1);
    Texture2D thickness(gd, 1, 1);

    PbrMaterialExtensions extensions;
    EXPECT_FALSE(extensions.isTransmissionEnabled());
    EXPECT_FLOAT_EQ(extensions.getThicknessFactor(), 0.0f)
        << "glTF's default is a thin surface with no interior";
    EXPECT_FLOAT_EQ(extensions.getAttenuationDistance(), 0.0f)
        << "zero stands for the infinite distance of a medium that absorbs nothing";
    EXPECT_FLOAT_EQ(extensions.getAttenuationColor().X, 1.0f);

    extensions.setTransmissionFactor(0.8f);
    extensions.setTransmissionTexture(&transmission);
    extensions.setThicknessFactor(1.25f);
    extensions.setThicknessTexture(&thickness);
    extensions.setAttenuationDistance(3.0f);
    extensions.setAttenuationColor(Vector3(0.9f, 0.4f, 0.1f));

    EXPECT_TRUE(extensions.isTransmissionEnabled());
    EXPECT_FALSE(extensions.isNeutral());
    EXPECT_FLOAT_EQ(extensions.getTransmissionFactor(), 0.8f);
    EXPECT_EQ(extensions.getTransmissionTexture(), &transmission);
    EXPECT_FLOAT_EQ(extensions.getThicknessFactor(), 1.25f);
    EXPECT_EQ(extensions.getThicknessTexture(), &thickness);
    EXPECT_FLOAT_EQ(extensions.getAttenuationDistance(), 3.0f);
    EXPECT_FLOAT_EQ(extensions.getAttenuationColor().Y, 0.4f);

    extensions.setTransmissionFactor(3.0f);
    EXPECT_FLOAT_EQ(extensions.getTransmissionFactor(), 1.0f);
    extensions.setThicknessFactor(-2.0f);
    EXPECT_FLOAT_EQ(extensions.getThicknessFactor(), 1.25f)
        << "a negative thickness is nonsense and must be ignored";
    extensions.setAttenuationDistance(-4.0f);
    EXPECT_FLOAT_EQ(extensions.getAttenuationDistance(), 0.0f)
        << "a negative distance folds onto the value meaning infinite";
    extensions.setAttenuationColor(Vector3(2.0f, -1.0f, 0.5f));
    EXPECT_FLOAT_EQ(extensions.getAttenuationColor().X, 1.0f);
    EXPECT_FLOAT_EQ(extensions.getAttenuationColor().Y, 0.0f);

    extensions.setTransmissionFactor(0.0f);
    EXPECT_FALSE(extensions.isTransmissionEnabled())
        << "zero has to switch it off with its maps still bound";
}

TEST(PbrMaterialExtensionsTest, TheIridescenceFieldsRoundTripAndAreValidated)
{
    GraphicsDevice gd;
    Texture2D strength(gd, 1, 1);
    Texture2D thickness(gd, 1, 1);

    PbrMaterialExtensions extensions;
    EXPECT_FALSE(extensions.isIridescenceEnabled());
    EXPECT_FLOAT_EQ(extensions.getIridescenceIor(), 1.3f) << "glTF's default film";
    EXPECT_FLOAT_EQ(extensions.getIridescenceThicknessMinimum(), 100.0f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceThicknessMaximum(), 400.0f);

    extensions.setIridescenceFactor(0.9f);
    extensions.setIridescenceIor(1.8f);
    extensions.setIridescenceThicknessMinimum(50.0f);
    extensions.setIridescenceThicknessMaximum(900.0f);
    extensions.setIridescenceTexture(&strength);
    extensions.setIridescenceThicknessTexture(&thickness);

    EXPECT_TRUE(extensions.isIridescenceEnabled());
    EXPECT_FALSE(extensions.isNeutral());
    EXPECT_FLOAT_EQ(extensions.getIridescenceFactor(), 0.9f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceIor(), 1.8f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceThicknessMinimum(), 50.0f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceThicknessMaximum(), 900.0f);
    EXPECT_EQ(extensions.getIridescenceTexture(), &strength);
    EXPECT_EQ(extensions.getIridescenceThicknessTexture(), &thickness);

    extensions.setIridescenceFactor(2.0f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceFactor(), 1.0f);
    extensions.setIridescenceIor(0.4f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceIor(), 1.8f) << "a vacuum is the floor";
    extensions.setIridescenceThicknessMaximum(-1.0f);
    EXPECT_FLOAT_EQ(extensions.getIridescenceThicknessMaximum(), 900.0f)
        << "a negative thickness is nonsense and must be ignored";

    extensions.setIridescenceFactor(0.0f);
    EXPECT_FALSE(extensions.isIridescenceEnabled());
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

    a.setSheenColorFactor(Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_NE(a, b);
    b.setSheenColorFactor(Vector3(0.1f, 0.2f, 0.3f));
    a.setSheenRoughness(0.7f);
    EXPECT_NE(a, b);
    b.setSheenRoughness(0.7f);
    a.setSheenColorTexture(&first);
    EXPECT_NE(a, b);
    b.setSheenColorTexture(&first);
    a.setSheenRoughnessTexture(&second);
    EXPECT_NE(a, b);
    b.setSheenRoughnessTexture(&second);

    a.setTransmissionFactor(0.5f);
    EXPECT_NE(a, b);
    b.setTransmissionFactor(0.5f);
    a.setThicknessFactor(2.0f);
    EXPECT_NE(a, b);
    b.setThicknessFactor(2.0f);
    a.setAttenuationDistance(4.0f);
    EXPECT_NE(a, b);
    b.setAttenuationDistance(4.0f);
    a.setAttenuationColor(Vector3(0.5f, 0.5f, 0.5f));
    EXPECT_NE(a, b);
    b.setAttenuationColor(Vector3(0.5f, 0.5f, 0.5f));
    a.setTransmissionTexture(&first);
    EXPECT_NE(a, b);
    b.setTransmissionTexture(&first);
    a.setThicknessTexture(&second);
    EXPECT_NE(a, b);
    b.setThicknessTexture(&second);

    a.setIridescenceFactor(0.6f);
    EXPECT_NE(a, b);
    b.setIridescenceFactor(0.6f);
    a.setIridescenceIor(1.7f);
    EXPECT_NE(a, b);
    b.setIridescenceIor(1.7f);
    a.setIridescenceThicknessMinimum(20.0f);
    EXPECT_NE(a, b);
    b.setIridescenceThicknessMinimum(20.0f);
    a.setIridescenceThicknessMaximum(800.0f);
    EXPECT_NE(a, b);
    b.setIridescenceThicknessMaximum(800.0f);
    a.setIridescenceTexture(&first);
    EXPECT_NE(a, b);
    b.setIridescenceTexture(&first);
    a.setIridescenceThicknessTexture(&second);
    EXPECT_NE(a, b);
    b.setIridescenceThicknessTexture(&second);

    a.setSubsurfaceColor(Vector3(0.4f, 0.1f, 0.1f));
    EXPECT_NE(a, b);
    b.setSubsurfaceColor(Vector3(0.4f, 0.1f, 0.1f));
    a.setSubsurfaceWrap(0.9f);
    EXPECT_NE(a, b);
    b.setSubsurfaceWrap(0.9f);
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
    b.setSheenColorFactor(Vector3(0.5f, 0.0f, 0.0f));
    hashes.insert(b.GetHashCode());
    b.setSheenRoughness(0.6f);
    hashes.insert(b.GetHashCode());
    b.setTransmissionFactor(0.7f);
    hashes.insert(b.GetHashCode());
    b.setThicknessFactor(1.5f);
    hashes.insert(b.GetHashCode());
    b.setAttenuationDistance(2.5f);
    hashes.insert(b.GetHashCode());
    b.setAttenuationColor(Vector3(0.3f, 0.3f, 0.3f));
    hashes.insert(b.GetHashCode());
    b.setIridescenceFactor(0.4f);
    hashes.insert(b.GetHashCode());
    b.setIridescenceIor(1.9f);
    hashes.insert(b.GetHashCode());
    b.setIridescenceThicknessMinimum(30.0f);
    hashes.insert(b.GetHashCode());
    b.setIridescenceThicknessMaximum(700.0f);
    hashes.insert(b.GetHashCode());
    b.setSubsurfaceColor(Vector3(0.2f, 0.1f, 0.1f));
    hashes.insert(b.GetHashCode());
    b.setSubsurfaceWrap(0.9f);
    hashes.insert(b.GetHashCode());
    EXPECT_EQ(hashes.size(), 16u) << "a field is missing from the hash";
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

    extensions.setSheenColorFactor(Vector3(0.5f, 0.25f, 0.0f));
    extensions.setSheenRoughness(0.5f);
    EXPECT_EQ(extensions.ToString(),
              "{Sheen:{Color:{X:0.5 Y:0.25 Z:0} Roughness:0.5 Textures:0}}");

    extensions.setClearcoatFactor(0.5f);
    extensions.setClearcoatRoughness(0.25f);
    EXPECT_EQ(extensions.ToString(),
              "{Sheen:{Color:{X:0.5 Y:0.25 Z:0} Roughness:0.5 Textures:0} "
              "Clearcoat:{Factor:0.5 Roughness:0.25 Textures:2}}")
        << "two lobes on at once must be separated rather than run together";

    extensions.setTransmissionFactor(0.75f);
    extensions.setThicknessFactor(2.0f);
    extensions.setAttenuationDistance(4.0f);
    EXPECT_EQ(extensions.ToString(),
              "{Transmission:{Factor:0.75 Thickness:2 AttenuationDistance:4 Textures:0} "
              "Sheen:{Color:{X:0.5 Y:0.25 Z:0} Roughness:0.5 Textures:0} "
              "Clearcoat:{Factor:0.5 Roughness:0.25 Textures:2}}");

    extensions.setIridescenceFactor(0.5f);
    EXPECT_EQ(extensions.ToString(),
              "{Iridescence:{Factor:0.5 Ior:1.3 Thickness:100..400 Textures:0} "
              "Transmission:{Factor:0.75 Thickness:2 AttenuationDistance:4 Textures:0} "
              "Sheen:{Color:{X:0.5 Y:0.25 Z:0} Roughness:0.5 Textures:0} "
              "Clearcoat:{Factor:0.5 Roughness:0.25 Textures:2}}")
        << "four lobes at once must read as four entries, in a stable order";

    extensions.setSubsurfaceColor(Vector3(0.4f, 0.1f, 0.05f));
    extensions.setSubsurfaceWrap(0.75f);
    EXPECT_EQ(extensions.ToString().substr(0, 56),
              "{Subsurface:{Color:{X:0.4 Y:0.1 Z:0.05} Wrap:0.75} Iride")
        << "the subsurface term must appear, and first";
}

} // namespace

#endif // CNA_CNAEXT
