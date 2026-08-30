# --- Generated renderer registry (plans/plan_runtimerenderer.md design decision 3) ---
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

# The identity -> family map, written exactly once and read by everything that needs either half
# of it. Kept in its own accessor so that a gate iterating over EVERY registered renderer (see
# cmake/RendererDescriptorGate.cmake) derives its inventory from this map rather than from a second
# hand-maintained list that a newly added family could be left out of.
#
# Entries are "<identity>;<namespace>" or "<identity>;<namespace>|<descriptor accessor>"; the
# accessor defaults to GetDescriptor.
#
# EasyGL is the one family serving several identities (plans/plan_glbackends.md), and since
# plans/plan_runtimerenderer.md phase P11 made its GL profile a runtime value, all five can be compiled in
# at once -- each reached through its own accessor on the single EasyGL target.
function(_cna_renderer_identity_map out_var)
    set(_map
        SDL_RENDERER SdlRenderer
        OPENGLES2    EasyGL|GetDescriptorOpenGLES2
        OPENGLES3    EasyGL|GetDescriptorOpenGLES3
        OPENGL33     EasyGL|GetDescriptorOpenGL33
        WEBGL1       EasyGL|GetDescriptorWebGL1
        WEBGL2       EasyGL|GetDescriptorWebGL2
        BGFX         Bgfx
        VULKAN       Vulkan
        WEBGPU       WebGPU
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
        GLIDE        Glide
        GDI          Gdi
        METAL        Metal
        FNA3D        Fna3d
        OPENVG       OpenVg
        TINYGL       TinyGL
        PIXIJS       PixiJs
        NANOVG       NanoVg)

    set(${out_var} "${_map}" PARENT_SCOPE)
endfunction()

# Returns every renderer identity the registry knows how to map, in declaration order.
#
# This is the build's renderer inventory: cmake/RendererDescriptorGate.cmake walks it so that a
# family added to the map above is covered by the descriptor gate on the same commit, with no
# second list to keep in step.
#
# @param out_var Variable to receive the identity list, in the caller's scope.
function(cna_all_renderer_identities out_var)
    _cna_renderer_identity_map(_map)
    set(_identities)
    list(LENGTH _map _length)
    math(EXPR _last "${_length} - 2")
    foreach(_index RANGE 0 ${_last} 2)
        list(GET _map ${_index} _identity)
        list(APPEND _identities "${_identity}")
    endforeach()
    set(${out_var} "${_identities}" PARENT_SCOPE)
endfunction()

# Maps a CNA_GRAPHICS_RENDERER identity to the family that implements it, as
# "<namespace>" or "<namespace>|<descriptor accessor>". The accessor defaults to GetDescriptor.
function(cna_renderer_identity_to_namespace identity out_var)
    _cna_renderer_identity_map(_map)

    list(FIND _map "${identity}" _index)
    if(_index EQUAL -1)
        message(FATAL_ERROR
            "CNA: renderer identity '${identity}' has no implementing namespace in "
            "cmake/RendererRegistry.cmake. A new renderer identity must be registered here as well "
            "as in GraphicsRendererType.hpp and scripts/check_renderer_identities.py.")
    endif()
    math(EXPR _value_index "${_index} + 1")
    list(GET _map ${_value_index} _entry)
    set(${out_var} "${_entry}" PARENT_SCOPE)
endfunction()

# Splits a map entry into its namespace and descriptor accessor.
function(_cna_split_renderer_entry entry ns_var accessor_var)
    string(FIND "${entry}" "|" _bar)
    if(_bar EQUAL -1)
        set(${ns_var} "${entry}" PARENT_SCOPE)
        set(${accessor_var} "GetDescriptor" PARENT_SCOPE)
    else()
        string(SUBSTRING "${entry}" 0 ${_bar} _ns)
        math(EXPR _after "${_bar} + 1")
        string(SUBSTRING "${entry}" ${_after} -1 _accessor)
        set(${ns_var} "${_ns}" PARENT_SCOPE)
        set(${accessor_var} "${_accessor}" PARENT_SCOPE)
    endif()
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

    set(_accessors)
    set(_namespaces)
    set(_declarations "")
    set(_entries "")
    foreach(_identity IN LISTS _identities)
        cna_renderer_identity_to_namespace("${_identity}" _entry)
        _cna_split_renderer_entry("${_entry}" _namespace _accessor)

        # Two identities may share a NAMESPACE (EasyGL serves five), but never an accessor -- that
        # would mean two registry entries describing the same renderer.
        if("${_namespace}::${_accessor}" IN_LIST _accessors)
            message(FATAL_ERROR
                "CNA: renderer identities ${_identities} map to the same descriptor "
                "(${_namespace}::${_accessor}). Each identity needs its own accessor.")
        endif()
        list(APPEND _accessors "${_namespace}::${_accessor}")
        if(NOT _namespace IN_LIST _namespaces)
            list(APPEND _namespaces "${_namespace}")
        endif()

        string(APPEND _declarations
            "namespace CNA::Internal::Renderers::${_namespace}\n"
            "{\n"
            "    const GraphicsRendererDescriptor& ${_accessor}();\n"
            "}\n\n")
        string(APPEND _entries "            ${_namespace}::${_accessor}(),\n")
    endforeach()

    set(_generated "${CMAKE_BINARY_DIR}/generated/CnaRendererRegistry.generated.cpp")
    configure_file(
        "${CNA_SOURCE_DIR}/cmake/templates/CnaRendererRegistry.generated.cpp.in"
        "${_generated}"
        @ONLY)

    message(STATUS "CNA: renderer registry -- ${_count} family/families: ${_namespaces}")
    set(${out_var} "${_generated}" PARENT_SCOPE)
endfunction()
