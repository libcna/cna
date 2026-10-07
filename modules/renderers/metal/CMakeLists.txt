# The Metal policy suites (modules/renderers/metal/tests) are deliberately host-portable and
# compile into the CnaTests corpus on every renderer, so the policy headers stay reachable in
# every configuration through this unconditional interface target.
add_library(cna_renderer_metal_headers INTERFACE)
target_include_directories(cna_renderer_metal_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if("METAL" IN_LIST CNA_RENDERER_IDENTITIES)
    # Metal owns the renderer directly and attaches its CAMetalLayer to the platform-provided
    # Cocoa window. The Objective-C++ units join the renderer archive alongside the C++ ones.
    file(GLOB CNA_METAL_OBJCXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
    cna_add_renderer(${CNA_METAL_OBJCXX_SOURCES})
    # plans/plan_apple_m4.md AM4-037: the view the layer lives in is AppKit's on macOS, UIKit's on iOS.
    if(CNA_APPLE_IOS)
        set(_cna_metal_view_framework "-framework UIKit")
    else()
        set(_cna_metal_view_framework "-framework AppKit")
    endif()
    target_link_libraries(${RENDERER_TARGET} PRIVATE ${_cna_metal_view_framework} "-framework Metal" "-framework QuartzCore" "-framework Foundation")
    unset(_cna_metal_view_framework)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
