// SPDX-License-Identifier: MS-PL
// SOFTWARE-322: separate required XNA counts from natively forwarded buffered-draw ranges.
//
// Microsoft XNA and FNA validate positive required counts but pass vertex/index offsets, declared
// range hints, and native buffer capacity through to the graphics API. EasyGL must do the same:
//   DrawPrimitives:          primitiveCount < 1 throws; vertexStart is forwarded
//   DrawIndexedPrimitives:   primitiveCount/numVertices < 1 throw; offsets are forwarded
//   DrawInstancedPrimitives: primitiveCount < 1, instanceCount < 1
//   DrawUserPrimitives:      primitiveCount < 1

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <array>
#include <cstdint>
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

template<typename F>
static bool completes(F&& fn)
{
    try { fn(); return true; }
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

        // Use classic declared resources. CNAEXT's empty-declaration convenience buffer retains a
        // strict CPU-memory guard and is intentionally not evidence for the XNA contract.
        const std::array<VertexPositionColor, 6> vertices{};
        VertexBuffer vb(
            device, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(vertices.data(), 6);
        device.SetVertexBuffer(&vb);

        const std::array<std::uint16_t, 6> indices{};
        IndexBuffer ib(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        ib.SetData(indices.data(), 6);
        device.SetIndexBuffer(&ib);

        // Bind a BasicEffect so the effect-null guard is satisfied.
        BasicEffect fx(device);
        fx.Apply();

        // ── DrawPrimitives ─────────────────────────────────────────────────
        check(throwsAOOR([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, 0, 0); }),
              "DrawPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, 0, -1); }),
              "DrawPrimitives: primitiveCount=-1 throws ArgumentOutOfRangeException");
        check(completes([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, -1, 1); }),
              "DrawPrimitives: vertexStart=-1 is forwarded without a managed exception");
        check(completes([&]{ device.DrawPrimitives(PrimitiveType::TriangleList, 7, 1); }),
              "DrawPrimitives: a range past the vertex buffer is forwarded");

        // ── DrawIndexedPrimitives ──────────────────────────────────────────
        check(throwsAOOR([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 0, 0, 0); }),
              "DrawIndexedPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        check(completes([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, -1, 1); }),
              "DrawIndexedPrimitives: startIndex=-1 is forwarded");
        check(completes([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, -1, 0, 6, 0, 1); }),
              "DrawIndexedPrimitives: baseVertex=-1 is forwarded");
        check(completes([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, -1, 6, 0, 1); }),
              "DrawIndexedPrimitives: minVertexIndex=-1 is forwarded as a hint");
        check(completes([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 4, 1); }),
              "DrawIndexedPrimitives: an index range crossing the buffer end is forwarded");

        // ── DrawInstancedPrimitives ────────────────────────────────────────
        check(throwsAOOR([&]{ device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 0, 1); }),
              "DrawInstancedPrimitives: primitiveCount=0 throws ArgumentOutOfRangeException");
        check(throwsAOOR([&]{ device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 1, 0); }),
              "DrawInstancedPrimitives: instanceCount=0 throws ArgumentOutOfRangeException");

        check(completes([&]{ device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 1); }),
              "a valid indexed draw still works after every native-range probe");

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
