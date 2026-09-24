# --- Graphics Renderer Selection ---
# The renderer identity and its per-host default are settled by cmake/RendererIdentityDefault.cmake
# so that the SDL availability gate (cmake/SdlAvailability.cmake, plans/plan_x11.md X11-0090) can
# ask which renderer this build will use BEFORE the vendored SDL sub-build is decided, without
# either file restating the default rule and the two drifting apart.
include(cmake/RendererIdentityDefault.cmake)
# The public identity list and the early refusal of retired or unknown names live in
# cmake/RendererIdentities.cmake, which the top-level CMakeLists.txt includes before anything reads
# CNA_GRAPHICS_RENDERER. Included again here (a no-op after the first time) so this file keeps
# working on its own.
include(cmake/RendererIdentities.cmake)
list(JOIN CNA_RENDERER_PUBLIC_IDENTITIES ", " _cna_public_renderer_text)
set(CNA_GRAPHICS_RENDERER "${_cna_default_renderer}" CACHE STRING "Graphics renderer to use (${_cna_public_renderer_text})")
unset(_cna_public_renderer_text)
set_property(CACHE CNA_GRAPHICS_RENDERER PROPERTY STRINGS ${CNA_RENDERER_PUBLIC_IDENTITIES})

option(CNA_RENDERER_SDL_RENDERER "Enable SDL_Renderer graphics renderer" OFF)
option(CNA_RENDERER_OPENGLES2 "Enable OpenGL ES 2.0 graphics renderer (internally: EasyGL)" OFF)
option(CNA_RENDERER_OPENGLES3 "Enable OpenGL ES graphics renderer (internally: EasyGL)" OFF)
option(CNA_RENDERER_OPENGL33 "Enable desktop OpenGL 3.3 core graphics renderer (internally: EasyGL)" OFF)
option(CNA_RENDERER_WEBGL1 "Enable WebGL 1 graphics renderer, Emscripten only (internally: EasyGL)" OFF)
option(CNA_RENDERER_WEBGL2 "Enable WebGL 2 graphics renderer, Emscripten only (internally: EasyGL)" OFF)
option(CNA_RENDERER_VULKAN "Enable Vulkan graphics renderer" OFF)
option(CNA_RENDERER_WEBGPU "Enable WebGPU graphics renderer (wgpu-native)" OFF)
option(CNA_RENDERER_HEADLESS "Enable Headless (no GPU/window) graphics renderer" OFF)
option(CNA_RENDERER_SOFTWARE "Enable Software (CPU rasterizer) graphics renderer" OFF)
# plans/plan_stub.md: deliberately minimal no-op graphics renderer -- renders nothing, touches no SDL
# window/video subsystem/GPU library, keeps no bookkeeping of any kind (unlike HEADLESS's
# validation modes/counters or SOFTWARE's real CPU rasterizer). Named "Stub" rather than "Null" to
# avoid colliding with the <cstddef>/<cstdlib> NULL macro (see plans/plan_stub.md's naming section).
option(CNA_RENDERER_STUB "Enable Stub (no-op) graphics renderer" OFF)
# rswinkle/PortableGL: a single-header, C99, CPU software implementation of an OpenGL 3.x-ish
# pipeline (real buffers, vertex attribs, programmable vertex/fragment shaders as C function
# pointers, glDrawArrays/glDrawElements, textures) -- no GPU/window required, same "genuine
# CPU-only renderer" category as HEADLESS/SOFTWARE/STUB above, but the rasterization/shading
# pipeline is delegated to real PortableGL API calls rather than a hand-rolled rasterizer.
option(CNA_RENDERER_PORTABLEGL "Enable PortableGL (rswinkle/PortableGL, CPU software OpenGL 3.x) graphics renderer" OFF)
option(CNA_RENDERER_DIRECTX11 "Enable Direct3D 11 graphics renderer (Windows only)" OFF)
option(CNA_RENDERER_DIRECTX12 "Enable Direct3D 12 graphics renderer (Windows only)" OFF)
option(CNA_RENDERER_DIRECT2D "Enable Direct2D 1.1 graphics renderer (Windows only, 2D-only)" OFF)
# plans/plan_canvas.md: HTML Canvas 2D renderer -- Emscripten-only (design decision 1), a browser-native,
# GPU-free 2D-only renderer using canvas.getContext('2d') instead of WEBGL2's WebGL context.
option(CNA_RENDERER_CANVAS "Enable HTML Canvas 2D graphics renderer (Emscripten only)" OFF)
# plans/plan_html_dom.md: HTML DOM renderer -- Emscripten-only (design decision 1), 2D-only, rendering
# SpriteBatch output as pooled CSS-transformed <div> elements instead of rasterizing into a canvas.
option(CNA_RENDERER_HTML_DOM "Enable HTML DOM (CSS-composited) graphics renderer (Emscripten only)" OFF)
option(CNA_RENDERER_FREEDIRECT "Enable FreeDirect (DirectDraw via the ../free-direct sibling reimplementation) graphics renderer" OFF)
option(CNA_RENDERER_DIRECTX9 "Enable Direct3D 9 graphics renderer (Windows only)" OFF)
option(CNA_RENDERER_SDL_GPU "Enable SDL_gpu graphics renderer" OFF)

