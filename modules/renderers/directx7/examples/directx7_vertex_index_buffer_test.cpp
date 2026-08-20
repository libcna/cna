// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O5 (DX2-40..DX2-42): DirectX7VertexBufferRenderer/DirectX7IndexBufferRenderer CTest.
// Neither IVertexBufferRenderer nor IIndexBufferRenderer exposes a GetData()-style readback (both
// are write-only from the public interface's own perspective, matching every other CNA renderer) --
// so "round-trip" here means: SetData() succeeds and the buffer's own count/width metadata
// (GetVertexCount/GetIndexCount/IsThirtyTwoBit) reports back exactly what was just set, and
// over-capacity SetData calls are rejected rather than silently overflowing.
//
// Check A -- CreateVertexBuffer + SetData: GetVertexCount() reports the exact count set.
// Check B -- CreateVertexBuffer: SetData with more vertices than capacity throws.
// Check C -- CreateIndexBuffer16 + SetData16: GetIndexCount() correct, IsThirtyTwoBit()==false.
// Check D -- CreateIndexBuffer32 + SetData32: GetIndexCount() correct, IsThirtyTwoBit()==true.
// Check E -- CreateIndexBuffer16 rejects SetData32 (bit-width mismatch) with a throw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/DirectX7/DirectX7Renderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX7;

static constexpr int kCanvasSize = 64;

class DirectX7VertexIndexBufferTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX7Renderer&>(dev.GetRenderer());

        // Check A: VertexPositionColor-shaped data (stride 16), SetData -> GetVertexCount().
        {
            auto vb = renderer.CreateVertexBuffer(8);
            struct { float x, y, z; uint32_t color; } verts[3] = {
                {0.0f, 0.0f, 0.0f, 0xFFFF0000u},
                {1.0f, 0.0f, 0.0f, 0xFF00FF00u},
                {0.0f, 1.0f, 0.0f, 0xFF0000FFu},
            };
            vb->SetData(verts, 3, sizeof(verts[0]));
            check(vb->GetVertexCount() == 3, "CreateVertexBuffer + SetData: GetVertexCount() reports exactly 3");
        }

        // Check B: over-capacity SetData throws.
        {
            auto vb = renderer.CreateVertexBuffer(2);
            struct { float x, y, z; uint32_t color; } verts[3] = {};
            bool threw = false;
            try { vb->SetData(verts, 3, sizeof(verts[0])); }
            catch (const std::exception&) { threw = true; }
            check(threw, "CreateVertexBuffer: SetData beyond capacity throws");
        }

        // Check C: 16-bit index buffer.
        {
            auto ib = renderer.CreateIndexBuffer16(6);
            uint16_t idx[3] = {0, 1, 2};
            ib->SetData16(idx, 3);
            check(ib->GetIndexCount() == 3 && !ib->IsThirtyTwoBit(),
                  "CreateIndexBuffer16 + SetData16: GetIndexCount()==3, IsThirtyTwoBit()==false");
        }

        // Check D: 32-bit index buffer.
        {
            auto ib = renderer.CreateIndexBuffer32(6);
            uint32_t idx[3] = {0, 1, 2};
            ib->SetData32(idx, 3);
            check(ib->GetIndexCount() == 3 && ib->IsThirtyTwoBit(),
                  "CreateIndexBuffer32 + SetData32: GetIndexCount()==3, IsThirtyTwoBit()==true");
        }

        // Check E: a 16-bit index buffer rejects a 32-bit SetData call (bit-width mismatch).
        {
            auto ib = renderer.CreateIndexBuffer16(6);
            uint32_t idx[3] = {0, 1, 2};
            bool threw = false;
            try { ib->SetData32(idx, 3); }
            catch (const std::exception&) { threw = true; }
            check(threw, "CreateIndexBuffer16: SetData32 (bit-width mismatch) throws");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 5);
        result_ = (passCount_ == 5) ? 0 : 1;
        Exit();
    }

public:
    DirectX7VertexIndexBufferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX7VertexIndexBufferTest game;
    game.Run();
    return game.getResult();
}
