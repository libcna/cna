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

# Backend target
file(GLOB BACKEND_SOURCES "${BACKEND_DIR}/*.cpp")

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

if(CNA_GRAPHICS_BACKEND STREQUAL "SDL_RENDERER")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "EASYGL")
    target_link_libraries(${BACKEND_TARGET} PRIVATE easy-gl SDL3::SDL3)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "BGFX")
    target_link_libraries(${BACKEND_TARGET} PUBLIC bgfx bx)
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
    target_include_directories(${BACKEND_TARGET} PRIVATE ${CNA_BGFX_SHADER_INCLUDE_DIR})
elseif(CNA_GRAPHICS_BACKEND STREQUAL "VULKAN")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 Vulkan::Vulkan)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU")
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 WebGPU::WebGPU)
    if(CNA_WEBGPU_RUNTIME_LIBRARY)
        add_custom_command(TARGET ${BACKEND_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${BACKEND_TARGET}>"
            VERBATIM)
    endif()
elseif(CNA_GRAPHICS_BACKEND STREQUAL "D3D11")
    # plan_dx.md design decision 3 (DX-1): d3d11/dxgi is the confirmed minimal link set for the
    # stock offline-compiled shader pipeline. d3dcompiler is linked too, specifically for DX-58's
    # runtime D3DCompile() custom-ShaderEffect path (D3D11EffectBackend) -- confirmed safe to link
    # in isolation by DX-1/DX-14-compile's own spikes; the offline stock pipeline never calls it.
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 d3d11 dxgi d3dcompiler cna_backend_graphics_d3dcommon)
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX3")
    # plan_dx3.md design decision 10: free-direct's own public target is the literal lowercase
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
elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX30")
    # plan_dx30.md design decision 6: same confirmed minimal link set as DX1/DX2 -- ddraw + dxguid +
    # SDL3::SDL3. No separate Direct3D import library is needed here either (DX30-0 spike confirmed
    # IDirect3D2/IDirect3DDevice2 are still obtained purely via QueryInterface/CreateDevice, now off
    # an IDirectDraw2 object instead of v1 -- see plan_dx30.md section 1).
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 ddraw dxguid)
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
endif()
