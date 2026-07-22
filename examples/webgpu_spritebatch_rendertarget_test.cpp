// SPDX-License-Identifier: MS-PL
// REMED-GFX-019: WebGPU SpriteBatch clip-space mapping must be derived from the CURRENTLY-BOUND
// render target's own pixel dimensions, NOT the backbuffer's.
//
// Pre-fix defect (independently confirmed against production source and self-disclosed by
// webgpu_rendertargetcube_test.cpp's own Check-C comment): WebGPUGraphicsBackend::QueueSprite()
// converts a SpriteBatch destination rectangle to clip space via ComputeLogicalViewport() +
// physicalWidth_/physicalHeight_, all of which are backbuffer-scoped. When a RenderTarget2D (or
// cube face) of a different size than the backbuffer is bound, every sprite's destination
// rectangle is mis-mapped -- a SpriteBatch.Draw(dest) into an off-screen target lands at the wrong
// position/scale. This is the render-to-texture UI/minimap/post-process path, a common technique.
//
// This test uses a DELIBERATELY ASYMMETRIC backbuffer (96x72), so a mapping that divides by the
// backbuffer width (96) instead of the render target width (48) places the sprite at roughly half
// scale, in a distinct region -- see the per-probe predicted-vs-observed reasoning below. Only the
// binary/full-intensity colours Red/Black/Blue are used for spatial assertions, so a possible sRGB
// swapchain/target encoding cannot masquerade as a coordinate error (0/255 channel values are
// gamma-invariant at both encoding endpoints).
//
// Check A -- asymmetric RenderTarget2D (48x32), default Viewport: a Red sprite drawn into
//   Rectangle(7,5,17,11) must occupy exactly those render-target-relative pixels. Six probes
//   distinguish correct RT-relative placement from the pre-fix backbuffer-relative half-scale
//   mis-map (interior must be Red where the pre-fix sprite does NOT reach; a "bug-witness" pixel
//   inside the pre-fix half-scale rectangle but OUTSIDE the correct rectangle must be Black).
// Check B -- backbuffer control: the same kind of scene rendered directly to the 96x72 backbuffer
//   must be correct both before and after the fix (the backbuffer path is unchanged by GFX-019).
// Check C -- two DIFFERENT-sized render targets (48x32 and 64x40), same pixel-space destination
//   rectangle: the sprite must occupy the SAME render-target pixels in each, proving the target
//   size is read per-draw and not cached from the first target.
// Check D -- RT A -> backbuffer -> RT B transition isolation with DISTINCT destination rectangles:
//   each target receives only its own sprite, mapped against its own dimensions; no surface/RT
//   dimension state leaks across the target switches.
// Check E -- non-identity transformMatrix (scale 1.5 + translation) into a render target: the
//   transform is applied in render-target pixel space and only THEN converted to clip space using
//   the render target's dimensions -- it must neither stop working nor be double-scaled by the
//   backbuffer size.
// Check F -- texture orientation control (PointClamp, a vertically split red-top/blue-bottom
//   texture): the fix changes only the clip-space DIMENSION source, never UV orientation, so the
//   render target's top stays Red and its bottom stays Blue (no accidental vertical flip).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackbufferW = 96;
    constexpr int kBackbufferH = 72;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 20)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string RgbStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    Color ReadRT(RenderTarget2D& rt, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }

    Color ReadBackbuffer(GraphicsDevice& dev, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        dev.GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    Color ReadCubeFace(RenderTargetCube& rtc, CubeMapFace face, int size, int x, int y)
    {
        std::vector<Color> pix(static_cast<std::size_t>(size) * size, Color(0, 0, 0, 0));
        rtc.GetData(face, pix.data(), static_cast<int>(pix.size()));
        return pix[static_cast<std::size_t>(y) * size + x];
    }
}

class WebGpuSpriteBatchRenderTargetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;      // 1x1 solid white -- solid-fill sprites via a Colour tint.
    Texture2D splitTex_;      // 1x8: rows 0-3 Red (top), rows 4-7 Blue (bottom) -- orientation.
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ++totalCount_;
        if (ok) ++passCount_;
    }

    // Draw a solid Red sprite (white texture * Red tint) into destination, into whatever target is
    // currently bound. Uses a fresh Begin/End so each call is self-contained.
    void DrawRedSprite(GraphicsDevice& dev, const Rectangle& destination)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), Color::Red);
        sb.End();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
        std::vector<std::uint8_t> split;
        split.reserve(8 * 4);
        for (int row = 0; row < 8; ++row)
        {
            const bool top = row < 4;
            split.push_back(top ? 255 : 0);   // R
            split.push_back(0);                // G
            split.push_back(top ? 0 : 255);   // B
            split.push_back(255);              // A
        }
        splitTex_ = Texture2D::CreateFromPixels(dev, 1, 8, split);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ---------------------------------------------------------------------------------------
        // Check A -- asymmetric RenderTarget2D (48x32), default Viewport. Correct placement puts
        // the Red sprite at RT pixels x in [7,24), y in [5,16). The pre-fix backbuffer-relative
        // map (divide by 96x72, then rasterise into the full 48x32 target) instead compresses it
        // to roughly x in [3.5,12), y in [2.2,7.1) -- half scale, shifted toward the origin.
        // ---------------------------------------------------------------------------------------
        {
            RenderTarget2D rt(dev, 48, 32, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color interior   = ReadRT(rt, 15, 10);  // inside correct, OUTSIDE pre-fix rect
            const Color leftOut    = ReadRT(rt, 3, 10);    // left of the sprite
            const Color rightOut   = ReadRT(rt, 31, 10);   // right of the sprite
            const Color aboveOut   = ReadRT(rt, 15, 2);    // above the sprite
            const Color belowOut   = ReadRT(rt, 15, 20);   // below the sprite
            const Color bugWitness = ReadRT(rt, 5, 3);     // inside pre-fix half-scale rect only

            check(Matches(interior, Color::Red),
                  "Check A1: RT(48x32) sprite interior (15,10) is Red -- RT-relative placement, "
                  "not backbuffer-relative half-scale: got=" + RgbStr(interior));
            check(Matches(leftOut, Color::Black),
                  "Check A2: RT sprite left-outside (3,10) is Black: got=" + RgbStr(leftOut));
            check(Matches(rightOut, Color::Black),
                  "Check A3: RT sprite right-outside (31,10) is Black: got=" + RgbStr(rightOut));
            check(Matches(aboveOut, Color::Black),
                  "Check A4: RT sprite above-outside (15,2) is Black: got=" + RgbStr(aboveOut));
            check(Matches(belowOut, Color::Black),
                  "Check A5: RT sprite below-outside (15,20) is Black: got=" + RgbStr(belowOut));
            check(Matches(bugWitness, Color::Black),
                  "Check A6: bug-witness (5,3) -- inside the pre-fix backbuffer-relative rectangle "
                  "but outside the correct one -- is Black: got=" + RgbStr(bugWitness));
        }

        // ---------------------------------------------------------------------------------------
        // Check B -- backbuffer control (unchanged by GFX-019). Rectangle(14,10,34,22) -> the
        // sprite fills backbuffer pixels x in [14,48), y in [10,32).
        // ---------------------------------------------------------------------------------------
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(14, 10, 34, 22));

            const Color interior = ReadBackbuffer(dev, 31, 21);
            const Color rightOut = ReadBackbuffer(dev, 60, 21);
            const Color aboveOut = ReadBackbuffer(dev, 31, 4);
            check(Matches(interior, Color::Red),
                  "Check B1: backbuffer sprite interior (31,21) is Red: got=" + RgbStr(interior));
            check(Matches(rightOut, Color::Black),
                  "Check B2: backbuffer sprite right-outside (60,21) is Black: got=" + RgbStr(rightOut));
            check(Matches(aboveOut, Color::Black),
                  "Check B3: backbuffer sprite above-outside (31,4) is Black: got=" + RgbStr(aboveOut));
        }

        // ---------------------------------------------------------------------------------------
        // Check C -- two DIFFERENT-sized render targets, SAME pixel-space destination rectangle.
        // Both must place the sprite at the same RT pixels [7,24)x[5,16). A backend that cached
        // the first target's 48x32 dimensions would mis-scale the sprite in the 64x40 target.
        // ---------------------------------------------------------------------------------------
        {
            RenderTarget2D rtA(dev, 48, 32, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RenderTarget2D rtB(dev, 64, 40, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

            dev.SetRenderTarget(&rtA);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            dev.SetRenderTarget(&rtB);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color aInterior = ReadRT(rtA, 15, 10);
            const Color aOutside  = ReadRT(rtA, 40, 25);
            const Color bInterior = ReadRT(rtB, 15, 10);
            const Color bOutside  = ReadRT(rtB, 40, 25);
            check(Matches(aInterior, Color::Red),
                  "Check C1: RT A(48x32) interior (15,10) is Red: got=" + RgbStr(aInterior));
            check(Matches(aOutside, Color::Black),
                  "Check C2: RT A outside (40,25) is Black: got=" + RgbStr(aOutside));
            check(Matches(bInterior, Color::Red),
                  "Check C3: RT B(64x40) interior (15,10) is Red -- same pixel rect, larger target, "
                  "not the cached first-target size: got=" + RgbStr(bInterior));
            check(Matches(bOutside, Color::Black),
                  "Check C4: RT B outside (40,25) is Black: got=" + RgbStr(bOutside));
        }

        // ---------------------------------------------------------------------------------------
        // Check D -- RT A -> backbuffer -> RT B transition isolation with DISTINCT rectangles.
        // rectA=(7,5,17,11) into RT A; rectB=(30,20,20,12) into RT B. Each target must contain
        // only its own sprite, mapped against its own dimensions.
        // ---------------------------------------------------------------------------------------
        {
            RenderTarget2D rtA(dev, 48, 32, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RenderTarget2D rtB(dev, 64, 40, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

            dev.SetRenderTarget(&rtA);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.Clear(Color::Black);                        // an intervening backbuffer frame
            dev.SetRenderTarget(&rtB);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(30, 20, 20, 12));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color aOwn   = ReadRT(rtA, 15, 10);   // rectA interior
            const Color aOther = ReadRT(rtA, 39, 25);   // where rectB would be -- must be Black
            const Color bOwn   = ReadRT(rtB, 39, 25);   // rectB interior
            const Color bOther = ReadRT(rtB, 15, 10);   // where rectA would be -- must be Black
            check(Matches(aOwn, Color::Red),
                  "Check D1: RT A keeps its own sprite (15,10)=Red across the transition: got="
                  + RgbStr(aOwn));
            check(Matches(aOther, Color::Black),
                  "Check D2: RT A did NOT receive RT B's sprite (39,25)=Black: got=" + RgbStr(aOther));
            check(Matches(bOwn, Color::Red),
                  "Check D3: RT B has its own sprite (39,25)=Red: got=" + RgbStr(bOwn));
            check(Matches(bOther, Color::Black),
                  "Check D4: RT B did NOT receive RT A's sprite (15,10)=Black: got=" + RgbStr(bOther));
        }

        // ---------------------------------------------------------------------------------------
        // Check E -- non-identity transformMatrix (scale 1.5 + translation (2,3)) into RT A.
        // Sprite dest Rectangle(4,2,8,6) -> corners (4,2)..(12,8) -> transform x'=1.5x+2,
        // y'=1.5y+3 -> transformed rect x in [8,20], y in [6,15] in RT pixel space. The pre-fix
        // backbuffer-relative map would instead land it near x in [4,10].
        // ---------------------------------------------------------------------------------------
        {
            RenderTarget2D rt(dev, 48, 32, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            Matrix transform = Matrix::getIdentityProperty();
            transform.M11 = 1.5f;
            transform.M22 = 1.5f;
            transform.M41 = 2.0f;
            transform.M42 = 3.0f;

            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr,
                         nullptr, transform);
                sb.Draw(whiteTex_, Rectangle(4, 2, 8, 6), Rectangle(0, 0, 1, 1), Color::Red);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color interior = ReadRT(rt, 14, 10);  // inside transformed rect, outside pre-fix
            const Color outside  = ReadRT(rt, 25, 10);  // right of the transformed rect
            check(Matches(interior, Color::Red),
                  "Check E1: transformMatrix (scale 1.5 + translate) sprite interior (14,10) is Red "
                  "-- transform applied in RT pixel space, converted with RT dims: got="
                  + RgbStr(interior));
            check(Matches(outside, Color::Black),
                  "Check E2: transformMatrix sprite right-outside (25,10) is Black: got="
                  + RgbStr(outside));
        }

        // ---------------------------------------------------------------------------------------
        // Check F -- texture orientation control. splitTex_ is Red on top, Blue on bottom. Drawn
        // (PointClamp) into RT A over Rectangle(10,4,12,24) -> x in [10,22], y in [4,28]. Correct
        // orientation keeps Red at the target top and Blue at the bottom (no vertical flip).
        // ---------------------------------------------------------------------------------------
        {
            RenderTarget2D rt(dev, 48, 32, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                sb.Draw(splitTex_, Rectangle(10, 4, 12, 24), Rectangle(0, 0, 1, 8), Color::White);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color topPixel    = ReadRT(rt, 16, 6);   // near the top of the dest rect
            const Color bottomPixel = ReadRT(rt, 16, 26);  // near the bottom of the dest rect
            check(Matches(topPixel, Color::Red),
                  "Check F1: split texture top stays Red in the RT (no vertical flip): got="
                  + RgbStr(topPixel));
            check(Matches(bottomPixel, Color::Blue),
                  "Check F2: split texture bottom stays Blue in the RT (no vertical flip): got="
                  + RgbStr(bottomPixel));
        }

        // ---------------------------------------------------------------------------------------
        // Check G -- RenderTargetCube FACE (size 48, different from the 96x72 backbuffer). This is
        // the exact path webgpu_rendertargetcube_test.cpp's Check C deliberately avoided drawing a
        // SpriteBatch sprite into (routing around this defect with a full-screen 3D quad instead).
        // Same Rectangle(7,5,17,11) into the PositiveX face must occupy those face-relative pixels.
        // ---------------------------------------------------------------------------------------
        {
            constexpr int kCubeSize = 48;
            RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTarget(&rtc, CubeMapFace::PositiveX);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color interior = ReadCubeFace(rtc, CubeMapFace::PositiveX, kCubeSize, 15, 10);
            const Color outside  = ReadCubeFace(rtc, CubeMapFace::PositiveX, kCubeSize, 5, 3);
            check(Matches(interior, Color::Red),
                  "Check G1: RenderTargetCube face(48) sprite interior (15,10) is Red -- face-relative "
                  "placement, not backbuffer-relative: got=" + RgbStr(interior));
            check(Matches(outside, Color::Black),
                  "Check G2: RenderTargetCube face bug-witness (5,3) is Black: got=" + RgbStr(outside));
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    WebGpuSpriteBatchRenderTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferW);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferH);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuSpriteBatchRenderTargetTest game;
    game.Run();
    return game.getResult();
}
