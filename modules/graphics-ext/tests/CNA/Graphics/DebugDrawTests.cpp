// SPDX-License-Identifier: MS-PL
// MOD-2160, MOD-2162: drawing wireframe shapes, and the two depth modes.
//
// Every frustum, probe grid, cluster slice and light bound built in Phase 20 was verified by
// arithmetic, because nothing in this layer could draw one. These tests check the geometry the
// helper builds rather than the pixels it produces -- a box has to be the twelve edges joining the
// eight corners `BoundingBox::GetCorners` returns, and that is a statement about vertices, provable
// on a renderer that cannot read a frame back at all.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "RetroGraphicsTestSupport.hpp"

#include "CNA/Graphics/DebugDraw.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::Graphics::DebugDraw;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingFrustum;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

Matrix View()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero,
                                Vector3(0.0f, 1.0f, 0.0f));
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
}

/// A key for one endpoint, rounded, so a line can be looked up by where it goes rather than by the
/// order the helper happened to emit it in.
long Key(const Vector3& point)
{
    const auto q = [](const float v) { return static_cast<long>(std::lround(v * 1000.0f)); };
    return q(point.X) * 1000003L + q(point.Y) * 1009L + q(point.Z);
}

std::set<std::pair<long, long>> EdgeSet(const std::vector<VertexPositionColor>& vertices)
{
    std::set<std::pair<long, long>> edges;
    for (std::size_t i = 0; i + 1 < vertices.size(); i += 2)
    {
        const long a = Key(vertices[i].Position);
        const long b = Key(vertices[i + 1].Position);
        edges.insert({std::min(a, b), std::max(a, b)});
    }
    return edges;
}

// ── Shapes ──────────────────────────────────────────────────────────────────

TEST(DebugDrawTest, ABoxIsTwelveEdgesJoiningItsEightCorners)
{
    // The strong form: not "twelve lines were emitted" but "the twelve lines are exactly the box's
    // edges". A helper that emitted twelve arbitrary segments would pass the count and fail this.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    const BoundingBox box(Vector3(-1.0f, -2.0f, -3.0f), Vector3(4.0f, 5.0f, 6.0f));
    debug.addBox(box, Color::Yellow);

    EXPECT_EQ(debug.getLineCount(), 12);

    const std::set<std::pair<long, long>> drawn = EdgeSet(debug.getVertices(true));
    EXPECT_EQ(drawn.size(), 12u) << "two of the twelve lines are the same edge";

    // Every drawn endpoint must be a corner, and every corner must appear on exactly three edges --
    // which is what makes a box a box rather than a set of lines between its corners.
    const std::vector<Vector3> corners = box.GetCorners();
    std::set<long> cornerKeys;
    for (const Vector3& corner : corners) cornerKeys.insert(Key(corner));

    for (const auto& edge : drawn)
    {
        EXPECT_EQ(cornerKeys.count(edge.first), 1u);
        EXPECT_EQ(cornerKeys.count(edge.second), 1u);
    }
    for (const long corner : cornerKeys)
    {
        int touching = 0;
        for (const auto& edge : drawn)
            if (edge.first == corner || edge.second == corner) ++touching;
        EXPECT_EQ(touching, 3) << "a corner of the box has " << touching << " edges on it";
    }
}

TEST(DebugDrawTest, EveryBoxEdgeRunsAlongExactlyOneAxis)
{
    // A diagonal would satisfy the corner-count test above by joining the wrong pair.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addBox(BoundingBox(Vector3(-1.0f, -2.0f, -3.0f), Vector3(4.0f, 5.0f, 6.0f)),
                 Color::Yellow);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    for (std::size_t i = 0; i + 1 < vertices.size(); i += 2)
    {
        const Vector3& a = vertices[i].Position;
        const Vector3& b = vertices[i + 1].Position;
        int differing = 0;
        if (std::abs(a.X - b.X) > 1e-4f) ++differing;
        if (std::abs(a.Y - b.Y) > 1e-4f) ++differing;
        if (std::abs(a.Z - b.Z) > 1e-4f) ++differing;
        EXPECT_EQ(differing, 1) << "edge " << i / 2 << " is a diagonal, not an edge";
    }
}

