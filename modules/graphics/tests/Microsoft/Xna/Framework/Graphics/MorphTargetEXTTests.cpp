// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-62/63 (Phase 13B): engine-level tests for BlendMorphTargetsEXT/
// SetMorphWeightsEXT/EvaluateMorphWeightsEXT with hand-crafted synthetic data (no glTF involved --
// see RuntimeGltfModelTests.cpp for the glTF-extraction integration tests). Proves the blend math
// itself: weight=0 reproduces the base pose exactly, weight=1 applies the full delta, two targets
// combine additively, and a blended normal gets renormalized. BlendMorphTargetsEXT (the pure
// computation) is tested directly rather than via VertexBuffer::GetData(), since GetData reads
// back the typed-SetData CPU shadow buffer -- SetMorphWeightsEXT re-uploads via SetDataRaw, which
// (like the ModelTypeReader stride-52/56 paths it mirrors) never populates that shadow.

#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/TimeSpan.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // A single stride-32 (Position+Normal+TextureCoordinate) triangle, base pose at the origin
    // plane with a +Z normal, used as the base pose for every test below.
    std::vector<std::uint8_t> BuildBaseTriangleBytes()
    {
        const float verts[3][8] = {
            { 0, 0, 0,  0, 0, 1,  0, 0 },
            { 1, 0, 0,  0, 0, 1,  1, 0 },
            { 0, 1, 0,  0, 0, 1,  0, 1 },
        };
        std::vector<std::uint8_t> bytes(32 * 3);
        for (int i = 0; i < 3; ++i)
        {
            std::memcpy(bytes.data() + i * 32, verts[i], 32);
        }
        return bytes;
    }

    Vector3 ReadPosition(const std::vector<std::uint8_t>& bytes, int vertex)
    {
        float p[3];
        std::memcpy(p, bytes.data() + static_cast<std::size_t>(vertex) * 32, 12);
        return Vector3(p[0], p[1], p[2]);
    }

    Vector3 ReadNormal(const std::vector<std::uint8_t>& bytes, int vertex)
    {
        float n[3];
        std::memcpy(n, bytes.data() + static_cast<std::size_t>(vertex) * 32 + 12, 12);
        return Vector3(n[0], n[1], n[2]);
    }

    MorphTargetDataEXT BuildTwoTargetMorphData()
    {
        MorphTargetDataEXT morph;
        morph.BaseVertexBytes = BuildBaseTriangleBytes();
        morph.Stride = 32;
        // Target 0: moves every vertex by +1 in Z, no normal delta.
        morph.PositionDeltas.push_back({ Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1) });
        morph.NormalDeltas.emplace_back(); // empty -- no normal delta for target 0
        // Target 1: moves vertex 0 by +2 in X and tilts its normal toward +X (no delta for 1/2).
        morph.PositionDeltas.push_back({ Vector3(2, 0, 0), Vector3(0, 0, 0), Vector3(0, 0, 0) });
        morph.NormalDeltas.push_back({ Vector3(1, 0, 0), Vector3(0, 0, 0), Vector3(0, 0, 0) });
        return morph;
    }
}

TEST(BlendMorphTargetsEXTTest, ZeroWeightsReproduceTheBasePoseExactly)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    const auto blended = BlendMorphTargetsEXT(morph, {0.0f, 0.0f});
    EXPECT_EQ(ReadPosition(blended, 0), Vector3(0, 0, 0));
    EXPECT_EQ(ReadPosition(blended, 1), Vector3(1, 0, 0));
    EXPECT_EQ(ReadPosition(blended, 2), Vector3(0, 1, 0));
}

