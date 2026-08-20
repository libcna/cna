// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-10 (D9-100/D9-101/D9-102/D9-103): GraphicsProfile.Reach/HiDef made real on
// this renderer, through the real public GraphicsAdapter/Game/GraphicsDeviceManager/Texture2D API.
//
// Check A/B -- GraphicsAdapter::IsProfileSupported(): Reach is always true (every real D3D9 HAL
//   device already exceeds Reach's own floor, D9-32's own construction-time finding, re-used
//   here); HiDef is true on this real vs_3_0/ps_3_0-capable dev environment (matches DirectX9_Smoke's
//   own Check K precedent that HiDef device construction succeeds here).
// Check C/D -- GraphicsAdapter::QueryRenderTargetFormat(): SurfaceFormat.Color is accepted
//   unchanged under BOTH profiles (returns true); SurfaceFormat.Single (a HiDef-only format,
//   D9-100's own table) is REFUSED under GraphicsProfile.Reach specifically -- falls back to
//   Color, returns false -- this is the "at least one Reach-illegal request that must be
//   refused" D9-104's own text names as the required test shape.
// Check E -- the HiDef equivalent of Check D: the SAME SurfaceFormat.Single request, under
//   GraphicsProfile.HiDef, must be ALLOWED (not refused) -- D9-104's own required counterpart.
// Check F/G -- Texture2D size enforcement (D9-103): exactly GraphicsProfile.Reach's own 2048
//   ceiling succeeds; one pixel over throws System::NotSupportedException BEFORE any pixel data
//   is allocated (the check runs first in the constructor, so the "throws" case never pays for a
//   large allocation).
// Check H/I -- the HiDef equivalent: exactly 4096 succeeds, one pixel over throws.
// Check J/K -- MaxRenderTargets (D9-103 follow-up): binding 2 render targets via the real public
//   GraphicsDevice::SetRenderTargets() throws under Reach (its own MaxRenderTargets=1 ceiling,
//   D9-100's own table -- a SEPARATE, lower limit from XNA's general 4-target
//   MAX_RENDERTARGET_BINDINGS cap and from D9-54's own hardware NumSimultaneousRTs enforcement)
//   but succeeds under HiDef (ceiling=4) from the identical request.
// Check L/M -- TextureCube size enforcement (D9-103 follow-up): a normal-sized cube (64) still
//   succeeds under Reach (regression proof); one past Reach's own 512 ceiling (513) throws.
// Check N/O -- the HiDef counterpart: the SAME size (513) Check M refused now succeeds under
//   HiDef (ceiling=4096); one past HiDef's own 4096 ceiling (4097) throws.
// Check P/Q -- Texture3D (volume texture) enforcement (D9-103 follow-up): GraphicsProfile.Reach
//   forbids volume textures ENTIRELY (D9-100's own table, MaxVolumeExtent=0) -- even a trivial
//   1x1x1 request throws; the SAME request succeeds under HiDef.
// Check R -- the HiDef ceiling: one past HiDef's own 256 maximum volume extent throws.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

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
}

