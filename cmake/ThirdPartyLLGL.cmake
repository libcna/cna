include_guard(GLOBAL)

# LLGL (https://github.com/LukasBanana/LLGL) integration for the LLGL graphics renderer.
#
# LLGL is itself an abstraction over OpenGL/Vulkan/Direct3D/Metal, so unlike every other renderer in
# this project CNA does not talk to a native graphics API here -- it talks to LLGL, and LLGL picks
# the native API at RUNTIME (see LlglRendererSelection.cpp). That makes the module set compiled in
# here a real capability boundary: a renderer module that was not built cannot be selected at
# runtime, no matter what CNA_LLGL_RENDERER asks for.
#
# Acquisition follows the pinned-tag FetchContent shape (like BGFX), with an explicit local-source
# escape hatch for reproducible/offline builds (like CNA_WEBGPU_ROOT).

set(CNA_LLGL_GIT_TAG "Release-v0.04b" CACHE STRING
    "LLGL git tag/commit to fetch (pinned; do not track a moving branch)")
set(CNA_LLGL_GIT_REPOSITORY "https://github.com/LukasBanana/LLGL.git" CACHE STRING
    "LLGL git repository URL")
set(CNA_LLGL_ROOT "" CACHE PATH
    "Path to an existing LLGL source checkout; when set, no download is performed")

# LLGL builds its Vulkan module only when a Vulkan SDK is actually present, so this defaults to
# whatever the host can honestly provide instead of hard-failing a machine without Vulkan headers.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    set(_cna_llgl_vulkan_default ON)
else()
    set(_cna_llgl_vulkan_default OFF)
endif()

option(CNA_LLGL_BUILD_RENDERER_OPENGL
       "Build LLGL's required OpenGL renderer module (the supported CNA LLGL runtime)" ON)
option(CNA_LLGL_BUILD_RENDERER_VULKAN "Build LLGL's Vulkan renderer module (experimental in LLGL)"
       ${_cna_llgl_vulkan_default})
# The Null module renders nothing at all. It is built so CNA_LLGL_RENDERER=Null can be requested
# EXPLICITLY for API-lifecycle diagnostics, and it is deliberately excluded from the automatic
# fallback chain -- a renderer that silently "succeeds" while drawing nothing is exactly the kind of
# fabricated success this project's renderers must never produce.
option(CNA_LLGL_BUILD_RENDERER_NULL "Build LLGL's Null renderer module (explicit opt-in only)" ON)

