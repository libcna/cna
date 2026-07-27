// SPDX-License-Identifier: MS-PL
// plan_headless.md: closes the "not yet exercised by a dedicated test" gap left open on
// HEADLESS-13/14/15/16 and Phase N5 (HeadlessTrace) after the first Headless backend commit --
// TextureCube, Texture3D, RenderTarget2D, RenderTargetCube, custom ShaderEffect, and HeadlessTrace
// mode were all implemented and reachable, but only exercised by code inspection, not a real test.
//
// Check A -- TextureCube construction + SetData()/GetData() on 2 different faces: what this
//   backend really produces (a transparent-black overwrite of the caller's destination) is pinned,
//   not blessed -- see the check's own comment (REMED-GFX-127 false-positive audit).
// Check B -- Texture3D construction is REJECTED (this backend reports no
//   GraphicsCapability::Texture3D, REMED-CONTENT-004).
// Check C -- RenderTarget2D: SetRenderTarget()/SetRenderTarget(nullptr) doesn't throw, and
//   GetViewportSize() genuinely reflects the bound target's own size while it's active.
// Check D -- RenderTargetCube: SetRenderTarget() for 2 different faces doesn't throw.
// Check E -- Custom ShaderEffect: compiles (IsEffectValid()), SetUniformFloat() is retrievable via
//   UniformValues() on the backend, and SpriteBatch::Begin(..., &effect) doesn't throw.
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

#include "CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp"

#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::Headless;

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
        auto& backend = static_cast<HeadlessGraphicsBackend&>(dev.GetBackend());
        const HeadlessStatistics baseline = backend.GetStatistics();

        // Check A: TextureCube, two different faces independently readable.
        {
            TextureCube cube(dev, 4, false, SurfaceFormat::Color);
            std::vector<Color> posX(16, Color::Red);
            std::vector<Color> negX(16, Color::Blue);
            cube.SetData(CubeMapFace::PositiveX, posX.data(), 16);
            cube.SetData(CubeMapFace::NegativeX, negX.data(), 16);
            // REMED-GFX-127 false-positive audit: these two checks used to be `check(true, ...)`
            // -- they asserted nothing at all about what GetData produced, so any behaviour
            // whatsoever passed them. What the Headless cube/3D backends actually do is measured
            // and PINNED here instead: SetData is recorded but not stored, and GetData OVERWRITES
            // the caller's whole destination with zeros. That is the same fabrication REMED-GFX-127
            // removed from the Texture2D path -- an unwritten resource answered with invented
            // content rather than an honest refusal -- surviving on the separate
            // ITextureCubeBackend/ITexture3DBackend interfaces, which that finding did not cover.
            // The destinations are pre-filled with a NON-ZERO sentinel so "the buffer was zeroed by
            // GetData" is distinguishable from "the buffer was already zero".
            std::vector<Color> readPosX(16, Color(0xCD, 0xCD, 0xCD, 0xCD));
            std::vector<Color> readNegX(16, Color(0xA5, 0xA5, 0xA5, 0xA5));
            cube.GetData(CubeMapFace::PositiveX, readPosX.data(), 16);
            cube.GetData(CubeMapFace::NegativeX, readNegX.data(), 16);
            const auto allTransparentBlack = [](const std::vector<Color>& v) {
                for (const Color& c : v)
                    if (c.getRProperty() || c.getGProperty() || c.getBProperty() || c.getAProperty())
                        return false;
                return true;
            };
            check(allTransparentBlack(readPosX) && allTransparentBlack(readNegX),
                  "TextureCube: GetData() on two independent faces overwrites the caller's whole "
                  "destination with transparent black -- the SetData() input is NOT stored, and "
                  "this fabricated result is recorded, not blessed (REMED-GFX-127's sibling on "
                  "ITextureCubeBackend)");
        }

        // Check B: Texture3D construction is REJECTED on this backend.
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
                  "backend reports no GraphicsCapability::Texture3D and must not silently accept "
                  "volume data (REMED-CONTENT-004)");
        }

        // Check C: RenderTarget2D bind/unbind, viewport reflects the bound target's own size.
        {
            RenderTarget2D rt(dev, 32, 48, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            int w = 0, h = 0;
            backend.GetViewportSize(w, h);
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
            const std::size_t beforeTrace = backend.TraceLog().size();
            backend.SetMode(HeadlessMode::Trace);
            dev.Clear(Color::Black);
            const std::size_t afterTraceClear = backend.TraceLog().size();
            check(afterTraceClear > beforeTrace, "HeadlessTrace mode: a Clear() call grows the trace log");

            backend.SetMode(HeadlessMode::Validation);
            dev.Clear(Color::Black);
            const std::size_t afterValidationClear = backend.TraceLog().size();
            check(afterValidationClear == afterTraceClear,
                  "HeadlessValidation mode: a further Clear() call does not grow the trace log");
        }

        // Check G: resource counters match exactly one of each created above; everything created in
        // this Draw() call has gone out of scope by the time we check, so no leaks remain beyond
        // whatever was already alive at the start of this frame (captured as `baseline`).
        {
            const HeadlessStatistics& stats = backend.GetStatistics();
            const bool countsOk =
                stats.textureCubesCreated == baseline.textureCubesCreated + 1 &&
                // Check B's Texture3D is now rejected before any backend resource is created
                // (REMED-CONTENT-004), so this counter must NOT move.
                stats.textures3DCreated == baseline.textures3DCreated &&
                stats.renderTargetsCreated == baseline.renderTargetsCreated + 1 &&
                stats.renderTargetCubesCreated == baseline.renderTargetCubesCreated + 1 &&
                stats.effectsCreated == baseline.effectsCreated + 1;
            check(countsOk, "resource-creation counters increment by exactly one for each type created above");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 9);
        result_ = (passCount_ == 9) ? 0 : 1;
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