class D3D9GraphicsProfileTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        GraphicsAdapter& adapter = GraphicsAdapter::getDefaultAdapterProperty();

        // Check A/B: IsProfileSupported().
        check(adapter.IsProfileSupported(GraphicsProfile::Reach),
              "GraphicsAdapter::IsProfileSupported(GraphicsProfile.Reach): always true -- every "
              "real D3D9 HAL device already exceeds Reach's own floor");
        check(adapter.IsProfileSupported(GraphicsProfile::HiDef),
              "GraphicsAdapter::IsProfileSupported(GraphicsProfile.HiDef): true on this real "
              "vs_3_0/ps_3_0-capable dev environment");

        // Check C: SurfaceFormat.Color accepted unchanged under Reach.
        {
            SurfaceFormat selectedFormat;
            DepthFormat selectedDepth;
            int selectedMs;
            const bool accepted = adapter.QueryRenderTargetFormat(
                GraphicsProfile::Reach, SurfaceFormat::Color, DepthFormat::None, 0,
                selectedFormat, selectedDepth, selectedMs);
            check(accepted && selectedFormat == SurfaceFormat::Color,
                  "GraphicsAdapter::QueryRenderTargetFormat(Reach, SurfaceFormat.Color, ...): "
                  "accepted unchanged (true, selectedFormat==Color)");
        }

        // Check D: SurfaceFormat.Single (HiDef-only, D9-100's own table) REFUSED under Reach --
        // the "Reach-illegal request that must be refused" D9-104 names.
        {
            SurfaceFormat selectedFormat;
            DepthFormat selectedDepth;
            int selectedMs;
            const bool accepted = adapter.QueryRenderTargetFormat(
                GraphicsProfile::Reach, SurfaceFormat::Single, DepthFormat::None, 0,
                selectedFormat, selectedDepth, selectedMs);
            check(!accepted && selectedFormat == SurfaceFormat::Color,
                  "GraphicsAdapter::QueryRenderTargetFormat(Reach, SurfaceFormat.Single, ...): "
                  "REFUSED -- Single is HiDef-only (D9-100's own table), falls back to Color, "
                  "returns false");
        }

        // Check E: the SAME SurfaceFormat.Single request, under HiDef, must be ALLOWED --
        // D9-104's own required HiDef counterpart to Check D.
        {
            SurfaceFormat selectedFormat;
            DepthFormat selectedDepth;
            int selectedMs;
            const bool accepted = adapter.QueryRenderTargetFormat(
                GraphicsProfile::HiDef, SurfaceFormat::Single, DepthFormat::None, 0,
                selectedFormat, selectedDepth, selectedMs);
            check(accepted && selectedFormat == SurfaceFormat::Single,
                  "GraphicsAdapter::QueryRenderTargetFormat(HiDef, SurfaceFormat.Single, ...): "
                  "ALLOWED -- the SAME request Check D refused, now accepted under HiDef, proving "
                  "the profile (not just hardware support) genuinely gates the decision");
        }

        // Check F/G: Texture2D size enforcement under Reach (D9-103). The exact-ceiling case
        // (2048) allocates real pixel data (16MB); the over-ceiling case (2049) throws BEFORE any
        // allocation (the check runs first in both Texture2D constructors).
        {
            bool threwAtCeiling = false;
            try { Texture2D t(dev, 2048, 2048); } catch (const std::exception&) { threwAtCeiling = true; }
            check(!threwAtCeiling,
                  "Texture2D(dev, 2048, 2048) under GraphicsProfile.Reach: succeeds -- exactly at "
                  "Reach's own ceiling (D9-100's own table)");

            bool threwOverCeiling = false;
            try { Texture2D t(dev, 2049, 2049); }
            catch (const System::NotSupportedException&) { threwOverCeiling = true; }
            catch (const std::exception&) { /* wrong exception type -- leave threwOverCeiling false */ }
            check(threwOverCeiling,
                  "Texture2D(dev, 2049, 2049) under GraphicsProfile.Reach: throws "
                  "System::NotSupportedException -- one pixel past Reach's own 2048 ceiling");
        }

        // Check J: MaxRenderTargets under Reach (D9-103 follow-up) -- binding 2 render targets
        // via the real public GraphicsDevice::SetRenderTargets() throws, Reach's own ceiling is 1.
        {
            RenderTarget2D rt0(dev, 4, 4);
            RenderTarget2D rt1(dev, 4, 4);
            std::vector<RenderTargetBinding> bindings{RenderTargetBinding(&rt0), RenderTargetBinding(&rt1)};
            bool threw = false;
            try { dev.SetRenderTargets(bindings); }
            catch (const System::NotSupportedException&) { threw = true; }
            catch (const std::exception&) { /* wrong exception type -- leave threw false */ }
            dev.SetRenderTargets({}); // always unbind before this Game exits, threw or not
            check(threw,
                  "GraphicsDevice::SetRenderTargets() with 2 targets under GraphicsProfile.Reach: "
                  "throws System::NotSupportedException -- Reach's own MaxRenderTargets ceiling is 1");
        }

        // Check L/M: TextureCube size enforcement under Reach (D9-103 follow-up).
        {
            bool threwNormal = false;
            try { TextureCube c(dev, 64, false, SurfaceFormat::Color); } catch (const std::exception&) { threwNormal = true; }
            check(!threwNormal,
                  "TextureCube(dev, 64, ...) under GraphicsProfile.Reach: succeeds -- a normal-sized "
                  "cube is unaffected by the new size ceiling (regression proof)");

            bool threwOverCube = false;
            try { TextureCube c(dev, 513, false, SurfaceFormat::Color); }
            catch (const System::NotSupportedException&) { threwOverCube = true; }
            catch (const std::exception&) { /* wrong exception type -- leave threwOverCube false */ }
            check(threwOverCube,
                  "TextureCube(dev, 513, ...) under GraphicsProfile.Reach: throws "
                  "System::NotSupportedException -- one past Reach's own 512 cube-size ceiling");
        }

        // Check P: Texture3D (volume texture) is forbidden ENTIRELY under Reach -- even a
        // trivial 1x1x1 request throws (D9-100's own table: MaxVolumeExtent=0 for Reach).
        {
            bool threw = false;
            try { Texture3D t(dev, 1, 1, 1, false, SurfaceFormat::Color); }
            catch (const System::NotSupportedException&) { threw = true; }
            catch (const std::exception&) { /* wrong exception type -- leave threw false */ }
            check(threw,
                  "Texture3D(dev, 1, 1, 1, ...) under GraphicsProfile.Reach: throws "
                  "System::NotSupportedException -- Reach does not support volume textures at all");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9GraphicsProfileTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::Reach);
    }
};

