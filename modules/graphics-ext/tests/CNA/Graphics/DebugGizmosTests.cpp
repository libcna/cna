// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2161: gizmos for the engine layer's own invisible structures.
//
// This is the tool Phase 20 wanted and did without. A light's reach, a probe grid's spacing, a
// cluster's depth slices and a cascade's fitted volume are decisions the layer makes silently, and
// every one of them was checked by arithmetic because there was no way to look at one.
//
// The tests check the geometry each gizmo builds against the structure it claims to describe --
// a point light's sphere has to have the light's range, a probe grid has to have a marker at each
// probe position the volume reports. A gizmo drawing plausible lines in the wrong place is exactly
// the failure that a debug helper cannot afford, because nobody checks a debug view against
// anything.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/DebugDraw.hpp"
#include "CNA/Graphics/DebugGizmos.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::ClusteredLightGrid;
using CNA::Graphics::DebugDraw;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::LightProbeVolumeEXT;
using CNA::Graphics::PointLightEXT;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::SpotLightEXT;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingFrustum;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

Matrix View()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero,
                                Vector3(0.0f, 1.0f, 0.0f));
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 1.0f, 100.0f);
}

float Distance(const Vector3& a, const Vector3& b)
{
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool AnyVertexNear(const std::vector<VertexPositionColor>& vertices, const Vector3& point,
                   const float tolerance)
{
    for (const VertexPositionColor& vertex : vertices)
        if (Distance(vertex.Position, point) <= tolerance) return true;
    return false;
}

// ── Lights ──────────────────────────────────────────────────────────────────

TEST(DebugGizmosTest, APointLightsSphereHasItsRange)
{
    // The sphere is the whole claim a point light makes -- beyond Range it contributes nothing --
    // and Range is the number most often set to something that turns out not to cover the room.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    PointLightEXT light;
    light.Position = Vector3(3.0f, 4.0f, 5.0f);
    light.Range    = 7.0f;
    CNA::Graphics::addPointLightGizmo(debug, light, Color::Yellow);

    ASSERT_GT(debug.getLineCount(), 0);
    int onTheSphere = 0;
    for (const VertexPositionColor& vertex : debug.getVertices(true))
    {
        const float radius = Distance(vertex.Position, light.Position);
        // Everything is either on the range sphere or on the small cross at the centre.
        EXPECT_TRUE(std::abs(radius - light.Range) < 1e-2f || radius <= light.Range * 0.05f + 1e-3f)
            << "a vertex sits at " << radius << ", neither on the range nor at the centre";
        if (std::abs(radius - light.Range) < 1e-2f) ++onTheSphere;
    }
    EXPECT_GT(onTheSphere, 24) << "the range sphere is missing";
}

TEST(DebugGizmosTest, ASpotLightsConeReachesItsRangeAtItsOuterAngle)
{
    // The cone's base radius is range * tan(outer angle) and its centre is range along the axis.
    // Getting either wrong draws a plausible cone in the wrong place, which is the one failure a
    // debug helper cannot afford -- nobody checks a debug view against anything.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    SpotLightEXT light;
    light.Position   = Vector3(0.0f, 10.0f, 0.0f);
    light.Direction  = Vector3(0.0f, -1.0f, 0.0f);
    light.Range      = 8.0f;
    light.InnerAngle = 0.2f;
    light.OuterAngle = 0.6f;
    CNA::Graphics::addSpotLightGizmo(debug, light, Color::White, 16);

    const float outerRadius = light.Range * std::tan(light.OuterAngle);
    const float innerRadius = light.Range * std::tan(light.InnerAngle);
    const Vector3 baseCentre(0.0f, light.Position.Y - light.Range, 0.0f);

    int outer = 0, inner = 0, apex = 0;
    for (const VertexPositionColor& vertex : debug.getVertices(true))
    {
        if (Distance(vertex.Position, light.Position) < 1e-3f) { ++apex; continue; }
        EXPECT_NEAR(vertex.Position.Y, baseCentre.Y, 1e-3f)
            << "a cone vertex is not on the base plane or at the apex";
        const float radius = std::sqrt(vertex.Position.X * vertex.Position.X
                                     + vertex.Position.Z * vertex.Position.Z);
        if (std::abs(radius - outerRadius) < 1e-2f) ++outer;
        else if (std::abs(radius - innerRadius) < 1e-2f) ++inner;
        else ADD_FAILURE() << "a cone vertex is at radius " << radius << ", neither " << outerRadius
                           << " nor " << innerRadius;
    }
    EXPECT_GT(outer, 16) << "the outer cone is missing";
    EXPECT_GT(inner, 16) << "the inner cone is missing -- the falloff has nothing to show";
    EXPECT_GT(apex, 4) << "no ribs join the apex to the base";
}

TEST(DebugGizmosTest, ADegenerateSpotDirectionDoesNotProduceNaNs)
{
    // normalize(vec3(0)) is NaN and every comparison against NaN is false, so a degenerate
    // direction would draw a gizmo made entirely of nothing, with no error to point at.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    SpotLightEXT light;
    light.Direction = Vector3(0.0f, 0.0f, 0.0f);
    CNA::Graphics::addSpotLightGizmo(debug, light, Color::White, 8);

    ASSERT_GT(debug.getLineCount(), 0);
    for (const VertexPositionColor& vertex : debug.getVertices(true))
    {
        EXPECT_FALSE(std::isnan(vertex.Position.X));
        EXPECT_FALSE(std::isnan(vertex.Position.Y));
        EXPECT_FALSE(std::isnan(vertex.Position.Z));
    }
}

TEST(DebugGizmosTest, ADirectionalArrowPointsTheWayTheLightTravels)
{
    // The convention matters: DirectionalLightEXT::Direction is where the light *goes*, not where
    // it comes from, and an arrow drawn the other way would be a plausible picture of a wrong fact.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    DirectionalLightEXT light;
    light.Direction = Vector3(0.0f, -1.0f, 0.0f);
    const Vector3 at(1.0f, 2.0f, 3.0f);
    CNA::Graphics::addDirectionalLightGizmo(debug, light, at, 4.0f, Color::White);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    ASSERT_GE(vertices.size(), 2u);
    // The shaft runs from four units *against* the travel direction to the point itself.
    EXPECT_NEAR(vertices[0].Position.Y, 6.0f, 1e-4f);
    EXPECT_NEAR(vertices[1].Position.Y, 2.0f, 1e-4f);
    EXPECT_TRUE(AnyVertexNear(vertices, at, 1e-3f)) << "the arrow does not reach the point";
}

// ── Probe volumes ───────────────────────────────────────────────────────────

TEST(DebugGizmosTest, AProbeVolumeGetsItsBoundsAndAMarkerAtEveryProbe)
{
    // The spacing is the point: irradiance is interpolated between probes, so a grid coarser than
    // the geometry inside it leaks light through walls, and the grid is the only way to see that
    // before it happens.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    const BoundingBox bounds(Vector3(-4.0f, 0.0f, -4.0f), Vector3(4.0f, 4.0f, 4.0f));
    LightProbeVolumeEXT volume(bounds, 3, 2, 3);
    CNA::Graphics::addProbeVolumeGizmo(debug, volume, Color::Green, 0.2f);

    // Twelve box edges plus three arms per probe.
    EXPECT_EQ(debug.getLineCount(), 12 + volume.getProbeCount() * 3);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    for (int z = 0; z < volume.getCountZ(); ++z)
        for (int y = 0; y < volume.getCountY(); ++y)
            for (int x = 0; x < volume.getCountX(); ++x)
                EXPECT_TRUE(AnyVertexNear(vertices, volume.getProbePosition(x, y, z), 0.25f))
                    << "no marker at probe (" << x << ", " << y << ", " << z << ")";
}

// ── Clustered grids ─────────────────────────────────────────────────────────

TEST(DebugGizmosTest, EachDepthSliceGetsOneBoxAndTheyGrowWithDistance)
{
    // The slices and not the tiles: a 16 by 8 grid over 24 slices is 3072 boxes, which is a thicket
    // rather than a picture. What the slices show is that the slicing is *exponential* -- the near
    // ones are thin and the far ones enormous -- which is the property the whole scheme rests on.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    ClusteredLightGrid grid(4, 4, 6);
    grid.setProjection(Projection(), 1.0f, 100.0f);
    // The identity: the gizmo's world transform is a no-op, so the vertices come back in view
    // space and can be compared against the grid's own numbers directly.
    CNA::Graphics::addClusterSliceGizmo(debug, grid, Matrix::getIdentityProperty(), Color::Cyan);

    EXPECT_EQ(debug.getLineCount(), grid.getSliceCount() * 12);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    ASSERT_EQ(vertices.size(), static_cast<std::size_t>(grid.getSliceCount()) * 24);

    float previousDepth = 0.0f;
    for (int slice = 0; slice < grid.getSliceCount(); ++slice)
    {
        float nearest = -1e9f, furthest = 1e9f;
        for (std::size_t i = 0; i < 24; ++i)
        {
            const float z = vertices[static_cast<std::size_t>(slice) * 24 + i].Position.Z;
            nearest  = std::max(nearest, z);    // view space looks down -Z
            furthest = std::min(furthest, z);
        }
        const float depth = nearest - furthest;
        EXPECT_GT(depth, 0.0f) << "slice " << slice << " has no depth";
        if (slice > 0)
            EXPECT_GT(depth, previousDepth)
                << "slice " << slice << " is not deeper than the one before it, so the slicing is "
                   "not exponential";
        previousDepth = depth;
    }
}

TEST(DebugGizmosTest, AGridWithNoProjectionDrawsNothingRatherThanThrowing)
{
    // A debug helper that throws is worse than one that draws nothing: it takes the frame down at
    // exactly the moment somebody is trying to see what is wrong with it.
    GraphicsDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    ClusteredLightGrid grid(4, 4, 6);
    EXPECT_NO_THROW(CNA::Graphics::addClusterSliceGizmo(debug, grid,
                                                        Matrix::getIdentityProperty(),
                                                        Color::Cyan));
    EXPECT_EQ(debug.getLineCount(), 0);
}

TEST(DebugGizmosTest, TheSliceBoxesArePlacedByTheViewMatrixTheyAreGiven)
{
    // The grid's bounds are in view space, so a wrong or missing transform puts every slice at the
    // origin -- a picture that looks like a working gizmo describing a broken grid.
    GraphicsDevice gd;
    DebugDraw debug(gd);

    ClusteredLightGrid grid(2, 2, 3);
    grid.setProjection(Projection(), 1.0f, 100.0f);

    debug.begin(View(), Projection());
    CNA::Graphics::addClusterSliceGizmo(debug, grid, Matrix::getIdentityProperty(), Color::Cyan);
    const std::vector<VertexPositionColor> atOrigin = debug.getVertices(true);

    debug.begin(View(), Projection());
    CNA::Graphics::addClusterSliceGizmo(debug, grid,
                                        Matrix::CreateTranslation(Vector3(100.0f, 0.0f, 0.0f)),
                                        Color::Cyan);
    const std::vector<VertexPositionColor>& moved = debug.getVertices(true);

    ASSERT_EQ(atOrigin.size(), moved.size());
    for (std::size_t i = 0; i < moved.size(); ++i)
        EXPECT_NEAR(moved[i].Position.X - atOrigin[i].Position.X, 100.0f, 1e-3f);
}

// ── Cascades ────────────────────────────────────────────────────────────────

TEST(DebugGizmosTest, EachCascadeGetsTheFrustumItsMatrixDescribes)
{
    // What a cascade set actually decided, which is otherwise invisible: how much world each level
    // covers and how much they overlap. A cascade fitted far larger than its split needs is
    // resolution thrown away and looks like nothing at all in the rendered frame.
    GraphicsDevice gd;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 3);
    if (!cascades.isSupported())
        GTEST_SKIP() << "this renderer cannot run cascaded shadow maps";

    DirectionalLightEXT light;
    light.Direction = Vector3(-0.4f, -1.0f, -0.3f);
    cascades.update(light, View(), Projection());

    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    CNA::Graphics::addCascadeGizmo(debug, cascades, Color::Magenta);

    EXPECT_EQ(debug.getLineCount(), cascades.getCascadeCount() * 12);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    for (int i = 0; i < cascades.getCascadeCount(); ++i)
    {
        const BoundingFrustum expected(cascades.getCascadeMatrix(i));
        for (const Vector3& corner : expected.GetCorners())
            EXPECT_TRUE(AnyVertexNear(vertices, corner, 1e-2f))
                << "cascade " << i << " is missing a corner of its own volume";
    }
}

TEST(DebugGizmosTest, LaterCascadesCoverMoreWorldThanEarlierOnes)
{
    // The property the whole cascade scheme exists for, made visible: each level trades resolution
    // for reach. A set where the volumes came out the same size is one whose split lambda did
    // nothing, and nothing in a rendered frame says so.
    GraphicsDevice gd;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 3);
    if (!cascades.isSupported())
        GTEST_SKIP() << "this renderer cannot run cascaded shadow maps";

    DirectionalLightEXT light;
    light.Direction = Vector3(-0.4f, -1.0f, -0.3f);
    cascades.update(light, View(), Projection());

    const auto extent = [&](const int index) {
        const std::vector<Vector3> corners =
            BoundingFrustum(cascades.getCascadeMatrix(index)).GetCorners();
        float widest = 0.0f;
        for (const Vector3& a : corners)
            for (const Vector3& b : corners) widest = std::max(widest, Distance(a, b));
        return widest;
    };

    for (int i = 1; i < cascades.getCascadeCount(); ++i)
        EXPECT_GT(extent(i), extent(i - 1))
            << "cascade " << i << " is no larger than cascade " << i - 1;
}

} // namespace

#endif // CNA_CNAEXT
