# SDL_shadercross translates CNA's one committed SPIR-V stock-shader corpus to the native shader
# formats consumed by SDL_gpu's D3D12 and Metal drivers. Keep it separate from libshaderc: the
# latter compiles source to SPIR-V, while this dependency translates that stable SPIR-V at runtime.

include_guard(GLOBAL)

set(CNA_SDL_SHADERCROSS_GIT_REPOSITORY "https://github.com/libsdl-org/SDL_shadercross.git"
    CACHE STRING "Official SDL_shadercross repository used by the SDL_GPU renderer")
set(CNA_SDL_SHADERCROSS_GIT_TAG "1ff05bec573988a98ef9e0260b4da44f512b8367"
    CACHE STRING "Pinned SDL_shadercross revision used by the SDL_GPU renderer")
set(CNA_SPIRV_CROSS_GIT_REPOSITORY "https://github.com/KhronosGroup/SPIRV-Cross.git"
    CACHE STRING "SPIRV-Cross repository used by SDL_shadercross")
set(CNA_SPIRV_CROSS_GIT_TAG "vulkan-sdk-1.4.350.0"
    CACHE STRING "Pinned SPIRV-Cross revision used by SDL_shadercross")

function(cna_configure_sdl_shadercross)
    if(TARGET cna_sdl_shadercross)
        return()
    endif()

    include(FetchContent)

    # SDL_shadercross's upstream vendored mode also requires complete DXC, SPIRV-Headers and
    # SPIRV-Tools submodules even when DXC is disabled. CNA needs only the C API from SPIRV-Cross
    # for SPIR-V -> MSL/HLSL translation, so provide that target explicitly and fetch no unrelated
    # compiler/tool sources. The static closure also avoids a second runtime DLL/dylib beside SDL.
    set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_MSL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_FORCE_PIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        cna_spirv_cross
        GIT_REPOSITORY "${CNA_SPIRV_CROSS_GIT_REPOSITORY}"
        GIT_TAG "${CNA_SPIRV_CROSS_GIT_TAG}"
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE)
    FetchContent_MakeAvailable(cna_spirv_cross)

    # The package-discovery spelling uses underscores while the source target uses dashes.
    # Publishing the alias makes SDL_shadercross consume the already-configured source target
    # instead of requiring a separately installed SPIRV-Cross package.
    if(NOT TARGET spirv_cross_c)
        add_library(spirv_cross_c ALIAS spirv-cross-c)
    endif()

    set(SDLSHADERCROSS_DXC OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_TESTS OFF CACHE BOOL "" FORCE)
    set(_cna_sdl_shadercross_patch
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/sdl-shadercross-1ff05bec-disable-unused-export.patch")
    set(_cna_sdl_shadercross_patch_script
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/patches/apply-sdl-shadercross-patch.cmake")
    FetchContent_Declare(
        cna_sdl_shadercross_source
        GIT_REPOSITORY "${CNA_SDL_SHADERCROSS_GIT_REPOSITORY}"
        GIT_TAG "${CNA_SDL_SHADERCROSS_GIT_TAG}"
        GIT_SUBMODULES ""
        UPDATE_DISCONNECTED TRUE
        PATCH_COMMAND "${CMAKE_COMMAND}"
                      "-DCNA_SDL_SHADERCROSS_PATCH_FILE=${_cna_sdl_shadercross_patch}"
                      -P "${_cna_sdl_shadercross_patch_script}")

    # CMake's documented source override bypasses all download/update/patch steps. Apply the same
    # idempotent patch explicitly so offline and local-source builds have identical semantics.
    if(FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                    "-DCNA_SDL_SHADERCROSS_PATCH_FILE=${_cna_sdl_shadercross_patch}"
                    -P "${_cna_sdl_shadercross_patch_script}"
            WORKING_DIRECTORY "${FETCHCONTENT_SOURCE_DIR_CNA_SDL_SHADERCROSS_SOURCE}"
            RESULT_VARIABLE _cna_sdl_shadercross_patch_result)
        if(NOT _cna_sdl_shadercross_patch_result EQUAL 0)
            message(FATAL_ERROR
                "CNA: failed to ensure the SDL_shadercross integration patch is applied")
        endif()
    endif()
    FetchContent_MakeAvailable(cna_sdl_shadercross_source)

    if(NOT TARGET SDL3_shadercross-static)
        message(FATAL_ERROR
            "CNA: SDL_shadercross ${CNA_SDL_SHADERCROSS_GIT_TAG} did not define its static target")
    endif()

    add_library(cna_sdl_shadercross INTERFACE)
    # SDLGPU-98: SDL_shadercross-static deliberately links only SDL's header target upstream.
    # MinGW's GNU ld resolves archive references from left to right, while the wider CNA closure
    # can place SDL's import library before this archive. Rescan the two as one dependency group
    # there so ShaderCross's SDL calls are resolved regardless of an earlier SDL occurrence.
    if(MINGW)
        target_link_libraries(cna_sdl_shadercross INTERFACE
            "$<LINK_GROUP:RESCAN,SDL3_shadercross-static,SDL3::SDL3>")
    else()
        target_link_libraries(cna_sdl_shadercross INTERFACE
            SDL3_shadercross-static SDL3::SDL3)
    endif()
    message(STATUS
        "CNA: SDL_shadercross pinned at ${CNA_SDL_SHADERCROSS_GIT_TAG} with SPIRV-Cross ${CNA_SPIRV_CROSS_GIT_TAG}")
endfunction()