TEST(DebugDrawTest, AFrustumIsTwelveEdgesJoiningItsEightCorners)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    const BoundingFrustum frustum(View() * Projection());
    debug.addFrustum(frustum, Color::Cyan);

    EXPECT_EQ(debug.getLineCount(), 12);
    EXPECT_EQ(EdgeSet(debug.getVertices(true)).size(), 12u);

    std::set<long> cornerKeys;
    for (const Vector3& corner : frustum.GetCorners()) cornerKeys.insert(Key(corner));
    for (const auto& edge : EdgeSet(debug.getVertices(true)))
    {
        EXPECT_EQ(cornerKeys.count(edge.first), 1u);
        EXPECT_EQ(cornerKeys.count(edge.second), 1u);
    }
}

TEST(DebugDrawTest, ASphereIsThreeRingsAndEveryPointIsOnIt)
{
    // Three rings rather than a mesh: a wireframe ball is unreadable at any line count a debug
    // helper can afford. What must hold is that every vertex is on the sphere's surface.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    const Vector3 centre(1.0f, 2.0f, 3.0f);
    constexpr float radius = 2.5f;
    constexpr int segments = 16;
    debug.addSphere(centre, radius, Color::Red, segments);

    EXPECT_EQ(debug.getLineCount(), segments * 3);

    for (const VertexPositionColor& vertex : debug.getVertices(true))
    {
        const float dx = vertex.Position.X - centre.X;
        const float dy = vertex.Position.Y - centre.Y;
        const float dz = vertex.Position.Z - centre.Z;
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), radius, 1e-3f);
    }
}

TEST(DebugDrawTest, TheSegmentCountIsClampedRatherThanTrusted)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);

    debug.begin(View(), Projection());
    debug.addSphere(Vector3::Zero, 1.0f, Color::Red, 0);
    EXPECT_EQ(debug.getLineCount(), 4 * 3);

    debug.begin(View(), Projection());
    debug.addSphere(Vector3::Zero, 1.0f, Color::Red, 10000);
    EXPECT_EQ(debug.getLineCount(), 128 * 3);
}

TEST(DebugDrawTest, ABoundingSphereOverloadUsesItsOwnCentreAndRadius)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addSphere(BoundingSphere(Vector3(5.0f, 0.0f, 0.0f), 3.0f), Color::Red, 8);

    for (const VertexPositionColor& vertex : debug.getVertices(true))
    {
        const float dx = vertex.Position.X - 5.0f;
        const float dy = vertex.Position.Y;
        const float dz = vertex.Position.Z;
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 3.0f, 1e-3f);
    }
}

TEST(DebugDrawTest, ACrossIsThreeAxisAlignedArms)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addCross(Vector3(1.0f, 1.0f, 1.0f), 0.5f, Color::White);

    EXPECT_EQ(debug.getLineCount(), 3);
    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    EXPECT_NEAR(vertices[0].Position.X, 0.5f, 1e-5f);
    EXPECT_NEAR(vertices[1].Position.X, 1.5f, 1e-5f);
    EXPECT_NEAR(vertices[3].Position.Y, 1.5f, 1e-5f);
    EXPECT_NEAR(vertices[5].Position.Z, 1.5f, 1e-5f);
}

TEST(DebugDrawTest, TheColourIsCarriedOnEveryVertex)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addLine(Vector3::Zero, Vector3::One, Color(10, 20, 30, 255));

    for (const VertexPositionColor& vertex : debug.getVertices(true))
    {
        EXPECT_EQ(vertex.Color.getRProperty(), 10);
        EXPECT_EQ(vertex.Color.getGProperty(), 20);
        EXPECT_EQ(vertex.Color.getBProperty(), 30);
    }
}

