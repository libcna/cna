// SPDX-License-Identifier: MS-PL
// Task 205: Validate primitive count and vertex/index ranges in draw calls.
//
// Guards added (CNAEXT — FNA does not validate at this level):
//   DrawPrimitives:          primitiveCount < 1, vertexStart < 0
//   DrawIndexedPrimitives:   primitiveCount < 1, startIndex < 0, baseVertex < 0
//   DrawInstancedPrimitives: primitiveCount < 1, instanceCount < 1
//   DrawUserPrimitives:      primitiveCount < 1
//
// Each bad argument must throw System::ArgumentOutOfRangeException.
// A BasicEffect is bound via Apply() so that the effect-null guard is
// satisfied and control reaches the range guards.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

template<typename F>
static bool throwsAOOR(F&& fn)
{
    try { fn(); return false; }
    catch (const System::ArgumentOutOfRangeException&) { return true; }
    catch (...) { return false; }
}

class DrawRangeValidationTest : public Game
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
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();

        // Bind a vertex buffer and an index buffer so the resource guards pass.
        VertexBuffer vb(device, 6);
        device.SetVertexBuffer(&vb);

        IndexBuffer ib(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        device.SetIndexBuffer(&ib);

        // Bind a BasicEffect so the effect-null guard is satisfied.
        BasicEffect fx(device);
        fx.Apply();

        // ── DrawPrimitives ─────────────────────────────────────────────────
        check(throwsAOOR([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, 0, 0); }),
              "DrawPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, 0, -1); }),
              "DrawPrimitives: primitiveCount=-1 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, -1, 1); }),
              "DrawPrimitives: vertexStart=-1 throws ArgumentOutOfRangeException");

        // ── DrawIndexedPrimitives ──────────────────────────────────────────
        check(throwsAOOR([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 0); }),
              "DrawIndexedPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, -1, 1); }),
              "DrawIndexedPrimitives: startIndex=-1 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, -1, 0, 0, 0, 1); }),
              "DrawIndexedPrimitives: baseVertex=-1 throws ArgumentOutOfRangeException");

        // ── DrawInstancedPrimitives ────────────────────────────────────────
        check(throwsAOOR([&]{ device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 0, 1); }),
              "DrawInstancedPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 1, 0); }),
              "DrawInstancedPrimitives: instanceCount=0 throws ArgumentOutOfRangeException");

        // ── DrawUserPrimitives ─────────────────────────────────────────────
        {
            VertexPositionColor dummy[3]{};
            check(throwsAOOR([&]{ device.DrawUserPrimitives(PrimitiveType::TriangleList, dummy, 0, 0); }),
                  "DrawUserPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    DrawRangeValidationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DrawRangeValidationTest game;
    game.Run();
    return game.getResult();
}
