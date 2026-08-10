// SPDX-License-Identifier: MS-PL
// Task 723: Re-verify CreateVertexBuffer/CreateIndexBuffer/Dynamic variants throw correctly on
// SDL_Renderer with Task-261-style rigor (an earlier phase covered this at a lighter pass --
// this task exercises EVERY construction overload, not just the 2-arg ones, plus edge cases).
//
// SdlRenderer::CreateVertexBuffer/CreateIndexBuffer16 throw
// std::runtime_error("SDL_Renderer does not support 3D: ...") unconditionally; CreateIndexBuffer32
// is not separately overridden, so it uses IGraphicsRenderer's own default delegation to
// CreateIndexBuffer16 -- meaning a 32-bit IndexBuffer/DynamicIndexBuffer throws the SAME
// "CreateIndexBuffer16" message as a 16-bit one, not a distinct "CreateIndexBuffer32" message.
// DynamicVertexBuffer/DynamicIndexBuffer both delegate straight to VertexBuffer's/IndexBuffer's
// own constructor with their `dynamic` parameter completely unused (an ignored bool) -- so they
// throw identically to their non-dynamic base types, renderer-wise there is no distinction at all.
//
// This test exercises all 8 construction overloads/variants (VertexBuffer x2 ctors,
// DynamicVertexBuffer, IndexBuffer x2 index widths, DynamicIndexBuffer x2 index widths), plus
// confirms the throw fires unconditionally BEFORE any argument validation -- a zero-count and a
// negative-count construction both still throw the SAME renderer-unsupported message, not an
// ArgumentOutOfRangeException, since the renderer call happens in the member-initializer list
// before the constructor body (where any such validation would live) ever runs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlBufferConstructionThrowsTest : public Game
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
        const auto& vertexDecl = VertexPositionColor::getVertexDeclarationStatic();

        const std::string kNoVb = "SDL_Renderer does not support 3D: CreateVertexBuffer";
        const std::string kNoIb = "SDL_Renderer does not support 3D: CreateIndexBuffer16";

        // --- VertexBuffer: both public constructor overloads. ---
        check(ThrowsExactRuntimeError([&] { VertexBuffer vb(dev, 4); }, kNoVb),
              "VertexBuffer(device, count) throws the exact expected message");
        check(ThrowsExactRuntimeError([&] { VertexBuffer vb(dev, vertexDecl, 4, BufferUsage::None); }, kNoVb),
              "VertexBuffer(device, VertexDeclaration, count, BufferUsage) throws the exact expected message");

        // --- DynamicVertexBuffer: its `dynamic` flag is unused -- same renderer call, same throw. ---
        check(ThrowsExactRuntimeError([&] { DynamicVertexBuffer dvb(dev, vertexDecl, 4, BufferUsage::None); }, kNoVb),
              "DynamicVertexBuffer(...) throws the exact expected message, identical to VertexBuffer's");

        // --- IndexBuffer: both index widths. CreateIndexBuffer32 is not separately overridden on
        //     this renderer, so it delegates to CreateIndexBuffer16 -- same message, both widths. ---
        check(ThrowsExactRuntimeError([&] { IndexBuffer ib(dev, 4); }, kNoIb),
              "IndexBuffer(device, count) [16-bit default] throws the exact expected message");
        check(ThrowsExactRuntimeError(
                  [&] { IndexBuffer ib(dev, IndexElementSize::SixteenBits, 4, BufferUsage::None); }, kNoIb),
              "IndexBuffer(device, SixteenBits, count, BufferUsage) throws the exact expected message");
        check(ThrowsExactRuntimeError(
                  [&] { IndexBuffer ib(dev, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None); }, kNoIb),
              "IndexBuffer(device, ThirtyTwoBits, count, BufferUsage) throws CreateIndexBuffer16's message (32-bit path delegates to it)");

        // --- DynamicIndexBuffer: both widths, same delegation story as DynamicVertexBuffer. ---
        check(ThrowsExactRuntimeError(
                  [&] { DynamicIndexBuffer dib(dev, IndexElementSize::SixteenBits, 4, BufferUsage::None); }, kNoIb),
              "DynamicIndexBuffer(SixteenBits, ...) throws the exact expected message");
        check(ThrowsExactRuntimeError(
                  [&] { DynamicIndexBuffer dib(dev, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None); }, kNoIb),
              "DynamicIndexBuffer(ThirtyTwoBits, ...) throws CreateIndexBuffer16's message");

        // --- Edge case: the renderer throw fires BEFORE any argument validation -- zero and
        //     negative counts still hit the SAME message, not an ArgumentOutOfRangeException,
        //     since the renderer call happens in the member-initializer list. ---
        check(ThrowsExactRuntimeError([&] { VertexBuffer vb(dev, 0); }, kNoVb),
              "VertexBuffer(device, 0) still throws the renderer message, not a validation exception");
        check(ThrowsExactRuntimeError([&] { VertexBuffer vb(dev, -1); }, kNoVb),
              "VertexBuffer(device, -1) still throws the renderer message, not a validation exception");
        check(ThrowsExactRuntimeError([&] { IndexBuffer ib(dev, -1); }, kNoIb),
              "IndexBuffer(device, -1) still throws the renderer message, not a validation exception");

        // --- The device must remain fully usable after every throw above. ---
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
    SdlBufferConstructionThrowsTest()
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
    SdlBufferConstructionThrowsTest game;
    game.Run();
    return game.getResult();
}
