// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-008 -- the L6 rung of the oracle ladder, and the §21.1 contract rows whose
// "Assert at" column says L6.
//
// L3 proved the importer understood the file. L5 proved it packed the right bytes. Neither says
// anything about whether those facts reach a shader: D7's factor-only gold material decoded
// perfectly at L3 for the whole of the audit and still rendered opaque white, because no code
// assigned it to an effect. Every test here compares a *captured effect parameter* against a value
// another layer already established independently -- the manifest's spec-derived material block
// (L3) or node world matrix (L4) -- so a passing run means the value survived the whole trip and
// not merely that two copies of the same mistake agree.

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfDrawParamsOracleEXT.hpp"
#include "GltfFixtureCorpus.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using CnaTest::GltfOracle::CaptureDrawParamsEXT;
using CnaTest::GltfOracle::ColumnMajor3x3;
using CnaTest::GltfOracle::ColumnMajor4x4;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::CorpusFixtureIds;
using CnaTest::GltfOracle::DrawParamsDump;
using CnaTest::GltfOracle::IsRejectionFixture;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::NormalMatrixFromWorldEXT;
using CnaTest::GltfOracle::NumberOr;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;
using CnaTest::GltfOracle::ToJson;
using CNA::Internal::GltfImport::TextureSlotEXT;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;

namespace
{
    constexpr float kTolerance = 1e-5f;

    /// A camera that is nothing like the identity in any of its three matrices, so a capture that
    /// silently dropped one -- or transposed it, or swapped view for projection -- cannot pass by
    /// coincidence.
    Matrix TestView()
    {
        return Matrix::CreateLookAt({3.0f, 4.0f, 12.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    }

    Matrix TestProjection()
    {
        return Matrix::CreatePerspectiveFieldOfView(1.0471976f, 1.6f, 0.25f, 250.0f);
    }

    void ExpectFlatNear(const std::vector<double>& expected, const float* actual, std::size_t count,
                         const std::string& what)
    {
        ASSERT_EQ(count, expected.size()) << what << ": wrong element count";
        for (std::size_t i = 0; i < count; ++i)
        {
            EXPECT_NEAR(static_cast<float>(expected[i]), actual[i], kTolerance)
                << what << ", element " << i;
        }
    }

    /// `transpose(inverse(world3x3))` built from XNA's own `Matrix::Invert`, as the independent
    /// second opinion on the renderer-side cofactor derivation the capture records.
    ColumnMajor3x3 ExpectedNormalMatrix(const Matrix& world)
    {
        Matrix upper3x3 = world;
        upper3x3.M14 = upper3x3.M24 = upper3x3.M34 = 0.0f;
        upper3x3.M41 = upper3x3.M42 = upper3x3.M43 = 0.0f;
        upper3x3.M44 = 1.0f;
        const Matrix inverseTranspose = Matrix::Transpose(Matrix::Invert(upper3x3));
        // Column-major 3x3: column c, row r is the XNA row-major element M(c+1)(r+1).
        const float* m = &inverseTranspose.M11;
        return {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    }

    /// Every fixture the runtime .gltf loader can actually produce a Model for: a rejection
    /// fixture has no model at all, and its refusal is asserted in full by
    /// GltfContainerValidation rather than half-asserted here.
    std::vector<std::string> LoadableFixtureIds()
    {
        std::vector<std::string> ids;
        for (const std::string& id : CorpusFixtureIds())
        {
            const LoadedFixture fixture(id);
            if (!fixture.Ok() || IsRejectionFixture(fixture.Expected())) { continue; }
            ids.push_back(id);
        }
        return ids;
    }

    /// The L3 material record of the fixture's first primitive, or a null value when it has none.
    const JsonValue& FirstMaterial(const LoadedFixture& fixture)
    {
        const JsonValue& primitives = Path(fixture.Expected(), "l3.primitives");
        if (primitives.type != JsonType::Array || primitives.arrayValue.empty())
        {
            return CnaTest::GltfOracle::JsonNull();
        }
        return Member(primitives.arrayValue.front(), "material");
    }
}

// --- GLTF-008: the harness's own acceptance ----------------------------------------------------

// "A PbrEffect draw yields all 12 §21.1 quantities." Asserted one contract row at a time on the
// fixture that authors every material property at once, so a row that stopped being captured
// fails by name instead of by a diff nobody reads.
TEST(GltfDrawParamsOracleL6, APbrDrawYieldsEverySection211QuantityItCanCarry)
{
    const LoadedFixture fixture("mat-factor-only-gold");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");

    const Matrix world = Matrix::CreateScale(2.0f, 3.0f, 4.0f) *
                         Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    const std::vector<DrawParamsDump> captured =
        CaptureDrawParamsEXT(model, world, TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size()) << "the fixture has exactly one drawable part";
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    // Row 1 -- World / View / Projection. The world the renderer receives is the one the effect
    // was given, and view/projection arrive unaltered.
    EXPECT_EQ(d.world, d.worldColMajor) << "the bound world is not the world the renderer gets";
    float expectedView[16];
    TestView().ToColumnMajor(expectedView);
    float expectedProjection[16];
    TestProjection().ToColumnMajor(expectedProjection);
    for (std::size_t i = 0; i < 16; ++i)
    {
        EXPECT_NEAR(expectedView[i], d.view[i], kTolerance) << "view element " << i;
        EXPECT_NEAR(expectedProjection[i], d.projection[i], kTolerance) << "projection element " << i;
    }

    // Row 2 -- normal matrix. Non-uniform scale is in the world above precisely so this row is
    // not satisfied by the world 3x3 itself.
    const ColumnMajor3x3 expectedNormal = ExpectedNormalMatrix(world);
    for (std::size_t i = 0; i < expectedNormal.size(); ++i)
    {
        EXPECT_NEAR(expectedNormal[i], d.normalMatrix[i], kTolerance) << "normal matrix " << i;
    }

    // Rows 3-5 -- base colour factor, metallic/roughness, emissive: captured, non-default, and
    // equal to what the file authored (the L3 manifest states each one independently).
    const JsonValue& material = FirstMaterial(fixture);
    ASSERT_EQ(JsonType::Object, material.type);
    ExpectFlatNear(Numbers(Member(material, "baseColorFactor")), d.diffuseColor.data(), 4,
                   "baseColorFactor -> diffuseColor");
    EXPECT_NEAR(static_cast<float>(NumberOr(material, "metallicFactor", -1.0)), d.metallicFactor,
                kTolerance);
    EXPECT_NEAR(static_cast<float>(NumberOr(material, "roughnessFactor", -1.0)), d.roughnessFactor,
                kTolerance);
    ExpectFlatNear(Numbers(Member(material, "emissiveFactor")), d.emissiveColor.data(), 3,
                   "emissiveFactor -> emissiveColor");

    // Rows 6-9 -- the texture rows. This material authors no map at all, so the capture must
    // report every slot empty; that a slot is *bound* is the L6 fact, its channel semantics L7.
    EXPECT_FALSE(d.hasBaseColorMap);
    EXPECT_FALSE(d.hasNormalMap);
    EXPECT_FALSE(d.hasMetallicRoughnessMap);
    EXPECT_FALSE(d.hasOcclusionMap);
    EXPECT_FALSE(d.hasEmissiveMap);

    // Rows 10-11 -- bone palette and influences per vertex. An unskinned draw carries no palette,
    // which is a captured fact rather than an absent one.
    EXPECT_FALSE(d.skinned);
    EXPECT_EQ(0, d.boneCount);
    EXPECT_TRUE(d.boneTransforms.empty());

    // Row 12 -- alpha mode and cutoff. Both halves are captured: what the effect carries, and what
    // the GPU parameter block would apply. They currently disagree by design (see the dedicated
    // test below), and the capture is what makes that visible instead of assumed.
    EXPECT_TRUE(d.carriesAlphaState);
    EXPECT_EQ(StringOr(material, "alphaMode", "?"), d.alphaMode);
    EXPECT_NEAR(static_cast<float>(NumberOr(material, "alphaCutoff", -1.0)), d.alphaCutoff,
                kTolerance);
    EXPECT_EQ(4u, d.alphaTest.size());

    // Row 13 -- double-sidedness, carried on the effect; applying it is RasterizerState, i.e. L7.
    EXPECT_EQ(CnaTest::GltfOracle::BoolOr(material, "doubleSided", false), d.doubleSided);

    // The shader variant the whole block selects.
    EXPECT_TRUE(d.pbr);
    EXPECT_TRUE(d.lightingEnabled);
    EXPECT_EQ("Microsoft.Xna.Framework.Graphics.PbrEffect", d.effectTypeName);
}

// A capture that reported nothing would pass every "no divergence" test in this file. It must
// cover exactly the parts ModelMesh::Draw would draw -- so it is pinned against the corpus.
TEST(GltfDrawParamsOracleL6, EveryDrawablePartOfEveryFixtureIsCaptured)
{
    std::size_t totalParts = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        std::size_t drawable = 0;
        const auto& meshes = model.getMeshesProperty();
        for (int mi = 0; mi < meshes.getCountProperty(); ++mi)
        {
            const auto& parts = meshes[mi]->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                if (parts[pi]->getEffectProperty() != nullptr &&
                    parts[pi]->getPrimitiveCountProperty() > 0)
                {
                    ++drawable;
                }
            }
        }

        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        EXPECT_EQ(drawable, captured.size()) << "capture does not cover every drawn part";
        totalParts += captured.size();
    }
    EXPECT_GT(totalParts, 0u) << "the corpus produced no drawable part at all";
}

// --- GLTF-266: World / View / Projection ---------------------------------------------------------

// The world matrix a part binds must be the node world matrix L4 computed from the file, times the
// application's own world. Asserting it against L4 -- and not against a second walk of the bone
// hierarchy -- is what makes this a conformance test rather than a consistency one.
TEST(GltfConformanceL6, BoundWorldMatrixMatchesTheExpectedNodeWorld)
{
    const Matrix appWorld = Matrix::CreateTranslation(100.0f, 200.0f, 300.0f);

    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& instances = Path(fixture.Expected(), "l4.instances");
        if (instances.type != JsonType::Array || instances.arrayValue.size() != 1u)
        {
            // A multi-instance fixture needs a mesh-to-instance mapping this assertion does not
            // carry; xf-shared-mesh's placement is already locked at L4 itself.
            continue;
        }

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        const std::vector<DrawParamsDump> captured =
            CaptureDrawParamsEXT(model, appWorld, TestView(), TestProjection());
        if (captured.empty()) { continue; }

        const std::vector<double> nodeWorld =
            Numbers(Member(instances.arrayValue.front(), "worldMatrixColumnMajor"));
        ASSERT_EQ(16u, nodeWorld.size());

        // expected = nodeWorld then the application world, in that order -- column-major, so the
        // application transform is applied last exactly as Model::Draw's `bone * world` does.
        Matrix expectedNode;
        float* e = &expectedNode.M11;
        for (std::size_t c = 0; c < 4; ++c)
        {
            for (std::size_t r = 0; r < 4; ++r)
            {
                e[c * 4 + r] = static_cast<float>(nodeWorld[c * 4 + r]);
            }
        }
        float expected[16];
        (expectedNode * appWorld).ToColumnMajor(expected);

        for (const DrawParamsDump& d : captured)
        {
            SCOPED_TRACE(ToJson(d));
            for (std::size_t i = 0; i < 16; ++i)
            {
                EXPECT_NEAR(expected[i], d.worldColMajor[i], kTolerance) << "world element " << i;
            }
        }
    }
}

