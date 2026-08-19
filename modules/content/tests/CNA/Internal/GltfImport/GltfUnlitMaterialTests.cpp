// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-337 / GLTF-338: `KHR_materials_unlit`.
//
// One of the few glTF extensions that MAPS onto XNA rather than being approximated. The extension
// means "shade this surface with its base colour and nothing else", and `BasicEffect`'s
// `LightingEnabled = false` is precisely that -- so the assertions here are equalities, not
// tolerances on a documented loss.
//
// Two things make it more than a one-line flag, and both are asserted:
//
//   * the base colour has to travel with it. On the non-PBR path nothing else reads
//     `baseColorFactor`, so an unlit material would otherwise import unlit and WHITE -- which is
//     the effect's own default, and therefore indistinguishable from "the extension was ignored"
//     for any material that happens to be white;
//   * the lighting rig has to be skipped. Every path through `ApplyPunctualLightsEXT` ends with
//     lighting on -- the no-lights fallback calls `EnableDefaultLighting()` -- so applying it
//     after setting the flag would immediately undo it, and the file would render lit by XNA's
//     default three-light rig with no sign anything went wrong.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"

using CNA::Internal::JsonValue;
using CnaTest::GltfOracle::BoolOr;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::NumberOr;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

namespace
{
    constexpr float kTolerance = 1e-5f;

    /// The first mesh part's effect of a freshly loaded corpus fixture.
    Microsoft::Xna::Framework::Graphics::Effect* FirstEffect(Model& model)
    {
        const auto& meshes = model.getMeshesProperty();
        if (meshes.getCountProperty() == 0) { return nullptr; }
        const auto& parts = meshes[0]->getMeshPartsProperty();
        if (parts.getCountProperty() == 0) { return nullptr; }
        return parts[0]->getEffectProperty();
    }
}

TEST(GltfUnlitMaterial, AnUnlitMaterialTurnsLightingOffAndCarriesItsBaseColour)
{
    const LoadedFixture fixture("mat-unlit");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& expected = Path(fixture.Expected(), "l4.unlit");

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-unlit");

    auto* basic = dynamic_cast<BasicEffect*>(FirstEffect(model));
    ASSERT_NE(nullptr, basic) << "an unlit material must reach BasicEffect, not the PBR path";
    EXPECT_EQ(BoolOr(expected, "lightingEnabled", true), basic->getLightingEnabledProperty());

    // The colour, and the reason it is authored neither white nor grey: at white this assertion
    // would pass against an effect that had never been given a colour at all.
    const std::vector<double> rgb = Numbers(Member(expected, "diffuseColor"));
    ASSERT_EQ(3u, rgb.size());
    const auto diffuse = basic->getDiffuseColorProperty();
    EXPECT_NEAR(static_cast<float>(rgb[0]), diffuse.X, kTolerance);
    EXPECT_NEAR(static_cast<float>(rgb[1]), diffuse.Y, kTolerance);
    EXPECT_NEAR(static_cast<float>(rgb[2]), diffuse.Z, kTolerance);
    EXPECT_NEAR(static_cast<float>(NumberOr(expected, "alpha", -1.0)), basic->getAlphaProperty(),
                kTolerance);
}

TEST(GltfUnlitMaterial, TheDefaultLightingFallbackDoesNotUndoTheUnlitFlag)
{
    // The failure this exists to catch, and the one that would look like nothing at all. Both
    // unlit fixtures declare no light, so the no-lights fallback applies -- and that fallback
    // calls EnableDefaultLighting(), which sets LightingEnabled = true. Ordering the flag before
    // the rig would leave every unlit material in the corpus lit by XNA's default three-light
    // arrangement, which renders perfectly plausibly.
    for (const std::string& id : {"mat-unlit", "mat-unlit-vertex-color-alpha"})
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        auto* basic = dynamic_cast<BasicEffect*>(FirstEffect(model));
        ASSERT_NE(nullptr, basic);
        EXPECT_FALSE(basic->getLightingEnabledProperty());

        // ...and the rig demonstrably did not run. Asserted on slot 0's COLOUR rather than on its
        // Enabled flag: BasicEffect's own constructor enables slot 0, so an Enabled check would be
        // testing a default. EnableDefaultLighting() overwrites the colour with XNA's warm key
        // light, so a slot still at the effect's own black is proof the rig was skipped.
        const auto key = basic->getDirectionalLight0Property().getDiffuseColorProperty();
        EXPECT_NEAR(0.0f, key.X + key.Y + key.Z, kTolerance)
            << "slot 0 carries a lighting colour, so EnableDefaultLighting() ran after the unlit "
               "flag was set and silently turned lighting back on";
        EXPECT_FALSE(basic->getDirectionalLight1Property().getEnabledProperty());
        EXPECT_FALSE(basic->getDirectionalLight2Property().getEnabledProperty());
    }
}

