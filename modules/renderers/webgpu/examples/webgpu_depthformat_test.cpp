// SPDX-License-Identifier: MS-PL
// WEBGPU-39: RenderTarget2D honours its DepthFormat exactly instead of over-provisioning every
// target with Depth24Stencil8. This test proves the four FNA DepthFormat values are observably
// distinct on the WebGPU renderer:
//
//   None            -> no depth attachment at all. The pipeline carries no depthStencil state, so
//                      depth testing cannot happen: a FARTHER fragment drawn AFTER a nearer one
//                      still overwrites it (painter's order wins).
//   Depth16         -> a real depth-only attachment. Depth testing works (the farther fragment is
//                      rejected) and NO stencil load/store op is named on the pass -- if the code
//                      wrongly emitted stencil ops on a depth-only format, wgpu-native would reject
//                      the render pass and the draw would not land, so a correct depth result also
//                      proves the stencil-op gating.
//   Depth24         -> same contract as Depth16 (depth-only, no stencil).
//   Depth24Stencil8 -> depth testing works AND stencil operations bake into the pipeline: a
//                      stamp-then-gate sequence on the render target's own attachment gates
//                      correctly (green only where stencil was stamped).
//
// Every draw uses BasicEffect with identity world/view/projection, so a vertex position is already
// a clip-space coordinate (WebGPU clip z in [0,1], 0 = near). DepthStencilState::Default compares
// LessEqual, so a nearer z=0.3 quad drawn first survives a farther z=0.7 quad drawn second on any
// depth-carrying format; on None the second draw always wins.
//
// Exit code 0 = every check PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

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

    const char* formatName(DepthFormat f)
    {
        switch (f)
        {
            case DepthFormat::None: return "None";
            case DepthFormat::Depth16: return "Depth16";
            case DepthFormat::Depth24: return "Depth24";
            case DepthFormat::Depth24Stencil8: return "Depth24Stencil8";
        }
        return "?";
    }

    // A full-screen quad (NDC x/y in [-1,1]) at a fixed clip-space z, every vertex the same colour.
    VertexBuffer MakeQuad(GraphicsDevice& dev, float z, Color c)
    {
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), c },
            { Vector3(-1.0f, -1.0f, z), c },
            { Vector3( 1.0f, -1.0f, z), c },
            { Vector3(-1.0f,  1.0f, z), c },
            { Vector3( 1.0f, -1.0f, z), c },
            { Vector3( 1.0f,  1.0f, z), c },
        };
        vb.SetData(verts, 0, 6);
        return vb;
    }

    // A quad spanning [x0,x1] in NDC x, full height, at z=0.5 -- used by the stencil stamp/gate.
    VertexBuffer MakeHalfQuad(GraphicsDevice& dev, float x0, float x1, Color c)
    {
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, 0.5f), c },
            { Vector3(x0, -1.0f, 0.5f), c },
            { Vector3(x1, -1.0f, 0.5f), c },
            { Vector3(x0,  1.0f, 0.5f), c },
            { Vector3(x1, -1.0f, 0.5f), c },
            { Vector3(x1,  1.0f, 0.5f), c },
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

class WebGpuDepthFormatTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        ++checkCount_;
        if (ok) ++passCount_;
    }

    std::vector<Color> ReadTarget(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        target.GetData(0, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    static Color At(const std::vector<Color>& pixels, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y) * kSize + x];
    }

    void DrawColored(GraphicsDevice& dev, VertexBuffer& vb)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Draw a near Red quad then a farther Green quad, both full-screen, depth test enabled.
    // depth-carrying formats reject the far draw (centre stays Red); None accepts it (centre Green).
    Color RenderDepthOrder(GraphicsDevice& dev, DepthFormat format)
    {
        RenderTarget2D rt(dev, kSize, kSize, false, SurfaceFormat::Color, format, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.Clear(Color::Blue);

        VertexBuffer nearQuad = MakeQuad(dev, 0.3f, Color::Red);
        DrawColored(dev, nearQuad);
        VertexBuffer farQuad = MakeQuad(dev, 0.7f, Color::Green);
        DrawColored(dev, farQuad);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return At(ReadTarget(rt), kSize / 2, kSize / 2);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Depth-order contract per format. Red = the depth test kept the near fragment; Green = the
        // far fragment overwrote it (no depth test).
        const Color noneCentre = RenderDepthOrder(dev, DepthFormat::None);
        check(colorNear(noneCentre, Color::Green),
              "DepthFormat::None has no depth attachment -> far fragment overwrites (centre green)");

        const Color d16Centre = RenderDepthOrder(dev, DepthFormat::Depth16);
        check(colorNear(d16Centre, Color::Red),
              "DepthFormat::Depth16 depth test rejects the far fragment (centre red)");

        const Color d24Centre = RenderDepthOrder(dev, DepthFormat::Depth24);
        check(colorNear(d24Centre, Color::Red),
              "DepthFormat::Depth24 depth test rejects the far fragment (centre red)");

        const Color d24s8Centre = RenderDepthOrder(dev, DepthFormat::Depth24Stencil8);
        check(colorNear(d24s8Centre, Color::Red),
              "DepthFormat::Depth24Stencil8 depth test rejects the far fragment (centre red)");

        // Stencil contract: only Depth24Stencil8 carries a stencil buffer. Stamp the left half
        // (Always/Replace ref=1) then gate a full-screen Green draw (Equal/Keep ref=1) -- green may
        // land only where the stencil was stamped.
        {
            RenderTarget2D rt(dev, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.Clear(Color::Blue);

            dev.setDepthStencilStateProperty(StencilStamp());
            VertexBuffer stamp = MakeHalfQuad(dev, -1.0f, 0.0f, Color::Red);
            DrawColored(dev, stamp);

            dev.setDepthStencilStateProperty(StencilGate());
            VertexBuffer gate = MakeHalfQuad(dev, -1.0f, 1.0f, Color::Green);
            DrawColored(dev, gate);

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pixels = ReadTarget(rt);
            check(colorNear(At(pixels, kSize / 4, kSize / 2), Color::Green),
                  "Depth24Stencil8 render target: stencil gate passes where stamped (left green)");
            check(colorNear(At(pixels, (kSize * 3) / 4, kSize / 2), Color::Blue),
                  "Depth24Stencil8 render target: stencil gate rejected where unstamped (right blue)");
        }

        std::printf("=== %d/%d PASS (formats: None/Depth16/Depth24/Depth24Stencil8) ===\n",
                    passCount_, checkCount_);
        std::printf("[INFO] centres: None=%s Depth16=%s Depth24=%s Depth24Stencil8=%s\n",
                    formatName(DepthFormat::None), formatName(DepthFormat::Depth16),
                    formatName(DepthFormat::Depth24), formatName(DepthFormat::Depth24Stencil8));
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuDepthFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuDepthFormatTest game;
    game.Run();
    return game.getResult();
}
