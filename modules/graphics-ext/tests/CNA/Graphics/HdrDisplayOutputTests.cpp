// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2092: HDR display output.
//
// Two halves, and they fail differently. The first is the promise that costs nothing to keep and is
// easy to break: in sRGB the pass copies through pixel for pixel, so SDR output is exactly the frame
// the pipeline already produced and the pass can be left in the chain on every machine. The second
// is the encoding itself, written once in GLSL and once in C++ and compared against each other on
// the GPU -- because a PQ curve that is subtly wrong looks like a plausible image, and only a
// display nobody here has would show the difference.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/DisplayColorSpace.hpp"
#include "CNA/Graphics/HdrDisplayOutput.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::DisplayColorSpace;
using CNA::Graphics::HdrDisplayOutput;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 8;

/// A source of distinct greys and colours, so an encoding error somewhere in the middle of the
/// curve shows up rather than only at the ends.
std::vector<Color> SourceTexels()
{
    std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 255));
    for (std::size_t i = 0; i < texels.size(); ++i)
    {
        const int value = static_cast<int>(i * 255 / (texels.size() - 1));
        texels[i] = Color(value, (value * 2) % 256, 255 - value, 255);
    }
    return texels;
}

std::unique_ptr<Texture2D> MakeSource(GraphicsDevice& device, const std::vector<Color>& texels)
{
    auto texture = std::make_unique<Texture2D>(device, kSize, kSize);
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::vector<Color> Encode(GraphicsDevice& device, HdrDisplayOutput& output,
                          const std::vector<Color>& texels)
{
    const auto source = MakeSource(device, texels);
    RenderTarget2D destination(device, kSize, kSize);
    output.draw(source.get(), &destination, kSize, kSize);
    std::vector<Color> result(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    destination.GetData(result.data(), static_cast<int>(result.size()));
    return result;
}

TEST(HdrDisplayOutputTest, TheColorSpaceOrdinalsAreTheOnesTheShaderBranchesOn)
{
    // The shader selects its branch with a plain integer, so these three values are part of the
    // contract between the two halves rather than an implementation detail of the enum.
    EXPECT_EQ(static_cast<int>(DisplayColorSpace::Srgb), 0);
    EXPECT_EQ(static_cast<int>(DisplayColorSpace::Scrgb), 1);
    EXPECT_EQ(static_cast<int>(DisplayColorSpace::Hdr10), 2);
}

TEST(HdrDisplayOutputTest, TheSwapChainAnswersSrgbAndSaysSoRatherThanPretending)
{
    GraphicsDevice device;
    EXPECT_EQ(device.GetDisplayColorSpaceEXT(), DisplayColorSpace::Srgb);
    EXPECT_TRUE(device.SupportsDisplayColorSpaceEXT(DisplayColorSpace::Srgb));
    EXPECT_TRUE(device.SetDisplayColorSpaceEXT(DisplayColorSpace::Srgb));

    // No CNA platform back end offers an HDR swap chain yet, and a renderer that accepted the
    // request without reconfiguring anything would have every caller encode for a display that is
    // not there. The refusal is the feature.
    EXPECT_FALSE(device.SetDisplayColorSpaceEXT(DisplayColorSpace::Hdr10));
    EXPECT_FALSE(device.SupportsDisplayColorSpaceEXT(DisplayColorSpace::Scrgb));
    EXPECT_EQ(device.GetDisplayColorSpaceEXT(), DisplayColorSpace::Srgb)
        << "a refused request changed the state anyway";
}

TEST(HdrDisplayOutputTest, ThePqCurveHitsItsKnownPoints)
{
    EXPECT_NEAR(HdrDisplayOutput::encodePq(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(HdrDisplayOutput::encodePq(10000.0f), 1.0f, 1e-5f);
    // The value the standard is usually quoted by: 100 nits, SDR reference white, lands just past
    // half of the code range -- which is what makes PQ's bottom half so much of its precision.
    EXPECT_NEAR(HdrDisplayOutput::encodePq(100.0f), 0.5081f, 2e-3f);
    EXPECT_GT(HdrDisplayOutput::encodePq(1000.0f), HdrDisplayOutput::encodePq(100.0f));
}

TEST(HdrDisplayOutputTest, ThePqCurveRoundTrips)
{
    for (const float nits : {0.1f, 1.0f, 10.0f, 100.0f, 203.0f, 1000.0f, 4000.0f, 10000.0f})
        EXPECT_NEAR(HdrDisplayOutput::decodePq(HdrDisplayOutput::encodePq(nits)), nits,
                    std::max(nits * 0.005f, 1e-3f));
}

TEST(HdrDisplayOutputTest, TheWidePrimariesLeaveWhiteWhere_ItWas)
{
    // Rec. 709 and Rec. 2020 share D65, so neutral must stay neutral -- a matrix transcribed with
    // one wrong digit tints every grey in the frame, which is the failure that is hardest to see
    // and easiest to ship.
    const Vector3 white = HdrDisplayOutput::rec709ToRec2020(Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_NEAR(white.X, 1.0f, 1e-4f);
    EXPECT_NEAR(white.Y, 1.0f, 1e-4f);
    EXPECT_NEAR(white.Z, 1.0f, 1e-4f);

    // And a saturated Rec. 709 primary must come out less saturated in the wider space, since the
    // same colour needs less of a wider gamut's primary to express.
    const Vector3 red = HdrDisplayOutput::rec709ToRec2020(Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_LT(red.X, 1.0f);
    EXPECT_GT(red.Y, 0.0f);
}

TEST(HdrDisplayOutputTest, TheRollOffApproachesThePeakWithoutReachingIt)
{
    constexpr float kPeak = 1000.0f;
    EXPECT_FLOAT_EQ(HdrDisplayOutput::rollOff(0.0f, kPeak), 0.0f);
    float previous = 0.0f;
    for (const float nits : {1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f})
    {
        const float rolled = HdrDisplayOutput::rollOff(nits, kPeak);
        EXPECT_GT(rolled, previous) << "the roll-off is not monotonic at " << nits;
        EXPECT_LT(rolled, kPeak) << "the roll-off exceeded the peak at " << nits;
        previous = rolled;
    }
    // Well below the peak it has to be nearly the identity, or it would darken the whole frame to
    // protect a highlight that was never there.
    EXPECT_NEAR(HdrDisplayOutput::rollOff(10.0f, kPeak), 10.0f, 0.2f);
}

TEST(HdrDisplayOutputTest, TheSettingsRoundTripAndKeepPeakAbovePaperWhite)
{
    GraphicsDevice device;
    HdrDisplayOutput output(device);
    EXPECT_EQ(output.getColorSpace(), DisplayColorSpace::Srgb);

    output.setPaperWhiteNits(250.0f);
    EXPECT_FLOAT_EQ(output.getPaperWhiteNits(), 250.0f);
    output.setPaperWhiteNits(-5.0f);
    EXPECT_FLOAT_EQ(output.getPaperWhiteNits(), 1.0f);

    // A peak below paper white would mean diffuse white is brighter than the brightest thing the
    // display can show, which is not a setting so much as a contradiction.
    output.setPaperWhiteNits(200.0f);
    output.setPeakNits(50.0f);
    EXPECT_FLOAT_EQ(output.getPeakNits(), 200.0f);
    output.setPeakNits(4000.0f);
    EXPECT_FLOAT_EQ(output.getPeakNits(), 4000.0f);

    output.setColorSpace(DisplayColorSpace::Hdr10);
    EXPECT_EQ(output.getColorSpace(), DisplayColorSpace::Hdr10);
}

TEST(HdrDisplayOutputTest, InSrgbTheEncodeIsTheIdentity)
{
    const Vector3 colour(0.25f, 0.5f, 0.75f);
    const Vector3 encoded = HdrDisplayOutput::encode(DisplayColorSpace::Srgb, colour, 200.0f,
                                                     1000.0f);
    EXPECT_FLOAT_EQ(encoded.X, colour.X);
    EXPECT_FLOAT_EQ(encoded.Y, colour.Y);
    EXPECT_FLOAT_EQ(encoded.Z, colour.Z);
}

TEST(HdrDisplayOutputTest, DrawingRefusesWhatItCannotEncode)
{
    GraphicsDevice device;
    HdrDisplayOutput output(device);
    const auto source = MakeSource(device, SourceTexels());
    EXPECT_THROW(output.draw(nullptr, nullptr, kSize, kSize), std::invalid_argument);
    EXPECT_THROW(output.draw(source.get(), nullptr, 0, kSize), std::invalid_argument);
    EXPECT_THROW(output.draw(source.get(), nullptr, kSize, -2), std::invalid_argument);
}

TEST(HdrDisplayOutputTest, SdrOutputIsTheFrameThePipelineAlreadyProduced)
{
    GraphicsDevice device;
    HdrDisplayOutput output(device);
    if (!output.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    const std::vector<Color> texels = SourceTexels();
    const std::vector<Color> result = Encode(device, output, texels);
    ASSERT_EQ(result.size(), texels.size());
    for (std::size_t i = 0; i < texels.size(); ++i)
    {
        EXPECT_EQ(result[i].getRProperty(), texels[i].getRProperty()) << "at texel " << i;
        EXPECT_EQ(result[i].getGProperty(), texels[i].getGProperty()) << "at texel " << i;
        EXPECT_EQ(result[i].getBProperty(), texels[i].getBProperty()) << "at texel " << i;
    }
}

TEST(HdrDisplayOutputTest, TheShaderAndTheCpuEncodeAgreeInEveryHdrSpace)
{
    GraphicsDevice device;
    HdrDisplayOutput output(device);
    if (!output.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    const std::vector<Color> texels = SourceTexels();
    for (const DisplayColorSpace space : {DisplayColorSpace::Scrgb, DisplayColorSpace::Hdr10})
    {
        output.setColorSpace(space);
        // scRGB scales by paperWhite/80 and the destination here is an 8-bit target, so that
        // space is given a paper white of 40 nits: it keeps the result inside [0, 1] while staying
        // a real scale rather than the identity, which 80 nits would have been -- and an identity
        // would make the agreement below true for a pass that did nothing at all.
        output.setPaperWhiteNits(space == DisplayColorSpace::Scrgb ? 40.0f : 200.0f);
        output.setPeakNits(1000.0f);

        const std::vector<Color> result = Encode(device, output, texels);
        ASSERT_EQ(result.size(), texels.size());

        int differing = 0;
        for (std::size_t i = 0; i < texels.size(); ++i)
        {
            const Vector3 scene(static_cast<float>(texels[i].getRProperty()) / 255.0f,
                                static_cast<float>(texels[i].getGProperty()) / 255.0f,
                                static_cast<float>(texels[i].getBProperty()) / 255.0f);
            const Vector3 expected = HdrDisplayOutput::encode(space, scene,
                                                              output.getPaperWhiteNits(),
                                                              output.getPeakNits());
            EXPECT_NEAR(static_cast<float>(result[i].getRProperty()) / 255.0f, expected.X, 0.02f)
                << "space " << static_cast<int>(space) << ", texel " << i;
            EXPECT_NEAR(static_cast<float>(result[i].getGProperty()) / 255.0f, expected.Y, 0.02f)
                << "space " << static_cast<int>(space) << ", texel " << i;
            EXPECT_NEAR(static_cast<float>(result[i].getBProperty()) / 255.0f, expected.Z, 0.02f)
                << "space " << static_cast<int>(space) << ", texel " << i;
            if (result[i].getRProperty() != texels[i].getRProperty()) ++differing;
        }
        // A pass that quietly copied through would satisfy an agreement test only if the encoding
        // were the identity, which neither of these is.
        EXPECT_GT(differing, 0) << "space " << static_cast<int>(space)
                                << " left the frame untouched";
    }
}

} // namespace

#endif // CNA_CNAEXT
