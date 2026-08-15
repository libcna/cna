# The Glide ABI/policy suites (modules/renderers/glide/tests) are deliberately host-portable
# and compile into the CnaTests corpus on every renderer, so the ABI/policy headers stay
# reachable in every configuration through this unconditional interface target.
add_library(cna_renderer_glide_headers INTERFACE)
target_include_directories(cna_renderer_glide_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CNA_GRAPHICS_RENDERER STREQUAL "GLIDE")
    # Glide itself is dynamically loaded at runtime (glide3x.dll) and receives the application's
    # existing Win32 HWND. Do not link or vendor a particular Glide emulator here.
    cna_add_renderer()
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