TEST(BlendMorphTargetsEXTTest, FullWeightOnTargetZeroAppliesTheFullPositionDelta)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    const auto blended = BlendMorphTargetsEXT(morph, {1.0f, 0.0f});
    EXPECT_FLOAT_EQ(ReadPosition(blended, 0).Z, 1.0f);
    EXPECT_FLOAT_EQ(ReadPosition(blended, 1).Z, 1.0f);
    EXPECT_FLOAT_EQ(ReadPosition(blended, 2).Z, 1.0f);
    // Target 0 has no normal delta -- normal must stay exactly the base (0,0,1).
    const Vector3 n0 = ReadNormal(blended, 0);
    EXPECT_FLOAT_EQ(n0.Z, 1.0f);
    EXPECT_FLOAT_EQ(n0.X, 0.0f);
}

TEST(BlendMorphTargetsEXTTest, HalfWeightAppliesHalfTheDelta)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    const auto blended = BlendMorphTargetsEXT(morph, {0.5f, 0.0f});
    EXPECT_NEAR(ReadPosition(blended, 0).Z, 0.5f, 1e-6f);
}

TEST(BlendMorphTargetsEXTTest, TwoTargetsCombineAdditively)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    const auto blended = BlendMorphTargetsEXT(morph, {1.0f, 1.0f});
    // Vertex 0 gets both target 0's +Z and target 1's +2X.
    EXPECT_FLOAT_EQ(ReadPosition(blended, 0).X, 2.0f);
    EXPECT_FLOAT_EQ(ReadPosition(blended, 0).Z, 1.0f);
    // Vertices 1/2 only get target 0's +Z (target 1's delta is zero for them).
    EXPECT_FLOAT_EQ(ReadPosition(blended, 1).X, 1.0f);
    EXPECT_FLOAT_EQ(ReadPosition(blended, 1).Z, 1.0f);
}

TEST(BlendMorphTargetsEXTTest, BlendedNormalIsRenormalizedToUnitLength)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    const auto blended = BlendMorphTargetsEXT(morph, {0.0f, 1.0f});
    // Base (0,0,1) + delta (1,0,0) = (1,0,1), length sqrt(2) -- must come back unit length.
    const Vector3 n0 = ReadNormal(blended, 0);
    const float len = std::sqrt(n0.X * n0.X + n0.Y * n0.Y + n0.Z * n0.Z);
    EXPECT_NEAR(len, 1.0f, 1e-5f);
    EXPECT_NEAR(n0.X, 1.0f / std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(n0.Z, 1.0f / std::sqrt(2.0f), 1e-5f);
}

TEST(BlendMorphTargetsEXTTest, TextureCoordinateIsUnaffectedByBlending)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    const auto blended = BlendMorphTargetsEXT(morph, {1.0f, 1.0f});
    float uv[2];
    std::memcpy(uv, blended.data() + 1 * 32 + 24, 8);
    EXPECT_FLOAT_EQ(uv[0], 1.0f);
    EXPECT_FLOAT_EQ(uv[1], 0.0f);
}

TEST(BlendMorphTargetsEXTTest, WrongWeightCountThrows)
{
    const MorphTargetDataEXT morph = BuildTwoTargetMorphData();
    EXPECT_THROW(BlendMorphTargetsEXT(morph, {1.0f}), std::runtime_error);
    EXPECT_THROW(BlendMorphTargetsEXT(morph, {1.0f, 1.0f, 1.0f}), std::runtime_error);
}

