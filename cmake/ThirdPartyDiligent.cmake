# DiligentCore integration for the CNA Diligent graphics renderer (plans/plan_diligent.md DILIGENT-1/2).
#
# DiligentCore is itself a graphics abstraction layer over D3D11/D3D12/Vulkan/OpenGL/Metal, so this
# module's job is only to make its engine factories available; which of them the renderer actually
# uses is a RUNTIME decision (see DiligentRenderer::SelectRenderDevice).
#
# Source acquisition is FetchContent with a pinned tag. To build against a local checkout instead
# (offline, or to avoid re-cloning ~200 MB per fresh build tree), use FetchContent's own standard
# override rather than a CNA-specific option:
#
#   cmake -DCNA_GRAPHICS_RENDERER=DILIGENT \
#         -DFETCHCONTENT_SOURCE_DIR_DILIGENTCORE=$HOME/deps/DiligentCore ...
#
# That checkout must be a recursive clone of the same tag (DiligentCore vendors glslang,
# SPIRV-Tools, SPIRV-Cross, Vulkan-Headers and volk as submodules).

include_guard(GLOBAL)

set(CNA_DILIGENT_VERSION "v2.5.6" CACHE STRING "Pinned DiligentCore release used by CNA")

function(cna_configure_diligent)
    if(TARGET Diligent-GraphicsEngineVk-static OR TARGET Diligent-GraphicsEngineOpenGL-static)
        return()
    endif()

    # DILIGENT-2: build only what CNA links. Diligent's own tests/format validation are pure build
    # cost here, and the archiver (offline pipeline-state packing) has no CNA consumer.
    set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DILIGENT_BUILD_CORE_TESTS OFF CACHE BOOL "" FORCE)
    set(DILIGENT_NO_FORMAT_VALIDATION ON CACHE BOOL "" FORCE)
    set(DILIGENT_NO_ARCHIVER ON CACHE BOOL "" FORCE)
    set(DILIGENT_INSTALL_CORE OFF CACHE BOOL "" FORCE)

    # HLSL is CNA's single shader source language on this renderer (design decision 4), including on
    # the non-Direct3D device types, so Diligent's HLSL front end must stay enabled: it is what
    # cross-compiles CNA's HLSL to SPIR-V (Vulkan) and to GLSL (OpenGL).
    set(DILIGENT_NO_HLSL OFF CACHE BOOL "" FORCE)

    # Which engines exist at all is a compile-time platform question; which one is used is decided
    # at runtime. Direct3D headers exist only on Windows, Metal only on Apple, and Diligent's own
    # CMake already gates those -- the entries below are the ones CNA has a reason to state.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(DILIGENT_NO_DIRECT3D11 ON CACHE BOOL "" FORCE)
        set(DILIGENT_NO_DIRECT3D12 ON CACHE BOOL "" FORCE)
    endif()
    set(DILIGENT_NO_WEBGPU ON CACHE BOOL "" FORCE)

    # DILIGENT-2, found empirically: DiligentCore's OpenGL engine needs GL/GLX development headers
    # on Linux (its bundled GLEW includes <GL/glx.h>). A machine with a Vulkan loader but no
    # libgl-dev would otherwise fail deep inside a third-party target with an unhelpful error, so
    # the OpenGL engine is disabled with a clear STATUS line instead. Vulkan needs no system SDK:
    # DiligentCore vendors Vulkan-Headers and loads the loader through volk at runtime.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_path(CNA_DILIGENT_GLX_INCLUDE_DIR NAMES GL/glx.h)
        if(NOT CNA_DILIGENT_GLX_INCLUDE_DIR)
            message(STATUS
                "CNA Diligent: GL/glx.h not found -- building without the OpenGL device type "
                "(install libgl-dev/libglx-dev to enable it). Vulkan remains available.")
            set(DILIGENT_NO_OPENGL ON CACHE BOOL "" FORCE)
        endif()
    endif()

    include(FetchContent)
    FetchContent_Declare(
        DiligentCore
        GIT_REPOSITORY https://github.com/DiligentGraphics/DiligentCore.git
        GIT_TAG ${CNA_DILIGENT_VERSION}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(DiligentCore)

    # Diligent's headers are consumed as <Graphics/GraphicsEngine/interface/...>, which resolves
    # through the source root rather than through any single target's interface.
    set(CNA_DILIGENT_SOURCE_DIR "${diligentcore_SOURCE_DIR}" CACHE INTERNAL "DiligentCore source root")

    set(_cna_diligent_engines)
    if(TARGET Diligent-GraphicsEngineVk-static)
        list(APPEND _cna_diligent_engines "Vulkan")
    endif()
    if(TARGET Diligent-GraphicsEngineOpenGL-static)
        list(APPEND _cna_diligent_engines "OpenGL")
    endif()
    if(TARGET Diligent-GraphicsEngineD3D11-static)
        list(APPEND _cna_diligent_engines "D3D11")
    endif()
    if(TARGET Diligent-GraphicsEngineD3D12-static)
        list(APPEND _cna_diligent_engines "D3D12")
    endif()
    if(NOT _cna_diligent_engines)
        message(FATAL_ERROR "CNA Diligent: DiligentCore built no usable engine for this platform.")
    endif()
    set(CNA_DILIGENT_ENGINES "${_cna_diligent_engines}" CACHE INTERNAL "Diligent engines available to CNA")
    message(STATUS "CNA Diligent: using DiligentCore ${CNA_DILIGENT_VERSION}, engines: ${_cna_diligent_engines}")
endfunction()

# Links the Diligent engine targets that actually got built into @p target, and defines one
# CNA_DILIGENT_HAS_<ENGINE> macro per engine so the renderer's runtime device selection can be
# compiled against exactly the set that exists.
function(cna_link_diligent target)
    target_include_directories(${target} PRIVATE "${CNA_DILIGENT_SOURCE_DIR}")

    # Diligent-BuildSettings defines a bare `DEBUG` in Debug configurations, which collides with
    # CNA's own `CNA::LogLevel::DEBUG` enumerator and turns every translation unit that includes
    # CNA/LogLevel.hpp into a syntax error (found empirically on the first build of this renderer).
    # Diligent's own code uses DILIGENT_DEBUG, which is set separately and left alone; the bare
    # spelling exists only for third-party compatibility, so undefining it costs nothing. This has
    # to be a compile option rather than a definition: CMake emits COMPILE_DEFINITIONS before
    # COMPILE_OPTIONS, and -U only cancels a -D that precedes it.
    target_compile_options(${target} PRIVATE -UDEBUG)
    target_link_libraries(${target} PRIVATE
        Diligent-GraphicsEngine
        Diligent-Common
        Diligent-GraphicsAccessories
        Diligent-Primitives
        Diligent-BuildSettings)

    if(TARGET Diligent-GraphicsEngineVk-static)
        target_link_libraries(${target} PRIVATE Diligent-GraphicsEngineVk-static)
        target_compile_definitions(${target} PRIVATE CNA_DILIGENT_HAS_VULKAN=1)
    endif()
    if(TARGET Diligent-GraphicsEngineOpenGL-static)
        target_link_libraries(${target} PRIVATE Diligent-GraphicsEngineOpenGL-static)
        target_compile_definitions(${target} PRIVATE CNA_DILIGENT_HAS_OPENGL=1)
    endif()
    if(TARGET Diligent-GraphicsEngineD3D11-static)
        target_link_libraries(${target} PRIVATE Diligent-GraphicsEngineD3D11-static)
        target_compile_definitions(${target} PRIVATE CNA_DILIGENT_HAS_D3D11=1)
    endif()
    if(TARGET Diligent-GraphicsEngineD3D12-static)
        target_link_libraries(${target} PRIVATE Diligent-GraphicsEngineD3D12-static)
        target_compile_definitions(${target} PRIVATE CNA_DILIGENT_HAS_D3D12=1)
    endif()
endfunction()
