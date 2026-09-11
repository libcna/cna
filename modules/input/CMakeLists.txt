file(GLOB_RECURSE CNA_INPUT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_input Input ${CNA_INPUT_SOURCES})
target_link_libraries(cna_input PUBLIC cna_graphics_core cna_math cna_core cna_platform)
# plans/plan_dx.md DX-252: the other half of the graphics-core <-> input archive cycle's repetition
# count -- see modules/graphics/CMakeLists.txt for why two repetitions are not enough once a
# renderer family adds a third archive (cna_renderer_d3dcommon) that reaches back into
# cna_graphics_core.
set_target_properties(cna_input PROPERTIES LINK_INTERFACE_MULTIPLICITY 3)
cna_link_sharp_runtime(cna_input PUBLIC Core.Base)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
