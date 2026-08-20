// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2046: the app-facing light collection, and the bounding volume the cluster
// assignment sorts by.
//
// The bounding sphere is the part worth testing hard. A point light's is obvious. A spot light's is
// a cone's bounding sphere, which is not centred on the light and has two cases -- and if it is
// wrong in the tight direction the lighting develops holes, while if it is wrong in the loose
// direction a torch claims every cluster behind the person holding it. So the tests sample the lit
// volume itself, including the *spherical* end a cone-with-a-flat-base would leave outside.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::PointLightEXT;
using CNA::Graphics::ClusteredLightEXT;
using CNA::Graphics::ClusteredLightSetEXT;
using CNA::Graphics::ClusteredLightType;
using CNA::Graphics::SpotLightEXT;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Vector3;

ClusteredLightEXT MakePoint()
{
    ClusteredLightEXT light;
    light.Type = ClusteredLightType::Point;
    light.Position = Vector3(1.0f, 2.0f, 3.0f);
    light.Range = 10.0f;
    return light;
}

ClusteredLightEXT MakeSpot(const float outerAngle)
{
    ClusteredLightEXT light;
    light.Type = ClusteredLightType::Spot;
    light.Position = Vector3(0.0f, 0.0f, 0.0f);
    light.Direction = Vector3(0.0f, 0.0f, -1.0f);
    light.Range = 10.0f;
    light.InnerAngle = outerAngle * 0.7f;
    light.OuterAngle = outerAngle;
    return light;
}

