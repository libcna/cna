// SPDX-License-Identifier: MS-PL
// Task 203: Verify draw calls throw std::runtime_error when no vertex buffer is bound.
//
// DrawPrimitives, DrawIndexedPrimitives, and DrawInstancedPrimitives all guard
// against a null currentVertexBuffer_ and throw before touching the GPU backend.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DrawNoVertexBufferTest : public Game
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

        // Ensure no vertex buffer is bound (default after init).
        device.SetVertexBuffer(nullptr);

        // 1. DrawPrimitives with no VB bound must throw std::runtime_error.
        {
            bool threw = false;
            try { device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1); }
            catch (const std::runtime_error&) { threw = true; }
            catch (...) {}
            check(threw, "DrawPrimitives throws runtime_error when no VB bound");
        }

        // 2. DrawIndexedPrimitives with no VB bound must throw std::runtime_error.
        {
            bool threw = false;
            try { device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 1); }
            catch (const std::runtime_error&) { threw = true; }
            catch (...) {}
            check(threw, "DrawIndexedPrimitives throws runtime_error when no VB bound");
        }

        // 3. DrawInstancedPrimitives with no VB bound must throw std::runtime_error.
        {
            bool threw = false;
            try { device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 1, 1); }
            catch (const std::runtime_error&) { threw = true; }
            catch (...) {}
            check(threw, "DrawInstancedPrimitives throws runtime_error when no VB bound");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    DrawNoVertexBufferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DrawNoVertexBufferTest game;
    game.Run();
    return game.getResult();
}