TEST(GltfUnlitMaterial, ALitMaterialStillGetsTheDefaultLightingRig)
{
    // The control. Without it, the assertions above are satisfied by an importer that turned
    // lighting off for everything -- which would be a far larger regression than leaving unlit
    // materials lit, and would pass every test in this file.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-vertex-color-pbr");

    // plan_gltf.md GLTF-462: this primitive is vertex-coloured AND metallic-roughness, which used to
    // put it on BasicEffect; it arrives as a PbrEffect now, with its colour multiplying base colour.
    // The control's point is unchanged and is the reason `unlitEXT` is its own flag rather than "not
    // usePbr": a LIT material must still get the lighting rig, whichever effect carries it.
    auto* pbr = dynamic_cast<PbrEffect*>(FirstEffect(model));
    ASSERT_NE(nullptr, pbr) << "a vertex-coloured metallic-roughness primitive imports through "
                               "PbrEffect (GLTF-462) -- and must still be LIT";
    EXPECT_TRUE(pbr->VertexColorEnabledEXT)
        << "the colour stream is present in the stride-60 record, so the effect has to be told to "
           "read it -- otherwise COLOR_0 arrives on the GPU and is ignored";
    EXPECT_TRUE(pbr->getLightingEnabledProperty());
    EXPECT_TRUE(pbr->getDirectionalLight0Property().getEnabledProperty());
}

// --- GLTF-338: unlit combined with vertex colour and alpha ---------------------------------------

TEST(GltfUnlitMaterial, UnlitSurvivesAlongsideVertexColourAndATranslucentBaseColour)
{
    // The two things an unlit material is most often combined with, and the two most easily lost
    // when "lighting off" is implemented as a special case rather than as a flag. Vertex colour
    // multiplies the diffuse colour in the shader exactly as it does for a lit BasicEffect, so
    // unlit needs no separate path -- but an implementation that made it its own effect type would
    // have to reimplement that, and would drop the alpha channel first.
    const LoadedFixture fixture("mat-unlit-vertex-color-alpha");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& expected = Path(fixture.Expected(), "l4.unlit");

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-unlit-vertex-color-alpha");

    auto* basic = dynamic_cast<BasicEffect*>(FirstEffect(model));
    ASSERT_NE(nullptr, basic);
    EXPECT_FALSE(basic->getLightingEnabledProperty());
    EXPECT_EQ(BoolOr(expected, "vertexColorEnabled", false), basic->VertexColorEnabled)
        << "the colour bytes are in the vertex buffer either way; without this flag the shader "
           "simply ignores them";

    // The extension does not exempt a surface from alphaMode: an unlit material with a
    // translucent base colour is the ordinary way to author a decal, and it is exactly where
    // "unlit" is most likely to be read as "no material state at all".
    EXPECT_NEAR(static_cast<float>(NumberOr(expected, "alpha", -1.0)), basic->getAlphaProperty(),
                kTolerance)
        << "baseColorFactor's alpha was dropped, so the surface is translucent at the wrong "
           "translucency -- or opaque";

    const std::vector<double> rgb = Numbers(Member(expected, "diffuseColor"));
    ASSERT_EQ(3u, rgb.size());
    const auto diffuse = basic->getDiffuseColorProperty();
    EXPECT_NEAR(static_cast<float>(rgb[0]), diffuse.X, kTolerance);
    EXPECT_NEAR(static_cast<float>(rgb[1]), diffuse.Y, kTolerance);
    EXPECT_NEAR(static_cast<float>(rgb[2]), diffuse.Z, kTolerance);
}

TEST(GltfUnlitMaterial, ASkinnedUnlitMaterialIsApproximatedRatherThanMapped)
{
    // The named limit in the registry's own classification, asserted so it is a recorded boundary
    // rather than an untested claim. SkinnedEffect's shader is lit by construction and has no
    // LightingEnabled flag -- real XNA's has none either -- so the nearest expressible thing is a
    // rig that contributes nothing but the surface's own colour: no directional light, ambient
    // white, so diffuse * ambient is diffuse.
    //
    // Approximate rather than exact, because a specular term would still apply if the material
    // asked for one. Saying so here is the difference between a limit and a bug.
    const LoadedFixture fixture("skin-unlit");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-unlit");

    auto* skinned = dynamic_cast<SkinnedEffect*>(FirstEffect(model));
    ASSERT_NE(nullptr, skinned) << "a skinned unlit primitive reaches SkinnedEffect";
    EXPECT_FALSE(skinned->getDirectionalLight0Property().getEnabledProperty());
    EXPECT_FALSE(skinned->getDirectionalLight1Property().getEnabledProperty());
    EXPECT_FALSE(skinned->getDirectionalLight2Property().getEnabledProperty());

    const auto ambient = skinned->getAmbientLightColorProperty();
    EXPECT_NEAR(1.0f, ambient.X, kTolerance)
        << "with no directional light and a black ambient the surface would be BLACK, which is a "
           "worse answer than leaving it lit";
    EXPECT_NEAR(1.0f, ambient.Y, kTolerance);
    EXPECT_NEAR(1.0f, ambient.Z, kTolerance);
}

