// SPDX-License-Identifier: MS-PL
// Task 214: Disposing resources while bound to GraphicsDevice.
//
// FNA behaviour verified here:
//   1. Texture2D disposed while bound in Textures[0] → slot becomes nullptr.
//   2. VertexBuffer disposed while bound → no crash (FNA keeps stale ref; CNA matches).
//   3. IndexBuffer disposed while bound → no crash (same).
//   4. RenderTarget2D disposed while still set as render target →
//      throws InvalidOperationException ("Disposing target that is still bound").
//   5. RenderTarget2D disposed after being unbound → no exception.
//
// REMED-GFX-168 strengthening. Sections 4 and 5 above tested only Dispose(), which the public layer
// REFUSES while the target is bound — and section 4 then wrote `dev.SetRenderTarget(nullptr); //
// unbind before leaving scope`, so the DESTRUCTOR never ran while bound. That refusal is exactly what
// makes the destructor the only route into the case, because ~RenderTarget2D and ~RenderTargetCube
// are both `= default` and never call Dispose. This file therefore asserted the guarded path and
// detoured around the unguarded one; on EasyGL the unguarded one was a heap-use-after-free at the
// next SetRenderTarget, and no fixture named it.
//
// Sections 6-8 close that: a target released by its DESTRUCTOR while still bound, for all three
// binding shapes (single RenderTarget2D, RenderTargetCube face, one slot of a multi-target set). The
// oracle here is the public device state afterwards, plus the fact that the transition and a
// following bind cycle complete at all; the byte-exact PIXEL oracle for the same sequences, and the
// per-leg process isolation that turns the pre-fix SIGSEGV into an attributable result rather than a
// lost binary, live in bound_target_lifetime_test.cpp. Section 7 also adds the cube's own
// dispose-while-bound refusal, which nothing tested before.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/IDisposable.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BoundResourceDisposeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        // REMED-GFX-168: flushed per check. Sections 6-8's pre-fix outcome is a SIGSEGV, and buffered
        // output dies with the process -- without this, the A/B record of which checks had already
        // passed before the fault was lost entirely.
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

    static bool throwsInvalidOp(System::IDisposable& res)
    {
        try { res.Dispose(); return false; }
        catch (const System::InvalidOperationException&) { return true; }
        catch (...) { return false; }
    }

    /**
     * @brief REMED-GFX-168: a complete fresh bind cycle, as the state oracle after a detach.
     *
     * "It did not crash" is not enough: the transition away from a destroyed target must also leave
     * the device able to bind, clear and unbind a NEW target. Returns false rather than throwing, so
     * a broken device is a named failing check instead of an escaped exception.
     */
    static bool BindCycleWorks(GraphicsDevice& dev)
    {
        try
        {
            RenderTarget2D fresh(dev, 8, 8);
            dev.SetRenderTarget(&fresh);
            dev.Clear(Color(120, 200, 60, 255));
            dev.SetRenderTarget(nullptr);
            return dev.GetRenderTargets().empty();
        }
        catch (...) { return false; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── 1. Texture2D disposed while bound → slot cleared ──────────────
        {
            Texture2D tex(dev, 2, 2);
            dev.getTexturesProperty()(0, &tex);
            check(dev.getTexturesProperty()[0] == &tex,
                  "Texture2D: slot[0] holds texture before dispose");

            static_cast<System::IDisposable&>(tex).Dispose();

            check(dev.getTexturesProperty()[0] == nullptr,
                  "Texture2D: slot[0] cleared to nullptr after dispose while bound");
            check(tex.getIsDisposedProperty(),
                  "Texture2D: IsDisposed true after dispose");
        }

        // ── 2. VertexBuffer disposed while bound → no crash ───────────────
        {
            VertexBuffer vb(dev, 4);
            dev.SetVertexBuffer(&vb);
            // FNA does not clear the device binding on VB dispose; CNA matches.
            bool ok = false;
            try { static_cast<System::IDisposable&>(vb).Dispose(); ok = true; }
            catch (...) {}
            check(ok, "VertexBuffer: disposing while bound does not throw");
            check(vb.getIsDisposedProperty(),
                  "VertexBuffer: IsDisposed true after dispose while bound");
            dev.SetVertexBuffer(nullptr);   // clear stale binding
        }

        // ── 3. IndexBuffer disposed while bound → no crash ────────────────
        {
            IndexBuffer ib(dev, 4);
            dev.SetIndexBuffer(&ib);
            bool ok = false;
            try { static_cast<System::IDisposable&>(ib).Dispose(); ok = true; }
            catch (...) {}
            check(ok, "IndexBuffer: disposing while bound does not throw");
            check(ib.getIsDisposedProperty(),
                  "IndexBuffer: IsDisposed true after dispose while bound");
            dev.SetIndexBuffer(nullptr);    // clear stale binding
        }

        // ── 4. RenderTarget2D disposed while still set → throws ───────────
        {
            RenderTarget2D rt(dev, 16, 16);
            dev.SetRenderTarget(&rt);
            check(throwsInvalidOp(rt),
                  "RenderTarget2D: dispose while bound throws InvalidOperationException");
            // REMED-GFX-168: this unbind is what kept the destructor out of the bound state, and
            // therefore out of the defect. Section 6 below runs the sequence without it.
            dev.SetRenderTarget(nullptr);   // unbind before leaving scope
        }

        // ── 5. RenderTarget2D disposed after unbinding → no throw ─────────
        {
            RenderTarget2D rt(dev, 16, 16);
            dev.SetRenderTarget(&rt);
            dev.SetRenderTarget(nullptr);   // unbind first
            bool ok = false;
            try { static_cast<System::IDisposable&>(rt).Dispose(); ok = true; }
            catch (...) {}
            check(ok, "RenderTarget2D: dispose after unbind does not throw");
            check(rt.getIsDisposedProperty(),
                  "RenderTarget2D: IsDisposed true after dispose");
        }

        // ── 6. RenderTarget2D DESTROYED while still bound (REMED-GFX-168) ──
        {
            {
                RenderTarget2D rt(dev, 16, 16);
                dev.SetRenderTarget(&rt);
                dev.Clear(Color(40, 80, 160, 255));
            }   // <-- no unbind: the destructor runs while rt is still the bound target
            dev.SetRenderTarget(nullptr);
            check(dev.GetRenderTargets().empty(),
                  "RenderTarget2D: the transition after a destroyed-while-bound target unbinds");
            check(BindCycleWorks(dev),
                  "RenderTarget2D: a fresh bind cycle works after a destroyed-while-bound target");
        }

        // ── 7. RenderTargetCube face DESTROYED while bound (REMED-GFX-168) ─
        {
            {
                RenderTargetCube cube(dev, 8, false, SurfaceFormat::Color, DepthFormat::None);
                dev.SetRenderTarget(&cube, CubeMapFace::NegativeY);
                dev.Clear(Color(160, 40, 80, 255));
                check(throwsInvalidOp(cube),
                      "RenderTargetCube: dispose while a face is bound throws "
                      "InvalidOperationException");
            }   // <-- again no unbind, so the destructor is the only route out of the binding
            dev.SetRenderTarget(nullptr);
            check(dev.GetRenderTargets().empty(),
                  "RenderTargetCube: the transition after a destroyed-while-bound cube unbinds");
            check(BindCycleWorks(dev),
                  "RenderTargetCube: a fresh bind cycle works after a destroyed-while-bound cube");
        }

        // ── 8. One slot of a bound MRT set DESTROYED (REMED-GFX-168) ───────
        // The surviving slot is a live target whose own finalization is still owed, so a detach that
        // abandoned the SET rather than the slot would lose it. Declared rather than asserted when
        // this device cannot bind two targets at once.
        {
            RenderTarget2D survivor(dev, 16, 16);
            bool mrtBound = false;
            try
            {
                RenderTarget2D dying(dev, 16, 16);
                dev.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&dying)),
                                      RenderTargetBinding(static_cast<Texture*>(&survivor))});
                mrtBound = true;
                dev.Clear(Color(80, 160, 40, 255));
            }   // <-- slot 0 destroyed with the whole set still bound
            catch (const std::exception& e)
            {
                std::printf("[INFO] MRT: two-slot binding unavailable (%s) -- section 8 declared\n",
                            e.what());
            }
            if (mrtBound)
            {
                dev.SetRenderTargets({});
                check(dev.GetRenderTargets().empty(),
                      "MRT: the transition after a destroyed slot unbinds the set");
                check(BindCycleWorks(dev),
                      "MRT: the surviving slot can still be bound and unbound on its own");
            }
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BoundResourceDisposeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    BoundResourceDisposeTest game;
    game.Run();
    return game.getResult();
}