// View and projection are the application's own matrices and must arrive untouched -- no
// transpose, no fold into the world, no silent identity.
TEST(GltfConformanceL6, ViewAndProjectionReachEveryDrawUnaltered)
{
    float expectedView[16];
    TestView().ToColumnMajor(expectedView);
    float expectedProjection[16];
    TestProjection().ToColumnMajor(expectedProjection);

    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        for (const DrawParamsDump& d : CaptureDrawParamsEXT(
                 model, Matrix::getIdentityProperty(), TestView(), TestProjection()))
        {
            SCOPED_TRACE(ToJson(d));
            for (std::size_t i = 0; i < 16; ++i)
            {
                EXPECT_NEAR(expectedView[i], d.view[i], kTolerance) << "view element " << i;
                EXPECT_NEAR(expectedProjection[i], d.projection[i], kTolerance)
                    << "projection element " << i;
            }
        }
    }
}

// --- GLTF-267: uNormalMatrix is transpose(inverse(world3x3)) --------------------------------------

// The renderer's own cofactor derivation, checked against XNA's Matrix::Invert on the same world
// matrix, over the whole corpus. Two independent routes to the same nine numbers.
TEST(GltfConformanceL6, NormalMatrixIsTheInverseTransposeOfTheWorldUpper3x3)
{
    // Every world 3x3 in the corpus was diagonal until an animation fixture put a rotation in one,
    // and a diagonal matrix is its own transpose -- so a transposition error in either route was
    // invisible. Counted here so the sweep cannot quietly go back to proving nothing: with only
    // symmetric worlds it would pass whichever convention it used.
    std::size_t asymmetricWorlds = 0;

    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        for (const DrawParamsDump& d : CaptureDrawParamsEXT(
                 model, Matrix::getIdentityProperty(), TestView(), TestProjection()))
        {
            SCOPED_TRACE(ToJson(d));
            // Matrix::ToColumnMajor is a straight sequential copy of XNA's row-major storage --
            // reinterpreting a row-vector matrix's rows as a column-vector matrix's columns IS
            // the transpose, so no arithmetic one is performed. Copying the array straight back
            // therefore reproduces the XNA world exactly, and transposing here would ask for the
            // inverse rotation: correct for every diagonal world, wrong for every rotated one.
            Matrix world;
            float* w = &world.M11;
            for (std::size_t i = 0; i < 16; ++i) { w[i] = d.worldColMajor[i]; }

            if (std::fabs(world.M12 - world.M21) > kTolerance ||
                std::fabs(world.M13 - world.M31) > kTolerance ||
                std::fabs(world.M23 - world.M32) > kTolerance)
            {
                ++asymmetricWorlds;
            }

            const ColumnMajor3x3 expected = ExpectedNormalMatrix(world);
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                EXPECT_NEAR(expected[i], d.normalMatrix[i], kTolerance) << "normal matrix " << i;
            }
        }
    }

    EXPECT_GT(asymmetricWorlds, 0u)
        << "no fixture's world 3x3 is asymmetric, so this sweep agrees with itself under either "
           "transposition convention and proves nothing about which one is right";
}