TEST(GltfUnlitMaterial, EveryUnlitFixtureCarriesTheFlagThroughTheImporterItself)
{
    // Below the loaders, so a change that fixed one loader and not the other cannot hide here.
    // `unlitEXT` is its own flag rather than "not usePbr" because the two mean different things,
    // and the sweep asserts that distinction directly: every unlit fixture sets it, and the
    // vertex-coloured metallic-roughness fixture -- which is also non-PBR -- does not.
    using namespace CNA::Internal::GltfImport;

    struct Case { const char* id; bool unlit; };
    for (const Case& c : {Case{"mat-unlit", true},
                          Case{"mat-unlit-vertex-color-alpha", true},
                          Case{"skin-unlit", true},
                          Case{"mat-vertex-color-pbr", false},
                          Case{"mat-factor-only-gold", false}})
    {
        SCOPED_TRACE(c.id);
        const LoadedFixture fixture(c.id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const cgltf_data& data = fixture.Data();
        ASSERT_GT(data.meshes_count, 0u);
        const MeshOut out = ExtractMesh(&data, data.meshes[0].primitives[0], "probe", nullptr, 1.0f);
        EXPECT_EQ(c.unlit, out.unlitEXT);
        if (c.unlit)
        {
            EXPECT_FALSE(out.usePbr) << "an unlit material is never metallic-roughness";
        }
    }
}

// --- GLTF-349: an archived material model, converted rather than refused -------------------------

TEST(GltfUnlitMaterial, ASpecularGlossinessMaterialIsConvertedToMetallicRoughness)
{
    // Khronos archived the extension, but it is what a decade of older assets are authored in, so
    // refusing it would reject content that is otherwise perfectly importable. Converted with the
    // standard mapping -- and every term asserted, because a conversion that dropped or inverted
    // one produces a plausible material rather than an obviously wrong one.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("mat-specular-glossiness");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& expected = Path(fixture.Expected(), "l4.specularGlossiness");

    const cgltf_data& data = fixture.Data();
    const MeshOut out = ExtractMesh(&data, data.meshes[0].primitives[0], "probe", nullptr, 1.0f);

    EXPECT_TRUE(out.convertedFromSpecularGlossinessEXT);
    EXPECT_TRUE(out.usePbr)
        << "the point of converting is that the material reaches the PBR path; leaving it on the "
           "non-PBR one, as before, silently drops every factor it carries";

    const std::vector<double> base = Numbers(Member(expected, "convertedBaseColorFactor"));
    ASSERT_EQ(4u, base.size());
    EXPECT_NEAR(static_cast<float>(base[0]), out.material.baseColorFactor.X, kTolerance);
    EXPECT_NEAR(static_cast<float>(base[1]), out.material.baseColorFactor.Y, kTolerance);
    EXPECT_NEAR(static_cast<float>(base[2]), out.material.baseColorFactor.Z, kTolerance);
    EXPECT_NEAR(static_cast<float>(base[3]), out.material.baseColorFactor.W, kTolerance);

    EXPECT_NEAR(static_cast<float>(NumberOr(expected, "convertedMetallicFactor", -1.0)),
                out.material.metallicFactor, kTolerance);
    // Glossiness 0.8 gives roughness 0.2 -- neither the glTF default (1.0) nor the un-inverted
    // 0.8, so "forgot to invert" and "forgot entirely" are both separable from correct.
    EXPECT_NEAR(static_cast<float>(NumberOr(expected, "convertedRoughnessFactor", -1.0)),
                out.material.roughnessFactor, kTolerance);

    // The loss, with its size: near 0 means the material was effectively dielectric and the
    // conversion is close to exact; near 1 means a strongly coloured specular went missing.
    EXPECT_NEAR(static_cast<float>(NumberOr(expected, "droppedSpecularStrength", -1.0)),
                out.droppedSpecularStrengthEXT, 1e-3f);
}

TEST(GltfUnlitMaterial, ConvertingSpecularGlossinessDoesNotAmountToClaimingIt)
{
    // An approximation is not an implementation. A file that *requires* the extension is asking
    // for the very term the conversion discards, so it must still be refused -- otherwise
    // converting it would have quietly widened the extensionsRequired gate.
    using namespace CNA::Internal::GltfImport;
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_pbrSpecularGlossiness"));

    const GltfExtensionRecordEXT* record =
        FindGltfExtensionEXT("KHR_materials_pbrSpecularGlossiness");
    ASSERT_NE(nullptr, record);
    EXPECT_EQ(GltfExtensionSupportEXT::Approximated, record->support);
    EXPECT_FALSE(record->claimed);
}

TEST(GltfUnlitMaterial, AnOrdinaryMetallicRoughnessMaterialIsNotReportedAsConverted)
{
    // The control. Without it, a flag hardwired true would satisfy every assertion above, and
    // every import would carry a warning about an extension the file never mentioned.
    using namespace CNA::Internal::GltfImport;

    for (const std::string& id : {"mat-factor-only-gold", "mat-unlit"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const cgltf_data& data = fixture.Data();
        const MeshOut out = ExtractMesh(&data, data.meshes[0].primitives[0], "probe", nullptr, 1.0f);
        EXPECT_FALSE(out.convertedFromSpecularGlossinessEXT);
        EXPECT_FLOAT_EQ(0.0f, out.droppedSpecularStrengthEXT);
    }
}
