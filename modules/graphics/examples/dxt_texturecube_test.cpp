// SPDX-License-Identifier: MS-PL
// plan_vulkan.md VULKAN-240: compressed TextureCube storage must be a sampled capability, not a
// constructor/property claim. The same source runs on EasyGL and Vulkan. It uploads exact DXT
// blocks through TextureCube's byte overload, reads them back as Color, samples the cube through
// EnvironmentMapEffect, and replaces one block of a larger face.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTargetSize = 32;
    constexpr std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };

    void AppendColorBlock(std::vector<std::uint8_t>& blocks, std::uint16_t color565)
    {
        blocks.push_back(static_cast<std::uint8_t>(color565 & 0xFFu));
        blocks.push_back(static_cast<std::uint8_t>(color565 >> 8u));
        blocks.push_back(0u);
        blocks.push_back(0u);
        blocks.insert(blocks.end(), 4, 0u);
    }

    std::vector<std::uint8_t> SolidBlock(SurfaceFormat format, std::uint16_t color565)
    {
        std::vector<std::uint8_t> blocks;
        if (format == SurfaceFormat::Dxt3)
            blocks.insert(blocks.end(), 8, 0xFFu);
        else if (format == SurfaceFormat::Dxt5)
        {
            blocks.push_back(0xFFu);
            blocks.push_back(0xFFu);
            blocks.insert(blocks.end(), 6, 0u);
        }
        AppendColorBlock(blocks, color565);
        return blocks;
    }

    bool ExactRgb(const Color& value, const Color& expected)
    {
        return value.getRProperty() == expected.getRProperty() &&
               value.getGProperty() == expected.getGProperty() &&
               value.getBProperty() == expected.getBProperty();
    }

    bool CloseRgb(const Color& value, const Color& expected, int tolerance = 8)
    {
        return std::abs(static_cast<int>(value.getRProperty()) -
                        static_cast<int>(expected.getRProperty())) <= tolerance &&
               std::abs(static_cast<int>(value.getGProperty()) -
                        static_cast<int>(expected.getGProperty())) <= tolerance &&
               std::abs(static_cast<int>(value.getBProperty()) -
                        static_cast<int>(expected.getBProperty())) <= tolerance;
    }
}

class DxtTextureCubeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int passed_ = 0;
    int failed_ = 0;
    bool skipped_ = false;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

    static void FillCube(TextureCube& cube, const std::vector<std::uint8_t>& block)
    {
        for (const CubeMapFace face : kFaces)
            cube.SetData(face, block.data(), static_cast<int>(block.size()));
    }

    Color SampleCube(GraphicsDevice& device, TextureCube& cube)
    {
        const Color white(255, 255, 255, 255);
        Texture2D base(device, 1, 1);
        base.SetData(&white, 1);

        RenderTarget2D target(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        EnvironmentMapEffect effect(device);
        effect.setTextureProperty(&base);
        effect.setEnvironmentMapProperty(&cube);
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), normal, Vector2(0.0f, 0.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f,  1.0f, 0.0f), normal, Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, 0.0f), normal, Vector2(1.0f, 0.0f)},
            {Vector3( 1.0f,  1.0f, 0.0f), normal, Vector2(1.0f, 1.0f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color center(0, 0, 0, 0);
        const Rectangle centerRegion(kTargetSize / 2, kTargetSize / 2, 1, 1);
        target.GetData(0, &centerRegion, &center, 0, 1);
        return center;
    }

    void RunFormat(
        GraphicsDevice& device, SurfaceFormat format, std::uint16_t color565,
        const Color& expected, const char* name)
    {
        TextureCube cube(device, 4, false, format);
        const std::vector<std::uint8_t> block = SolidBlock(format, color565);
        FillCube(cube, block);

        std::vector<Color> readback(16, Color(1, 2, 3, 4));
        cube.GetData(CubeMapFace::PositiveX, readback.data(), 16);
        bool readbackMatches = true;
        for (const Color& pixel : readback)
            readbackMatches = readbackMatches && ExactRgb(pixel, expected);
        Check(readbackMatches, std::string(name) +
                                   ": compressed face reads back as the encoded color");

        const Color sampled = SampleCube(device, cube);
        Check(CloseRgb(sampled, expected), std::string(name) +
                                              ": EnvironmentMapEffect samples native cube storage");
    }

    void RunPartialBlockUpdate(GraphicsDevice& device)
    {
        TextureCube cube(device, 8, true, SurfaceFormat::Dxt1);
        const std::vector<std::uint8_t> red = SolidBlock(SurfaceFormat::Dxt1, 0xF800u);
        const std::vector<std::uint8_t> blue = SolidBlock(SurfaceFormat::Dxt1, 0x001Fu);
        std::vector<std::uint8_t> fourRedBlocks;
        for (int i = 0; i < 4; ++i)
            fourRedBlocks.insert(fourRedBlocks.end(), red.begin(), red.end());
        cube.SetData(CubeMapFace::NegativeZ, fourRedBlocks.data(),
                     static_cast<int>(fourRedBlocks.size()));

        const Rectangle lowerRight(4, 4, 4, 4);
        cube.SetData(CubeMapFace::NegativeZ, 0, &lowerRight,
                     blue.data(), 0, static_cast<int>(blue.size()));

        std::vector<Color> readback(64, Color(0, 0, 0, 0));
        cube.GetData(CubeMapFace::NegativeZ, readback.data(), 64);
        bool correct = true;
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                const Color expected = x >= 4 && y >= 4
                    ? Color(0, 0, 255, 255)
                    : Color(255, 0, 0, 255);
                correct = correct && ExactRgb(
                    readback[static_cast<std::size_t>(y * 8 + x)], expected);
            }
        }
        Check(correct, "DXT1: a block-aligned partial update changes only the requested block");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool completed = false;
        if (completed) return;
        completed = true;

        auto& device = getGraphicsDeviceProperty();
        const bool dxt1 = device.GetRenderer().IsCompressedCubeTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt1));
        const bool dxt3 = device.GetRenderer().IsCompressedCubeTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt3));
        const bool dxt5 = device.GetRenderer().IsCompressedCubeTransferFormatEXT(
            static_cast<int>(SurfaceFormat::Dxt5));
        if (!dxt1 || !dxt3 || !dxt5)
        {
            std::printf("[SKIP] the selected device does not expose all three compressed cube formats\n");
            skipped_ = true;
            Exit();
            return;
        }

        RunFormat(device, SurfaceFormat::Dxt1, 0xF800u,
                  Color(255, 0, 0, 255), "DXT1");
        RunFormat(device, SurfaceFormat::Dxt3, 0x07E0u,
                  Color(0, 255, 0, 255), "DXT3");
        RunFormat(device, SurfaceFormat::Dxt5, 0x001Fu,
                  Color(0, 0, 255, 255), "DXT5");
        RunPartialBlockUpdate(device);

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    DxtTextureCubeTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kTargetSize);
        manager_->setPreferredBackBufferHeightProperty(kTargetSize);
    }

    int Result() const
    {
        return skipped_ ? 77 : (failed_ == 0 ? 0 : 1);
    }
};

int main()
{
    DxtTextureCubeTest test;
    test.Run();
    return test.Result();
}
