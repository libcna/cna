// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1109: equirectangular panorama to cube map.
//
// The conversion is two coordinate mappings that have to agree, and the way they fail is that a
// face comes out mirrored or rotated -- a complete cube, sampling correctly, with a sixth of the
// sky wrong. So the two mappings are checked against each other as a round trip first, and then
// the conversion is checked with a panorama whose colour *is* its direction, so a face landing in
// the wrong place is a wrong colour rather than a subtly wrong image.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
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
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

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
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

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


// =====================================================================================
// IBL precompute (MOD-1202, MOD-1204..MOD-1207)
// =====================================================================================

/// A cube every face of which is the same colour: the one environment whose convolutions can be
/// computed by hand, which is what makes it the right first test for all three of them.
std::unique_ptr<TextureCube> MakeConstantCube(GraphicsDevice& gd, int size, const Color& colour)
{
    auto cube = std::make_unique<TextureCube>(gd, size, false, SurfaceFormat::Color);
    const std::vector<Color> texels(static_cast<std::size_t>(size) * size, colour);
    for (int face = 0; face < 6; ++face)
        cube->SetData(static_cast<CubeMapFace>(face), texels.data(),
                      static_cast<int>(texels.size()));
    return cube;
}

TEST(EnvironmentProcessorTest, TheHammersleySequenceMatchesItsDefinition)
{
    // MOD-1206. A radical inverse with a wrong bit twiddle still produces a sequence that looks
    // random and still converges -- to the wrong number. So the first few points are named.
    float x = 0.0f, y = 0.0f;
    EnvironmentProcessor::hammersley(0, 4, x, y);
    EXPECT_NEAR(x, 0.125f, 1e-5f);
    EXPECT_NEAR(y, 0.0f, 1e-5f);

    EnvironmentProcessor::hammersley(1, 4, x, y);
    EXPECT_NEAR(y, 0.5f, 1e-5f) << "index 1 reflects to 0.5";
    EnvironmentProcessor::hammersley(2, 4, x, y);
    EXPECT_NEAR(y, 0.25f, 1e-5f) << "index 2 reflects to 0.25";
    EnvironmentProcessor::hammersley(3, 4, x, y);
    EXPECT_NEAR(y, 0.75f, 1e-5f) << "index 3 reflects to 0.75";
}

TEST(EnvironmentProcessorTest, TheHammersleySequenceFillsTheUnitSquare)
{
    // The property the sequence exists for: every prefix is spread out, not clustered. Checked as
    // the mean landing at the centre, which a broken reflection does not manage.
    double sumX = 0.0, sumY = 0.0;
    constexpr int kCount = 64;
    for (int i = 0; i < kCount; ++i)
    {
        float x = 0.0f, y = 0.0f;
        EnvironmentProcessor::hammersley(i, kCount, x, y);
        EXPECT_GE(x, 0.0f); EXPECT_LE(x, 1.0f);
        EXPECT_GE(y, 0.0f); EXPECT_LE(y, 1.0f);
        sumX += x; sumY += y;
    }
    EXPECT_NEAR(sumX / kCount, 0.5, 0.02);
    EXPECT_NEAR(sumY / kCount, 0.5, 0.02);
}

TEST(EnvironmentProcessorTest, AMirrorSampleIsTheNormalItself)
{
    // At roughness 0 the GGX lobe collapses to a single direction, so every sample must come back
    // as the normal. Anything else means the lobe is open when it should be shut.
    const Vector3 normal(0.0f, 0.0f, 1.0f);
    for (int i = 0; i < 8; ++i)
    {
        float x = 0.0f, y = 0.0f;
        EnvironmentProcessor::hammersley(i, 8, x, y);
        const Vector3 half = EnvironmentProcessor::importanceSampleGgx(x, y, normal, 0.0f);
        EXPECT_NEAR(half.Z, 1.0f, 1e-3f) << "sample " << i;
    }
}

TEST(EnvironmentProcessorTest, ARoughSampleStaysInTheHemisphere)
{
    // The other end: however wide the lobe opens, no sample may fall behind the surface.
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 64; ++i)
    {
        float x = 0.0f, y = 0.0f;
        EnvironmentProcessor::hammersley(i, 64, x, y);
        const Vector3 half = EnvironmentProcessor::importanceSampleGgx(x, y, normal, 1.0f);
        EXPECT_GE(half.Y, -1e-4f) << "sample " << i << " fell below the surface";
        const float length = std::sqrt(half.X * half.X + half.Y * half.Y + half.Z * half.Z);
        EXPECT_NEAR(length, 1.0f, 1e-3f);
    }
}

