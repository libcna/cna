#pragma once

#include <string>

namespace CNA::Internal::Backends::Skia
{
    /**
     * SKIA-155: result of translating one fragment source through the deliberately restricted
     * GLSL-to-SkSL translator. See docs/skia-glsl-to-sksl-translator-contract.md for the accepted
     * grammar and the full rationale.
     */
    struct GlslToSkslTranslationResultEXT final
    {
        bool success = false;
        /** Translated SkSL 100 source, valid only when `success` is true. */
        std::string sksl;
        /** Human-readable diagnostic naming the rejected construct and citing its 1-based
         * `line:column`, valid only when `success` is false. */
        std::string errorMessage;
    };

    /**
     * Translates @p glslSource (a complete GLSL ES 3.00 fragment shader, `#version` line optional)
     * into SkSL 100 runtime-shader source, accepting only the narrow grammar
     * docs/skia-glsl-to-sksl-translator-contract.md fixes: uniform scalar/vector/matrix/sampler2D
     * declarations, at most one `in vec2` UV varying, at most one `out vec4` colour, and a single
     * `void main()` with a straight-line, branch-free-of-early-exit body. Every other construct
     * (discard, gl_FragDepth, a second `out`/`in`, derivatives, precision qualifiers, cube/volume
     * sampling, the `cnaSampleUV` backend macro, helper function definitions, preprocessor
     * directives other than `#version`) is rejected with a source location rather than silently
     * mistranslated.
     */
    [[nodiscard]] GlslToSkslTranslationResultEXT TranslateGlslToSkslEXT(const std::string& glslSource);
} // namespace CNA::Internal::Backends::Skia
