// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstddef>
#include <cstdint>

namespace CNA::Internal::Backends::Gdi
{
    /** @brief Integer rectangle used by the pure GDI presentation planner. */
    struct GdiPresentationRect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    enum class GdiBlitPath
    {
        None,
        NativeFull,
        NativeDirty,
        Stretch
    };

    /** @brief Complete, side-effect-free decision for one GDI presentation attempt. */
    struct GdiPresentationPlan
    {
        GdiBlitPath path = GdiBlitPath::None;
        GdiPresentationRect destination{};
        GdiPresentationRect source{};
        bool clearClient = false;
    };

    /** @brief Result and telemetry from the small DIB-to-HDC transaction. */
    struct GdiBlitResult
    {
        bool success = false;
        int copiedLines = 0;
        std::uint32_t win32Error = 0;
        const char* operation = "none";
    };

    /** @brief Last selected plan and native result, exposed only as internal test telemetry. */
    struct GdiPresentationTelemetry
    {
        bool valid = false;
        GdiPresentationPlan plan{};
        int stretchMode = 0;
        GdiBlitResult result{};
    };

    /** Calculates destination geometry for every CNA presentation mode. */
    [[nodiscard]] GdiPresentationRect CalculateGdiPresentationDestination(
        CnaPresentationMode presentationMode, int clientWidth, int clientHeight,
        int sourceWidth, int sourceHeight);

    /** Selects no-op, full native, dirty native, or scaled presentation. */
    [[nodiscard]] GdiPresentationPlan BuildGdiPresentationPlan(
        CnaPresentationMode presentationMode,
        int clientWidth, int clientHeight, int sourceWidth, int sourceHeight,
        bool dirtyPresentationRequested, bool clientNeedsRepair,
        bool backbufferFullyDirty, bool dirtyValid,
        const GdiPresentationRect& dirtyRectangle);

    /** Pure inverse presentation-coordinate transform. */
    [[nodiscard]] bool MapGdiWindowToLogical(
        CnaPresentationMode presentationMode,
        int clientWidth, int clientHeight, int logicalWidth, int logicalHeight,
        float windowX, float windowY, float& logicalX, float& logicalY);

    /** Pure forward presentation-coordinate transform. */
    [[nodiscard]] bool MapGdiLogicalToWindow(
        CnaPresentationMode presentationMode,
        int clientWidth, int clientHeight, int logicalWidth, int logicalHeight,
        float logicalX, float logicalY, float& windowX, float& windowY);

    /**
     * Executes one already-planned RGBA8 DIB transfer into a Win32 HDC.
     *
     * @param deviceContext Native HDC stored as void* to keep windows.h out of this header.
     * @param forceZeroDibResult Test injection: bypass the DIB call and report zero copied lines.
     */
    [[nodiscard]] GdiBlitResult BlitGdiRgbaToDeviceContext(
        void* deviceContext, int clientWidth, int clientHeight,
        int sourceWidth, int sourceHeight,
        const std::uint8_t* rgbaPixels, std::size_t rgbaByteCount,
        const GdiPresentationPlan& plan, int stretchMode,
        bool forceZeroDibResult = false);
}
