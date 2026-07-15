// SPDX-License-Identifier: MS-PL
// plan_dx9.md Phase D9-10 (D9-100/D9-101/D9-102/D9-103): GraphicsProfile.Reach/HiDef made real on
// this backend, through the real public GraphicsAdapter/Game/GraphicsDeviceManager/Texture2D API.
//
// Check A/B -- GraphicsAdapter::IsProfileSupported(): Reach is always true (every real D3D9 HAL
//   device already exceeds Reach's own floor, D9-32's own construction-time finding, re-used
//   here); HiDef is true on this real vs_3_0/ps_3_0-capable dev environment (matches D3D9_Smoke's
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
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <memory>

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

// A SEPARATE HiDef-profile Game/GraphicsDeviceManager, matching D3D9_Smoke's own Check K
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
        // real device: GraphicsDeviceManager::applyToExistingBackend() carried the requested
        // profile into a transient GraphicsDeviceInformation but never wrote it back onto the
        // live GraphicsDevice before this fix. Fixed with a new NOXNA
        // GraphicsDevice::SetGraphicsProfileEXT(), called from applyToExistingBackend() right
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
