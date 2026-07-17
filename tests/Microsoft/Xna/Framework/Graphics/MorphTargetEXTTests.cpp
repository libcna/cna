// SPDX-License-Identifier: MS-PL
//
// plan_cnj.md CNB-62/63 (Phase 13B): engine-level tests for BlendMorphTargetsEXT/
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
