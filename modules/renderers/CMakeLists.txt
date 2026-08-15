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
    cna_link_sharp_runtime(${RENDERER_TARGET} PUBLIC)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_include_directories(${RENDERER_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    # The four PLAT-76 allowlisted SDL-backed families resolve the snapshot's WindowId at one
    # SDL3 implementation edge. This remains PRIVATE so the platform implementation tree never
    # becomes part of the renderer or framework include contract; every other family is SDL-free.
    target_include_directories(${RENDERER_TARGET} PRIVATE
        "${CMAKE_SOURCE_DIR}/modules/platform/src")
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

# plan_fx.md FX-061: the MojoShader-to-CNA translation shared by every backend that executes
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

if(NOT CNA_SELECTED_RENDERER STREQUAL "metal" AND NOT CNA_SELECTED_RENDERER STREQUAL "glide")
    add_subdirectory(${CNA_SELECTED_RENDERER})
endif()

# CANVAS, HTML_DOM and SVG_DOM are Emscripten-only selections, but their
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