// The case the rule exists for. Under `scale = [2,3,4]` the world 3x3 and its inverse transpose
// send the fixture's (0.6,0.8,0) normal in visibly different directions; a normal matrix that had
// quietly become the world 3x3 would pass every uniform-scale fixture and fail here.
TEST(GltfConformanceL6, NonUniformScaleSeparatesTheNormalMatrixFromTheWorldMatrix)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-scale-nonuniform");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    // The node's scale reached the bound world at all.
    EXPECT_NEAR(2.0f, d.worldColMajor[0], kTolerance);
    EXPECT_NEAR(3.0f, d.worldColMajor[5], kTolerance);
    EXPECT_NEAR(4.0f, d.worldColMajor[10], kTolerance);

    // diag(1/2, 1/3, 1/4) -- the inverse transpose of diag(2,3,4).
    const ColumnMajor3x3 expected = {0.5f, 0.0f, 0.0f, 0.0f, 1.0f / 3.0f, 0.0f, 0.0f, 0.0f, 0.25f};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_NEAR(expected[i], d.normalMatrix[i], kTolerance) << "normal matrix " << i;
    }

    // And the two derivations really do differ here: the same normal comes out along a different
    // direction, so this fixture can fail rather than merely agree.
    const float nx = 0.6f, ny = 0.8f;
    const float correctX = d.normalMatrix[0] * nx, correctY = d.normalMatrix[4] * ny;
    const float naiveX = d.worldColMajor[0] * nx, naiveY = d.worldColMajor[5] * ny;
    const float correctRatio = correctY / correctX;
    const float naiveRatio   = naiveY / naiveX;
    EXPECT_GT(std::fabs(correctRatio - naiveRatio), 1.0f)
        << "the fixture no longer separates the two derivations";
}

// --- GLTF-218 / GLTF-220 / GLTF-223: material factors reach the shader ----------------------------

// Every material property the manifest states, on every fixture, compared against the parameter
// block the renderer would receive. This is the assertion D7 would have failed for the whole of
// the audit: the values decoded correctly at L3 and arrived at the GPU as PbrEffect's defaults.
TEST(GltfConformanceL6, MaterialFactorsReachTheBoundEffect)
{
    std::size_t checked = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& material = FirstMaterial(fixture);
        if (material.type != JsonType::Object) { continue; }

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        if (captured.empty()) { continue; }
        const DrawParamsDump& d = captured.front();
        SCOPED_TRACE(ToJson(d));
        // Only a PBR draw carries these; a vertex-coloured primitive selects BasicEffect and its
        // material factors are not this layer's contract.
        if (!d.pbr) { continue; }

        ExpectFlatNear(Numbers(Member(material, "baseColorFactor")), d.diffuseColor.data(), 4,
                       "baseColorFactor -> diffuseColor");
        EXPECT_NEAR(static_cast<float>(NumberOr(material, "metallicFactor", -1.0)),
                    d.metallicFactor, kTolerance);
        EXPECT_NEAR(static_cast<float>(NumberOr(material, "roughnessFactor", -1.0)),
                    d.roughnessFactor, kTolerance);
        // KHR_materials_emissive_strength multiplies the [0,1] emissiveFactor, and the PRODUCT is
        // what a renderer must bind. A fixture that carries the extension states all three numbers
        // (factor, strength, product) so this comparison reads the spec-derived answer rather than
        // recomputing one -- and so a strength that was applied twice, or not at all, is a
        // mismatch against a stated value.
        const JsonValue& scaledEmissive = Member(material, "emissiveFactorTimesStrength");
        ExpectFlatNear(Numbers(scaledEmissive.type == JsonType::Array
                                   ? scaledEmissive : Member(material, "emissiveFactor")),
                       d.emissiveColor.data(), 3, "emissive -> emissiveColor");
        EXPECT_EQ(StringOr(material, "alphaMode", "OPAQUE"), d.alphaMode);
        ++checked;
    }
    EXPECT_GT(checked, 0u) << "no fixture exercised the material contract";
}

// --- GLTF-258 / GLTF-263: bone palette and influence count ------------------------------------------

// The palette is game code's job in XNA -- GetSkinTransforms() into SetBoneTransforms() -- so this
// test does exactly that and then asserts the parameter block carries the same matrices, in the
// same paletteIndex order, alongside the four influences per vertex CNA always packs.
TEST(GltfConformanceL6, SkinnedDrawBindsThePaletteAndFourInfluencesPerVertex)
{
    using Microsoft::Xna::Framework::Graphics::AnimationPlayer;
    using Microsoft::Xna::Framework::Graphics::SkinningData;

    std::size_t skinnedFixtures = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        auto* skinningData = dynamic_cast<SkinningData*>(model.getTagProperty());
        if (skinningData == nullptr) { continue; }
        ++skinnedFixtures;

        AnimationPlayer player(*skinningData);
        player.Update(System::TimeSpan::Zero, false, false);
        const std::vector<Matrix>& palette = player.GetSkinTransforms();
        ASSERT_FALSE(palette.empty()) << "a skinned model with an empty palette";

        const std::size_t applied = CnaTest::GltfOracle::ApplyBonePaletteEXT(model, palette);
        EXPECT_GT(applied, 0u) << "no part carries a skinned effect";

        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        ASSERT_FALSE(captured.empty());

        for (const DrawParamsDump& d : captured)
        {
            SCOPED_TRACE(ToJson(d));
            // A file may hold a skinned mesh AND a static one (GLTF-137's `skin-plus-static-mesh`).
            // The palette belongs to the skinned parts; a static prop in the same model is not
            // skinned and must not be, so it is skipped rather than asserted against a rig.
            if (!d.skinned) { continue; }
            // GLTF-258: CNA always packs four influences, so four is what the shader must sum.
            EXPECT_EQ(4, d.weightsPerVertex);
            // GLTF-263: the uploaded palette is GetSkinTransforms()'s, entry for entry.
            ASSERT_EQ(palette.size(), static_cast<std::size_t>(d.boneCount));
            ASSERT_EQ(palette.size(), d.boneTransforms.size());
            for (std::size_t b = 0; b < palette.size(); ++b)
            {
                float expected[16];
                palette[b].ToColumnMajor(expected);
                for (std::size_t i = 0; i < 16; ++i)
                {
                    EXPECT_NEAR(expected[i], d.boneTransforms[b][i], kTolerance)
                        << "bone " << b << " element " << i;
                }
            }
        }
    }
    EXPECT_GT(skinnedFixtures, 0u) << "the corpus has no skinned fixture any more";
}

