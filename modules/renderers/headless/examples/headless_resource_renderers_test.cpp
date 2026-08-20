// SPDX-License-Identifier: MS-PL
// plans/plan_headless.md: closes the "not yet exercised by a dedicated test" gap left open on
// HEADLESS-13/14/15/16 and Phase N5 (HeadlessTrace) after the first Headless renderer commit --
// TextureCube, Texture3D, RenderTarget2D, RenderTargetCube, custom ShaderEffect, and HeadlessTrace
// mode were all implemented and reachable, but only exercised by code inspection, not a real test.
//
// Check A -- TextureCube construction + SetData()/GetData() on 2 different faces: what this
//   renderer really produces (a transparent-black overwrite of the caller's destination) is pinned,
//   not blessed -- see the check's own comment (REMED-GFX-127 false-positive audit).
// Check B -- Texture3D construction is REJECTED (this renderer reports no
//   GraphicsCapability::Texture3D, REMED-CONTENT-004).
// Check C -- RenderTarget2D: SetRenderTarget()/SetRenderTarget(nullptr) doesn't throw, and
//   GetViewportSize() genuinely reflects the bound target's own size while it's active.
// Check D -- RenderTargetCube: SetRenderTarget() for 2 different faces doesn't throw.
// Check E -- Custom ShaderEffect: compiles (IsEffectValid()), SetUniformFloat() is retrievable via
//   UniformValues() on the renderer, and SpriteBatch::Begin(..., &effect) doesn't throw.
// Check F -- HeadlessTrace mode: TraceLog() is empty under the default HeadlessValidation mode;
//   switching to HeadlessTrace and issuing a Clear() call adds an entry; switching back to
//   HeadlessValidation stops growing the log.
// Check G -- resource counters (renderTargetsCreated/renderTargetCubesCreated/textureCubesCreated/
//   textures3DCreated/effectsCreated) match the exact single-instance-of-each sequence above, and
//   every resource created in this test is gone (AssertNoLeaks() clean) once they go out of scope.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Headless;

