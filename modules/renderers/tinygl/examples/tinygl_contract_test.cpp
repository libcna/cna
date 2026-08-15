// SPDX-License-Identifier: MS-PL
// Cross-contract regressions found by the post-implementation audit of plan_tinygl.md.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Internal/Renderers/TinyGL/TinyGLRenderer.hpp"
#include "System/NotSupportedException.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::TinyGL;

namespace
{
    constexpr int kChecks = 23;

    VertexDeclaration PosColorDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool Near(const Color& value, int r, int g, int b, int tolerance = 4)
    {
        return std::abs(static_cast<int>(value.getRProperty()) - r) <= tolerance &&
               std::abs(static_cast<int>(value.getGProperty()) - g) <= tolerance &&
               std::abs(static_cast<int>(value.getBProperty()) - b) <= tolerance;
    }

    void FillQuad(VertexPositionColor* out, const Color& color)
    {
        out[0] = {Vector3(-1, 1, 0), color};
        out[1] = {Vector3(1, 1, 0), color};
        out[2] = {Vector3(1, -1, 0), color};
        out[3] = {Vector3(-1, 1, 0), color};
        out[4] = {Vector3(1, -1, 0), color};
        out[5] = {Vector3(-1, -1, 0), color};
    }
}

class TinyGLContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    bool Refused(const std::function<void()>& operation)
    {
        try { operation(); }
        catch (const System::NotSupportedException&) { return true; }
        catch (...) { return false; }
        return false;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<TinyGLRenderer&>(dev.GetRenderer());
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Check(renderer.SupportsDepthStencil(),
              "the depth-state hook stays enabled for TinyGL's real depth plane");
        Check(renderer.SupportsDepthBuffer(), "the standalone depth hook reports the real depth plane");
        Check(!renderer.SupportsStencilBuffer(), "the standalone stencil hook reports no stencil plane");

        Check(Refused([&] { Texture2D mip(dev, 4, 4, true, SurfaceFormat::Color); }),
              "Texture2D creation deterministically refuses a mip chain");
        Check(Refused([&] { renderer.ApplySamplerMipState(0, 1, 0.0f); }),
              "a non-default MaxMipLevel is refused");
        Check(Refused([&] { renderer.ApplySamplerMipState(0, 0, 0.25f); }),
              "a non-zero mip LOD bias is refused");
        Check(Refused([&] {
                  BlendState masked;
                  masked.setMultiSampleMaskProperty(0);
                  dev.setBlendStateProperty(masked);
              }),
              "a non-default MultiSampleMask is refused");

        VertexBuffer vb(dev, PosColorDecl(), 12, BufferUsage::None);
        VertexPositionColor vertices[12];
        FillQuad(vertices, Color(0, 0, 255, 255));
        FillQuad(vertices + 6, Color(255, 0, 0, 255));
        vb.SetData(vertices, 0, 12);

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color::Black);
        BasicEffect effect(dev);
        effect.VertexColorEnabled = true;
        effect.setDiffuseColorProperty(Vector3(0.5f, 1.0f, 1.0f));
        effect.setAlphaProperty(0.5f);
        effect.Apply();
        dev.SetVertexBuffer(&vb, 6);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Color modulated = ReadPixel(dev, 32, 32);
        Check(modulated.getRProperty() >= 58 && modulated.getRProperty() <= 68 &&
                  modulated.getGProperty() < 5 && modulated.getBProperty() < 5,
              "vertex colour is modulated by BasicEffect DiffuseColor and Alpha");

        effect.setDiffuseColorProperty(Vector3(1, 1, 1));
        effect.setAlphaProperty(1.0f);
        effect.Apply();
        dev.Clear(Color::Black);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 6, 2);
        Check(Near(ReadPixel(dev, 32, 32), 255, 0, 0), "vertexStart selects the requested records");

        dev.Clear(Color::Black);
        dev.SetVertexBuffer(&vb, 6);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        Check(Near(ReadPixel(dev, 32, 32), 255, 0, 0), "VertexBufferBinding.VertexOffset is honoured");

        IndexBuffer offsetIb(dev, IndexElementSize::SixteenBits, 9, BufferUsage::None);
        const std::uint16_t offsetIndices[9] = {0, 0, 0, 0, 1, 2, 3, 4, 5};
        offsetIb.SetData(offsetIndices, 0, 9);
        dev.Clear(Color::Black);
        dev.SetVertexBuffer(&vb);
        dev.setIndicesProperty(&offsetIb);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 6, 0, 6, 3, 2);
        Check(Near(ReadPixel(dev, 32, 32), 255, 0, 0),
              "startIndex and baseVertex select the requested indexed records");
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);

        Texture2D green(dev, 4, 4);
        std::vector<Color> greenPixels(16, Color(0, 255, 0, 255));
        green.SetData(greenPixels.data(), 16);

        dev.Clear(Color(0, 0, 40, 255));
        SpriteBatch originBatch(dev);
        originBatch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        originBatch.Draw(green, Rectangle(16, 16, 16, 16), Rectangle(0, 0, 4, 4), Color::White,
                         0.0f, Vector2(2, 2), SpriteEffects::None, 0.0f);
        originBatch.End();
        Check(Near(ReadPixel(dev, 10, 10), 0, 255, 0),
              "SpriteBatch origin is scaled from source texels and positions the anchor correctly");

        Texture2D twoColor(dev, 2, 1);
        const Color twoPixels[2] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255)};
        twoColor.SetData(twoPixels, 2);
        dev.Clear(Color::Black);
        SpriteBatch flipBatch(dev);
        flipBatch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        flipBatch.Draw(twoColor, Rectangle(8, 8, 32, 8), Rectangle(0, 0, 2, 1), Color::White,
                       0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, 0.0f);
        flipBatch.End();
        Check(Near(ReadPixel(dev, 12, 11), 0, 255, 0) &&
                  Near(ReadPixel(dev, 35, 11), 255, 0, 0),
              "SpriteBatch horizontal flip reverses the sampled source rectangle");

        dev.Clear(Color(0, 0, 40, 255));
        SpriteBatch rotationBatch(dev);
        rotationBatch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        rotationBatch.Draw(green, Rectangle(32, 24, 8, 4), Rectangle(0, 0, 4, 4), Color::White,
                           1.57079632679f, Vector2(0, 0), SpriteEffects::None, 0.0f);
        rotationBatch.End();
        Check(Near(ReadPixel(dev, 30, 29), 0, 255, 0) &&
                  Near(ReadPixel(dev, 36, 26), 0, 0, 40),
              "SpriteBatch rotation changes quad geometry around the requested origin");

        Texture2D transparentRed(dev, 1, 1);
        const Color transparent(255, 0, 0, 0);
        transparentRed.SetData(&transparent, 1);
        dev.Clear(Color(0, 0, 40, 255));
        SpriteBatch opaqueBatch(dev);
        opaqueBatch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        opaqueBatch.Draw(transparentRed, Rectangle(24, 24, 8, 8), Color::White);
        opaqueBatch.End();
        Check(Near(ReadPixel(dev, 27, 27), 255, 0, 0),
              "BlendState.Opaque draws RGB even when the source texture alpha is zero");

        dev.Clear(Color(0, 0, 40, 255));
        SpriteBatch alphaTintBatch(dev);
        alphaTintBatch.Begin();
        alphaTintBatch.Draw(green, Rectangle(24, 24, 8, 8), Color(255, 255, 255, 0));
        alphaTintBatch.End();
        Check(Near(ReadPixel(dev, 27, 27), 0, 0, 40),
              "SpriteBatch tint alpha participates in the documented cutout approximation");

        dev.Clear(Color(0, 0, 40, 255));
        dev.setViewportProperty(Viewport(20, 10, 16, 16));
        SpriteBatch viewportBatch(dev);
        viewportBatch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr);
        viewportBatch.Draw(green, Rectangle(0, 0, 8, 8), Color::White);
        viewportBatch.End();
        Check(Near(ReadPixel(dev, 22, 12), 0, 255, 0) && Near(ReadPixel(dev, 2, 2), 0, 0, 40),
              "SpriteBatch destination coordinates are local to the active viewport");
        dev.setViewportProperty(Viewport(0, 0, 65, 67));

        bool drawBeforeBeginThrew = false;
        try
        {
            auto directBatch = renderer.CreateSpriteBatch();
            directBatch->Draw(green.GetRenderer(), 0, 0);
        }
        catch (...) { drawBeforeBeginThrew = true; }
        Check(drawBeforeBeginThrew, "the renderer-level SpriteBatch refuses Draw before Begin");

        IndexBuffer badIb(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        const std::uint16_t badIndices[6] = {6, 7, 8, 99, 6, 6};
        badIb.SetData(badIndices, 0, 6);
        dev.Clear(Color(0, 0, 40, 255));
        effect.Apply();
        dev.SetVertexBuffer(&vb);
        dev.setIndicesProperty(&badIb);
        bool badIndexThrew = false;
        try { dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 12, 0, 2); }
        catch (...) { badIndexThrew = true; }
        dev.setIndicesProperty(nullptr);
        dev.SetVertexBuffer(nullptr);
        Check(badIndexThrew && Near(ReadPixel(dev, 32, 32), 0, 0, 40),
              "an invalid decoded index is rejected before any triangle is submitted");

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        const bool badFillRefused = Refused([&] {
            renderer.ApplyRasterizerState(1, 99, false, 0.0f, 0.0f);
        });
        dev.Clear(Color::Black);
        effect.Apply();
        dev.SetVertexBuffer(&vb, 6);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        Check(badFillRefused && Near(ReadPixel(dev, 32, 32), 255, 0, 0),
              "an invalid FillMode is rejected before cull state is mutated");

        renderer.Clear(0.25f, 0.5f, 0.75f, 1.0f);
        std::uint8_t edge[4] = {};
        renderer.ReadBackbuffer(64, 66, 1, 1, edge);
        Check(edge[0] >= 60 && edge[1] >= 124 && edge[2] >= 188,
              "a 65-pixel backbuffer retains and clears its final logical column");

        renderer.SetVirtualResolution(69, 9);
        int logicalW = 0, logicalH = 0;
        renderer.GetViewportSize(logicalW, logicalH);
        Check(logicalW == 69 && logicalH == 9,
              "resize preserves the exact requested logical dimensions");
        renderer.Clear(0.25f, 0.5f, 0.75f, 1.0f);
        renderer.ReadBackbuffer(68, 8, 1, 1, edge);
        Check(edge[0] >= 60 && edge[1] >= 124 && edge[2] >= 188,
              "a resized non-aligned backbuffer retains its final logical column");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    TinyGLContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(65);
        gdm_->setPreferredBackBufferHeightProperty(67);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    TinyGLContractTest game;
    game.Run();
    return game.Result();
}
