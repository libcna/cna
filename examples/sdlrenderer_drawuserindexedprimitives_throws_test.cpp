// SPDX-License-Identifier: MS-PL
// Task 722: Verify all 10 DrawUserIndexedPrimitives typed + VertexDeclaration overloads throw
// the correct exception TYPE and MESSAGE on SDL_Renderer -- 4 typed vertex types (VertexPositionColor,
// VertexPositionColorTexture, VertexPositionTexture, VertexPositionNormalTexture) x 2 index widths
// (16-bit/32-bit) = 8, plus the raw-pointer + explicit VertexDeclaration overload x 2 index widths = 10.
//
// Same two-path shape as Task 721's DrawUserPrimitives: each overload checks "has an Effect been
// applied" FIRST (shared, not SDL_Renderer-specific) -- with no effect applied, all 10 throw
// std::runtime_error("GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.").
// With a real effect applied, every one of the 10 implementations calls
// backend_->CreateVertexBuffer(numVertices) BEFORE backend_->CreateIndexBuffer16/32(indexCount) --
// so CreateVertexBuffer's own std::runtime_error("SDL_Renderer does not support 3D:
// CreateVertexBuffer") always fires first, for BOTH the 16-bit and 32-bit index overloads alike.
// This means CreateIndexBuffer16/32's own ThrowNo3D message is never actually reachable through
// ANY DrawUserIndexedPrimitives call on this backend -- a finding worth confirming explicitly,
// not just assumed.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlDrawUserIndexedPrimitivesThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template <typename F>
    static bool ThrowsExactRuntimeError(F&& fn, const std::string& expectedMessage)
    {
        try
        {
            fn();
            return false;
        }
        catch (const std::runtime_error& e)
        {
            return std::strcmp(e.what(), expectedMessage.c_str()) == 0;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        static const VertexPositionColor vpc[3] = {
            {Vector3::Zero, Color::White}, {Vector3::Zero, Color::White}, {Vector3::Zero, Color::White}
        };
        static const VertexPositionColorTexture vpct[3] = {
            {Vector3::Zero, Color::White, Vector2::Zero},
            {Vector3::Zero, Color::White, Vector2::Zero},
            {Vector3::Zero, Color::White, Vector2::Zero}
        };
        static const VertexPositionTexture       vpt[3]{};
        static const VertexPositionNormalTexture vpnt[3]{};
        static const std::uint16_t idx16[3]{0, 1, 2};
        static const std::uint32_t idx32[3]{0, 1, 2};
        const auto& vertexDecl = VertexPositionColor::getVertexDeclarationStatic();

        const std::string kNoEffect = "GraphicsDevice::DrawUserIndexedPrimitives: no effect has been applied.";
        const std::string kNo3D     = "SDL_Renderer does not support 3D: CreateVertexBuffer";

        // --- No effect applied: all 10 overloads throw the shared "no effect" message. ---
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx16, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionColor, 16-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpct, 0, 3, idx16, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionColorTexture, 16-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpt, 0, 3, idx16, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionTexture, 16-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpnt, 0, 3, idx16, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionNormalTexture, 16-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx32, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionColor, 32-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpct, 0, 3, idx32, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionColorTexture, 32-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpt, 0, 3, idx32, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionTexture, 32-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpnt, 0, 3, idx32, 0, 1); }, kNoEffect),
              "DrawUserIndexedPrimitives(VertexPositionNormalTexture, 32-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx16, 0, 1, vertexDecl); }, kNoEffect),
              "DrawUserIndexedPrimitives(raw+VertexDeclaration, 16-bit) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx32, 0, 1, vertexDecl); }, kNoEffect),
              "DrawUserIndexedPrimitives(raw+VertexDeclaration, 32-bit) with no applied effect throws the exact expected message");

        // --- Apply a real effect (does not throw on SDL_Renderer -- backend-agnostic until draw). ---
        BasicEffect effect(dev);
        bool effectApplyThrew = false;
        try { effect.Apply(); } catch (const std::exception&) { effectApplyThrew = true; }
        check(!effectApplyThrew, "BasicEffect::Apply() does not throw on SDL_Renderer");

        // --- With an effect applied: all 10 overloads reach backend_->CreateVertexBuffer FIRST
        //     (before CreateIndexBuffer16/32), so all 10 -- both 16-bit and 32-bit index variants
        //     alike -- throw the SAME CreateVertexBuffer message, never CreateIndexBuffer16/32's. ---
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx16, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionColor, 16-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpct, 0, 3, idx16, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionColorTexture, 16-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpt, 0, 3, idx16, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionTexture, 16-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpnt, 0, 3, idx16, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionNormalTexture, 16-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx32, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionColor, 32-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpct, 0, 3, idx32, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionColorTexture, 32-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpt, 0, 3, idx32, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionTexture, 32-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpnt, 0, 3, idx32, 0, 1); }, kNo3D),
              "DrawUserIndexedPrimitives(VertexPositionNormalTexture, 32-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx16, 0, 1, vertexDecl); }, kNo3D),
              "DrawUserIndexedPrimitives(raw+VertexDeclaration, 16-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vpc, 0, 3, idx32, 0, 1, vertexDecl); }, kNo3D),
              "DrawUserIndexedPrimitives(raw+VertexDeclaration, 32-bit) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");

        // --- The device must remain fully usable after all 20 throws above. ---
        dev.Clear(Color(0, 255, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional after every throw above");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlDrawUserIndexedPrimitivesThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlDrawUserIndexedPrimitivesThrowsTest game;
    game.Run();
    return game.getResult();
}
