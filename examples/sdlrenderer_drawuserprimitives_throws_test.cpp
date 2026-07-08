// SPDX-License-Identifier: MS-PL
// Task 721: Verify all 5 DrawUserPrimitives typed + VertexDeclaration overloads throw the
// correct exception TYPE and MESSAGE on SDL_Renderer -- covering VertexPositionColor,
// VertexPositionColorTexture, VertexPositionTexture, VertexPositionNormalTexture, and the
// raw-pointer + explicit VertexDeclaration overload.
//
// Unlike Task 720's DrawPrimitives/DrawIndexedPrimitives/DrawInstancedPrimitives (which check
// for an already-bound VertexBuffer that can never exist on this backend), DrawUserPrimitives
// builds its OWN transient vertex buffer from caller-supplied data on every call. Each of the 5
// overloads checks "has an Effect been applied" FIRST (a shared, not SDL_Renderer-specific check)
// -- so with NO effect applied, all 5 throw std::runtime_error("GraphicsDevice::DrawUserPrimitives:
// no effect has been applied."). But WITH a real Effect applied (BasicEffect::Apply() itself does
// not throw on SDL_Renderer -- effects are backend-agnostic data objects until actually used to
// draw, confirmed separately by Task 726), each overload proceeds to
// backend_->CreateVertexBuffer(n), which DOES throw SDL_Renderer's own
// std::runtime_error("SDL_Renderer does not support 3D: CreateVertexBuffer"). This test exercises
// BOTH reachable exception paths for all 5 overloads -- 10 distinct throw checks in total.

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

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlDrawUserPrimitivesThrowsTest : public Game
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
        const auto& vertexDecl = VertexPositionColor::getVertexDeclarationStatic();

        const std::string kNoEffect = "GraphicsDevice::DrawUserPrimitives: no effect has been applied.";
        const std::string kNo3D     = "SDL_Renderer does not support 3D: CreateVertexBuffer";

        // --- No effect applied yet: all 5 overloads throw the shared "no effect" message. ---
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1); }, kNoEffect),
              "DrawUserPrimitives(VertexPositionColor) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpct, 0, 1); }, kNoEffect),
              "DrawUserPrimitives(VertexPositionColorTexture) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpt, 0, 1); }, kNoEffect),
              "DrawUserPrimitives(VertexPositionTexture) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpnt, 0, 1); }, kNoEffect),
              "DrawUserPrimitives(VertexPositionNormalTexture) with no applied effect throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1, vertexDecl); }, kNoEffect),
              "DrawUserPrimitives(raw+VertexDeclaration) with no applied effect throws the exact expected message");

        // --- Apply a real effect (does not throw on SDL_Renderer -- backend-agnostic until draw). ---
        BasicEffect effect(dev);
        bool effectApplyThrew = false;
        try { effect.Apply(); } catch (const std::exception&) { effectApplyThrew = true; }
        check(!effectApplyThrew, "BasicEffect::Apply() does not throw on SDL_Renderer");

        // --- With an effect applied: all 5 overloads now reach backend_->CreateVertexBuffer,
        //     which throws SDL_Renderer's own 3D-unsupported message. ---
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1); }, kNo3D),
              "DrawUserPrimitives(VertexPositionColor) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpct, 0, 1); }, kNo3D),
              "DrawUserPrimitives(VertexPositionColorTexture) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpt, 0, 1); }, kNo3D),
              "DrawUserPrimitives(VertexPositionTexture) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpnt, 0, 1); }, kNo3D),
              "DrawUserPrimitives(VertexPositionNormalTexture) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");
        check(ThrowsExactRuntimeError([&] { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1, vertexDecl); }, kNo3D),
              "DrawUserPrimitives(raw+VertexDeclaration) with an applied effect throws SDL_Renderer's exact 3D-unsupported message");

        // --- The device must remain fully usable after all 10 throws above. ---
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
    SdlDrawUserPrimitivesThrowsTest()
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
    SdlDrawUserPrimitivesThrowsTest game;
    game.Run();
    return game.getResult();
}
