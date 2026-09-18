#pragma once

#include <cstddef>
#include <string_view>

namespace CNA
{
    /**
     * @brief Identifies which CNA graphics renderer was selected for this build (compile-time choice, see CNA_GRAPHICS_RENDERER).
     *
     * The enumeration is dense and its ordinals are not a stable numeric contract: a retired
     * renderer's enumerator is removed, which moves the ordinals after it. The stable numbers are
     * the C ABI's CNA_GRAPHICS_RENDERER_* values, where a retired identity leaves a permanent gap
     * (docs/removed-renderers.md lists every retired name and value).
     */
    enum class GraphicsRendererType
    {
        /** @brief SDL's native 2D renderer (2D-only). */
        SdlRenderer,

        /** @brief OpenGL ES 2.0 (desktop/mobile, GLSL ES 1.00), internally implemented by EasyGL. */
        OpenGLES2,

        /** @brief OpenGL ES (desktop/mobile GLES 3.0), internally implemented by EasyGL. */
        OpenGLES3,

        /** @brief Desktop OpenGL 3.3 core profile, internally implemented by EasyGL. */
        OpenGL33,

        /** @brief WebGL 1 (Emscripten only, GLES 2.0), internally implemented by EasyGL. */
        WebGL1,

        /** @brief WebGL 2 (Emscripten only, GLES 3.0), internally implemented by EasyGL. */
        WebGL2,

        /** @brief Vulkan. */
        Vulkan,

        /** @brief WebGPU (experimental). */
        WebGPU,

        /** @brief Headless (no GPU/window). */
        Headless,

        /** @brief Software (CPU rasterizer). */
        Software,

        /** @brief Stub (no-op, renders nothing). */
        Stub,

        /** @brief Direct3D 11. */
        DirectX11,

        /** @brief Direct3D 12. */
        DirectX12,

        /** @brief Direct2D 1.1 (Windows, 2D-only). */
        Direct2D,

        /** @brief HTML Canvas 2D (Emscripten). */
        Canvas,

        /** @brief HTML DOM elements composited by CSS (Emscripten). */
        HtmlDom,

        /** @brief FreeDirect (DirectDraw via the ../free-direct sibling reimplementation). */
        FreeDirect,

        /** @brief Direct3D 9. */
        DirectX9,

        /** @brief SDL_GPU. */
        SdlGpu,

        /** @brief Real desktop OpenGL 4.x core profile. */
        OpenGL4,

        /** @brief Classic Win32 GDI with private CPU 2D rasterization. */
        Gdi,

        /** @brief Native Apple Metal. */
        Metal,

        /** @brief FNA3D (FNA-XNA/FNA3D), the XNA-shaped C graphics library FNA itself renders
         * through; it selects SDL_GPU, Direct3D 11 or OpenGL internally at runtime. */
        Fna3d,

        /** @brief SVG DOM (Emscripten only, 2D-only): SpriteBatch output as real SVG elements. */
        SvgDom,

        /** @brief PortableGL (rswinkle/PortableGL, CPU software OpenGL 3.x). */
        PortableGL
    };

