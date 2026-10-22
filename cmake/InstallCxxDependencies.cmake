# SPDX-License-Identifier: MS-PL

function(cna_install_cxx_dependencies)
    # Private compiled dependencies travel with the SDK, rather than referring
    # back to this build's archives. Retain upstream export names; changing them
    # would also alter any upstream export set configured in the same project.
    foreach(_target IN ITEMS enet draco_static FNA3D mojoshader easy-gl meta-gl
            SDL3_shadercross-static spirv-cross-core spirv-cross-glsl spirv-cross-hlsl
            spirv-cross-msl spirv-cross-c)
        if(NOT TARGET ${_target})
            continue()
        endif()
        set_property(GLOBAL APPEND PROPERTY CNA_CXX_BUNDLED_TARGETS "${_target}")
        get_target_property(_header_sets ${_target} INTERFACE_HEADER_SETS)
        set(_file_sets)
        foreach(_set IN LISTS _header_sets)
            if(NOT _set MATCHES "-NOTFOUND$")
                list(APPEND _file_sets FILE_SET "${_set}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
                    COMPONENT CNACpp)
            endif()
        endforeach()
        install(TARGETS ${_target} EXPORT CNACppTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT CNACpp
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT CNACpp
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT CNACpp
            ${_file_sets})
        get_target_property(_source ${_target} SOURCE_DIR)
        if(_target STREQUAL "mojoshader")
            set(_source "${_source}/MojoShader")
        endif()
        foreach(_license IN ITEMS LICENSE LICENSE.txt COPYING)
            if(EXISTS "${_source}/${_license}")
                install(FILES "${_source}/${_license}"
                    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/${_target}" COMPONENT CNACpp)
            endif()
        endforeach()
        if(_target STREQUAL "enet")
            install(DIRECTORY "${_source}/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT CNACpp)
        elseif(_target STREQUAL "draco_static")
            install(DIRECTORY "${_source}/src/draco" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
                COMPONENT CNACpp FILES_MATCHING PATTERN "*.h")
            install(FILES "${CMAKE_BINARY_DIR}/draco/draco_features.h"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/draco" COMPONENT CNACpp)
        elseif(_target STREQUAL "FNA3D")
            install(DIRECTORY "${_source}/include/"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cna/dependencies/fna3d" COMPONENT CNACpp)
        elseif(_target STREQUAL "mojoshader")
            install(FILES "${_source}/mojoshader.h"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cna/dependencies/mojoshader" COMPONENT CNACpp)
        elseif(_target STREQUAL "meta-gl")
            install(DIRECTORY "${_source}/third_party/Khronos/GLES2"
                "${_source}/third_party/Khronos/GLES3" "${_source}/third_party/Khronos/KHR"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT CNACpp)
            install(FILES "${_source}/third_party/Khronos/LICENSE-MIT.txt"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/meta-gl" COMPONENT CNACpp)
        elseif(_target STREQUAL "SDL3_shadercross-static")
            install(DIRECTORY "${_source}/include/"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT CNACpp)
        elseif(_target STREQUAL "spirv-cross-core")
            install(DIRECTORY "${_source}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/spirv_cross"
                COMPONENT CNACpp FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
                PATTERN .git EXCLUDE PATTERN reference EXCLUDE PATTERN shaders* EXCLUDE)
        endif()
    endforeach()

    # These libraries are compiled directly into CNA rather than exported as
    # separate archives. Their source files carry the complete embedded notices.
    install(FILES "${CNA_SOURCE_DIR}/third_party/cgltf/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/cgltf" COMPONENT CNACpp)
    install(FILES "${CNA_SOURCE_DIR}/third_party/stb/stb_image.h"
        "${CNA_SOURCE_DIR}/third_party/stb/stb_image_write.h"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/stb" COMPONENT CNACpp)
    if(CNA_AUDIO_PLATFORM STREQUAL "ALSA")
        install(FILES "${CNA_SOURCE_DIR}/third_party/stb/stb_vorbis.c"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/stb" COMPONENT CNACpp)
        install(FILES "${CNA_SOURCE_DIR}/third_party/dr_libs/dr_flac.h"
            "${CNA_SOURCE_DIR}/third_party/dr_libs/dr_mp3.h"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/dr_libs" COMPONENT CNACpp)
    endif()

    if(TARGET CNA_JsonHeaders)
        get_target_property(_json_roots CNA_JsonHeaders INTERFACE_INCLUDE_DIRECTORIES)
        foreach(_root IN LISTS _json_roots)
            if(EXISTS "${_root}/nlohmann/json.hpp")
                # The producer isolates host-only portable headers with a symlink.
                # Copy its target, never publish a link back to the host include root.
                file(REAL_PATH "${_root}/nlohmann" _json_source)
                install(DIRECTORY "${_json_source}"
                    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cna/dependencies/json" COMPONENT CNACpp)
                break()
            endif()
        endforeach()
    endif()
    if(TARGET CNA_JsonHeaders OR TARGET nlohmann_json::nlohmann_json)
        install(FILES "${CNA_SOURCE_DIR}/cmake/licenses/nlohmann-json-MIT.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/nlohmann-json" COMPONENT CNACpp)
    endif()

    if(TARGET SDL3::SDL3 AND NOT CNA_USE_SYSTEM_SDL)
        # Include SDL's real relocatable packages and headers as well as its DSOs.
        # Its pkg-config files encode the producer prefix; CMake clients do not
        # use them, so do not publish misleading .pc files with this SDK.
        install(DIRECTORY "${CNA_SDL_PREBUILT_ROOT}/install/"
            DESTINATION . COMPONENT CNACpp PATTERN pkgconfig EXCLUDE)
    endif()

    set(CNA_PACKAGE_WEBGPU_LIBRARY "")
    set(CNA_PACKAGE_WEBGPU_IMPLIB "")
    if(TARGET WebGPU::WebGPU AND NOT EMSCRIPTEN)
        install(FILES "${CNA_SOURCE_DIR}/cmake/licenses/wgpu-native-MIT.txt"
            "${CNA_SOURCE_DIR}/cmake/licenses/webgpu-headers-BSD.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA/webgpu" COMPONENT CNACpp)
        get_target_property(_library WebGPU::WebGPU IMPORTED_LOCATION)
        get_target_property(_implib WebGPU::WebGPU IMPORTED_IMPLIB)
        get_target_property(_headers WebGPU::WebGPU INTERFACE_INCLUDE_DIRECTORIES)
        if(WIN32)
            set(_runtime_dir "${CMAKE_INSTALL_BINDIR}")
        else()
            set(_runtime_dir "${CMAKE_INSTALL_LIBDIR}")
        endif()
        if(_library)
            get_filename_component(_filename "${_library}" NAME)
            set(CNA_PACKAGE_WEBGPU_LIBRARY "${_runtime_dir}/${_filename}")
            install(FILES "${_library}" DESTINATION "${_runtime_dir}" COMPONENT CNACpp)
        endif()
        if(_implib)
            get_filename_component(_filename "${_implib}" NAME)
            set(CNA_PACKAGE_WEBGPU_IMPLIB "${CMAKE_INSTALL_LIBDIR}/${_filename}")
            install(FILES "${_implib}" DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT CNACpp)
        endif()
        foreach(_headers_dir IN LISTS _headers)
            install(DIRECTORY "${_headers_dir}/"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cna/dependencies/webgpu" COMPONENT CNACpp)
        endforeach()
    endif()

    get_property(_targets GLOBAL PROPERTY CNA_CXX_INSTALL_TARGETS)
    set(_links "")
    foreach(_target IN LISTS _targets)
        get_target_property(_target_links ${_target} INTERFACE_LINK_LIBRARIES)
        if(_target_links)
            string(APPEND _links ";${_target_links}")
        endif()
    endforeach()
    foreach(_entry IN ITEMS "SDL|SDL3::" "MIXER|SDL3_mixer::" "FFMPEG|PkgConfig::LIBAV"
            "FREETYPE|Freetype::" "ZLIB|ZLIB::" "CURL|CURL::" "OPUS|PkgConfig::CNA_OPUS"
            "JSON|nlohmann_json::" "CROSS_JSON|CNA_JsonHeaders" "ZSTD|CNA::Zstd"
            "SHADERC|CNA::Shaderc" "WEBGPU|WebGPU::")
        string(REPLACE "|" ";" _fields "${_entry}")
        list(GET _fields 0 _name)
        list(GET _fields 1 _pattern)
        if(_links MATCHES "${_pattern}")
            set(CNA_PACKAGE_NEEDS_${_name} ON PARENT_SCOPE)
        else()
            set(CNA_PACKAGE_NEEDS_${_name} OFF PARENT_SCOPE)
        endif()
    endforeach()
    set(CNA_PACKAGE_WEBGPU_LIBRARY "${CNA_PACKAGE_WEBGPU_LIBRARY}" PARENT_SCOPE)
    set(CNA_PACKAGE_WEBGPU_IMPLIB "${CNA_PACKAGE_WEBGPU_IMPLIB}" PARENT_SCOPE)
endfunction()
