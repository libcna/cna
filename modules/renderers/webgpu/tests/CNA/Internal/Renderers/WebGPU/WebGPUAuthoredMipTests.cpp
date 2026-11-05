// SPDX-License-Identifier: MS-PL
// GAP-15: ordinary textures keep independently authored levels across level-zero writes.
// XNA 4.0 Refresh Graphics: Texture2D.SetData<T> IL_0000..000f / CopyData<T>
// IL_0390..03df; TextureCube.SetData<T> IL_0000..0011 / CopyData<T> IL_0397..03e9.
// Both lock/copy/unlock only the requested level (and cube face); neither generates a chain.

#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)

#include <algorithm>
#include <any>
#include <array>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    // Literal authored RGBA bytes, independent of renderer output. 8 -> 4 -> 2 -> 1.
    const std::array<Color, 4> kLevels{{Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                      Color(0, 0, 255, 255), Color(255, 255, 0, 255)}};
    const Color kReplacement(255, 0, 255, 255);

    GraphicsDevice MakeMipDevice()
    {
        auto p = PresentationParameters::CreateWindowedEXT(32, 32);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, p);
    }

    std::vector<std::uint8_t> SolidBytes(int edge, const Color& color)
    {
        std::vector<std::uint8_t> bytes;
        for (int i = 0; i < edge * edge; ++i)
        {
            bytes.push_back(color.getRProperty());
            bytes.push_back(color.getGProperty());
            bytes.push_back(color.getBProperty());
            bytes.push_back(color.getAProperty());
        }
        return bytes;
    }

    void Author2D(Texture2D& texture)
    {
        for (int level = 0; level < 4; ++level)
        {
            const auto bytes = SolidBytes(8 >> level, kLevels[level]);
            texture.SetData(level, nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
        }
    }

    // Use GPU readback explicitly: public Texture2D.GetData can return an authored CPU shadow
    // even when the GPU level has been overwritten by implicit mip generation.
    void ExpectGpu2D(Texture2D& texture, int level, const std::vector<std::uint8_t>& expected)
    {
        const int edge = 8 >> level;
        std::vector<std::uint8_t> actual(expected.size(), 0xCD);
        ASSERT_TRUE(texture.GetRenderer().GetData(level, 0, 0, edge, edge, actual.data(),
                                                 static_cast<int>(actual.size())));
        EXPECT_EQ(actual, expected) << "GPU mip " << level;
    }

    void ExpectLower2D(Texture2D& texture)
    {
        for (int level = 1; level < 4; ++level)
            ExpectGpu2D(texture, level, SolidBytes(8 >> level, kLevels[level]));
    }

    void ExpectSampled2D(GraphicsDevice& device, Texture2D& texture,
                         const std::array<Color, 4>& expected)
    {
        RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None,
                              0, RenderTargetUsage::PreserveContents);
        SpriteBatch batch(device);
        for (int level = 0; level < 4; ++level)
        {
            // Point filtering, 1:1 8x8 draw => lambda=log2(8/8)=0. MaxMipLevel clamps
            // the most detailed level to `level`, so this samples exactly that authored mip.
            SamplerState sampler;
            sampler.setFilterProperty(TextureFilter::Point);
            sampler.setMaxMipLevelProperty(level);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &sampler, nullptr, nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, 8, 8), Color::White);
            batch.End();
            device.SetRenderTarget(nullptr);
            const Rectangle center(4, 4, 1, 1);
            Color pixel = Color::Transparent;
            target.GetData(0, &center, &pixel, 0, 1);
            EXPECT_EQ(pixel, expected[level]) << "sampled mip " << level;
        }
    }

    Color CubeColor(int face, int level)
    {
        // All 24 face/level pairs differ: R=16+32*face, G=16+48*level, B=240-16*face.
        return Color(16 + 32 * face, 16 + 48 * level, 240 - 16 * face, 255);
    }

    void AuthorCube(TextureCube& cube)
    {
        for (int face = 0; face < 6; ++face)
            for (int level = 0; level < 4; ++level)
            {
                const auto bytes = SolidBytes(8 >> level, CubeColor(face, level));
                cube.SetData(static_cast<CubeMapFace>(face), level, nullptr, bytes.data(), 0,
                             static_cast<int>(bytes.size()));
            }
    }

    void ExpectGpuCube(TextureCube& cube, int face, int level,
                        const std::vector<std::uint8_t>& expected)
    {
        const int edge = 8 >> level;
        std::vector<std::uint8_t> actual(expected.size(), 0xCD);
        ASSERT_TRUE(cube.GetRenderer().GetData(face, level, 0, 0, edge, edge, actual.data(),
                                               static_cast<int>(actual.size())));
        EXPECT_EQ(actual, expected) << "face " << face << ", GPU mip " << level;
    }

    void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    std::vector<std::uint8_t> XnbTextureBody(bool cube)
    {
        // XNA Refresh Texture2DReader.Read IL_0000..0067 / TextureCubeReader.Read
        // IL_0000..006d: Color=0; width[,height]; count; length+RGBA bytes per level,
        // cube ordered face-major. Payload sizes: 256,64,16,4; no CNA writer as oracle.
        std::vector<std::uint8_t> bytes;
        AppendU32(bytes, 0);
        AppendU32(bytes, 8);
        if (!cube) AppendU32(bytes, 8);
        AppendU32(bytes, 4);
        for (int face = 0; face < (cube ? 6 : 1); ++face)
            for (int level = 0; level < 4; ++level)
            {
                const auto pixels = SolidBytes(8 >> level,
                                               cube ? CubeColor(face, level) : kLevels[level]);
                AppendU32(bytes, static_cast<std::uint32_t>(pixels.size()));
                bytes.insert(bytes.end(), pixels.begin(), pixels.end());
            }
        return bytes;
    }
}