// A SEPARATE HiDef-profile Game/GraphicsDeviceManager, matching DirectX9_Smoke's own Check K
// precedent (GraphicsProfile is fixed at device-construction time, not switchable mid-run) --
// Check H/I (the HiDef ceiling/over-ceiling pair) need a HiDef device, not the Reach one above.
class D3D9GraphicsProfileHiDefTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();

        // Check G-prime: a real, previously-undetected bug found and fixed while writing this
        // test -- Game's own GraphicsDevice_ member is eagerly default-constructed (hardcoded
        // GraphicsProfile::Reach) BEFORE GraphicsDeviceManager even exists, so
        // GraphicsDeviceManager::setGraphicsProfileProperty(HiDef) had NO path to ever reach the
        // real device: GraphicsDeviceManager::applyToExistingRenderer() carried the requested
        // profile into a transient GraphicsDeviceInformation but never wrote it back onto the
        // live GraphicsDevice before this fix. Fixed with a new CNAEXT
        // GraphicsDevice::SetGraphicsProfileEXT(), called from applyToExistingRenderer() right
        // before Reset(). Without this fix, dev.getGraphicsProfileProperty() here would
        // incorrectly report Reach even though this Game's own constructor requested HiDef --
        // which would also make Check H/I below meaningless (both would silently run under the
        // WRONG profile's ceiling).
        check(dev.getGraphicsProfileProperty() == GraphicsProfile::HiDef,
              "GraphicsDevice::getGraphicsProfileProperty() reports HiDef -- "
              "GraphicsDeviceManager::setGraphicsProfileProperty(HiDef) genuinely reached the "
              "real device, not silently ignored");

        // Check H/I: Texture2D size enforcement under HiDef. The exact-ceiling case (4096)
        // allocates real pixel data (64MB); the over-ceiling case (4097) throws BEFORE any
        // allocation.
        bool threwAtCeiling = false;
        try { Texture2D t(dev, 4096, 4096); } catch (const std::exception&) { threwAtCeiling = true; }
        check(!threwAtCeiling,
              "Texture2D(dev, 4096, 4096) under GraphicsProfile.HiDef: succeeds -- exactly at "
              "HiDef's own ceiling (D9-100's own table)");

        bool threwOverCeiling = false;
        try { Texture2D t(dev, 4097, 4097); }
        catch (const System::NotSupportedException&) { threwOverCeiling = true; }
        catch (const std::exception&) { /* wrong exception type -- leave threwOverCeiling false */ }
        check(threwOverCeiling,
              "Texture2D(dev, 4097, 4097) under GraphicsProfile.HiDef: throws "
              "System::NotSupportedException -- one pixel past HiDef's own 4096 ceiling");

        // Check K: MaxRenderTargets under HiDef -- the SAME 2-target request Check J refused
        // under Reach now succeeds (HiDef's own ceiling is 4).
        {
            RenderTarget2D rt0(dev, 4, 4);
            RenderTarget2D rt1(dev, 4, 4);
            std::vector<RenderTargetBinding> bindings{RenderTargetBinding(&rt0), RenderTargetBinding(&rt1)};
            bool threw = false;
            try { dev.SetRenderTargets(bindings); } catch (const std::exception&) { threw = true; }
            dev.SetRenderTargets({}); // unbind before this Game exits
            check(!threw,
                  "GraphicsDevice::SetRenderTargets() with 2 targets under GraphicsProfile.HiDef: "
                  "succeeds -- the SAME request Check J refused under Reach, HiDef's own ceiling is 4");
        }

        // Check N/O: TextureCube under HiDef -- the SAME size (513) Check M refused under Reach
        // now succeeds; one past HiDef's own 4096 ceiling (4097) throws.
        {
            bool threwAllowed = false;
            try { TextureCube c(dev, 513, false, SurfaceFormat::Color); } catch (const std::exception&) { threwAllowed = true; }
            check(!threwAllowed,
                  "TextureCube(dev, 513, ...) under GraphicsProfile.HiDef: succeeds -- the SAME "
                  "size Check M refused under Reach, HiDef's own cube-size ceiling is 4096");

            bool threwOverCube = false;
            try { TextureCube c(dev, 4097, false, SurfaceFormat::Color); }
            catch (const System::NotSupportedException&) { threwOverCube = true; }
            catch (const std::exception&) { /* wrong exception type -- leave threwOverCube false */ }
            check(threwOverCube,
                  "TextureCube(dev, 4097, ...) under GraphicsProfile.HiDef: throws "
                  "System::NotSupportedException -- one past HiDef's own 4096 cube-size ceiling");
        }

        // Check Q/R: Texture3D under HiDef -- the SAME 1x1x1 request Check P refused under Reach
        // now succeeds (volume textures ARE supported under HiDef); one past HiDef's own 256
        // maximum volume extent throws.
        {
            bool threwAllowed = false;
            try { Texture3D t(dev, 1, 1, 1, false, SurfaceFormat::Color); } catch (const std::exception&) { threwAllowed = true; }
            check(!threwAllowed,
                  "Texture3D(dev, 1, 1, 1, ...) under GraphicsProfile.HiDef: succeeds -- the SAME "
                  "request Check P refused under Reach, volume textures ARE supported under HiDef");

            bool threwOverVolume = false;
            try { Texture3D t(dev, 257, 1, 1, false, SurfaceFormat::Color); }
            catch (const System::NotSupportedException&) { threwOverVolume = true; }
            catch (const std::exception&) { /* wrong exception type -- leave threwOverVolume false */ }
            check(threwOverVolume,
                  "Texture3D(dev, 257, 1, 1, ...) under GraphicsProfile.HiDef: throws "
                  "System::NotSupportedException -- one past HiDef's own 256 maximum volume extent");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9GraphicsProfileHiDefTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
    }
};

int main()
{
    {
        D3D9GraphicsProfileTest game;
        game.Run();
    }
    {
        D3D9GraphicsProfileHiDefTest game;
        game.Run();
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