float DistanceFrom(const BoundingSphere& sphere, const Vector3& point)
{
    const float dx = point.X - sphere.Center.X;
    const float dy = point.Y - sphere.Center.Y;
    const float dz = point.Z - sphere.Center.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ── The collection ───────────────────────────────────────────────────────────

TEST(ClusteredLightSetTest, AFreshSetIsEmpty)
{
    const ClusteredLightSetEXT set;
    EXPECT_EQ(set.getCount(), 0);
    EXPECT_TRUE(set.isEmpty());
    EXPECT_TRUE(set.getLights().empty());
    EXPECT_TRUE(set.collectBounds().empty());
}

TEST(ClusteredLightSetTest, IndicesAreHandedOutInOrderAndSurviveAdditions)
{
    // Every stage downstream refers to a light by its index, so the numbering is API.
    ClusteredLightSetEXT set;
    EXPECT_EQ(set.add(MakePoint()), 0);
    EXPECT_EQ(set.add(MakeSpot(0.4f)), 1);
    EXPECT_EQ(set.add(MakePoint()), 2);
    EXPECT_EQ(set.getCount(), 3);
    EXPECT_EQ(set.getAt(1).Type, ClusteredLightType::Spot);
    EXPECT_EQ(set.getAt(2).Type, ClusteredLightType::Point);
}

TEST(ClusteredLightSetTest, TheSpecificLightTypesConvertWithoutLosingAnything)
{
    // A game keeps writing PointLightEXT and SpotLightEXT; the uniform record is this layer's
    // problem. Every field has to survive the crossing, and the cone fields must not be read off a
    // point light -- so the point light's converted record keeps the struct's own defaults there.
    ClusteredLightSetEXT set;

    PointLightEXT point;
    point.Position = Vector3(4.0f, -1.0f, 2.0f);
    point.Color = Vector3(0.2f, 0.4f, 0.9f);
    point.Intensity = 3.5f;
    point.Range = 17.0f;
    point.CastsShadows = true;
    const ClusteredLightEXT& converted = set.getAt(set.add(point));
    EXPECT_EQ(converted.Type, ClusteredLightType::Point);
    EXPECT_FLOAT_EQ(converted.Position.X, 4.0f);
    EXPECT_FLOAT_EQ(converted.Color.Z, 0.9f);
    EXPECT_FLOAT_EQ(converted.Intensity, 3.5f);
    EXPECT_FLOAT_EQ(converted.Range, 17.0f);
    EXPECT_TRUE(converted.CastsShadows);

    SpotLightEXT spot;
    spot.Position = Vector3(-2.0f, 6.0f, 0.5f);
    spot.Direction = Vector3(1.0f, 0.0f, 0.0f);
    spot.Color = Vector3(1.0f, 0.5f, 0.25f);
    spot.Intensity = 2.0f;
    spot.Range = 30.0f;
    spot.InnerAngle = 0.2f;
    spot.OuterAngle = 0.45f;
    spot.CastsShadows = true;
    const ClusteredLightEXT& convertedSpot = set.getAt(set.add(spot));
    EXPECT_EQ(convertedSpot.Type, ClusteredLightType::Spot);
    EXPECT_FLOAT_EQ(convertedSpot.Direction.X, 1.0f);
    EXPECT_FLOAT_EQ(convertedSpot.InnerAngle, 0.2f);
    EXPECT_FLOAT_EQ(convertedSpot.OuterAngle, 0.45f);
    EXPECT_FLOAT_EQ(convertedSpot.Range, 30.0f);
    EXPECT_TRUE(convertedSpot.CastsShadows);
}

TEST(ClusteredLightSetTest, RemovingRenumbersAndReplacingDoesNot)
{
    // The difference is stated in the header and is the reason zero intensity is allowed: it is how
    // a light is switched off without moving every index after it.
    ClusteredLightSetEXT set;
    ClusteredLightEXT a = MakePoint(); a.Range = 1.0f;
    ClusteredLightEXT b = MakePoint(); b.Range = 2.0f;
    ClusteredLightEXT c = MakePoint(); c.Range = 3.0f;
    set.add(a);
    set.add(b);
    set.add(c);

    set.removeAt(1);
    EXPECT_EQ(set.getCount(), 2);
    EXPECT_FLOAT_EQ(set.getAt(0).Range, 1.0f);
    EXPECT_FLOAT_EQ(set.getAt(1).Range, 3.0f) << "removal must close the gap";

    ClusteredLightEXT replacement = MakePoint();
    replacement.Range = 9.0f;
    set.replaceAt(0, replacement);
    EXPECT_FLOAT_EQ(set.getAt(0).Range, 9.0f);
    EXPECT_FLOAT_EQ(set.getAt(1).Range, 3.0f) << "replacement must not disturb its neighbours";

    ClusteredLightEXT off = MakePoint();
    off.Intensity = 0.0f;
    EXPECT_NO_THROW(set.replaceAt(1, off)) << "zero intensity is how a light is switched off";

    set.clear();
    EXPECT_TRUE(set.isEmpty());
}

TEST(ClusteredLightSetTest, AnIndexOutsideTheSetIsRefused)
{
    ClusteredLightSetEXT set;
    set.add(MakePoint());
    EXPECT_THROW((void)set.getAt(1), std::out_of_range);
    EXPECT_THROW((void)set.getAt(-1), std::out_of_range);
    EXPECT_THROW(set.removeAt(1), std::out_of_range);
    EXPECT_THROW(set.replaceAt(1, MakePoint()), std::out_of_range);
    EXPECT_THROW((void)set.getBoundsAt(1), std::out_of_range);
}

// ── Validation ───────────────────────────────────────────────────────────────

TEST(ClusteredLightSetTest, AnUnusableLightIsRefusedRatherThanSkipped)
{
    ClusteredLightSetEXT set;
    const float nan = std::nan("");

    ClusteredLightEXT noRange = MakePoint();  noRange.Range = 0.0f;
    ClusteredLightEXT backRange = MakePoint(); backRange.Range = -1.0f;
    ClusteredLightEXT darkening = MakePoint(); darkening.Intensity = -0.5f;
    ClusteredLightEXT nowhere = MakePoint(); nowhere.Position = Vector3(nan, 0.0f, 0.0f);
    ClusteredLightEXT noColour = MakePoint(); noColour.Color = Vector3(0.0f, nan, 0.0f);
    ClusteredLightEXT infiniteRange = MakePoint();
    infiniteRange.Range = std::numeric_limits<float>::infinity();

    for (const ClusteredLightEXT& bad : {noRange, backRange, darkening, nowhere, noColour,
                                        infiniteRange})
    {
        EXPECT_FALSE(ClusteredLightSetEXT::isUsable(bad));
        EXPECT_THROW(set.add(bad), std::invalid_argument);
    }
    EXPECT_EQ(set.getCount(), 0) << "a refused light must not be half-added";
}

TEST(ClusteredLightSetTest, AnUnusableConeIsRefused)
{
    ClusteredLightSetEXT set;

    ClusteredLightEXT inverted = MakeSpot(0.4f);
    inverted.InnerAngle = 0.6f;                       // wider than the outer angle
    ClusteredLightEXT tooWide = MakeSpot(2.0f);        // past a hemisphere
    ClusteredLightEXT negative = MakeSpot(0.4f);
    negative.InnerAngle = -0.1f;
    ClusteredLightEXT noDirection = MakeSpot(0.4f);
    noDirection.Direction = Vector3(0.0f, 0.0f, 0.0f);

    for (const ClusteredLightEXT& bad : {inverted, tooWide, negative, noDirection})
    {
        EXPECT_FALSE(ClusteredLightSetEXT::isUsable(bad));
        EXPECT_THROW(set.add(bad), std::invalid_argument);
    }

    // The same fields on a point light are never read, so they cannot make it unusable.
    ClusteredLightEXT point = MakePoint();
    point.Direction = Vector3(0.0f, 0.0f, 0.0f);
    point.InnerAngle = 5.0f;
    point.OuterAngle = -5.0f;
    EXPECT_TRUE(ClusteredLightSetEXT::isUsable(point));
    EXPECT_NO_THROW(set.add(point));
}

TEST(ClusteredLightSetTest, TheMaximumIsRefusedRatherThanGrown)
{
    ClusteredLightSetEXT set;
    for (int i = 0; i < ClusteredLightSetEXT::kMaxLights; ++i)
        ASSERT_EQ(set.add(MakePoint()), i);
    EXPECT_EQ(set.getCount(), ClusteredLightSetEXT::kMaxLights);
    EXPECT_THROW(set.add(MakePoint()), std::length_error);
    EXPECT_EQ(set.getCount(), ClusteredLightSetEXT::kMaxLights);
}

// ── Bounding volumes ─────────────────────────────────────────────────────────

TEST(ClusteredLightSetTest, APointLightIsBoundedByItsOwnRange)
{
    ClusteredLightSetEXT set;
    const BoundingSphere bounds = set.getBoundsAt(set.add(MakePoint()));
    EXPECT_FLOAT_EQ(bounds.Center.X, 1.0f);
    EXPECT_FLOAT_EQ(bounds.Center.Y, 2.0f);
    EXPECT_FLOAT_EQ(bounds.Center.Z, 3.0f);
    EXPECT_FLOAT_EQ(bounds.Radius, 10.0f);
}

TEST(ClusteredLightSetTest, ASpotLightsBoundsContainEveryPointItCanLight)
{
    // The lit volume is a spherical sector: the cone's sides, and a *curved* end at the range --
    // a cone with a flat base would leave the bulge outside. Both are sampled, at cone angles
    // either side of the 45-degree boundary where the formula changes case.
    ClusteredLightSetEXT set;
    for (const float outer : {0.15f, 0.4f, 0.7f, 0.785f, 0.9f, 1.2f, 1.5f})
    {
        const ClusteredLightEXT light = MakeSpot(outer);
        const BoundingSphere bounds = set.getBoundsAt(set.add(light));

        EXPECT_LE(DistanceFrom(bounds, light.Position), bounds.Radius * 1.0001f + 1e-4f)
            << "the apex is outside the bounds at outer angle " << outer;

        for (int a = 0; a <= 8; ++a)
        {
            const float phi = outer * static_cast<float>(a) / 8.0f;
            for (int r = 0; r <= 8; ++r)
            {
                const float distance = light.Range * static_cast<float>(r) / 8.0f;
                // Around the axis (0, 0, -1), so the perpendicular is x.
                const Vector3 point(distance * std::sin(phi), 0.0f, -distance * std::cos(phi));
                EXPECT_LE(DistanceFrom(bounds, point), bounds.Radius * 1.0001f + 1e-4f)
                    << "a lit point is outside the bounds at outer " << outer << ", phi " << phi
                    << ", distance " << distance;
            }
        }
    }
}

TEST(ClusteredLightSetTest, ANarrowSpotIsBoundedFarMoreTightlyThanItsRange)
{
    // The point of the cone bound. A sphere of the light's range centred on the light would be
    // 1000 times the volume for a narrow torch, and every cluster behind the holder would claim it.
    ClusteredLightSetEXT set;
    const int narrow = set.add(MakeSpot(0.15f));
    const int wide   = set.add(MakeSpot(1.5f));

    const BoundingSphere narrowBounds = set.getBoundsAt(narrow);
    const BoundingSphere wideBounds   = set.getBoundsAt(wide);

    EXPECT_LT(narrowBounds.Radius, 10.0f * 0.55f) << "a narrow cone gained nothing over its range";
    EXPECT_LT(narrowBounds.Center.Z, -1.0f) << "the bounds must sit out along the cone's axis";
    EXPECT_GT(wideBounds.Radius, narrowBounds.Radius)
        << "a wider cone must need a larger sphere, not a smaller one";
}

TEST(ClusteredLightSetTest, TheCollectedBoundsAreInIndexOrder)
{
    // This is what ClusteredLightAssignment::assign is handed, and the index agreement between the
    // two is the reason the set produces it rather than the caller assembling one.
    ClusteredLightSetEXT set;
    ClusteredLightEXT first = MakePoint();  first.Range = 4.0f;
    ClusteredLightEXT second = MakePoint(); second.Range = 7.0f;
    set.add(first);
    set.add(second);

    const std::vector<BoundingSphere> bounds = set.collectBounds();
    ASSERT_EQ(bounds.size(), 2u);
    EXPECT_FLOAT_EQ(bounds[0].Radius, 4.0f);
    EXPECT_FLOAT_EQ(bounds[1].Radius, 7.0f);
    for (int index = 0; index < set.getCount(); ++index)
        EXPECT_FLOAT_EQ(bounds[static_cast<std::size_t>(index)].Radius,
                        set.getBoundsAt(index).Radius);
}

} // namespace

#endif // CNA_CNAEXT