// ── MOD-2162: the two depth modes ───────────────────────────────────────────

TEST(DebugDrawTest, SubmissionsGoToTheListTheModeSelectedWhenTheyWereMade)
{
    // Per submission, not per batch: the two modes answer different questions in one frame, and a
    // helper that made it a batch-wide setting would need two batches to ask both.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());

    EXPECT_TRUE(debug.isDepthTested()) << "depth-tested is the default; an overlay by default would "
                                          "put every gizmo through the walls";
    debug.addLine(Vector3::Zero, Vector3::One, Color::Red);
    debug.setDepthTested(false);
    EXPECT_FALSE(debug.isDepthTested());
    debug.addLine(Vector3::Zero, Vector3::One, Color::Blue);
    debug.addLine(Vector3::Zero, Vector3::One, Color::Blue);

    EXPECT_EQ(debug.getVertices(true).size(), 2u);
    EXPECT_EQ(debug.getVertices(false).size(), 4u);
    EXPECT_EQ(debug.getLineCount(), 3);
}

TEST(DebugDrawTest, TheModeResetsWithEachBatch)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.setDepthTested(false);
    debug.begin(View(), Projection());
    EXPECT_TRUE(debug.isDepthTested());
}

TEST(DebugDrawTest, BeginForgetsWhateverTheLastBatchHeld)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addBox(BoundingBox(Vector3::Zero, Vector3::One), Color::Red);
    debug.begin(View(), Projection());
    EXPECT_EQ(debug.getLineCount(), 0);
}

TEST(DebugDrawTest, ClearForgetsTheShapesAndLeavesTheBatchOpen)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addBox(BoundingBox(Vector3::Zero, Vector3::One), Color::Red);
    debug.clear();
    EXPECT_EQ(debug.getLineCount(), 0);
    debug.addLine(Vector3::Zero, Vector3::One, Color::Red);
    EXPECT_EQ(debug.getLineCount(), 1);
}

// ── Drawing ─────────────────────────────────────────────────────────────────

TEST(DebugDrawTest, TheBatchIsEmptyAfterItIsDrawn)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D target(gd, 64, 64);
    gd.SetRenderTarget(&target);
    gd.Clear(Color::Black);

    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addBox(BoundingBox(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)),
                 Color::Yellow);
    debug.setDepthTested(false);
    debug.addCross(Vector3::Zero, 1.0f, Color::White);
    debug.end();

    gd.SetRenderTarget(nullptr);
    EXPECT_EQ(debug.getLineCount(), 0);
}

TEST(DebugDrawTest, EndingWithoutABatchOpenDoesNothing)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    EXPECT_NO_THROW(debug.end());
    EXPECT_NO_THROW(debug.end());
}

