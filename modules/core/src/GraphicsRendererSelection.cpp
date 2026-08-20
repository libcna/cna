// plans/plan_runtimerenderer.md phase P4/P5: renderer selection policy.
//
// Holds no graphics state of any kind -- what was asked for, whether the choice is still open, and
// what actually happened. The graphics module publishes the compiled-in set in and reads the
// decision back out (GraphicsRendererSelectionAccessEXT).

#include "CNA/GraphicsRendererSelection.hpp"

#include "CNA/GraphicsBackendCategory.hpp"
#include "CNA/GraphicsBackendMaturity.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/Logger.hpp"

#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
// Defined in GraphicsRendererSelectionEmscripten.cpp, which is the only place emscripten/val.h is
// included -- this translation unit stays free of the browser API.
extern "C" const char* cna_read_module_preferred_renderer();
#endif

namespace CNA
{
    namespace
    {
        struct SelectionState
        {
            std::vector<GraphicsRendererType> available;
            GraphicsRendererType defaultType = GraphicsRendererType::Stub;
            bool published = false;

            std::optional<GraphicsRendererType> preferred;
            std::optional<GraphicsRendererType> environmentPreferred;
            bool environmentConsulted = false;

            bool fallbackEnabled = false;
            std::vector<GraphicsRendererType> fallbackChain;

            std::optional<GraphicsRendererType> active;
            std::vector<GraphicsRendererFallbackRecord> history;

            /// Atomic rather than a plain bool so that a program violating the documented
            /// "select before any graphics thread starts" rule is detected rather than racing.
            std::atomic<bool> latched{false};

            /// Recomputed whenever the inputs change, so GetAttemptOrder() can hand out a stable span.
            std::vector<GraphicsRendererType> attemptOrder;
        };

        SelectionState& State()
        {
            static SelectionState state;
            return state;
        }

        [[noreturn]] void ThrowLatched(const char* what)
        {
            const SelectionState& state = State();
            std::string active = "<none>";
            if (state.active.has_value())
                active = std::string(getGraphicsRendererName(*state.active));

            throw System::InvalidOperationException(
                std::string("CNA::GraphicsRendererSelection::") + what +
                " was called after CNA had already begun using the " + active +
                " renderer. The graphics renderer must be selected before the first GraphicsDevice "
                "is constructed; it cannot be changed afterwards.");
        }

        std::string AvailableList()
        {
            std::string list;
            for (const GraphicsRendererType type : State().available)
            {
                if (!list.empty())
                    list += ", ";
                list += getGraphicsRendererName(type);
            }
            return list.empty() ? std::string("<none>") : list;
        }

        [[nodiscard]] bool Contains(const std::vector<GraphicsRendererType>& types,
                                     GraphicsRendererType type)
        {
            return std::find(types.begin(), types.end(), type) != types.end();
        }

        /// Design decision 6: an identity that is not compiled in is an error, NOT a silent
        /// downgrade -- unless the caller has explicitly asked for fallback, in which case the
        /// chain gets its chance and the rejection is recorded rather than thrown.
        void RejectIfNotCompiledIn(GraphicsRendererType type)
        {
            SelectionState& state = State();
            if (!state.published || Contains(state.available, type) || state.fallbackEnabled)
                return;

            throw System::InvalidOperationException(
                std::string("CNA::GraphicsRendererSelection: the ") +
                std::string(getGraphicsRendererName(type)) +
                " renderer is not compiled into this build. Available: " + AvailableList() +
                ". Rebuild with -DCNA_GRAPHICS_RENDERER=" +
                std::string(getGraphicsRendererName(type)) +
                ", or configure a fallback chain with SetFallbackChain().");
        }

        /// Design decision 1: the environment variable is a convenience below an explicit call, and
        /// follows the CNA_BGFX_RENDERER / CNA_DILIGENT_DEVICE precedent. A value naming a renderer
        /// that is not compiled in throws, consistent with SetPreferred() -- silently ignoring it
        /// would leave the user believing they had switched renderer when they had not.
        void ConsultEnvironmentOnce()
        {
            SelectionState& state = State();
            if (state.environmentConsulted)
                return;
            state.environmentConsulted = true;

            const char* raw = std::getenv("CNA_GRAPHICS_RENDERER");

#ifdef __EMSCRIPTEN__
            // plans/plan_runtimerenderer.md RTR-P10-23: a browser has no environment to set, so the page
            // supplies the same choice through `Module.cnaPreferredRenderer`. It is consulted HERE,
            // at the environment variable's precedence, rather than by calling SetPreferred() from
            // JS glue -- a page property must not outrank an explicit SetPreferred() call in the
            // program itself. An actual environment variable, if emscripten's shell provides one,
            // still wins over the property.
            if (raw == nullptr || *raw == '\0')
                raw = cna_read_module_preferred_renderer();
#endif

            if (raw == nullptr || *raw == '\0')
                return;

            GraphicsRendererType parsed{};
            if (!tryParseGraphicsRendererName(raw, parsed))
            {
                throw System::InvalidOperationException(
                    std::string("CNA_GRAPHICS_RENDERER=\"") + raw +
                    "\" does not name any CNA graphics renderer.");
            }

            RejectIfNotCompiledIn(parsed);
            state.environmentPreferred = parsed;
        }

        void RebuildAttemptOrder()
        {
            SelectionState& state = State();
            state.attemptOrder.clear();
            state.attemptOrder.push_back(GraphicsRendererSelection::GetSelected());

            if (!state.fallbackEnabled)
                return;

            for (const GraphicsRendererType candidate : state.fallbackChain)
            {
                if (!Contains(state.attemptOrder, candidate))
                    state.attemptOrder.push_back(candidate);
            }
        }

