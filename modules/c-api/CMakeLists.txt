# SPDX-License-Identifier: MS-PL

include(GNUInstallDirs)

function(cna_c_api_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(cna_c_api SHARED
    src/CnaCApi.cpp
    src/CnaCApiAudio.cpp
    src/CnaCApiColor.cpp
    src/CnaCApiContent.cpp
    src/CnaCApiCurve.cpp
    src/CnaCApiDetail.cpp
    src/CnaCApiDisplay.cpp
    src/CnaCApiEffects.cpp
    src/CnaCApiGraphics.cpp
    src/CnaCApiGraphicsResource.cpp
    src/CnaCApiGraphicsState.cpp
    src/CnaCApiGeometry.cpp
    src/CnaCApiRenderTarget.cpp
    src/CnaCApiSpriteFont.cpp
    src/CnaCApiInput.cpp
    src/CnaCApiMath.cpp
    src/CnaCApiMatrix.cpp
    src/CnaCApiPackedVectors.cpp
    src/CnaCApiQuaternion.cpp
    src/CnaCApiRuntime.cpp
    src/CnaCApiTexture.cpp
    src/CnaCApiTextureVolume.cpp
    src/CnaCApiVectors.cpp
    src/CnaCApiVertexResources.cpp
    src/CnaCApiVertexBuffer.cpp
    src/CnaCApiIndexBuffer.cpp
    src/CnaCApiVertexValues.cpp
)
add_library(CNA::CApi ALIAS cna_c_api)

set_target_properties(cna_c_api PROPERTIES
    EXPORT_NAME CApi
    OUTPUT_NAME cna_c_api
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(UNIX AND NOT APPLE)
    target_link_options(cna_c_api PRIVATE
        "LINKER:--exclude-libs,ALL"
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/CnaCApiExports.map"
    )
endif()

target_compile_definitions(cna_c_api PRIVATE CNA_C_API_BUILD)
target_include_directories(cna_c_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# Keep the initial ABI substrate narrowly linked to the canonical core module. Each later C API
# family adds the exact CNA module it adapts; linking the whole aggregate before a graphics/runtime
# adapter exists needlessly builds every renderer-facing archive.
target_link_libraries(cna_c_api PRIVATE cna_runtime)

install(TARGETS cna_c_api
    EXPORT CNACTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT CNACTargets
    FILE CNACTargets.cmake
    NAMESPACE CNA::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CNA
)

if(CNA_BUILD_TESTS)
    find_package(Threads REQUIRED)

    if(UNIX AND NOT APPLE)
        add_test(NAME CApi_Exports
            COMMAND ${CMAKE_COMMAND}
                -DCNA_C_API_LIBRARY=$<TARGET_FILE:cna_c_api>
                -DCNA_C_API_NM=${CMAKE_NM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckElfExports.cmake
        )
    endif()

    add_custom_target(cna_c_api_header_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPublicHeaders.cmake
        VERBATIM
    )
    add_test(NAME CApi_HeaderAudit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPublicHeaders.cmake
    )

    add_custom_target(cna_c_api_key_identity_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_KEYS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Keys.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKeyIdentities.cmake
        VERBATIM
    )
    add_test(NAME CApi_KeyIdentities
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_KEYS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Keys.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckKeyIdentities.cmake
    )

    add_custom_target(cna_c_api_gamepad_button_identity_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_BUTTONS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Buttons.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGamePadButtonIdentities.cmake
        VERBATIM
    )
    add_test(NAME CApi_GamePadButtonIdentities
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INPUT_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/include/CNA/C/input.h
            -DCNA_NATIVE_BUTTONS_HEADER=${PROJECT_SOURCE_DIR}/modules/input/include/Microsoft/Xna/Framework/Input/Buttons.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckGamePadButtonIdentities.cmake
    )

    add_executable(cna_c_api_abi_smoke
        tests/pure_c/AbiSmoke.c
    )
    set_target_properties(cna_c_api_abi_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_abi_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_abi_smoke)
    add_test(NAME CApi_AbiSmoke COMMAND cna_c_api_abi_smoke)

    add_executable(cna_c_api_error_smoke
        tests/pure_c/ErrorSmoke.c
    )
    set_target_properties(cna_c_api_error_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_error_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_error_smoke)
    add_test(NAME CApi_ErrorSmoke COMMAND cna_c_api_error_smoke)

    add_executable(cna_c_api_math_values_smoke
        tests/pure_c/MathValuesSmoke.c
    )
    set_target_properties(cna_c_api_math_values_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_math_values_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_math_values_smoke)
    add_test(NAME CApi_MathValuesSmoke COMMAND cna_c_api_math_values_smoke)

    add_executable(cna_c_api_vector_smoke
        tests/pure_c/VectorSmoke.c
    )
    set_target_properties(cna_c_api_vector_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vector_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_vector_smoke)
    add_test(NAME CApi_VectorSmoke COMMAND cna_c_api_vector_smoke)

    add_executable(cna_c_api_quaternion_smoke
        tests/pure_c/QuaternionSmoke.c
    )
    set_target_properties(cna_c_api_quaternion_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_quaternion_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_quaternion_smoke)
    add_test(NAME CApi_QuaternionSmoke COMMAND cna_c_api_quaternion_smoke)

    add_executable(cna_c_api_matrix_smoke
        tests/pure_c/MatrixSmoke.c
    )
    set_target_properties(cna_c_api_matrix_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_matrix_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_matrix_smoke)
    add_test(NAME CApi_MatrixSmoke COMMAND cna_c_api_matrix_smoke)

    add_executable(cna_c_api_geometry_smoke
        tests/pure_c/GeometrySmoke.c
    )
    set_target_properties(cna_c_api_geometry_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_geometry_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_geometry_smoke)
    add_test(NAME CApi_GeometrySmoke COMMAND cna_c_api_geometry_smoke)

    add_executable(cna_c_api_curve_smoke
        tests/pure_c/CurveSmoke.c
    )
    set_target_properties(cna_c_api_curve_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_curve_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_curve_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_curve_smoke)
    add_test(NAME CApi_CurveSmoke COMMAND cna_c_api_curve_smoke)

    add_executable(cna_c_api_color_smoke
        tests/pure_c/ColorSmoke.c
    )
    set_target_properties(cna_c_api_color_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_color_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_color_smoke)
    add_test(NAME CApi_ColorSmoke COMMAND cna_c_api_color_smoke)

    add_executable(cna_c_api_packed_vector_smoke
        tests/pure_c/PackedVectorSmoke.c
    )
    set_target_properties(cna_c_api_packed_vector_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_packed_vector_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_packed_vector_smoke)
    add_test(NAME CApi_PackedVectorSmoke COMMAND cna_c_api_packed_vector_smoke)

    add_executable(cna_c_api_vertex_value_smoke
        tests/pure_c/VertexValueSmoke.c
    )
    set_target_properties(cna_c_api_vertex_value_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vertex_value_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_vertex_value_smoke)
    add_test(NAME CApi_VertexValueSmoke COMMAND cna_c_api_vertex_value_smoke)

    add_executable(cna_c_api_vertex_resource_smoke
        tests/pure_c/VertexResourceSmoke.c
    )
    set_target_properties(cna_c_api_vertex_resource_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vertex_resource_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_vertex_resource_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_vertex_resource_smoke)
    add_test(NAME CApi_VertexResourceSmoke COMMAND cna_c_api_vertex_resource_smoke)

    add_executable(cna_c_api_vertex_buffer_smoke
        tests/pure_c/VertexBufferSmoke.c
    )
    set_target_properties(cna_c_api_vertex_buffer_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_vertex_buffer_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_vertex_buffer_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_vertex_buffer_smoke)
    add_test(NAME CApi_VertexBufferSmoke COMMAND cna_c_api_vertex_buffer_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_VertexBufferSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_index_buffer_smoke
        tests/pure_c/IndexBufferSmoke.c
    )
    set_target_properties(cna_c_api_index_buffer_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_index_buffer_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_index_buffer_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_index_buffer_smoke)
    add_test(NAME CApi_IndexBufferSmoke COMMAND cna_c_api_index_buffer_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_IndexBufferSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_effect_annotation_smoke
        tests/pure_c/EffectAnnotationSmoke.c
    )
    set_target_properties(cna_c_api_effect_annotation_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_annotation_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_annotation_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_annotation_smoke)
    add_test(NAME CApi_EffectAnnotationSmoke COMMAND cna_c_api_effect_annotation_smoke)

    add_executable(cna_c_api_effect_parameter_smoke
        tests/pure_c/EffectParameterSmoke.c
    )
    set_target_properties(cna_c_api_effect_parameter_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_parameter_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_parameter_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_parameter_smoke)
    add_test(NAME CApi_EffectParameterSmoke COMMAND cna_c_api_effect_parameter_smoke)

    add_executable(cna_c_api_graphics_resource_smoke
        tests/pure_c/GraphicsResourceSmoke.c
    )
    set_target_properties(cna_c_api_graphics_resource_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_resource_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_graphics_resource_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_resource_smoke)
    add_test(NAME CApi_GraphicsResourceSmoke COMMAND cna_c_api_graphics_resource_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsResourceSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_texture_smoke
        tests/pure_c/TextureSmoke.c
    )
    set_target_properties(cna_c_api_texture_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_texture_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_texture_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_texture_smoke)
    add_test(NAME CApi_TextureSmoke COMMAND cna_c_api_texture_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_TextureSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_texture_volume_smoke
        tests/pure_c/TextureVolumeSmoke.c
    )
    set_target_properties(cna_c_api_texture_volume_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_texture_volume_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_texture_volume_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_texture_volume_smoke)
    add_test(NAME CApi_TextureVolumeSmoke COMMAND cna_c_api_texture_volume_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_TextureVolumeSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_lifecycle_smoke
        tests/pure_c/LifecycleSmoke.c
    )
    set_target_properties(cna_c_api_lifecycle_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_lifecycle_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_lifecycle_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_lifecycle_smoke)
    add_test(NAME CApi_LifecycleSmoke COMMAND cna_c_api_lifecycle_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_LifecycleSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_content_smoke
        tests/pure_c/ContentSmoke.c
    )
    set_target_properties(cna_c_api_content_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_content_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_content_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_content_smoke)
    add_test(NAME CApi_ContentSmoke COMMAND cna_c_api_content_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ContentSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_graphics_surface_smoke
        tests/pure_c/GraphicsSurfaceSmoke.c
    )
    set_target_properties(cna_c_api_graphics_surface_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_surface_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_graphics_surface_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_surface_smoke)
    add_test(NAME CApi_GraphicsSurfaceSmoke COMMAND cna_c_api_graphics_surface_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsSurfaceSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_input_snapshots_smoke
        tests/pure_c/InputSnapshotsSmoke.c
    )
    set_target_properties(cna_c_api_input_snapshots_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_input_snapshots_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_input_snapshots_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_input_snapshots_smoke)
    add_test(NAME CApi_InputSnapshotsSmoke COMMAND cna_c_api_input_snapshots_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_InputSnapshotsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_smoke
        tests/pure_c/AudioSmoke.c
    )
    set_target_properties(cna_c_api_audio_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_audio_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_smoke)
    add_test(NAME CApi_AudioSmoke COMMAND cna_c_api_audio_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_AudioSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_unavailable_smoke
        tests/pure_c/AudioUnavailableSmoke.c
    )
    set_target_properties(cna_c_api_audio_unavailable_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_unavailable_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_unavailable_smoke)
    add_test(NAME CApi_AudioUnavailableSmoke COMMAND cna_c_api_audio_unavailable_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioUnavailableSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=cna_driver_that_does_not_exist;SDL_VIDEODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_AudioUnavailableSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=cna_driver_that_does_not_exist"
        )
    endif()

    add_library(cna_c_api_header_c OBJECT tests/pure_c/AbiHeaderC.c)
    set_target_properties(cna_c_api_header_c PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(cna_c_api_header_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_dependencies(cna_c_api_header_c cna_c_api_header_audit)
    add_dependencies(cna_c_api_header_c cna_c_api_key_identity_audit)
    add_dependencies(cna_c_api_header_c cna_c_api_gamepad_button_identity_audit)
    cna_c_api_enable_strict_warnings(cna_c_api_header_c)

    add_library(cna_c_api_header_cpp OBJECT tests/cpp/AbiHeaderCpp.cpp)
    target_include_directories(cna_c_api_header_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_dependencies(cna_c_api_header_cpp cna_c_api_header_audit)
    add_dependencies(cna_c_api_header_cpp cna_c_api_key_identity_audit)
    add_dependencies(cna_c_api_header_cpp cna_c_api_gamepad_button_identity_audit)
    cna_c_api_enable_strict_warnings(cna_c_api_header_cpp)

    add_executable(cna_c_api_handle_registry_test
        tests/cpp/HandleRegistryTest.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_handle_registry_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(cna_c_api_handle_registry_test PRIVATE
        Threads::Threads
        sharp_runtime_core_base
    )
    cna_c_api_enable_strict_warnings(cna_c_api_handle_registry_test)
    add_test(NAME CApi_HandleRegistry COMMAND cna_c_api_handle_registry_test)

    add_executable(cna_c_api_boundary_detail_test
        tests/cpp/BoundaryDetailTest.cpp
        src/CnaCApi.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_boundary_detail_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(cna_c_api_boundary_detail_test PRIVATE sharp_runtime_core_base)
    cna_c_api_enable_strict_warnings(cna_c_api_boundary_detail_test)
    add_test(NAME CApi_BoundaryDetail COMMAND cna_c_api_boundary_detail_test)
endif()
