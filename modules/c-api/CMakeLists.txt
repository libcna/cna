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
    src/CnaCApiXact.cpp
    src/CnaCApiColor.cpp
    src/CnaCApiContent.cpp
    src/CnaCApiContentReaders.cpp
    src/CnaCApiCoreExt.cpp
    src/CnaCApiCurve.cpp
    src/CnaCApiDetail.cpp
    src/CnaCApiDisplay.cpp
    src/CnaCApiEffects.cpp
    src/CnaCApiGraphics.cpp
    src/CnaCApiGraphicsDevice.cpp
    src/CnaCApiGraphicsExt.cpp
    src/CnaCApiGraphicsResource.cpp
    src/CnaCApiGraphicsState.cpp
    src/CnaCApiGamerServices.cpp
    src/CnaCApiGeometry.cpp
    src/CnaCApiRenderTarget.cpp
    src/CnaCApiSpriteFont.cpp
    src/CnaCApiStorage.cpp
    src/CnaCApiInput.cpp
    src/CnaCApiInputHaptics.cpp
    src/CnaCApiInputJoystick.cpp
    src/CnaCApiInputDevices.cpp
    src/CnaCApiMath.cpp
    src/CnaCApiMedia.cpp
    src/CnaCApiMediaLibrary.cpp
    src/CnaCApiMediaPlayer.cpp
    src/CnaCApiVideo.cpp
    src/CnaCApiSensors.cpp
    src/CnaCApiDevices.cpp
    src/CnaCApiGameComponents.cpp
    src/CnaCApiGameProperties.cpp
    src/CnaCApiGraphicsDeviceManager.cpp
    src/CnaCApiMatrix.cpp
    src/CnaCApiModels.cpp
    src/CnaCApiNet.cpp
    src/CnaCApiNetGamers.cpp
    src/CnaCApiNetSessions.cpp
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
target_link_libraries(cna_c_api PRIVATE cna_core cna_runtime cna_graphics_ext cna_storage cna_content cna_media cna_devices cna_devices_ext CNA_Net CNA_GamerServices)

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

    # CBIND-037E5: the `CNA::Runtime` facade is declared and never defined, which is why its rows are
    # recorded not-applicable. This is what stops that record from going stale in silence.
    add_test(NAME CApi_UnimplementedRuntimeFacade
        COMMAND ${CMAKE_COMMAND}
            -DCNA_RUNTIME_LIBRARY=$<TARGET_FILE:cna_runtime>
            -DCNA_RUNTIME_NM=${CMAKE_NM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckUnimplementedRuntimeFacade.cmake
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

    add_executable(cna_c_api_core_ext_smoke
        tests/pure_c/CoreExtSmoke.c
    )
    set_target_properties(cna_c_api_core_ext_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_core_ext_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_core_ext_smoke)
    add_test(NAME CApi_CoreExtSmoke COMMAND cna_c_api_core_ext_smoke)

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

    add_executable(cna_c_api_graphics_device_smoke
        tests/pure_c/GraphicsDeviceSmoke.c
    )
    set_target_properties(cna_c_api_graphics_device_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_device_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_device_smoke)
    add_test(NAME CApi_GraphicsDeviceSmoke COMMAND cna_c_api_graphics_device_smoke)

    add_executable(cna_c_api_draw3d_smoke
        tests/pure_c/Draw3DSmoke.c
    )
    set_target_properties(cna_c_api_draw3d_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_draw3d_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_draw3d_smoke)
    add_test(NAME CApi_Draw3DSmoke COMMAND cna_c_api_draw3d_smoke)
    set_tests_properties(CApi_Draw3DSmoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy"
    )

    add_executable(cna_c_api_graphics_ext_smoke
        tests/pure_c/GraphicsExtSmoke.c
    )
    set_target_properties(cna_c_api_graphics_ext_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_ext_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_ext_smoke)
    add_test(NAME CApi_GraphicsExtSmoke COMMAND cna_c_api_graphics_ext_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsExtSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_storage_smoke
        tests/pure_c/StorageSmoke.c
    )
    set_target_properties(cna_c_api_storage_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_storage_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_storage_smoke)
    add_test(NAME CApi_StorageSmoke COMMAND cna_c_api_storage_smoke)
    # The storage root is derived from the platform preference path, so the suite is pinned to a
    # build-tree location instead of writing into the developer's real user data directory.
    set_tests_properties(CApi_StorageSmoke PROPERTIES
        ENVIRONMENT "XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/storage-smoke-home"
    )

    add_executable(cna_c_api_content_reader_smoke
        tests/pure_c/ContentReaderSmoke.c
    )
    set_target_properties(cna_c_api_content_reader_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_content_reader_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_content_reader_smoke)
    add_test(NAME CApi_ContentReaderSmoke COMMAND cna_c_api_content_reader_smoke)
    set_tests_properties(CApi_ContentReaderSmoke PROPERTIES
        ENVIRONMENT "XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/content-reader-smoke-home"
    )

    add_executable(cna_c_api_net_smoke
        tests/pure_c/NetSmoke.c
    )
    set_target_properties(cna_c_api_net_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_net_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_net_smoke)
    add_test(NAME CApi_NetSmoke COMMAND cna_c_api_net_smoke)

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

    add_executable(cna_c_api_effect_technique_smoke
        tests/pure_c/EffectTechniqueSmoke.c
    )
    set_target_properties(cna_c_api_effect_technique_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_technique_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_technique_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_technique_smoke)
    add_test(NAME CApi_EffectTechniqueSmoke COMMAND cna_c_api_effect_technique_smoke)

    add_executable(cna_c_api_effect_smoke
        tests/pure_c/EffectSmoke.c
    )
    set_target_properties(cna_c_api_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_effect_smoke)
    add_test(NAME CApi_EffectSmoke COMMAND cna_c_api_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_EffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_basic_effect_smoke
        tests/pure_c/BasicEffectSmoke.c
    )
    set_target_properties(cna_c_api_basic_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_basic_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_basic_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_basic_effect_smoke)
    add_test(NAME CApi_BasicEffectSmoke COMMAND cna_c_api_basic_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_BasicEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_stock_effect_smoke
        tests/pure_c/StockEffectSmoke.c
    )
    set_target_properties(cna_c_api_stock_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_stock_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_stock_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_stock_effect_smoke)
    add_test(NAME CApi_StockEffectSmoke COMMAND cna_c_api_stock_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_StockEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_skinned_effect_smoke
        tests/pure_c/SkinnedEffectSmoke.c
    )
    set_target_properties(cna_c_api_skinned_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_skinned_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_skinned_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_skinned_effect_smoke)
    add_test(NAME CApi_SkinnedEffectSmoke COMMAND cna_c_api_skinned_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SkinnedEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_pbr_effect_smoke
        tests/pure_c/PbrEffectSmoke.c
    )
    set_target_properties(cna_c_api_pbr_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_pbr_effect_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_pbr_effect_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_pbr_effect_smoke)
    add_test(NAME CApi_PbrEffectSmoke COMMAND cna_c_api_pbr_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_PbrEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_model_bone_smoke
        tests/pure_c/ModelBoneSmoke.c
    )
    set_target_properties(cna_c_api_model_bone_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_bone_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_bone_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_bone_smoke)
    add_test(NAME CApi_ModelBoneSmoke COMMAND cna_c_api_model_bone_smoke)

    add_executable(cna_c_api_model_mesh_part_smoke
        tests/pure_c/ModelMeshPartSmoke.c
    )
    set_target_properties(cna_c_api_model_mesh_part_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_mesh_part_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_mesh_part_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_mesh_part_smoke)
    add_test(NAME CApi_ModelMeshPartSmoke COMMAND cna_c_api_model_mesh_part_smoke)

    add_executable(cna_c_api_model_mesh_smoke
        tests/pure_c/ModelMeshSmoke.c
    )
    set_target_properties(cna_c_api_model_mesh_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_mesh_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_mesh_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_mesh_smoke)
    add_test(NAME CApi_ModelMeshSmoke COMMAND cna_c_api_model_mesh_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ModelMeshSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_model_smoke
        tests/pure_c/ModelSmoke.c
    )
    set_target_properties(cna_c_api_model_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_model_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_model_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_model_smoke)
    add_test(NAME CApi_ModelSmoke COMMAND cna_c_api_model_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ModelSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_morph_target_smoke
        tests/pure_c/MorphTargetSmoke.c
    )
    set_target_properties(cna_c_api_morph_target_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_morph_target_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_morph_target_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_morph_target_smoke)
    add_test(NAME CApi_MorphTargetSmoke COMMAND cna_c_api_morph_target_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MorphTargetSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_skinned_model_smoke
        tests/pure_c/SkinnedModelSmoke.c
    )
    set_target_properties(cna_c_api_skinned_model_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_skinned_model_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_skinned_model_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_skinned_model_smoke)
    add_test(NAME CApi_SkinnedModelSmoke COMMAND cna_c_api_skinned_model_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SkinnedModelSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_animation_player_smoke
        tests/pure_c/AnimationPlayerSmoke.c
    )
    set_target_properties(cna_c_api_animation_player_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_animation_player_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_animation_player_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_animation_player_smoke)
    add_test(NAME CApi_AnimationPlayerSmoke COMMAND cna_c_api_animation_player_smoke)

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
    # The manifest suite builds its own content root through the storage API, so the storage root
    # is pinned to a build-tree location; dummy audio lets the WAV load route reach its real
    # success path instead of only its unsupported one.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_ContentSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/content-smoke-home"
        )
    else()
        set_tests_properties(CApi_ContentSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/content-smoke-home"
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

    add_executable(cna_c_api_haptics_smoke
        tests/pure_c/HapticsSmoke.c
    )
    set_target_properties(cna_c_api_haptics_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_haptics_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_haptics_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_haptics_smoke)
    add_test(NAME CApi_HapticsSmoke COMMAND cna_c_api_haptics_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_HapticsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_joystick_smoke
        tests/pure_c/JoystickSmoke.c
    )
    set_target_properties(cna_c_api_joystick_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_joystick_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_joystick_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_joystick_smoke)
    add_test(NAME CApi_JoystickSmoke COMMAND cna_c_api_joystick_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_JoystickSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_input_devices_smoke
        tests/pure_c/InputDevicesSmoke.c
    )
    set_target_properties(cna_c_api_input_devices_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_input_devices_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_input_devices_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_input_devices_smoke)
    add_test(NAME CApi_InputDevicesSmoke COMMAND cna_c_api_input_devices_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_InputDevicesSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_media_smoke
        tests/pure_c/MediaSmoke.c
    )
    set_target_properties(cna_c_api_media_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_media_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_media_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_media_smoke)
    add_test(NAME CApi_MediaSmoke COMMAND cna_c_api_media_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MediaSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    # The media library scans the platform's user folders, which SDL resolves through
    # $XDG_CONFIG_HOME/user-dirs.dirs. Pointing that at a generated fixture gives the suite a
    # deterministic library instead of whatever music the developer's machine happens to hold --
    # and keeps the test from ever reading or writing a real user directory.
    set(CNA_C_API_MEDIA_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/media_fixture")
    file(MAKE_DIRECTORY "${CNA_C_API_MEDIA_FIXTURE_ROOT}/config")
    file(MAKE_DIRECTORY "${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music")
    file(MAKE_DIRECTORY "${CNA_C_API_MEDIA_FIXTURE_ROOT}/Pictures")
    file(WRITE "${CNA_C_API_MEDIA_FIXTURE_ROOT}/config/user-dirs.dirs"
        "XDG_MUSIC_DIR=\"${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music\"\n"
        "XDG_PICTURES_DIR=\"${CNA_C_API_MEDIA_FIXTURE_ROOT}/Pictures\"\n"
    )

    add_executable(cna_c_api_media_library_smoke
        tests/pure_c/MediaLibrarySmoke.c
    )
    set_target_properties(cna_c_api_media_library_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_compile_definitions(cna_c_api_media_library_smoke PRIVATE
        CNA_C_API_MEDIA_FIXTURE_MUSIC="${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music"
        CNA_C_API_MEDIA_FIXTURE_PICTURES="${CNA_C_API_MEDIA_FIXTURE_ROOT}/Pictures"
    )
    target_link_libraries(cna_c_api_media_library_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_media_library_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_media_library_smoke)
    add_executable(cna_c_api_media_player_smoke
        tests/pure_c/MediaPlayerSmoke.c
    )
    set_target_properties(cna_c_api_media_player_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_compile_definitions(cna_c_api_media_player_smoke PRIVATE
        CNA_C_API_MEDIA_FIXTURE_MUSIC="${CNA_C_API_MEDIA_FIXTURE_ROOT}/Music"
    )
    target_link_libraries(cna_c_api_media_player_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_media_player_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_media_player_smoke)
    add_executable(cna_c_api_video_smoke
        tests/pure_c/VideoSmoke.c
    )
    set_target_properties(cna_c_api_video_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_video_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_video_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_video_smoke)
    add_executable(cna_c_api_sensor_values_smoke
        tests/pure_c/SensorValuesSmoke.c
    )
    set_target_properties(cna_c_api_sensor_values_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_sensor_values_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_sensor_values_smoke)
    add_test(NAME CApi_SensorValuesSmoke COMMAND cna_c_api_sensor_values_smoke)

    add_executable(cna_c_api_sensor_device_smoke
        tests/pure_c/SensorDeviceSmoke.c
    )
    set_target_properties(cna_c_api_sensor_device_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_sensor_device_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_sensor_device_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_sensor_device_smoke)
    add_test(NAME CApi_SensorDeviceSmoke COMMAND cna_c_api_sensor_device_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SensorDeviceSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_streaming_smoke
        tests/pure_c/AudioStreamingSmoke.c
    )
    set_target_properties(cna_c_api_audio_streaming_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_streaming_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_streaming_smoke)
    add_test(NAME CApi_AudioStreamingSmoke COMMAND cna_c_api_audio_streaming_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioStreamingSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_sound_effect_smoke
        tests/pure_c/AudioSoundEffectSmoke.c
    )
    set_target_properties(cna_c_api_audio_sound_effect_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_sound_effect_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_sound_effect_smoke)
    add_test(NAME CApi_AudioSoundEffectSmoke COMMAND cna_c_api_audio_sound_effect_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_AudioSoundEffectSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_xact_smoke
        tests/pure_c/XactSmoke.c
    )
    set_target_properties(cna_c_api_xact_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_xact_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_xact_smoke)
    add_test(NAME CApi_XactSmoke COMMAND cna_c_api_xact_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_XactSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_audio_3d_smoke
        tests/pure_c/Audio3DSmoke.c
    )
    set_target_properties(cna_c_api_audio_3d_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_audio_3d_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_audio_3d_smoke)
    add_test(NAME CApi_Audio3DSmoke COMMAND cna_c_api_audio_3d_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_Audio3DSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_graphics_device_manager_smoke
        tests/pure_c/GraphicsDeviceManagerSmoke.c
    )
    set_target_properties(cna_c_api_graphics_device_manager_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_graphics_device_manager_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_graphics_device_manager_smoke)
    add_test(NAME CApi_GraphicsDeviceManagerSmoke
        COMMAND cna_c_api_graphics_device_manager_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_GraphicsDeviceManagerSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_runtime_game_smoke
        tests/pure_c/RuntimeGameSmoke.c
    )
    set_target_properties(cna_c_api_runtime_game_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_runtime_game_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_runtime_game_smoke)
    add_test(NAME CApi_RuntimeGameSmoke COMMAND cna_c_api_runtime_game_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_RuntimeGameSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_runtime_components_smoke
        tests/pure_c/RuntimeComponentsSmoke.c
    )
    set_target_properties(cna_c_api_runtime_components_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_runtime_components_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_runtime_components_smoke)
    add_test(NAME CApi_RuntimeComponentsSmoke COMMAND cna_c_api_runtime_components_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_RuntimeComponentsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_devices_smoke
        tests/pure_c/DevicesSmoke.c
    )
    set_target_properties(cna_c_api_devices_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_devices_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_devices_smoke)
    add_test(NAME CApi_DevicesSmoke COMMAND cna_c_api_devices_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_DevicesSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_executable(cna_c_api_sensor_events_smoke
        tests/pure_c/SensorEventsSmoke.c
    )
    set_target_properties(cna_c_api_sensor_events_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_sensor_events_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_sensor_events_smoke)
    add_test(NAME CApi_SensorEventsSmoke COMMAND cna_c_api_sensor_events_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_SensorEventsSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_test(NAME CApi_VideoSmoke COMMAND cna_c_api_video_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_VideoSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        )
    else()
        set_tests_properties(CApi_VideoSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy"
        )
    endif()

    add_test(NAME CApi_MediaPlayerSmoke COMMAND cna_c_api_media_player_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MediaPlayerSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    else()
        set_tests_properties(CApi_MediaPlayerSmoke PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    endif()

    add_test(NAME CApi_MediaLibrarySmoke COMMAND cna_c_api_media_library_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_MediaLibrarySmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
        )
    else()
        set_tests_properties(CApi_MediaLibrarySmoke PROPERTIES
            ENVIRONMENT "XDG_CONFIG_HOME=${CNA_C_API_MEDIA_FIXTURE_ROOT}/config"
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
        ${PROJECT_SOURCE_DIR}/modules/core/include
        ${PROJECT_SOURCE_DIR}/modules/graphics/include
        ${PROJECT_SOURCE_DIR}/modules/storage/include
        ${PROJECT_SOURCE_DIR}/modules/content/include
        ${PROJECT_SOURCE_DIR}/modules/net/include
        ${PROJECT_SOURCE_DIR}/modules/gamer-services/include
        ${PROJECT_SOURCE_DIR}/modules/devices/include
        ${PROJECT_SOURCE_DIR}/modules/audio/include
        ${PROJECT_SOURCE_DIR}/modules/math/include
    )
    target_link_libraries(cna_c_api_handle_registry_test PRIVATE
        Threads::Threads
        sharp_runtime_core_base
    )
    cna_link_sharp_runtime(cna_c_api_handle_registry_test PRIVATE Core.Base IO Runtime)
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
        ${PROJECT_SOURCE_DIR}/modules/core/include
        ${PROJECT_SOURCE_DIR}/modules/graphics/include
        ${PROJECT_SOURCE_DIR}/modules/storage/include
        ${PROJECT_SOURCE_DIR}/modules/content/include
        ${PROJECT_SOURCE_DIR}/modules/net/include
        ${PROJECT_SOURCE_DIR}/modules/gamer-services/include
        ${PROJECT_SOURCE_DIR}/modules/devices/include
        ${PROJECT_SOURCE_DIR}/modules/audio/include
        ${PROJECT_SOURCE_DIR}/modules/math/include
    )
    # The storage, network-join and CNA core exceptions are the canonical types this test throws whose constructors
    # are not header-only, so proving their conversion needs those modules on the link line.
    target_link_libraries(cna_c_api_boundary_detail_test PRIVATE
        sharp_runtime_core_base
        cna_core
        cna_storage
        CNA_Net
        cna_devices
    )
    cna_link_sharp_runtime(cna_c_api_boundary_detail_test PRIVATE Core.Base IO Runtime)
    cna_c_api_enable_strict_warnings(cna_c_api_boundary_detail_test)
    add_test(NAME CApi_BoundaryDetail COMMAND cna_c_api_boundary_detail_test)
endif()
