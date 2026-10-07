if("SOFTWARE" IN_LIST CNA_RENDERER_IDENTITIES)
    file(GLOB _cna_software_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    # plans/plan_runtimerenderer.md RTR-P6-6: derive this family's own target rather than reading the
    # global scalar, which names the build DEFAULT and is only correct here because the caller
    # happens to have overwritten it first.
    cna_renderer_target_name(_cna_software_target)
    add_library(${_cna_software_target} STATIC ${_cna_software_sources})
    cna_renderer_common_setup(${_cna_software_target})
    # plans/plan_apple_m4.md AM4-053: this rasterizer is XNA's pixel oracle, and its coverage,
    # texel selection and blending are exact float comparisons. Clang 14+ and GCC contract
    # `a * b + c` into a fused multiply-add wherever the target has one (every arm64), which
    # moves results across those exact boundaries; XNA itself never fused. An x86-64 build
    # without -mfma has no FMA to contract into, so this changes nothing there (AM4-046's
    # reasoning for cna_math).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${_cna_software_target} PRIVATE -ffp-contract=off)
    endif()
    if(CNA_SOFTWARE_COMPILED_EFFECTS)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(CNA_SOFTWARE_STOCK_EFFECT
            "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/SpriteEffect.fxb")
        set(CNA_SOFTWARE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set(CNA_SOFTWARE_STOCK_EFFECT_HEADER
            "${CNA_SOFTWARE_GENERATED_DIR}/Fna3dStockEffectBlobs.hpp")
        add_custom_command(
            OUTPUT "${CNA_SOFTWARE_STOCK_EFFECT_HEADER}"
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                    "${CNA_SOFTWARE_STOCK_EFFECT_HEADER}"
                    "SpriteEffect=${CNA_SOFTWARE_STOCK_EFFECT}"
            DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/../fna3d/effects/embed_effects.py"
                    "${CNA_SOFTWARE_STOCK_EFFECT}"
            COMMENT "CNA: embedding the XNA SpriteEffect for Software compiled-effect inheritance"
            VERBATIM)
        add_custom_target(cna_renderer_software_sprite_effect
            DEPENDS "${CNA_SOFTWARE_STOCK_EFFECT_HEADER}")
        add_dependencies(${_cna_software_target} cna_renderer_software_sprite_effect)
        target_sources(${_cna_software_target} PRIVATE "${CNA_SOFTWARE_STOCK_EFFECT_HEADER}")
        target_include_directories(${_cna_software_target} PRIVATE
            "${CNA_SOFTWARE_GENERATED_DIR}")
        target_compile_definitions(${_cna_software_target} PRIVATE CNA_SOFTWARE_COMPILED_EFFECTS)
        target_link_libraries(${_cna_software_target} PRIVATE cna_renderer_mojoshader_effect)
    endif()
    set(RENDERER_TARGET "${_cna_software_target}")
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
