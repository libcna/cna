// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1109: equirectangular panorama to cube map.
//
// The conversion is two coordinate mappings that have to agree, and the way they fail is that a
// face comes out mirrored or rotated -- a complete cube, sampling correctly, with a sixth of the
// sky wrong. So the two mappings are checked against each other as a round trip first, and then
// the conversion is checked with a panorama whose colour *is* its direction, so a face landing in
// the wrong place is a wrong colour rather than a subtly wrong image.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::EnvironmentProcessor;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

TEST(EnvironmentProcessorTest, TheTwoMappingsAreInverses)
{
    // The property that makes the conversion correct at all: taking a face texel to a direction
    // and the direction back to a panorama coordinate must land where the direction actually is.
    // Checked as a round trip through the direction, since that is the shared value.
    for (int face = 0; face < 6; ++face)
        for (float u : {0.1f, 0.5f, 0.9f})
            for (float v : {0.1f, 0.5f, 0.9f})
            {
                const Vector3 direction = EnvironmentProcessor::faceDirection(face, u, v);
                float su = 0.0f, sv = 0.0f;
                EnvironmentProcessor::directionToEquirectangular(direction, su, sv);

                // Recovering the direction from the panorama coordinate: longitude and latitude
                // back to a unit vector, by the same convention.
                const float longitude = (su - 0.5f) * 2.0f * MathHelper::Pi;
                const float latitude  = (0.5f - sv) * MathHelper::Pi;
                const Vector3 back(std::cos(latitude) * std::sin(longitude),
                                   std::sin(latitude),
                                   -std::cos(latitude) * std::cos(longitude));

                EXPECT_NEAR(back.X, direction.X, 1e-4f) << "face " << face << " (" << u << "," << v << ")";
                EXPECT_NEAR(back.Y, direction.Y, 1e-4f) << "face " << face;
                EXPECT_NEAR(back.Z, direction.Z, 1e-4f) << "face " << face;
            }
}

TEST(EnvironmentProcessorTest, EachFaceCentreLooksDownItsOwnAxis)
{
    // The centre of face N must look along axis N. A face rotated 90 degrees still passes a round
    // trip -- its centre is the only texel that pins the orientation.
    struct Expectation { int face; Vector3 axis; const char* name; };
    const Expectation expectations[] = {
        {0, Vector3( 1.0f,  0.0f,  0.0f), "+X"},
        {1, Vector3(-1.0f,  0.0f,  0.0f), "-X"},
        {2, Vector3( 0.0f,  1.0f,  0.0f), "+Y"},
        {3, Vector3( 0.0f, -1.0f,  0.0f), "-Y"},
        {4, Vector3( 0.0f,  0.0f,  1.0f), "+Z"},
        {5, Vector3( 0.0f,  0.0f, -1.0f), "-Z"},
    };
    for (const Expectation& e : expectations)
    {
        const Vector3 centre = EnvironmentProcessor::faceDirection(e.face, 0.5f, 0.5f);
        EXPECT_NEAR(centre.X, e.axis.X, 1e-4f) << "face " << e.name;
        EXPECT_NEAR(centre.Y, e.axis.Y, 1e-4f) << "face " << e.name;
        EXPECT_NEAR(centre.Z, e.axis.Z, 1e-4f) << "face " << e.name;
    }
}

TEST(EnvironmentProcessorTest, TheCentreOfAPanoramaIsStraightAhead)
{
    // The convention this file commits to, stated as a test so it cannot drift: the middle of a
    // panorama is -Z, which is where a camera at its default orientation is looking.
    float u = 0.0f, v = 0.0f;
    EnvironmentProcessor::directionToEquirectangular(Vector3(0.0f, 0.0f, -1.0f), u, v);
    EXPECT_NEAR(u, 0.5f, 1e-4f);
    EXPECT_NEAR(v, 0.5f, 1e-4f);

    EnvironmentProcessor::directionToEquirectangular(Vector3(0.0f, 1.0f, 0.0f), u, v);
    EXPECT_NEAR(v, 0.0f, 1e-4f) << "straight up should be the top row";

    EnvironmentProcessor::directionToEquirectangular(Vector3(0.0f, -1.0f, 0.0f), u, v);
    EXPECT_NEAR(v, 1.0f, 1e-4f) << "straight down should be the bottom row";
}

