// SPDX-License-Identifier: MS-PL
//
// plan_runtimerenderer.md RTR-P10-23: the JS-side selection surface for a wasm bundle that holds
// several renderers.
//
// A browser build is the case where runtime selection earns the most: one bundle is downloaded and
// cached, and the page decides which renderer it wants before the program starts -- WEBGL2 where
// the GPU allows it, CANVAS or SVG_DOM where it does not, without shipping four bundles.
//
// The C++ contract is unchanged and this file adds none of its own: it is a thin translation of
// two JS-shaped ways of asking into CNA::GraphicsRendererSelection::SetPreferred(). Both go through
// the same latch, so a page that asks after the first GraphicsDevice exists is refused exactly as
// C++ code would be.
//
// Nothing here compiles off Emscripten.

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/val.h>

#include <string>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Logger.hpp"

namespace
{
    /// Applies @p name, turning any C++ exception into a false return.
    ///
    /// A browser has no useful place for an exception thrown across the wasm boundary, and the two
    /// failure modes here are both things a page can legitimately hit: an unknown name, and a call
    /// that arrives too late. Reporting them as false plus a log line keeps the page in control
    /// instead of aborting the module.
    bool ApplyPreferred(const std::string& name)
    {
        try
        {
            CNA::GraphicsRendererSelection::SetPreferred(name);
            return true;
        }
        catch (const std::exception& e)
        {
            CNA::Logger::Warn(
                std::string("JS asked for graphics renderer '") + name + "', which was refused: " +
                    e.what(),
                CNA::LogCategory::RENDER);
            return false;
        }
    }
}

extern "C"
{
    /**
     * @brief Requests a renderer by name from JavaScript, before the program starts.
     *
     * Callable as `Module._cna_set_preferred_renderer(...)` after string marshalling, but the
     * intended surface is `Module.cnaPreferredRenderer` (see below) -- this export exists so a page
     * that already drives the module directly does not have to go through a property.
     *
     * @param name A public renderer identity, in the CNA_GRAPHICS_RENDERER spelling,
     *             case-insensitive (e.g. "WEBGL2", "CANVAS", "SVG_DOM").
     * @return 1 when the renderer was selected, 0 when the name is not a renderer identity, is not
     *         compiled into this bundle with no fallback chain configured, or the selection has
     *         already latched.
     */
    EMSCRIPTEN_KEEPALIVE int cna_set_preferred_renderer(const char* name)
    {
        if (name == nullptr)
            return 0;
        return ApplyPreferred(std::string(name)) ? 1 : 0;
    }

    /**
     * @brief Reads `Module.cnaPreferredRenderer`, or null when the page did not set one.
     *
     * A page only has to write the property in its `Module` object:
     *
     * @code{.js}
     * var Module = { cnaPreferredRenderer: "CANVAS" };
     * @endcode
     *
     * This only READS. GraphicsRendererSelection consults it at the same point, and with the same
     * precedence, as the CNA_GRAPHICS_RENDERER environment variable on a native build -- below an
     * explicit SetPreferred() call, above the compile-time default. Applying it here instead would
     * make a page property indistinguishable from an explicit C++ call and quietly outrank one.
     *
     * @return The property's value, or nullptr when absent or empty. The storage is static and
     *         stays valid for the process; the value is read once.
     */
    EMSCRIPTEN_KEEPALIVE const char* cna_read_module_preferred_renderer()
    {
        static std::string cached;
        static bool read = false;
        if (!read)
        {
            read = true;
            const emscripten::val property =
                emscripten::val::module_property("cnaPreferredRenderer");
            if (!property.isUndefined() && !property.isNull())
                cached = property.as<std::string>();
        }
        return cached.empty() ? nullptr : cached.c_str();
    }
}

#endif  // __EMSCRIPTEN__
