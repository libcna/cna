// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/HtmlDom/HtmlDomRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomState.hpp"

#include <cstdint>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// REMED-GFX-127: synchronous readback of one target's own context. Returns 0 for an unknown id so
// the caller reports the failure rather than handing the shared layer a buffer it never filled.
EM_JS(int, CNA_HtmlDom_ReadTargetPixels, (int id, int x, int y, int w, int h, uint8_t* outPixels), {
    const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
    if (!entry || !entry.ctx) return 0;
    const imageData = entry.ctx.getImageData(x, y, w, h);
    HEAPU8.set(imageData.data, outPixels);
    return 1;
});
#endif

namespace CNA::Internal::Backends::HtmlDom
{
    HtmlDomRenderTargetBackend::HtmlDomRenderTargetBackend(int w, int h)
        : texture_(w, h)
    {
    }

    HtmlDomRenderTargetBackend::~HtmlDomRenderTargetBackend()
    {
        // A destroyed target must never leave the draw path pointing at its canvas.
        if (GetBoundRenderTargetIdEXT() == texture_.GetCanvasId())
            SetBoundRenderTargetIdEXT(0);
    }

    void HtmlDomRenderTargetBackend::BindAsRenderTarget()
    {
        SetBoundRenderTargetIdEXT(texture_.GetCanvasId());
    }

    void HtmlDomRenderTargetBackend::UnbindAsRenderTarget()
    {
        // Binding is absolute and idempotent -- it always points the draw path straight at this
        // target -- so switching from here to another target, or back to the DOM backbuffer, needs
        // no cleanup of its own first. Only an explicit unbind of THIS target does anything.
        if (GetBoundRenderTargetIdEXT() != texture_.GetCanvasId()) return;
        SetBoundRenderTargetIdEXT(0);
    }

    bool HtmlDomRenderTargetBackend::GetData(int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level > 0)
            throw System::NotSupportedException(
                "HtmlDomRenderTargetBackend::GetData: this backend has no mip chain; level " +
                std::to_string(level) + " was requested.");

        const int targetWidth = texture_.GetWidth();
        const int targetHeight = texture_.GetHeight();
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(targetWidth) ||
            bottom > static_cast<std::int64_t>(targetHeight))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(targetWidth) + "x" +
                    std::to_string(targetHeight) + " render target.");

        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");
        if (data == nullptr) return false;

#if defined(__EMSCRIPTEN__)
        return CNA_HtmlDom_ReadTargetPixels(texture_.GetCanvasId(), x, y, w, h,
                                            static_cast<std::uint8_t*>(data)) != 0;
#else
        // No browser means no canvas and no pixels. REMED-GFX-127: say so, rather than letting the
        // shared layer answer with its own zeroed scratch buffer.
        return false;
#endif
    }
}