TEST(DebugDrawTest, AnOverlayShapeSurvivesGeometryInFrontOfIt)
{
    // MOD-2162's actual claim, on the GPU: a line behind a solid wall is invisible depth-tested and
    // visible as an overlay. This is what makes the mode worth having -- finding the gizmo that
    // turned out to be inside the floor.
    CnaTest::RetroGraphics::HiDefDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    RenderTarget2D target(gd, 64, 64, false, SurfaceFormat::Color, DepthFormat::Depth24);

    // A solid wall, not a wall made of lines. The first version of this test built the occluder out
    // of DebugDraw lines two pixels apart and half the blue came through the gaps -- an occluder
    // with holes in it does not test occlusion.
    const auto at = [](const float x, const float y) {
        return VertexPositionColor(Vector3(x, y, 8.0f), Color(40, 0, 0, 255));
    };
    const VertexPositionColor wall[6] = {
        at(-2.0f, -2.0f), at(-2.0f, 2.0f), at(2.0f, 2.0f),
        at(-2.0f, -2.0f), at(2.0f, 2.0f),  at(2.0f, -2.0f),
    };

    const auto litPixels = [&](const bool depthTested) {
        gd.SetRenderTarget(&target);
        gd.Clear(Color::Black);
        gd.setRasterizerStateProperty(RasterizerState::CullNone);
        gd.setDepthStencilStateProperty(DepthStencilState::Default);

        BasicEffect solid(gd);
        solid.VertexColorEnabled = true;
        solid.World      = Matrix::getIdentityProperty();
        solid.View       = View();
        solid.Projection = Projection();
        solid.Apply();
        gd.SetVertexBuffer(nullptr);
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, wall, 0, 2);

        DebugDraw debug(gd);
        debug.begin(View(), Projection());
        debug.setDepthTested(depthTested);
        // A grid of lines well behind the wall.
        for (int i = -20; i <= 20; ++i)
            debug.addLine(Vector3(-1.0f, static_cast<float>(i) * 0.05f, -5.0f),
                          Vector3(1.0f, static_cast<float>(i) * 0.05f, -5.0f),
                          Color(0, 0, 255, 255));
        debug.end();

        gd.SetRenderTarget(nullptr);

        std::vector<Color> pixels(64 * 64, Color::Black);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        int blue = 0;
        for (const Color& pixel : pixels) if (pixel.getBProperty() > 128) ++blue;
        return blue;
    };

    const int hidden = litPixels(true);
    const int shown  = litPixels(false);
    std::printf("    blue pixels: depth-tested %d, overlay %d\n", hidden, shown);

    // Anti-vacuity: the overlay case must actually have drawn something.
    ASSERT_GT(shown, 100) << "the overlay lines did not reach the frame, so this compares nothing";
    EXPECT_EQ(hidden, 0)
        << "the depth-tested lines were not hidden by the solid wall in front of them";
}


// ── Exact geometry, worked out by hand ──────────────────────────────────────────────────

bool Near(const Vector3& a, const Vector3& b, const float tolerance)
{
    return std::abs(a.X - b.X) <= tolerance && std::abs(a.Y - b.Y) <= tolerance
        && std::abs(a.Z - b.Z) <= tolerance;
}

TEST(DebugDrawTest, ALineKeepsItsEndpointsInOrderAndTheWholeColour)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addLine(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f), Color(10, 20, 30, 40));

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    ASSERT_EQ(vertices.size(), 2U);
    EXPECT_EQ(vertices[0].Position, Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(vertices[1].Position, Vector3(4.0f, 5.0f, 6.0f));
    for (const VertexPositionColor& vertex : vertices)
        EXPECT_EQ(vertex.Color.getPackedValueProperty(), Color(10, 20, 30, 40).getPackedValueProperty())
            << "alpha included";
    EXPECT_TRUE(debug.getVertices(false).empty());
}

TEST(DebugDrawTest, ACrossIsExactlyThreeArmsOfTwiceItsSizeThroughThePoint)
{
    // addCross((1, 1, 1), 0.5): the arms run 0.5 -> 1.5 along each axis, holding the other two
    // coordinates at 1.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addCross(Vector3(1.0f, 1.0f, 1.0f), 0.5f, Color::White);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    ASSERT_EQ(vertices.size(), 6U);
    const std::pair<Vector3, Vector3> arms[3] = {
        {Vector3(0.5f, 1.0f, 1.0f), Vector3(1.5f, 1.0f, 1.0f)},
        {Vector3(1.0f, 0.5f, 1.0f), Vector3(1.0f, 1.5f, 1.0f)},
        {Vector3(1.0f, 1.0f, 0.5f), Vector3(1.0f, 1.0f, 1.5f)},
    };
    for (const auto& [from, to] : arms)
    {
        int found = 0;
        for (std::size_t i = 0; i + 1 < vertices.size(); i += 2)
        {
            const Vector3& a = vertices[i].Position;
            const Vector3& b = vertices[i + 1].Position;
            if ((Near(a, from, 1e-6f) && Near(b, to, 1e-6f)) || (Near(a, to, 1e-6f) && Near(b, from, 1e-6f)))
                ++found;
        }
        EXPECT_EQ(found, 1) << "arm (" << from.X << "," << from.Y << "," << from.Z << ") -> ("
                            << to.X << "," << to.Y << "," << to.Z << ")";
    }
}