TEST(SetMorphWeightsEXTTest, ReuploadsTheVertexBufferAndUpdatesStoredWeights)
{
    GraphicsDevice device;
    // VertexBuffer/ModelMeshPart are inherently 3D-pipeline concepts -- a renderer that honestly
    // reports no 3D pipeline has nothing to reupload.
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    VertexBuffer vb(device, 3);
    const auto baseBytes = BuildBaseTriangleBytes();
    vb.SetDataRaw(baseBytes.data(), 3, 32);
    IndexBuffer ib(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::uint16_t indices[3] = {0, 1, 2};
    ib.SetData(indices, 3);
    ModelMeshPart part(&vb, &ib, 3, 1, 0, 0);

    auto morph = std::make_unique<MorphTargetDataEXT>(BuildTwoTargetMorphData());
    part.setTagProperty(morph.get());

    SetMorphWeightsEXT(part, {1.0f, 0.0f});
    EXPECT_FLOAT_EQ(morph->Weights[0], 1.0f);
    EXPECT_FLOAT_EQ(morph->Weights[1], 0.0f);
}

TEST(SetMorphWeightsEXTTest, MissingTagThrows)
{
    GraphicsDevice device;
    // VertexBuffer/ModelMeshPart are inherently 3D-pipeline concepts -- a renderer that honestly
    // reports no 3D pipeline has no ModelMeshPart for SetMorphWeightsEXT to reject a missing tag
    // on.
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
    VertexBuffer vb(device, 1);
    IndexBuffer ib(device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    ModelMeshPart part(&vb, &ib, 1, 1, 0, 0);
    EXPECT_THROW(SetMorphWeightsEXT(part, {1.0f}), std::runtime_error);
}

TEST(EvaluateMorphWeightsEXTTest, EmptyTrackReturnsEmptyVector)
{
    MorphWeightTrackEXT track;
    EXPECT_TRUE(EvaluateMorphWeightsEXT(track, 0.5).empty());
}

TEST(EvaluateMorphWeightsEXTTest, LinearlyInterpolatesBetweenBracketingKeyframes)
{
    MorphWeightTrackEXT track;
    track.Keys.push_back({System::TimeSpan::FromSeconds(0.0), {0.0f, 1.0f}});
    track.Keys.push_back({System::TimeSpan::FromSeconds(1.0), {1.0f, 0.0f}});

    const auto mid = EvaluateMorphWeightsEXT(track, 0.5);
    ASSERT_EQ(mid.size(), 2u);
    EXPECT_NEAR(mid[0], 0.5f, 1e-6f);
    EXPECT_NEAR(mid[1], 0.5f, 1e-6f);

    const auto before = EvaluateMorphWeightsEXT(track, -1.0);
    EXPECT_NEAR(before[0], 0.0f, 1e-6f);
    const auto after = EvaluateMorphWeightsEXT(track, 5.0);
    EXPECT_NEAR(after[0], 1.0f, 1e-6f);
}

TEST(EvaluateMorphWeightsEXTTest, StepInterpolationHoldsTheLowerKeyframeValue)
{
    MorphWeightTrackEXT track;
    track.StepInterpolation = true;
    track.Keys.push_back({System::TimeSpan::FromSeconds(0.0), {0.0f}});
    track.Keys.push_back({System::TimeSpan::FromSeconds(1.0), {1.0f}});

    const auto mid = EvaluateMorphWeightsEXT(track, 0.75);
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_NEAR(mid[0], 0.0f, 1e-6f);
}

// CUBICSPLINE: both endpoint out/in-tangents zero -- the Hermite basis reduces to
// h00(s)*v0 + h01(s)*v1 (smoothstep-shaped, not linear). At s=0.25: h00=0.84375, h01=0.15625,
// so the interpolated value is 0.15625, not the linear 0.25 -- a genuine distinguishing case
// proving real Hermite evaluation runs, not a lerp.
TEST(EvaluateMorphWeightsEXTTest, CubicSplineEvaluatesRealHermiteCurveNotLinear)
{
    MorphWeightTrackEXT track;
    track.CubicSpline = true;
    MorphWeightKeyframeEXT k0;
    k0.Time = System::TimeSpan::FromSeconds(0.0);
    k0.Weights = {0.0f};
    k0.OutTangent = {0.0f};
    MorphWeightKeyframeEXT k1;
    k1.Time = System::TimeSpan::FromSeconds(1.0);
    k1.Weights = {1.0f};
    k1.InTangent = {0.0f};
    track.Keys.push_back(k0);
    track.Keys.push_back(k1);

    const auto quarter = EvaluateMorphWeightsEXT(track, 0.25);
    ASSERT_EQ(quarter.size(), 1u);
    EXPECT_NEAR(quarter[0], 0.15625f, 1e-5f);

    // Symmetric zero-tangent case: the midpoint is still exactly 0.5 (h00=h01=0.5 at s=0.5).
    const auto mid = EvaluateMorphWeightsEXT(track, 0.5);
    EXPECT_NEAR(mid[0], 0.5f, 1e-5f);

    const auto before = EvaluateMorphWeightsEXT(track, -1.0);
    EXPECT_NEAR(before[0], 0.0f, 1e-6f);
    const auto after = EvaluateMorphWeightsEXT(track, 5.0);
    EXPECT_NEAR(after[0], 1.0f, 1e-6f);
}

// A non-zero out-tangent at k0 pulls the curve above the endpoint-symmetric case near t=0 --
// distinguishes the tangent term (dt*h10*outTangent) from a tangent-less evaluation.
TEST(EvaluateMorphWeightsEXTTest, CubicSplineHonorsNonZeroTangents)
{
    MorphWeightTrackEXT track;
    track.CubicSpline = true;
    MorphWeightKeyframeEXT k0;
    k0.Time = System::TimeSpan::FromSeconds(0.0);
    k0.Weights = {0.0f};
    k0.OutTangent = {2.0f};
    MorphWeightKeyframeEXT k1;
    k1.Time = System::TimeSpan::FromSeconds(1.0);
    k1.Weights = {1.0f};
    k1.InTangent = {0.0f};
    track.Keys.push_back(k0);
    track.Keys.push_back(k1);

    // h10(0.25) = 0.25^3 - 2*0.25^2 + 0.25 = 0.015625 - 0.125 + 0.25 = 0.140625.
    // value = h00*0 + dt(=1)*h10*outTangent(=2) + h01*1 + dt*h11*0 = 0.84375*0 + 0.140625*2 + 0.15625
    //       = 0.28125 + 0.15625 = 0.4375.
    const auto quarter = EvaluateMorphWeightsEXT(track, 0.25);
    ASSERT_EQ(quarter.size(), 1u);
    EXPECT_NEAR(quarter[0], 0.4375f, 1e-5f);
}

// If either bracketing keyframe lacks tangent data (the non-CUBICSPLINE / plain LINEAR case),
// evaluation must fall back to plain LINEAR even when CubicSpline happens to be left true.
TEST(EvaluateMorphWeightsEXTTest, FallsBackToLinearWhenTangentsAreMissing)
{
    MorphWeightTrackEXT track;
    track.CubicSpline = true;
    track.Keys.push_back({System::TimeSpan::FromSeconds(0.0), {0.0f}});
    track.Keys.push_back({System::TimeSpan::FromSeconds(1.0), {1.0f}});

    const auto mid = EvaluateMorphWeightsEXT(track, 0.5);
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_NEAR(mid[0], 0.5f, 1e-6f);
}

// --- plans/plan_gltf.md GLTF-278: a normal delta must apply on EVERY stride with a Normal slot --------
//
// The stride carrying the Normal is not a property of the morph target; it is a property of the
// vertex layout the importer chose, and GLTF-215 changed that choice. A metallic-roughness
// material now lands on stride 48 (or 68 skinned) rather than 32, and both carry Normal at the
// same offset 12 -- so a normal delta must behave identically on all five strides that have the
// slot, and must be a no-op on the two that do not. Restating the ABI as a literal list is what
// let the two drift apart; these tests hold every entry of the real table to the same behaviour.

namespace
{
    /// The canonical layouts, by stride: byte offset of the Normal, or -1 when the stride has none.
    struct StrideNormalSlot
    {
        int stride;
        int normalOffset;
    };

    constexpr StrideNormalSlot kStrideNormalSlots[] = {
        {20, -1}, {24, -1}, {32, 12}, {48, 12}, {52, 12}, {56, 12}, {68, 12},
    };

    /// One vertex at the origin with a +Z normal, laid out for @p stride and zero elsewhere.
    std::vector<std::uint8_t> BuildOneVertexForStride(int stride, int normalOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stride), 0);
        const float position[3] = {0.0f, 0.0f, 0.0f};
        std::memcpy(bytes.data(), position, sizeof(position));
        if (normalOffset >= 0)
        {
            const float normal[3] = {0.0f, 0.0f, 1.0f};
            std::memcpy(bytes.data() + normalOffset, normal, sizeof(normal));
        }
        return bytes;
    }

    /// A single target that rotates the normal a quarter turn toward +X and shifts the position.
    MorphTargetDataEXT BuildSingleVertexMorphForStride(int stride, int normalOffset)
    {
        MorphTargetDataEXT morph;
        morph.Stride = stride;
        morph.BaseVertexBytes = BuildOneVertexForStride(stride, normalOffset);
        morph.PositionDeltas.push_back({Vector3(5, 0, 0)});
        morph.NormalDeltas.push_back({Vector3(1, 0, -1)});
        return morph;
    }
}

