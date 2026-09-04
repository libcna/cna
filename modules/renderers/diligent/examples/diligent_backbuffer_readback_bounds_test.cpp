// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-63: real-device proof that DiligentRenderer::ReadBackbuffer()
// is valid and bounded for every CnaPresentationMode -- constructed directly against
// IGraphicsRenderer (bypassing GraphicsDevice/GraphicsDeviceManager, matching
// directx3_resize_transaction_test.cpp's own established low-level pattern), since the physical
// (window) vs. virtual (logical) resolution mismatch this test needs cannot be produced through
// the public XNA API alone: SDL_SetWindowSize is a no-op under the headless Xvfb this renderer runs
// on (see backbuffer_readback_dimension_test.cpp's own note), but a mismatch can be requested
// directly at construction via GraphicsRendererCreateArgs::virtualWidth/virtualHeight.
//
// Physical window: 64x48 (4:3). Virtual/logical canvas: 40x40 (1:1, deliberately a different
// aspect) for every mode except NativeBackBuffer (which ignores virtual entirely) and
// FixedHeightDynamicWidth (which locks height and derives width from the physical aspect,
// replicated here with the exact same formula DiligentRenderer::ComputeLogicalViewport()
// itself uses -- 40 * 64/48 = 53.33).
//
// Before DILIGENT-63, ReadBackbuffer() clamped MinX/MinY to 0 but computed MaxX/MaxY as
// `clampedMin + physicalW/H` -- SHIFTING the copied region rather than clipping it whenever the
// requested physical origin was negative, and never clamped against the real back buffer extent
// at all. Overscan's own math (scale = max(scaleX, scaleY), centred) makes this physical
// out-of-bounds case arise from an entirely ordinary "read the whole logical canvas" call, not a
// contrived one: with this test's own 64x48/40x40 configuration, Overscan's viewport is
// (x=0, y=-8, width=64, height=64) -- 8 physical pixels of the logical canvas's top and bottom
// edges fall outside the real [0, 48) physical height on either side.
//
// Check per mode (Native, Stretch, Letterbox, Overscan, FixedHeightDynamicWidth) -- a full-canvas
// read's solidly-interior pixels equal the clear colour and the call does not throw.
// Additional Overscan checks -- rows solidly within the cropped-out top/bottom bands read back
// ZERO (uncovered), not the clear colour, garbage, or a crash.
// Additional Letterbox check -- a sub-rectangle read (not the full canvas) is also byte-exact,
// covering DILIGENT-63's own "subregion" acceptance criterion.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "CNA/Internal/Renderers/Diligent/DiligentRenderer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::Diligent::DiligentRenderer;

namespace
{
    constexpr int kPhysicalW = 64;
    constexpr int kPhysicalH = 48;
    constexpr int kVirtualW = 40;
    constexpr int kVirtualH = 40;

    constexpr float kClearR = 0.2f, kClearG = 0.6f, kClearB = 0.8f;
    constexpr std::uint8_t kClearR8 = 51, kClearG8 = 153, kClearB8 = 204; // 0.2/0.6/0.8 * 255

    bool IsClearColor(const std::uint8_t* px)
    {
        return std::abs(px[0] - kClearR8) <= 8 && std::abs(px[1] - kClearG8) <= 8 &&
               std::abs(px[2] - kClearB8) <= 8;
    }

    bool IsZero(const std::uint8_t* px)
    {
        return px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 0;
    }

    int passCount = 0;
    int totalCount = 0;