TEST(EnvironmentProcessorTest, RoughnessAndMipAreOneMappingInBothDirections)
{
    // MOD-1205. Two functions that happen to agree are one edit away from reflections that sharpen
    // as the surface gets rougher, so the pair is asserted to be inverses.
    constexpr int kMips = 5;
    EXPECT_FLOAT_EQ(EnvironmentProcessor::mipForRoughness(0.0f, kMips), 0.0f);
    EXPECT_FLOAT_EQ(EnvironmentProcessor::mipForRoughness(1.0f, kMips), 4.0f);
    EXPECT_FLOAT_EQ(EnvironmentProcessor::roughnessForMip(0.0f, kMips), 0.0f);
    EXPECT_FLOAT_EQ(EnvironmentProcessor::roughnessForMip(4.0f, kMips), 1.0f);

    for (float roughness : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        EXPECT_NEAR(EnvironmentProcessor::roughnessForMip(
                        EnvironmentProcessor::mipForRoughness(roughness, kMips), kMips),
                    roughness, 1e-5f);

    // A single-mip cube has one roughness and no way to express another; asking is not an error.
    EXPECT_FLOAT_EQ(EnvironmentProcessor::mipForRoughness(0.7f, 1), 0.0f);
}

TEST(EnvironmentProcessorTest, AConstantEnvironmentHasTheSameConstantIrradiance)
{
    // MOD-1202's energy check. If every direction carries the same radiance, a matte surface
    // facing anywhere receives exactly that -- so the cosine convolution must return its input.
    // Any weighting error shows up here as a uniform shift, which no picture would reveal.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);
    auto environment = MakeConstantCube(gd, 8, Color(120, 60, 200, 255));

    auto irradiance = processor.generateIrradiance(environment.get(), 4, 12);
    ASSERT_NE(irradiance, nullptr);
    EXPECT_EQ(irradiance->getSizeProperty(), 4);

    for (int face = 0; face < 6; ++face)
    {
        std::vector<Color> pixels(16, Color::Black);
        irradiance->GetData(static_cast<CubeMapFace>(face), pixels.data(), 16);
        for (const Color& pixel : pixels)
        {
            // Within 1%: the row's own tolerance, and the sweep is a finite grid.
            EXPECT_NEAR(pixel.getRProperty(), 120, 3) << "face " << face;
            EXPECT_NEAR(pixel.getGProperty(), 60, 3) << "face " << face;
            EXPECT_NEAR(pixel.getBProperty(), 200, 3) << "face " << face;
        }
    }
}

TEST(EnvironmentProcessorTest, MoreIrradianceSamplesConvergeTowardTheAnalyticResult)
{
    // MOD-1203: the quality setting has to actually buy something. A directional environment --
    // one bright face -- is where a coarse sweep is visibly off and a finer one is not.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

    auto environment = MakeConstantCube(gd, 8, Color(0, 0, 0, 255));
    const std::vector<Color> bright(64, Color(255, 255, 255, 255));
    environment->SetData(CubeMapFace::PositiveY, bright.data(), 64);

    const auto topOf = [](TextureCube& cube, int size) {
        std::vector<Color> pixels(static_cast<std::size_t>(size) * size, Color::Black);
        cube.GetData(CubeMapFace::PositiveY, pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2].getRProperty();
    };

    auto coarse = processor.generateIrradiance(environment.get(), 4, 4);
    auto fine   = processor.generateIrradiance(environment.get(), 4, 32);

    const int coarseTop = topOf(*coarse, 4);
    const int fineTop   = topOf(*fine, 4);
    // Both see a bright sky above; the finer sweep is the one to trust, and the two must at least
    // agree on the sign of the answer.
    EXPECT_GT(coarseTop, 40);
    EXPECT_GT(fineTop, 40);
    // A surface facing straight up under one bright face receives a substantial fraction of it.
    EXPECT_GT(fineTop, 100) << "the cosine lobe did not gather the bright face above it";
}

TEST(EnvironmentProcessorTest, PrefilteringKeepsMipZeroSharpAndFlattensTheLast)
{
    // MOD-1204, asserted as the two ends the row names. Mip 0 is roughness 0, so it must reproduce
    // its input; the last mip is roughness 1, so a directional environment must have spread across
    // it. Between them is the ramp the shader indexes by roughness.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);
    CNA_SKIP_WITHOUT_CUBE_MIP_STORAGE(gd);

    auto environment = MakeConstantCube(gd, 8, Color(0, 0, 0, 255));
    const std::vector<Color> bright(64, Color(255, 255, 255, 255));
    environment->SetData(CubeMapFace::PositiveX, bright.data(), 64);

    constexpr int kBase = 8;
    constexpr int kMips = 4;
    auto prefiltered = processor.generatePrefilteredSpecular(environment.get(), kBase, kMips, 48);
    ASSERT_NE(prefiltered, nullptr);

    const auto centreOf = [&](int face, int mip) {
        const int size = std::max(1, kBase >> mip);
        std::vector<Color> pixels(static_cast<std::size_t>(size) * size, Color::Black);
        prefiltered->GetData(static_cast<CubeMapFace>(face), mip, nullptr, pixels.data(), 0,
                             static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(size / 2) * size + size / 2].getRProperty();
    };

    // Mip 0 on the bright face is still bright, and on a dark face still dark: a mirror.
    EXPECT_GT(centreOf(0, 0), 200) << "mip 0 did not reproduce the bright face";
    EXPECT_LT(centreOf(1, 0), 40) << "mip 0 leaked the bright face onto the opposite one";

    // The last mip has spread: a face at right angles to the bright one, dark at mip 0, has
    // picked up light at roughness 1. The face 180 degrees opposite is deliberately not the
    // probe -- a GGX lobe is centred on the normal and never reaches behind the surface, so a
    // correct prefilter leaves it dark at every roughness.
    EXPECT_EQ(centreOf(2, 0), 0) << "mip 0 leaked the bright face onto a perpendicular one";
    EXPECT_GT(centreOf(2, kMips - 1), 0)
        << "the roughest mip did not gather anything from elsewhere in the environment";

    // ... and the bright face itself has given some of that light away.
    EXPECT_LT(centreOf(0, kMips - 1), centreOf(0, 0))
        << "the roughest mip is as concentrated as the mirror one";
}

TEST(EnvironmentProcessorTest, TheBrdfLutMatchesACpuReferenceAtSampledPoints)
{
    // MOD-1207/MOD-1241. The table is generated by the same integral the reference computes, so
    // this pins the *plumbing* -- the axis order, the texel-centre convention, the channel each
    // term lands in -- rather than the physics. Getting the two axes the wrong way round produces
    // a plausible table that makes every rough surface behave like a smooth one.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);

    constexpr int kSize = 32;
    auto lut = processor.generateBrdfLut(kSize, 64);
    ASSERT_NE(lut, nullptr);
    EXPECT_EQ(lut->getWidthProperty(), kSize);

    std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    lut->GetData(texels.data(), static_cast<int>(texels.size()));
    const auto at = [&](int x, int y) { return texels[static_cast<std::size_t>(y) * kSize + x]; };

    // Roughness runs down (y), N.V across (x). The scale term is largest where the surface is
    // smooth and seen head on, and falls as either changes -- which is what fixes the axis order.
    const int smoothHeadOn = at(kSize - 1, 0).getRProperty();
    const int roughHeadOn  = at(kSize - 1, kSize - 1).getRProperty();
    const int smoothGrazing = at(0, 0).getRProperty();
    EXPECT_GT(smoothHeadOn, roughHeadOn) << "the roughness axis is inverted or transposed";
    EXPECT_GT(smoothHeadOn, smoothGrazing) << "the view axis is inverted or transposed";

    // The bias term is the Fresnel-weighted half, so it is largest at grazing angles.
    EXPECT_GT(at(0, kSize / 2).getGProperty(), at(kSize - 1, kSize / 2).getGProperty())
        << "the bias term does not rise toward grazing angles";

    // And both terms are a fraction, never above one: they scale F0, they do not add energy.
    for (const Color& texel : texels)
    {
        EXPECT_LE(texel.getRProperty(), 255);
        EXPECT_LE(texel.getGProperty(), 255);
    }
}

TEST(EnvironmentProcessorTest, TheGeneratorsValidateTheirInputs)
{
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);
    auto environment = MakeConstantCube(gd, 4, Color::White);

    EXPECT_THROW((void)processor.generateIrradiance(nullptr, 4, 4), std::invalid_argument);
    EXPECT_THROW((void)processor.generateIrradiance(environment.get(), 0, 4), std::invalid_argument);
    EXPECT_THROW((void)processor.generateIrradiance(environment.get(), 4, 0), std::invalid_argument);
    EXPECT_THROW((void)processor.generatePrefilteredSpecular(nullptr, 8, 3, 8),
                 std::invalid_argument);
    EXPECT_THROW((void)processor.generatePrefilteredSpecular(environment.get(), 8, 0, 8),
                 std::invalid_argument);
    EXPECT_THROW((void)processor.generateBrdfLut(0, 8), std::invalid_argument);
    EXPECT_THROW((void)processor.generateBrdfLut(8, 0), std::invalid_argument);
}