    /**
     * @brief Returns the graphics renderer compiled into this build.
     *
     * Resolved entirely from the CNA_RENDERER_* compile definition cmake/RendererSelection.cmake
     * sets per renderer, so this is a compile-time constant -- usable in a constant expression
     * (e.g. static_assert(CNA::getCurrentGraphicsRendererType() == CNA::GraphicsRendererType::OpenGLES3)).
     *
     * The 5 GL-family public renderers (OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2) all share the
     * internal CNA_RENDERER_EASYGL identity (see plans/plan_glbackends.md) -- the CNA_GL_PROFILE_*
     * compile definition set alongside it distinguishes which of the 5 public names was selected.
     *
     * @return The active GraphicsRendererType, determined at compile time by CNA_GRAPHICS_RENDERER.
     */
    constexpr GraphicsRendererType getCurrentGraphicsRendererType()
    {
#if defined(CNA_RENDERER_SDL_RENDERER)
        return GraphicsRendererType::SdlRenderer;
#elif defined(CNA_RENDERER_EASYGL)
#if defined(CNA_GL_PROFILE_OPENGL33)
        return GraphicsRendererType::OpenGL33;
#elif defined(CNA_GL_PROFILE_WEBGL1)
        return GraphicsRendererType::WebGL1;
#elif defined(CNA_GL_PROFILE_WEBGL2)
        return GraphicsRendererType::WebGL2;
#elif defined(CNA_GL_PROFILE_OPENGLES2)
        return GraphicsRendererType::OpenGLES2;
#else // CNA_GL_PROFILE_OPENGLES3 (default within CNA_RENDERER_EASYGL)
        return GraphicsRendererType::OpenGLES3;
#endif
#elif defined(CNA_RENDERER_VULKAN)
        return GraphicsRendererType::Vulkan;
#elif defined(CNA_RENDERER_WEBGPU)
        return GraphicsRendererType::WebGPU;
#elif defined(CNA_RENDERER_HEADLESS)
        return GraphicsRendererType::Headless;
#elif defined(CNA_RENDERER_SOFTWARE)
        return GraphicsRendererType::Software;
#elif defined(CNA_RENDERER_STUB)
        return GraphicsRendererType::Stub;
#elif defined(CNA_RENDERER_DIRECTX11)
        return GraphicsRendererType::DirectX11;
#elif defined(CNA_RENDERER_DIRECTX12)
        return GraphicsRendererType::DirectX12;
#elif defined(CNA_RENDERER_DIRECT2D)
        return GraphicsRendererType::Direct2D;
#elif defined(CNA_RENDERER_CANVAS)
        return GraphicsRendererType::Canvas;
#elif defined(CNA_RENDERER_HTML_DOM)
        return GraphicsRendererType::HtmlDom;
#elif defined(CNA_RENDERER_FREEDIRECT)
        return GraphicsRendererType::FreeDirect;
#elif defined(CNA_RENDERER_DIRECTX9)
        return GraphicsRendererType::DirectX9;
#elif defined(CNA_RENDERER_SDL_GPU)
        return GraphicsRendererType::SdlGpu;
#elif defined(CNA_RENDERER_OPENGL4)
        return GraphicsRendererType::OpenGL4;
#elif defined(CNA_RENDERER_GDI)
        return GraphicsRendererType::Gdi;
#elif defined(CNA_RENDERER_METAL)
        return GraphicsRendererType::Metal;
#elif defined(CNA_RENDERER_FNA3D)
        return GraphicsRendererType::Fna3d;
#elif defined(CNA_RENDERER_SVG_DOM)
        return GraphicsRendererType::SvgDom;
#elif defined(CNA_RENDERER_PORTABLEGL)
        return GraphicsRendererType::PortableGL;
#else
#error "CNA: no CNA_RENDERER_* compile definition set -- graphics renderer selection (cmake/RendererSelection.cmake) is broken"
#endif
    }

