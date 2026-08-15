# plan_fna3d.md: the FNA3D renderer draws through FNA3D (fetched and pinned by
# cmake/ThirdPartyFNA3D.cmake), which itself dispatches onto SDL_GPU, Direct3D 11 or OpenGL at
# runtime. SDL3 supplies the window FNA3D presents into -- the same window every other windowed
# renderer uses; FNA3D's own only dependency is that same SDL3.

# FNA3D's sole shader entry point takes a compiled Direct3D 9 Effect binary, so the vendored XNA
# stock effects (effects/, provenance in effects/README.md) are embedded into a generated header
# rather than loaded from disk at run time -- a test binary stays self-contained, exactly like the
# generated shader headers the Vulkan/SDL_GPU/sokol families carry.
set(CNA_FNA3D_STOCK_EFFECTS
    SpriteEffect
    BasicEffect
    AlphaTestEffect
    DualTextureEffect
    EnvironmentMapEffect
    SkinnedEffect)

set(_cna_fna3d_effect_args)
set(_cna_fna3d_effect_inputs)
foreach(_effect IN LISTS CNA_FNA3D_STOCK_EFFECTS)
    set(_effect_path "${CMAKE_CURRENT_SOURCE_DIR}/effects/${_effect}.fxb")
    if(NOT EXISTS "${_effect_path}")
        message(FATAL_ERROR
            "CNA: missing vendored stock effect ${_effect_path}. FNA3D can execute no other kind "
            "of shader, so the renderer cannot draw without it.")
    endif()
    list(APPEND _cna_fna3d_effect_args "${_effect}=${_effect_path}")
    list(APPEND _cna_fna3d_effect_inputs "${_effect_path}")
endforeach()

set(CNA_FNA3D_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CNA_FNA3D_STOCK_EFFECT_HEADER "${CNA_FNA3D_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    OUTPUT "${CNA_FNA3D_STOCK_EFFECT_HEADER}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/effects/embed_effects.py"
            "${CNA_FNA3D_STOCK_EFFECT_HEADER}"
            ${_cna_fna3d_effect_args}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/effects/embed_effects.py" ${_cna_fna3d_effect_inputs}
    COMMENT "CNA: embedding the FNA3D renderer's stock effect binaries"
    VERBATIM)
add_custom_target(cna_renderer_fna3d_effects DEPENDS "${CNA_FNA3D_STOCK_EFFECT_HEADER}")

cna_add_renderer()
add_dependencies(${RENDERER_TARGET} cna_renderer_fna3d_effects)
target_sources(${RENDERER_TARGET} PRIVATE "${CNA_FNA3D_STOCK_EFFECT_HEADER}")
target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_FNA3D_GENERATED_DIR}")
# cna_fna3d is PUBLIC because this module's own public headers include FNA3D.h/mojoshader.h
# (CNA/Internal/Renderers/Fna3d/Fna3dApi.hpp), so every consumer of those headers -- the graphics
# module's window-flag hook, the renderer's tests and examples -- needs the same include roots and
# MojoShader switches.
target_link_libraries(${RENDERER_TARGET} PUBLIC cna_fna3d)
# plan_fx.md FX-061: the MojoShader-to-CNA reflection and state translation is shared with every
# other backend that executes compiled effects, so it lives in common/mojoshader rather than here.
target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local.
add_subdirectory(examples)