// plan_gltf.md GLTF-262. An identity bone palette is not a neutral value: it means "every joint
// matrix is the identity", so a mesh drawn that way is posed in joint space and glTF's own
// inverse(globalTransform(meshNode)) cancellation (section 3.7.3) never applies. A skinned model
// that had been loaded and not yet animated therefore rendered WRONG, not merely still -- the
// L6 capture is what made that measurable. Loading now poses the bind pose, and this test holds
// it: a freshly loaded model's palette is the bind-pose one, and it is demonstrably not the
// MaxBones identity default it used to be.
TEST(GltfConformanceL6, AFreshlyLoadedSkinnedModelIsAlreadyPosedInItsBindPose)
{
    using Microsoft::Xna::Framework::Graphics::AnimationPlayer;
    using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
    using Microsoft::Xna::Framework::Graphics::SkinningData;

    std::size_t skinnedFixtures = 0;
    bool anyFixtureHasANonIdentityBindPose = false;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        auto* skinningData = dynamic_cast<SkinningData*>(model.getTagProperty());
        if (skinningData == nullptr) { continue; }
        ++skinnedFixtures;

        // The bind-pose palette, computed the way an application would: a player with no clip
        // started leaves every bone at its bind pose.
        AnimationPlayer player(*skinningData);
        player.Update(System::TimeSpan::Zero, false, false);
        const std::vector<Matrix>& bindPose = player.GetSkinTransforms();
        ASSERT_FALSE(bindPose.empty());

        // Nothing has touched the effects since Load returned.
        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        ASSERT_FALSE(captured.empty());

        for (const DrawParamsDump& d : captured)
        {
            SCOPED_TRACE(ToJson(d));
            if (!d.skinned) { continue; }   // a static part of the same model -- see above
            ASSERT_EQ(bindPose.size(), static_cast<std::size_t>(d.boneCount))
                << "the loaded palette is not the bind-pose one";
            ASSERT_EQ(bindPose.size(), d.boneTransforms.size());
            for (std::size_t b = 0; b < bindPose.size(); ++b)
            {
                float expected[16];
                bindPose[b].ToColumnMajor(expected);
                for (std::size_t i = 0; i < 16; ++i)
                {
                    EXPECT_NEAR(expected[i], d.boneTransforms[b][i], kTolerance)
                        << "bone " << b << " element " << i;
                }
            }
        }

        // And it is not the old MaxBones-sized default, on any fixture.
        EXPECT_NE(static_cast<std::size_t>(SkinnedPbrEffect::MaxBones), bindPose.size());
        for (const Matrix& m : bindPose)
        {
            if (m != Matrix::getIdentityProperty()) { anyFixtureHasANonIdentityBindPose = true; }
        }
    }
    EXPECT_GT(skinnedFixtures, 0u) << "the corpus has no skinned fixture any more";
    // At least one corpus fixture must have a bind pose that differs from the identity default,
    // or the assertion above could pass on a model that was never posed at all. Deliberately a
    // corpus-wide claim and not a per-fixture one: `skin-armature-ancestor`'s bind pose IS
    // all-identity, and that is the whole point of GLTF-260 -- the armature ancestor's transform
    // cancels out of the joint matrix. `skin-mesh-node-transform` is the one that cannot,
    // because glTF requires the mesh node's own transform to be cancelled (section 3.7.3).
    EXPECT_TRUE(anyFixtureHasANonIdentityBindPose)
        << "no skinned fixture has a non-identity bind pose, so this test can no longer tell a "
           "posed model from an unposed one";
}

// --- GLTF-230 / GLTF-372: which half of the alpha state is applied, as numbers ---------------------

// docs/gltf-api-change-review.md §1.3/§1.4 decided that alphaMode, alphaCutoff and doubleSided
// would be CARRIED by the effect rather than applied to device state. GLTF-372 moved exactly one
// of them across that line -- the MASK cutoff, which is fragment-program work and not device state
// -- and the line itself is what this test measures: BLEND's compositing and doubleSided's culling
// are still BlendState and RasterizerState the application owns, and a BLEND material must
// therefore still bind the never-discard alpha test. Both directions matter: an alphaTest that
// stopped being the default here would mean a blended surface had started cutting holes in itself.
TEST(GltfConformanceL6, ABlendMaterialCarriesItsAlphaStateWithoutDiscardingAnything)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    // Carried: the file's own state, on the effect.
    EXPECT_TRUE(d.carriesAlphaState);
    EXPECT_EQ("BLEND", d.alphaMode);
    EXPECT_NEAR(0.5f, d.alphaCutoff, kTolerance);
    EXPECT_TRUE(d.doubleSided);
    // And the alpha itself does reach the GPU, in diffuseColor.w -- blending it is the part
    // GLTF-230 still owns.
    EXPECT_NEAR(0.5f, d.diffuseColor[3], kTolerance);

    // Not applied: GpuDrawParams::alphaTest keeps its {0,0,1,1} never-discard default. The effect
    // carries a cutoff of 0.5, so a mask rule applied to the wrong mode would show up as 0.5 here
    // and cut away every fragment this material has -- its alpha is 0.5 too.
    EXPECT_NEAR(0.0f, d.alphaTest[0], kTolerance);
    EXPECT_NEAR(0.0f, d.alphaTest[1], kTolerance);
    EXPECT_NEAR(1.0f, d.alphaTest[2], kTolerance);
    EXPECT_NEAR(1.0f, d.alphaTest[3], kTolerance);
}

// plan_gltf.md GLTF-372. Every PBR shader already evaluates uAlphaTest and discards on it; nothing
// filled the vector in, so a MASK material was indistinguishable from an OPAQUE one at the only
// place the distinction exists. The fixture states the four numbers a renderer receives, so this
// compares against the manifest rather than against a second copy of the mapping.
TEST(GltfConformanceL6, AMaskMaterialsCutoffReachesTheDrawsAlphaTestVector)
{
    const LoadedFixture fixture("mat-alpha-mask-cutoff");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& material = FirstMaterial(fixture);
    const std::vector<double> expectedAlphaTest = Numbers(Member(material, "gpuAlphaTest"));
    ASSERT_EQ(4u, expectedAlphaTest.size()) << "the fixture must state the vector it expects";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-alpha-mask-cutoff");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    EXPECT_EQ("MASK", d.alphaMode);
    EXPECT_NEAR(static_cast<float>(NumberOr(material, "alphaCutoff", -1.0)), d.alphaCutoff,
                kTolerance);
    ExpectFlatNear(expectedAlphaTest, d.alphaTest.data(), d.alphaTest.size(), "alphaTest");

    // The cutoff is not the material's own alpha, and the fixture authors them apart precisely so
    // that a vector built from the wrong alpha quantity fails by value rather than by luck.
    const std::vector<double> baseColorFactor = Numbers(Member(material, "baseColorFactor"));
    ASSERT_EQ(4u, baseColorFactor.size());
    EXPECT_NEAR(static_cast<float>(baseColorFactor[3]), d.diffuseColor[3], kTolerance);
    EXPECT_NE(d.alphaTest[0], d.diffuseColor[3])
        << "the fixture has stopped discriminating: its cutoff and its alpha now coincide";

    // The discard side of the shader's own expression, evaluated here so the sign convention is
    // asserted as behaviour and not as four remembered constants. Below the cutoff must discard;
    // at and above it must not.
    const auto discards = [&d](float alpha) {
        const float weight = (d.alphaTest[1] > 0.0f)
            ? ((std::fabs(alpha - d.alphaTest[0]) < d.alphaTest[1]) ? d.alphaTest[2] : d.alphaTest[3])
            : ((alpha < d.alphaTest[0]) ? d.alphaTest[2] : d.alphaTest[3]);
        return weight < 0.0f;
    };
    const float cutoff = d.alphaTest[0];
    EXPECT_TRUE(discards(cutoff - 0.01f)) << "alpha below the cutoff must be discarded";
    EXPECT_FALSE(discards(cutoff)) << "alpha exactly at the cutoff must be kept (glTF §3.9.4)";
    EXPECT_FALSE(discards(1.0f)) << "an opaque fragment must never be discarded";
}

