# --- rlgl (standalone low-level OpenGL abstraction from raylib) ---
#
# plans/plan_rlgl.md RLGL-003/RLGL-005: fetch the official raylib source at an immutable release
# commit, but compile only src/rlgl.h and its bundled desktop-GL loader. Raylib's CMake project is
# deliberately not configured: CNA owns the window, context, game loop, input, audio, and resource
# API. The nonexistent SOURCE_SUBDIR makes FetchContent populate the checkout without calling the
# repository's top-level CMakeLists.txt.
#
# Offline builds may pass -DFETCHCONTENT_SOURCE_DIR_RLGL=/path/to/raylib. The pointed directory
# must be the raylib repository root containing src/rlgl.h.

set(CNA_RLGL_REVISION "dbc56a87da87d973a9c5baa4e7438a9d20121d28"
    CACHE STRING "Pinned raylib 6.0 commit providing standalone rlgl")
set(CNA_RLGL_SOURCE_URL
    "https://github.com/raysan5/raylib/archive/${CNA_RLGL_REVISION}.tar.gz"
    CACHE STRING "Immutable raylib source archive providing standalone rlgl")
set(CNA_RLGL_SOURCE_SHA256
    "81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126"
    CACHE STRING "SHA-256 of the pinned raylib source archive")

function(cna_configure_rlgl)
    if(TARGET cna_rlgl_headers)
        return()
    endif()

    include(FetchContent)
    FetchContent_Declare(
        rlgl
        URL            "${CNA_RLGL_SOURCE_URL}"
        URL_HASH       "SHA256=${CNA_RLGL_SOURCE_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        SOURCE_SUBDIR  "cna-standalone-rlgl-do-not-configure"
    )
    FetchContent_MakeAvailable(rlgl)

    if(NOT EXISTS "${rlgl_SOURCE_DIR}/src/rlgl.h"
            OR NOT EXISTS "${rlgl_SOURCE_DIR}/src/external/glad.h")
        message(FATAL_ERROR
            "CNA: fetched raylib at ${rlgl_SOURCE_DIR}, but standalone src/rlgl.h or its bundled "
            "GLAD loader is missing. CNA_RLGL_REVISION=${CNA_RLGL_REVISION} may not identify a "
            "compatible raylib checkout.")
    endif()

    add_library(cna_rlgl_headers INTERFACE)
    target_include_directories(cna_rlgl_headers INTERFACE "${rlgl_SOURCE_DIR}/src")
    target_compile_definitions(cna_rlgl_headers INTERFACE GRAPHICS_API_OPENGL_33)

    message(STATUS
        "CNA: standalone rlgl from raylib 6.0 pinned at ${CNA_RLGL_REVISION} (OpenGL 3.3 core)")
endfunction()
