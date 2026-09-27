# Standalone retro effects, DebugDraw, and their portable shader packages.
file(GLOB_RECURSE CNA_GRAPHICS_EXT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_graphics_ext GraphicsExt ${CNA_GRAPHICS_EXT_SOURCES})
target_link_libraries(cna_graphics_ext PUBLIC cna_graphics_core)
cna_link_sharp_runtime(cna_graphics_ext PUBLIC Core.Base)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
