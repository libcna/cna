// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2060: the third kind of light -- one that is a surface rather than a point.
//
// The struct is data, so the only behaviour it has is IsValidEXT, and the only thing that makes it
// worth testing is what "valid" has to mean: a shape the form factor can integrate over. Two
// parallel axes enclose no area, and the integral answers that with a division by zero rather than
// with darkness, so it has to be refused before it reaches a shader.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <limits>

namespace {

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AreaLightEXT;
using Microsoft::Xna::Framework::Graphics::AreaLightShapeEXT;

AreaLightEXT Usable()
{
    AreaLightEXT light;
    light.Shape = AreaLightShapeEXT::Rectangle;
    light.Position = Vector3(0.0f, 3.0f, 0.0f);
    light.RightAxis = Vector3(1.0f, 0.0f, 0.0f);
    light.UpAxis = Vector3(0.0f, 0.0f, 1.0f);
    light.Range = 15.0f;
    return light;
}

TEST(AreaLightEXTTest, TheDefaultsDescribeAUsableLight)
{
    const AreaLightEXT light;
    EXPECT_EQ(light.Shape, AreaLightShapeEXT::Rectangle);
    EXPECT_FLOAT_EQ(light.Intensity, 1.0f);
    EXPECT_FLOAT_EQ(light.Range, 20.0f);
    EXPECT_FALSE(light.TwoSided) << "a light with a backing is the usual case";
    EXPECT_TRUE(light.IsValidEXT()) << "the defaults must not need fixing before they can be used";
}

TEST(AreaLightEXTTest, AUsableLightIsAccepted)
{
    EXPECT_TRUE(Usable().IsValidEXT());

    for (const AreaLightShapeEXT shape : {AreaLightShapeEXT::Rectangle, AreaLightShapeEXT::Disc,
                                          AreaLightShapeEXT::Tube})
    {
        AreaLightEXT light = Usable();
        light.Shape = shape;
        EXPECT_TRUE(light.IsValidEXT()) << "shape " << static_cast<int>(shape);
    }
}

TEST(AreaLightEXTTest, ADegenerateSurfaceIsRefused)
{
    AreaLightEXT noRange = Usable();      noRange.Range = 0.0f;
    AreaLightEXT backRange = Usable();    backRange.Range = -1.0f;
    AreaLightEXT darkening = Usable();    darkening.Intensity = -0.5f;
    AreaLightEXT noRight = Usable();      noRight.RightAxis = Vector3(0.0f, 0.0f, 0.0f);
    AreaLightEXT noUp = Usable();         noUp.UpAxis = Vector3(0.0f, 0.0f, 0.0f);

    for (const AreaLightEXT& bad : {noRange, backRange, darkening, noRight, noUp})
        EXPECT_FALSE(bad.IsValidEXT());
}

TEST(AreaLightEXTTest, ParallelAxesEncloseNoAreaAndAreRefusedForASurface)
{
    // The one case that is not obviously degenerate: both axes have length, but they lie along the
    // same line, so the rectangle they describe has no area at all.
    AreaLightEXT flat = Usable();
    flat.RightAxis = Vector3(1.0f, 0.0f, 0.0f);
    flat.UpAxis = Vector3(2.0f, 0.0f, 0.0f);

    flat.Shape = AreaLightShapeEXT::Rectangle;
    EXPECT_FALSE(flat.IsValidEXT()) << "a rectangle with no area was accepted";
    flat.Shape = AreaLightShapeEXT::Disc;
    EXPECT_FALSE(flat.IsValidEXT()) << "a disc with no area was accepted";

    // A tube is a line with a radius rather than a surface, so parallel axes are meaningful there.
    flat.Shape = AreaLightShapeEXT::Tube;
    EXPECT_TRUE(flat.IsValidEXT()) << "a tube does not need its axes to span a plane";
}

TEST(AreaLightEXTTest, NonFiniteNumbersAreRefused)
{
    const float nan = std::nan("");
    const float infinity = std::numeric_limits<float>::infinity();

    AreaLightEXT nowhere = Usable();  nowhere.Position = Vector3(nan, 0.0f, 0.0f);
    AreaLightEXT noColour = Usable(); noColour.Color = Vector3(0.0f, infinity, 0.0f);
    AreaLightEXT noAxis = Usable();   noAxis.RightAxis = Vector3(nan, 1.0f, 0.0f);
    AreaLightEXT noReach = Usable();  noReach.Range = infinity;
    AreaLightEXT noPower = Usable();  noPower.Intensity = nan;

    for (const AreaLightEXT& bad : {nowhere, noColour, noAxis, noReach, noPower})
        EXPECT_FALSE(bad.IsValidEXT());
}

TEST(AreaLightEXTTest, ZeroIntensityIsAcceptedBecauseItIsHowALightIsSwitchedOff)
{
    AreaLightEXT off = Usable();
    off.Intensity = 0.0f;
    EXPECT_TRUE(off.IsValidEXT());
}

} // namespace
