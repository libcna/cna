# plan_sdlgpu.md SDLGPU-1: SDL_gpu.h is part of SDL3 itself (SDL_gpu.c is already compiled
# into the same SDL3 library every other renderer links against) -- no separate find_package
# or FetchContent is needed, unlike VULKAN/WEBGPU/BGFX.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
set_property(TARGET ${RENDERER_TARGET} PROPERTY REQUIRES_PLATFORM SDL3)

# plan_sdlgpu.md SDLGPU-42/43: SdlGpuEffectRenderer needs a REAL runtime GLSL->SPIR-V compile
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
target_link_libraries(${RENDERER_TARGET} PRIVATE "${CNA_SHADERC_LIBRARY}")

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
