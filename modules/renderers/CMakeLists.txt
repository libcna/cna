# =====================================================================================
# Renderer module composition. Every implementation family owns
# modules/renderers/<family>/{CMakeLists.txt,src/,include/,tests/}; exactly one family
# builds the selected ${RENDERER_TARGET} per configuration (RendererSelection.cmake resolves
# the public identity to RENDERER_DIR = modules/renderers/<family>). The 46 public renderer
# identities live in RendererSelection.cmake + GraphicsRendererType.hpp and are pinned by
# scripts/check_renderer_identities.py; families and helper targets here are
# implementation structure, not identities.
# =====================================================================================

# Common renderer interface: the shared public compile definitions. Public include roots
# ride the owning module targets now -- renderers reach the graphics/math/core headers
# through their declared reverse edges below, not through a global include directory.
add_library(cna_renderer_common INTERFACE)
target_link_libraries(cna_renderer_common INTERFACE cna_build_flags)

# Applies the common renderer-target surface to an existing ${RENDERER_TARGET}: the common
# interface, the sharp-runtime closure, the module's own public include root, and the
# reverse edges into the framework modules.
function(cna_renderer_common_setup)
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_common)
    # plan_runtimerenderer.md RTR-P6-4: this family's own identity macro, on this family's target.
    # Its sources guard on it (#ifdef CNA_RENDERER_SOFTWARE inside SoftwareRenderer.cpp, and so on),
    # and in a multi-renderer build it must NOT be visible project-wide -- only the default's is.
    if(CNA_RENDERER_TARGET_DEFINE)
        string(REPLACE "," ";" _cna_target_defines "${CNA_RENDERER_TARGET_DEFINE}")
        target_compile_definitions(${RENDERER_TARGET} PRIVATE ${_cna_target_defines})
    endif()
    cna_link_sharp_runtime(${RENDERER_TARGET} PUBLIC)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_include_directories(${RENDERER_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    # D3D11's/D3D12's SpriteBatch renderer (plan_dx.md DX-70/DX-71) calls back into
    # Microsoft::Xna::Framework::Graphics::Effect::Apply() for SpriteBatch::Begin(effect)'s
    # custom-Effect path -- a genuine, honest circular dependency between the renderer static
    # library and the CNA graphics core (the renderer needs a CNA-defined symbol, while CNA needs
    # the renderer to implement IGraphicsRenderer). Under single-pass archive resolution any
    # executable linking CNA hit a real "undefined reference to Effect::Apply()" at link time.
    # CMake's documented static-library-cycle support resolves this by repeating the archives on
    # the final link line once this cycle is declared.
    #
    # The header-inlined IGraphicsRenderer defaults (HandleUnsupported3DCall -> CNA::Logger::Warn,
    # the default instanced-draw guard, ...) give EVERY renderer the same structural reverse edge,
    # and the module split surfaces it deterministically (first hit for real: VULKAN's
    # cna_reference_dump link). Declared unconditionally: the reverse edges land on the specific
    # modules that define the symbols (Effect/VertexDeclaration/DxtUtil -> cna_graphics_core,
    # Logger -> cna_core, named math/colour values -> cna_math). These PRIVATE edges also carry
    # the framework include roots the renderer TUs compile against.
    target_link_libraries(${RENDERER_TARGET} PRIVATE cna_graphics_core cna_core cna_math)
endfunction()

# Defines the selected ${RENDERER_TARGET} from the calling family's src/ directory
# (non-recursive, exactly as the former central glob: shader headers live in src/shaders/
# and are included includer-relative). Extra arguments append additional sources
# (Objective-C++ units, shared cross-family units).
function(cna_add_renderer)
    file(GLOB _cna_renderer_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(APPEND _cna_renderer_sources ${ARGN})
    add_library(${RENDERER_TARGET} STATIC ${_cna_renderer_sources})
    cna_renderer_common_setup()
endfunction()

get_filename_component(CNA_SELECTED_RENDERER "${RENDERER_DIR}" NAME)

# plan_runtimerenderer.md phase P6: every selected family, not only the default one. In
# single-renderer mode this list has exactly one entry and equals CNA_SELECTED_RENDERER.
set(CNA_SELECTED_RENDERER_FAMILIES)
foreach(_cna_renderer_dir IN LISTS CNA_RENDERER_DIRS)
    get_filename_component(_cna_renderer_family "${_cna_renderer_dir}" NAME)
    list(APPEND CNA_SELECTED_RENDERER_FAMILIES "${_cna_renderer_family}")
endforeach()

# metal and glide keep unconditional header-interface targets: their policy suites in
# modules/renderers/{metal,glide}/tests are deliberately host-portable and compile into the
# CnaTests corpus on every renderer (see the suites' own header comments), so their
# CNA/Internal/Renderers/<X> policy headers must stay reachable in every configuration.
add_subdirectory(metal)
add_subdirectory(glide)

# plan_dx.md design decision 4: D3DCommon shared core, consumed by the directx11 and directx12
# families only -- DIRECTX9 and DIRECTX10 are confirmed independent (design decisions 12 and the
# plan_d3d10.md link audit).
if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
    add_subdirectory(common/d3d)
endif()

# GDI presents the Software renderer's CPU framebuffer through classic Win32 GDI: the
# software module publishes its shared 2D translation-unit list (and its header interface)
# for the GDI configuration. Ordering matters: software must be entered before gdi so the
# published list is visible there.
if(CNA_GRAPHICS_RENDERER STREQUAL "GDI")
    add_subdirectory(software)
endif()

# plan_runtimerenderer.md phase P6: each family's own CMakeLists.txt guards on
# `CNA_GRAPHICS_RENDERER STREQUAL "<ITS IDENTITY>"` and defines `${RENDERER_TARGET}`. Both of those
# are scalars naming the DEFAULT renderer, so entering a non-default family's directory without
# re-establishing them would either skip its block entirely or make it redefine the default's
# target. Re-point both per family instead; in single-renderer mode this loop runs once with the
# values it already had.
set(_cna_entered_families)
set(_cna_saved_graphics_renderer "${CNA_GRAPHICS_RENDERER}")
set(_cna_saved_renderer_target "${RENDERER_TARGET}")

list(LENGTH CNA_SELECTED_RENDERER_FAMILIES _cna_family_count)
math(EXPR _cna_family_last "${_cna_family_count} - 1")
foreach(_cna_family_index RANGE ${_cna_family_last})
    list(GET CNA_SELECTED_RENDERER_FAMILIES ${_cna_family_index} _cna_renderer_family)
    list(GET CNA_RENDERER_IDENTITIES ${_cna_family_index} _cna_renderer_identity)
    list(GET CNA_RENDERER_TARGETS ${_cna_family_index} _cna_renderer_target)
    list(GET CNA_RENDERER_TARGET_DEFINES ${_cna_family_index} _cna_renderer_target_define)

    set(CNA_GRAPHICS_RENDERER "${_cna_renderer_identity}")
    set(RENDERER_TARGET "${_cna_renderer_target}")
    set(CNA_RENDERER_TARGET_DEFINE "${_cna_renderer_target_define}")

    # plan_runtimerenderer.md phase P11: several IDENTITIES may share one implementation family --
    # EasyGL serves up to five. The family directory defines one target and must be entered once;
    # entering it again is a CMake error. The extra identities need no directory of their own,
    # only their own registry entry, which cmake/RendererRegistry.cmake already emits.
    if(_cna_renderer_family IN_LIST _cna_entered_families)
        continue()
    endif()
    list(APPEND _cna_entered_families "${_cna_renderer_family}")

    if(NOT _cna_renderer_family STREQUAL "metal" AND NOT _cna_renderer_family STREQUAL "glide")
        add_subdirectory(${_cna_renderer_family})
    endif()
endforeach()

set(CNA_GRAPHICS_RENDERER "${_cna_saved_graphics_renderer}")
set(RENDERER_TARGET "${_cna_saved_renderer_target}")

# plan_svg_dom.md SHARED-HOOK REQUIREMENT: SVG_DOM (like HTML_DOM/CANVAS) is Emscripten-only, so
# its own modules/renderers/svg-dom/CMakeLists.txt -- which defines its native host-side contract
# test target (CNA_BUILD_SVG_DOM_HOST_TESTS, mirroring HTML_DOM's identical
# CNA_BUILD_HTML_DOM_HOST_TESTS precedent) -- is only entered via the ${CNA_SELECTED_RENDERER}
# dispatch above, which itself requires selecting SVG_DOM, which requires Emscripten
# (cmake/RendererSelection.cmake's own platform gate). That leaves the native host-test target
# genuinely unreachable in ANY non-Emscripten configuration regardless of which renderer is
# selected -- the same latent gap HTML_DOM's own cna_test_htmldom_host target already has (no
# CI/script reference to it exists anywhere in this repo either). Proven while implementing
# SVG_DOM's own host tests: this is a pre-existing shared-dispatcher gap, not something specific
# to one renderer, and the smallest fix is a small, off-by-default (OFF unless explicitly
# requested), fully self-contained addition here -- it defines its own target directly rather than
# entering svg-dom's own CMakeLists.txt (which would redefine ${RENDERER_TARGET}, a name that
# belongs to whichever renderer IS actually selected), so it has zero effect on any other
# renderer's build unless a developer explicitly passes -DCNA_BUILD_SVG_DOM_HOST_TESTS=ON.
option(CNA_BUILD_SVG_DOM_HOST_TESTS
    "Build native host-side SVG DOM renderer contract tests (see the comment above)" OFF)
if(CNA_BUILD_TESTS AND CNA_BUILD_SVG_DOM_HOST_TESTS AND NOT EMSCRIPTEN)
    add_executable(cna_test_svgdom_host
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/tests/CNA/Internal/Renderers/SvgDom/SvgDomRendererTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/src/SvgDomRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/src/SvgDomRenderTargetRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/src/SvgDomSpriteBatchRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/src/SvgDomState.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/src/SvgDomTextureRenderer.cpp)
    target_include_directories(cna_test_svgdom_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/svg-dom/include)
    target_compile_definitions(cna_test_svgdom_host PRIVATE CNA_SVG_DOM_HOST_TESTS=1)
    target_link_libraries(cna_test_svgdom_host PRIVATE
        cna_graphics_core cna_core cna_math gtest_main SDL3::SDL3 SDL3_image::SDL3_image)
    cna_link_sharp_runtime(cna_test_svgdom_host PRIVATE)
    add_test(NAME SvgDomHostContracts COMMAND cna_test_svgdom_host)
    set_tests_properties(SvgDomHostContracts PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(SvgDomHostContracts PROPERTIES LABELS "SVG_DOM;host-only")
endif()
