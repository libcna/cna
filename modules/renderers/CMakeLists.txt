# =====================================================================================
# Renderer module composition. Every implementation family owns
# modules/renderers/<family>/{CMakeLists.txt,src/,include/,tests/}; exactly one family
# builds the selected ${RENDERER_TARGET} per configuration (RendererSelection.cmake resolves
# the public identity to RENDERER_DIR = modules/renderers/<family>). The public renderer
# identities live in RendererSelection.cmake + GraphicsRendererType.hpp and are pinned by
# scripts/check_renderer_identities.py -- 49 of them today, over 45 families, since one family may
# serve several identities. Deliberately not restated as a number anywhere a reader could trust it
# instead of the script: this comment carried "47" for two identities longer than it was true.
# Families and helper targets here are implementation structure, not identities.
# =====================================================================================

# Common renderer interface: the shared public compile definitions. Public include roots
# ride the owning module targets now -- renderers reach the graphics/math/core headers
# through their declared reverse edges below, not through a global include directory.
add_library(cna_renderer_common INTERFACE)
target_link_libraries(cna_renderer_common INTERFACE cna_build_config)

# plans/plan_runtimerenderer.md RTR-P6-6: this family's target name, derived from its own directory.
#
# modules/renderers/<family> builds cna_renderer_<family>, '-' becoming '_'
# (sdl-renderer -> cna_renderer_sdl_renderer). Checked against RendererSelection.cmake's 42
# dir/target pairs before this was introduced: all 42 agree, so the mapping is a fact about the
# layout rather than a convention this file hopes for.
#
# @param out_var Variable to receive the target name, in the caller's scope.
function(cna_renderer_target_name out_var)
    get_filename_component(_cna_family "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    string(REPLACE "-" "_" _cna_family_suffix "${_cna_family}")
    set(${out_var} "cna_renderer_${_cna_family_suffix}" PARENT_SCOPE)
endfunction()

# Applies the common renderer-target surface to an existing target: the common interface, the
# sharp-runtime closure, the module's own public include root, and the reverse edges into the
# framework modules.
#
# plans/plan_runtimerenderer.md RTR-P6-6: takes the target as an ARGUMENT. It used to read the global
# ${RENDERER_TARGET} scalar, which names the DEFAULT renderer -- correct only because
# add_subdirectory() below is wrapped in a loop that overwrites that scalar before entering each
# family and restores it afterwards. That made every family's correctness depend on an unwritten
# rule ("never read RENDERER_TARGET outside the loop"), and a violation is invisible in a
# single-renderer build, where the default IS the only renderer.
#
# @param target The renderer target to configure.
function(cna_renderer_common_setup target)
    set(RENDERER_TARGET "${target}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_common)
    # plans/plan_runtimerenderer.md RTR-P6-4: this family's own identity macro, on this family's target.
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
    # The four PLAT-76 allowlisted SDL-backed families resolve the snapshot's WindowId at one
    # SDL3 implementation edge. This remains PRIVATE so the platform implementation tree never
    # becomes part of the renderer or framework include contract; every other family is SDL-free.
    target_include_directories(${RENDERER_TARGET} PRIVATE
        "${CNA_SOURCE_DIR}/modules/platform/src")
    # D3D11's/D3D12's SpriteBatch renderer (plans/plan_dx.md DX-70/DX-71) calls back into
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

# Defines this family's renderer target from the calling family's src/ directory
# (non-recursive, exactly as the former central glob: shader headers live in src/shaders/
# and are included includer-relative). Extra arguments append additional sources
# (Objective-C++ units, shared cross-family units).
#
# plans/plan_runtimerenderer.md RTR-P6-6: the target name is DERIVED from the directory this is called
# from -- modules/renderers/<family> builds cna_renderer_<family>, with '-' replaced by '_'.
# Verified to hold for all 42 families against RendererSelection.cmake's own dir/target pairs.
# Deriving it locally removes the last reason for a family to read a global scalar that was set
# somewhere else entirely: a family cannot now be handed the wrong target, because the only input
# is where its own CMakeLists.txt lives.
#
# RENDERER_TARGET is published back into the caller's scope so the rest of a family's
# CMakeLists.txt keeps working unchanged -- but it now holds THAT family's target, not the build
# default's, whether or not any loop set it first.
function(cna_add_renderer)
    cna_renderer_target_name(_cna_this_target)
    file(GLOB _cna_renderer_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(APPEND _cna_renderer_sources ${ARGN})
    add_library(${_cna_this_target} STATIC ${_cna_renderer_sources})
    cna_renderer_common_setup(${_cna_this_target})
    set(RENDERER_TARGET "${_cna_this_target}" PARENT_SCOPE)
endfunction()

get_filename_component(CNA_SELECTED_RENDERER "${RENDERER_DIR}" NAME)

# plans/plan_runtimerenderer.md phase P6: every selected family, not only the default one. In
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

# plans/plan_dx.md design decision 4: D3DCommon shared core, consumed by the directx11 and directx12
# families only -- DIRECTX9 and DIRECTX10 are confirmed independent (design decisions 12 and the
# plans/plan_d3d10.md link audit).
if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
    add_subdirectory(common/d3d)
endif()

# plans/plan_fx.md FX-061: the MojoShader-to-CNA translation shared by every backend that executes
# compiled XNA effects. Entered only where cna_mojoshader was configured, which today means the
# FNA3D renderer or an SDL_GPU build with -DCNA_SDL_GPU_COMPILED_EFFECTS=ON.
if(TARGET cna_mojoshader)
    add_subdirectory(common/mojoshader)
endif()

# GDI presents the Software renderer's CPU framebuffer through classic Win32 GDI: the
# software module publishes its shared 2D translation-unit list (and its header interface)
# for the GDI configuration. Ordering matters: software must be entered before gdi so the
# published list is visible there.
if(CNA_GRAPHICS_RENDERER STREQUAL "GDI")
    add_subdirectory(software)
endif()

# plans/plan_runtimerenderer.md phase P6: each family's own CMakeLists.txt guards on
# `CNA_GRAPHICS_RENDERER STREQUAL "<ITS IDENTITY>"` and defines `${RENDERER_TARGET}`. Both of those
# are scalars naming the DEFAULT renderer, so entering a non-default family's directory without
# re-establishing them would either skip its block entirely or make it redefine the default's
# target. Re-point both per family instead; in single-renderer mode this loop runs once with the
# values it already had.
set(_cna_entered_families)
set(_cna_saved_graphics_renderer "${CNA_GRAPHICS_RENDERER}")

list(LENGTH CNA_SELECTED_RENDERER_FAMILIES _cna_family_count)
math(EXPR _cna_family_last "${_cna_family_count} - 1")
foreach(_cna_family_index RANGE ${_cna_family_last})
    list(GET CNA_SELECTED_RENDERER_FAMILIES ${_cna_family_index} _cna_renderer_family)
    list(GET CNA_RENDERER_IDENTITIES ${_cna_family_index} _cna_renderer_identity)
    list(GET CNA_RENDERER_TARGETS ${_cna_family_index} _cna_renderer_target)
    list(GET CNA_RENDERER_TARGET_DEFINES ${_cna_family_index} _cna_renderer_target_define)

    set(CNA_GRAPHICS_RENDERER "${_cna_renderer_identity}")
    set(CNA_RENDERER_TARGET_DEFINE "${_cna_renderer_target_define}")

    # plans/plan_runtimerenderer.md RTR-P6-3: RENDERER_TARGET is deliberately NOT re-pointed here any
    # more. It used to be, and that overwrite was the whole reason a family could read a global
    # scalar and get the right answer -- correctness resting on an unwritten rule that no read may
    # happen outside this loop, invisible in single-renderer mode where the default is the only
    # renderer there is. Since RTR-P6-6 each family derives its own target from its own directory
    # (cna_add_renderer / cna_renderer_target_name) and publishes it into its own scope, so the
    # overwrite has no remaining reader: verified across all 42 families that none reads
    # ${RENDERER_TARGET} before establishing its own target.
    #
    # CNA_GRAPHICS_RENDERER and CNA_RENDERER_TARGET_DEFINE still are re-pointed: family
    # CMakeLists.txt files guard on `CNA_GRAPHICS_RENDERER STREQUAL "<IDENTITY>"`, and the define
    # names the identity being entered.

    # plans/plan_runtimerenderer.md phase P11: several IDENTITIES may share one implementation family --
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

# Compile the descriptors of every family this configuration did NOT enter above, so that no
# registered renderer's pre-construction contract can rot unseen between the rare builds that
# select it (cmake/RendererDescriptorGate.cmake).
cna_add_renderer_descriptor_gate()

# CANVAS, HTML_DOM, SVG_DOM and PIXIJS are Emscripten-only selections, but their
# implementation-independent contract tests deliberately compile on a native host with EM_JS
# bodies excluded. Register those opt-in host targets here, outside selected-renderer dispatch, so
# they remain reachable without weakening any renderer's platform gate.
option(CNA_BUILD_CANVAS_HOST_TESTS
    "Build native host-side Canvas renderer contract tests" OFF)
if(CNA_BUILD_TESTS AND CNA_BUILD_CANVAS_HOST_TESTS AND NOT EMSCRIPTEN)
    add_executable(cna_test_canvas_host
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas/tests/CNA/Internal/Renderers/Canvas/CanvasRendererTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas/src/CanvasRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas/src/CanvasRenderTargetRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas/src/CanvasSpriteBatchRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas/src/CanvasTextureRenderer.cpp)
    target_include_directories(cna_test_canvas_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/canvas/include)
    target_compile_definitions(cna_test_canvas_host PRIVATE CNA_CANVAS_HOST_TESTS=1)
    target_link_libraries(cna_test_canvas_host PRIVATE
        cna_graphics_core cna_core cna_math gtest_main)
    cna_link_sharp_runtime(cna_test_canvas_host PRIVATE)
    add_test(NAME CanvasHostContracts COMMAND cna_test_canvas_host)
    set_tests_properties(CanvasHostContracts PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(CanvasHostContracts PROPERTIES LABELS "CANVAS;host-only")
endif()

option(CNA_BUILD_HTML_DOM_HOST_TESTS
    "Build native host-side HTML DOM renderer contract tests" OFF)
if(CNA_BUILD_TESTS AND CNA_BUILD_HTML_DOM_HOST_TESTS AND NOT EMSCRIPTEN)
    add_executable(cna_test_htmldom_host
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/tests/CNA/Internal/Renderers/HtmlDom/HtmlDomRendererTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/src/HtmlDomRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/src/HtmlDomRenderTargetRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/src/HtmlDomSpriteBatchRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/src/HtmlDomState.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/src/HtmlDomTextureRenderer.cpp)
    target_include_directories(cna_test_htmldom_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/html-dom/include)
    target_compile_definitions(cna_test_htmldom_host PRIVATE CNA_HTML_DOM_HOST_TESTS=1)
    target_link_libraries(cna_test_htmldom_host PRIVATE
        cna_graphics_core cna_core cna_math gtest_main)
    cna_link_sharp_runtime(cna_test_htmldom_host PRIVATE)
    add_test(NAME HtmlDomHostContracts COMMAND cna_test_htmldom_host)
    set_tests_properties(HtmlDomHostContracts PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(HtmlDomHostContracts PROPERTIES LABELS "HTML_DOM;host-only")
endif()

option(CNA_BUILD_SVG_DOM_HOST_TESTS
    "Build native host-side SVG DOM renderer contract tests" OFF)
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
        cna_graphics_core cna_core cna_math gtest_main)
    cna_link_sharp_runtime(cna_test_svgdom_host PRIVATE)
    add_test(NAME SvgDomHostContracts COMMAND cna_test_svgdom_host)
    set_tests_properties(SvgDomHostContracts PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(SvgDomHostContracts PROPERTIES LABELS "SVG_DOM;host-only")
endif()

# plans/plan_platform.md PLAT-58/PLAT-61: PIXIJS gained the same opt-in host target as its three
# browser-only siblings when it was migrated off its raw SDL_Window* creation contract. Without
# it the family's surface-contract tests would only ever compile under an Emscripten toolchain,
# which is exactly how the regression stayed invisible on a native host.
option(CNA_BUILD_PIXIJS_HOST_TESTS
    "Build native host-side PixiJS renderer contract tests" OFF)
if(CNA_BUILD_TESTS AND CNA_BUILD_PIXIJS_HOST_TESTS AND NOT EMSCRIPTEN)
    add_executable(cna_test_pixijs_host
        ${CMAKE_CURRENT_SOURCE_DIR}/pixijs/tests/CNA/Internal/Renderers/PixiJs/PixiJsRendererTests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/pixijs/src/PixiJsRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/pixijs/src/PixiJsRenderTargetRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/pixijs/src/PixiJsSpriteBatchRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/pixijs/src/PixiJsTextureRenderer.cpp)
    target_include_directories(cna_test_pixijs_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/pixijs/include)
    target_compile_definitions(cna_test_pixijs_host PRIVATE CNA_PIXIJS_HOST_TESTS=1)
    target_link_libraries(cna_test_pixijs_host PRIVATE
        cna_graphics_core cna_core cna_math gtest_main)
    cna_link_sharp_runtime(cna_test_pixijs_host PRIVATE)
    add_test(NAME PixiJsHostContracts COMMAND cna_test_pixijs_host)
    set_tests_properties(PixiJsHostContracts PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(PixiJsHostContracts PROPERTIES LABELS "PIXIJS;host-only")
endif()
