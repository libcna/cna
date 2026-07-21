// SPDX-License-Identifier: MS-PL
// REMED-GFX-007 (WebGPU slice): env_map3d.wgsl's fragment stage must add EmissiveColor UNSCALED
// (FNA Lighting.fxh: litRGB = lightSum*DiffuseColor + EmissiveColor), NOT
// (EmissiveColor + lightSum)*DiffuseColor. The wrong form re-multiplies the already-ambient-folded
// emissive by DiffuseColor a second time (and, because the CPU layer pre-folds Alpha into both
// operands, also squares Alpha) -- identical root cause to the Vulkan/Bgfx/SdlGpu slices.
//
// COLOR-SPACE HANDLING. WebGPU's swapchain/render-target format is chosen from surface
// capabilities and is frequently *_Srgb (see ConfigureSurface()'s preferredFormats: BGRA8UnormSrgb
// first). ReadBackbuffer() returns the raw stored bytes (only a BGRA->RGBA swizzle, no sRGB
// decode), so an intermediate linear value like (100,50,25) would read back sRGB-encoded and
// environment-dependent. To stay correct under ANY transfer function this test never hard-codes an
// intermediate expected byte. Instead it CALIBRATES: an EnvironmentMapEffect with
// EnvironmentMapAmount=1 and Fresnel disabled performs a flat FULL REPLACE
// (rgb = envSample.rgb * combinedAlpha, combinedAlpha=1 here), so rendering a solid cube of a
// KNOWN LINEAR color C and reading it back measures exactly T(C) -- the same output-encoding path
// the emissive draw uses. Each emissive draw is then compared against the calibration of its own
// predicted-correct linear color. Any byte difference between an emissive draw and its matching
// calibration is therefore semantic, not color-space, by construction.
//
// Geometry: a camera-facing quad at z=0.5, EnvironmentMapAmount=0 on the emissive draws to suppress
// the cube blend entirely (rgb = litRGB * textureColor), zero directional lights (lightSum=0), and
// PointClamp sampling to read exact 1x1 texels (matching webgpu_envmap3d_test.cpp's conventions).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 12)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    VertexBuffer MakeFacingQuad(GraphicsDevice& dev)
    {
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const Vector3 n(0.0f, 0.0f, -1.0f);
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), n, Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
            cube->SetData(face, &col, 1);
        return cube;
    }
}

class WebGpuEnvMapEmissiveTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> tex_;
    std::unique_ptr<TextureCube> dummyCube_;
    int passCount_ = 0;
    int total_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label, Color got, Color want)
    {
        ++total_;
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++passCount_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected~(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
        }
    }

    // CALIBRATION: EnvironmentMapAmount=1, Fresnel disabled -> flat full replace. With
    // combinedAlpha=1 (default Diffuse alpha=1, texture alpha=1) the output rgb == the cube color,
    // so this returns T(cubeColor) through the identical output-encoding path the emissive draws
    // use. cubeColor is a KNOWN linear color.
    Color RenderCalibration(GraphicsDevice& dev, Color cubeColor)
    {
        auto cube = MakeSolidCube(dev, cubeColor);
        dev.Clear(Color::Black);
        VertexBuffer vb = MakeFacingQuad(dev);
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(tex_.get());
        fx.setEnvironmentMapProperty(cube.get());
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        return readCenter(dev);
    }

    // EnvironmentMapAmount=0 -> cube suppressed, rgb = litRGB * textureColor; no directional lights.
    Color RenderEmissive(GraphicsDevice& dev, Vector3 diffuse, Vector3 emissive, float alpha)
    {
        dev.Clear(Color::Black);
        VertexBuffer vb = MakeFacingQuad(dev);
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(tex_.get());
        fx.setEnvironmentMapProperty(dummyCube_.get());
        fx.setDiffuseColorProperty(diffuse);
        fx.setEmissiveColorProperty(emissive);
        fx.setAlphaProperty(alpha);
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        return readCenter(dev);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Color kTex(200, 100, 50, 255);
        tex_ = std::make_unique<Texture2D>(dev, 1, 1);
        tex_->SetData(&kTex, 1);
        dummyCube_ = MakeSolidCube(dev, Color(0, 255, 0, 255));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        // Calibrations: measure T() of the linear colors the emissive draws should produce.
        const Color calibCore = RenderCalibration(dev, Color(100, 50, 25, 255));   // tex*0.5
        const Color calibAlpha = RenderCalibration(dev, Color(50, 25, 13, 255));   // tex*0.25

        // (1) CORE discriminator -- non-white Diffuse + non-zero Emissive, Alpha=1.
        //   correct   litRGB = 0*0.5 + 0.5 = 0.5  -> tex*0.5  == calibCore
        //   wrong  (emissive+lightSum)*diffuse = 0.25 -> tex*0.25 == calibAlpha (fails vs calibCore)
        Color core = RenderEmissive(dev, Vector3(0.5f, 0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f), 1.0f);
        check(colorNear(core, calibCore),
              "REMED-GFX-007 core: non-white Diffuse + Emissive, Alpha=1 -> emissive added unscaled",
              core, calibCore);

        // (2) ALPHA-SQUARING discriminator -- Alpha=0.5.
        //   correct   litRGB = emissive*Alpha = 0.25 -> tex*0.25 == calibAlpha
        //   wrong  (emissive*Alpha)*(diffuse*Alpha) = 0.0625 -> tex*0.0625 (fails vs calibAlpha)
        Color alphaHalf = RenderEmissive(dev, Vector3(0.5f, 0.5f, 0.5f), Vector3(0.5f, 0.5f, 0.5f), 0.5f);
        check(colorNear(alphaHalf, calibAlpha),
              "REMED-GFX-007 alpha: Alpha=0.5 -> no alpha-squaring (single Alpha, not Alpha^2)",
              alphaHalf, calibAlpha);

        // (3) WHITE-DIFFUSE control -- both formulas coincide (the masking case). Passes pre/post.
        Color whiteDiffuse = RenderEmissive(dev, Vector3(1.0f, 1.0f, 1.0f), Vector3(0.5f, 0.5f, 0.5f), 1.0f);
        check(colorNear(whiteDiffuse, calibCore),
              "control: white Diffuse -> both formulas coincide (masked case)",
              whiteDiffuse, calibCore);

        // (4) ZERO-EMISSIVE control -- litRGB=0 -> black regardless of formula.
        Color zeroEmissive = RenderEmissive(dev, Vector3(0.5f, 0.5f, 0.5f), Vector3(0.0f, 0.0f, 0.0f), 1.0f);
        check(colorNear(zeroEmissive, Color::Black),
              "control: zero Emissive, no lights -> black",
              zeroEmissive, Color::Black);

        std::printf("calib: core(tex*0.5)=(%d,%d,%d) alpha(tex*0.25)=(%d,%d,%d)\n",
            calibCore.getRProperty(), calibCore.getGProperty(), calibCore.getBProperty(),
            calibAlpha.getRProperty(), calibAlpha.getGProperty(), calibAlpha.getBProperty());
        std::printf("=== %d/%d PASS ===\n", passCount_, total_);
        result_ = (passCount_ == total_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuEnvMapEmissiveTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuEnvMapEmissiveTest game;
    game.Run();
    return game.getResult();
}
