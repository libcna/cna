#pragma once

// plan_runtimerenderer.md design decision 7: fallback is opt-in, ordered, and ALWAYS reported.
// This is the reported part -- one record per renderer that was tried and rejected, so a game (or
// a bug report) can see exactly which renderers were attempted and why each was passed over,
// rather than discovering after the fact that it silently got a different renderer than it asked
// for.

#include "CNA/GraphicsRendererType.hpp"

#include <string>
#include <string_view>

namespace CNA
{
    /** @brief Why a renderer was passed over during fallback resolution. */
    enum class GraphicsRendererFallbackReason
    {
        /** @brief The identity is not compiled into this build at all. */
        NotCompiledIn,

        /** @brief The renderer's own availability probe reported it cannot run here. */
        ProbeUnavailable,

        /** @brief The renderer was attempted and its construction threw. */
        InitializationFailed,

        /**
         * @brief The renderer needs a different window kind and the window could not be recreated.
         *
         * Only ever produced when the SDL window was supplied by the caller through
         * PresentationParameters::DeviceWindowHandle -- CNA may not destroy a window it does not
         * own (plan_runtimerenderer.md design decision 8).
         */
        WindowKindConflict
    };

    /**
     * @brief Returns a short, stable, human-readable name for a fallback reason.
     *
     * Used in log lines and exception messages; the spelling is part of the diagnostic contract.
     *
     * @param reason The reason to name.
     * @return The reason's name, e.g. "ProbeUnavailable".
     */
    constexpr std::string_view getGraphicsRendererFallbackReasonName(
        GraphicsRendererFallbackReason reason)
    {
        switch (reason)
        {
            case GraphicsRendererFallbackReason::NotCompiledIn:        return "NotCompiledIn";
            case GraphicsRendererFallbackReason::ProbeUnavailable:     return "ProbeUnavailable";
            case GraphicsRendererFallbackReason::InitializationFailed: return "InitializationFailed";
            case GraphicsRendererFallbackReason::WindowKindConflict:   return "WindowKindConflict";
        }
        return "UNKNOWN";
    }

    /**
     * @brief One entry in the record of renderers tried and rejected before the active one.
     *
     * The history is empty when the preferred renderer was created on the first attempt, which is
     * the overwhelmingly common case and the only possible one while fallback stays disabled
     * (the default -- see plan_runtimerenderer.md design decision 6).
     */
    struct GraphicsRendererFallbackRecord
    {
        /** @brief The renderer identity that was tried and passed over. */
        GraphicsRendererType type = GraphicsRendererType::Stub;

        /** @brief Why it was passed over. */
        GraphicsRendererFallbackReason reason = GraphicsRendererFallbackReason::NotCompiledIn;

        /**
         * @brief Diagnostic detail.
         *
         * For GraphicsRendererFallbackReason::InitializationFailed this is the message of the
         * exception the renderer's construction threw, preserved verbatim. For the other reasons
         * it is a short explanatory sentence. Never empty.
         */
        std::string message;
    };
}
