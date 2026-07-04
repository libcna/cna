include_guard(GLOBAL)

function(cna_configure_vendored_enet)
    if(TARGET enet)
        return()
    endif()

    set(ENET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet")

    if(NOT EXISTS "${ENET_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "CNA: ENet source not found at ${ENET_DIR}. "
            "Run: git submodule update --init third_party/enet")
    endif()

    # Suppress ENet's own cmake_minimum_required warning under CMake 3.x
    set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)

    add_subdirectory("${ENET_DIR}" enet EXCLUDE_FROM_ALL)

    # ENet's own CMakeLists.txt only links ws2_32/winmm for MinGW.
    # Also wire them up for MSVC.
    if(WIN32 AND NOT MINGW)
        target_link_libraries(enet PUBLIC ws2_32 winmm)
    endif()

    # Make enet headers available via <enet/enet.h>
    target_include_directories(enet PUBLIC "${ENET_DIR}/include")

    message(STATUS "CNA: Using vendored ENet from ${ENET_DIR}")
endfunction()
