# CNA graphics extensions beyond the XNA 4.0 contract (PbrMaterial, CRTEffect,
# DepthEffect, AsciiPostProcessEffect, RenderPipelineSettings and their CNA/Graphics headers)
# -- the complete implementation of the former cna_noxna STATIC library. CNA::CnaExt (renamed
# from CNA::NoXna in the 2026-08 CNAEXT naming normalization) survives as an
# INTERFACE umbrella over the extension modules (modules/CMakeLists.txt).
#
# AsciiPostProcessEffect (and its internal CNA/Internal/Graphics/Ascii/ quantizer/font-atlas
# helpers) migrated here from the former public ASCII graphics-renderer identity
# (CNA_GRAPHICS_RENDERER=ASCII, removed) -- see docs/ascii-post-process-effect.md.
file(GLOB_RECURSE CNA_GRAPHICS_EXT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_graphics_ext GraphicsExt ${CNA_GRAPHICS_EXT_SOURCES})
target_link_libraries(cna_graphics_ext PUBLIC cna_graphics_core)
cna_link_sharp_runtime(cna_graphics_ext PUBLIC Core.Base)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