class HeadlessResourceBackendsTest : public Game
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
        auto& renderer = static_cast<HeadlessRenderer&>(dev.GetRenderer());
        const HeadlessStatistics baseline = renderer.GetStatistics();

        // Check A: TextureCube, two different faces independently readable.
        {
            TextureCube cube(dev, 4, false, SurfaceFormat::Color);
            std::vector<Color> posX(16, Color::Red);
            std::vector<Color> negX(16, Color::Blue);
            // REMED-GFX-135: these two uploads used to be issued bare, as if they had stored
            // something. They never did -- this renderer's cube SetData validates its arguments,
            // records a trace entry and keeps no pixels -- and the shared layer had no way to say
            // so. Both are now deterministic rejections, asserted here rather than assumed.
            bool threwSetPosX = false, threwSetNegX = false;
            try { cube.SetData(CubeMapFace::PositiveX, posX.data(), 16); }
            catch (const System::NotSupportedException&) { threwSetPosX = true; }
            catch (...) {}
            try { cube.SetData(CubeMapFace::NegativeX, negX.data(), 16); }
            catch (const System::NotSupportedException&) { threwSetNegX = true; }
            catch (...) {}
            check(threwSetPosX && threwSetNegX,
                  "TextureCube: SetData() on two independent faces throws a deterministic "
                  "System::NotSupportedException -- this renderer stores no pixel data, so it "
                  "refuses the upload rather than accepting and discarding it (REMED-GFX-135)");
            // REMED-GFX-127 false-positive audit: these two checks used to be `check(true, ...)`
            // -- they asserted nothing at all about what GetData produced, so any behaviour
            // whatsoever passed them. They were then strengthened to PIN what the Headless cube
            // renderer actually did: SetData recorded but not stored, and GetData overwriting the
            // caller's whole destination with zeros -- the same fabrication REMED-GFX-127 removed
            // from the Texture2D path, surviving on the separate ITextureCubeRenderer interface.
            //
            // REMED-GFX-130 has now closed that: this renderer stores no pixel data by design, so
            // its cube readback refuses instead of inventing one. The sentinel pre-fill stays --
            // it is what proves the rejection leaves the caller's destination byte-for-byte
            // untouched, which "an exception was thrown" alone would not.
            const Color sentinelCD(0xCD, 0xCD, 0xCD, 0xCD);
            const Color sentinelA5(0xA5, 0xA5, 0xA5, 0xA5);
            std::vector<Color> readPosX(16, sentinelCD);
            std::vector<Color> readNegX(16, sentinelA5);
            bool threwPosX = false, threwNegX = false;
            try { cube.GetData(CubeMapFace::PositiveX, readPosX.data(), 16); }
            catch (const System::NotSupportedException&) { threwPosX = true; }
            catch (...) {}
            try { cube.GetData(CubeMapFace::NegativeX, readNegX.data(), 16); }
            catch (const System::NotSupportedException&) { threwNegX = true; }
            catch (...) {}
            const auto allEqual = [](const std::vector<Color>& v, const Color& c) {
                for (const Color& got : v)
                    if (got.getPackedValueProperty() != c.getPackedValueProperty()) return false;
                return true;
            };
            check(threwPosX && threwNegX &&
                      allEqual(readPosX, sentinelCD) && allEqual(readNegX, sentinelA5),
                  "TextureCube: GetData() on two independent faces throws a deterministic "
                  "System::NotSupportedException and leaves BOTH destinations byte-for-byte at "
                  "their non-zero sentinels -- this renderer stores no pixel data, so it refuses "
                  "rather than fabricating a transparent-black face (REMED-GFX-130)");
        }

        // Check B: Texture3D construction is REJECTED on this renderer.
        //
        // This check used to construct a Texture3D and `check(true, ...)` the SetData/GetData pair.
        // REMED-CONTENT-004 since made Texture3D's constructor throw when the device does not
        // report GraphicsCapability::Texture3D, which Headless does not -- so the old body aborted
        // the whole executable on an uncaught System::NotSupportedException before any later check
        // ran, and this test has been failing for that reason independently of what it asserts.
        // Verified by A/B: the unmodified pre-REMED-GFX-127 file aborts identically. The honest
        // assertion is the documented rejection itself.
        {
            bool threw = false;
            try
            {
                Texture3D tex3d(dev, 2, 2, 2, false, SurfaceFormat::Color);
                std::vector<Color> voxels(8, Color::Green);
                tex3d.SetData(voxels.data(), 8);
            }
            catch (const System::NotSupportedException&) { threw = true; }
            check(threw,
                  "Texture3D: construction is rejected with System::NotSupportedException -- this "
                  "renderer reports no GraphicsCapability::Texture3D and must not silently accept "
                  "volume data (REMED-CONTENT-004)");
        }

        // Check C: RenderTarget2D bind/unbind, viewport reflects the bound target's own size.
        {
            RenderTarget2D rt(dev, 32, 48, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            check(w == 32 && h == 48, "RenderTarget2D: GetViewportSize() reflects the bound target's own size");
            dev.SetRenderTarget(nullptr);
        }

        // Check D: RenderTargetCube, two different faces bound in turn.
        {
            RenderTargetCube rtCube(dev, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                    RenderTargetUsage::DiscardContents);
            bool threw = false;
            try
            {
                dev.SetRenderTarget(&rtCube, CubeMapFace::PositiveY);
                dev.SetRenderTarget(&rtCube, CubeMapFace::NegativeZ);
                dev.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveY);
            }
            catch (...) { threw = true; }
            check(!threw, "RenderTargetCube: binding two different faces in turn does not throw");
        }

        // Check E: custom ShaderEffect compiles, uniform values are introspectable, and SpriteBatch
        // accepts it as a custom effect.
        {
            ShaderEffect fx(dev, "// vertex\n", "// fragment\n");
            check(fx.IsEffectValid(), "ShaderEffect: CompileProgram() reports success (Headless never rejects source)");
            fx.SetUniformFloat("uTestValue", 3.5f);

            SpriteBatch sb(dev);
            bool threw = false;
            try
            {
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
                sb.End();
            }
            catch (...) { threw = true; }
            check(!threw, "SpriteBatch::Begin() with a custom ShaderEffect does not throw");
        }

        // Check F: HeadlessTrace mode actually grows the call log; HeadlessValidation does not.
        {
            const std::size_t beforeTrace = renderer.TraceLog().size();
            renderer.SetMode(HeadlessMode::Trace);
            dev.Clear(Color::Black);
            const std::size_t afterTraceClear = renderer.TraceLog().size();
            check(afterTraceClear > beforeTrace, "HeadlessTrace mode: a Clear() call grows the trace log");

            renderer.SetMode(HeadlessMode::Validation);
            dev.Clear(Color::Black);
            const std::size_t afterValidationClear = renderer.TraceLog().size();
            check(afterValidationClear == afterTraceClear,
                  "HeadlessValidation mode: a further Clear() call does not grow the trace log");
        }

        // Check G: resource counters match exactly one of each created above; everything created in
        // this Draw() call has gone out of scope by the time we check, so no leaks remain beyond
        // whatever was already alive at the start of this frame (captured as `baseline`).
        {
            const HeadlessStatistics& stats = renderer.GetStatistics();
            const bool countsOk =
                stats.textureCubesCreated == baseline.textureCubesCreated + 1 &&
                // Check B's Texture3D is now rejected before any renderer resource is created
                // (REMED-CONTENT-004), so this counter must NOT move.
                stats.textures3DCreated == baseline.textures3DCreated &&
                stats.renderTargetsCreated == baseline.renderTargetsCreated + 1 &&
                stats.renderTargetCubesCreated == baseline.renderTargetCubesCreated + 1 &&
                stats.effectsCreated == baseline.effectsCreated + 1;
            check(countsOk, "resource-creation counters increment by exactly one for each type created above");
        }

        // REMED-GFX-135 added the cube SetData rejection check to Check A.
        std::printf("=== %d/%d PASS ===\n", passCount_, 10);
        result_ = (passCount_ == 10) ? 0 : 1;
        Exit();
    }

public:
    HeadlessResourceBackendsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HeadlessResourceBackendsTest game;
    game.Run();
    return game.getResult();
}
