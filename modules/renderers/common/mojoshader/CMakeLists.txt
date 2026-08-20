# plans/plan_fx.md FX-061/FX-062/FX-063/FX-065: the MojoShader-to-CNA half of compiled effect support,
# shared by every backend that executes XNA Effect Framework bytecode. Extracted from the FNA3D
# renderer once SDL_GPU needed the same code; before that it had exactly one consumer and there was
# nothing to share.
#
# Follows the same shape as common/d3d: built only for the configurations that ask for it, and
# added by the renderers composition rather than unconditionally.
file(GLOB MOJOSHADER_EFFECT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(cna_renderer_mojoshader_effect STATIC ${MOJOSHADER_EFFECT_SOURCES})
target_link_libraries(cna_renderer_mojoshader_effect PUBLIC cna_renderer_common cna_mojoshader)
cna_link_sharp_runtime(cna_renderer_mojoshader_effect PUBLIC)
target_include_directories(cna_renderer_mojoshader_effect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Same reverse edges as every renderer target: this compiles against the graphics/math/core public
# headers and calls CNA-owned symbols.
target_link_libraries(cna_renderer_mojoshader_effect PRIVATE cna_graphics_core cna_core cna_math)