TEST(WebGPUAuthoredMips, FullLevelZeroWritePreservesEveryLowerLevel)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    auto device = MakeMipDevice();
    auto& renderer = dynamic_cast<WebGPURenderer&>(device.GetRenderer());
    const auto errors = renderer.GetUncapturedErrorCountEXT();
    // Both explicit-level public upload paths must preserve authored levels.
    for (bool colors : {false, true})
    {
        SCOPED_TRACE(colors ? "Color elements" : "byte elements");
        Texture2D texture(device, 8, 8, true, SurfaceFormat::Color);
        Author2D(texture);
        ExpectLower2D(texture);
        if (colors)
        {
            const std::vector<Color> pixels(64, kReplacement);
            texture.SetData(0, nullptr, pixels.data(), 0, 64);
        }
        else
        {
            const auto bytes = SolidBytes(8, kReplacement);
            texture.SetData(0, nullptr, bytes.data(), 0, static_cast<int>(bytes.size()));
        }
        ExpectGpu2D(texture, 0, SolidBytes(8, kReplacement));
        ExpectLower2D(texture);
        auto expected = kLevels;
        expected[0] = kReplacement;
        ExpectSampled2D(device, texture, expected);
    }
    EXPECT_EQ(renderer.GetUncapturedErrorCountEXT(), errors);
}

TEST(WebGPUAuthoredMips, PartialLevelZeroWritePreservesLowerLevelsAndUntouchedTexels)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    auto device = MakeMipDevice();
    Texture2D texture(device, 8, 8, true, SurfaceFormat::Color);
    Author2D(texture);
    const Rectangle region(2, 3, 2, 2);
    const std::vector<Color> patch(4, kReplacement);
    texture.SetData(0, &region, patch.data(), 0, 4);
    auto expected = SolidBytes(8, kLevels[0]);
    const auto pixel = SolidBytes(1, kReplacement);
    for (int y = 3; y < 5; ++y)
        for (int x = 2; x < 4; ++x)
            std::copy(pixel.begin(), pixel.end(), expected.begin() + (y * 8 + x) * 4);
    ExpectGpu2D(texture, 0, expected);
    ExpectLower2D(texture);
    // Sample each unaffected mip; level zero is checked texel-for-texel above.
    auto sampleExpected = kLevels;
    // The center sample (4,4) is outside [2,4)x[3,5).
    ExpectSampled2D(device, texture, sampleExpected);
}

