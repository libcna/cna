# --- Generated renderer registry (plan_runtimerenderer.md design decision 3) ---
#
# Emits one translation unit listing the renderer families linked into this build, so that
# GraphicsRendererRegistry can hand GraphicsDevice a table of GraphicsRendererDescriptor instead of
# every family defining the same factory symbol.
#
# Why generated rather than self-registering: inside a static archive the linker discards any object
# file nothing references, so a renderer translation unit that registered itself from a static
# initializer would silently vanish from the binary unless the whole archive were force-linked
# (--whole-archive / OBJECT libraries). That would also have to coexist with the declared
# cna_graphics_core <-> renderer archive cycles this project already relies on (see
# modules/renderers/CMakeLists.txt). An explicit generated table has none of those problems: no
# static-initialization order, no linker flags, and the set is visible in the build tree.
#
# In a single-renderer build -- the default and recommended mode -- the table has exactly one entry.

# Maps a CNA_GRAPHICS_RENDERER identity to the C++ namespace of the family implementing it.
# EasyGL is the one family serving several identities (plan_glbackends.md).
function(cna_renderer_identity_to_namespace identity out_var)
    set(_map
        SDL_RENDERER SdlRenderer
        OPENGLES2    EasyGL
        OPENGLES3    EasyGL
        OPENGL33     EasyGL
        WEBGL1       EasyGL
        WEBGL2       EasyGL
        BGFX         Bgfx
        VULKAN       Vulkan
        WEBGPU       WebGPU
        MAGNUM       Magnum
        HEADLESS     Headless
        SOFTWARE     Software
        STUB         Stub
        PORTABLEGL   PortableGL
        DIRECTX11    DirectX11
        DIRECTX12    DirectX12
        DIRECT2D     Direct2D
        CANVAS       Canvas
        HTML_DOM     HtmlDom
        SVG_DOM      SvgDom
        SKIA         Skia
        BLEND2D      Blend2D
        FREEDIRECT   FreeDirect
        DIRECTX9     DirectX9
        DIRECTX1     DirectX1
        DIRECTX2     DirectX2
        DIRECTX3     DirectX3
        DIRECTX5     DirectX5
        DIRECTX6     DirectX6
        DIRECTX7     DirectX7
        DIRECTX8     DirectX8
        DIRECTX10    DirectX10
        SDL_GPU      SdlGpu
        OPENGLES1    OpenGLES1
        OPENGL4      OpenGL4
        OPENGL1      OpenGL1
        OPENGL2      OpenGL2
        WICKED       Wicked
        SOKOL        Sokol
        DILIGENT     Diligent
        GLIDE        Glide
        GDI          Gdi
        LLGL         Llgl
        METAL        Metal
        FNA3D        Fna3d
        OPENVG       OpenVg)

    list(FIND _map "${identity}" _index)
    if(_index EQUAL -1)
        message(FATAL_ERROR
            "CNA: renderer identity '${identity}' has no implementing namespace in "
            "cmake/RendererRegistry.cmake. A new renderer identity must be registered here as well "
            "as in GraphicsRendererType.hpp and scripts/check_renderer_identities.py.")
    endif()
    math(EXPR _value_index "${_index} + 1")
    list(GET _map ${_value_index} _namespace)
    set(${out_var} "${_namespace}" PARENT_SCOPE)
endfunction()

# Generates the registry translation unit for ${identities} and returns its path in out_var.
# The FIRST identity is this build's default -- the one GraphicsRendererRegistry::Default() returns
# and the one CNA uses when nothing selects a renderer at runtime.
function(cna_generate_renderer_registry out_var)
    set(_identities ${ARGN})
    list(LENGTH _identities _count)
    if(_count EQUAL 0)
        message(FATAL_ERROR "CNA: cna_generate_renderer_registry() called with no renderer identity.")
    endif()

    set(_namespaces)
    foreach(_identity IN LISTS _identities)
        cna_renderer_identity_to_namespace("${_identity}" _namespace)
        if(_namespace IN_LIST _namespaces)
            message(FATAL_ERROR
                "CNA: renderer identities ${_identities} include two served by the same "
                "implementation family (${_namespace}). They cannot coexist in one binary until "
                "that family's profile becomes a runtime choice (plan_runtimerenderer.md phase P11).")
        endif()
        list(APPEND _namespaces "${_namespace}")
    endforeach()

    set(_declarations "")
    set(_entries "")
    foreach(_namespace IN LISTS _namespaces)
        string(APPEND _declarations
            "namespace CNA::Internal::Renderers::${_namespace}\n"
            "{\n"
            "    const GraphicsRendererDescriptor& GetDescriptor();\n"
            "}\n\n")
        string(APPEND _entries "            ${_namespace}::GetDescriptor(),\n")
    endforeach()

    set(_generated "${CMAKE_BINARY_DIR}/generated/CnaRendererRegistry.generated.cpp")
    configure_file(
        "${CNA_SOURCE_DIR}/cmake/templates/CnaRendererRegistry.generated.cpp.in"
        "${_generated}"
        @ONLY)

    message(STATUS "CNA: renderer registry -- ${_count} family/families: ${_namespaces}")
    set(${out_var} "${_generated}" PARENT_SCOPE)
endfunction()