# plans/plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics renderer -- deliberately
# independent of the GL-family OPENGLES3/OPENGL33/WEBGL1/WEBGL2 renderers (internally EasyGL; the
# OPENGLES3 default targets OpenGL ES 3.0 via EasyGLRenderer's own
# SDL_GL_CONTEXT_PROFILE_ES context request, not a real desktop GL 4.x core profile).
option(CNA_RENDERER_OPENGL4 "Enable real desktop OpenGL 4.x core-profile graphics renderer" OFF)

option(CNA_RENDERER_GDI "Enable classic Win32 GDI (2D-only) graphics renderer" OFF)

# Metal owns the renderer directly; SDL provides only the native macOS window and CAMetalLayer.
option(CNA_RENDERER_METAL "Enable native Apple Metal graphics renderer (macOS only)" OFF)

# plans/plan_fna3d.md: FNA3D (https://github.com/FNA-XNA/FNA3D) -- the XNA-shaped C graphics library FNA
# itself renders through. It names a portable middleware layer rather than one native API: FNA3D picks SDL_GPU, Direct3D 11 or OpenGL at RUNTIME (overridable
# with the FNA3D_FORCE_DRIVER SDL hint). Shaders are Direct3D 9 Effect Framework binaries executed
# through MojoShader -- FNA3D has no other shader entry point at all, so the stock-effect blobs are
# fetched from the pinned FNA checkout alongside FNA3D itself (see cmake/ThirdPartyFNA3D.cmake).
option(CNA_RENDERER_FNA3D "Enable FNA3D graphics renderer (FNA-XNA/FNA3D + MojoShader)" OFF)

# plans/plan_svg_dom.md: SVG DOM renderer -- Emscripten-only (same design decision as CANVAS/HTML_DOM),
# 2D-only, rendering SpriteBatch output as real <svg>/<image> elements (an <svg> viewport per
# sprite crops its source rectangle; SVG-native feColorMatrix filters apply the tint) instead of
# either rasterizing into a <canvas> (CANVAS) or CSS-transforming pooled <div>s (HTML_DOM).
option(CNA_RENDERER_SVG_DOM "Enable SVG DOM graphics renderer (Emscripten only)" OFF)

set(_cna_enabled_renderers)
foreach(_cna_identity IN LISTS CNA_RENDERER_PUBLIC_IDENTITIES)
    if(CNA_RENDERER_${_cna_identity})
        list(APPEND _cna_enabled_renderers "${_cna_identity}")
    endif()
endforeach()

if(_cna_enabled_renderers)
    list(LENGTH _cna_enabled_renderers _cna_enabled_renderers_count)
    if(NOT _cna_enabled_renderers_count EQUAL 1)
        message(FATAL_ERROR "CNA: Exactly one renderer option must be ON when using CNA_RENDERER_* options.")
    endif()

    list(GET _cna_enabled_renderers 0 CNA_GRAPHICS_RENDERER)
endif()

# --- Multi-renderer selection (plans/plan_runtimerenderer.md design decision 1, phase P6) ---
#
# CNA_GRAPHICS_RENDERER stays the primary, single-valued option: it names this build's DEFAULT
# renderer, and a build that sets nothing else behaves exactly as it always has.
#
# CNA_GRAPHICS_RENDERERS is the opt-in second mode -- a list of identities to compile in, from which
# one is chosen at runtime (CNA::GraphicsRendererSelection). When it is not set it is simply the
# single default, so every code path below runs identically for existing builds.
#
# Resolved by its own file, which `cmake -P` can run standalone, so the membership contract has a
# real test rather than only a comment (cmake/Tests/RendererDefaultCase.cmake). Included, not
# called as a function, because it publishes CNA_RENDERER_IDENTITIES into this scope.
include(cmake/RendererDefaultSelection.cmake)

# Design decision 11: reject an unbuildable combination here, with a reason, rather than letting it
# surface as a duplicate-symbol link error.
include(cmake/RendererCombinations.cmake)
cna_validate_renderer_combination(${_cna_renderer_identities})

list(LENGTH _cna_renderer_identities _cna_renderer_identity_count)
if(_cna_renderer_identity_count GREATER 1)
    # The set and its default were already announced by RendererDefaultSelection.cmake; this line
    # states only what is different about this mode.
    message(STATUS "CNA: multi-renderer build -- the renderer is chosen at runtime "
                   "(CNA::GraphicsRendererSelection)")
    # Consumed by GraphicsRendererType.hpp and the identity-reporting accessors (phase P7).
    add_compile_definitions(CNA_MULTI_RENDERER)
endif()

