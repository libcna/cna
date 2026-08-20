#pragma once

// plans/plan_runtimerenderer.md phase P4 (design decisions 1, 5, 6, 7): the public API for choosing which
// compiled-in graphics renderer CNA will use.
//
// Compile-time selection (-DCNA_GRAPHICS_RENDERER=<X>) remains the default and recommended mode.
// This API does not replace it: in a single-renderer build it validates a request against the one
// renderer that exists, which is still worth doing -- a game asking for VULKAN in an OPENGLES3
// build should be told so, not silently handed something else.
//
// Why this lives in the core module rather than alongside the registry in graphics: it holds only
// POLICY -- what was asked for, whether the choice is still open, what actually happened. It knows
// nothing about descriptors or SDL. The graphics module publishes the compiled-in set into it and
// reads the decision back out.

#include "CNA/CNAHelper.hpp"
#include "CNA/GraphicsRendererFallbackRecord.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace CNA
{
    /**
     * @brief CNAEXT. Chooses which compiled-in graphics renderer CNA will use.
     *
     * Not part of the XNA 4.0 API -- XNA had exactly one renderer and no notion of choosing between
     * several, so this whole surface is a CNA extension.
     *
     * Process-wide, and deliberately so: the choice has to be made before the first GraphicsDevice
     * is constructed, which is before a game has anywhere natural to put per-instance state. All of
     * it must be called before any graphics thread starts (see IsLatched()).
     */
    class CNAEXT GraphicsRendererSelection
    {
    public:
        /**
         * @brief Requests a specific renderer.
         *
         * @param type The renderer to use.
         * @throws System::InvalidOperationException if the selection is already latched, or if
         *         @p type is not compiled into this build and no fallback chain was configured.
         */
        static void SetPreferred(GraphicsRendererType type);

        /**
         * @brief Requests a specific renderer by name.
         *
         * Accepts exactly the CNA_GRAPHICS_RENDERER spellings, case-insensitively.
         *
         * @param name The renderer name, e.g. "VULKAN".
         * @throws System::ArgumentException if @p name is not a public renderer identity at all.
         * @throws System::InvalidOperationException under the same conditions as the overload above.
         */
        static void SetPreferred(std::string_view name);

        /**
         * @brief The renderer CNA will attempt first.
         *
         * Does not latch: asking what is currently selected must never be the thing that freezes
         * the selection.
         *
         * Resolution order, highest first: an explicit SetPreferred() call, the
         * CNA_GRAPHICS_RENDERER environment variable, this build's compile-time default.
         *
         * @return The currently selected renderer identity.
         */
        [[nodiscard]] static GraphicsRendererType GetSelected();

        /**
         * @brief Whether the selection can still be changed.
         *
         * Becomes true when the first GraphicsDevice begins construction. It forbids CHANGING the
         * selection, not creating a renderer again: GraphicsDevice::Reset() and its multisample
         * reconstruction path legitimately rebuild the same renderer on a live device.
         *
         * @return true once CNA has begun using the selected renderer.
         */
        [[nodiscard]] static bool IsLatched();

        /**
         * @brief The renderer identities compiled into this build.
         *
         * Exactly one entry in a single-renderer build. Published by the graphics module; empty
         * only if queried before that module has been linked in, which cannot happen in a real
         * program.
         *
         * @return A span over statically stored identities.
         */
        [[nodiscard]] static std::span<const GraphicsRendererType> GetAvailable();

        /**
         * @brief Whether an identity is compiled into this build.
         *
         * @param type The identity to test.
         * @return true when a renderer serving @p type is linked in.
         */
        [[nodiscard]] static bool IsAvailable(GraphicsRendererType type);

        /**
         * @brief The renderer that was actually created.
         *
         * Equals GetSelected() unless a configured fallback chain substituted another one.
         *
         * @return The active renderer identity.
         * @throws System::InvalidOperationException if nothing has been created yet -- before the
         *         latch there is no honest answer to give.
         */
        [[nodiscard]] static GraphicsRendererType GetActive();

        // --- Fallback (design decisions 6 and 7) -------------------------------------------
        //
        // OFF by default. A renderer that is unavailable or fails to initialize is an ERROR, not an
        // invitation to substitute something else: a game that asked for Vulkan and silently got a
        // CPU rasterizer is a worse outcome than a clear failure.

        /**
         * @brief Enables fallback and defines the ordered chain tried after the preferred renderer.
         *
         * @param chain Renderers to try, in order, if the preferred one cannot be used. Identities
         *        not compiled into this build are permitted here and are simply skipped (and
         *        recorded), so one chain can serve several build configurations.
         * @throws System::InvalidOperationException if the selection is already latched.
         */
        static void SetFallbackChain(std::span<const GraphicsRendererType> chain);

        /**
         * @brief Enables fallback across every compiled-in renderer.
         *
         * The order is derived from CNA::GraphicsBackendMaturity and CNA::GraphicsBackendCategory
         * rather than a ranking invented here: mature GPU renderers first, CPU renderers next,
         * STUB last.
         *
         * @param enabled Whether to enable automatic fallback.
         * @throws System::InvalidOperationException if the selection is already latched.
         */
        static void EnableAutomaticFallback(bool enabled);

        /**
         * @brief Whether any fallback is configured.
         *
         * @return true if SetFallbackChain() or EnableAutomaticFallback(true) was called.
         */
        [[nodiscard]] static bool IsFallbackEnabled();

        /**
         * @brief The renderers tried and rejected before the active one, in the order tried.
         *
         * Empty when the preferred renderer was created on the first attempt -- which is the
         * overwhelmingly common case, and the only possible one while fallback stays disabled.
         *
         * @return A span over the recorded attempts.
         */
        [[nodiscard]] static std::span<const GraphicsRendererFallbackRecord> GetFallbackHistory();

        /**
         * @brief CNAEXT, test-only. Restores the pristine state.
         *
         * Selection is process-wide and latching is one-way, which is correct for a game and
         * useless for a test suite that needs to exercise both sides of the latch. Not part of the
         * supported API: a game calling this would be re-opening a decision the rest of CNA has
         * already acted on.
         */
        static void ResetForTestingEXT();

    private:
        friend class GraphicsRendererSelectionAccessEXT;
    };

    /**
     * @brief CNAEXT, internal. The graphics module's side of the selection handshake.
     *
     * Separated from the public class so the two directions are not confusable: games call
     * GraphicsRendererSelection, and the graphics module calls this.
     */
    class CNAEXT GraphicsRendererSelectionAccessEXT
    {
    public:
        /**
         * @brief Publishes the compiled-in renderer set and this build's default.
         *
         * Called by the graphics module before the first selection query. Idempotent.
         *
         * @param available The identities linked into this build; must not be empty.
         * @param defaultType The identity CNA_GRAPHICS_RENDERER named; must appear in @p available.
         */
        static void PublishAvailable(std::span<const GraphicsRendererType> available,
                                      GraphicsRendererType defaultType);

        /**
         * @brief Freezes the selection and records what was actually created.
         *
         * @param active The renderer that was created.
         */
        static void Latch(GraphicsRendererType active);

        /**
         * @brief Records a renderer that was tried and rejected.
         *
         * @param record What was tried, why it was rejected, and the diagnostic detail.
         */
        static void RecordFallback(const GraphicsRendererFallbackRecord& record);

        /**
         * @brief The ordered list of renderers to attempt, preferred first.
         *
         * One entry when fallback is disabled. Entries are not filtered against the compiled-in
         * set -- the caller records a NotCompiledIn rejection for those, so the history explains
         * every step rather than silently omitting some.
         *
         * @return The attempt order.
         */
        [[nodiscard]] static std::span<const GraphicsRendererType> GetAttemptOrder();
    };
}
