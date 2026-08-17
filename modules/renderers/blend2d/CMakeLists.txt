# plan_blend2d.md: genuine CPU-raster 2D-only renderer built on the Blend2D vector graphics
# engine (cmake/ThirdPartyBlend2D.cmake configured blend2d::blend2d before this file runs, from
# RendererSelection.cmake's BLEND2D branch).
# PUBLIC (not PRIVATE): Blend2DRenderer.hpp's own public class interfaces expose BLImage/
# BLContext types directly (Blend2DNativeImageSourceEXT::NativeImageEXT() etc.), so this
# renderer's test/example executables -- which link ${RENDERER_TARGET} transitively through
# CNA -- need blend2d's include directories reachable too, not just this archive's own TUs.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PUBLIC blend2d::blend2d)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
