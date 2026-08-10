# The Metal policy suites (modules/renderers/metal/tests) are deliberately host-portable and
# compile into the CnaTests corpus on every backend, so the policy headers stay reachable in
# every configuration through this unconditional interface target.
add_library(cna_renderer_metal_headers INTERFACE)
target_include_directories(cna_renderer_metal_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CNA_GRAPHICS_BACKEND STREQUAL "METAL")
    # Metal owns the renderer directly; SDL provides only the native macOS window and
    # CAMetalLayer. The Objective-C++ units join the backend archive alongside the C++ ones.
    file(GLOB CNA_METAL_OBJCXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
    cna_add_renderer_backend(${CNA_METAL_OBJCXX_SOURCES})
    target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()
