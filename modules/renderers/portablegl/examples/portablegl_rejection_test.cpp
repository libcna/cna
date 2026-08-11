// SPDX-License-Identifier: MS-PL
// Deterministic-rejection regression test for the PortableGL renderer.
//
// A capability this renderer reports as false must be REFUSED at the public API, not accepted and
// quietly turned into something else. Three paths used to do exactly that: SpriteBatch.Begin with
// a custom Effect inherited a no-op SetCustomEffect and drew with the built-in sprite shader; an
// incompatible VertexDeclaration that happened to share the 16-byte stride was reinterpreted as
// VertexPositionColor; and a render-target binding was accepted and dropped.
//
// Each check pairs the refusal with a following VALID operation, because a renderer that refuses
// by leaving half-configured native state behind is no better than one that accepts.
//
// Check A -- SpriteBatch.Begin with a custom Effect throws, and an ordinary batch then draws.
// Check B -- a same-stride but incompatible VertexDeclaration is refused, and a compatible one
//   then draws.
// Check C -- a vertex buffer whose stride this renderer does not implement is refused.
// Check D -- binding a RenderTarget2D is refused, and an ordinary backbuffer draw then works.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "portablegl_test_support.hpp"

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace portablegl_test;

namespace
{
    constexpr int kSize = 32;
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);

    /// Same 16-byte stride and same byte offsets as PosColorDecl(), but the colour is declared at
    /// usage index 1. Nothing about the stride distinguishes the two, and the native layout binds
    /// Color0 -- so accepting this would feed the caller's Color1 bytes to a Color0 attribute.
    VertexDeclaration SameStrideIncompatibleDecl()
    {
        return VertexDeclaration(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    1),
        });
    }

    /// A stride this renderer's colored route does not implement at all.
    VertexDeclaration PositionTextureDecl()
    {
        return VertexDeclaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    }
}

class PortableGLRejectionTest : public PortableGLTestGame
{
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;

    Rectangle Whole() const { return Rectangle(0, 0, kSize, kSize); }

    void DrawQuadWith(const VertexDeclaration& declaration, const Color& color)
    {
        auto& dev = getGraphicsDeviceProperty();
        const auto verts = FullQuad(color);
        VertexBuffer vb(dev, declaration, 6, BufferUsage::None);
        vb.SetData(verts.data(), 0, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);
        const std::vector<std::uint8_t> pixels = {0, 255, 0, 255};
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, pixels));
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        // Check A -- a custom SpriteBatch Effect. SupportsCapability(CustomEffects) is false, so
        // the batch must not begin at all rather than begin and draw with the built-in shader.
        {
            dev.Clear(kBlack);
            BasicEffect custom(dev);
            bool threw = false;
            try
            {
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr,
                                    nullptr, nullptr, &custom);
                spriteBatch_->Draw(*texture_, Rectangle(0, 0, kSize, kSize),
                                   std::optional<Rectangle>(Rectangle(0, 0, 1, 1)), Color::White);
                spriteBatch_->End();
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "check A: SpriteBatch.Begin with a custom Effect is refused");
            check(RegionIs(Whole(), kBlack),
                  "check A: and the refused batch drew nothing with the built-in shader");

            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr,
                                nullptr, nullptr);
            spriteBatch_->Draw(*texture_, Rectangle(0, 0, kSize, kSize),
                               std::optional<Rectangle>(Rectangle(0, 0, 1, 1)), Color::White);
            spriteBatch_->End();
            check(RegionIs(Whole(), kGreen),
                  "check A: an ordinary batch on the same SpriteBatch still draws afterwards");
        }

        // Check B -- an incompatible declaration that shares the stride.
        {
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            dev.Clear(kBlack);
            bool threw = false;
            try
            {
                DrawQuadWith(SameStrideIncompatibleDecl(), kRed);
            }
            catch (const std::exception&) { threw = true; }
            check(threw,
                  "check B: a same-stride VertexDeclaration with a different semantic is refused");
            check(RegionIs(Whole(), kBlack),
                  "check B: and the refused draw wrote no pixels");

            DrawQuadWith(PosColorDecl(), kRed);
            check(RegionIs(Whole(), kRed),
                  "check B: the compatible declaration still draws after the refusal");
        }

        // Check C -- a stride the colored route does not implement.
        {
            dev.Clear(kBlack);
            bool threw = false;
            try
            {
                DrawQuadWith(PositionTextureDecl(), kRed);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "check C: an unimplemented vertex stride is refused");

            DrawQuadWith(PosColorDecl(), kGreen);
            check(RegionIs(Whole(), kGreen),
                  "check C: and a supported layout still draws afterwards");
        }

        // Check D -- render targets. PortableGL creates none, so a binding must fail rather than
        // redirecting the game's rendering into the back buffer without telling it.
        {
            dev.Clear(kBlack);
            bool threw = false;
            try
            {
                RenderTarget2D target(dev, 16, 16);
                std::vector<RenderTargetBinding> bindings;
                bindings.emplace_back(static_cast<Texture*>(&target));
                dev.SetRenderTargets(bindings);
            }
            catch (const std::exception&) { threw = true; }
            check(threw, "check D: binding a RenderTarget2D is refused");

            DrawQuadWith(PosColorDecl(), kRed);
            check(RegionIs(Whole(), kRed),
                  "check D: ordinary back-buffer rendering still works after the refusal");
        }

        Finish();
    }

public:
    PortableGLRejectionTest() : PortableGLTestGame(kSize) {}
};

int main()
{
    PortableGLRejectionTest game;
    game.Run();
    return game.getResult();
}
