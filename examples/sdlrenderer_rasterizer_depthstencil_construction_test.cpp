// SPDX-License-Identifier: MS-PL
// Task 729: Verify RasterizerState/DepthStencilState can be constructed and assigned without
// throwing on SDL_Renderer -- pure data, mirrors Task 724's VertexDeclaration pattern.
//
// Unlike VertexBuffer/IndexBuffer/Texture3D/etc. (which call a backend factory method that either
// throws -- Tasks 720-723, 727 -- or is BLOCKED pending a decision -- Task 725),
// GraphicsDevice::setRasterizerStateProperty/setDepthStencilStateProperty call
// IGraphicsBackend::ApplyRasterizerState/ApplyDepthStencilState, whose documented DEFAULT
// (in IGraphicsBackend.hpp) is an explicit no-op -- "Applies a RasterizerState to the backend.
// Default: no-op." / "Applies a DepthStencilState to the backend. Default: no-op." --
// SdlGraphicsBackend does not override either, so both are genuine, already-decided,
// intentional no-ops on this 2D-only backend: construction and assignment never touch anything
// that could throw, and the property values themselves round-trip correctly regardless (they are
// pure data, same as RasterizerState/DepthStencilState's own GraphicsResource base -- like
// VertexDeclaration, Task 724 -- never actually reaching GPU state on this backend).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlRasterizerDepthStencilConstructionTest : public Game
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
    static bool DoesNotThrow(F&& fn)
    {
        try { fn(); return true; }
        catch (const std::exception&) { return false; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // --- Construction: default, named-preset-style, and custom property values. ---
        check(DoesNotThrow([&] { RasterizerState rs; (void)rs; }),
              "RasterizerState() [default] does not throw");
        check(DoesNotThrow([&] {
                  RasterizerState rs;
                  rs.setCullModeProperty(CullMode::CullClockwiseFace);
                  rs.setFillModeProperty(FillMode::WireFrame);
                  rs.setScissorTestEnableProperty(true);
              }),
              "RasterizerState() with property setters does not throw");
        check(DoesNotThrow([&] { DepthStencilState ds; (void)ds; }),
              "DepthStencilState() [default] does not throw");
        check(DoesNotThrow([&] {
                  DepthStencilState ds;
                  ds.setDepthBufferEnableProperty(true);
                  ds.setDepthBufferWriteEnableProperty(true);
                  ds.setStencilEnableProperty(true);
              }),
              "DepthStencilState() with property setters does not throw");

        // --- Assignment via GraphicsDevice: this is where a genuine backend call happens
        //     (ApplyRasterizerState/ApplyDepthStencilState) -- confirmed to never throw here. ---
        check(DoesNotThrow([&] { dev.setRasterizerStateProperty(RasterizerState::CullNone); }),
              "setRasterizerStateProperty(CullNone) does not throw");
        check(DoesNotThrow([&] { dev.setRasterizerStateProperty(RasterizerState::CullClockwise); }),
              "setRasterizerStateProperty(CullClockwise) does not throw");
        check(DoesNotThrow([&] { dev.setDepthStencilStateProperty(DepthStencilState::None); }),
              "setDepthStencilStateProperty(None) does not throw");
        check(DoesNotThrow([&] { dev.setDepthStencilStateProperty(DepthStencilState::Default); }),
              "setDepthStencilStateProperty(Default) does not throw");

        // --- The assigned state is genuinely stored and reported back (pure data, not silently
        //     dropped), even though this backend's own GPU application is a no-op. ---
        dev.setRasterizerStateProperty(RasterizerState::CullClockwise);
        check(dev.getRasterizerStateProperty().getCullModeProperty() == CullMode::CullClockwiseFace,
              "GraphicsDevice reports back the exact RasterizerState just assigned");
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        check(dev.getDepthStencilStateProperty().getDepthBufferEnableProperty() == false,
              "GraphicsDevice reports back the exact DepthStencilState just assigned");

        // --- The device must remain fully usable throughout. ---
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color(0, 255, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional throughout");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlRasterizerDepthStencilConstructionTest()
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
    SdlRasterizerDepthStencilConstructionTest game;
    game.Run();
    return game.getResult();
}
