# SPDX-License-Identifier: MS-PL
include_guard(GLOBAL)

# Add a post-build copy for one imported shared-library target. Static and interface targets do not
# carry a runtime payload. UNKNOWN_LIBRARY is accepted because some vendor packages cannot describe
# their Windows import/runtime pair more precisely.
function(_cna_deploy_imported_runtime target_name dependency_target)
    if(NOT TARGET "${dependency_target}")
        return()
    endif()
    get_target_property(_cna_dependency_type "${dependency_target}" TYPE)
    if(NOT _cna_dependency_type MATCHES "^(SHARED_LIBRARY|MODULE_LIBRARY|UNKNOWN_LIBRARY)$")
        return()
    endif()
    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${dependency_target}>"
            "$<TARGET_FILE_DIR:${target_name}>"
        VERBATIM)
endfunction()

# Installed MinGW consumers do not load CNA's source-tree ThirdPartySDL.cmake, so locate the
# toolchain runtimes here as a fallback. Source-tree consumers use the established helper instead.
function(_cna_deploy_installed_mingw_runtime target_name)
    if(NOT MINGW OR EMSCRIPTEN OR ANDROID)
        return()
    endif()
    foreach(_cna_runtime_name IN ITEMS
            libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libgcc_s_sjlj-1.dll
            libstdc++-6.dll libwinpthread-1.dll)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${_cna_runtime_name}"
            OUTPUT_VARIABLE _cna_runtime_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_cna_runtime_path AND _cna_runtime_path MATCHES "[/\\\\]"
           AND EXISTS "${_cna_runtime_path}")
            file(TO_CMAKE_PATH "${_cna_runtime_path}" _cna_runtime_path)
            add_custom_command(TARGET "${target_name}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_cna_runtime_path}"
                    "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM)
        endif()
    endforeach()
endfunction()

#[=======================================================================[.rst:
cna_deploy_runtime
------------------

.. command:: cna_deploy_runtime

  Copies the runtime payload selected by CNA next to an executable::

    cna_deploy_runtime(my_game)

  The payload can include SDL3, SDL3_mixer, a renderer-owned shared library such as wgpu-native,
  and the dynamic MinGW C++ runtime. The operation is a no-op when the selected configuration has
  no deployable payload, and repeated calls for the same target are ignored.
#]=======================================================================]
function(cna_deploy_runtime target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "cna_deploy_runtime: '${target_name}' is not a CMake target")
    endif()
    get_target_property(_cna_target_type "${target_name}" TYPE)
    if(NOT _cna_target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "cna_deploy_runtime: '${target_name}' is not an executable target")
    endif()
    get_target_property(_cna_already_configured "${target_name}" CNA_RUNTIME_DEPLOYMENT_CONFIGURED)
    if(_cna_already_configured)
        return()
    endif()
    set_property(TARGET "${target_name}" PROPERTY CNA_RUNTIME_DEPLOYMENT_CONFIGURED TRUE)

    if(COMMAND cna_copy_sdl_runtime)
        cna_copy_sdl_runtime("${target_name}")
    elseif(WIN32)
        foreach(_cna_sdl_candidates IN ITEMS
                "SDL3::SDL3-shared;SDL3::SDL3"
                "SDL3_mixer::SDL3_mixer-shared;SDL3_mixer::SDL3_mixer")
            foreach(_cna_sdl_target IN LISTS _cna_sdl_candidates)
                if(TARGET "${_cna_sdl_target}")
                    _cna_deploy_imported_runtime("${target_name}" "${_cna_sdl_target}")
                    break()
                endif()
            endforeach()
        endforeach()
    endif()

    if(COMMAND cna_copy_renderer_runtime)
        cna_copy_renderer_runtime("${target_name}")
    elseif(TARGET WebGPU::WebGPU)
        _cna_deploy_imported_runtime("${target_name}" WebGPU::WebGPU)
    endif()

    if(COMMAND cna_copy_mingw_cxx_runtime)
        cna_copy_mingw_cxx_runtime("${target_name}")
    else()
        _cna_deploy_installed_mingw_runtime("${target_name}")
    endif()
endfunction()
