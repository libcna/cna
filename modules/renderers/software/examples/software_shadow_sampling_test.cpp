// SPDX-License-Identifier: MS-PL
// Deterministic CPU oracle for CNAEXT stock-effect shadow reception.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::BlendWriteState;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::ImageData;
using CNA::Internal::Renderers::ITextureRenderer;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void Check(bool ok, const char* label)
    {
        ++totalCount;
        if (ok) ++passCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
    }

    struct VPNTanT
    {
        float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v;
    };

    constexpr VPNTanT kTriangle[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    };

    struct VPNTanTW
    {
        float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v;
        float w0, w1, w2, w3;
        std::uint8_t indices[4];
    };

    constexpr VPNTanTW kSkinnedTriangle[3] = {
        {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}},
        { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}},
        {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
         1.0f, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0}},
    };

    std::unique_ptr<ITextureRenderer> MakeTexture(
        IGraphicsRenderer& renderer, int width, int height,
        const std::vector<std::uint8_t>& pixels)
    {
        ImageData image;
        image.width = width;
        image.height = height;
        image.pixels = pixels;
        return renderer.CreateTexture(image);
    }

    std::unique_ptr<ITextureRenderer> MakeSolidTexture(
        IGraphicsRenderer& renderer, int width, int height, std::uint8_t value)
    {
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(width * height * 4), 255);
        for (int pixel = 0; pixel < width * height; ++pixel)
        {
            pixels[static_cast<std::size_t>(pixel) * 4 + 0] = value;
            pixels[static_cast<std::size_t>(pixel) * 4 + 1] = value;
            pixels[static_cast<std::size_t>(pixel) * 4 + 2] = value;
        }
        return MakeTexture(renderer, width, height, pixels);
    }

    void SetAmbientOnlyPbr(GpuDrawParams& params, ITextureRenderer* white)
    {
        params.pbr = true;
        params.texture0 = white;
        params.diffuseColor[0] = params.diffuseColor[1] =
            params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
        params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 1.0f;
        params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 0.0f;
        params.light1Diffuse[0] = params.light1Diffuse[1] = params.light1Diffuse[2] = 0.0f;
        params.light2Diffuse[0] = params.light2Diffuse[1] = params.light2Diffuse[2] = 0.0f;
        params.pbrMetallicFactor = 0.0f;
        params.pbrRoughnessFactor = 1.0f;
    }

    void SetDirectPbr(GpuDrawParams& params, ITextureRenderer* white)
    {
        SetAmbientOnlyPbr(params, white);
        params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
        params.eyePositionWorld[2] = 3.0f;
        params.light0Dir[0] = params.light0Dir[1] = 0.0f;
        params.light0Dir[2] = -1.0f;
        params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 6.0f;
    }
}

class SoftwareShadowSamplingTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    int result_ = 1;

    int Shade(IGraphicsRenderer& renderer, GraphicsDevice& device,
              CNA::Internal::Renderers::IVertexBufferRenderer& vertices,
              const GpuDrawParams& params)
    {
        device.Clear(Color::Black);
        renderer.DrawPrimitivesEx(
            vertices, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
            Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, params);
        const Rectangle region(32, 32, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel.getRProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = device.GetRenderer();
        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, BlendWriteState{});
        renderer.ApplyDepthStencilState(
            false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(0, 0, false, 0.0f, 0.0f);
        renderer.ApplySamplerState(0, 1, 1, 1, 1);

        auto vertices = renderer.CreateVertexBuffer(3);
        vertices->SetData(kTriangle, 3, sizeof(VPNTanT));
        auto skinnedVertices = renderer.CreateVertexBuffer(3);
        skinnedVertices->SetData(kSkinnedTriangle, 3, sizeof(VPNTanTW));
        auto white = MakeSolidTexture(renderer, 1, 1, 255);
        auto black = MakeSolidTexture(renderer, 1, 1, 0);

        Check(renderer.SupportsShadowSamplingEXT(),
              "Software advertises the shadow sampling it executes");

        const auto shadeDirectional = [&](ITextureRenderer* map, int radius = 0) {
            GpuDrawParams params;
            SetDirectPbr(params, white.get());
            params.shadowsEnabled = true;
            params.shadowMap = map;
            params.shadowDepthBias = 0.0f;
            params.shadowPcfRadius = radius;
            return Shade(renderer, device, *vertices, params);
        };
        const int directionalLit = shadeDirectional(white.get());
        const int directionalShadow = shadeDirectional(black.get());
        Check(directionalLit > 80 && directionalShadow < 10,
              "PBR directional map attenuates direct light only");

        const auto shadeSkinnedDirectional = [&](ITextureRenderer* map) {
            GpuDrawParams params;
            SetDirectPbr(params, white.get());
            params.skinned = true;
            params.weightsPerVertex = 1;
            params.boneCount = 1;
            const float identity[16] = {
                1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            for (int element = 0; element < 16; ++element)
                params.boneTransforms[element] = identity[element];
            params.shadowsEnabled = true;
            params.shadowMap = map;
            params.shadowDepthBias = 0.0f;
            params.shadowPcfRadius = 0;
            return Shade(renderer, device, *skinnedVertices, params);
        };
        Check(shadeSkinnedDirectional(white.get()) > 80 &&
                  shadeSkinnedDirectional(black.get()) < 10,
              "skinned PBR shadow lookup uses the post-bone world position");

        std::vector<std::uint8_t> pcfPixels(3u * 3u * 4u, 255);
        for (int channel = 0; channel < 3; ++channel)
            pcfPixels[(1u * 3u + 1u) * 4u + static_cast<std::size_t>(channel)] = 0;
        auto pcfMap = MakeTexture(renderer, 3, 3, pcfPixels);
        const int hardShadow = shadeDirectional(pcfMap.get(), 0);
        const int softShadow = shadeDirectional(pcfMap.get(), 1);
        Check(hardShadow < 10 && softShadow > 70,
              "directional PCF radius selects one tap versus a 3x3 neighbourhood");

        std::vector<std::uint8_t> atlasPixels(8u * 4u * 4u, 255);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                for (int channel = 0; channel < 3; ++channel)
                    atlasPixels[(static_cast<std::size_t>(y * 8 + x) * 4u) +
                                static_cast<std::size_t>(channel)] = 0;
        auto atlas = MakeTexture(renderer, 8, 4, atlasPixels);
        const auto shadeCascade = [&](bool secondCascade) {
            GpuDrawParams params;
            SetDirectPbr(params, white.get());
            params.shadowsEnabled = true;
            params.shadowMap = atlas.get();
            params.shadowDepthBias = 0.0f;
            params.shadowPcfRadius = 0;
            params.cascadeCount = 2;
            params.cascadeSplits[0] = secondCascade ? -1.0f : 1.0f;
            params.cascadeSplits[1] = 2.0f;
            for (int cascade = 0; cascade < 2; ++cascade)
            {
                float* matrix = params.cascadeMatricesColMajor + cascade * 16;
                matrix[12] = cascade == 0 ? 0.25f : 0.75f;
                matrix[13] = 0.5f;
                matrix[14] = 0.5f;
                matrix[15] = 1.0f;
            }
            return Shade(renderer, device, *vertices, params);
        };
        Check(shadeCascade(false) < 10 && shadeCascade(true) > 80,
              "cascade selection samples only the selected atlas slice");

        auto whiteCube = renderer.CreateTextureCube(1, false, 0);
        auto blackCube = renderer.CreateTextureCube(1, false, 0);
        const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
        const std::uint8_t blackPixel[4] = {0, 0, 0, 255};
        for (int face = 0; face < 6; ++face)
        {
            whiteCube->SetData(face, 0, 0, 0, 1, 1, whitePixel, sizeof(whitePixel));
            blackCube->SetData(face, 0, 0, 0, 1, 1, blackPixel, sizeof(blackPixel));
        }
        const auto shadePoint = [&](CNA::Internal::Renderers::ITextureCubeRenderer* cube) {
            GpuDrawParams params;
            SetAmbientOnlyPbr(params, white.get());
            params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
            params.punctualKind = 1;
            params.punctualPosition[2] = 1.0f;
            params.punctualDiffuse[0] = params.punctualDiffuse[1] = params.punctualDiffuse[2] = 6.0f;
            params.punctualRange = 10.0f;
            params.punctualShadowBias = 0.0f;
            params.punctualShadowCube = cube;
            return Shade(renderer, device, *vertices, params);
        };
        Check(shadePoint(whiteCube.get()) > 80 && shadePoint(blackCube.get()) < 10,
              "point-light cube shadow attenuates only the punctual light");

        const auto shadeSpot = [&](ITextureRenderer* map) {
            GpuDrawParams params;
            SetAmbientOnlyPbr(params, white.get());
            params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = 0.0f;
            params.punctualKind = 2;
            params.punctualPosition[2] = 1.0f;
            params.punctualDirection[0] = params.punctualDirection[1] = 0.0f;
            params.punctualDirection[2] = -1.0f;
            params.punctualDiffuse[0] = params.punctualDiffuse[1] = params.punctualDiffuse[2] = 6.0f;
            params.punctualRange = 10.0f;
            params.punctualCosInner = 0.94f;
            params.punctualCosOuter = 0.76f;
            params.punctualShadowBias = 0.0f;
            params.punctualShadowMap = map;
            return Shade(renderer, device, *vertices, params);
        };
        Check(shadeSpot(white.get()) > 80 && shadeSpot(black.get()) < 10,
              "spot-light 2D shadow attenuates only the punctual light");

        const auto shadeClassic = [&](ITextureRenderer* map, float ambient) {
            GpuDrawParams params;
            params.lightingEnabled = true;
            params.diffuseColor[0] = params.diffuseColor[1] =
                params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
            params.ambientColor[0] = params.ambientColor[1] = params.ambientColor[2] = ambient;
            params.light0Dir[2] = -1.0f;
            params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 1.0f;
            params.eyePositionWorld[2] = 3.0f;
            params.shadowsEnabled = true;
            params.shadowMap = map;
            params.shadowDepthBias = 0.0f;
            params.shadowPcfRadius = 0;
            // Deliberately leave PreferPerPixelLighting false: shadow reception must defer the
            // classic light split itself instead of attenuating already-combined vertex colour.
            return Shade(renderer, device, *vertices, params);
        };
        const int classicLit = shadeClassic(white.get(), 0.25f);
        const int classicShadow = shadeClassic(black.get(), 0.25f);
        Check(classicLit > 200 && classicShadow > 45 && classicShadow < 85,
              "classic shadow preserves ambient while attenuating directional diffuse");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        std::fflush(stdout);
        result_ = passCount == totalCount ? 0 : 1;
        Exit();
    }

public:
    SoftwareShadowSamplingTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    SoftwareShadowSamplingTest game;
    game.Run();
    return game.Result();
}
