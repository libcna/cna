// SPDX-License-Identifier: MS-PL
// WEBGPU-83 (Phase 2): prove that stencil operations now bake into a NON-colored3d 3D family's
// pipeline. The colored3d route already honours DepthStencilState stencil ops (proven by the
// shared rendertarget_depthstencil_usage acceptance test); this test exercises the same
// stamp-then-gate sequence on the Textured3D family (BasicEffect + a texture, stride-20
// VertexPositionTexture -> GetOrCreatePipelineTextured3D), which reaches the shared
// FillWGPUStencilState / HashStencilState / CaptureStencilStateEXT machinery the family
// extension wired up.
//
// Within ONE back-buffer bind cycle:
//   - Clear the 64x64 buffer to Blue.
//   - Stamp the LEFT half (NDC x in [-1,0]) with a textured draw under StencilStamp
//     (Always/Replace, ref=1): the left half gets colour Red and stencil value 1; the right half
//     is untouched (still Blue, stencil 0).
//   - Gate a FULL-SCREEN textured draw under StencilGate (Equal/Keep, ref=1) with colour Green: it
//     may write only where stencil == 1, i.e. the left half.
//
// Check A -- left-half centre is Green: the gate passed exactly where the stamp had run.
// Check B -- right-half centre is still Blue: the gate was REJECTED where nothing was stamped.
//   This is the discriminator. If the Textured3D family did NOT bake the stencil state into its
//   pipeline, the gate draw would paint Green across the whole buffer and the right half would be
//   Green, not Blue -- exactly the silent-wrongness this task closes.
//
// Exit code 0 = both checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

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

    Color readPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    // A quad spanning [x0,x1] in NDC x, full height, at z=0.5 -- a plain textured (stride-20) quad.
    VertexBuffer MakeQuad(GraphicsDevice& dev, float x0, float x1)
    {
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const VertexPositionTexture verts[6] = {
            { Vector3(x0,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3(x0, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
            { Vector3(x1, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(x0,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3(x1, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(x1,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }

    DepthStencilState StencilStamp()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(1);
        return ds;
    }

    DepthStencilState StencilGate()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        ds.setReferenceStencilProperty(1);
        return ds;
    }
}

class WebGpuStencilFamilyTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D redTex_;
    Texture2D greenTex_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    void DrawTextured(GraphicsDevice& dev, VertexBuffer& vb, Texture2D& tex)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(false);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        redTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                              std::vector<std::uint8_t>{255, 0, 0, 255});
        greenTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                std::vector<std::uint8_t>{0, 255, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);

        dev.Clear(Color::Blue);

        // Stamp the left half: writes Red and sets stencil = 1 there.
        dev.setDepthStencilStateProperty(StencilStamp());
        VertexBuffer stampQuad = MakeQuad(dev, -1.0f, 0.0f);
        DrawTextured(dev, stampQuad, redTex_);

        // Gate a full-screen Green draw: may write only where stencil == 1 (the left half).
        dev.setDepthStencilStateProperty(StencilGate());
        VertexBuffer gateQuad = MakeQuad(dev, -1.0f, 1.0f);
        DrawTextured(dev, gateQuad, greenTex_);

        const Color left = readPixel(dev, kSize / 4, kSize / 2);       // x=16 -- inside the stamp
        const Color right = readPixel(dev, (kSize * 3) / 4, kSize / 2); // x=48 -- outside the stamp

        check(colorNear(left, Color::Lime),
              "Textured3D gate passes where the stencil was stamped (left half -> green)");
        check(colorNear(right, Color::Blue),
              "Textured3D gate is rejected where nothing was stamped (right half -> blue)");

        std::printf("=== %d/2 PASS ===\n", passCount_);
        result_ = (passCount_ == 2) ? 0 : 1;
        Exit();
    }

public:
    WebGpuStencilFamilyTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuStencilFamilyTest game;
    game.Run();
    return game.getResult();
}