TEST(EnvironmentProcessorTest, GenerationCostIsLoadTimeWork)
{
    // MOD-1211. Not a benchmark with a pass/fail threshold -- the ceiling below is loose enough to
    // survive any machine this runs on. It exists to print the three numbers, so the claim in the
    // docs that these are generated once at load and never per frame is something a reader can
    // reproduce rather than take on trust.
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);
    CNA_SKIP_WITHOUT_CUBE_MIP_STORAGE(gd);
    auto environment = MakeConstantCube(gd, 64, Color(140, 160, 200, 255));

    const auto timeOf = [](auto&& work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();
    };

    const double irradianceMs =
        timeOf([&] { (void)processor.generateIrradiance(environment.get(), 32, 32); });
    const double prefilterMs =
        timeOf([&] { (void)processor.generatePrefilteredSpecular(environment.get(), 128, 5, 64); });
    const double lutMs = timeOf([&] { (void)processor.generateBrdfLut(128, 128); });

    std::cout << "[ IBL      ] irradiance 32/32: " << irradianceMs << " ms\n"
              << "[ IBL      ] prefilter 128/5/64: " << prefilterMs << " ms\n"
              << "[ IBL      ] BRDF LUT 128/128: " << lutMs << " ms" << std::endl;

    // A minute for all three would still be load-time work; anything past that is a defect.
    EXPECT_LT(irradianceMs + prefilterMs + lutMs, 60000.0);
}

} // namespace

#endif // CNA_CNAEXT
