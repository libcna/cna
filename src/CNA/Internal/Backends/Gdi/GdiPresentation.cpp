// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Backends/Gdi/GdiPresentation.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace CNA::Internal::Backends::Gdi
{
    namespace
    {
        struct RgbaBitmapInfo
        {
            BITMAPINFOHEADER header{};
            DWORD channelMasks[3] = {
                0x000000FFu,
                0x0000FF00u,
                0x00FF0000u,
            };
        };

        int SaturatingRoundToInt(double value)
        {
            if (!std::isfinite(value))
                return value < 0.0 ? std::numeric_limits<int>::min()
                                   : std::numeric_limits<int>::max();
            const double rounded = std::round(value);
            if (rounded <= static_cast<double>(std::numeric_limits<int>::min()))
                return std::numeric_limits<int>::min();
            if (rounded >= static_cast<double>(std::numeric_limits<int>::max()))
                return std::numeric_limits<int>::max();
            return static_cast<int>(rounded);
        }

        int SaturatingInt(long long value)
        {
            return static_cast<int>(std::clamp<long long>(
                value, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        }

        GdiPresentationRect ClipDirtyRectangle(const GdiPresentationRect& rectangle,
                                               int width, int height)
        {
            const long long right = static_cast<long long>(rectangle.x) + rectangle.width;
            const long long bottom = static_cast<long long>(rectangle.y) + rectangle.height;
            const int left = static_cast<int>(std::clamp<long long>(rectangle.x, 0, width));
            const int top = static_cast<int>(std::clamp<long long>(rectangle.y, 0, height));
            const int clippedRight = static_cast<int>(std::clamp<long long>(right, 0, width));
            const int clippedBottom = static_cast<int>(std::clamp<long long>(bottom, 0, height));
            if (clippedRight <= left || clippedBottom <= top)
                return {};
            return {left, top, clippedRight - left, clippedBottom - top};
        }

        GdiBlitResult Failure(const char* operation, DWORD error)
        {
            return {false, 0, static_cast<std::uint32_t>(error), operation};
        }

        GdiBlitResult DibResult(const char* operation, int copiedLines)
        {
            if (copiedLines == 0 || copiedLines == static_cast<int>(GDI_ERROR))
                return Failure(operation, GetLastError());
            return {true, copiedLines, 0, operation};
        }
    }

    GdiPresentationSize ResolveGdiLogicalSize(
        CnaPresentationMode presentationMode,
        int drawablePixelWidth, int drawablePixelHeight,
        int requestedVirtualWidth, int requestedVirtualHeight,
        int retainedWidth, int retainedHeight)
    {
        const bool hasDrawableSize = drawablePixelWidth > 0 && drawablePixelHeight > 0;
        if (presentationMode == CnaPresentationMode::FixedHeightDynamicWidth &&
            requestedVirtualHeight > 0)
        {
            if (hasDrawableSize)
            {
                return {
                    std::max(1, SaturatingRoundToInt(
                        static_cast<double>(drawablePixelWidth) * requestedVirtualHeight /
                        drawablePixelHeight)),
                    requestedVirtualHeight,
                };
            }
            if (retainedWidth > 0 && retainedHeight > 0)
                return {retainedWidth, retainedHeight};
            return {std::max(1, requestedVirtualWidth), requestedVirtualHeight};
        }

        if (requestedVirtualWidth > 0 && requestedVirtualHeight > 0)
            return {requestedVirtualWidth, requestedVirtualHeight};
        if (hasDrawableSize)
            return {drawablePixelWidth, drawablePixelHeight};
        return {retainedWidth, retainedHeight};
    }

    GdiPresentationRect CalculateGdiPresentationDestination(
        CnaPresentationMode presentationMode, int clientWidth, int clientHeight,
        int sourceWidth, int sourceHeight)
    {
        if (clientWidth <= 0 || clientHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
            return {};

        if (presentationMode == CnaPresentationMode::NativeBackBuffer)
            return {0, 0, sourceWidth, sourceHeight};
        if (presentationMode == CnaPresentationMode::Stretch ||
            presentationMode == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            return {0, 0, clientWidth, clientHeight};
        }

        const double sx = static_cast<double>(clientWidth) / sourceWidth;
        const double sy = static_cast<double>(clientHeight) / sourceHeight;
        const double scale = presentationMode == CnaPresentationMode::Overscan
            ? std::max(sx, sy) : std::min(sx, sy);
        const int width = std::max(1, SaturatingRoundToInt(sourceWidth * scale));
        const int height = std::max(1, SaturatingRoundToInt(sourceHeight * scale));
        return {
            SaturatingInt((static_cast<long long>(clientWidth) - width) / 2),
            SaturatingInt((static_cast<long long>(clientHeight) - height) / 2),
            width,
            height,
        };
    }

    GdiPresentationPlan BuildGdiPresentationPlan(
        CnaPresentationMode presentationMode,
        int clientWidth, int clientHeight, int sourceWidth, int sourceHeight,
        bool dirtyPresentationRequested, bool clientNeedsRepair,
        bool backbufferFullyDirty, bool dirtyValid,
        const GdiPresentationRect& dirtyRectangle)
    {
        GdiPresentationPlan plan;
        plan.destination = CalculateGdiPresentationDestination(
            presentationMode, clientWidth, clientHeight, sourceWidth, sourceHeight);
        if (plan.destination.width <= 0 || plan.destination.height <= 0)
            return plan;

        const bool scaled = plan.destination.width != sourceWidth ||
                            plan.destination.height != sourceHeight;
        const bool mustPresentFull = !dirtyPresentationRequested || clientNeedsRepair ||
                                     backbufferFullyDirty || scaled;
        if (mustPresentFull)
        {
            plan.path = scaled ? GdiBlitPath::Stretch : GdiBlitPath::NativeFull;
            plan.source = {0, 0, sourceWidth, sourceHeight};
            plan.clearClient = true;
            return plan;
        }

        if (dirtyValid)
        {
            plan.source = ClipDirtyRectangle(dirtyRectangle, sourceWidth, sourceHeight);
            if (plan.source.width > 0 && plan.source.height > 0)
                plan.path = GdiBlitPath::NativeDirty;
        }
        return plan;
    }

    bool MapGdiWindowToLogical(
        CnaPresentationMode presentationMode,
        int clientWidth, int clientHeight, int logicalWidth, int logicalHeight,
        float windowX, float windowY, float& logicalX, float& logicalY)
    {
        const GdiPresentationRect destination = CalculateGdiPresentationDestination(
            presentationMode, clientWidth, clientHeight, logicalWidth, logicalHeight);
        if (destination.width <= 0 || destination.height <= 0 ||
            !std::isfinite(windowX) || !std::isfinite(windowY) ||
            windowX < destination.x || windowY < destination.y ||
            windowX >= static_cast<double>(destination.x) + destination.width ||
            windowY >= static_cast<double>(destination.y) + destination.height)
        {
            return false;
        }
        logicalX = static_cast<float>(
            (windowX - destination.x) * logicalWidth / destination.width);
        logicalY = static_cast<float>(
            (windowY - destination.y) * logicalHeight / destination.height);
        return logicalX >= 0.0f && logicalY >= 0.0f &&
               logicalX < logicalWidth && logicalY < logicalHeight;
    }

    bool MapGdiLogicalToWindow(
        CnaPresentationMode presentationMode,
        int clientWidth, int clientHeight, int logicalWidth, int logicalHeight,
        float logicalX, float logicalY, float& windowX, float& windowY)
    {
        if (logicalWidth <= 0 || logicalHeight <= 0 ||
            !std::isfinite(logicalX) || !std::isfinite(logicalY) ||
            logicalX < 0.0f || logicalY < 0.0f ||
            logicalX >= logicalWidth || logicalY >= logicalHeight)
        {
            return false;
        }
        const GdiPresentationRect destination = CalculateGdiPresentationDestination(
            presentationMode, clientWidth, clientHeight, logicalWidth, logicalHeight);
        if (destination.width <= 0 || destination.height <= 0)
            return false;
        windowX = static_cast<float>(
            destination.x + static_cast<double>(logicalX) * destination.width / logicalWidth);
        windowY = static_cast<float>(
            destination.y + static_cast<double>(logicalY) * destination.height / logicalHeight);
        return true;
    }

    bool MapGdiSdlWindowToLogical(
        CnaPresentationMode presentationMode,
        int windowCoordinateWidth, int windowCoordinateHeight,
        int drawablePixelWidth, int drawablePixelHeight,
        int logicalWidth, int logicalHeight,
        float windowX, float windowY, float& logicalX, float& logicalY)
    {
        if (windowCoordinateWidth <= 0 || windowCoordinateHeight <= 0 ||
            drawablePixelWidth <= 0 || drawablePixelHeight <= 0 ||
            !std::isfinite(windowX) || !std::isfinite(windowY))
        {
            return false;
        }

        const float pixelX = static_cast<float>(
            static_cast<double>(windowX) * drawablePixelWidth / windowCoordinateWidth);
        const float pixelY = static_cast<float>(
            static_cast<double>(windowY) * drawablePixelHeight / windowCoordinateHeight);
        return MapGdiWindowToLogical(
            presentationMode, drawablePixelWidth, drawablePixelHeight,
            logicalWidth, logicalHeight, pixelX, pixelY, logicalX, logicalY);
    }

    bool MapGdiLogicalToSdlWindow(
        CnaPresentationMode presentationMode,
        int windowCoordinateWidth, int windowCoordinateHeight,
        int drawablePixelWidth, int drawablePixelHeight,
        int logicalWidth, int logicalHeight,
        float logicalX, float logicalY, float& windowX, float& windowY)
    {
        if (windowCoordinateWidth <= 0 || windowCoordinateHeight <= 0 ||
            drawablePixelWidth <= 0 || drawablePixelHeight <= 0)
        {
            return false;
        }

        float pixelX = 0.0f;
        float pixelY = 0.0f;
        if (!MapGdiLogicalToWindow(
                presentationMode, drawablePixelWidth, drawablePixelHeight,
                logicalWidth, logicalHeight, logicalX, logicalY, pixelX, pixelY))
        {
            return false;
        }

        windowX = static_cast<float>(
            static_cast<double>(pixelX) * windowCoordinateWidth / drawablePixelWidth);
        windowY = static_cast<float>(
            static_cast<double>(pixelY) * windowCoordinateHeight / drawablePixelHeight);
        return std::isfinite(windowX) && std::isfinite(windowY);
    }

    GdiBlitResult BlitGdiRgbaToDeviceContext(
        void* rawDeviceContext, int clientWidth, int clientHeight,
        int sourceWidth, int sourceHeight,
        const std::uint8_t* rgbaPixels, std::size_t rgbaByteCount,
        const GdiPresentationPlan& plan, int stretchMode, bool forceZeroDibResult)
    {
        const HDC deviceContext = static_cast<HDC>(rawDeviceContext);
        if (plan.path == GdiBlitPath::None)
            return {true, 0, 0, "none"};
        if (deviceContext == nullptr || clientWidth <= 0 || clientHeight <= 0 ||
            sourceWidth <= 0 || sourceHeight <= 0 || rgbaPixels == nullptr)
        {
            return Failure("validate DIB arguments", ERROR_INVALID_PARAMETER);
        }

        const std::size_t width = static_cast<std::size_t>(sourceWidth);
        const std::size_t height = static_cast<std::size_t>(sourceHeight);
        if (width > std::numeric_limits<std::size_t>::max() / 4u ||
            height > std::numeric_limits<std::size_t>::max() / (width * 4u))
        {
            return Failure("validate DIB size", ERROR_ARITHMETIC_OVERFLOW);
        }
        const std::size_t requiredBytes = width * height * 4u;
        if (rgbaByteCount < requiredBytes || requiredBytes > std::numeric_limits<DWORD>::max())
            return Failure("validate DIB size", ERROR_INSUFFICIENT_BUFFER);

        if (plan.clearClient)
        {
            SetLastError(ERROR_SUCCESS);
            HBRUSH blackBrush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            if (blackBrush == nullptr)
                return Failure("GetStockObject(BLACK_BRUSH)", GetLastError());
            const RECT clientRect{0, 0, clientWidth, clientHeight};
            SetLastError(ERROR_SUCCESS);
            if (FillRect(deviceContext, &clientRect, blackBrush) == 0)
                return Failure("FillRect", GetLastError());
        }

        RgbaBitmapInfo bitmapInfo{};
        bitmapInfo.header.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.header.biWidth = sourceWidth;
        bitmapInfo.header.biHeight = -sourceHeight;
        bitmapInfo.header.biPlanes = 1;
        bitmapInfo.header.biBitCount = 32;
        bitmapInfo.header.biCompression = BI_BITFIELDS;
        bitmapInfo.header.biSizeImage = static_cast<DWORD>(requiredBytes);

        const auto callDib = [&](const auto& operation) {
            if (forceZeroDibResult)
            {
                SetLastError(ERROR_WRITE_FAULT);
                return 0;
            }
            SetLastError(ERROR_SUCCESS);
            return operation();
        };

        if (plan.path == GdiBlitPath::NativeDirty)
        {
            const GdiPresentationRect dirty = ClipDirtyRectangle(
                plan.source, sourceWidth, sourceHeight);
            if (dirty.width <= 0 || dirty.height <= 0)
                return Failure("validate dirty DIB rectangle", ERROR_INVALID_PARAMETER);
            RgbaBitmapInfo dirtyInfo = bitmapInfo;
            dirtyInfo.header.biHeight = -dirty.height;
            dirtyInfo.header.biSizeImage = static_cast<DWORD>(width * dirty.height * 4u);
            const std::size_t offset = static_cast<std::size_t>(dirty.y) * width * 4u;
            const long long destinationX = static_cast<long long>(plan.destination.x) + dirty.x;
            const long long destinationY = static_cast<long long>(plan.destination.y) + dirty.y;
            if (destinationX < std::numeric_limits<int>::min() ||
                destinationX > std::numeric_limits<int>::max() ||
                destinationY < std::numeric_limits<int>::min() ||
                destinationY > std::numeric_limits<int>::max())
            {
                return Failure("validate dirty DIB destination", ERROR_ARITHMETIC_OVERFLOW);
            }
            const int copiedLines = callDib([&] {
                return SetDIBitsToDevice(
                    deviceContext, static_cast<int>(destinationX), static_cast<int>(destinationY),
                    static_cast<DWORD>(dirty.width), static_cast<DWORD>(dirty.height),
                    dirty.x, 0, 0, static_cast<UINT>(dirty.height), rgbaPixels + offset,
                    reinterpret_cast<const BITMAPINFO*>(&dirtyInfo), DIB_RGB_COLORS);
            });
            return DibResult("SetDIBitsToDevice(dirty band)", copiedLines);
        }

        if (plan.path == GdiBlitPath::NativeFull)
        {
            const int copiedLines = callDib([&] {
                return SetDIBitsToDevice(
                    deviceContext, plan.destination.x, plan.destination.y,
                    static_cast<DWORD>(sourceWidth), static_cast<DWORD>(sourceHeight),
                    0, 0, 0, static_cast<UINT>(sourceHeight), rgbaPixels,
                    reinterpret_cast<const BITMAPINFO*>(&bitmapInfo), DIB_RGB_COLORS);
            });
            return DibResult("SetDIBitsToDevice(full frame)", copiedLines);
        }

        SetLastError(ERROR_SUCCESS);
        const int oldStretchMode = SetStretchBltMode(deviceContext, stretchMode);
        if (oldStretchMode == 0)
            return Failure("SetStretchBltMode", GetLastError());
        if (stretchMode == HALFTONE)
        {
            SetLastError(ERROR_SUCCESS);
            if (!SetBrushOrgEx(deviceContext, 0, 0, nullptr))
            {
                const DWORD error = GetLastError();
                (void)SetStretchBltMode(deviceContext, oldStretchMode);
                return Failure("SetBrushOrgEx(HALFTONE)", error);
            }
        }
        const int copiedLines = callDib([&] {
            return StretchDIBits(
                deviceContext,
                plan.destination.x, plan.destination.y,
                plan.destination.width, plan.destination.height,
                0, 0, sourceWidth, sourceHeight, rgbaPixels,
                reinterpret_cast<const BITMAPINFO*>(&bitmapInfo), DIB_RGB_COLORS, SRCCOPY);
        });
        const DWORD copyError = GetLastError();
        SetLastError(ERROR_SUCCESS);
        if (SetStretchBltMode(deviceContext, oldStretchMode) == 0)
            return Failure("SetStretchBltMode(restore)", GetLastError());
        if (copiedLines == 0 || copiedLines == static_cast<int>(GDI_ERROR))
            return Failure("StretchDIBits", copyError);
        return {true, copiedLines, 0, "StretchDIBits"};
    }
}