    void Check(bool ok, const std::string& label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok)
            ++passCount;
    }

    /// Reads a single pixel via the renderer's own ReadBackbuffer(), returning whether it threw.
    bool ReadPixel(DiligentRenderer& renderer, int x, int y, std::uint8_t out[4])
    {
        try
        {
            renderer.ReadBackbuffer(x, y, 1, 1, out);
            return true;
        }
        catch (const std::exception& error)
        {
            std::printf("    (ReadBackbuffer(%d,%d,1,1) threw: %s)\n", x, y, error.what());
            return false;
        }
    }

    void RunMode(SDL_Window* window, const char* label, CnaPresentationMode mode, int virtualW,
                int virtualH)
    {
        CNA::Examples::SdlTestGlContext glContext(window);
        GraphicsRendererCreateArgs args = CNA::Examples::SdlTestRendererArgs(
            window, &glContext, nullptr, virtualW, virtualH, mode);

        DiligentRenderer renderer(args);
        renderer.Clear(kClearR, kClearG, kClearB, 1.0f);

        // A full-canvas read's solidly-interior pixels (well away from any edge that could be
        // affected by rounding or, for Overscan, genuine cropping) must read the clear colour and
        // must not throw.
        const int cx = virtualW / 2;
        const int cy = virtualH / 2;
        std::uint8_t px[4] = {0, 0, 0, 0};
        const bool readOk = ReadPixel(renderer, cx, cy, px);
        Check(readOk, std::string(label) + ": centre pixel read does not throw");
        if (readOk)
            Check(IsClearColor(px), std::string(label) + ": centre pixel reads the clear colour");

        if (mode == CnaPresentationMode::Overscan)
        {
            // This configuration's own Overscan viewport: x=0, y=-8, width=64, height=64 (scale =
            // max(64/40, 48/40) = 1.6, centred). Logical rows solidly within [0, 3) map to physical
            // y < 0; rows solidly within [37, 40) map to physical y >= 48. Both are genuinely
            // outside the real back buffer and must read back zero, not the clear colour, garbage,
            // or crash the copy.
            std::uint8_t top[4] = {0, 0, 0, 0};
            const bool topOk = ReadPixel(renderer, cx, 0, top);
            Check(topOk, "Overscan: top-edge pixel read does not throw");
            if (topOk)
                Check(IsZero(top), "Overscan: top-edge pixel (physically out of bounds) reads zero");

            std::uint8_t bottom[4] = {0, 0, 0, 0};
            const bool bottomOk = ReadPixel(renderer, cx, virtualH - 1, bottom);
            Check(bottomOk, "Overscan: bottom-edge pixel read does not throw");
            if (bottomOk)
                Check(IsZero(bottom),
                     "Overscan: bottom-edge pixel (physically out of bounds) reads zero");
        }

        if (mode == CnaPresentationMode::Letterbox)
        {
            // Subregion check (DILIGENT-63's own acceptance criterion): a small rectangle fully
            // inside the visible canvas, away from the pillarbox bars, must read back byte-exact.
            constexpr int kSubW = 6;
            constexpr int kSubH = 6;
            std::vector<std::uint8_t> sub(static_cast<std::size_t>(kSubW) * kSubH * 4, 0xCD);
            bool subThrew = false;
            try
            {
                renderer.ReadBackbuffer(cx - kSubW / 2, cy - kSubH / 2, kSubW, kSubH, sub.data());
            }
            catch (const std::exception& error)
            {
                subThrew = true;
                std::printf("    (Letterbox subregion read threw: %s)\n", error.what());
            }
            Check(!subThrew, "Letterbox: subregion read does not throw");
            if (!subThrew)
            {
                bool allClear = true;
                for (int i = 0; i < kSubW * kSubH; ++i)
                {
                    if (!IsClearColor(&sub[static_cast<std::size_t>(i) * 4]))
                        allClear = false;
                }
                Check(allClear, "Letterbox: subregion reads the clear colour everywhere");
            }
        }
    }
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] CNA Diligent smoke: no usable device (SDL_InitSubSystem failed: %s)\n",
                    SDL_GetError());
        return 77;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN;
    const char* deviceOverride = std::getenv("CNA_DILIGENT_DEVICE");
    if (deviceOverride != nullptr && std::strcmp(deviceOverride, "opengl") == 0)
        flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL;
    SDL_Window* window = SDL_CreateWindow("Diligent backbuffer readback bounds", kPhysicalW,
                                          kPhysicalH, flags);
    if (window == nullptr)
    {
        std::printf("[SKIP] CNA Diligent smoke: no usable device (SDL_CreateWindow failed: %s)\n",
                    SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 77;
    }

    int result = 1;
    try
    {
        RunMode(window, "NativeBackBuffer", CnaPresentationMode::NativeBackBuffer, kPhysicalW,
               kPhysicalH);
        RunMode(window, "Stretch", CnaPresentationMode::Stretch, kVirtualW, kVirtualH);
        RunMode(window, "Letterbox", CnaPresentationMode::Letterbox, kVirtualW, kVirtualH);
        RunMode(window, "Overscan", CnaPresentationMode::Overscan, kVirtualW, kVirtualH);
        // ComputeLogicalViewport()'s own formula: logicalWidth = virtualHeight * physicalWidth /
        // physicalHeight. Passed as the integer virtualWidth here to match what a real caller
        // reads back from PresentationParameters after Reset() recomputes it the same way.
        const int fixedLogicalWidth =
            static_cast<int>(static_cast<float>(kVirtualH) * kPhysicalW / kPhysicalH);
        RunMode(window, "FixedHeightDynamicWidth", CnaPresentationMode::FixedHeightDynamicWidth,
               fixedLogicalWidth, kVirtualH);

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        result = passCount == totalCount ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        result = 1;
    }

    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}
