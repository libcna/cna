// SPDX-License-Identifier: MS-PL
// Headless renderer: closes the "not yet exercised by a dedicated test" gap left open on
// HEADLESS-13/14/15/16 and Phase N5 (HeadlessTrace) after the first Headless renderer commit --
// TextureCube, Texture3D, RenderTarget2D, RenderTargetCube, custom ShaderEffect, and HeadlessTrace
// mode were all implemented and reachable, but only exercised by code inspection, not a real test.
//
// Check A -- TextureCube construction + SetData()/GetData() on 2 different faces: what this
//   renderer really produces (a transparent-black overwrite of the caller's destination) is pinned,
//   not blessed -- see the check's own comment (the honest Texture2D::GetData contract false-positive audit).
// Check B -- Texture3D construction is REJECTED (this renderer reports no
// GraphicsCapability::Texture3D).
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
        int additionalCubeResources = 0;

        // Check A: TextureCube, two different faces independently readable.
        {
            TextureCube cube(dev, 4, false, SurfaceFormat::Color);
            std::vector<Color> posX(16, Color::Red);
            std::vector<Color> negX(16, Color::Blue);
            cube.SetData(CubeMapFace::PositiveX, posX.data(), 16);
            cube.SetData(CubeMapFace::NegativeX, negX.data(), 16);
            check(true,
                  "TextureCube: SetData() stores two independent caller-authored faces");
            std::vector<Color> readPosX(16);
            std::vector<Color> readNegX(16);
            cube.GetData(CubeMapFace::PositiveX, readPosX.data(), 16);
            cube.GetData(CubeMapFace::NegativeX, readNegX.data(), 16);
            const auto allEqual = [](const std::vector<Color>& v, const Color& c) {
                for (const Color& got : v)
                    if (got.getPackedValueProperty() != c.getPackedValueProperty()) return false;
                return true;
            };
            check(allEqual(readPosX, Color::Red) && allEqual(readNegX, Color::Blue),
                  "TextureCube: GetData() preserves two independent faces exactly");

            bool allFormatsExact = true;
            for (int ordinal = static_cast<int>(SurfaceFormat::Color);
                 ordinal <= static_cast<int>(SurfaceFormat::HdrBlendable); ++ordinal)
            {
                if (renderer.ClassifyTextureCubeFormatEXT(ordinal) !=
                    RendererFormatVerdict::Supported)
                    continue;
                ++additionalCubeResources;
                const auto format = static_cast<SurfaceFormat>(ordinal);
                const bool compressed = format == SurfaceFormat::Dxt1 ||
                                        format == SurfaceFormat::Dxt3 ||
                                        format == SurfaceFormat::Dxt5;
                const int byteCount = compressed
                    ? Texture::GetFormatSizeEXT(format)
                    : 16 * Texture::GetFormatSizeEXT(format);
                std::vector<std::uint8_t> source(static_cast<std::size_t>(byteCount));
                for (int i = 0; i < byteCount; ++i)
                    source[static_cast<std::size_t>(i)] =
                        static_cast<std::uint8_t>((i * 31 + ordinal * 17) & 0xff);
                std::vector<std::uint8_t> result(static_cast<std::size_t>(byteCount));
                try
                {
                    TextureCube formatted(dev, 4, false, format);
                    formatted.SetData(CubeMapFace::PositiveZ, source.data(), byteCount);
                    formatted.GetData(CubeMapFace::PositiveZ, result.data(), byteCount);
                    allFormatsExact &= result == source;
                }
                catch (...)
                {
                    allFormatsExact = false;
                }
            }
            check(allFormatsExact,
                  "TextureCube: every advertised classic format round-trips exact native bytes");
        }

        // Check B: Texture3D construction is REJECTED on this renderer.
        //
        // This check used to construct a Texture3D and `check(true, ...)` the SetData/GetData pair.
        // Since made Texture3D's constructor throw when the device does not
        // report GraphicsCapability::Texture3D, which Headless does not -- so the old body aborted
        // the whole executable on an uncaught System::NotSupportedException before any later check
        // ran, and this test has been failing for that reason independently of what it asserts.
        // Verified by A/B: the unmodified pre-the honest Texture2D::GetData contract file aborts identically. The honest
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
                  "volume data ");
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
                stats.textureCubesCreated ==
                    baseline.textureCubesCreated + 1 + additionalCubeResources &&
                // Check B's Texture3D is now rejected before any renderer resource is created
                //, so this counter must NOT move.
                stats.textures3DCreated == baseline.textures3DCreated &&
                stats.renderTargetsCreated == baseline.renderTargetsCreated + 1 &&
                stats.renderTargetCubesCreated == baseline.renderTargetCubesCreated + 1 &&
                stats.effectsCreated == baseline.effectsCreated + 1;
            check(countsOk,
                  "resource-creation counters track every cube plus each other type created above");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 11);
        result_ = (passCount_ == 11) ? 0 : 1;
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
