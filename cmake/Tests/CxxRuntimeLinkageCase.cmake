# SPDX-License-Identifier: MS-PL
# The runtime-first link option (cmake/BuildPerformance.cmake) is on the link line of a program
# whose C++ runtime is shared and on no other: a program that asked for a static runtime keeps
# it, however it asked, and -static-libgcc alone does not count.

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_CXX_RUNTIME_PROBE CNA_CXX_RUNTIME_WORKDIR
                           CNA_CXX_RUNTIME_COMPILER)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_CXX_RUNTIME_WORKDIR}")
set(_failures "")

# configure(<build dir> <scenario> <cache arguments...>)
function(configure build scenario)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${CNA_CXX_RUNTIME_PROBE}" -B "${build}"
            -G "Unix Makefiles" -DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}
            -DCMAKE_CXX_COMPILER=${CNA_CXX_RUNTIME_COMPILER} ${ARGN}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${scenario}: configure failed (${_result})\n${_output}\n${_error}")
    endif()
endfunction()

# expect(<build dir> <scenario> <program> <ON|OFF>)
function(expect build scenario program expected)
    file(GLOB_RECURSE _link_txt "${build}/link.txt")
    list(FILTER _link_txt INCLUDE REGEX "/${program}\\.dir/link\\.txt$")
    if(NOT _link_txt)
        message(FATAL_ERROR "${scenario}: no link line for ${program}")
    endif()
    file(READ "${_link_txt}" _line)
    if(_line MATCHES "--push-state,--as-needed -l(stdc|c)\\+\\+ ")
        set(_present ON)
    else()
        set(_present OFF)
    endif()
    if(NOT _present STREQUAL expected)
        string(STRIP "${_line}" _line)
        string(APPEND _failures "\n  ${scenario}: ${program} has the option ${_present}, "
                                "expected ${expected}: ${_line}")
        set(_failures "${_failures}" PARENT_SCOPE)
    endif()
endfunction()

set(_build "${CNA_CXX_RUNTIME_WORKDIR}/build")
set(_reset -DCMAKE_BUILD_TYPE= -DCMAKE_CXX_FLAGS= -DCMAKE_EXE_LINKER_FLAGS=
           -DCMAKE_EXE_LINKER_FLAGS_RELEASE= -DCNA_CXX_RUNTIME_LINKAGE=AUTO)

configure("${_build}" default ${_reset})
foreach(_program IN ITEMS plain libgcc_option plain_shared)
    expect("${_build}" default ${_program} ON)
endforeach()
# config_option asks for a static runtime in Release only; a flag inside a generator expression
# counts in every configuration, which only costs the option.
foreach(_program IN ITEMS libstdcxx_option both_options static_option config_option link_flags
                          link_libraries libstdcxx_shared directory_option)
    expect("${_build}" default ${_program} OFF)
endforeach()

configure("${_build}" static-libgcc ${_reset} -DCMAKE_EXE_LINKER_FLAGS=-static-libgcc)
expect("${_build}" static-libgcc plain ON)

configure("${_build}" static-libgcc+libstdc++ ${_reset}
    "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++")
expect("${_build}" static-libgcc+libstdc++ plain OFF)
expect("${_build}" static-libgcc+libstdc++ plain_shared ON)

configure("${_build}" release-flags ${_reset} -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE=-static-libstdc++)
expect("${_build}" release-flags plain OFF)

configure("${_build}" release-flags-in-debug ${_reset} -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE=-static-libstdc++)
expect("${_build}" release-flags-in-debug plain ON)

configure("${_build}" cxx-flags ${_reset} -DCMAKE_CXX_FLAGS=-static-libstdc++)
expect("${_build}" cxx-flags plain OFF)
expect("${_build}" cxx-flags plain_shared OFF)

configure("${_build}" linkage-static ${_reset} -DCNA_CXX_RUNTIME_LINKAGE=STATIC)
expect("${_build}" linkage-static plain OFF)

configure("${_build}" linkage-shared ${_reset} -DCNA_CXX_RUNTIME_LINKAGE=SHARED)
expect("${_build}" linkage-shared libstdcxx_option ON)

# A toolchain file's initial flags, read once by a new build tree.
set(_toolchain "${CNA_CXX_RUNTIME_WORKDIR}/static-runtime-toolchain.cmake")
file(WRITE "${_toolchain}" "set(CMAKE_EXE_LINKER_FLAGS_INIT -static-libstdc++)\n")
configure("${CNA_CXX_RUNTIME_WORKDIR}/toolchain" toolchain "-DCMAKE_TOOLCHAIN_FILE=${_toolchain}")
expect("${CNA_CXX_RUNTIME_WORKDIR}/toolchain" toolchain plain OFF)

if(_failures)
    message(FATAL_ERROR "C++ runtime linkage:${_failures}")
endif()
message(STATUS "C++ runtime linkage: every program keeps the runtime it asked for")
