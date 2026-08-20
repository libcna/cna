#pragma once

// plans/plan_runtimerenderer.md design decision 3: the set of renderer families linked into this build is
// an EXPLICIT, generated table -- not static-initializer self-registration.
//
// Self-registration is the obvious design and the wrong one here: inside a static archive the
// linker discards any object file nothing references, so a self-registering renderer translation
// unit silently vanishes from the binary unless the whole archive is force-linked
// (--whole-archive / OBJECT libraries). That would also have to coexist with the declared
// cna_graphics_core <-> renderer archive cycles this project already relies on
// (modules/renderers/CMakeLists.txt). CMake generates CnaRendererRegistry.generated.cpp instead:
// deterministic, no static-init ordering, no linker flags.
//
// In a single-renderer build -- which stays the default and recommended mode (design decision 1) --
// the table has exactly one entry and this indirection costs one function-pointer call per
// GraphicsDevice construction, and none per frame.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace CNA::Internal::Renderers
{
    /**
     * @brief The renderer families compiled into this build.
     *
     * Every accessor is a pure query over a table fixed at link time; none of them creates a
     * renderer, touches SDL, or has any side effect.
     */
    namespace GraphicsRendererRegistry
    {
        /**
         * @brief All descriptors compiled into this build, in the order CMake declared them.
         *
         * Never empty: a build with no renderer fails to configure
         * (cmake/RendererSelection.cmake). In a single-renderer build the span has size 1.
         *
         * @return A span over statically stored descriptors, valid for the lifetime of the program.
         */
        [[nodiscard]] std::span<const GraphicsRendererDescriptor> All();

        /**
         * @brief Number of renderer families compiled into this build.
         *
         * @return All().size().
         */
        [[nodiscard]] std::size_t Count();

        /**
         * @brief Finds the descriptor serving a public renderer identity.
         *
         * @param type The identity to look up.
         * @return The descriptor, or nullptr when that identity is not compiled into this build.
         */
        [[nodiscard]] const GraphicsRendererDescriptor* Find(GraphicsRendererType type);

        /**
         * @brief Finds the descriptor whose name matches, case-insensitively.
         *
         * Accepts exactly the CNA_GRAPHICS_RENDERER spellings ("SDL_RENDERER", "OPENGLES3", ...).
         * Case-insensitive because these names reach CNA through environment variables and command
         * lines, which are typed by hand.
         *
         * @param name The renderer name to look up.
         * @return The descriptor, or nullptr when no compiled-in renderer has that name.
         */
        [[nodiscard]] const GraphicsRendererDescriptor* Find(std::string_view name);

        /**
         * @brief The descriptor of the renderer this build treats as its default.
         *
         * In a single-renderer build this is the only entry. In a multi-renderer build it is the
         * one named by CNA_GRAPHICS_RENDERER, which must be a member of CNA_GRAPHICS_RENDERERS.
         *
         * @return The default descriptor; never nullptr.
         */
        [[nodiscard]] const GraphicsRendererDescriptor& Default();
    }
}