TEST(DebugDrawTest, ASphereIsThreeClosedRegularPolygonsOneInEachAxisPlane)
{
    // Three great circles, one per axis plane, `segments` lines each. A closed regular polygon
    // of s sides inscribed in radius r has s distinct vertices and every side 2 r sin(pi / s):
    // here s = 6, r = 2, so every side is 2 * 2 * sin(30 degrees) = 2.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    const Vector3 centre(1.0f, 2.0f, 3.0f);
    constexpr int segments = 6;
    debug.addSphere(centre, 2.0f, Color::Red, segments);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    ASSERT_EQ(vertices.size(), static_cast<std::size_t>(3 * segments * 2));
    std::set<int> planeAxes;
    for (int ring = 0; ring < 3; ++ring)
    {
        SCOPED_TRACE("ring " + std::to_string(ring));
        const auto line = [&](int index) {
            const std::size_t at = static_cast<std::size_t>((ring * segments + index) * 2);
            return std::pair<Vector3, Vector3>{vertices[at].Position, vertices[at + 1].Position};
        };

        // One coordinate is the centre's on every vertex of the ring: that is its plane's axis.
        int axis = -1;
        for (int candidate = 0; candidate < 3; ++candidate)
        {
            bool constant = true;
            for (int i = 0; i < segments; ++i)
            {
                const auto [a, b] = line(i);
                const float ca[3] = {a.X, a.Y, a.Z};
                const float cb[3] = {b.X, b.Y, b.Z};
                const float cc[3] = {centre.X, centre.Y, centre.Z};
                if (std::abs(ca[candidate] - cc[candidate]) > 1e-5f || std::abs(cb[candidate] - cc[candidate]) > 1e-5f)
                    constant = false;
            }
            if (constant)
            {
                EXPECT_EQ(axis, -1) << "a ring lies on more than one axis plane, so it is degenerate";
                axis = candidate;
            }
        }
        EXPECT_NE(axis, -1) << "the ring is not in an axis plane through the centre";
        planeAxes.insert(axis);

        std::vector<Vector3> distinct;
        for (int i = 0; i < segments; ++i)
        {
            const auto [a, b] = line(i);
            EXPECT_NEAR(Vector3::Distance(a, b), 2.0f, 1e-4f) << "side " << i;
            const auto next = line((i + 1) % segments);
            EXPECT_TRUE(Near(b, next.first, 1e-5f)) << "side " << i << " does not meet the next one";
            if (std::none_of(distinct.begin(), distinct.end(), [&](const Vector3& p) { return Near(p, a, 1e-4f); }))
                distinct.push_back(a);
        }
        EXPECT_EQ(distinct.size(), static_cast<std::size_t>(segments));
    }
    EXPECT_EQ(planeAxes, (std::set<int>{0, 1, 2})) << "the three rings must be in three different planes";
}

