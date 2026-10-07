if(APPLE)
    file(GLOB CNA_WEBGPU_OBJCXX_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
endif()
cna_add_renderer(${CNA_WEBGPU_OBJCXX_SOURCES})
target_link_libraries(${RENDERER_TARGET} PRIVATE WebGPU::WebGPU)
# Under Emscripten this renderer makes WebGPU's asynchronous adapter, device, validation and map
# requests blocking by suspending the Wasm stack with Asyncify (WaitForCompletion()). The deepest of
# those suspensions, the adapter/device request inside GraphicsDevice construction, does not fit
# Emscripten's default 4 KiB unwind buffer: measured at 4,324-4,704 bytes in Debug bundles (the
# renderer benchmark, cna_house3d_demo), where the default aborted with `unreachable`. Every
# Asyncify executable of a build that compiles this renderer in -- alone or beside WEBGL2 -- gets
# an order of magnitude of headroom for game code above a suspension; other builds keep the default.
if(EMSCRIPTEN)
    target_link_options(cna_emscripten_asyncify INTERFACE -sASYNCIFY_STACK_SIZE=65536)
endif()
if(APPLE)
    target_link_libraries(${RENDERER_TARGET} PRIVATE "-framework AppKit" "-framework QuartzCore")
endif()
# plans/plan_webgpu.md WEBGPU-167: compiled XNA effects go through MojoShader's portable SPIR-V
# profile plus the translation shared with every other compiled-effect backend. Opt-in, because
# MojoShader is a fetched dependency this renderer does not otherwise need.
if(CNA_WEBGPU_COMPILED_EFFECTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CNA_WEBGPU_STOCK_EFFECT
        "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
    set(CNA_WEBGPU_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(CNA_WEBGPU_STOCK_EFFECT_HEADER
        "${CNA_WEBGPU_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
    add_custom_command(
        OUTPUT "${CNA_WEBGPU_STOCK_EFFECT_HEADER}"
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_WEBGPU_STOCK_EFFECT_HEADER}"
                "SpriteEffect=${CNA_WEBGPU_STOCK_EFFECT}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                "${CNA_WEBGPU_STOCK_EFFECT}"
        COMMENT "CNA: embedding the XNA SpriteEffect for WebGPU compiled-effect inheritance"
        VERBATIM)
    add_custom_target(cna_renderer_webgpu_sprite_effect
        DEPENDS "${CNA_WEBGPU_STOCK_EFFECT_HEADER}")
    add_dependencies(${RENDERER_TARGET} cna_renderer_webgpu_sprite_effect)
    target_sources(${RENDERER_TARGET} PRIVATE "${CNA_WEBGPU_STOCK_EFFECT_HEADER}")
    target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_WEBGPU_GENERATED_DIR}")
    target_link_libraries(${RENDERER_TARGET} PUBLIC cna_renderer_mojoshader_effect)
endif()

if(CNA_WEBGPU_RUNTIME_LIBRARY)
    add_custom_command(TARGET ${RENDERER_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:${RENDERER_TARGET}>"
        VERBATIM)
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