TEST(WebGPUAuthoredMips, CubeFullAndPartialWritesPreserveEveryFaceAndLowerLevel)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    auto device = MakeMipDevice();
    for (bool partial : {false, true})
        for (int changed = 0; changed < 6; ++changed)
        {
            SCOPED_TRACE(::testing::Message() << "changed face " << changed << ", partial " << partial);
            TextureCube cube(device, 8, true, SurfaceFormat::Color);
            AuthorCube(cube);
            const Rectangle region(2, 3, 2, 2);
            const auto patch = SolidBytes(partial ? 2 : 8, kReplacement);
            cube.SetData(static_cast<CubeMapFace>(changed), 0, partial ? &region : nullptr,
                         patch.data(), 0, static_cast<int>(patch.size()));
            for (int face = 0; face < 6; ++face)
                for (int level = 0; level < 4; ++level)
                {
                    auto expected = SolidBytes(8 >> level, CubeColor(face, level));
                    if (face == changed && level == 0)
                    {
                        if (!partial) expected = patch;
                        else
                        {
                            const auto pixel = SolidBytes(1, kReplacement);
                            for (int y = 3; y < 5; ++y)
                                for (int x = 2; x < 4; ++x)
                                    std::copy(pixel.begin(), pixel.end(), expected.begin() + (y * 8 + x) * 4);
                        }
                    }
                    ExpectGpuCube(cube, face, level, expected);
                }
        }
}

TEST(WebGPUAuthoredMips, XnbTexture2DLevelsSurviveLaterBaseUpload)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    auto device = MakeMipDevice();
    Microsoft::Xna::Framework::Content::ContentManager manager;
    manager.setGraphicsDevice(device);
    const auto bytes = XnbTextureBody(false);
    System::IO::MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()), false);
    Microsoft::Xna::Framework::Content::ContentReader input(&manager, &stream, "gap15-2d", 5, 'w');
    CNA::Internal::Xnb::Texture2DReader typeReader;
    auto texture = std::any_cast<Texture2D>(typeReader.ReadUntyped(input, std::any{}));
    ASSERT_EQ(texture.getLevelCountProperty(), 4);
    ExpectGpu2D(texture, 0, SolidBytes(8, kLevels[0]));
    ExpectLower2D(texture);
    const auto replacement = SolidBytes(8, kReplacement);
    texture.SetData(0, nullptr, replacement.data(), 0, static_cast<int>(replacement.size()));
    ExpectGpu2D(texture, 0, replacement);
    ExpectLower2D(texture);
    auto expected = kLevels;
    expected[0] = kReplacement;
    ExpectSampled2D(device, texture, expected);
}

TEST(WebGPUAuthoredMips, XnbTextureCubeLevelsSurviveLaterFaceUpload)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    auto device = MakeMipDevice();
    Microsoft::Xna::Framework::Content::ContentManager manager;
    manager.setGraphicsDevice(device);
    const auto bytes = XnbTextureBody(true);
    System::IO::MemoryStream stream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()), false);
    Microsoft::Xna::Framework::Content::ContentReader input(&manager, &stream, "gap15-cube", 5, 'w');
    CNA::Internal::Xnb::TextureCubeReader typeReader;
    auto cube = std::any_cast<TextureCube>(typeReader.ReadUntyped(input, std::any{}));
    ASSERT_EQ(cube.getLevelCountProperty(), 4);
    for (int face = 0; face < 6; ++face)
        for (int level = 0; level < 4; ++level)
            ExpectGpuCube(cube, face, level, SolidBytes(8 >> level, CubeColor(face, level)));
    const auto replacement = SolidBytes(8, kReplacement);
    cube.SetData(CubeMapFace::NegativeZ, 0, nullptr, replacement.data(), 0,
                 static_cast<int>(replacement.size()));
    for (int face = 0; face < 6; ++face)
        for (int level = 0; level < 4; ++level)
            ExpectGpuCube(cube, face, level,
                          face == 5 && level == 0 ? replacement
                              : SolidBytes(8 >> level, CubeColor(face, level)));
}

#endif
