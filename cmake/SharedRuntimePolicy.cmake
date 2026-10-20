# SPDX-License-Identifier: MS-PL
# CNA-DEMOS-ISSUE-001: COMPILE_ONLY appeared in 3.27, but cyclic static archives
# inside WHOLE_ARCHIVE require 3.30's link-feature deduplication. On 3.28/3.29 the
# otherwise identical link repeats each whole archive and fails with duplicate
# definitions. Keep older CMake usable through the existing static configuration.
set(_cna_shared_library_supported OFF)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT ANDROID AND NOT CMAKE_CROSSCOMPILING
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
    set(_cna_shared_library_supported ON)
endif()
option(CNA_SHARED_LIBRARY
    "Link the CNA runtime into one shared library (libcna.so) instead of statically into every executable"
    ${_cna_shared_library_supported})
if(CNA_SHARED_LIBRARY)
    if(NOT _cna_shared_library_supported)
        message(FATAL_ERROR
            "CNA_SHARED_LIBRARY=ON needs a native ELF GNU/Clang toolchain and CMake >= 3.30 "
            "(have ${CMAKE_SYSTEM_NAME}, ${CMAKE_CXX_COMPILER_ID}, CMake ${CMAKE_VERSION}). "
            "Cyclic WHOLE_ARCHIVE links need link-feature deduplication. "
            "Configure with -DCNA_SHARED_LIBRARY=OFF or upgrade CMake.")
    endif()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CMAKE_LINK_DEPENDS_NO_SHARED ON)
endif()
