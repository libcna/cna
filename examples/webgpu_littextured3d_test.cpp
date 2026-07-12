// SPDX-License-Identifier: MS-PL
// Phase 58/59/63 (WEBGPU-22): verify WebGPUGraphicsBackend's lit_textured3d.wgsl /
// GetOrCreatePipelineLitTextured3D() / DrawPrimitivesEx() dispatch for stride-32
// (VertexPositionNormalTexture) draws -- the first WebGPU 3D shader with real Blinn-Phong
// lighting (FNA's Lighting.fxh ComputeLights(), ported from VulkanGraphicsBackend's
// lit_textured3d.{vert,frag}.glsl), driven by a second UBO (litBindGroupLayout_) alongside the
// existing 128-byte primary uniforms.
//
// All checks use World=View=Projection=Identity (BasicEffect defaults), a quad at z=0.5 with
// Normal=(0,0,-1) (facing the camera at the origin) and a white 1x1 texture unless noted.
//
// Check A -- LightingEnabled=false, green texture, default (white) DiffuseColor: renders green --
//   proves the stride-32 dispatch and the shader's unlit fallback branch both work (regression
//   parity with textured3d.wgsl's own Check A).
// Check B -- LightingEnabled=true, AmbientLightColor=black, DirectionalLight0 enabled with
//   Direction=(0,0,1) (light travels into the screen, hitting the quad's camera-facing side):
//   renders white -- proves a light facing the surface actually illuminates it.
// Check C -- same as B but DirectionalLight0.Direction=(0,0,-1) (light hits the back of the quad,
//   not the visible face): renders black -- proves the N-dot-L "does this light face the surface"
//   gate really excludes light from the wrong side, not just "always full bright".
// Check D -- LightingEnabled=true, DirectionalLight0 disabled, AmbientLightColor=(1,1,1): renders
//   white -- proves ambient light alone reaches the shader (with no directional light contributing
//   at all, the surface would otherwise be black). Uses a pure white/black extreme rather than a
//   mid-tone expected value since the WebGPU surface prefers an sRGB swapchain format, so a raw
//   readback of a genuinely mid-range linear fragment value comes back gamma-encoded, not linear
//   (this is a pre-existing WebGPU backend characteristic, not specific to this shader).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool colorNear(Color a, Color b, int tol = 24)
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
}

class WebGpuLitTextured3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    Texture2D greenTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{255, 255, 255, 255});
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                 std::vector<std::uint8_t>{0, 255, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // Check A: LightingEnabled=false -- plain texture*diffuse, no lighting math at all.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&greenTex_);
            fx.setLightingEnabledProperty(false);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Lime),
                  "LightingEnabled=false renders plain texture*diffuse (green)");
        }

        // Check B: a light facing the visible side of the quad -- fully lit, renders white.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex_);
            fx.setLightingEnabledProperty(true);
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::White),
                  "a light facing the surface normal fully illuminates it (white)");
        }

        // Check C: the same light, now shining on the back of the quad -- N.L <= 0, no diffuse.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex_);
            fx.setLightingEnabledProperty(true);
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.DirectionalLight0.setEnabledProperty(true);
            fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::Black),
                  "a light behind the surface contributes no diffuse (black)");
        }

        // Check D: no directional light at all -- ambient alone still reaches the shader.
        {
            dev.Clear(Color::Black);
            VertexBuffer vb = MakeFacingQuad(dev);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&whiteTex_);
            fx.setLightingEnabledProperty(true);
            fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.DirectionalLight0.setEnabledProperty(false);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            check(colorNear(readCenter(dev), Color::White),
                  "ambient light alone reaches the shader independent of directional lights");
        }

        std::printf("=== %d/4 PASS ===\n", passCount_);
        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    WebGpuLitTextured3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuLitTextured3DTest game;
    game.Run();
    return game.getResult();
}