// The control for the row above, over the whole corpus: a material that is not MASK must bind the
// never-discard default. Without it, "the cutoff reaches the shader" would also be satisfied by an
// implementation that wrote a reference for every material and cut holes in every opaque surface.
TEST(GltfConformanceL6, OnlyAMaskMaterialWritesAnAlphaTestReference)
{
    int maskDraws = 0;
    int nonMaskDraws = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        for (const DrawParamsDump& d : CaptureDrawParamsEXT(
                 model, Matrix::getIdentityProperty(), TestView(), TestProjection()))
        {
            SCOPED_TRACE(ToJson(d));
            if (d.alphaMode == "MASK")
            {
                ++maskDraws;
                EXPECT_NEAR(d.alphaCutoff, d.alphaTest[0], kTolerance);
                EXPECT_LT(d.alphaTest[2], 0.0f) << "a mask must discard below its cutoff";
                EXPECT_GT(d.alphaTest[3], 0.0f) << "a mask must keep alpha at or above its cutoff";
                continue;
            }
            ++nonMaskDraws;
            EXPECT_NEAR(0.0f, d.alphaTest[0], kTolerance);
            EXPECT_NEAR(0.0f, d.alphaTest[1], kTolerance);
            EXPECT_GE(d.alphaTest[2], 0.0f) << "a non-mask draw must never discard";
            EXPECT_GE(d.alphaTest[3], 0.0f) << "a non-mask draw must never discard";
        }
    }
    EXPECT_GE(maskDraws, 1) << "no MASK draw was captured, so the positive half proved nothing";
    EXPECT_GE(nonMaskDraws, 10)
        << "too few non-mask draws to call this a control over the corpus";
}

// --- The topology the draw would actually issue ------------------------------------------------------

// GLTF-073 gave each part its own PrimitiveType; L6 is where that becomes a draw parameter rather
// than a field. The manifest's own importPolicy states which topology and how many primitives.
TEST(GltfConformanceL6, CapturedTopologyAndPrimitiveCountMatchTheImportPolicy)
{
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& primitives = Path(fixture.Expected(), "l3.primitives");
        if (primitives.type != JsonType::Array || primitives.arrayValue.size() != 1u) { continue; }
        const JsonValue& policy = Member(primitives.arrayValue.front(), "importPolicy");
        if (policy.type != JsonType::Object) { continue; }

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        if (captured.empty()) { continue; }
        const DrawParamsDump& d = captured.front();
        SCOPED_TRACE(ToJson(d));

        // glTF mode name -> the XNA PrimitiveType a conforming draw issues (plan_gltf.md §10.1).
        const std::string topology = StringOr(policy, "topologyName", "");
        std::string expectedType;
        if (topology == "TRIANGLES")        { expectedType = "TriangleList"; }
        else if (topology == "TRIANGLE_STRIP") { expectedType = "TriangleStrip"; }
        else if (topology == "LINES")       { expectedType = "LineList"; }
        else if (topology == "LINE_STRIP")  { expectedType = "LineStrip"; }
        else if (topology == "POINTS")      { expectedType = "PointListEXT"; }
        ASSERT_FALSE(expectedType.empty()) << "unmapped imported topology '" << topology << "'";

        EXPECT_EQ(expectedType, d.primitiveType);
        EXPECT_EQ(static_cast<int>(NumberOr(policy, "primitiveCount", -1)), d.primitiveCount);
    }
}

// --- plan_gltf.md GLTF-137: every mesh group is imported ------------------------------------------

// `CollectMeshGroups` makes one group per distinct skin plus one for the unskinned meshes, and the
// runtime loader took `groups.front()` -- so a file holding a character and a prop imported
// whichever owned the first mesh node and dropped the other in silence. `skin-plus-static-mesh`
// authors the skinned node first, so the static prop is what used to vanish. Asserted through the
// L6 capture because that is where "was it imported" and "would it actually be drawn" are the same
// question: a part with no effect, or with no primitives, never reaches the capture at all.
TEST(GltfConformanceL6, EveryMeshGroupOfAMixedFileIsImportedAndDrawable)
{
    const LoadedFixture fixture("skin-plus-static-mesh");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-plus-static-mesh");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(2u, captured.size())
        << "the file has two mesh groups and both must be drawable; a count of 1 is the "
           "groups.front() drop GLTF-137 removed";

    std::size_t skinnedParts = 0;
    std::size_t staticParts  = 0;
    for (const DrawParamsDump& d : captured)
    {
        SCOPED_TRACE(ToJson(d));
        if (d.skinned) { ++skinnedParts; } else { ++staticParts; }
    }
    // Both kinds, and each keeps its own nature: importing the prop by quietly giving it the
    // character's skinned effect would be a different bug wearing this one's clothes.
    EXPECT_EQ(1u, skinnedParts) << "the skinned mesh is missing or was imported twice";
    EXPECT_EQ(1u, staticParts)  << "the static mesh is missing, or was imported as skinned";

    // The static prop must arrive at the place its own node puts it. The manifest states that
    // placement independently at L4, so this compares against the file rather than against a
    // second reading of the loader.
    const JsonValue& instances = Path(fixture.Expected(), "l4.instances");
    ASSERT_EQ(JsonType::Array, instances.type);
    bool checkedStaticPlacement = false;
    for (const JsonValue& instance : instances.arrayValue)
    {
        if (StringOr(instance, "nodeName", "") != "StaticMeshNode") { continue; }
        const std::vector<double> nodeWorld = Numbers(Member(instance, "worldMatrixColumnMajor"));
        ASSERT_EQ(16u, nodeWorld.size());
        for (const DrawParamsDump& d : captured)
        {
            if (d.skinned) { continue; }
            SCOPED_TRACE(ToJson(d));
            for (std::size_t i = 0; i < 16; ++i)
            {
                EXPECT_NEAR(static_cast<float>(nodeWorld[i]), d.worldColMajor[i], kTolerance)
                    << "static mesh world element " << i;
            }
            checkedStaticPlacement = true;
        }
    }
    EXPECT_TRUE(checkedStaticPlacement) << "the static instance is not in the L4 manifest";
}

// --- plan_gltf.md GLTF-222: KHR_materials_emissive_strength ----------------------------------------

// The extension exists because `emissiveFactor` is a [0,1] value and HDR-authored content wants
// more. A strength that is dropped, or applied and then clamped, destroys exactly that -- so the
// assertion is not merely "the emissive matches" but "the emissive is above 1 and matches".
TEST(GltfConformanceL6, EmissiveStrengthMultipliesTheFactorAndSurvivesAboveOne)
{
    const LoadedFixture fixture("mat-emissive-strength");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& material = FirstMaterial(fixture);
    ASSERT_EQ(JsonType::Object, material.type);

    const std::vector<double> authored = Numbers(Member(material, "emissiveFactor"));
    const std::vector<double> product  = Numbers(Member(material, "emissiveFactorTimesStrength"));
    const double strength = NumberOr(material, "emissiveStrength", -1.0);
    ASSERT_EQ(3u, authored.size());
    ASSERT_EQ(3u, product.size());
    ASSERT_GT(strength, 1.0) << "the fixture no longer authors a strength that changes anything";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-emissive-strength");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    ASSERT_TRUE(d.pbr) << "a material carrying only factors must still select the PBR path";
    ExpectFlatNear(product, d.emissiveColor.data(), 3, "emissiveFactor * strength");

    // The two failure shapes named separately, so a failure says which one happened.
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_GT(std::fabs(d.emissiveColor[i] - static_cast<float>(authored[i])), 1e-4f)
            << "channel " << i << ": the strength was not applied at all";
    }
    EXPECT_GT(d.emissiveColor[0], 1.0f)
        << "the emissive was clamped to [0,1] -- which is precisely what the extension exists to "
           "lift, so a clamp makes carrying it pointless";
}