TEST(BlendMorphTargetsEXTTest, NormalDeltaAppliesOnEveryStrideThatHasANormalSlot)
{
    for (const StrideNormalSlot& slot : kStrideNormalSlots)
    {
        SCOPED_TRACE("stride " + std::to_string(slot.stride));
        const MorphTargetDataEXT morph =
            BuildSingleVertexMorphForStride(slot.stride, slot.normalOffset);
        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
        ASSERT_EQ(morph.BaseVertexBytes.size(), blended.size());

        // The position delta applies on every stride -- Position is at offset 0 in all of them.
        float position[3];
        std::memcpy(position, blended.data(), sizeof(position));
        EXPECT_FLOAT_EQ(5.0f, position[0]);

        if (slot.normalOffset < 0)
        {
            // Strides 20 and 24 have no Normal, so nothing may be written where one would be.
            // Every byte past the position must still be zero: a blend that "helpfully" wrote a
            // normal at offset 12 here would land on a TextureCoordinate or a Color.
            for (std::size_t i = 12; i < blended.size(); ++i)
            {
                EXPECT_EQ(0u, blended[i]) << "byte " << i << " was written on a normal-less stride";
            }
            continue;
        }

        // (0,0,1) + (1,0,-1) = (1,0,0), renormalized -- a full quarter turn onto +X.
        float normal[3];
        std::memcpy(normal, blended.data() + slot.normalOffset, sizeof(normal));
        EXPECT_NEAR(1.0f, normal[0], 1e-5f);
        EXPECT_NEAR(0.0f, normal[1], 1e-5f);
        EXPECT_NEAR(0.0f, normal[2], 1e-5f);
    }
}

