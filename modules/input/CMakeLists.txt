file(GLOB_RECURSE CNA_INPUT_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_input Input ${CNA_INPUT_SOURCES})
target_link_libraries(cna_input PUBLIC cna_graphics_core cna_math cna_core cna_platform)
cna_link_sharp_runtime(cna_input PUBLIC Core.Base)
target_link_libraries(cna_input PRIVATE SDL3::SDL3)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
