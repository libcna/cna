// SPDX-License-Identifier: MS-PL
// Task 720: Verify DrawPrimitives/DrawIndexedPrimitives/DrawInstancedPrimitives throw the
// correct exception TYPE and MESSAGE on SDL_Renderer -- not just "throws something".
//
// SDL_Renderer is intentionally 2D-only: SdlRenderer::CreateVertexBuffer and
// CreateIndexBuffer16 (and, by default delegation, CreateIndexBuffer32) both throw
// std::runtime_error("SDL_Renderer does not support 3D: ...") unconditionally, and this
// happens INSIDE VertexBuffer's/IndexBuffer's own constructor (their member-initializer
// list calls GraphicsDevice::GetRenderer().CreateVertexBuffer/CreateIndexBuffer16 directly) --
// meaning a valid VertexBuffer/IndexBuffer object can never exist bound to an SDL_Renderer
// GraphicsDevice in the first place. As a result, GraphicsDevice::DrawPrimitives/
// DrawIndexedPrimitives/DrawInstancedPrimitives -- which check their bound buffers BEFORE ever
// reaching the renderer's own 3D-draw entry points -- always throw at that shared (not
// SDL_Renderer-specific) missing-buffer check on this renderer.
//
// SOFTWARE-253 restored Microsoft XNA's validation order and exception type: arguments first
// (so numVertices must be positive), then a missing effect, then -- for the indexed and instanced
// draws -- a missing index buffer before a missing vertex buffer, every one of them an
// InvalidOperationException. A BasicEffect is therefore applied first, as the EasyGL twin of this
// test (easygl_draw_novertexbuffer_test.cpp) does, and the indexed/instanced draws report the
// index buffer: with neither buffer constructible here, that is the refusal they reach.
// This test confirms that chain end to end: the exact exception type and exact message text for
// all 3 draw entry points, plus the underlying construction throws that explain why no buffer
// can ever be bound here.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <typeinfo>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlDrawPrimitivesThrowsTest : public Game
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

    // Runs fn(); returns true iff it threw EXACTLY std::runtime_error (not some other
    // std::exception subtype) whose what() is EXACTLY expectedMessage.
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

    // Runs fn(); returns true iff it threw EXACTLY System::InvalidOperationException (not a
    // subclass) whose what() is EXACTLY expectedMessage.
    template <typename F>
    static bool ThrowsExactInvalidOperation(F&& fn, const std::string& expectedMessage)
    {
        try
        {
            fn();
            return false;
        }
        catch (const System::InvalidOperationException& e)
        {
            return typeid(e) == typeid(System::InvalidOperationException) &&
                   std::strcmp(e.what(), expectedMessage.c_str()) == 0;
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

        // An applied effect gets every draw past XNA's "no effect" check to the missing buffer.
        BasicEffect effect(dev);
        effect.Apply();

        // --- The 3 draw entry points: exact exception type + exact message. ---
        check(ThrowsExactInvalidOperation(
                  [&] { dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1); },
                  "GraphicsDevice::DrawPrimitives: no vertex buffer is bound."),
              "DrawPrimitives with no bound vertex buffer throws InvalidOperationException with the exact expected message");

        check(ThrowsExactInvalidOperation(
                  [&] { dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1); },
                  "GraphicsDevice::DrawIndexedPrimitives: no index buffer is bound."),
              "DrawIndexedPrimitives with no bound index/vertex buffer throws InvalidOperationException with the exact expected message");

        check(ThrowsExactInvalidOperation(
                  [&] { dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1); },
                  "GraphicsDevice::DrawInstancedPrimitives: no index buffer is bound."),
              "DrawInstancedPrimitives with no bound index/vertex buffer throws InvalidOperationException with the exact expected message");

        // --- Why the above is the ONLY reachable state on this renderer: a VertexBuffer/
        //     IndexBuffer can never be successfully constructed here in the first place. ---
        check(ThrowsExactRuntimeError(
                  [&] { VertexBuffer vb(dev, 4); (void)vb; },
                  "SDL_Renderer does not support 3D: CreateVertexBuffer"),
              "Constructing a VertexBuffer on SDL_Renderer throws std::runtime_error with the exact expected message");

        check(ThrowsExactRuntimeError(
                  [&] { IndexBuffer ib(dev, 4); (void)ib; },
                  "SDL_Renderer does not support 3D: CreateIndexBuffer16"),
              "Constructing an IndexBuffer on SDL_Renderer throws std::runtime_error with the exact expected message");

        // --- The device must remain fully usable after all the throws above. ---
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
    SdlDrawPrimitivesThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // SOFTWARE-213 made GetBackBufferData HiDef-only, as in XNA 4.0, and this fixture reads the
        // back buffer -- so under GraphicsDeviceManager's default Reach profile it aborted before
        // its first check (plans/plan_gpu_test_isolation.md GTI-0007 names the class).
        gdm_->setGraphicsProfileProperty(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlDrawPrimitivesThrowsTest game;
    game.Run();
    return game.getResult();
}
