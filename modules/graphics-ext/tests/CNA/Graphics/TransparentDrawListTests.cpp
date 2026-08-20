// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2101..MOD-2103: ordering transparent draws.
//
// Three claims, and each is asserted against the thing that would break it rather than against a
// screenshot. The order is back-to-front (a frame drawn in the wrong order looks almost right, which
// is why this needs a test at all). Equal distances keep submission order, because an unstable
// tie-break makes a frame flicker between orderings that are each individually defensible. And the
// key is the nearest point of the bounds rather than their centre, which is the one case a centre
// sort visibly gets wrong.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/TransparentDrawList.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::TransparentDrawList;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

/// A camera at +Z looking towards the origin, which is where every distance below is measured from.
Matrix View()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
}

BoundingBox At(const float x, const float y, const float z, const float half = 0.5f)
{
    const Vector3 centre(x, y, z);
    const Vector3 extent(half, half, half);
    return BoundingBox(centre - extent, centre + extent);
}

TEST(TransparentDrawListTest, TheEyeIsRecoveredFromTheViewMatrix)
{
    const Vector3 eye = TransparentDrawList::cameraPositionOf(View());
    EXPECT_NEAR(eye.X, 0.0f, 1e-4f);
    EXPECT_NEAR(eye.Y, 0.0f, 1e-4f);
    EXPECT_NEAR(eye.Z, 10.0f, 1e-4f);
}

TEST(TransparentDrawListTest, TheKeyIsTheNearestPointOfTheBoundsAndNotTheCentre)
{
    const Vector3 eye(0.0f, 0.0f, 10.0f);

    // A box from z = -10 to z = 2: its centre is 14 away, its near face 8.
    const BoundingBox longBox(Vector3(-0.5f, -0.5f, -10.0f), Vector3(0.5f, 0.5f, 2.0f));
    EXPECT_NEAR(TransparentDrawList::sortKey(longBox, eye), 8.0f, 1e-4f);

    // The eye inside the bounds is distance zero, not a negative or a NaN.
    EXPECT_FLOAT_EQ(TransparentDrawList::sortKey(At(0.0f, 0.0f, 10.0f, 2.0f), eye), 0.0f);

    // Off-axis: the nearest point is the clamped corner, so the distance is the diagonal.
    const float diagonal = TransparentDrawList::sortKey(At(3.0f, 4.0f, 10.0f, 0.0f), eye);
    EXPECT_NEAR(diagonal, 5.0f, 1e-4f);
}

TEST(TransparentDrawListTest, ALongObjectCrossingAShortOneSortsByWhatTheCameraMeetsFirst)
{
    // MOD-2103, and the whole reason the key is not the centre. The long box reaches nearer to the
    // camera than the short one, so it must be drawn LAST -- but its centre is further away, so a
    // centre sort would draw it first and composite it behind something it visibly passes in front
    // of. The test states both numbers so the failure is readable rather than a swapped index.
    const Vector3 eye(0.0f, 0.0f, 10.0f);
    const BoundingBox longBox(Vector3(-0.5f, -0.5f, -20.0f), Vector3(0.5f, 0.5f, 4.0f));
    const BoundingBox shortBox = At(0.0f, 0.0f, 0.0f);

    const float longCentreDistance  = 10.0f - (-20.0f + 4.0f) * 0.5f;   // 18
    const float shortCentreDistance = 10.0f;
    ASSERT_GT(longCentreDistance, shortCentreDistance)
        << "the setup no longer distinguishes a centre sort from a nearest-point one";

    EXPECT_LT(TransparentDrawList::sortKey(longBox, eye),
              TransparentDrawList::sortKey(shortBox, eye))
        << "the long box reaches nearer the camera and must sort nearer";

    TransparentDrawList list;
    std::vector<std::string> drawn;
    list.submit(longBox, [&drawn] { drawn.emplace_back("long"); });
    list.submit(shortBox, [&drawn] { drawn.emplace_back("short"); });
    list.drawSorted(View());

    ASSERT_EQ(drawn.size(), 2u);
    EXPECT_EQ(drawn[0], "short") << "the further object must be drawn first";
    EXPECT_EQ(drawn[1], "long");
}