# The per-identity configuration below is a MACRO, not a function, on purpose: macros do not create
# a scope, so every set()/add_compile_definitions()/add_subdirectory() inside behaves exactly as it
# did when this was straight-line code. For a single-identity list the execution is identical.
#
# plans/plan_runtimerenderer.md RTR-P6-4: an identity's own CNA_RENDERER_<X> macro is announced by
# appending it to _cna_identity_defines -- NEVER by calling add_compile_definitions() here. The
# loop below applies the list to that family's own target and, for the DEFAULT identity only, to
# the whole project. add_compile_definitions() is directory-scoped and this file is included from
# the top-level CMakeLists.txt, so calling it from an arm defines that identity's macro
# project-wide for every identity in the list, not just the default -- which breaks the invariant
# every compile-time renderer question in the tree rests on ("the CNA_RENDERER_<X> that is defined
# names the DEFAULT"), including getCurrentGraphicsRendererType()'s #elif chain, which would then
# answer with whichever identity happens to sit earliest in it. Two renderers were once added that
# way; scripts/check_runtime_renderer_discipline.py now fails on a new one.
macro(cna_configure_renderer_identity)
    set(_cna_identity_defines)
# PLAT-140: a terminal consumes finished CPU frames through IPlatformSurfacePresenter; it has no
# graphical native window that a GPU API could bind. This check deliberately precedes every
# renderer dependency probe below, so an incompatible pair always fails with this explanation
# instead of, for example, first asking a TERMINAL+VULKAN build to install a Vulkan SDK.
set(_cna_terminal_renderers SOFTWARE PORTABLEGL HEADLESS STUB)
if(CNA_PLATFORM STREQUAL "TERMINAL" AND NOT CNA_GRAPHICS_RENDERER IN_LIST _cna_terminal_renderers)
    list(JOIN _cna_terminal_renderers ", " _cna_terminal_renderers_text)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=TERMINAL has no native graphical window, so renderer "
        "${CNA_GRAPHICS_RENDERER} cannot be selected.\n"
        "Choose a CPU renderer: ${_cna_terminal_renderers_text}.")
endif()

