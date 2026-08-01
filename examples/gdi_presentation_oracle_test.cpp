// SPDX-License-Identifier: MS-PL
// GDI-054: deterministic geometry and RGBA-DIB output oracle using a memory DIBSection.

#include "CNA/Internal/Backends/Gdi/GdiPresentation.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace CNA::Internal::Backends;
using namespace CNA::Internal::Backends::Gdi;

namespace
{
    bool Expect(bool condition, const char* message)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", message);
        return condition;
    }

    bool SameRect(const GdiPresentationRect& actual, const GdiPresentationRect& expected)
    {
        return actual.x == expected.x && actual.y == expected.y &&
               actual.width == expected.width && actual.height == expected.height;
    }

    class MemorySurface final
    {
    public:
        MemorySurface(int width, int height) : width_(width), height_(height)
        {
            context_ = CreateCompatibleDC(nullptr);
            if (context_ == nullptr)
                throw std::runtime_error("CreateCompatibleDC failed");

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width_;
            info.bmiHeader.biHeight = -height_;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            bitmap_ = CreateDIBSection(context_, &info, DIB_RGB_COLORS,
                                       reinterpret_cast<void**>(&pixels_), nullptr, 0);
            if (bitmap_ == nullptr || pixels_ == nullptr)
                throw std::runtime_error("CreateDIBSection failed");
            previous_ = SelectObject(context_, bitmap_);
            if (previous_ == nullptr || previous_ == HGDI_ERROR)
                throw std::runtime_error("SelectObject(DIBSection) failed");
        }

        ~MemorySurface()
        {
            if (context_ != nullptr && previous_ != nullptr && previous_ != HGDI_ERROR)
                (void)SelectObject(context_, previous_);
            if (bitmap_ != nullptr) (void)DeleteObject(bitmap_);
            if (context_ != nullptr) (void)DeleteDC(context_);
        }

        MemorySurface(const MemorySurface&) = delete;
        MemorySurface& operator=(const MemorySurface&) = delete;

        void Clear(std::uint8_t blue, std::uint8_t green, std::uint8_t red)
        {
            for (int y = 0; y < height_; ++y)
            {
                for (int x = 0; x < width_; ++x)
                {
                    std::uint8_t* pixel = Pixel(x, y);
                    pixel[0] = blue;
                    pixel[1] = green;
                    pixel[2] = red;
                    pixel[3] = 0;
                }
            }
        }

        [[nodiscard]] HDC Context() const { return context_; }
        [[nodiscard]] int Width() const { return width_; }
        [[nodiscard]] int Height() const { return height_; }

        [[nodiscard]] bool IsRgb(int x, int y, int red, int green, int blue,
                                 int tolerance = 0) const
        {
            const std::uint8_t* pixel = Pixel(x, y);
            return std::abs(static_cast<int>(pixel[2]) - red) <= tolerance &&
                   std::abs(static_cast<int>(pixel[1]) - green) <= tolerance &&
                   std::abs(static_cast<int>(pixel[0]) - blue) <= tolerance;
        }

    private:
        [[nodiscard]] std::uint8_t* Pixel(int x, int y)
        {
            return pixels_ + (static_cast<std::size_t>(y) * width_ + x) * 4u;
        }
        [[nodiscard]] const std::uint8_t* Pixel(int x, int y) const
        {
            return pixels_ + (static_cast<std::size_t>(y) * width_ + x) * 4u;
        }

        int width_ = 0;
        int height_ = 0;
        HDC context_ = nullptr;
        HBITMAP bitmap_ = nullptr;
        HGDIOBJ previous_ = nullptr;
        std::uint8_t* pixels_ = nullptr;
    };

    std::vector<std::uint8_t> FourColors()
    {
        // Top-down RGBA: red, green / blue, yellow.
        return {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 0, 255,
        };
    }

    bool ExpectFourCorners(const MemorySurface& surface, const char* label)
    {
        const bool ok =
            surface.IsRgb(0, 0, 255, 0, 0) &&
            surface.IsRgb(surface.Width() - 1, 0, 0, 255, 0) &&
            surface.IsRgb(0, surface.Height() - 1, 0, 0, 255) &&
            surface.IsRgb(surface.Width() - 1, surface.Height() - 1, 255, 255, 0);
        return Expect(ok, label);
    }
}