        /// Design decision 7: the automatic chain is DERIVED from the maturity/category enums this
        /// project already maintains rather than from a ranking invented here, so it cannot drift
        /// away from how the renderers are actually described elsewhere.
        std::vector<GraphicsRendererType> BuildAutomaticChain()
        {
            const SelectionState& state = State();

            std::vector<GraphicsRendererType> chain(state.available.begin(), state.available.end());
            std::stable_sort(chain.begin(), chain.end(),
                [](GraphicsRendererType a, GraphicsRendererType b) {
                    const auto rank = [](GraphicsRendererType type) {
                        // Lower sorts earlier. Stub is last on purpose: it renders nothing, so it is
                        // a last resort rather than a fallback anyone wants silently.
                        if (type == GraphicsRendererType::Stub)
                            return 100;
                        const int maturityRank = static_cast<int>(getGraphicsBackendMaturity(type));
                        const int categoryRank =
                            getGraphicsBackendCategory(type) == GraphicsBackendCategory::Software ? 10 : 0;
                        return categoryRank + maturityRank;
                    };
                    return rank(a) < rank(b);
                });
            return chain;
        }
    }

    void GraphicsRendererSelection::SetPreferred(GraphicsRendererType type)
    {
        if (IsLatched())
            ThrowLatched("SetPreferred");

        RejectIfNotCompiledIn(type);
        State().preferred = type;
        RebuildAttemptOrder();
    }

    void GraphicsRendererSelection::SetPreferred(std::string_view name)
    {
        GraphicsRendererType parsed{};
        if (!tryParseGraphicsRendererName(name, parsed))
        {
            throw System::ArgumentException(
                std::string("\"") + std::string(name) +
                "\" does not name any CNA graphics renderer.",
                "name");
        }
        SetPreferred(parsed);
    }

    GraphicsRendererType GraphicsRendererSelection::GetSelected()
    {
        SelectionState& state = State();
        if (state.preferred.has_value())
            return *state.preferred;

        ConsultEnvironmentOnce();
        if (state.environmentPreferred.has_value())
            return *state.environmentPreferred;

        return state.defaultType;
    }

    bool GraphicsRendererSelection::IsLatched()
    {
        return State().latched.load(std::memory_order_acquire);
    }

    std::span<const GraphicsRendererType> GraphicsRendererSelection::GetAvailable()
    {
        return State().available;
    }

    bool GraphicsRendererSelection::IsAvailable(GraphicsRendererType type)
    {
        return Contains(State().available, type);
    }

    GraphicsRendererType GraphicsRendererSelection::GetActive()
    {
        const SelectionState& state = State();
        if (!state.active.has_value())
        {
            throw System::InvalidOperationException(
                "CNA::GraphicsRendererSelection::GetActive() was called before any graphics "
                "renderer had been created. Use GetSelected() for the renderer CNA will attempt.");
        }
        return *state.active;
    }

    void GraphicsRendererSelection::SetFallbackChain(std::span<const GraphicsRendererType> chain)
    {
        if (IsLatched())
            ThrowLatched("SetFallbackChain");

        SelectionState& state = State();
        state.fallbackChain.assign(chain.begin(), chain.end());
        state.fallbackEnabled = true;
        RebuildAttemptOrder();
    }

    void GraphicsRendererSelection::EnableAutomaticFallback(bool enabled)
    {
        if (IsLatched())
            ThrowLatched("EnableAutomaticFallback");

        SelectionState& state = State();
        state.fallbackEnabled = enabled;
        state.fallbackChain = enabled ? BuildAutomaticChain()
                                      : std::vector<GraphicsRendererType>{};
        RebuildAttemptOrder();
    }

    bool GraphicsRendererSelection::IsFallbackEnabled()
    {
        return State().fallbackEnabled;
    }

    std::span<const GraphicsRendererFallbackRecord> GraphicsRendererSelection::GetFallbackHistory()
    {
        return State().history;
    }

    void GraphicsRendererSelection::ResetForTestingEXT()
    {
        SelectionState& state = State();
        state.preferred.reset();
        state.environmentPreferred.reset();
        state.environmentConsulted = false;
        state.fallbackEnabled = false;
        state.fallbackChain.clear();
        state.active.reset();
        state.history.clear();
        state.latched.store(false, std::memory_order_release);
        RebuildAttemptOrder();
    }

    void GraphicsRendererSelectionAccessEXT::PublishAvailable(
        std::span<const GraphicsRendererType> available, GraphicsRendererType defaultType)
    {
        SelectionState& state = State();
        state.available.assign(available.begin(), available.end());
        state.defaultType = defaultType;
        state.published = true;
        RebuildAttemptOrder();
    }

    void GraphicsRendererSelectionAccessEXT::Latch(GraphicsRendererType active)
    {
        SelectionState& state = State();
        state.active = active;
        state.latched.store(true, std::memory_order_release);
    }

    void GraphicsRendererSelectionAccessEXT::RecordFallback(
        const GraphicsRendererFallbackRecord& record)
    {
        State().history.push_back(record);

        // Design decision 7: fallback is never silent. A game that ends up on a different renderer
        // than it asked for must be able to see that in its own log, not only by querying afterwards.
        Logger::Warn(
            std::string("graphics renderer ") + std::string(getGraphicsRendererName(record.type)) +
            " was not used (" + std::string(getGraphicsRendererFallbackReasonName(record.reason)) +
            "): " + record.message,
            LogCategory::RENDER);
    }

    std::span<const GraphicsRendererType> GraphicsRendererSelectionAccessEXT::GetAttemptOrder()
    {
        SelectionState& state = State();
        if (state.attemptOrder.empty())
            RebuildAttemptOrder();
        return state.attemptOrder;
    }
}
