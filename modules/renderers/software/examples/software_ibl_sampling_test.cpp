// SPDX-License-Identifier: MS-PL
// Deterministic CPU oracle for CNAEXT PBR split-sum image-based lighting.

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

#include <cmath>
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
using CNA::Internal::Renderers::ITextureCubeRenderer;
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

    bool Near(int actual, int expected, int tolerance = 5)
    {
        return std::abs(actual - expected) <= tolerance;
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
        IGraphicsRenderer& renderer, std::uint8_t r, std::uint8_t g,
        std::uint8_t b, std::uint8_t a = 255)
    {
        ImageData image;
        image.width = 1;
        image.height = 1;
        image.pixels = {r, g, b, a};
        return renderer.CreateTexture(image);
    }

    void FillCubeLevel(ITextureCubeRenderer& cube, int level, int dimension,
                       std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(dimension * dimension * 4), 255);
        for (int pixel = 0; pixel < dimension * dimension; ++pixel)
        {
            pixels[static_cast<std::size_t>(pixel) * 4 + 0] = r;
            pixels[static_cast<std::size_t>(pixel) * 4 + 1] = g;
            pixels[static_cast<std::size_t>(pixel) * 4 + 2] = b;
        }
        for (int face = 0; face < 6; ++face)
            cube.SetData(face, level, 0, 0, dimension, dimension,
                         pixels.data(), pixels.size());
    }

    void SetPbr(GpuDrawParams& params, ITextureRenderer* white)
    {
        params.pbr = true;
        params.textureEnabled = true;
        params.texture0 = white;
        params.diffuseColor[0] = params.diffuseColor[1] =
            params.diffuseColor[2] = params.diffuseColor[3] = 1.0f;
        params.eyePositionWorld[2] = 3.0f;
        params.light0Diffuse[0] = params.light0Diffuse[1] = params.light0Diffuse[2] = 0.0f;
        params.light1Diffuse[0] = params.light1Diffuse[1] = params.light1Diffuse[2] = 0.0f;
        params.light2Diffuse[0] = params.light2Diffuse[1] = params.light2Diffuse[2] = 0.0f;
        params.emissiveColor[0] = params.emissiveColor[1] = params.emissiveColor[2] = 0.0f;
        params.pbrMetallicFactor = 0.0f;
        params.pbrRoughnessFactor = 1.0f;
        params.pbrEncodeOutputToSrgb = false;
    }
}

