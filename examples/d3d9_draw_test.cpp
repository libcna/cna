// SPDX-License-Identifier: MS-PL
// plan_dx9.md Phase D9-8 (D9-82): this backend's first real 3D triangle. Proves
// DrawColoredPrimitives()/DrawIndexedColoredPrimitives() -- the real BasicEffect
// VertexColorEnabled-only path (ShaderIndex 3, "BasicEffect_VSBasicVcNoFog"/
// "BasicEffect_PSBasicNoFog") -- through the real public Game/GraphicsDeviceManager/
// GraphicsDevice API, mirroring D3D11_Smoke's own Check P exactly (same "oversized triangle"
// full-NDC trick, same before/after-Clear() discipline, same CullMode::None reset so this basic
// check does not depend on D9-21's still-open D3DCULL winding proof).
//
// Check A -- DrawColoredPrimitives: a known vertex color paints over a known-blue Clear()
//   background at the same readback location (before=blue, after=red).
// Check B -- DrawIndexedColoredPrimitives: same proof, indexed path.
// Check C -- real WorldViewProj register upload: translating World far outside the NDC cube
//   ([-1,1]) leaves the background genuinely UNCHANGED -- proves the constant upload is real and
//   actually affects the transform, not a hardcoded/ignored no-op that would paint the same
//   full-screen triangle regardless of World.
//
// Real finding (verified empirically, not assumed -- see plan_dx9.md D9-82's own closure note):
// D9-22's original D3D9VertexDeclarations.hpp chose D3DDECLTYPE_D3DCOLOR for the COLOR0 element,
// mirroring D3D11's own DXGI_FORMAT_R8G8B8A8_UNORM choice by "same semantic meaning" -- but
// D3DDECLTYPE_D3DCOLOR's real contract (MSDN D3DDECLTYPE enumeration: "Input is a D3DCOLOR and is
// expanded to RGBA order") expects ARGB-packed memory bytes (B,G,R,A ascending), while XNA's own
// Color.PackedValue is R,G,B,A ascending (D3D9FormatMapping.cpp's own comment already established
// this for render-target formats) -- feeding CNA's native byte order through D3DDECLTYPE_D3DCOLOR
// silently swaps the R and B channels. Fixed by switching to D3DDECLTYPE_UBYTE4N (four bytes
// normalized in their existing order, no ARGB swizzle) -- exactly matching XNA's real memory
// layout with zero reordering. This test's exact-red (not swapped-to-blue) readback is the
// empirical proof the fix is correct, not merely plausible.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include "CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::D3D9;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    struct VPC { float x, y, z; uint32_t color; };

    // Same full-NDC "oversized triangle" trick D3D11_Smoke's own Check P established: with
    // world=view=projection=Identity, these Position values ARE clip-space coordinates directly.
    // 0xFF0000FFu packed as XNA's own R,G,B,A ascending-byte Color convention = opaque red.
    const VPC kTri[3] = {
        {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
        { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
        {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
    };
    const uint16_t kTriIdx[3] = {0, 1, 2};
}

class D3D9DrawTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<D3D9GraphicsBackend&>(dev.GetBackend());

        // Known-safe baseline, matching D3D11_Smoke's own Check P discipline: opaque blend, no
        // depth/stencil, no culling (so this basic check does not depend on D9-21's still-open
        // D3DCULL winding proof).
        backend.ApplyBlendState(0, 0, 1, 1, 0, 0);
        backend.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        backend.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);

        const Microsoft::Xna::Framework::Rectangle centerRegion(28, 28, 4, 4);

        // Check A: DrawColoredPrimitives (non-indexed).
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            std::vector<Color> before(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, before.data(), 0, static_cast<int>(before.size()));

            backend.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);

            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));

            bool beforeIsBlue = true, afterIsRed = true;
            for (const Color& p : before)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    beforeIsBlue = false;
            for (const Color& p : after)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    afterIsRed = false;
            check(beforeIsBlue && afterIsRed,
                  "D3D9GraphicsBackend::DrawColoredPrimitives(): real BasicEffect-VertexColor draw paints "
                  "the EXACT vertex color (R,G,B channels in the correct order, not swapped) over the "
                  "Clear() background at the same readback location");
        }

        // Check B: DrawIndexedColoredPrimitives (indexed).
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));
            auto ib = backend.CreateIndexBuffer16(3);
            ib->SetData16(kTriIdx, 3);

            dev.Clear(Color(0, 0, 255, 255));
            std::vector<Color> before(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, before.data(), 0, static_cast<int>(before.size()));

            backend.DrawIndexedColoredPrimitives(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);

            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));

            bool beforeIsBlue = true, afterIsRed = true;
            for (const Color& p : before)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    beforeIsBlue = false;
            for (const Color& p : after)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    afterIsRed = false;
            check(beforeIsBlue && afterIsRed,
                  "D3D9GraphicsBackend::DrawIndexedColoredPrimitives(): real indexed draw paints the "
                  "exact vertex color over the Clear() background at the same readback location");
        }

        // Check C: the WorldViewProj register upload is real -- a World translation that moves the
        // triangle entirely outside the NDC cube leaves the background genuinely unpainted. If the
        // constant upload were a no-op (e.g. wrong register, or silently never called), the same
        // full-NDC-covering triangle geometry would still paint the whole viewport regardless of
        // World, and this check would (correctly) fail.
        {
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            const Matrix farAway = Matrix::CreateTranslation(1000.0f, 0.0f, 0.0f);
            backend.DrawColoredPrimitives(*vb, farAway, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);

            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));
            bool stillBlue = true;
            for (const Color& p : after)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    stillBlue = false;
            check(stillBlue,
                  "D3D9GraphicsBackend::DrawColoredPrimitives(): WorldViewProj constant upload is real -- "
                  "translating World far outside the NDC cube genuinely leaves the background unpainted");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9DrawTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    {
        D3D9DrawTest game;
        game.Run();
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
