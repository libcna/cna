# SPDX-License-Identifier: MS-PL
include_guard(GLOBAL)
include(GNUInstallDirs)

option(CNA_INSTALL_CXX "Install the relocatable CNA C++ module SDK" OFF)
if(CNA_INSTALL_CXX)
    # The SDK ships the exact dependency closure compiled into its archives.
    # Source-only add_subdirectory consumers retain Sharp Runtime's default OFF.
    set(SHARP_RUNTIME_INSTALL ON CACHE BOOL "Install Sharp Runtime with the CNA SDK" FORCE)
endif()

function(cna_register_cxx_target target export_name)
    set_target_properties(${target} PROPERTIES EXPORT_NAME "${export_name}")
    if(CNA_INSTALL_CXX)
        get_property(_registered GLOBAL PROPERTY CNA_CXX_INSTALL_TARGETS)
        if(NOT target IN_LIST _registered)
            set_property(GLOBAL APPEND PROPERTY CNA_CXX_INSTALL_TARGETS "${target}")
            install(TARGETS ${target} EXPORT CNACppTargets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT CNACpp
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT CNACpp
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT CNACpp)
        endif()
    endif()
endfunction()

function(cna_module_include_root target visibility)
    file(RELATIVE_PATH _module "${CNA_SOURCE_DIR}/modules" "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_destination "${CMAKE_INSTALL_INCLUDEDIR}/cna/${_module}")
    target_include_directories(${target} ${visibility}
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${_destination}>")
    if(CNA_INSTALL_CXX)
        get_property(_installed GLOBAL PROPERTY CNA_CXX_INSTALL_HEADER_ROOTS)
        if(NOT _module IN_LIST _installed)
            set_property(GLOBAL APPEND PROPERTY CNA_CXX_INSTALL_HEADER_ROOTS "${_module}")
            install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
                DESTINATION "${_destination}" COMPONENT CNACpp)
        endif()
    endif()
endfunction()

function(cna_install_cxx_package)
    if(NOT CNA_INSTALL_CXX)
        return()
    endif()
    if(NOT CNA_SHARP_RUNTIME_IS_MODULAR OR NOT SHARP_RUNTIME_INSTALL)
        message(FATAL_ERROR "CNA_INSTALL_CXX requires an installable modular Sharp Runtime checkout")
    endif()
    get_target_property(_sharp_export_name sharp_runtime_headers EXPORT_NAME)
    if(NOT _sharp_export_name STREQUAL "Headers")
        message(FATAL_ERROR
            "CNA_INSTALL_CXX requires Sharp Runtime's modular install support "
            "(published next 1dc8785b or newer); its version number alone is not sufficient")
    endif()
    include(CMakePackageConfigHelpers)

    foreach(_entry IN ITEMS
            "CNA|CNA" "cna_build_config|BuildConfig" "cna_core_headers|CoreHeaders"
            "cna_cnaext|CnaExt" "cna_renderer_common|RendererCommon"
            "cna_video_ffmpeg|VideoFfmpeg" "cna_gamerservices_avatar_catalog|GamerServicesAvatarCatalog"
            "cna_shared|Shared" "cna_emscripten_exception_abi|EmscriptenExceptionAbi"
            "cna_draco|DependencyDraco" "cna_mojoshader|DependencyMojoShader"
            "cna_fna3d|DependencyFna3d" "cna_sdl_shadercross|DependencySdlShadercross")
        string(REPLACE "|" ";" _fields "${_entry}")
        list(GET _fields 0 _target)
        list(GET _fields 1 _name)
        if(TARGET ${_target})
            cna_register_cxx_target(${_target} "${_name}")
        endif()
    endforeach()
    if(TARGET cna_shared)
        set_target_properties(cna_shared PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()

    include("${CNA_SOURCE_DIR}/cmake/InstallCxxDependencies.cmake")
    cna_install_cxx_dependencies()

    get_property(_sdk_targets GLOBAL PROPERTY CNA_CXX_INSTALL_TARGETS)
    get_property(_bundled_targets GLOBAL PROPERTY CNA_CXX_BUNDLED_TARGETS)
    list(APPEND _sdk_targets ${_bundled_targets})
    sharp_runtime_get_enabled_components(_sharp_components)
    foreach(_component IN LISTS _sharp_components)
        get_target_property(_target "SharpRuntime::${_component}" ALIASED_TARGET)
        list(APPEND _sdk_targets "${_target}")
    endforeach()
    if(TARGET cna_c_api)
        list(APPEND _sdk_targets cna_c_api)
    endif()
    list(REMOVE_DUPLICATES _sdk_targets)
    set(_sdk_archives)
    foreach(_target IN LISTS _sdk_targets)
        get_target_property(_type ${_target} TYPE)
        if(_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY)$")
            list(APPEND _sdk_archives ${_target})
        endif()
    endforeach()
    add_custom_target(cna_cpp_sdk DEPENDS ${_sdk_archives})

    install(FILES "${CNA_GENERATED_INCLUDE_DIR}/CNA/Version.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cna/generated/CNA" COMPONENT CNACpp)
    install(FILES "${CNA_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/CNA" COMPONENT CNACpp)
    install(EXPORT CNACppTargets NAMESPACE CNA::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CNA" COMPONENT CNACpp)

    set(CNA_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
    set(CNA_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
    set(CNA_CPP_COMPONENTS)
    get_property(_exported_targets GLOBAL PROPERTY CNA_CXX_INSTALL_TARGETS)
    foreach(_target IN LISTS _exported_targets)
        get_target_property(_name ${_target} EXPORT_NAME)
        list(APPEND CNA_CPP_COMPONENTS "${_name}")
    endforeach()
    configure_package_config_file("${CNA_SOURCE_DIR}/cmake/CNACppConfig.cmake.in"
        "${CNA_BINARY_DIR}/CNACppConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CNA"
        PATH_VARS CNA_INSTALL_LIBDIR CNA_INSTALL_INCLUDEDIR)
    install(FILES "${CNA_BINARY_DIR}/CNACppConfig.cmake"
        "${CNA_SOURCE_DIR}/cmake/CxxRuntimeLinkage.cmake"
        "${CNA_SOURCE_DIR}/cmake/DeployRuntime.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CNA" COMPONENT CNACpp)

    # Preserve the established CNA package's C ABI version/compatibility contract,
    # independently of whether this installation also contains its C adapter.
    file(STRINGS "${CNA_SOURCE_DIR}/modules/c-api/include/CNA/C/abi.h" _abi
        REGEX "^#define CNA_ABI_VERSION_(MAJOR|MINOR|PATCH) UINT32_C\\([0-9]+\\)$")
    foreach(_part IN ITEMS MAJOR MINOR PATCH)
        foreach(_line IN LISTS _abi)
            if(_line MATCHES "^#define CNA_ABI_VERSION_${_part} UINT32_C\\(([0-9]+)\\)$")
                set(_abi_${_part} "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        if(NOT DEFINED _abi_${_part})
            message(FATAL_ERROR "Cannot read CNA package ABI version ${_part}")
        endif()
    endforeach()
    configure_package_config_file("${CNA_SOURCE_DIR}/modules/c-api/cmake/CNAConfig.cmake.in"
        "${CNA_BINARY_DIR}/CNAConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CNA"
        PATH_VARS CNA_INSTALL_LIBDIR CNA_INSTALL_INCLUDEDIR)
    write_basic_package_version_file("${CNA_BINARY_DIR}/CNAConfigVersion.cmake"
        VERSION "${_abi_MAJOR}.${_abi_MINOR}.${_abi_PATCH}" COMPATIBILITY SameMajorVersion)
    install(FILES "${CNA_BINARY_DIR}/CNAConfig.cmake" "${CNA_BINARY_DIR}/CNAConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CNA" COMPONENT CNACpp)
endfunction()
