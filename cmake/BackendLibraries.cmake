# Common backend interfaces
add_library(cna_backend_graphics_common INTERFACE)
target_include_directories(cna_backend_graphics_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# plan_dx.md design decision 4: D3DCommon shared core, consumed by cna_backend_graphics_d3d11 (and,
# once Phase DX12 is authorized, cna_backend_graphics_d3d12). Only built when actually needed --
# same discipline as every other on-demand backend dependency in this file.
# plan_dx9.md design decision 12: D3D9 deliberately does NOT join this condition. D3DFORMAT is a
# different enum space from DXGI_FORMAT and D3D9 has no state objects at all -- D3D9 gets its own
# D3D9FormatMapping/D3D9StateMapping/D3D9VertexDeclarations instead of expanding this shared core.
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D11" OR CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    file(GLOB D3DCOMMON_SOURCES "src/CNA/Internal/Backends/D3DCommon/*.cpp")
    add_library(cna_backend_graphics_d3dcommon STATIC ${D3DCOMMON_SOURCES})
    target_link_libraries(cna_backend_graphics_d3dcommon PUBLIC cna_backend_graphics_common SHARP_RUNTIME d3d11 dxgi)
    target_include_directories(cna_backend_graphics_d3dcommon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# plan_ascii.md design decision 2: ASCII is a thin decorator around SDL_RENDERER's own
# SdlGraphicsBackend (composition, not reimplementation) -- it needs SdlGraphicsBackend.cpp's real
# implementation compiled in even though CNA_GRAPHICS_BACKEND=SDL_RENDERER was not itself selected.
# Mirrors the D3DCommon shared-core pattern just above: a small static lib, built only when
# actually needed. CNA_BACKEND_SDL_RENDERER is deliberately NOT defined for this build, so
# SdlGraphicsBackend.cpp's own #ifdef CNA_BACKEND_SDL_RENDERER-guarded CreateGraphicsBackend() is
# compiled out here, leaving Ascii's own CreateGraphicsBackend() (guarded by CNA_BACKEND_ASCII) as
# the only factory definition -- no duplicate-symbol clash.
if(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    file(GLOB CNA_ASCII_SDLRENDERER_SOURCES "src/CNA/Internal/Backends/SdlRenderer/*.cpp")
    add_library(cna_backend_graphics_sdl_renderer_core STATIC ${CNA_ASCII_SDLRENDERER_SOURCES})
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
    target_link_libraries(cna_backend_graphics_sdl_renderer_core PRIVATE SDL3::SDL3)
    target_include_directories(cna_backend_graphics_sdl_renderer_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# GDI presents the Software backend's CPU framebuffer through classic Win32 GDI. Keep this dependency
# explicit: future Software translation units must not enter the 2D-only GDI build without review.
# GDI consumes the CPU 2D implementation through a dedicated translation unit. It deliberately
# compiles neither the Software 3D/cube resource implementations nor the 3D draw entry points;
# the full SOFTWARE backend compiles the remaining general implementation plus the shared 2D units.
set(CNA_GDI_SOFTWARE_SOURCES)
if(CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    set(CNA_GDI_SOFTWARE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareFramebufferAllocation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareTextureAllocation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareFramebuffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareTexture2D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareRenderTarget2D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend2DState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareSpriteBatch.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend2D.cpp"
    )
    # GDI-078: report the actual GDI archive boundary at configure time, so plan_gdi.md/NEXT_gdi.md
    # source/object counts can be checked against generated evidence instead of a copied number.
    list(LENGTH CNA_GDI_SOFTWARE_SOURCES CNA_GDI_SHARED_SOURCE_COUNT)
    math(EXPR CNA_GDI_ARCHIVE_TOTAL "${CNA_GDI_SHARED_SOURCE_COUNT} + 3")
    message(STATUS
        "CNA: GDI backend archive = ${CNA_GDI_SHARED_SOURCE_COUNT} shared CPU-2D sources + 3 "
        "GDI-owned units = ${CNA_GDI_ARCHIVE_TOTAL} translation units")
endif()

# Backend target
file(GLOB BACKEND_SOURCES "${BACKEND_DIR}/*.cpp")
if(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")
    # This wrapper includes SoftwareGraphicsBackend.cpp with CNA_SOFTWARE_2D_ONLY for the GDI
    # archive. The complete SOFTWARE archive compiles the implementation directly, once.
    list(REMOVE_ITEM BACKEND_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend2D.cpp")
endif()
list(APPEND BACKEND_SOURCES ${CNA_GDI_SOFTWARE_SOURCES})

# plan_dx9.md Phase D9-11 (D9-110/D9-111), design decision 16: D3D9EffectBackend.cpp calls
# D3DCompile() (the custom-ShaderEffect runtime compile path), which needs d3dcompiler -- but the
# stock D3D9 pipeline must stay d3dcompiler-free (unlike D3D11/D3D12, which link it into their
# whole backend target unconditionally, DX-58/DX-121). Excluded from the main glob and built as its
# own isolated static library instead, with d3dcompiler linked ONLY there (D9-111's own "on this
# target only"). D3D9ConstantTable.cpp (D9-110's CTAB parser) moves here too -- it has no consumer
# outside D3D9EffectBackend.cpp, and leaving it in the main glob while D3D9EffectBackend.cpp moved
# out created a genuine link-order circular dependency between the two targets (this effect target
# calling ParseConstantTableEXT() while ALSO needing to be linked INTO the main backend target for
# D3D9GraphicsBackend::CreateEffectBackend() to construct one) -- found empirically via a real
# "undefined reference to ParseConstantTableEXT" link failure across every D3D9 test binary, not
# assumed in advance. ${BACKEND_TARGET} links this target (D9-112) so
# D3D9GraphicsBackend::CreateEffectBackend() can construct one.
set(D3D9_EFFECT_BACKEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/D3D9/D3D9ConstantTable.cpp"
)
if(CNA_GRAPHICS_BACKEND STREQUAL "D3D9" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CNA/Internal/Backends/D3D9/D3D9EffectBackend.cpp")
    list(REMOVE_ITEM BACKEND_SOURCES ${D3D9_EFFECT_BACKEND_SOURCES})
    add_library(cna_backend_graphics_d3d9_effect STATIC ${D3D9_EFFECT_BACKEND_SOURCES})
    target_link_libraries(cna_backend_graphics_d3d9_effect PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
    target_link_libraries(cna_backend_graphics_d3d9_effect PRIVATE d3d9 d3dcompiler)
    target_include_directories(cna_backend_graphics_d3d9_effect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

add_library(${BACKEND_TARGET} STATIC ${BACKEND_SOURCES})
target_link_libraries(${BACKEND_TARGET} PUBLIC cna_backend_graphics_common SHARP_RUNTIME)
target_include_directories(${BACKEND_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL1")
    find_package(OpenGL REQUIRED)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "GLIDE")
    # Glide itself is dynamically loaded at runtime (glide3x.dll); SDL is used only to obtain the
    # application's existing Win32 HWND. Do not link or vendor a particular Glide emulator here.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "GDI")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 gdi32)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES" OR CNA_GRAPHICS_BACKEND STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_BACKEND STREQUAL "WEBGL1" OR CNA_GRAPHICS_BACKEND STREQUAL "WEBGL2")
    target_link_libraries(${BACKEND_TARGET} PRIVATE easy-gl SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    target_link_libraries(${BACKEND_TARGET} PUBLIC bgfx bx)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
    target_include_directories(${BACKEND_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 Vulkan::Vulkan)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WICKED")
    # plan_wicked.md WICKED-2: Wicked Engine vendors its own Vulkan headers and loads the loader
    # through volk at runtime, so no find_package(Vulkan) is needed here (unlike the VULKAN backend
    # just above). WickedEngine is PUBLIC because WickedGraphicsBackend.hpp includes
    # wiGraphicsDevice.h -- every target compiling against this backend's header needs the same
    # include directory and the same SDL3/WI_UNORDERED_MAP_TYPE compile definitions.
    target_link_libraries(${BACKEND_TARGET} PUBLIC WickedEngine)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
    if(CNA_WICKED_DXCOMPILER)
        add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CNA_WICKED_DXCOMPILER}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
            VERBATIM)
    endif()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 WebGPU::WebGPU)
    if(CNA_WEBGPU_RUNTIME_LIBRARY)
        add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
            VERBATIM)
    endif()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "MAGNUM")
    # plan_magnum.md MAGNUM-2: Magnum::GL is the whole rendering surface this backend uses, and
    # Magnum::<platform>Context supplies the OpenGL function loader Platform::GLContext needs for a
    # context CNA created itself through SDL3 (which stays the window/context owner, exactly as in
    # every other windowed backend here). CNA_MAGNUM_CONTEXT_COMPONENT is resolved per platform in
    # cmake/ThirdPartyMagnum.cmake.
    target_link_libraries(${BACKEND_TARGET} PRIVATE
        SDL3::SDL3 Magnum::GL Magnum::${CNA_MAGNUM_CONTEXT_COMPONENT} Magnum::Magnum)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
    # plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the
    # stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's
    # runtime D3DCompile() custom-ShaderEffect path (D3D11EffectBackend) -- confirmed safe to link
    # in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d11 dxgi d3dcompiler cna_backend_graphics_d3dcommon)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "FREEDIRECT")
    # plan_freedirect.md design decision 10: free-direct's own public target is the literal lowercase
    # `free-direct` (PUBLIC-links free-api::free-api, PUBLIC-exposes its own include/ dir so
    # #include <ddraw.h> resolves) -- no ALIAS namespace exists for it, unlike easy-gl's own target.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 free-direct)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D12")
    # plan_dx.md DX-100/DX-101: d3d12+dxgi alone was confirmed sufficient for device/queue/
    # command-list creation by DX-100's own spike -- start from that minimum, same discipline as
    # DX-12/design decision 3. dxguid is deliberately NOT linked (still unneeded). d3dcompiler was
    # added by DX-121 (D3D12EffectBackend's runtime D3DCompile() path) -- confirmed safe to link in
    # isolation by D3D11's own DX-14-compile/DX-58 precedent.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d12 dxgi d3dcompiler cna_backend_graphics_d3dcommon)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "CANVAS")
    # plan_canvas.md design decision 2: reuses the existing SDL-created window/canvas element,
    # so still links SDL3 even though actual rendering goes through EM_JS/Canvas2D, not SDL_Renderer.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SKIA")
    # SKIA-160: CNA_SKIA_LINK_TARGET is CNA::Skia (raster, default) or CNA::SkiaGanesh (GANESH
    # mode), set by BackendSelection.cmake's CNA_SKIA_MODE branch above -- never both; the two are
    # mutually exclusive GN builds of the same checkout and cannot be linked into one binary.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ${CNA_SKIA_LINK_TARGET})
    # SKIA-140/141: SkiaTextureBackend.cpp calls CNA::Internal::Graphics::DxtUtil/Bc7Util, which
    # live in the main CNA library, not this backend target. CNA already PUBLIC-links
    # ${BACKEND_TARGET} above (CnaLibrary.cmake), so a plain executable's link command scans
    # libCNA.a before this target's archive; a single-pass linker cannot resolve a symbol defined
    # in the earlier archive from a reference in the later one. Declaring the reverse edge here
    # makes the mutual dependency explicit so CMake's own generator repeats/groups the two
    # archives correctly, instead of leaving it to accidental transitive link order (found via a
    # real "undefined reference to Bc7Util::DecompressBc7" failure on cna_reference_dump, which
    # has no other path that happens to pull DxtUtil/Bc7Util's translation unit in first).
    target_link_libraries(${BACKEND_TARGET} PRIVATE CNA)
    # The pinned upstream raster archives are built with `skia_enable_ganesh=false` and RTTI
    # disabled. GCC/Clang's broad `undefined` group otherwise instruments virtual calls made by
    # this adapter with `vptr` checks and then requires typeinfo symbols (for example SkCanvas)
    # which those archives deliberately do not export. Keep every other UBSan check enabled and
    # exclude only the incompatible RTTI-dependent check at the third-party boundary.
    if(CNA_SANITIZE MATCHES "(^|,)undefined(,|$)"
       AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${BACKEND_TARGET} PRIVATE -fno-sanitize=vptr)
    endif()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "ASCII")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 cna_backend_graphics_sdl_renderer_core)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D9")
    # plan_dx9.md design decision 16 / D9-2: d3d9 alone was confirmed sufficient (no dxguid, no
    # dxgi -- D3D9 predates DXGI) by D9-2's own spike. No cna_backend_graphics_d3dcommon (design
    # decision 12 -- D3D9 does not share D3DCommon). d3dcompiler is deliberately NOT linked here --
    # design decision 9/16 keep the stock pipeline dependency-free; it will only be added, later, to
    # a custom-ShaderEffect-only target if Phase D9-11 is authorized (ask-first per its own row).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d9)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX1")
    # plan_dx1.md design decision 10: ddraw + dxguid (GUID storage for IID_IDirectDraw etc.) is the
    # confirmed minimal link set (DX1-0 spike) -- no free-direct, no DXVK, no d3dcompiler, no
    # d3d11/dxgi. SDL3 is linked only for window/HWND access (design decision 3), same as every
    # other backend.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX2")
    # plan_dx2.md design decision 10: same confirmed minimal link set as DX1 -- ddraw + dxguid +
    # SDL3::SDL3. No separate Direct3D import library is needed: IDirect3D2/IDirect3DDevice2 are
    # obtained purely via QueryInterface/CreateDevice on DirectDraw objects/surfaces (confirmed
    # during the DX2-0 spike, see plan_dx2.md section 1).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    # plan_dx3.md design decision 6: same confirmed minimal link set as DX1/DX2 -- ddraw + dxguid +
    # SDL3::SDL3. No separate Direct3D import library is needed here either (DX30-0 spike confirmed
    # IDirect3D2/IDirect3DDevice2 are still obtained purely via QueryInterface/CreateDevice, now off
    # an IDirectDraw2 object instead of v1 -- see plan_dx3.md section 1).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX5")
    # plan_dx5.md design decision 9: same confirmed minimal link set as DX1/DX2/DX3 -- ddraw +
    # dxguid + SDL3::SDL3. No separate Direct3D import library is needed here either (DX5-0 spike
    # confirmed IDirect3D3/IDirect3DDevice3 are still obtained purely via QueryInterface/
    # CreateDevice, now off an IDirectDraw4 object instead of v2 -- see plan_dx5.md section 1).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX6")
    # plan_dx6.md design decision 10: same confirmed minimal link set as DX1/DX2/DX3/DX5 -- ddraw +
    # dxguid + SDL3::SDL3. DX6 introduces no new interface at all, so no new link dependency either.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX7")
    # plan_dx7.md design decision 14: same confirmed minimal link set as DX1/DX2/DX3/DX5/DX6 --
    # ddraw + dxguid + SDL3::SDL3. DirectDrawCreateEx/IDirectDraw7/IDirect3D7 all resolve from the
    # same import libraries, spike-confirmed (DX7-0) with no new link dependency.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX8")
    # plan_dx8.md design decision 2/14: mingw-w64's x86_64 target ships NO real d3d8 import
    # library at all (only the unrelated libd3d8thk.a "thunk" library; only the i686/32-bit target
    # has a real one) -- DXVK's own d3d8.dll.a (D8VK, merged into DXVK 2.0+) exports the real
    # Direct3DCreate8 symbol and is linked against directly instead, spike-confirmed (DX8-0a).
    set(CNA_DX8_DXVK_LIB "/usr/lib/dxvk/wine64/d3d8.dll.a" CACHE FILEPATH
        "Path to DXVK's own d3d8 import library (exports Direct3DCreate8) -- no MinGW x86_64 import library exists for real d3d8.")
    if(NOT EXISTS "${CNA_DX8_DXVK_LIB}")
        message(FATAL_ERROR
            "CNA: DX8 backend requires DXVK's own d3d8 import library, not found at "
            "${CNA_DX8_DXVK_LIB} -- install the dxvk package (providing /usr/lib/dxvk/wine64/"
            "d3d8.dll.a) or set -DCNA_DX8_DXVK_LIB=<path> to point at it explicitly.")
    endif()
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 "${CNA_DX8_DXVK_LIB}")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D10")
    # plan_d3d10.md design decision: unlike DX8, mingw-w64 ships a REAL d3d10 import library
    # (libd3d10.a) -- no DXVK .dll.a linking hack needed. DXVK itself ships no d3d10.dll at all
    # (only d3d10core.dll); the real d3d10.dll/d3d10_1.dll come from Wine's own builtin, which
    # forward to d3d10core (overridden to DXVK's real implementation at the Wine-prefix level, not
    # a link-time concern).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d10 dxgi d3dcompiler)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL2")
    find_package(OpenGL REQUIRED)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DILIGENT")
    # plan_diligent.md DILIGENT-2: SDL3 provides the window (and the native handle Diligent's swap
    # chain is created from); every graphics call goes through the Diligent engine targets that
    # cna_configure_diligent() actually built for this platform.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
    cna_link_diligent(${BACKEND_TARGET})
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")
    # plan_sdlgpu.md SDLGPU-1: SDL_gpu.h is part of SDL3 itself (SDL_gpu.c is already compiled
    # into the same SDL3 library every other backend links against) -- no separate find_package
    # or FetchContent is needed, unlike VULKAN/WEBGPU/BGFX above.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)

    # plan_sdlgpu.md SDLGPU-42/43: SdlGpuEffectBackend needs a REAL runtime GLSL->SPIR-V compile
    # for arbitrary user ShaderEffect source (SDL_gpu only accepts precompiled bytecode) -- unlike
    # D3D11/D3D12's d3dcompiler, this environment has no libshaderc-dev package (no unversioned
    # .so symlink, no headers), only the runtime libshaderc1 package's versioned .so.1. Prefer a
    # normal find_library() first (works if a dev package IS present, e.g. other machines/CI), and
    # fall back to globbing standard library directories for the exact versioned file otherwise --
    # mirrors compile_shaders.py's own ctypes loader fallback list.
    find_library(CNA_SHADERC_LIBRARY NAMES shaderc_shared shaderc)
    if(NOT CNA_SHADERC_LIBRARY)
        file(GLOB CNA_SHADERC_CANDIDATES
            /usr/lib/*/libshaderc.so*
            /usr/lib/libshaderc.so*
            /usr/local/lib/libshaderc.so*
        )
        if(CNA_SHADERC_CANDIDATES)
            list(GET CNA_SHADERC_CANDIDATES 0 CNA_SHADERC_LIBRARY)
        endif()
    endif()
    if(NOT CNA_SHADERC_LIBRARY)
        message(FATAL_ERROR "CNA: libshaderc not found (required for SDL_GPU's runtime ShaderEffect GLSL compile, SDLGPU-42/43) -- install libshaderc1 or libshaderc-dev")
    endif()
    message(STATUS "CNA: using libshaderc at ${CNA_SHADERC_LIBRARY}")
    target_link_libraries(${BACKEND_TARGET} PRIVATE "${CNA_SHADERC_LIBRARY}")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES1")
    # plan_opengles1.md design decision 1: requires a REAL system OpenGL ES 1.1 (fixed-function
    # "Common", CM) library and Khronos headers -- e.g. Debian/Ubuntu's libgles1 + libgles-dev
    # (GLESv1_CM.so + GLES/gl.h + GLES/glext.h), or the equivalent on an embedded/Android SDK.
    # Same "hard system dependency, not vendored, FATAL_ERROR with install instructions if
    # missing" shape as VULKAN's find_package(Vulkan REQUIRED) just above, not a soft find_library
    # fallback -- this backend cannot function at all without a genuine ES1 CM implementation.
    find_library(CNA_GLESV1_CM_LIBRARY NAMES GLESv1_CM)
    find_path(CNA_GLES_INCLUDE_DIR NAMES GLES/gl.h)
    if(NOT CNA_GLESV1_CM_LIBRARY OR NOT CNA_GLES_INCLUDE_DIR)
        message(FATAL_ERROR
            "CNA: OpenGL ES 1.1 (GLESv1_CM) library/headers not found -- install libgles1 "
            "libgles-dev (Debian/Ubuntu), providing libGLESv1_CM.so plus GLES/gl.h and "
            "GLES/glext.h, or the equivalent on other distros/embedded SDKs.")
    endif()
    message(STATUS "CNA: using GLESv1_CM at ${CNA_GLESV1_CM_LIBRARY} (headers: ${CNA_GLES_INCLUDE_DIR})")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 "${CNA_GLESV1_CM_LIBRARY}")
    target_include_directories(${BACKEND_TARGET} PRIVATE "${CNA_GLES_INCLUDE_DIR}")
elseif(CNA_GRAPHICS_BACKEND STREQUAL "OPENGL4")
    # plan_opengl4.md GL4-1: real desktop OpenGL 4.x core profile via the platform's own GL
    # library (libGL/opengl32/OpenGL.framework, resolved by find_package(OpenGL) in
    # BackendSelection.cmake) plus SDL3 for window/context management -- no other new
    # third-party dependency (GL4Loader.hpp/.cpp is this backend's own hand-rolled loader for
    # the GL 1.2+ entry points a core profile needs).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 OpenGL::GL)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOKOL")
    # plan_sokol.md SOKOL-2/SOKOL-3: cna_sokol_headers (cmake/ThirdPartySokol.cmake) carries the
    # fetched sokol include directory, the CNA_SOKOL_API define sokol_gfx.h dispatches on, and --
    # for the GL APIs -- OpenGL::GL, since sokol_gfx.h has no GL loader outside Windows. SDL3
    # supplies the window and, via SDL_GL_CreateContext, the GL context sokol_gfx renders into
    # (design decision 1: CNA keeps owning the window and the game loop, so sokol_app is not used).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 cna_sokol_headers)
endif()
