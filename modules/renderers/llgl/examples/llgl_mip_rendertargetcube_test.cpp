// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-35: RenderTargetCube mip-mapping -- real, correctly downsampled mip content in
// a RenderTargetCube's own shared colour texture, not just a declared LevelCount, mirroring
// examples/llgl_rendertarget2d_mip_test.cpp's own LLGL-26 follow-up technique against one cube
// face (CubeMapFace::PositiveX) instead of a plain RenderTarget2D.
//
// Method: render a 7:1 asymmetric split (top 56/64 rows red, bottom 8/64 rows blue -- a 1x8
// point-stretched source pattern, so every mip level down to 8x8 is an EXACT, unblended 7:1 split
// with no seam aliasing) into one face of a mipMap=true 64x64 RenderTargetCube via SpriteBatch,
// unbind (triggers RecordAndSubmitFrame()'s GenerateMips() call), then read levels back directly
// via RenderTargetCube::GetData(face, level, ...):
//
// Check A -- construction succeeds with the right LevelCount (7 for 64x64: 64,32,16,8,4,2,1).
// Check B -- level 0, well inside each region: crisp, unblended red/blue (sanity: the base draw
//   itself worked, independent of mip generation).
// Check C -- level 3 (8x8 -- an EXACT 1:1 downsample of the original 1x8 source pattern, so still
//   a crisp, unblended 7:1 split with no averaging at all) reads crisp red at row 0 and crisp blue
//   at row 7 -- proves the downsample cascade computed the RIGHT content at an intermediate level,
//   not just the right dimensions.
// Check D -- the coarsest level (6, 1x1) reads back the whole image's true 7:1 weighted average
//   (~223,0,32) -- neither pure red nor pure blue nor transparent-black garbage -- proof of a REAL
//   downsample all the way to the top of the chain, not a chain that stops partway.
// Check E -- GetData() beyond LevelCount (level 7 on a 7-level cube) returns false.
// Check F -- a NON-mipmapped RenderTargetCube still only has level 0 (LevelCount == 1) and
//   GetData(face, 1, ...) on it also returns false -- mip-mapping stays opt-in.
//
// No _OpenGL CTest variant: this project's own OpenGL module has no cube-texture support at all,
// the same reason every other RenderTargetCube test on this renderer is Vulkan-module only.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr CubeMapFace kFace = CubeMapFace::PositiveX;

    // 1x8: rows 0..6 red, row 7 blue -- point-stretched to fill kSize (an integer multiple of 8)
    // gives an exact 56-red/8-blue split with no seam aliasing at level 0, and remains an EXACT,
    // unblended 7:1 split all the way down to level 3 (8x8), since 64/8 = 8 exactly.
    std::vector<std::uint8_t> MakeSplitPatternPixels()
    {
        std::vector<std::uint8_t> pixels;
        pixels.reserve(8 * 4);
        for (int i = 0; i < 7; ++i)
            pixels.insert(pixels.end(), {255, 0, 0, 255});
        pixels.insert(pixels.end(), {0, 0, 255, 255});
        return pixels;
    }
}

class LlglMipRenderTargetCubeTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<Texture2D> patternTexture;
        try
        {
            patternTexture = std::make_unique<Texture2D>(
                Texture2D::CreateFromPixels(device, 1, 8, MakeSplitPatternPixels()));
        }
        catch (const std::exception&)
        {
            patternTexture.reset();
        }
        if (!ExpectTrue("Texture2D::CreateFromPixels() succeeds for the 1x8 split pattern",
                        patternTexture != nullptr))
        {
            return;
        }

        std::unique_ptr<RenderTargetCube> cube;
        try
        {
            cube = std::make_unique<RenderTargetCube>(
                device, kSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None);
        }
        catch (const std::exception&)
        {
            cube.reset();
        }
        // 64 -> 32 -> 16 -> 8 -> 4 -> 2 -> 1: 7 levels.
        if (!ExpectTrue("mipMap=true RenderTargetCube(64) construction succeeds with LevelCount=7",
                        cube != nullptr && cube->getLevelCountProperty() == 7))
        {
            return;
        }

        SpriteBatch spriteBatch(device);
        SamplerState pointClamp = SamplerState::PointClamp;

        device.SetRenderTarget(cube.get(), kFace);
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*patternTexture, Rectangle(0, 0, kSize, kSize), Color::White);
        spriteBatch.End();
        // Unbinding is what triggers RecordAndSubmitFrame()'s GenerateMips() call, once this
        // render pass has finished.
        device.SetRenderTarget(nullptr);

        // --- Check B: level 0, well inside each region -- must stay crisp, unblended. ---
        Color level0Top(0, 0, 0, 0), level0Bottom(0, 0, 0, 0);
        const Rectangle level0TopRegion(kSize / 2, 16, 1, 1);
        const Rectangle level0BottomRegion(kSize / 2, 60, 1, 1);
        cube->GetData(kFace, 0, &level0TopRegion, &level0Top, 0, 1);
        cube->GetData(kFace, 0, &level0BottomRegion, &level0Bottom, 0, 1);
        ExpectTrue("level 0 stays crisp red/blue (no premature blending at the base level)",
                  level0Top.getRProperty() >= 200 && level0Top.getBProperty() <= 30 &&
                  level0Bottom.getBProperty() >= 200 && level0Bottom.getRProperty() <= 30);

        // --- Check C: level 3 (8x8) is an EXACT 1:1 downsample of the original 1x8 pattern --
        // row 0 must be crisp red and row 7 must be crisp blue, no blending at all. ---
        Color level3Top(0, 0, 0, 0), level3Bottom(0, 0, 0, 0);
        bool level3Threw = false;
        try
        {
            const Rectangle level3TopRegion(0, 0, 1, 1);
            const Rectangle level3BottomRegion(0, 7, 1, 1);
            cube->GetData(kFace, 3, &level3TopRegion, &level3Top, 0, 1);
            cube->GetData(kFace, 3, &level3BottomRegion, &level3Bottom, 0, 1);
        }
        catch (const std::exception&)
        {
            level3Threw = true;
        }
        ExpectTrue("level 3 (8x8, an exact 1:1 downsample of the source pattern) reads back crisp, "
                  "unblended red at row 0 and blue at row 7 -- the downsample cascade computed the "
                  "RIGHT content at an intermediate level, not just the right dimensions",
                  !level3Threw &&
                  level3Top.getRProperty() >= 200 && level3Top.getBProperty() <= 30 &&
                  level3Bottom.getBProperty() >= 200 && level3Bottom.getRProperty() <= 30);

        // --- Check D: the coarsest level (6, 1x1) is the whole image's true 7:1 weighted average
        // (~223,0,32) -- neither pure red, pure blue, nor transparent-black garbage. ---
        Color coarsest(0, 0, 0, 0);
        bool coarsestThrew = false;
        try
        {
            const Rectangle coarsestRegion(0, 0, 1, 1);
            cube->GetData(kFace, 6, &coarsestRegion, &coarsest, 0, 1);
        }
        catch (const std::exception&)
        {
            coarsestThrew = true;
        }
        ExpectTrue("the coarsest level (1x1) reads back the whole image's true 7:1 weighted average "
                  "(~223,0,32), not pure red/blue or garbage -- a real downsample all the way to "
                  "the top of the chain",
                  !coarsestThrew &&
                  coarsest.getRProperty() >= 190 && coarsest.getRProperty() <= 240 &&
                  coarsest.getBProperty() >= 15  && coarsest.getBProperty() <= 55  &&
                  coarsest.getGProperty() <= 20  && coarsest.getAProperty() >= 235);

        // --- Check E: a level beyond LevelCount (7 on a 7-level cube) is rejected, not a
        // garbage read. ---
        Color beyond(0, 0, 0, 0);
        bool beyondThrew = false;
        try
        {
            const Rectangle beyondRegion(0, 0, 1, 1);
            cube->GetData(kFace, 7, &beyondRegion, &beyond, 0, 1);
        }
        catch (const std::exception&)
        {
            beyondThrew = true;
        }
        ExpectTrue("GetData() beyond LevelCount (level 7 on a 7-level cube) throws, not a garbage "
                  "read", beyondThrew);

        // --- Check F: mip-mapping stays opt-in -- a plain (mipMap=false, the default)
        // RenderTargetCube still only has one level. ---
        std::unique_ptr<RenderTargetCube> plainCube;
        try
        {
            plainCube = std::make_unique<RenderTargetCube>(
                device, kSize, /*mipMap=*/false, SurfaceFormat::Color, DepthFormat::None);
        }
        catch (const std::exception&)
        {
            plainCube.reset();
        }
        if (ExpectTrue("mipMap=false RenderTargetCube still has LevelCount=1",
                       plainCube != nullptr && plainCube->getLevelCountProperty() == 1))
        {
            Color plainLevel1(0, 0, 0, 0);
            bool plainLevel1Threw = false;
            try
            {
                const Rectangle plainLevel1Region(0, 0, 1, 1);
                plainCube->GetData(kFace, 1, &plainLevel1Region, &plainLevel1, 0, 1);
            }
            catch (const std::exception&)
            {
                plainLevel1Threw = true;
            }
            ExpectTrue("GetData(face, 1, ...) on a non-mipmapped RenderTargetCube throws",
                      plainLevel1Threw);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglMipRenderTargetCubeTest>();
}