class SoftwareIblSamplingTest final : public Game
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
        renderer.ApplySamplerState(10, 1, 1, 1, 1);
        renderer.ApplySamplerState(11, 1, 1, 1, 1);
        renderer.ApplySamplerState(12, 1, 1, 1, 1);

        auto vertices = renderer.CreateVertexBuffer(3);
        vertices->SetData(kTriangle, 3, sizeof(VPNTanT));
        auto skinnedVertices = renderer.CreateVertexBuffer(3);
        skinnedVertices->SetData(kSkinnedTriangle, 3, sizeof(VPNTanTW));
        auto white = MakeTexture(renderer, 255, 255, 255);
        auto blackLut = MakeTexture(renderer, 0, 0, 0);
        auto blackIrradiance = renderer.CreateTextureCube(1, false, 0);
        auto blackSpecular = renderer.CreateTextureCube(1, false, 0);
        FillCubeLevel(*blackIrradiance, 0, 1, 0, 0, 0);
        FillCubeLevel(*blackSpecular, 0, 1, 0, 0, 0);

        Check(renderer.SupportsImageBasedLightingEXT(),
              "Software advertises the image-based lighting it executes");

        GpuDrawParams flatAmbient;
        SetPbr(flatAmbient, white.get());
        flatAmbient.ambientColor[0] = flatAmbient.ambientColor[1] =
            flatAmbient.ambientColor[2] = 0.25f;
        Check(Near(Shade(renderer, device, *vertices, flatAmbient), 64),
              "disabled IBL retains the flat ambient term");

        GpuDrawParams replacement = flatAmbient;
        replacement.iblEnabled = true;
        replacement.iblIrradiance = blackIrradiance.get();
        replacement.iblPrefilteredSpecular = blackSpecular.get();
        replacement.iblBrdfLut = blackLut.get();
        Check(Shade(renderer, device, *vertices, replacement) < 5,
              "enabled IBL replaces rather than adds the flat ambient term");

        auto halfIrradiance = renderer.CreateTextureCube(1, false, 0);
        auto quarterSpecular = renderer.CreateTextureCube(1, false, 0);
        FillCubeLevel(*halfIrradiance, 0, 1, 128, 128, 128);
        FillCubeLevel(*quarterSpecular, 0, 1, 64, 64, 64);
        auto splitSumLut = MakeTexture(renderer, 128, 26, 0);
        GpuDrawParams splitSum;
        SetPbr(splitSum, white.get());
        splitSum.iblEnabled = true;
        splitSum.iblIrradiance = halfIrradiance.get();
        splitSum.iblPrefilteredSpecular = quarterSpecular.get();
        splitSum.iblBrdfLut = splitSumLut.get();
        const int splitSumFull = Shade(renderer, device, *vertices, splitSum);
        Check(Near(splitSumFull, 131, 4),
              "split-sum diffuse irradiance and BRDF-weighted specular match the analytic value");
        splitSum.iblIntensity = 0.5f;
        Check(Near(Shade(renderer, device, *vertices, splitSum), splitSumFull / 2, 3),
              "IBL intensity scales the complete environment contribution once");

        auto mipSpecular = renderer.CreateTextureCube(4, true, 0);
        FillCubeLevel(*mipSpecular, 0, 4, 0, 0, 0);
        FillCubeLevel(*mipSpecular, 1, 2, 128, 128, 128);
        FillCubeLevel(*mipSpecular, 2, 1, 255, 255, 255);
        auto scaleOnlyLut = MakeTexture(renderer, 255, 0, 0);
        GpuDrawParams mipParams;
        SetPbr(mipParams, white.get());
        mipParams.pbrMetallicFactor = 1.0f;
        mipParams.pbrRoughnessFactor = 0.5f;
        mipParams.iblEnabled = true;
        mipParams.iblIrradiance = blackIrradiance.get();
        mipParams.iblPrefilteredSpecular = mipSpecular.get();
        mipParams.iblBrdfLut = scaleOnlyLut.get();
        mipParams.iblPrefilteredMipCount = 3;
        Check(Near(Shade(renderer, device, *vertices, mipParams), 128, 3),
              "roughness selects the documented explicit prefiltered cube mip");

        mipParams.pbrRoughnessFactor = 0.25f; // explicit LOD 0.5
        renderer.ApplySamplerState(11, 1, 1, 1, 1); // point mip selection -> level 0
        const int pointMip = Shade(renderer, device, *vertices, mipParams);
        renderer.ApplySamplerState(11, 0, 1, 1, 1); // linear mip selection -> level 0/1 blend
        const int linearMip = Shade(renderer, device, *vertices, mipParams);
        Check(pointMip < 5 && Near(linearMip, 64, 4),
              "public sampler slot 11 controls prefiltered cube mip interpolation");

        GpuDrawParams skinned = splitSum;
        skinned.iblIntensity = 1.0f;
        skinned.skinned = true;
        skinned.weightsPerVertex = 1;
        skinned.boneCount = 1;
        const float identity[16] = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (int element = 0; element < 16; ++element)
            skinned.boneTransforms[element] = identity[element];
        Check(Near(Shade(renderer, device, *skinnedVertices, skinned), splitSumFull, 3),
              "skinned PBR uses the post-bone normal and view vector for IBL");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        std::fflush(stdout);
        result_ = passCount == totalCount ? 0 : 1;
        Exit();
    }

public:
    SoftwareIblSamplingTest()
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
    SoftwareIblSamplingTest game;
    game.Run();
    return game.Result();
}