TEST(EnvironmentProcessorTest, AMarkedPanoramaLandsOnTheExpectedFaces)
{
    // A panorama whose colour encodes its own direction, so a misplaced face is a wrong colour
    // rather than a subtly wrong image. Red rises with longitude, green with latitude.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);

    constexpr int kWidth  = 64;
    constexpr int kHeight = 32;
    Texture2D panorama(gd, kWidth, kHeight);
    std::vector<Color> texels(static_cast<std::size_t>(kWidth) * kHeight, Color::Black);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
            texels[static_cast<std::size_t>(y) * kWidth + x] =
                Color(static_cast<int>(x * 255 / (kWidth - 1)),
                      static_cast<int>(y * 255 / (kHeight - 1)), 0, 255);
    panorama.SetData(texels.data(), static_cast<int>(texels.size()));

    constexpr int kFace = 8;
    auto cube = processor.convertEquirectangular(&panorama, kFace);
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getSizeProperty(), kFace);

    const auto centreOf = [&](int face) {
        std::vector<Color> pixels(static_cast<std::size_t>(kFace) * kFace, Color::Black);
        cube->GetData(static_cast<CubeMapFace>(face), pixels.data(),
                      static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(kFace / 2) * kFace + kFace / 2];
    };

    // +Y is straight up: the top of the panorama, so green is near zero.
    EXPECT_LT(centreOf(2).getGProperty(), 40) << "+Y did not come from the top of the panorama";
    // -Y is straight down: the bottom, so green is near full.
    EXPECT_GT(centreOf(3).getGProperty(), 215) << "-Y did not come from the bottom";
    // The four horizontal faces all come from the middle band.
    for (const int face : {0, 1, 4, 5})
    {
        const int green = centreOf(face).getGProperty();
        EXPECT_GT(green, 100) << "face " << face << " is not on the horizon";
        EXPECT_LT(green, 160) << "face " << face << " is not on the horizon";
    }
    // And they are spread around it: -Z at the centre of the image, +X a quarter to one side,
    // +Z at the wrapped edge, -X a quarter to the other.
    EXPECT_NEAR(centreOf(5).getRProperty(), 128, 12) << "-Z should sit at the panorama's centre";
    EXPECT_NEAR(centreOf(0).getRProperty(), 191, 12) << "+X should sit a quarter along";
    EXPECT_NEAR(centreOf(1).getRProperty(), 64, 12)  << "-X should sit a quarter the other way";
}

TEST(EnvironmentProcessorTest, AConstantPanoramaGivesAConstantCube)
{
    // The energy check a conversion cannot fail quietly: if every direction is the same colour,
    // every texel of every face must be that colour. A sampling bug shows up as an edge or a seam.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);

    Texture2D panorama(gd, 16, 8);
    const std::vector<Color> texels(16 * 8, Color(70, 130, 180, 255));
    panorama.SetData(texels.data(), static_cast<int>(texels.size()));

    auto cube = processor.convertEquirectangular(&panorama, 4);
    for (int face = 0; face < 6; ++face)
    {
        std::vector<Color> pixels(16, Color::Black);
        cube->GetData(static_cast<CubeMapFace>(face), pixels.data(), 16);
        for (const Color& pixel : pixels)
        {
            EXPECT_EQ(pixel.getRProperty(), 70) << "face " << face;
            EXPECT_EQ(pixel.getGProperty(), 130) << "face " << face;
            EXPECT_EQ(pixel.getBProperty(), 180) << "face " << face;
        }
    }
}

TEST(EnvironmentProcessorTest, TheInputsAreValidated)
{
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    Texture2D panorama(gd, 8, 4);

    EXPECT_THROW((void)processor.convertEquirectangular(nullptr, 8), std::invalid_argument);
    EXPECT_THROW((void)processor.convertEquirectangular(&panorama, 0), std::invalid_argument);
    EXPECT_THROW((void)processor.convertEquirectangular(&panorama, -4), std::invalid_argument);
}

} // namespace

#endif // CNA_CNAEXT