# Configures LLGL and publishes the target list every consumer must link.
#
# With LLGL_BUILD_STATIC_LIB the renderer modules are separate static libraries that are NOT pulled
# in transitively by the core LLGL target -- LLGL's own CMakeLists records them in the
# LLGL_GLOBAL_MODULE_LIST global property and expects the final application to link all of them, so
# that its static module registry can resolve a module by name at runtime. This function forwards
# that list as CNA_LLGL_LIBRARIES in the caller's scope.
function(cna_configure_llgl)
    set(LLGL_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
    set(LLGL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLGL_BUILD_WRAPPER_C99 OFF CACHE BOOL "" FORCE)
    set(LLGL_BUILD_WRAPPER_CSHARP OFF CACHE BOOL "" FORCE)
    # LLGL reports unrecoverable errors through LLGL_TRAP(), which aborts the process outright
    # unless exceptions are enabled. CNA surfaces renderer failures as exceptions everywhere else
    # (GraphicsDevice constructors, Texture2D uploads), so an abort would replace a reportable
    # failure with a crash.
    set(LLGL_ENABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
    set(LLGL_BUILD_RENDERER_NULL ${CNA_LLGL_BUILD_RENDERER_NULL} CACHE BOOL "" FORCE)
    set(LLGL_BUILD_RENDERER_OPENGL ${CNA_LLGL_BUILD_RENDERER_OPENGL} CACHE BOOL "" FORCE)
    set(LLGL_BUILD_RENDERER_VULKAN ${CNA_LLGL_BUILD_RENDERER_VULKAN} CACHE BOOL "" FORCE)

    if(NOT CNA_LLGL_BUILD_RENDERER_OPENGL)
        message(FATAL_ERROR
            "CNA: the supported LLGL contract requires the OpenGL renderer module. "
            "CNA_LLGL_BUILD_RENDERER_VULKAN is compile coverage only at the pinned dependency "
            "revision, and the Null module draws nothing; neither is an automatic fallback.")
    endif()

    # LLGL's internal LLGL_VA_ARGS macro is built on `, ## __VA_ARGS__`, a GNU extension that
    # swallows the comma when the variadic list is empty. CNA compiles with CMAKE_CXX_EXTENSIONS
    # OFF, and inheriting that into LLGL's own sources leaves a stray comma in every LLGL_TRAP call
    # -- LLGL does not compile at all without extensions (found empirically: "expected
    # primary-expression before ')'" from Core/Exception.h). This is set in this function's own
    # scope, so it reaches LLGL's subdirectory and nothing else.
    set(CMAKE_CXX_EXTENSIONS ON)

    if(CNA_LLGL_ROOT)
        if(NOT EXISTS "${CNA_LLGL_ROOT}/CMakeLists.txt")
            message(FATAL_ERROR
                "CNA: CNA_LLGL_ROOT='${CNA_LLGL_ROOT}' does not contain a CMakeLists.txt -- it must "
                "point at an LLGL source checkout (git clone ${CNA_LLGL_GIT_REPOSITORY}).")
        endif()
        message(STATUS "CNA: using local LLGL source at ${CNA_LLGL_ROOT}")
        add_subdirectory("${CNA_LLGL_ROOT}" llgl)
    else()
        message(STATUS "CNA: fetching LLGL ${CNA_LLGL_GIT_TAG} from ${CNA_LLGL_GIT_REPOSITORY}")
        include(FetchContent)
        FetchContent_Declare(
            llgl
            GIT_REPOSITORY "${CNA_LLGL_GIT_REPOSITORY}"
            GIT_TAG "${CNA_LLGL_GIT_TAG}"
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
        )
        FetchContent_MakeAvailable(llgl)
    endif()

    get_property(_cna_llgl_modules GLOBAL PROPERTY LLGL_GLOBAL_MODULE_LIST)
    if(NOT _cna_llgl_modules)
        message(FATAL_ERROR
            "CNA: LLGL was configured but published no module targets (LLGL_GLOBAL_MODULE_LIST is "
            "empty) -- the LLGL version in use does not match this integration.")
    endif()

    # Belt and braces for the extension requirement above: a target that already carried an
    # explicit CXX_EXTENSIONS property would not have picked up the directory-scope value.
    foreach(_cna_llgl_target IN LISTS _cna_llgl_modules)
        if(TARGET ${_cna_llgl_target})
            set_target_properties(${_cna_llgl_target} PROPERTIES CXX_EXTENSIONS ON)
        endif()
    endforeach()

    message(STATUS "CNA: LLGL modules linked: ${_cna_llgl_modules}")
    set(CNA_LLGL_LIBRARIES ${_cna_llgl_modules} PARENT_SCOPE)

    # Mirrors the module set into compile definitions so the renderer's runtime renderer selection
    # can refuse a module that was never built instead of failing deep inside LLGL::RenderSystem.
    #
    # plans/plan_runtimerenderer.md RTR-P6-8: published as a LIST for the llgl module to put on its own
    # target, not applied with add_compile_definitions() here. add_compile_definitions() is
    # DIRECTORY-scoped, and a function does not create a directory scope -- called from
    # RendererSelection.cmake, which the top-level CMakeLists includes, it defined these three
    # macros for every target in the project. That was invisible in a single-renderer build, where
    # the only renderer is LLGL anyway; in a multi-renderer build it hands every other renderer's
    # target macros describing LLGL's module set. Only
    # modules/renderers/llgl/{include,src}/.../LlglRendererSelection.* ever reads them.
    set(_cna_llgl_definitions "")
    if(CNA_LLGL_BUILD_RENDERER_OPENGL)
        list(APPEND _cna_llgl_definitions CNA_LLGL_HAS_OPENGL)
    endif()
    if(CNA_LLGL_BUILD_RENDERER_VULKAN)
        list(APPEND _cna_llgl_definitions CNA_LLGL_HAS_VULKAN)
    endif()
    if(CNA_LLGL_BUILD_RENDERER_NULL)
        list(APPEND _cna_llgl_definitions CNA_LLGL_HAS_NULL)
    endif()
    set(CNA_LLGL_COMPILE_DEFINITIONS ${_cna_llgl_definitions} PARENT_SCOPE)
endfunction()