TEST(DebugDrawTest, AFrustumsEdgesAreItsNearRingItsFarRingAndFourEdgesFromNearToFar)
{
    // The camera of View() * Projection() sits at z = 10 looking down -z with a 45-degree
    // vertical field of view, aspect 1, near 0.1 and far 100. The near plane is z = 9.9 with a
    // half-extent of 0.1 * tan(22.5 degrees) = 0.0414214; the far plane is z = -90 with a
    // half-extent of 100 * tan(22.5 degrees) = 41.4214. The twelve edges are the four sides of
    // each of those squares plus the four edges joining the corners of the same (x, y) sign.
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addFrustum(BoundingFrustum(View() * Projection()), Color::Cyan);

    const float nearHalf = 0.1f * std::tan(MathHelper::PiOver4 / 2.0f);
    const float farHalf = 100.0f * std::tan(MathHelper::PiOver4 / 2.0f);
    struct Corner
    {
        Vector3 position;
        int sx, sy;
        bool isNear;
    };
    std::vector<Corner> corners;
    for (const bool isNear : {true, false})
        for (const int sx : {-1, 1})
            for (const int sy : {-1, 1})
            {
                const float half = isNear ? nearHalf : farHalf;
                corners.push_back({Vector3(sx * half, sy * half, isNear ? 9.9f : -90.0f), sx, sy, isNear});
            }
    const auto cornerOf = [&corners](const Vector3& p) {
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            const float tolerance = corners[i].isNear ? 1e-4f : 5e-2f;
            if (Near(p, corners[i].position, tolerance)) return static_cast<int>(i);
        }
        return -1;
    };

    std::set<std::pair<int, int>> expected;
    for (std::size_t i = 0; i < corners.size(); ++i)
        for (std::size_t j = i + 1; j < corners.size(); ++j)
        {
            const Corner& a = corners[i];
            const Corner& b = corners[j];
            const int signsDiffering = (a.sx != b.sx ? 1 : 0) + (a.sy != b.sy ? 1 : 0);
            if ((a.isNear == b.isNear && signsDiffering == 1) || (a.isNear != b.isNear && signsDiffering == 0))
                expected.insert({static_cast<int>(i), static_cast<int>(j)});
        }
    ASSERT_EQ(expected.size(), 12U);

    const std::vector<VertexPositionColor>& vertices = debug.getVertices(true);
    ASSERT_EQ(vertices.size(), 24U);
    std::set<std::pair<int, int>> drawn;
    for (std::size_t i = 0; i < vertices.size(); i += 2)
    {
        const int a = cornerOf(vertices[i].Position);
        const int b = cornerOf(vertices[i + 1].Position);
        ASSERT_NE(a, -1) << "vertex " << i << " is not a frustum corner";
        ASSERT_NE(b, -1) << "vertex " << i + 1 << " is not a frustum corner";
        drawn.insert({std::min(a, b), std::max(a, b)});
    }
    EXPECT_EQ(drawn, expected);
}

TEST(DebugDrawTest, AnOverlayBoxGoesWhollyToTheOverlayList)
{
    CnaTest::RetroGraphics::HiDefDevice gd;
    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.setDepthTested(false);
    debug.addBox(BoundingBox(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)), Color::Yellow);
    debug.addSphere(Vector3::Zero, 1.0f, Color::Red, 4);
    EXPECT_TRUE(debug.getVertices(true).empty());
    EXPECT_EQ(debug.getVertices(false).size(), static_cast<std::size_t>(12 * 2 + 3 * 4 * 2));
    EXPECT_EQ(debug.getLineCount(), 12 + 12);
}

TEST(DebugDrawTest, EndRestoresTheDepthStencilStateItFound)
{
    // "Restores the depth state it found": the overlay pass draws with DepthStencilState::None,
    // so a caller's DepthRead must be back in place afterwards.
    CnaTest::RetroGraphics::HiDefDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::DepthStencilState;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    RenderTarget2D target(gd, 32, 32, false, SurfaceFormat::Color, DepthFormat::Depth24);
    gd.SetRenderTarget(&target);
    gd.Clear(Color::Black);
    gd.setDepthStencilStateProperty(DepthStencilState::DepthRead);

    DebugDraw debug(gd);
    debug.begin(View(), Projection());
    debug.addLine(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Color::Red);
    debug.setDepthTested(false);
    debug.addLine(Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Color::Blue);
    debug.end();

    const DepthStencilState& after = gd.getDepthStencilStateProperty();
    EXPECT_TRUE(after.getDepthBufferEnableProperty());
    EXPECT_FALSE(after.getDepthBufferWriteEnableProperty());
    EXPECT_EQ(after.getDepthBufferFunctionProperty(), DepthStencilState::DepthRead.getDepthBufferFunctionProperty());
    gd.SetRenderTarget(nullptr);
}

} // namespace

#endif // CNA_CNAEXT