// The regression in its own words: stride 48 is what an ordinary glTF metallic-roughness mesh gets
// after GLTF-215, and it used to be excluded, so the position moved and the normal did not.
TEST(BlendMorphTargetsEXTTest, ThePbrStridesAreNotExcludedFromNormalBlending)
{
    for (const int stride : {48, 68})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const MorphTargetDataEXT morph = BuildSingleVertexMorphForStride(stride, 12);
        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});

        float normal[3];
        std::memcpy(normal, blended.data() + 12, sizeof(normal));
        const bool unchanged = normal[0] == 0.0f && normal[1] == 0.0f && normal[2] == 1.0f;
        EXPECT_FALSE(unchanged)
            << "the base normal survived a full-weight normal delta -- GLTF-278 has regressed";
    }
}

// --- plans/plan_gltf.md GLTF-279: TANGENT deltas, with the handedness left alone ------------------------
//
// Morph tangent deltas were never extracted at all, so a morphed PBR surface kept its rest-pose
// tangent basis while its positions and normals moved -- normal mapping then lit the deformed
// surface with the undeformed basis. The subtlety is the fourth component: glTF morphs the tangent
// DIRECTION only, because `w` is the UV winding's handedness and interpolating it would pass
// through 0, which is not a handedness at all.

namespace
{
    /// One stride-48 vertex: Position(0), Normal(12), Tangent(24, vec4), TexCoord(40).
    std::vector<std::uint8_t> BuildStride48Vertex(float tangentW)
    {
        std::vector<std::uint8_t> bytes(48, 0);
        const float position[3] = {0.0f, 0.0f, 0.0f};
        const float normal[3]   = {0.0f, 0.0f, 1.0f};
        const float tangent[4]  = {1.0f, 0.0f, 0.0f, tangentW};
        std::memcpy(bytes.data(), position, sizeof(position));
        std::memcpy(bytes.data() + 12, normal, sizeof(normal));
        std::memcpy(bytes.data() + 24, tangent, sizeof(tangent));
        return bytes;
    }

