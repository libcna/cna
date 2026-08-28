// SPDX-License-Identifier: MS-PL
// WEBGPU-47: a disposed VertexBuffer / IndexBuffer must throw System::ObjectDisposedException from
// SetData on the WebGPU backend, not upload into a released buffer or crash.
//
// The guard itself lives at the XNA GraphicsResource layer (VertexBuffer.cpp / IndexBuffer.cpp
// check getIsDisposedProperty() before any renderer call), so it is renderer-agnostic; this test
// proves the WebGPU device-construction path leaves it intact -- i.e. the renderer does not
// somehow satisfy SetData on a disposed buffer. It also confirms a normal (non-disposed) upload
// still works on the same device, so the exception is disposal-specific, not a broken buffer.
//
// Check A -- a live VertexBuffer.SetData succeeds (baseline: uploads are working on this device).
// Check B -- after Dispose(), VertexBuffer.SetData throws System::ObjectDisposedException.
// Check C -- a live IndexBuffer.SetData succeeds.
// Check D -- after Dispose(), IndexBuffer.SetData throws System::ObjectDisposedException.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // Returns true iff calling fn() threw System::ObjectDisposedException specifically.
    template <typename Fn>
    bool throwsObjectDisposed(Fn&& fn)
    {
        try
        {
            fn();
            return false;
        }
        catch (const System::ObjectDisposedException&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

class WebGpuDisposedGuardTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();

        const VertexPositionColor verts[3] = {
            {Vector3(0.0f, 0.5f, 0.0f), Color(255, 0, 0, 255)},
            {Vector3(-0.5f, -0.5f, 0.0f), Color(0, 255, 0, 255)},
            {Vector3(0.5f, -0.5f, 0.0f), Color(0, 0, 255, 255)},
        };
        const std::uint16_t indices[3] = {0, 1, 2};

        // Check A: a live upload succeeds.
        {
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3,
                            BufferUsage::None);
            bool ok = true;
            try { vb.SetData(verts, 3); }
            catch (...) { ok = false; }
            check(ok, "Check A: SetData on a live VertexBuffer succeeds");
        }

        // Check B: SetData after Dispose() throws ObjectDisposedException.
        {
            VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3,
                            BufferUsage::None);
            vb.Dispose();
            check(throwsObjectDisposed([&] { vb.SetData(verts, 3); }),
                  "Check B: SetData on a disposed VertexBuffer throws ObjectDisposedException");
        }

        // Check C: a live index upload succeeds.
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            bool ok = true;
            try { ib.SetData(indices, 3); }
            catch (...) { ok = false; }
            check(ok, "Check C: SetData on a live IndexBuffer succeeds");
        }

        // Check D: SetData after Dispose() throws ObjectDisposedException.
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ib.Dispose();
            check(throwsObjectDisposed([&] { ib.SetData(indices, 3); }),
                  "Check D: SetData on a disposed IndexBuffer throws ObjectDisposedException");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuDisposedGuardTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuDisposedGuardTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
