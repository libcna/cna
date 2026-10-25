include_guard(GLOBAL)

# third_party/enet is upstream ENet (https://github.com/lsalzman/enet) at master commit
# 5a9c537fd464b3c6d3c55e1d3bd47588faf71b42 (2026-06-23, ENet 1.3.18 and later fixes), unmodified;
# only upstream's .github/ and .gitignore are left out. The release tags still shift a signed 1 into
# bit 31 of the unsequenced window and the fragment bitmaps (undefined behaviour); master shifts an
# unsigned one, which modules/net/tests/enet_*_test.c check under UBSan. Everything CNA needs on top
# of the upstream tree is here, not in it.
function(cna_configure_vendored_enet)
    if(TARGET enet)
        return()
    endif()

    set(ENET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet")

    if(NOT EXISTS "${ENET_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "CNA: ENet source not found at ${ENET_DIR}; it is part of the CNA "
            "checkout (third_party/enet), not a submodule.")
    endif()

    # Upstream's cmake_minimum_required(2.8.12...3.20) already selects every policy its project
    # needs. Its add_library(enet) follows BUILD_SHARED_LIBS; CNA links ENet statically. Upstream
    # links ws2_32/winmm on every Windows toolchain itself.
    set(BUILD_SHARED_LIBS OFF)

    add_subdirectory("${ENET_DIR}" enet EXCLUDE_FROM_ALL)

    # Make enet headers available via <enet/enet.h>
    target_include_directories(enet PUBLIC
        "$<BUILD_INTERFACE:${ENET_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    message(STATUS "CNA: Using vendored ENet from ${ENET_DIR}")
endfunction()
