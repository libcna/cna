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

#include "GltfDrawParamsOracleEXT.hpp"
#include "GltfFixtureCorpus.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"

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
            Matrix world;
            float* w = &world.M11;
            for (std::size_t i = 0; i < 16; ++i) { w[i] = d.worldColMajor[i]; }
            // worldColMajor is column-major; XNA's Matrix is row-major, so the array above is the
            // transpose of the bound world. Transposing it back gives the matrix to invert.
            world = Matrix::Transpose(world);

            const ColumnMajor3x3 expected = ExpectedNormalMatrix(world);
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                EXPECT_NEAR(expected[i], d.normalMatrix[i], kTolerance) << "normal matrix " << i;
            }
        }
    }
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
        ExpectFlatNear(Numbers(Member(material, "emissiveFactor")), d.emissiveColor.data(), 3,
                       "emissiveFactor -> emissiveColor");
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
            EXPECT_TRUE(d.skinned) << "a skinned model bound an unskinned shader variant";
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

// The palette test above would be worthless if a model that never received a palette happened to
// bind the same numbers. It does not: an untouched skinned effect binds XNA's own default, which
// SkinnedEffect has always been MaxBones identity matrices, not the fixture's joint transforms.
// Measuring that default is also the honest answer to "what does an application that forgets
// SetBoneTransforms actually draw?" -- every joint matrix is the identity, so the mesh renders in
// joint space rather than in its bind pose.
TEST(GltfConformanceL6, AnUntouchedSkinnedEffectBindsTheIdentityPaletteNotTheFixturesOwn)
{
    using Microsoft::Xna::Framework::Graphics::AnimationPlayer;
    using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
    using Microsoft::Xna::Framework::Graphics::SkinningData;

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-mesh-node-transform");
    auto* skinningData = dynamic_cast<SkinningData*>(model.getTagProperty());
    ASSERT_NE(nullptr, skinningData);

    const std::vector<DrawParamsDump> captured = CaptureDrawParamsEXT(
        model, Matrix::getIdentityProperty(), TestView(), TestProjection());
    ASSERT_FALSE(captured.empty());
    for (const DrawParamsDump& d : captured)
    {
        SCOPED_TRACE(ToJson(d));
        EXPECT_TRUE(d.skinned);
        EXPECT_EQ(SkinnedPbrEffect::MaxBones, d.boneCount);
        for (const ColumnMajor4x4& bone : d.boneTransforms)
        {
            float identity[16];
            Matrix::getIdentityProperty().ToColumnMajor(identity);
            for (std::size_t i = 0; i < 16; ++i)
            {
                EXPECT_NEAR(identity[i], bone[i], kTolerance);
            }
        }
    }

    // And the fixture's real palette is nothing like that default, so the populated-palette test
    // above cannot be passing on a coincidence.
    AnimationPlayer player(*skinningData);
    player.Update(System::TimeSpan::Zero, false, false);
    const std::vector<Matrix>& palette = player.GetSkinTransforms();
    ASSERT_FALSE(palette.empty());
    bool anyNonIdentity = false;
    for (const Matrix& m : palette)
    {
        if (m != Matrix::getIdentityProperty()) { anyNonIdentity = true; break; }
    }
    EXPECT_TRUE(anyNonIdentity)
        << "the skin fixture's palette is all-identity, so it cannot distinguish a bound palette "
           "from an unbound one";
    EXPECT_NE(static_cast<std::size_t>(SkinnedPbrEffect::MaxBones), palette.size());
}

// --- GLTF-230's L6 half: what "carried, not applied" looks like as a number -------------------------

// docs/gltf-api-change-review.md §1.3/§1.4 decided that alphaMode, alphaCutoff and doubleSided
// would be CARRIED by the effect and not yet APPLIED to any device state. That is a real boundary,
// and this test measures it rather than describing it: a BLEND material's alphaTest block is still
// the never-discard default. When GLTF-230 wires it, this test fails -- which is the point. It is
// the tripwire that stops the boundary from being crossed silently in either direction.
TEST(GltfConformanceL6, AlphaStateIsCarriedOnTheEffectButNotYetInTheParameterBlock)
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

    // Not applied: GpuDrawParams::alphaTest is untouched from its {0,0,1,1} never-discard default.
    EXPECT_NEAR(0.0f, d.alphaTest[0], kTolerance);
    EXPECT_NEAR(0.0f, d.alphaTest[1], kTolerance);
    EXPECT_NEAR(1.0f, d.alphaTest[2], kTolerance);
    EXPECT_NEAR(1.0f, d.alphaTest[3], kTolerance);
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
