# =====================================================================================
# Renderer module composition. Every implementation family owns
# modules/renderers/<family>/{CMakeLists.txt,src/,include/,tests/}; exactly one family
# builds the selected ${BACKEND_TARGET} per configuration (BackendSelection.cmake resolves
# the public identity to BACKEND_DIR = modules/renderers/<family>). The 41 public renderer
# identities live in BackendSelection.cmake + GraphicsBackendType.hpp and are pinned by
# scripts/check_renderer_identities.py; families and helper targets here are
# implementation structure, not identities.
# =====================================================================================

# Common backend interface: the shared public compile definitions. Public include roots
# ride the owning module targets now -- backends reach the graphics/math/core headers
# through their declared reverse edges below, not through a global include directory.
add_library(cna_backend_graphics_common INTERFACE)
target_link_libraries(cna_backend_graphics_common INTERFACE cna_build_flags)

# Applies the common renderer-target surface to an existing ${BACKEND_TARGET}: the common
# interface, the sharp-runtime closure, the module's own public include root, and the
# reverse edges into the framework modules.
function(cna_renderer_backend_common_setup)
    target_link_libraries(${BACKEND_TARGET} PUBLIC cna_backend_graphics_common)
    cna_link_sharp_runtime(${BACKEND_TARGET} PUBLIC)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_include_directories(${BACKEND_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    # D3D11's/D3D12's SpriteBatch backend (plan_dx.md DX-70/DX-71) calls back into
    # Microsoft::Xna::Framework::Graphics::Effect::Apply() for SpriteBatch::Begin(effect)'s
    # custom-Effect path -- a genuine, honest circular dependency between the backend static
    # library and the CNA graphics core (the backend needs a CNA-defined symbol, while CNA needs
    # the backend to implement IGraphicsBackend). Under single-pass archive resolution any
    # executable linking CNA hit a real "undefined reference to Effect::Apply()" at link time.
    # CMake's documented static-library-cycle support resolves this by repeating the archives on
    # the final link line once this cycle is declared.
    #
    # The header-inlined IGraphicsBackend defaults (HandleUnsupported3DCall -> CNA::Logger::Warn,
    # the default instanced-draw guard, ...) give EVERY backend the same structural reverse edge,
    # and the module split surfaces it deterministically (first hit for real: VULKAN's
    # cna_reference_dump link). Declared unconditionally: the reverse edges land on the specific
    # modules that define the symbols (Effect/VertexDeclaration/DxtUtil -> cna_graphics_core,
    # Logger -> cna_core, named math/colour values -> cna_math). These PRIVATE edges also carry
    # the framework include roots the backend TUs compile against.
    target_link_libraries(${BACKEND_TARGET} PRIVATE cna_graphics_core cna_core cna_math)
endfunction()

# Defines the selected ${BACKEND_TARGET} from the calling family's src/ directory
# (non-recursive, exactly as the former central glob: shader headers live in src/shaders/
# and are included includer-relative). Extra arguments append additional sources
# (Objective-C++ units, shared cross-family units).
function(cna_add_renderer_backend)
    file(GLOB _cna_backend_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(APPEND _cna_backend_sources ${ARGN})
    add_library(${BACKEND_TARGET} STATIC ${_cna_backend_sources})
    cna_renderer_backend_common_setup()
endfunction()

get_filename_component(CNA_SELECTED_RENDERER "${BACKEND_DIR}" NAME)

# metal and glide keep unconditional header-interface targets: their policy suites in
# modules/renderers/{metal,glide}/tests are deliberately host-portable and compile into the
# CnaTests corpus on every backend (see the suites' own header comments), so their
# CNA/Internal/Backends/<X> policy headers must stay reachable in every configuration.
add_subdirectory(metal)
add_subdirectory(glide)

# plan_dx.md design decision 4: D3DCommon shared core, consumed by the d3d11 and d3d12
# families only -- D3D9 and D3D10 are confirmed independent (design decisions 12 and the
# plan_d3d10.md link audit).
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    add_subdirectory(common/d3d)
endif()

# plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
# SdlGraphicsBackend (composition, not reimplementation) -- the sdl-renderer module builds
# its sources as a shared core library for the ASCII configuration.
if(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    add_subdirectory(sdl-renderer)
endif()

# GDI presents the Software backend's CPU framebuffer through classic Win32 GDI: the
# software module publishes its shared 2D translation-unit list (and its header interface)
# for the GDI configuration. Ordering matters: software must be entered before gdi so the
# published list is visible there.
if(CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    add_subdirectory(software)
endif()

if(NOT CNA_SELECTED_RENDERER STREQUAL "metal" AND NOT CNA_SELECTED_RENDERER STREQUAL "glide")
    add_subdirectory(${CNA_SELECTED_RENDERER})
endif()