TEST(TransparentDrawListTest, DrawsRunFurthestFirstWhateverOrderTheyArrivedIn)
{
    TransparentDrawList list;
    std::vector<int> drawn;
    // Submitted nearest-first, which is the wrong order and the one a naive loop produces.
    list.submit(At(0.0f, 0.0f, 8.0f), [&drawn] { drawn.push_back(2); });
    list.submit(At(0.0f, 0.0f, 0.0f), [&drawn] { drawn.push_back(10); });
    list.submit(At(0.0f, 0.0f, -20.0f), [&drawn] { drawn.push_back(30); });
    EXPECT_EQ(list.getCount(), 3);

    list.drawSorted(View());
    ASSERT_EQ(drawn.size(), 3u);
    EXPECT_GT(drawn[0], drawn[1]);
    EXPECT_GT(drawn[1], drawn[2]) << "the list did not order back to front";
}

TEST(TransparentDrawListTest, EqualDistancesKeepSubmissionOrder)
{
    // MOD-2102. Four boxes on a circle around the eye: every one is exactly as far away, so the
    // comparator can express no preference and the only defensible answer is the order they came
    // in. Run twice, because an unstable sort is allowed to be consistent by luck once.
    TransparentDrawList list;
    std::vector<int> first;
    std::vector<int> second;
    const float radius = 6.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float angle = static_cast<float>(i) * 1.5707964f;
        const BoundingBox at = At(std::sin(angle) * radius, 0.0f, 10.0f + std::cos(angle) * radius,
                                  0.0f);
        list.submit(at, [i, &first, &second] {
            (first.size() < 4 ? first : second).push_back(i);
        });
    }
    list.drawSorted(View());
    list.drawSorted(View());

    const std::vector<int> expected{0, 1, 2, 3};
    EXPECT_EQ(first, expected);
    EXPECT_EQ(second, expected) << "the same list ordered itself differently the second time";
}

TEST(TransparentDrawListTest, ClearEndsAFrameAndDrawSortedDoesNot)
{
    // Separate on purpose: a game drawing the same transparent set into several views -- a
    // reflection, a shadow, a cube face -- submits once and draws several times.
    TransparentDrawList list;
    int calls = 0;
    list.submit(At(0.0f, 0.0f, 0.0f), [&calls] { ++calls; });

    list.drawSorted(View());
    list.drawSorted(View());
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(list.getCount(), 1) << "drawing consumed the list";

    list.clear();
    EXPECT_EQ(list.getCount(), 0);
    list.drawSorted(View());
    EXPECT_EQ(calls, 2) << "a cleared list still drew something";
}

TEST(TransparentDrawListTest, AnEmptySubmissionIsRefused)
{
    TransparentDrawList list;
    EXPECT_THROW(list.submit(At(0.0f, 0.0f, 0.0f), std::function<void()>()), std::invalid_argument);
    EXPECT_EQ(list.getCount(), 0);
}

TEST(TransparentDrawListTest, AnEmptyListIsNotAnError)
{
    TransparentDrawList list;
    EXPECT_EQ(list.getCount(), 0);
    EXPECT_TRUE(list.getSortedOrderEXT(View()).empty());
    EXPECT_NO_THROW(list.drawSorted(View()));
}

TEST(TransparentDrawListTest, TheOrderCanBeInspectedWithoutDrawing)
{
    TransparentDrawList list;
    list.submit(At(0.0f, 0.0f, 8.0f), [] {});
    list.submit(At(0.0f, 0.0f, -20.0f), [] {});
    const std::vector<int> order = list.getSortedOrderEXT(View());
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1) << "the furthest submission should come first";
    EXPECT_EQ(order[1], 0);
}

} // namespace

#endif // CNA_CNAEXT