int main()
{
    bool ok = true;

    ok &= Expect(SameRect(CalculateGdiPresentationDestination(
                            CnaPresentationMode::NativeBackBuffer, 7, 9, 3, 5),
                          {0, 0, 3, 5}),
                 "NativeBackBuffer keeps source dimensions");
    ok &= Expect(SameRect(CalculateGdiPresentationDestination(
                            CnaPresentationMode::Stretch, 7, 9, 3, 5),
                          {0, 0, 7, 9}),
                 "Stretch covers an odd-sized client");
    ok &= Expect(SameRect(CalculateGdiPresentationDestination(
                            CnaPresentationMode::FixedHeightDynamicWidth, 7, 9, 3, 5),
                          {0, 0, 7, 9}),
                 "FixedHeightDynamicWidth presentation covers its resolved client");
    ok &= Expect(SameRect(CalculateGdiPresentationDestination(
                            CnaPresentationMode::Letterbox, 7, 9, 3, 5),
                          {1, 0, 5, 9}),
                 "Letterbox rounds and centers odd destination geometry");
    ok &= Expect(SameRect(CalculateGdiPresentationDestination(
                            CnaPresentationMode::Overscan, 7, 9, 3, 5),
                          {0, -1, 7, 12}),
                 "Overscan rounds and centers cropped odd destination geometry");
    ok &= Expect(SameRect(CalculateGdiPresentationDestination(
                            CnaPresentationMode::Stretch, 0, 9, 3, 5), {}),
                 "non-drawable client produces an empty destination");

    const GdiPresentationSize dynamicSize = ResolveGdiLogicalSize(
        CnaPresentationMode::FixedHeightDynamicWidth,
        180, 101, 23, 17, 23, 17);
    ok &= Expect(dynamicSize.width == 30 && dynamicSize.height == 17,
                 "dynamic-width mode derives logical width from drawable-pixel aspect");
    const GdiPresentationSize minimizedSize = ResolveGdiLogicalSize(
        CnaPresentationMode::FixedHeightDynamicWidth,
        0, 0, 23, 17, dynamicSize.width, dynamicSize.height);
    ok &= Expect(minimizedSize.width == dynamicSize.width &&
                     minimizedSize.height == dynamicSize.height,
                 "zero-size minimize transition retains the last logical backbuffer dimensions");

    const GdiPresentationPlan noDamage = BuildGdiPresentationPlan(
        CnaPresentationMode::Stretch, 4, 4, 4, 4,
        true, false, false, false, {});
    ok &= Expect(noDamage.path == GdiBlitPath::None,
                 "dirty mode skips a synchronized no-damage frame");
    const GdiPresentationPlan dirty = BuildGdiPresentationPlan(
        CnaPresentationMode::Stretch, 4, 4, 4, 4,
        true, false, false, true, {1, 2, 2, 1});
    ok &= Expect(dirty.path == GdiBlitPath::NativeDirty && !dirty.clearClient &&
                     SameRect(dirty.source, {1, 2, 2, 1}),
                 "1:1 dirty frame selects an unclobbered partial band");
    const GdiPresentationPlan clippedDirty = BuildGdiPresentationPlan(
        CnaPresentationMode::Stretch, 4, 4, 4, 4,
        true, false, false, true, {-2, 3, 4, 4});
    ok &= Expect(clippedDirty.path == GdiBlitPath::NativeDirty &&
                     SameRect(clippedDirty.source, {0, 3, 2, 1}),
                 "dirty planning clips with overflow-safe exclusive edges");
    const GdiPresentationPlan repair = BuildGdiPresentationPlan(
        CnaPresentationMode::Stretch, 4, 4, 4, 4,
        true, true, false, false, {});
    ok &= Expect(repair.path == GdiBlitPath::NativeFull && repair.clearClient,
                 "native-client invalidation forces a complete 1:1 repaint");
    const GdiPresentationPlan scaled = BuildGdiPresentationPlan(
        CnaPresentationMode::Stretch, 7, 9, 3, 5,
        true, false, false, true, {1, 1, 1, 1});
    ok &= Expect(scaled.path == GdiBlitPath::Stretch && scaled.clearClient,
                 "scaled output rejects partial damage and chooses StretchDIBits");

    float logicalX = -1.0f, logicalY = -1.0f;
    ok &= Expect(!MapGdiWindowToLogical(CnaPresentationMode::Letterbox,
                                        7, 9, 3, 5, 0.99f, 4.0f, logicalX, logicalY),
                 "window-to-logical rejects the left letterbox bar");
    ok &= Expect(MapGdiWindowToLogical(CnaPresentationMode::Letterbox,
                                       7, 9, 3, 5, 1.0f, 0.0f, logicalX, logicalY) &&
                     std::fabs(logicalX) < 0.001f && std::fabs(logicalY) < 0.001f,
                 "window-to-logical includes destination top-left");
    ok &= Expect(!MapGdiWindowToLogical(CnaPresentationMode::Letterbox,
                                        7, 9, 3, 5, 6.0f, 0.0f, logicalX, logicalY),
                 "window-to-logical excludes destination right edge");
    float windowX = -1.0f, windowY = -1.0f;
    ok &= Expect(MapGdiLogicalToWindow(CnaPresentationMode::Letterbox,
                                       7, 9, 3, 5, 0.0f, 0.0f, windowX, windowY) &&
                     std::fabs(windowX - 1.0f) < 0.001f && std::fabs(windowY) < 0.001f,
                 "logical-to-window maps top-left to the centered destination");
    ok &= Expect(!MapGdiLogicalToWindow(CnaPresentationMode::Letterbox,
                                        7, 9, 3, 5, 3.0f, 0.0f, windowX, windowY),
                 "logical-to-window excludes logical right edge");

    struct DensityCase
    {
        int drawableWidth;
        int drawableHeight;
        const char* label;
    };
    constexpr DensityCase densityCases[]{
        {120, 80, "100%"},
        {180, 120, "150%"},
        {240, 160, "200%"},
    };
    for (const DensityCase& density : densityCases)
    {
        logicalX = logicalY = -1.0f;
        const bool forward = MapGdiSdlWindowToLogical(
            CnaPresentationMode::Stretch, 120, 80,
            density.drawableWidth, density.drawableHeight,
            60, 40, 60.0f, 40.0f, logicalX, logicalY);
        char forwardMessage[160]{};
        std::snprintf(forwardMessage, sizeof(forwardMessage),
                      "%s scale maps SDL-window center through drawable pixels", density.label);
        ok &= Expect(forward && std::fabs(logicalX - 30.0f) < 0.001f &&
                         std::fabs(logicalY - 20.0f) < 0.001f,
                     forwardMessage);

        windowX = windowY = -1.0f;
        const bool inverse = MapGdiLogicalToSdlWindow(
            CnaPresentationMode::Stretch, 120, 80,
            density.drawableWidth, density.drawableHeight,
            60, 40, 30.0f, 20.0f, windowX, windowY);
        char inverseMessage[160]{};
        std::snprintf(inverseMessage, sizeof(inverseMessage),
                      "%s scale maps logical center back to SDL-window coordinates", density.label);
        ok &= Expect(inverse && std::fabs(windowX - 60.0f) < 0.001f &&
                         std::fabs(windowY - 40.0f) < 0.001f,
                     inverseMessage);

        // A square drawable letterboxes the 2:1 logical image into its middle half. SDL event
        // coordinates must hit the same bars at every drawable density.
        logicalX = logicalY = -1.0f;
        char barMessage[160]{};
        std::snprintf(barMessage, sizeof(barMessage),
                      "%s scale rejects the same SDL-coordinate letterbox bar", density.label);
        ok &= Expect(!MapGdiSdlWindowToLogical(
                         CnaPresentationMode::Letterbox, 100, 100,
                         density.drawableWidth, density.drawableWidth,
                         80, 40, 50.0f, 24.99f, logicalX, logicalY),
                     barMessage);
    }
    ok &= Expect(!MapGdiSdlWindowToLogical(
                     CnaPresentationMode::Stretch, 0, 80, 120, 80,
                     60, 40, 1.0f, 1.0f, logicalX, logicalY) &&
                     !MapGdiLogicalToSdlWindow(
                         CnaPresentationMode::Stretch, 120, 80, 0, 80,
                         60, 40, 1.0f, 1.0f, windowX, windowY),
                 "SDL/drawable coordinate wrappers reject zero-sized metrics");

    try
    {
        const std::vector<std::uint8_t> fourColors = FourColors();

        MemorySurface native(2, 2);
        native.Clear(255, 0, 255);
        const GdiPresentationPlan nativePlan = BuildGdiPresentationPlan(
            CnaPresentationMode::Stretch, 2, 2, 2, 2,
            false, false, true, true, {0, 0, 2, 2});
        const GdiBlitResult nativeResult = BlitGdiRgbaToDeviceContext(
            native.Context(), 2, 2, 2, 2, fourColors.data(), fourColors.size(),
            nativePlan, COLORONCOLOR);
        ok &= Expect(nativeResult.success && nativeResult.copiedLines > 0,
                     "native SetDIBitsToDevice succeeds on a memory DC");
        ok &= ExpectFourCorners(native,
            "native DIB preserves RGBA channel mapping and top-down row orientation");

        MemorySurface stretched(4, 4);
        stretched.Clear(255, 0, 255);
        const GdiPresentationPlan stretchPlan = BuildGdiPresentationPlan(
            CnaPresentationMode::Stretch, 4, 4, 2, 2,
            false, false, true, true, {0, 0, 2, 2});
        const GdiBlitResult stretchResult = BlitGdiRgbaToDeviceContext(
            stretched.Context(), 4, 4, 2, 2, fourColors.data(), fourColors.size(),
            stretchPlan, COLORONCOLOR);
        ok &= Expect(stretchResult.success && stretchResult.copiedLines > 0,
                     "scaled COLORONCOLOR StretchDIBits succeeds on a memory DC");
        ok &= ExpectFourCorners(stretched,
                               "scaled nearest output preserves all four oriented corners");

        MemorySurface halftone(5, 5);
        const GdiPresentationPlan halftonePlan = BuildGdiPresentationPlan(
            CnaPresentationMode::Stretch, 5, 5, 2, 2,
            false, false, true, true, {0, 0, 2, 2});
        const GdiBlitResult halftoneResult = BlitGdiRgbaToDeviceContext(
            halftone.Context(), 5, 5, 2, 2, fourColors.data(), fourColors.size(),
            halftonePlan, HALFTONE);
        ok &= Expect(halftoneResult.success && halftoneResult.copiedLines > 0,
                     "HALFTONE filter setup and scaled DIB transfer succeed");

        MemorySurface letterbox(6, 4);
        letterbox.Clear(255, 0, 255);
        const GdiPresentationPlan letterboxPlan = BuildGdiPresentationPlan(
            CnaPresentationMode::Letterbox, 6, 4, 2, 2,
            false, false, true, true, {0, 0, 2, 2});
        const GdiBlitResult letterboxResult = BlitGdiRgbaToDeviceContext(
            letterbox.Context(), 6, 4, 2, 2, fourColors.data(), fourColors.size(),
            letterboxPlan, COLORONCOLOR);
        ok &= Expect(letterboxResult.success &&
                         SameRect(letterboxPlan.destination, {1, 0, 4, 4}),
                     "letterbox plan blits into the centered memory-DC rectangle");
        bool barsBlack = true;
        for (int y = 0; y < 4; ++y)
            barsBlack &= letterbox.IsRgb(0, y, 0, 0, 0) &&
                         letterbox.IsRgb(5, y, 0, 0, 0);
        ok &= Expect(barsBlack && letterbox.IsRgb(1, 0, 255, 0, 0) &&
                         letterbox.IsRgb(4, 3, 255, 255, 0),
                     "letterbox output has deterministic black bars and oriented content");

        MemorySurface overscan(6, 4);
        overscan.Clear(255, 0, 255);
        const GdiPresentationPlan overscanPlan = BuildGdiPresentationPlan(
            CnaPresentationMode::Overscan, 6, 4, 2, 2,
            false, false, true, true, {0, 0, 2, 2});
        const GdiBlitResult overscanResult = BlitGdiRgbaToDeviceContext(
            overscan.Context(), 6, 4, 2, 2, fourColors.data(), fourColors.size(),
            overscanPlan, COLORONCOLOR);
        bool overscanCovered = overscanResult.success;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 6; ++x)
                overscanCovered &= !overscan.IsRgb(x, y, 0, 0, 0);
        ok &= Expect(overscanCovered && SameRect(overscanPlan.destination, {0, -1, 6, 6}),
                     "overscan output covers and clips the complete memory surface");

        std::vector<std::uint8_t> grid(4 * 4 * 4);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y * 4 + x) * 4u;
                grid[index + 0] = static_cast<std::uint8_t>(10 + x * 40);
                grid[index + 1] = static_cast<std::uint8_t>(20 + y * 50);
                grid[index + 2] = 100;
                grid[index + 3] = 255;
            }
        }
        MemorySurface dirtySurface(4, 4);
        dirtySurface.Clear(0, 0, 0);
        const GdiPresentationPlan dirtyPlan = BuildGdiPresentationPlan(
            CnaPresentationMode::Stretch, 4, 4, 4, 4,
            true, false, false, true, {1, 2, 2, 1});
        const GdiBlitResult dirtyResult = BlitGdiRgbaToDeviceContext(
            dirtySurface.Context(), 4, 4, 4, 4, grid.data(), grid.size(),
            dirtyPlan, COLORONCOLOR);
        bool onlyDirtyPixelsChanged = dirtyResult.success;
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const bool inside = y == 2 && (x == 1 || x == 2);
                onlyDirtyPixelsChanged &= inside
                    ? dirtySurface.IsRgb(x, y, 10 + x * 40, 20 + y * 50, 100)
                    : dirtySurface.IsRgb(x, y, 0, 0, 0);
            }
        }
        ok &= Expect(onlyDirtyPixelsChanged,
                     "non-zero-Y dirty DIB updates exactly its requested subrectangle");

        MemorySurface clipped(2, 2);
        clipped.Clear(255, 0, 255);
        const GdiPresentationPlan clippedPlan = BuildGdiPresentationPlan(
            CnaPresentationMode::NativeBackBuffer, 2, 2, 4, 4,
            false, false, true, true, {0, 0, 4, 4});
        const GdiBlitResult clippedResult = BlitGdiRgbaToDeviceContext(
            clipped.Context(), 2, 2, 4, 4, grid.data(), grid.size(),
            clippedPlan, COLORONCOLOR);
        ok &= Expect(clippedResult.success && clippedResult.copiedLines > 0 &&
                         clipped.IsRgb(0, 0, 10, 20, 100) &&
                         clipped.IsRgb(1, 1, 50, 70, 100),
                     "clipped SetDIBitsToDevice accepts a positive legal scan-line count");

        const GdiBlitResult forcedFailure = BlitGdiRgbaToDeviceContext(
            native.Context(), 2, 2, 2, 2, fourColors.data(), fourColors.size(),
            nativePlan, COLORONCOLOR, true);
        ok &= Expect(!forcedFailure.success && forcedFailure.copiedLines == 0 &&
                         forcedFailure.win32Error == ERROR_WRITE_FAULT,
                     "DIB helper exposes deterministic zero-line failure telemetry");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "GDI presentation oracle failed: %s\n", error.what());
        ok = false;
    }

    return ok ? 0 : 1;
}
