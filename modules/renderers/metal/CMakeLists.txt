# The Metal policy suites (modules/renderers/metal/tests) are deliberately host-portable and
# compile into the CnaTests corpus on every renderer, so the policy headers stay reachable in
# every configuration through this unconditional interface target.
add_library(cna_renderer_metal_headers INTERFACE)
target_include_directories(cna_renderer_metal_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(CNA_GRAPHICS_RENDERER STREQUAL "METAL")
    # Metal owns the renderer directly and attaches its CAMetalLayer to the platform-provided
    # Cocoa window. The Objective-C++ units join the renderer archive alongside the C++ ones.
    file(GLOB CNA_METAL_OBJCXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
    cna_add_renderer(${CNA_METAL_OBJCXX_SOURCES})
    target_link_libraries(${RENDERER_TARGET} PRIVATE "-framework AppKit" "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