// --- plan_gltf.md GLTF-152: ModelMeshPart counts and offsets ----------------------------------------

// The count half was replaced by GLTF-078's topology-aware helper and is asserted per topology by
// `CapturedTopologyAndPrimitiveCountMatchTheImportPolicy` above. The offsets are the other half and
// had no test at all: the glTF loader gives every part its own vertex and index buffer, so
// `VertexOffset` and `StartIndex` are 0 by construction. That is a real invariant rather than a
// coincidence -- the draw reads `[StartIndex, StartIndex + 3*PrimitiveCount)` against a buffer
// sized exactly for the part -- and a future change that starts packing several parts into one
// buffer must set them rather than inherit a silent 0.
TEST(GltfConformanceL6, EveryImportedPartOwnsItsBuffersSoItsOffsetsAreZero)
{
    std::size_t checkedParts = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        const auto& meshes = model.getMeshesProperty();
        for (int mi = 0; mi < meshes.getCountProperty(); ++mi)
        {
            const auto& parts = meshes[mi]->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                const auto* part = parts[pi];
                ASSERT_NE(nullptr, part);
                SCOPED_TRACE("mesh " + std::to_string(mi) + " part " + std::to_string(pi));
                EXPECT_EQ(0, part->getVertexOffsetProperty());
                EXPECT_EQ(0, part->getStartIndexProperty());
                // And the buffer really is the part's own: NumVertices must span all of it, which
                // is what makes a zero VertexOffset correct rather than merely untested.
                ASSERT_NE(nullptr, part->getVertexBufferProperty());
                EXPECT_EQ(part->getVertexBufferProperty()->getVertexCountProperty(),
                          part->getNumVerticesProperty());
                ++checkedParts;
            }
        }
    }
    EXPECT_GT(checkedParts, 0u) << "no part was checked";
}

// --- plan_gltf.md GLTF-210/GLTF-212: colour space reaches the renderer ------------------------------

// glTF §3.9.2 assigns colour spaces per map, and it does so unconditionally: baseColorTexture and
// emissiveTexture are sRGB-encoded, normalTexture / occlusionTexture / metallicRoughnessTexture are
// linear. Before this, CNA sampled all five raw and wrote the lit result without encoding -- a
// double error that partly cancels visually and is quantitatively wrong everywhere.
//
// Only the two decisions the file actually makes are carried. There is deliberately NO flag for the
// three linear maps: the specification leaves no choice there, and a flag would invent one.
TEST(GltfConformanceL6, EveryPbrDrawDeclaresItsTexturesColourSpace)
{
    std::size_t checked = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        for (const DrawParamsDump& d : CaptureDrawParamsEXT(
                 model, Matrix::getIdentityProperty(), TestView(), TestProjection()))
        {
            if (!d.pbr) { continue; }
            SCOPED_TRACE(ToJson(d));
            EXPECT_TRUE(d.baseColorTextureIsSrgb)
                << "the base-colour texture is declared linear -- glTF §3.9.2 says sRGB";
            EXPECT_TRUE(d.emissiveTextureIsSrgb)
                << "the emissive texture is declared linear -- glTF §3.9.2 says sRGB";
            EXPECT_TRUE(d.encodeOutputToSrgb)
                << "the lit result reaches the framebuffer unencoded";
            ++checked;
        }
    }
    EXPECT_GT(checked, 0u) << "no PBR draw exercised the colour-space contract";
}

// The three are separate decisions and must stay separately settable: an application drawing into
// an sRGB render target has to turn the output encode off without also claiming its textures are
// linear. A single "colour management on/off" flag would make that impossible to express.
TEST(GltfConformanceL6, TheThreeColourSpaceDecisionsAreIndependent)
{
    using Microsoft::Xna::Framework::Graphics::PbrEffect;

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");

    auto* effect = dynamic_cast<PbrEffect*>(
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(nullptr, effect);

    effect->setEncodeOutputToSrgbEXTProperty(false);
    std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    EXPECT_FALSE(captured.front().encodeOutputToSrgb);
    EXPECT_TRUE(captured.front().baseColorTextureIsSrgb) << "turning off the encode changed a decode";
    EXPECT_TRUE(captured.front().emissiveTextureIsSrgb);

    effect->setEncodeOutputToSrgbEXTProperty(true);
    effect->setBaseColorTextureIsSrgbEXTProperty(false);
    captured = CaptureDrawParamsEXT(model, Matrix::getIdentityProperty(), TestView(),
                                    TestProjection());
    ASSERT_EQ(1u, captured.size());
    EXPECT_FALSE(captured.front().baseColorTextureIsSrgb);
    EXPECT_TRUE(captured.front().emissiveTextureIsSrgb) << "the two decodes are not independent";
    EXPECT_TRUE(captured.front().encodeOutputToSrgb);
}

// --- plan_gltf.md GLTF-224 / GLTF-225: normalTexture.scale and occlusionTexture.strength ----------

// Neither was ever read, so a material that dialled its normal map down to a subtle 0.35 got the
// full-strength 1.0 instead -- not a subtle difference. The fixture authors both away from 1 (the
// value both the spec default and CNA's fallback use) and away from each other, so "dropped" and
// "swapped" are different failures.
TEST(GltfConformanceL6, NormalScaleAndOcclusionStrengthReachTheBoundEffect)
{
    const LoadedFixture fixture("mat-normal-occlusion-scale");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const JsonValue& material = FirstMaterial(fixture);
    ASSERT_EQ(JsonType::Object, material.type);

    const double expectedNormalScale = NumberOr(material, "normalScale", -1.0);
    const double expectedOcclusion   = NumberOr(material, "occlusionStrength", -1.0);
    ASSERT_GT(expectedNormalScale, 0.0);
    ASSERT_NE(expectedNormalScale, expectedOcclusion) << "the fixture cannot detect a swap";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-normal-occlusion-scale");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    EXPECT_NEAR(static_cast<float>(expectedNormalScale), d.normalScale, kTolerance);
    EXPECT_NEAR(static_cast<float>(expectedOcclusion), d.occlusionStrength, kTolerance);
    // Named separately, because "arrived as 1" is the exact failure both had before and a
    // near-miss comparison alone would not say which of the two ways it failed.
    EXPECT_NE(1.0f, d.normalScale) << "normalTexture.scale arrived as the default -- it was dropped";
    EXPECT_NE(1.0f, d.occlusionStrength)
        << "occlusionTexture.strength arrived as the default -- it was dropped";
}

// Every other corpus material declares neither, and glTF's default for both is 1. A capture that
// invented some other value there would break content that never asked for the feature.
TEST(GltfConformanceL6, AMaterialDeclaringNeitherScalarGetsGltfsOwnDefaultOfOne)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    EXPECT_NEAR(1.0f, captured.front().normalScale, kTolerance);
    EXPECT_NEAR(1.0f, captured.front().occlusionStrength, kTolerance);
}