    /**
     * @brief Returns the human-readable name of any graphics renderer identity.
     *
     * The returned view matches the CNA_GRAPHICS_RENDERER CMake option value exactly
     * (e.g. "OPENGLES3", "SDL_RENDERER", "DIRECTX9") and points at static storage (a string
     * literal), so it stays valid for the lifetime of the program.
     *
     * plans/plan_runtimerenderer.md RTR-P7-5: this is the single place the identity names exist. It takes the
     * identity as a parameter rather than reading the compile-time one, so it serves both the
     * compile-time selection (through getCurrentGraphicsRendererName() below) and any runtime
     * selection, without the table being written twice.
     *
     * @param type The renderer identity to name.
     * @return The renderer's name, or "UNKNOWN" for a value outside the enum.
     */
    constexpr std::string_view getGraphicsRendererName(GraphicsRendererType type)
    {
        switch (type)
        {
            case GraphicsRendererType::SdlRenderer: return "SDL_RENDERER";
            case GraphicsRendererType::OpenGLES2:    return "OPENGLES2";
            case GraphicsRendererType::OpenGLES3:    return "OPENGLES3";
            case GraphicsRendererType::OpenGL33:    return "OPENGL33";
            case GraphicsRendererType::WebGL1:       return "WEBGL1";
            case GraphicsRendererType::WebGL2:       return "WEBGL2";
            case GraphicsRendererType::Vulkan:       return "VULKAN";
            case GraphicsRendererType::WebGPU:       return "WEBGPU";
            case GraphicsRendererType::Headless:     return "HEADLESS";
            case GraphicsRendererType::Software:     return "SOFTWARE";
            case GraphicsRendererType::Stub:          return "STUB";
            case GraphicsRendererType::DirectX11:        return "DIRECTX11";
            case GraphicsRendererType::DirectX12:        return "DIRECTX12";
            case GraphicsRendererType::Direct2D:     return "DIRECT2D";
            case GraphicsRendererType::Canvas:       return "CANVAS";
            case GraphicsRendererType::HtmlDom:      return "HTML_DOM";
            case GraphicsRendererType::FreeDirect:           return "FREEDIRECT";
            case GraphicsRendererType::DirectX9:          return "DIRECTX9";
            case GraphicsRendererType::SdlGpu:        return "SDL_GPU";
            case GraphicsRendererType::OpenGL4:       return "OPENGL4";
            case GraphicsRendererType::Gdi:           return "GDI";
            case GraphicsRendererType::Metal:          return "METAL";
            case GraphicsRendererType::Fna3d:         return "FNA3D";
            case GraphicsRendererType::SvgDom:         return "SVG_DOM";
            case GraphicsRendererType::PortableGL:    return "PORTABLEGL";
        }
        return "UNKNOWN";
    }

    /**
     * @brief Resolves a renderer name to its identity, case-insensitively.
     *
     * Accepts exactly the CNA_GRAPHICS_RENDERER spellings ("SDL_RENDERER", "OPENGLES3",
     * "DIRECTX9", ...). Case-insensitive because these names reach CNA through environment
     * variables and command lines, which are typed by hand.
     *
     * Answering "is this a real identity" is separate from "is it compiled into this build" --
     * GraphicsRendererRegistry answers the latter.
     *
     * @param name The renderer name to resolve.
     * @param outType Receives the identity when the name is recognized; untouched otherwise.
     * @return true when @p name names one of the public renderer identities.
     */
    constexpr bool tryParseGraphicsRendererName(std::string_view name, GraphicsRendererType& outType)
    {
        constexpr auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const char x = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 'a' + 'A') : a[i];
                const char y = (b[i] >= 'a' && b[i] <= 'z') ? static_cast<char>(b[i] - 'a' + 'A') : b[i];
                if (x != y)
                    return false;
            }
            return true;
        };

        for (int ordinal = 0; ordinal <= static_cast<int>(GraphicsRendererType::PortableGL); ++ordinal)
        {
            const auto candidate = static_cast<GraphicsRendererType>(ordinal);
            if (equalsIgnoreCase(getGraphicsRendererName(candidate), name))
            {
                outType = candidate;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Returns the human-readable name of the graphics renderer compiled into this build.
     *
     * The returned view matches the CNA_GRAPHICS_RENDERER CMake option value exactly
     * (e.g. "OPENGLES3", "SDL_RENDERER", "DIRECTX9") and points at static storage (a string literal),
     * so it stays valid for the lifetime of the program. Like getCurrentGraphicsRendererType(),
     * this is a compile-time constant.
     *
     * @return The active renderer's name.
     */
    constexpr std::string_view getCurrentGraphicsRendererName()
    {
        return getGraphicsRendererName(getCurrentGraphicsRendererType());
    }
} // CNA
