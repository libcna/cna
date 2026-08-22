# SPDX-License-Identifier: MS-PL

# Copies runtime libraries owned by the selected renderer next to an executable.
# Renderer modules are static libraries, so a POST_BUILD command attached to a
# renderer target cannot know where a consuming application's executable lives.
function(cna_copy_renderer_runtime target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "cna_copy_renderer_runtime: '${target_name}' is not a CMake target")
    endif()

    set(_cna_renderer_runtime_libraries)
    if(CNA_WICKED_DXCOMPILER)
        list(APPEND _cna_renderer_runtime_libraries "${CNA_WICKED_DXCOMPILER}")
    endif()
    if(CNA_WEBGPU_RUNTIME_LIBRARY)
        list(APPEND _cna_renderer_runtime_libraries "${CNA_WEBGPU_RUNTIME_LIBRARY}")
    endif()

    foreach(_cna_renderer_runtime_library IN LISTS _cna_renderer_runtime_libraries)
        add_custom_command(TARGET "${target_name}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_cna_renderer_runtime_library}"
                "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM)
    endforeach()
endfunction()
