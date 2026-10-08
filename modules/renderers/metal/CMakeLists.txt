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
    # plans/plan_apple_m4.md AM4-144: compiled XNA effects -- MojoShader's SPIR-V, SPIRV-Cross's MSL,
    # and XNA's own SpriteEffect embedded for SpriteBatch's retained vertex stage, as WebGPU does.
    if(CNA_METAL_COMPILED_EFFECTS)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(CNA_METAL_STOCK_EFFECT
            "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
        set(CNA_METAL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set(CNA_METAL_STOCK_EFFECT_HEADER
            "${CNA_METAL_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
        add_custom_command(
            OUTPUT "${CNA_METAL_STOCK_EFFECT_HEADER}"
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                    "${CNA_METAL_STOCK_EFFECT_HEADER}"
                    "SpriteEffect=${CNA_METAL_STOCK_EFFECT}"
            DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                    "${CNA_METAL_STOCK_EFFECT}"
            COMMENT "CNA: embedding the XNA SpriteEffect for Metal compiled-effect inheritance"
            VERBATIM)
        add_custom_target(cna_renderer_metal_sprite_effect
            DEPENDS "${CNA_METAL_STOCK_EFFECT_HEADER}")
        add_dependencies(${RENDERER_TARGET} cna_renderer_metal_sprite_effect)
        target_sources(${RENDERER_TARGET} PRIVATE "${CNA_METAL_STOCK_EFFECT_HEADER}")
        target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_METAL_GENERATED_DIR}")
        target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
        target_link_libraries(${RENDERER_TARGET} PRIVATE spirv-cross-msl)
    endif()
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
