// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-177 / GLTF-275 / GLTF-276 / GLTF-277 / GLTF-283 / GLTF-284: how morph deltas
// are applied.
//
// A morph target is a per-vertex delta array, and `BlendMorphTargetsEXT` adds a weighted sum of
// those arrays onto the base vertex bytes. Every rule here is one where the wrong answer still
// produces a mesh -- a slightly-off mesh, or a correctly-shaped mesh lit wrongly -- which is why
// each needs an assertion rather than a look.
//
// These drive `BlendMorphTargetsEXT` directly rather than through a corpus asset, because what is
// under test is the blend arithmetic over a stated base buffer: an end-to-end fixture would prove
// the same sum while making it far harder to say which term was wrong.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr float kTolerance = 1e-5f;

    /// Stride 48 is the unskinned PBR layout: Position, Normal, Tangent, TextureCoordinate. It is
    /// the stride an ordinary glTF mesh lands on since GLTF-215, and it has both of the slots this
    /// file needs -- a normal and a tangent -- so one base buffer serves every case.
    constexpr int kStride = 48;

    int OffsetOf(VertexElementUsage usage)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                kStride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known);
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == 0)
            {
                return layout.elements[i].offset;
            }
        }
        return -1;
    }

    /// A `vertexCount`-vertex base buffer where vertex `v`'s position is `(v, 0, 0)`, its normal is
    /// `+Z` and its tangent is `(+X, w = -1)`. The handedness is deliberately NOT the default +1,
    /// so a blend that rewrites `w` instead of preserving it is visible.
    std::vector<std::uint8_t> BaseVertices(int vertexCount)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(vertexCount) * kStride, 0);
        const int positionOffset = OffsetOf(VertexElementUsage::Position);
        const int normalOffset = OffsetOf(VertexElementUsage::Normal);
        const int tangentOffset = OffsetOf(VertexElementUsage::Tangent);
        EXPECT_GE(normalOffset, 0);
        EXPECT_GE(tangentOffset, 0);

        for (int v = 0; v < vertexCount; ++v)
        {
            const std::size_t base = static_cast<std::size_t>(v) * kStride;
            const float position[3] = {static_cast<float>(v), 0.0f, 0.0f};
            const float normal[3] = {0.0f, 0.0f, 1.0f};
            const float tangent[4] = {1.0f, 0.0f, 0.0f, -1.0f};
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(positionOffset), position,
                        sizeof(position));
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(normalOffset), normal,
                        sizeof(normal));
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(tangentOffset), tangent,
                        sizeof(tangent));
        }
        return bytes;
    }

    std::vector<float> Read(const std::vector<std::uint8_t>& bytes, int vertex, int offset,
                            std::size_t count)
    {
        std::vector<float> out(count);
        std::memcpy(out.data(),
                    bytes.data() + static_cast<std::size_t>(vertex) * kStride +
                        static_cast<std::size_t>(offset),
                    count * sizeof(float));
        return out;
    }
}

// --- GLTF-275: POSITION deltas are a weighted sum -----------------------------------------------

TEST(GltfMorphBlending, PositionDeltasFromSeveralTargetsAccumulateRatherThanOverwrite)
{
    // Two targets pulling on the same vertex must both contribute. A blend that assigns rather than
    // accumulates gives the last target's delta alone, which for a face rig means one expression
    // silently cancelling every other -- and the mesh still looks like a face.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(2);
    morph.PositionDeltas = {
        {Vector3(10.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)},
        {Vector3(0.0f, 100.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)},
    };
    morph.NormalDeltas = {{}, {}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {0.5f, 0.25f});
    const std::vector<float> position = Read(blended, 0, OffsetOf(VertexElementUsage::Position), 3);
    EXPECT_NEAR(0.0f + 0.5f * 10.0f, position[0], kTolerance)
        << "target 0's contribution is missing or was overwritten";
    EXPECT_NEAR(0.25f * 100.0f, position[1], kTolerance)
        << "target 1's contribution is missing";
    EXPECT_NEAR(0.0f, position[2], kTolerance);
}

