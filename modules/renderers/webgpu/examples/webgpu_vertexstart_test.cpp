// SPDX-License-Identifier: MS-PL
// WEBGPU-70: the reachable draw routes honour vertexStart / baseVertex / startIndex. XNA exposes
// these through DrawPrimitives(type, vertexStart, count) and DrawIndexedPrimitives(type, baseVertex,
// minVertexIndex, numVertices, startIndex, count) -- the WebGPU renderer applies a vertexStart*stride
// byte offset on the non-indexed route and forwards baseVertex/startIndex to
// wgpuRenderPassEncoderDrawIndexed on the indexed route. This drives each with two identically-placed
// triangles of different colours (red at vertices 0..2, blue at 3..5) so a wrong/ignored offset
// selects the wrong triangle and flips the sampled colour -- the discriminator.
//
// (XNA's DrawInstancedPrimitives has no vertexStart parameter, so the one route that ignores
// vertexStart -- DrawInstancedPrimitivesEx -- is unreachable through any XNA API and is deliberately
// not exercised here; see the WEBGPU-70 plan row.)

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kRed(220, 0, 0, 255);
    const Color kBlue(0, 0, 220, 255);

    // A big triangle that covers the centre pixel; both colours use the same three positions.
    Vector3 P0() { return Vector3(-2, -2, 0); }
    Vector3 P1() { return Vector3(2, -2, 0); }
    Vector3 P2() { return Vector3(0, 3, 0); }

    std::string Txt(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + ")";
    }
    bool IsRed(const Color& c) { return c.getRProperty() > 150 && c.getBProperty() < 80; }
    bool IsBlue(const Color& c) { return c.getBProperty() > 150 && c.getRProperty() < 80; }
}

class WebGpuVertexStartTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int passed_ = 0, total_ = 0, result_ = 1;

    Color ReadCentre()
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        target_->GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }

    void ApplyEffect(GraphicsDevice& dev, BasicEffect& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateTranslation(0, 0, -4.0f));
        fx.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f));
        fx.setTextureEnabledProperty(false);
        fx.setLightingEnabledProperty(false);
        fx.VertexColorEnabled = true;
        fx.Apply();
    }

    void Check(bool ok, const std::string& label, const Color& got)
    {
        ++total_;
        if (ok) ++passed_;
        std::printf("[%s] %s got=%s\n", ok ? "PASS" : "FAIL", label.c_str(), Txt(got).c_str());
    }

protected:
    void LoadContent() override
    {
        target_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), kSize, kSize, false,
                                                   SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                                                   RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        // vertices 0..2 = red triangle, 3..5 = blue triangle (same positions).
        const VertexPositionColor verts[] = {
            {P0(), kRed}, {P1(), kRed}, {P2(), kRed},
            {P0(), kBlue}, {P1(), kBlue}, {P2(), kBlue},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);

        const std::uint16_t idx[] = {0, 1, 2, 3, 4, 5};
        IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        ib.SetData(idx, 0, 6);

        const auto renderNonIndexed = [&](int vertexStart) {
            dev.SetRenderTarget(target_.get());
            dev.Clear(Color(0, 0, 0, 255));
            BasicEffect fx(dev);
            ApplyEffect(dev, fx);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, vertexStart, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return ReadCentre();
        };

        const auto renderIndexed = [&](int baseVertex, int startIndex) {
            dev.SetRenderTarget(target_.get());
            dev.Clear(Color(0, 0, 0, 255));
            BasicEffect fx(dev);
            ApplyEffect(dev, fx);
            dev.SetVertexBuffer(&vb);
            dev.SetIndexBuffer(&ib);
            // numVertices is the vertex-range span from baseVertex; baseVertex+numVertices must stay
            // within the 6-vertex buffer (XNA validates this), so span the rest of the buffer.
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, 0, 6 - baseVertex, startIndex, 1);
            dev.SetIndexBuffer(nullptr);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return ReadCentre();
        };

        // Non-indexed vertexStart: 0 -> red triangle, 3 -> blue triangle.
        const Color a = renderNonIndexed(0);
        Check(IsRed(a), "DrawPrimitives vertexStart=0 draws the red triangle (0..2)", a);
        const Color b = renderNonIndexed(3);
        Check(IsBlue(b), "DrawPrimitives vertexStart=3 draws the blue triangle (3..5)", b);

        // Indexed baseVertex: added to every decoded index (indices 0,1,2 + base 3 -> verts 3,4,5).
        const Color c = renderIndexed(0, 0);
        Check(IsRed(c), "DrawIndexedPrimitives baseVertex=0 draws the red triangle", c);
        const Color d = renderIndexed(3, 0);
        Check(IsBlue(d), "DrawIndexedPrimitives baseVertex=3 shifts to the blue triangle", d);

        // Indexed startIndex: first index element read (indices 3,4,5 + base 0 -> verts 3,4,5).
        const Color e = renderIndexed(0, 3);
        Check(IsBlue(e), "DrawIndexedPrimitives startIndex=3 selects the blue triangle's indices", e);

        std::printf("=== WEBGPU-70 vertexStart/baseVertex/startIndex: %d/%d PASS ===\n", passed_, total_);
        result_ = (passed_ == total_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuVertexStartTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    int getResult() const { return result_; }
};

int main()
{
    WebGpuVertexStartTest game;
    game.Run();
    return game.getResult();
}