    MorphTargetDataEXT BuildTangentMorph(float tangentW)
    {
        MorphTargetDataEXT morph;
        morph.Stride = 48;
        morph.BaseVertexBytes = BuildStride48Vertex(tangentW);
        morph.PositionDeltas.push_back({Vector3(0, 0, 0)});
        morph.NormalDeltas.emplace_back();
        // Rotates the tangent a quarter turn from +X toward +Y.
        morph.TangentDeltas.push_back({Vector3(-1, 1, 0)});
        return morph;
    }
}

TEST(BlendMorphTargetsEXTTest, TangentDeltaRotatesTheTangentDirection)
{
    const MorphTargetDataEXT morph = BuildTangentMorph(1.0f);
    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});

    float tangent[4];
    std::memcpy(tangent, blended.data() + 24, sizeof(tangent));
    // (1,0,0) + (-1,1,0) = (0,1,0), renormalized.
    EXPECT_NEAR(0.0f, tangent[0], 1e-5f);
    EXPECT_NEAR(1.0f, tangent[1], 1e-5f);
    EXPECT_NEAR(0.0f, tangent[2], 1e-5f);
}

// The handedness must survive untouched, in BOTH signs -- a blend that wrote a 4-component tangent
// would flip or zero it, and a mirrored UV island would then light inside out.
TEST(BlendMorphTargetsEXTTest, TangentHandednessIsNeverBlended)
{
    for (const float handedness : {1.0f, -1.0f})
    {
        SCOPED_TRACE("w = " + std::to_string(handedness));
        const MorphTargetDataEXT morph = BuildTangentMorph(handedness);
        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});

        float tangent[4];
        std::memcpy(tangent, blended.data() + 24, sizeof(tangent));
        EXPECT_FLOAT_EQ(handedness, tangent[3])
            << "the tangent's w was modified -- handedness is UV winding, not pose";
    }

    // And a half weight must not drag it toward zero either, which is what interpolating it would
    // do and is the reason it is stored as a Vector3 delta in the first place.
    const MorphTargetDataEXT morph = BuildTangentMorph(-1.0f);
    const std::vector<std::uint8_t> halfBlended = BlendMorphTargetsEXT(morph, {0.5f});
    float tangent[4];
    std::memcpy(tangent, halfBlended.data() + 24, sizeof(tangent));
    EXPECT_FLOAT_EQ(-1.0f, tangent[3]);
}

// A layout with no tangent slot must be left entirely alone, the same way the normal-less strides
// are: writing a tangent at offset 24 of a stride-32 vertex would land on its TextureCoordinate.
TEST(BlendMorphTargetsEXTTest, AStrideWithNoTangentSlotIsUntouchedByATangentDelta)
{
    MorphTargetDataEXT morph;
    morph.Stride = 32;
    morph.BaseVertexBytes = BuildBaseTriangleBytes();
    morph.PositionDeltas.push_back({Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(0, 0, 0)});
    morph.NormalDeltas.emplace_back();
    morph.TangentDeltas.push_back({Vector3(9, 9, 9), Vector3(9, 9, 9), Vector3(9, 9, 9)});

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    EXPECT_EQ(morph.BaseVertexBytes, blended)
        << "a tangent delta modified a layout that has no tangent";
}