# plans/plan_dx.md design decision 2: DIRECTX11/DIRECTX12 genuinely cannot build anywhere but Windows (native or
# MinGW/MSVC cross-compile) -- d3d11.h/d3d12.h/dxgi.h do not exist elsewhere, so this is a hard
# FATAL_ERROR. plans/plan_dx9.md design decision 1 extends this same gate to DIRECTX9 (d3d9.h is
# equally Windows-only), and DIRECT2D and GDI need the Windows SDK in the same way. FreeDirect is not
# listed: it renders through the SDL3-backed ../free-direct and builds natively on Linux.
if((CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9" OR CNA_GRAPHICS_RENDERER STREQUAL "DIRECT2D" OR CNA_GRAPHICS_RENDERER STREQUAL "GDI")
        AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
        "CNA: ${CNA_GRAPHICS_RENDERER} renderer only builds when targeting Windows. Either build "
        "natively on Windows, or cross-compile from Linux with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake")
endif()

# plans/plan_apple.md APPLE-4: an iOS configure is rejected here unless CNA actually wires the selected
# renderer up for iOS. This runs before the individual per-renderer gates below so the failure
# names the platform rather than a dependency that was never configured for an iOS sysroot.
# No-op on macOS and on every non-Apple target.
cna_apple_validate_renderer("${CNA_GRAPHICS_RENDERER}")

# Native Metal is currently available only when targeting macOS. SDL is used only for
# window/CAMetalLayer integration; all rendering is performed directly through Metal.
# iOS is Metal's other natural home and the Apple allow-list above already refuses it by default;
# CNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON is the single documented escape hatch for experimenting
# with it there (plans/plan_apple.md APPLE-11), and changes nothing about what is supported.
if(CNA_GRAPHICS_RENDERER STREQUAL "METAL" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CNA_APPLE_IOS AND CNA_APPLE_ALLOW_UNVALIDATED_RENDERER)
        message(WARNING
            "CNA: configuring METAL for iOS. The renderer's supported contract covers macOS only "
            "(docs/metal-renderer.md); its iOS build has no compile, runtime or pixel evidence.")
    else()
        message(FATAL_ERROR
            "CNA: METAL renderer is currently supported only on macOS; iOS and tvOS remain unvalidated.")
    endif()
endif()

# plans/plan_canvas.md design decision 1: HTML Canvas 2D is a browser DOM API and cannot exist outside
# an Emscripten/WebAssembly build -- same hard-gate shape as the DIRECTX11/DIRECTX12 Windows-only check
# above, new condition.
if(CNA_GRAPHICS_RENDERER STREQUAL "CANVAS" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: CANVAS renderer only builds when targeting Emscripten (HTML Canvas is a browser DOM "
        "API). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/cmake/Modules/"
        "Platform/Emscripten.cmake (or use emcmake).")
endif()

# plans/plan_html_dom.md design decision 1: document, HTMLDivElement and CSS only exist inside a browser.
if(CNA_GRAPHICS_RENDERER STREQUAL "HTML_DOM" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: HTML_DOM renderer only builds when targeting Emscripten (it renders through real DOM "
        "elements and CSS). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/"
        "cmake/Modules/Platform/Emscripten.cmake (or use emcmake).")
endif()

# plans/plan_svg_dom.md design decision 1: same reasoning as CANVAS/HTML_DOM above -- document, SVG
# namespace elements and the browser DOM only exist inside a browser.
if(CNA_GRAPHICS_RENDERER STREQUAL "SVG_DOM" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "CNA: SVG_DOM renderer only builds when targeting Emscripten (it renders through real SVG "
        "DOM elements). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/"
        "cmake/Modules/Platform/Emscripten.cmake (or use emcmake).")
endif()

# plans/plan_glbackends.md Phase A/GLB-7: all 5 GL-family public renderers (OPENGLES2/OPENGLES3/OPENGL33
# desktop, WEBGL1/WEBGL2 Emscripten) share one internal implementation (EasyGL, on top of the
# sibling easy-gl library) -- this block sets it up once regardless of which of the 5 was selected.
#
# GLB-38 done: the WebGL1 work (GLB-30..35) landed on easy-gl develop, so this builds against
# the canonical '../easy-gl' sibling checkout again; the temporary '../easy-glrvc' (branch
# 'rvc') redirect from GLB-7 is retired.
if(CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
    if((CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
            OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33") AND EMSCRIPTEN)
        message(FATAL_ERROR
            "CNA: ${CNA_GRAPHICS_RENDERER} is a desktop/mobile GL renderer and cannot target "
            "Emscripten -- use WEBGL1 or WEBGL2 instead.")
    endif()
    if((CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2") AND NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "CNA: ${CNA_GRAPHICS_RENDERER} only builds when targeting Emscripten (WebGL is a "
            "browser API). Configure with -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/"
            "cmake/Modules/Platform/Emscripten.cmake (or use emcmake), or use OPENGLES2/OPENGLES3/"
            "OPENGL33 for a native desktop/mobile GL build.")
    endif()
    if((CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3")
            AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(WARNING "CNA: ${CNA_GRAPHICS_RENDERER} renderer is primarily tested on Linux. Other platforms may require additional setup.")
    endif()
    # easy-gl is a SIBLING repository checkout, not a git submodule of this
    # repo (Task DEV-BUILD-001) -- see sharp-runtime's identical check above
    # for the full rationale.
    # plans/plan_runtimerenderer.md P11: several GL identities can now be selected at once, and they all
    # share this one easy-gl subdirectory -- add it only for the first of them.
    if(NOT TARGET easy-gl AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easy-gl/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: Missing sibling repository 'easy-gl' at "
            "${CMAKE_CURRENT_SOURCE_DIR}/../easy-gl -- this is a separate git "
            "checkout (branch 'develop' of easy-gl) expected next to this repo's "
            "own directory, not a git submodule (git submodule update --init will "
            "not fetch it). easy-gl itself expects its own sibling '../meta-gl' "
            "checkout (branch 'develop' of meta-gl).")
    endif()
    if(NOT _cna_easygl_subdir_added)
        if(EMSCRIPTEN)
            set(EASYGL_EMSCRIPTEN_EXCEPTION_MODEL "JS" CACHE STRING
                "Exception ABI used by easy-gl when embedded in CNA" FORCE)
        endif()
        add_subdirectory(../easy-gl easy-gl)
        set(_cna_easygl_subdir_added TRUE)
    endif()
endif()

# plans/plan_freedirect.md design decision 10 / Task DX3-2: free-direct is a SIBLING repository checkout, not a
# git submodule of this repo -- same rationale as sharp-runtime/easy-gl's identical checks above.
# free-direct's own CMakeLists.txt (add_subdirectory(../free-api ...)) resolves SDL3::SDL3/
# SDL3_image::SDL3_image/SDL3_mixer::SDL3_mixer from CNA's own already-vendored targets (set up by
# cna_configure_vendored_sdl() above, before renderer selection runs), so no
# -DFREE_API_USE_SYSTEM_SDL3 flag is needed here, mirroring how ../free-eggbert/../planetblupi
# already consume free-direct today (design decision 10).
if(CNA_GRAPHICS_RENDERER STREQUAL "FREEDIRECT")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../free-direct/CMakeLists.txt")
        message(FATAL_ERROR
            "CNA: Missing sibling repository 'free-direct' at "
            "${CMAKE_CURRENT_SOURCE_DIR}/../free-direct -- this is a separate git "
            "checkout expected next to this repo's own directory, not a git "
            "submodule (git submodule update --init will not fetch it). Fix: "
            "cd ${CMAKE_CURRENT_SOURCE_DIR}/.. && "
            "git clone https://github.com/openeggbert/free-direct.git")
    endif()
    add_subdirectory(../free-direct free-direct)
endif()

if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
    message(STATUS "CNA: Using SDL_RENDERER graphics renderer")
    set(RENDERER_DIR "modules/renderers/sdl-renderer")
    set(RENDERER_TARGET "cna_renderer_sdl_renderer")
    list(APPEND _cna_identity_defines CNA_RENDERER_SDL_RENDERER)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SDL_RENDERER")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
    message(STATUS "CNA: Using ${CNA_GRAPHICS_RENDERER} graphics renderer (internal implementation: EasyGL)")
    set(RENDERER_DIR "modules/renderers/easygl")
    set(RENDERER_TARGET "cna_renderer_easygl")
    # CNA_RENDERER_EASYGL is the internal implementation identity -- existing #ifdef
    # CNA_RENDERER_EASYGL guards elsewhere in the codebase keep working unmodified regardless of
    # which of the 5 public GL profiles below was selected (plans/plan_glbackends.md GLB-3).
    list(APPEND _cna_identity_defines CNA_RENDERER_EASYGL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_EASYGL")
    # CNA_GL_PROFILE_* selects the GL context/shader profile within the shared EasyGL
    # implementation -- see plans/plan_glbackends.md Phase B/GLB-8 for how EasyGLRenderer.cpp
    # uses this to choose context-creation attributes and shader headers.
    list(APPEND _cna_identity_defines "CNA_GL_PROFILE_${CNA_GRAPHICS_RENDERER}")
    # plans/plan_fx.md FX-062: compiled XNA effects on this renderer go through MojoShader's own OpenGL
    # adapter, which emits GLSL/GLSLES/GLSLES3 source text for whichever profile it is asked for --
    # entirely in parallel to EasyGL's own GLSL ES 3.00-authored-and-string-rewritten stock shaders.
    # Off by default because it pulls a fetched dependency into a renderer that does not otherwise
    # need one. Uses add_compile_definitions directly rather than _cna_identity_defines, matching
    # the sibling CNA_SDL_GPU_COMPILED_EFFECTS option below: an opt-in flag, not a per-identity
    # define every build of this renderer needs.
    option(CNA_EASYGL_COMPILED_EFFECTS
           "Build EasyGL support for compiled XNA Effect bytecode (plans/plan_fx.md FX-062)" OFF)
    if(CNA_EASYGL_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_EASYGL_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "VULKAN")
    message(STATUS "CNA: Using VULKAN graphics renderer")
    set(RENDERER_DIR "modules/renderers/vulkan")
    set(RENDERER_TARGET "cna_renderer_vulkan")
    list(APPEND _cna_identity_defines CNA_RENDERER_VULKAN)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_VULKAN")
    find_package(Vulkan REQUIRED)
    # plans/plan_fx.md FX-065: compiled XNA Effect bytecode through MojoShader's portable SPIR-V profile.
    # Off by default and shaped exactly like the CNA_EASYGL_COMPILED_EFFECTS and
    # CNA_SDL_GPU_COMPILED_EFFECTS options above, for the same reason: MojoShader is a fetched
    # dependency this renderer does not otherwise need. Unlike those two there is no
    # MojoShader-provided adapter to link against (there is no `mojoshader_vulkan.c`) -- the
    # nine-function effect backend is CNA's own, written directly against MOJOSHADER_parse with
    # the SPIR-V profile, which FX-064's existence gate proved against a real device.
    option(CNA_VULKAN_COMPILED_EFFECTS
           "Build Vulkan support for compiled XNA Effect bytecode (plans/plan_fx.md FX-065)" OFF)
    if(CNA_VULKAN_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_VULKAN_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "WEBGPU")
    message(STATUS "CNA: Using WEBGPU graphics renderer")
    set(RENDERER_DIR "modules/renderers/webgpu")
    set(RENDERER_TARGET "cna_renderer_webgpu")
    list(APPEND _cna_identity_defines CNA_RENDERER_WEBGPU)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_WEBGPU")
    include(cmake/ThirdPartyWebGPU.cmake)
    cna_configure_webgpu()
    # plans/plan_webgpu.md WEBGPU-167: compiled XNA Effect bytecode through MojoShader's portable
    # SPIR-V profile plus the combined-image-sampler split WebGPU's shading model needs. Off by
    # default and shaped exactly like the CNA_EASYGL_COMPILED_EFFECTS, CNA_SDL_GPU_COMPILED_EFFECTS
    # and CNA_VULKAN_COMPILED_EFFECTS options, for the same reason: MojoShader is a fetched
    # dependency this renderer does not otherwise need. Like Vulkan there is no MojoShader-provided
    # adapter -- the nine-function effect backend is CNA's own.
    option(CNA_WEBGPU_COMPILED_EFFECTS
           "Build WebGPU support for compiled XNA Effect bytecode (plans/plan_webgpu.md WEBGPU-167)" OFF)
    if(CNA_WEBGPU_COMPILED_EFFECTS)
        # WEBGPU-203/204: browser WebGPU still ingests WGSL and nothing else -- emdawnwebgpu's own
        # createShaderModule switch has a single case, ShaderSourceWGSL. What changed on 2026-09-06
        # is that CNA now TRANSLATES the SPIR-V this route emits into WGSL
        # (modules/renderers/common/mojoshader/src/SpirvToWgsl.cpp), so the Emscripten build no
        # longer has to be refused here. The option is buildable on every target; the shader-module
        # representation is the only thing that differs, and under Emscripten it is fixed at WGSL.
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_WEBGPU_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
    message(STATUS "CNA: Using HEADLESS (no GPU/window) graphics renderer")
    set(RENDERER_DIR "modules/renderers/headless")
    set(RENDERER_TARGET "cna_renderer_headless")
    list(APPEND _cna_identity_defines CNA_RENDERER_HEADLESS)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_HEADLESS")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE")
    message(STATUS "CNA: Using SOFTWARE (CPU rasterizer) graphics renderer")
    set(RENDERER_DIR "modules/renderers/software")
    set(RENDERER_TARGET "cna_renderer_software")
    list(APPEND _cna_identity_defines CNA_RENDERER_SOFTWARE)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SOFTWARE")
    # SOFTWARE-162/165: compiled Effects remain opt-in for the same dependency reason as EasyGL,
    # SDL_GPU and Vulkan. When selected, however, the completed CPU executor advertises and runs
    # the full shared public contract rather than exposing a private staging-only runtime.
    option(CNA_SOFTWARE_COMPILED_EFFECTS
           "Build Software support for compiled XNA Effect bytecode" OFF)
    if(CNA_SOFTWARE_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_SOFTWARE_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "STUB")
    message(STATUS "CNA: Using STUB (no-op) graphics renderer")
    set(RENDERER_DIR "modules/renderers/stub")
    set(RENDERER_TARGET "cna_renderer_stub")
    list(APPEND _cna_identity_defines CNA_RENDERER_STUB)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_STUB")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
    message(STATUS "CNA: Using DIRECTX11 graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx11")
    set(RENDERER_TARGET "cna_renderer_directx11")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX11)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX11")
    # plans/plan_fx.md FX-063: MojoShader ships a native D3D11 adapter, but remains an optional
    # dependency for builds that do not execute compiled XNA Effect Framework bytecode.
    option(CNA_DIRECTX11_COMPILED_EFFECTS
           "Build DirectX 11 support for compiled XNA Effect bytecode (plans/plan_fx.md FX-063)" OFF)
    if(CNA_DIRECTX11_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_DIRECTX11_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
    message(STATUS "CNA: Using DIRECTX12 graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx12")
    set(RENDERER_TARGET "cna_renderer_directx12")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX12)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX12")
    # plans/plan_fx.md FX-134: D3D12 has no MojoShader adapter, so CNA supplies the backend while
    # keeping the dependency absent from ordinary D3D12 builds.
    option(CNA_DIRECTX12_COMPILED_EFFECTS
           "Build DirectX 12 support for compiled XNA Effect bytecode (plans/plan_fx.md FX-134)" OFF)
    if(CNA_DIRECTX12_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_DIRECTX12_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECT2D")
    message(STATUS "CNA: Using DIRECT2D graphics renderer (Windows-only, 2D-only)")
    set(RENDERER_DIR "modules/renderers/direct2d")
    set(RENDERER_TARGET "cna_renderer_direct2d")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECT2D)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECT2D")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "CANVAS")
    message(STATUS "CNA: Using CANVAS (HTML Canvas 2D) graphics renderer")
    set(RENDERER_DIR "modules/renderers/canvas")
    set(RENDERER_TARGET "cna_renderer_canvas")
    list(APPEND _cna_identity_defines CNA_RENDERER_CANVAS)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_CANVAS")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "HTML_DOM")
    message(STATUS "CNA: Using HTML_DOM (CSS-composited DOM elements) graphics renderer")
    set(RENDERER_DIR "modules/renderers/html-dom")
    set(RENDERER_TARGET "cna_renderer_html_dom")
    list(APPEND _cna_identity_defines CNA_RENDERER_HTML_DOM)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_HTML_DOM")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "FREEDIRECT")
    message(STATUS "CNA: Using FreeDirect (DirectDraw via free-direct) graphics renderer")
    set(RENDERER_DIR "modules/renderers/freedirect")
    set(RENDERER_TARGET "cna_renderer_freedirect")
    list(APPEND _cna_identity_defines CNA_RENDERER_FREEDIRECT)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_FREEDIRECT")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9")
    message(STATUS "CNA: Using DIRECTX9 graphics renderer")
    set(RENDERER_DIR "modules/renderers/directx9")
    set(RENDERER_TARGET "cna_renderer_directx9")
    list(APPEND _cna_identity_defines CNA_RENDERER_DIRECTX9)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_DIRECTX9")
    # plans/plan_fx.md FX-070: compiled XNA effects already contain native D3D9 tokens;
    # MojoShader is optional and is used only for the container runtime and reflection.
    option(CNA_DIRECTX9_COMPILED_EFFECTS
           "Build DirectX 9 support for compiled XNA Effect bytecode (plans/plan_fx.md FX-070)" OFF)
    if(CNA_DIRECTX9_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_DIRECTX9_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "METAL")
    enable_language(OBJCXX)
    message(STATUS "CNA: Using native METAL graphics renderer")
    set(RENDERER_DIR "modules/renderers/metal")
    set(RENDERER_TARGET "cna_renderer_metal")
    list(APPEND _cna_identity_defines CNA_RENDERER_METAL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_METAL")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "SDL_GPU")
    message(STATUS "CNA: Using SDL_GPU graphics renderer")
    set(RENDERER_DIR "modules/renderers/sdl-gpu")
    set(RENDERER_TARGET "cna_renderer_sdl_gpu")
    list(APPEND _cna_identity_defines CNA_RENDERER_SDL_GPU)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SDL_GPU")
    # plans/plan_sdlgpu.md SDLGPU-92: Vulkan consumes CNA's committed SPIR-V stock shaders
    # directly. D3D12 and Metal require SDL_shadercross to translate those same blobs to DXBC or
    # MSL. Keep Linux/Android's already-native route dependency-free by default, while making the
    # dependency the default on the platforms that cannot construct the renderer without it.
    if(WIN32 OR APPLE)
        set(_cna_sdl_gpu_shadercross_default ON)
    else()
        set(_cna_sdl_gpu_shadercross_default OFF)
    endif()
    option(CNA_SDL_GPU_SHADERCROSS
           "Enable portable SPIR-V stock shaders through SDL_shadercross"
           ${_cna_sdl_gpu_shadercross_default})
    if(CNA_SDL_GPU_SHADERCROSS)
        include(cmake/ThirdPartySDLShaderCross.cmake)
        cna_configure_sdl_shadercross()
    endif()
    # plans/plan_fx.md FX-061: compiled XNA effects on this renderer go through MojoShader's own SDL_GPU
    # adapter, which emits SPIR-V -- the format this renderer already builds its pipelines from.
    # Off by default because it pulls a fetched dependency into a renderer that does not otherwise
    # need one; the capability stays false until the FX-060 shared suite passes here.
    option(CNA_SDL_GPU_COMPILED_EFFECTS
           "Build SDL_GPU support for compiled XNA Effect bytecode (plans/plan_fx.md FX-061)" OFF)
    if(CNA_SDL_GPU_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        if(CNA_SDL_GPU_SHADERCROSS)
            cna_enable_mojoshader_sdl_gpu_shadercross()
        endif()
        add_compile_definitions(CNA_SDL_GPU_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "OPENGL4")
    message(STATUS "CNA: Using OPENGL4 (real desktop OpenGL 4.x core profile) graphics renderer")
    set(RENDERER_DIR "modules/renderers/opengl4")
    set(RENDERER_TARGET "cna_renderer_opengl4")
    list(APPEND _cna_identity_defines CNA_RENDERER_OPENGL4)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_OPENGL4")
    find_package(OpenGL REQUIRED)
    # plans/plan_opengl4_modern_graphics.md GL4-0020: compiled XNA Effect bytecode through
    # MojoShader's own OpenGL adapter, asked for its desktop GLSL 1.20 dialect -- the route EasyGL's
    # desktop profile takes under CNA_EASYGL_COMPILED_EFFECTS. Off by default and shaped exactly like
    # that option, for the same reason: MojoShader is a fetched dependency this renderer does not
    # otherwise need. OPENGL4 is a real SDL-free renderer (native X11/Wayland), so in a
    # CNA_ENABLE_SDL=OFF build cna_configure_mojoshader() compiles MojoShader on the C standard
    # library rather than SDL3's.
    option(CNA_OPENGL4_COMPILED_EFFECTS
           "Build OpenGL4 support for compiled XNA Effect bytecode (plans/plan_opengl4_modern_graphics.md GL4-0020)" OFF)
    if(CNA_OPENGL4_COMPILED_EFFECTS)
        include(cmake/ThirdPartyFNA3D.cmake)
        cna_configure_mojoshader()
        add_compile_definitions(CNA_OPENGL4_COMPILED_EFFECTS)
    endif()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "GDI")
    message(STATUS "CNA: Using classic Win32 GDI 2D graphics renderer")
    set(RENDERER_DIR "modules/renderers/gdi")
    set(RENDERER_TARGET "cna_renderer_gdi")
    list(APPEND _cna_identity_defines CNA_RENDERER_GDI)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_GDI")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "FNA3D")
    message(STATUS "CNA: Using FNA3D graphics renderer (FNA-XNA/FNA3D + MojoShader)")
    set(RENDERER_DIR "modules/renderers/fna3d")
    set(RENDERER_TARGET "cna_renderer_fna3d")
    list(APPEND _cna_identity_defines CNA_RENDERER_FNA3D)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_FNA3D")
    include(cmake/ThirdPartyFNA3D.cmake)
    cna_configure_fna3d()
elseif(CNA_GRAPHICS_RENDERER STREQUAL "SVG_DOM")
    message(STATUS "CNA: Using SVG_DOM (real SVG DOM elements) graphics renderer")
    set(RENDERER_DIR "modules/renderers/svg-dom")
    set(RENDERER_TARGET "cna_renderer_svg_dom")
    list(APPEND _cna_identity_defines CNA_RENDERER_SVG_DOM)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_SVG_DOM")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "PORTABLEGL")
    message(STATUS "CNA: Using PORTABLEGL (rswinkle/PortableGL, CPU software OpenGL 3.x) graphics renderer")
    set(RENDERER_DIR "modules/renderers/portablegl")
    set(RENDERER_TARGET "cna_renderer_portablegl")
    list(APPEND _cna_identity_defines CNA_RENDERER_PORTABLEGL)
    set(CNA_RENDERER_DEFINE "CNA_RENDERER_PORTABLEGL")
    include(cmake/ThirdPartyPortableGL.cmake)
    cna_configure_portablegl()
else()
    message(FATAL_ERROR "CNA: Unknown graphics renderer: ${CNA_GRAPHICS_RENDERER}")
endif()
endmacro()

foreach(_cna_identity IN LISTS _cna_renderer_identities)
    set(CNA_GRAPHICS_RENDERER "${_cna_identity}")
    cna_configure_renderer_identity()
    list(APPEND CNA_RENDERER_TARGETS "${RENDERER_TARGET}")
    list(APPEND CNA_RENDERER_DIRS "${RENDERER_DIR}")

    # Each family's own sources guard on its identity macro, so every family gets its define on its
    # own target (applied in cna_renderer_common_setup once the target exists).
    string(REPLACE ";" "," _cna_joined_defines "${_cna_identity_defines}")
    list(APPEND CNA_RENDERER_TARGET_DEFINES "${_cna_joined_defines}")
    list(APPEND CNA_RENDERER_DEFINES "${CNA_RENDERER_DEFINE}")

    # Only the DEFAULT identity's macros are defined project-wide. That keeps a single-renderer
    # build exactly as it was, and keeps the compile-time accessors and the existing 892 test and
    # example #ifdef sites meaningful in a multi-renderer build: they describe the default. Making
    # the corpus itself renderer-agnostic is plans/plan_runtimerenderer.md phase P9.
    if(_cna_identity STREQUAL _cna_default_renderer_identity)
        foreach(_cna_define IN LISTS _cna_identity_defines)
            add_compile_definitions(${_cna_define})
        endforeach()
    endif()
endforeach()

# Restore the default identity, and leave RENDERER_TARGET/RENDERER_DIR pointing at it. Those two
# scalars are still read in ~128 places; in single-renderer mode they mean exactly what they always
# did, and in multi-renderer mode they mean "the default renderer".
set(CNA_GRAPHICS_RENDERER "${_cna_default_renderer_identity}")
list(GET CNA_RENDERER_TARGETS 0 RENDERER_TARGET)
list(GET CNA_RENDERER_DIRS 0 RENDERER_DIR)
# CNA_RENDERER_DEFINE rides cna_build_config INTERFACE, so it reaches EVERY module and every
# consumer. After the loop it would otherwise hold the LAST identity's macro rather than the
# default's -- which in a HEADLESS;SOFTWARE;STUB build meant the whole project compiled as though
# STUB were selected. It names the default renderer, like the two scalars above.
list(GET CNA_RENDERER_DEFINES 0 CNA_RENDERER_DEFINE)

# Applies the browser-context contract of the selected public renderer to a final Emscripten link.
# WebGL profiles must be exact: accepting Emscripten's WebGL 1 default for WEBGL2 makes EasyGL emit
# GLSL ES 3.00 into a WebGL 1 context, while forcing version 2 for WEBGL1 defeats that deliberately
# separate renderer. Canvas/DOM/WebGPU and other renderer families do not inherit irrelevant GL
# requirements from this helper.
function(cna_apply_emscripten_renderer_link_contract target)
    if(NOT EMSCRIPTEN)
        return()
    endif()
    if(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1")
        set(_cna_webgl_version 1)
    elseif(CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2")
        set(_cna_webgl_version 2)
    else()
        return()
    endif()
    target_link_options("${target}" PRIVATE
        "-sMIN_WEBGL_VERSION=${_cna_webgl_version}"
        "-sMAX_WEBGL_VERSION=${_cna_webgl_version}")
    set_target_properties("${target}" PROPERTIES
        CNA_EMSCRIPTEN_MIN_WEBGL_VERSION "${_cna_webgl_version}"
        CNA_EMSCRIPTEN_MAX_WEBGL_VERSION "${_cna_webgl_version}")
endfunction()