// --- plan_gltf.md GLTF-376: the light parameters the shader actually receives -------------------
//
// GLTF-325 locks what `ExtractPunctualLightsEXT` produces; this locks what survives all the way to
// the parameter block a renderer binds. The two are different claims: a light can be extracted
// correctly and then never applied, or applied to the wrong effect, and nothing on the import side
// would notice.
//
// The shape being pinned is XNA's, not glTF's: three directional lights, and a **disabled** one
// expressed as a zero diffuse colour rather than by a flag -- the shaders add all three
// unconditionally, so zero is what makes the addition a no-op. A renderer that instead expected a
// count, or an enable bit, would read a fully-lit third light out of every model that has one or
// two.

TEST(GltfConformanceL6, EveryLitDrawCarriesThreeLightsWithDisabledOnesAtZeroColour)
{
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);
        if (model.getMeshesProperty().getCountProperty() == 0) { continue; }

        const std::vector<DrawParamsDump> draws = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
            Matrix::getIdentityProperty());

        for (const DrawParamsDump& draw : draws)
        {
            SCOPED_TRACE("mesh " + std::to_string(draw.meshIndex) + " part " +
                         std::to_string(draw.partIndex));
            for (std::size_t l = 0; l < 3; ++l)
            {
                SCOPED_TRACE("light " + std::to_string(l));
                // Every channel is a real number in the unit range. A NaN or a negative colour
                // here subtracts light, which renders as a surface darker than black wherever the
                // term lands -- and negative light is not something any later stage checks for.
                for (std::size_t c = 0; c < 3; ++c)
                {
                    EXPECT_TRUE(std::isfinite(draw.lightDiffuseColors[l][c]))
                        << "diffuse channel " << c << " is not finite";
                    EXPECT_GE(draw.lightDiffuseColors[l][c], 0.0f)
                        << "a negative diffuse channel subtracts light";
                    EXPECT_LE(draw.lightDiffuseColors[l][c], 1.0f)
                        << "an out-of-gamut diffuse channel -- GLTF-326 clamps at import, so "
                           "anything above 1 here arrived after that clamp";
                    EXPECT_TRUE(std::isfinite(draw.lightDirections[l][c]))
                        << "direction component " << c << " is not finite";
                }

                // A light's direction is used whether or not the light contributes, so it must be
                // a usable unit vector in both cases -- a zero-length direction normalises to NaN
                // in any shader that normalises it defensively.
                const float length = std::sqrt(
                    draw.lightDirections[l][0] * draw.lightDirections[l][0] +
                    draw.lightDirections[l][1] * draw.lightDirections[l][1] +
                    draw.lightDirections[l][2] * draw.lightDirections[l][2]);
                EXPECT_NEAR(1.0f, length, 1e-4f)
                    << "the direction is not unit length (" << length << ")";
            }
        }
    }
}

TEST(GltfConformanceL6, ALitDrawWithNoFileLightsStillReceivesAUsableDefault)
{
    // The property that makes the whole light path safe by construction: a glTF file need not
    // declare any lights at all -- most do not -- and a lit effect drawn with three zero-coloured
    // lights renders black. CNA supplies a default rather than leaving the model unlit, and this
    // asserts the result is a light that actually contributes rather than a filled-in structure
    // that happens to be present.
    const std::string id = "mat-factor-only-gold";
    const LoadedFixture fixture(id);
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().lights_count))
        << "this fixture must declare no lights, or it tests the wrong thing";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>(id);
    if (model.getMeshesProperty().getCountProperty() == 0) { GTEST_SKIP() << "no drawable mesh"; }

    const std::vector<DrawParamsDump> draws = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
        Matrix::getIdentityProperty());
    ASSERT_FALSE(draws.empty());

    for (const DrawParamsDump& draw : draws)
    {
        if (!draw.lightingEnabled) { continue; }
        float brightest = 0.0f;
        for (std::size_t l = 0; l < 3; ++l)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                brightest = std::max(brightest, draw.lightDiffuseColors[l][c]);
            }
        }
        EXPECT_GT(brightest, 0.0f)
            << "a lit draw received three zero-coloured lights, so the model renders black -- a "
               "file that declares no lights is the common case, not an edge one";
    }
}

// --- GLTF-208: the file's own sampler reaches the drawn part --------------------------------------

TEST(GltfConformanceL6, ADeclaredSamplerSurvivesImportOntoThePartThatDrawsWithIt)
{
    // GLTF-202/GLTF-203 mapped glTF's sampler enums to XNA's, and GltfSamplerMappingTests asserts
    // that mapping exhaustively over raw enum values. What none of that could show is whether a
    // real file's sampler arrives on the part -- there was no textured fixture in the corpus until
    // GLTF-190. This is that assertion, at the layer a draw reads it from.
    //
    // `tex-reference-checkerboard` declares NEAREST filtering and CLAMP_TO_EDGE on both axes,
    // which are three separate departures from the LinearWrap every imported texture used to get
    // by inheriting whatever the device had.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("tex-reference-checkerboard");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    ASSERT_FALSE(d.samplers.empty()) << "the part carries no sampler state at all";
    const int expectedFilter =
        static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureFilter::Point);
    const int expectedAddress =
        static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Clamp);
    EXPECT_EQ(expectedFilter, d.samplers.front().filter)
        << "NEAREST did not arrive as Point -- the texture draws smoothed";
    EXPECT_EQ(expectedAddress, d.samplers.front().addressU)
        << "CLAMP_TO_EDGE did not arrive on U; UVs outside [0,1] tile instead of clamping";
    EXPECT_EQ(expectedAddress, d.samplers.front().addressV);
}

TEST(GltfConformanceL6, SamplersStayPerSlotWhenTwoTexturesShareOneImage)
{
    // An image cache is allowed (and desirable), but sampler state belongs to the texture object,
    // not the image. This fixture references one cgltf_image from two cgltf_texture objects and
    // deliberately gives the base-colour and normal slots different filters and address modes.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("texture-shared-two-samplers");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    const auto slot = [](TextureSlotEXT value) { return static_cast<std::size_t>(value); };
    ASSERT_GT(d.samplers.size(), slot(TextureSlotEXT::Normal));
    const auto& base = d.samplers[slot(TextureSlotEXT::BaseColor)];
    const auto& normal = d.samplers[slot(TextureSlotEXT::Normal)];
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureFilter::Point),
              base.filter);
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Clamp),
              base.addressU);
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Clamp),
              base.addressV);
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureFilter::Linear),
              normal.filter);
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Mirror),
              normal.addressU);
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap),
              normal.addressV);
}

TEST(GltfConformanceL6, APartWithNoDeclaredSamplerKeepsTheDefaultRatherThanTheLastFilesOne)
{
    // The control, and the one that catches a sampler leaking between parts: every corpus fixture
    // without a texture must still report XNA's own LinearWrap. A per-part array that was really
    // shared state would show the previous fixture's Point/Clamp here.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-identity");

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    ASSERT_FALSE(captured.front().samplers.empty());
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureFilter::Linear),
              captured.front().samplers.front().filter);
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap),
              captured.front().samplers.front().addressU);
}

// --- plan_gltf.md GLTF-365: the capture is effect-agnostic ------------------------------------------

