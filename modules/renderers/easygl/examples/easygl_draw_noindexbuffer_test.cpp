// SPDX-License-Identifier: MS-PL
// Task 204: Verify indexed draw calls throw std::runtime_error when no index buffer is bound.
//
// With a vertex buffer bound (so the VB-null guard passes), both
// DrawIndexedPrimitives and DrawInstancedPrimitives must throw before
// reaching the GPU renderer when currentIndexBuffer_ is null.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DrawNoIndexBufferTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // Bind a vertex buffer so the VB-null guard is satisfied.
        VertexBuffer vb(device, 6);
        device.SetVertexBuffer(&vb);

        // Ensure no index buffer is bound (default after init).
        device.SetIndexBuffer(nullptr);

        // 1. DrawIndexedPrimitives with no IB bound must throw std::runtime_error.
        {
            bool threw = false;
            try { device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 1); }
            catch (const std::runtime_error&) { threw = true; }
            catch (...) {}
            check(threw, "DrawIndexedPrimitives throws runtime_error when no IB bound");
        }

        // 2. DrawInstancedPrimitives with no IB bound must throw std::runtime_error.
        {
            bool threw = false;
            try { device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 1, 1); }
            catch (const std::runtime_error&) { threw = true; }
            catch (...) {}
            check(threw, "DrawInstancedPrimitives throws runtime_error when no IB bound");
        }

        // Clean up
        device.SetVertexBuffer(nullptr);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    DrawNoIndexBufferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DrawNoIndexBufferTest game;
    game.Run();
    return game.getResult();
}
