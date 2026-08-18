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

#include <string>

#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Logger.hpp"

namespace
{
    // Reads Module['cnaPreferredRenderer'] into a caller-owned buffer.
    //
    // This used to be `emscripten::val::module_property(...)`, which is embind -- and embind's
    // support library never reached any link line in this project, so EVERY Emscripten executable
    // linking cna_core failed with six undefined `_emval_*` symbols. That is a link-time failure,
    // and RTR-P10-23's evidence was that `cna_core` COMPILES and both exports are present in the
    // archive, which is exactly the check that cannot see it. Found by a real bundle link during
    // the PIXIJS platform migration.
    //
    // Fixed by dropping embind rather than by adding `-lembind`, for two reasons: EM_JS is how
    // every other JS interop in this project is written (the Canvas, HTML DOM, SVG DOM and PixiJS
    // renderers all use it), and pulling embind's runtime into every consumer's bundle to read one
    // string property is a real cost paid by pages that never set it.
    //
    // The copy is written by hand rather than through `stringToUTF8`, so it cannot depend on a JS
    // runtime helper surviving link-time dead-code elimination. Renderer identities are ASCII by
    // construction ("WEBGL2", "SVG_DOM", ...), so a code unit outside ASCII means the value is not
    // a renderer name at all and the read reports "absent" rather than truncating it into
    // something that might parse.
    EM_JS(int, CnaReadModulePreferredRenderer, (char* buffer, int capacity), {
        const value = Module['cnaPreferredRenderer'];
        if (value === undefined || value === null) return 0;
        const text = String(value);
        if (text.length === 0 || text.length > capacity - 1) return 0;
        for (let i = 0; i < text.length; ++i) {
            const code = text.charCodeAt(i);
            if (code === 0 || code > 127) return 0;
            HEAPU8[buffer + i] = code;
        }
        HEAPU8[buffer + text.length] = 0;
        return text.length;
    });

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
        // Comfortably longer than the longest identity spelling ("SDL_RENDERER", 12), and the JS
        // side refuses anything that does not fit rather than truncating it.
        static char buffer[64];
        static bool read = false;
        if (!read)
        {
            read = true;
            if (CnaReadModulePreferredRenderer(buffer, static_cast<int>(sizeof(buffer))) <= 0)
                buffer[0] = '\0';
        }
        return buffer[0] == '\0' ? nullptr : buffer;
    }
}

#endif  // __EMSCRIPTEN__
