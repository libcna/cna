# plans/plan_sdlgpu.md SDLGPU-1: SDL_gpu.h is part of SDL3 itself (SDL_gpu.c is already compiled
# into the same SDL3 library every other renderer links against) -- no separate find_package
# or FetchContent is needed, unlike VULKAN/WEBGPU/BGFX.
cna_add_renderer()
set_property(TARGET ${RENDERER_TARGET} PROPERTY REQUIRES_PLATFORM SDL3)

# plans/plan_sdlgpu.md SDLGPU-92: the built-in shader path links SDL_shadercross statically when
# configured. Windows and Apple default this on because their native SDL_gpu drivers consume
# DXBC/MSL, not SPIR-V. Linux/Android retain the direct dependency-free Vulkan route by default.
if(CNA_SDL_GPU_SHADERCROSS)
    if(NOT TARGET cna_sdl_shadercross)
        message(FATAL_ERROR
            "CNA_SDL_GPU_SHADERCROSS=ON but the pinned SDL_shadercross target was not configured")
    endif()
    target_link_libraries(${RENDERER_TARGET} PRIVATE cna_sdl_shadercross)
    target_compile_definitions(${RENDERER_TARGET} PRIVATE CNA_SDL_GPU_SHADERCROSS)
endif()

# plans/plan_fx.md FX-061: compiled XNA effects go through MojoShader's own SDL_GPU adapter plus the
# translation shared with every other compiled-effect backend. Opt-in, because MojoShader is a
# fetched dependency this renderer does not otherwise need.
if(CNA_SDL_GPU_COMPILED_EFFECTS)
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

# SDLGPU-98: keep SDL after every static archive which calls it. GNU/MinGW resolves archives in
# one pass; putting SDL first left SDL_shadercross's SDL_SetError/SDL_CreateGPU* references
# unresolved even though the correct import library was present on the link line.
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)

# plans/plan_sdlgpu.md SDLGPU-42/43/97: ShaderEffect needs a target-native runtime
# GLSL->SPIR-V compiler, and the resulting SPIR-V needs a driver that accepts it. Never let a
# cross-build discover the build host's versioned ELF library through the Linux-only fallback:
# the old unconditional glob made MinGW try to link x86_64-linux-gnu/libshaderc.so.1. D3D12 and
# Metal also cannot consume ShaderEffect's raw SPIR-V path, so they truthfully leave this optional
# CNA-specific capability disabled until that path gains native output translation. This does not
# affect ordinary compiled XNA effects, which use the separate MojoShader/ShaderCross route.
set(CNA_SDL_GPU_SHADER_EFFECTS_AVAILABLE OFF)
set(_cna_sdl_gpu_shader_effect_spirv_target ON)
if(WIN32 OR APPLE OR EMSCRIPTEN)
    set(_cna_sdl_gpu_shader_effect_spirv_target OFF)
endif()
if(_cna_sdl_gpu_shader_effect_spirv_target)
    find_library(CNA_SDL_GPU_SHADERC_LIBRARY NAMES shaderc_shared shaderc)
    if(NOT CNA_SDL_GPU_SHADERC_LIBRARY AND NOT CMAKE_CROSSCOMPILING)
        file(GLOB CNA_SDL_GPU_SHADERC_CANDIDATES
            /usr/lib/*/libshaderc.so*
            /usr/lib/libshaderc.so*
            /usr/local/lib/libshaderc.so*
        )
        if(CNA_SDL_GPU_SHADERC_CANDIDATES)
            list(GET CNA_SDL_GPU_SHADERC_CANDIDATES 0 CNA_SDL_GPU_SHADERC_LIBRARY)
        endif()
    endif()
endif()

if(_cna_sdl_gpu_shader_effect_spirv_target AND CNA_SDL_GPU_SHADERC_LIBRARY)
    set(CNA_SDL_GPU_SHADER_EFFECTS_AVAILABLE ON)
    message(STATUS
        "CNA: SDL_GPU ShaderEffect enabled with target libshaderc at ${CNA_SDL_GPU_SHADERC_LIBRARY}")
    target_link_libraries(${RENDERER_TARGET} PRIVATE "${CNA_SDL_GPU_SHADERC_LIBRARY}")
    target_compile_definitions(${RENDERER_TARGET} PRIVATE CNA_SDL_GPU_SHADER_EFFECTS)
else()
    message(STATUS
        "CNA: SDL_GPU ShaderEffect disabled (no target-native GLSL-to-driver shader path); "
        "CustomEffects will report false")
endif()
unset(_cna_sdl_gpu_shader_effect_spirv_target)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