TEST(GltfMorphBlending, AZeroWeightedTargetContributesNothingAtAll)
{
    // The neutral pose has to be exactly the base mesh, not approximately it. A blend that runs the
    // arithmetic anyway accumulates float error on every vertex of every un-posed target, which for
    // a rig with fifty targets is a mesh that drifts while nothing is animating.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(2);
    morph.PositionDeltas = {{Vector3(1e7f, 1e7f, 1e7f), Vector3(1e7f, 1e7f, 1e7f)}};
    morph.NormalDeltas = {{}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {0.0f});
    EXPECT_EQ(morph.BaseVertexBytes, blended)
        << "a zero-weighted target changed the mesh -- the neutral pose must be byte-identical to "
           "the base, not merely close to it";
}

// --- GLTF-276: a target with no POSITION delta ---------------------------------------------------

TEST(GltfMorphBlending, ATargetWithNoPositionDeltaLeavesPositionsAlone)
{
    // §3.7.2.2 lets a target carry any subset of POSITION/NORMAL/TANGENT -- a target that only
    // moves normals is a legitimate way to author a shading-only variation. The absent stream must
    // read as zero and not as garbage, and CNA zero-fills it at extraction; this asserts the blend
    // honours that rather than reading past the end of a shorter array.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(2);
    morph.PositionDeltas = {{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)}};
    morph.NormalDeltas = {{Vector3(1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f)}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    for (int v = 0; v < 2; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> position =
            Read(blended, v, OffsetOf(VertexElementUsage::Position), 3);
        EXPECT_NEAR(static_cast<float>(v), position[0], kTolerance);
        EXPECT_NEAR(0.0f, position[1], kTolerance);
        EXPECT_NEAR(0.0f, position[2], kTolerance);
    }
}

// --- GLTF-277 / GLTF-283: NORMAL deltas, and the renormalisation afterwards ---------------------