// The L6 rung is only an oracle if it measures whatever effect a part happens to carry. It reads
// parameters through `Effect::FillGpuDrawParams` -- the same virtual call the draw path makes -- so
// being effect-agnostic is a property of the design; this asserts it as a fact, on all five stock
// effects, because a capture that silently produced an empty record for an unfamiliar effect would
// turn every later "asserted at L6" claim into a claim about PbrEffect alone.
//
// Two of the five are unreachable by import (glTF has no dual-texture material, and a skinned glTF
// primitive selects SkinnedPbrEffect or SkinnedEffect by its material model), so the test binds
// them onto a real imported part rather than pretending a fixture could produce one.
TEST(GltfDrawParamsOracleL6, EveryStockEffectIsCapturableWithItsOwnDistinguishingParameter)
{
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
    using Microsoft::Xna::Framework::Graphics::SkinnedEffect;

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);

    // A PBR draw and a skinned PBR draw, straight out of the corpus.
    {
        Model model = cm.Load<Model>("mat-factor-only-gold");
        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        ASSERT_EQ(1u, captured.size());
        SCOPED_TRACE(ToJson(captured.front()));
        EXPECT_EQ("Microsoft.Xna.Framework.Graphics.PbrEffect", captured.front().effectTypeName);
        EXPECT_TRUE(captured.front().pbr);
        EXPECT_FALSE(captured.front().skinned);
    }
    {
        Model model = cm.Load<Model>("skin-mesh-node-transform");
        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        ASSERT_FALSE(captured.empty());
        SCOPED_TRACE(ToJson(captured.front()));
        EXPECT_EQ("Microsoft.Xna.Framework.Graphics.SkinnedPbrEffect",
                  captured.front().effectTypeName);
        EXPECT_TRUE(captured.front().pbr);
        EXPECT_TRUE(captured.front().skinned);
    }

    // The remaining three, bound onto an imported part. The part keeps its own buffers and
    // primitive count, so what changes between captures is the effect and nothing else.
    Model model = cm.Load<Model>("xf-identity");
    ModelMeshPart* part = model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    ASSERT_NE(nullptr, part);

    BasicEffect basic(gd);
    basic.setLightingEnabledProperty(true);
    basic.setDiffuseColorProperty({0.25f, 0.5f, 0.75f});
    DualTextureEffect dual(gd);
    SkinnedEffect skinned(gd);
    skinned.setWeightsPerVertexProperty(2);

    struct Case
    {
        Effect* effect;
        std::string typeName;
        bool expectDualTexture;
        bool expectSkinned;
    };
    const std::vector<Case> cases{
        {&basic,   "Microsoft.Xna.Framework.Graphics.BasicEffect",       false, false},
        {&dual,    "Microsoft.Xna.Framework.Graphics.DualTextureEffect", true,  false},
        {&skinned, "Microsoft.Xna.Framework.Graphics.SkinnedEffect",     false, true},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.typeName);
        part->setEffectProperty(c.effect);
        const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
            model, Matrix::getIdentityProperty(), TestView(), TestProjection());
        ASSERT_EQ(1u, captured.size()) << "the capture skipped a part it would have drawn";
        const DrawParamsDump& d = captured.front();
        SCOPED_TRACE(ToJson(d));

        EXPECT_EQ(c.typeName, d.effectTypeName);
        EXPECT_EQ(c.expectDualTexture, d.dualTexture);
        EXPECT_EQ(c.expectSkinned, d.skinned);
        EXPECT_FALSE(d.pbr) << "only the two PBR effects select the metallic-roughness shader";
        // The matrices reach a non-PBR effect too -- the capture binds through IEffectMatrices,
        // which every stock effect implements, not through any one effect's own setter.
        ExpectFlatNear(std::vector<double>(d.world.begin(), d.world.end()), d.worldColMajor.data(),
                       d.worldColMajor.size(), "world -> worldColMajor");
        EXPECT_EQ("TriangleList", d.primitiveType);
        EXPECT_GT(d.primitiveCount, 0);
    }
    EXPECT_NEAR(0.25f, [&] {
        part->setEffectProperty(&basic);
        return CaptureDrawParamsEXT(model, Matrix::getIdentityProperty(), TestView(),
                                    TestProjection()).front().diffuseColor[0];
    }(), kTolerance) << "BasicEffect's own DiffuseColor did not reach the captured block";

    // Leave the model holding an effect it does not own for no longer than the test needs it.
    part->setEffectProperty(nullptr);
}

// --- plan_gltf.md GLTF-377: fog is state no glTF file can ask for -----------------------------------

// The PBR fragment program carries a fog term, because CNA's effects are XNA's and an XNA
// application may turn fog on. glTF has no fog at all, so an imported draw that arrived with it
// enabled would be shading no file asked for -- and, unlike a wrong factor, it would look
// plausible. Asserted over every loadable fixture, on both the flag and the vector, because they
// can disagree: a renderer that ignored the flag would still fog on a non-zero vector.
TEST(GltfConformanceL6, NoImportedDrawArrivesWithFogEnabled)
{
    std::size_t checked = 0;
    for (const std::string& id : LoadableFixtureIds())
    {
        SCOPED_TRACE(id);
        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        for (const DrawParamsDump& d : CaptureDrawParamsEXT(
                 model, Matrix::getIdentityProperty(), TestView(), TestProjection()))
        {
            SCOPED_TRACE(ToJson(d));
            EXPECT_FALSE(d.fogEnabled) << "an imported draw turned fog on";
            // All-zero is the true no-op: dot(position, 0) is 0, the fog factor saturates to 0 and
            // the renderers' keep = 1 - factor leaves the fragment exactly as the BRDF left it.
            for (std::size_t i = 0; i < d.fogVector.size(); ++i)
            {
                EXPECT_NEAR(0.0f, d.fogVector[i], kTolerance) << "fogVector[" << i << "]";
            }
            ++checked;
        }
    }
    EXPECT_GT(checked, 20u) << "too few draws captured to call this a sweep";
}

// The control for the row above. "Fog is off" would also be satisfied by a capture that could not
// see fog at all, or by an effect whose fog switch does nothing -- and either would make the sweep
// a tautology. Turning fog on by hand, the way an XNA application would, must move both quantities.
TEST(GltfDrawParamsOracleL6, TheFogCaptureCanActuallySeeFog)
{
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");

    auto* effect = dynamic_cast<Microsoft::Xna::Framework::Graphics::PbrEffect*>(
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(nullptr, effect);
    effect->setFogEnabledProperty(true);
    effect->setFogColorProperty({0.1f, 0.2f, 0.3f});
    effect->setFogStartProperty(1.0f);
    effect->setFogEndProperty(20.0f);

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_EQ(1u, captured.size());
    const DrawParamsDump& d = captured.front();
    SCOPED_TRACE(ToJson(d));

    EXPECT_TRUE(d.fogEnabled);
    ExpectFlatNear({0.1, 0.2, 0.3}, d.fogColor.data(), d.fogColor.size(), "fogColor");
    const float magnitude = std::fabs(d.fogVector[0]) + std::fabs(d.fogVector[1]) +
                            std::fabs(d.fogVector[2]) + std::fabs(d.fogVector[3]);
    EXPECT_GT(magnitude, 0.0f) << "an enabled fog produced an all-zero vector, which is a no-op";
}
