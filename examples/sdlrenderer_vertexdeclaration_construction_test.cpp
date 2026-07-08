// SPDX-License-Identifier: MS-PL
// Task 724: Verify VertexDeclaration construction does NOT throw on SDL_Renderer -- it is pure
// data (stride + element list), no backend call is involved at all, so only an actual draw call
// using it should throw, never declaring/constructing the layout itself.
//
// Unlike VertexBuffer/IndexBuffer (whose constructors immediately call
// device.GetBackend().CreateVertexBuffer/CreateIndexBuffer16, throwing on this 2D-only backend --
// Tasks 720-723), VertexDeclaration extends GraphicsResource but every one of its constructors
// relies on GraphicsResource's default `device = nullptr` argument -- it is NEVER tied to any
// GraphicsDevice, let alone a specific backend. This test confirms all 4 constructor overloads
// construct cleanly on SDL_Renderer, and that passing the resulting declaration to
// GraphicsDevice::DrawUserPrimitives's raw+VertexDeclaration overload (Task 721) still throws
// only at the DRAW call, never at declaration time -- proving the "pure data, no backend touch"
// claim end to end rather than just asserting it.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlVertexDeclarationConstructionTest : public Game
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

        // --- All 4 constructor overloads construct cleanly, no backend involved. ---
        check(DoesNotThrow([&] { VertexDeclaration vd; (void)vd; }),
              "VertexDeclaration() [default] does not throw");

        check(DoesNotThrow([&] {
                  VertexDeclaration vd{
                      VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                      VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)
                  };
                  (void)vd;
              }),
              "VertexDeclaration(initializer_list<VertexElement>) [auto-stride] does not throw");

        check(DoesNotThrow([&] {
                  VertexDeclaration vd(16, {
                      VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                      VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)
                  });
                  (void)vd;
              }),
              "VertexDeclaration(stride, initializer_list<VertexElement>) does not throw");

        check(DoesNotThrow([&] {
                  std::vector<VertexElement> elems{
                      VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)
                  };
                  VertexDeclaration vd(12, elems);
                  (void)vd;
              }),
              "VertexDeclaration(stride, vector<VertexElement>) does not throw");

        // --- The constructed declaration reports the stride/elements it was given. ---
        VertexDeclaration decl(16, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)
        });
        check(decl.getVertexStrideProperty() == 16 && decl.GetVertexElements().size() == 2,
              "Constructed VertexDeclaration reports the exact stride and element count given");

        // --- Passing this declaration to an actual draw call still throws -- but only THERE,
        //     never during the declaration/construction above (Task 721's own finding: no effect
        //     applied throws first; this confirms construction itself never touches that path). ---
        static const VertexPositionColor vpc[1]{};
        bool threwOnDraw = false;
        try { dev.DrawUserPrimitives(PrimitiveType::TriangleList, vpc, 0, 1, decl); }
        catch (const std::exception&) { threwOnDraw = true; }
        check(threwOnDraw,
              "Using the declaration in an actual DrawUserPrimitives call still throws (at the draw, not the declaration)");

        // --- The device must remain fully usable afterward. ---
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
    SdlVertexDeclarationConstructionTest()
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
    SdlVertexDeclarationConstructionTest game;
    game.Run();
    return game.getResult();
}