TEST(GltfMorphBlending, ABlendedNormalIsRenormalisedBecauseASumOfUnitVectorsIsNotOne)
{
    // A weighted sum of unit normals is not itself unit length -- +Z plus +X has length sqrt(2), and
    // a 1.41-length normal brightens every lit surface it touches by 41%. The renormalisation is
    // what makes a morphed mesh light like an unmorphed one, and its absence looks like a lighting
    // bug rather than a morph one.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(0.0f, 0.0f, 0.0f)}};
    morph.NormalDeltas = {{Vector3(1.0f, 0.0f, 0.0f)}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const std::vector<float> normal = Read(blended, 0, OffsetOf(VertexElementUsage::Normal), 3);
    const float length =
        std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    EXPECT_NEAR(1.0f, length, kTolerance)
        << "the blended normal has length " << length
        << " -- an un-renormalised sum brightens every surface it touches";

    // And it points where the sum points: +Z plus +X, normalised.
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(invSqrt2, normal[0], 1e-4f);
    EXPECT_NEAR(0.0f, normal[1], 1e-4f);
    EXPECT_NEAR(invSqrt2, normal[2], 1e-4f);
}

TEST(GltfMorphBlending, ABlendedTangentKeepsItsHandednessUntouched)
{
    // plan_gltf.md GLTF-279. The tangent's `w` is a sign, not a value: it says which way the
    // bitangent runs, and it is a property of the UV winding rather than of the pose. Blending it
    // would take it through 0, which is not a handedness at all -- so only xyz is morphed.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(0.0f, 0.0f, 0.0f)}};
    morph.NormalDeltas = {{}};
    morph.TangentDeltas = {{Vector3(0.0f, 1.0f, 0.0f)}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const std::vector<float> tangent = Read(blended, 0, OffsetOf(VertexElementUsage::Tangent), 4);
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(invSqrt2, tangent[0], 1e-4f);
    EXPECT_NEAR(invSqrt2, tangent[1], 1e-4f);
    EXPECT_NEAR(0.0f, tangent[2], 1e-4f);
    EXPECT_FLOAT_EQ(-1.0f, tangent[3])
        << "the handedness was blended; it is a sign decided by the UV winding, and interpolating "
           "it passes through 0, which is not a handedness";
}

// --- GLTF-284: the weight vector must match the target count -----------------------------------

TEST(GltfMorphBlending, AWeightVectorOfTheWrongLengthThrowsAndNamesBothCounts)
{
    // The weight vector comes from the application, and a mismatch is a caller bug rather than a
    // file one -- which is exactly why it must not be tolerated silently. Ignoring the extra
    // weights, or padding the missing ones with zero, gives a plausible mesh from an incoherent
    // call and hides the bug at the point it could still be attributed.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(1.0f, 0.0f, 0.0f)}, {Vector3(0.0f, 1.0f, 0.0f)}};
    morph.NormalDeltas = {{}, {}};

    for (const std::vector<float>& weights :
         std::vector<std::vector<float>>{{}, {1.0f}, {1.0f, 0.0f, 0.0f}})
    {
        SCOPED_TRACE(std::to_string(weights.size()) + " weight(s) for 2 targets");
        std::string message;
        try
        {
            (void)BlendMorphTargetsEXT(morph, weights);
        }
        catch (const std::exception& e)
        {
            message = e.what();
        }
        ASSERT_FALSE(message.empty()) << "a mismatched weight vector was accepted";
        EXPECT_NE(std::string::npos, message.find("2 weight")) << message;
        EXPECT_NE(std::string::npos, message.find(std::to_string(weights.size()))) << message;
    }
}

TEST(GltfMorphBlending, TheCorrectWeightCountIsAcceptedSoTheGuardIsNotSimplyAlwaysThrowing)
{
    // The control for the test above. Without it, a guard that rejected every call would pass every
    // assertion there while making morphing impossible.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(1.0f, 0.0f, 0.0f)}, {Vector3(0.0f, 1.0f, 0.0f)}};
    morph.NormalDeltas = {{}, {}};

    EXPECT_NO_THROW((void)BlendMorphTargetsEXT(morph, {0.5f, 0.5f}));
}

// --- GLTF-177: the renormalisation point on the CPU blend path -------------------------------------

TEST(GltfMorphBlending, ABlendedNormalAndTangentComeBackUnitLength)
{
    // A weighted sum of unit vectors is not a unit vector, and the shortfall is largest exactly
    // where morphing is most visible: two normals 90 degrees apart at half weight each sum to
    // length 0.707, so a fully morphed surface would light about 30% too dark. The shader's own
    // `normalize` would rescue the *drawn* result, but not a CPU-side reader of the blended buffer
    // -- a bounds computation, a picking ray, a `.cnj` re-export -- so the policy
    // (docs/gltf-conventions.md) puts the renormalisation here, on the buffer everything sees.
    //
    // The base normal is +Z and the delta turns it toward +X; at weight 1 the sum is (1,0,1),
    // whose length is sqrt(2). Nothing about that is subtle, which is the point.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.Stride = kStride;
    morph.PositionDeltas.push_back({Vector3(0.0f, 0.0f, 0.0f)});
    morph.NormalDeltas.push_back({Vector3(1.0f, 0.0f, -0.0f)});
    morph.TangentDeltas.push_back({Vector3(0.0f, 1.0f, 0.0f)});

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    ASSERT_EQ(morph.BaseVertexBytes.size(), blended.size());

    float normal[3];
    std::memcpy(normal, blended.data() + static_cast<std::size_t>(OffsetOf(VertexElementUsage::Normal)),
                sizeof(normal));
    const float normalLength =
        std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    EXPECT_NEAR(1.0f, normalLength, 1e-4f)
        << "the blended normal is " << normalLength << " long; a morphed surface lights by that "
           "factor";
    // And it points the way the sum does, not merely somewhere unit-length: (1,0,1) normalised.
    EXPECT_NEAR(0.70710678f, normal[0], 1e-4f);
    EXPECT_NEAR(0.70710678f, normal[2], 1e-4f);

    float tangent[4];
    std::memcpy(tangent, blended.data() + static_cast<std::size_t>(OffsetOf(VertexElementUsage::Tangent)),
                sizeof(tangent));
    const float tangentLength =
        std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
    EXPECT_NEAR(1.0f, tangentLength, 1e-4f) << "the blended tangent is not unit length";
    EXPECT_FLOAT_EQ(-1.0f, tangent[3])
        << "renormalisation rewrote the handedness sign, which is not a length at all";
}
