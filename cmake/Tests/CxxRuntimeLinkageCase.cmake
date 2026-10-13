# SPDX-License-Identifier: MS-PL
# The runtime-first link option (cmake/BuildPerformance.cmake) is on the link line of a program
# whose C++ runtime is shared and on no other: a program that asked for a static runtime keeps
# it, however it asked and in whichever configuration it asked, and -static-libgcc alone does not
# count. Some of the programs are also linked, with mold and lld where present, and the runtime
# each one needs is read back.

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_CXX_RUNTIME_PROBE CNA_CXX_RUNTIME_WORKDIR
                           CNA_CXX_RUNTIME_COMPILER)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_CXX_RUNTIME_WORKDIR}")
set(_failures "")

# configure(<build dir> <scenario> <cache arguments...>): the output is left in _output.
function(configure build scenario)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${CNA_CXX_RUNTIME_PROBE}" -B "${build}"
            -DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}
            -DCMAKE_CXX_COMPILER=${CNA_CXX_RUNTIME_COMPILER} ${ARGN}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${scenario}: configure failed (${_result})\n${_output}\n${_error}")
    endif()
    set(_output "${_output}" PARENT_SCOPE)
endfunction()

function(fail message)
    string(APPEND _failures "\n  ${message}")
    set(_failures "${_failures}" PARENT_SCOPE)
endfunction()

# expect(<build dir> <scenario> <ON|OFF> <program...>)
function(expect build scenario expected)
    foreach(_program IN LISTS ARGN)
        file(GLOB_RECURSE _link_txt "${build}/link.txt")
        list(FILTER _link_txt INCLUDE REGEX "/${_program}\\.dir/link\\.txt$")
        if(NOT _link_txt)
            message(FATAL_ERROR "${scenario}: no link line for ${_program}")
        endif()
        file(READ "${_link_txt}" _line)
        if(_line MATCHES "--push-state,--as-needed -l(stdc|c)\\+\\+ ")
            set(_present ON)
        else()
            set(_present OFF)
        endif()
        if(NOT _present STREQUAL expected)
            string(STRIP "${_line}" _line)
            fail("${scenario}: ${_program} has the option ${_present}, expected ${expected}: ${_line}")
        endif()
    endforeach()
    set(_failures "${_failures}" PARENT_SCOPE)
endfunction()

# expect_runtime(<build dir> <scenario> <SHARED|STATIC> <program...>): links them and reads which
# C++ runtime each one needs.
function(expect_runtime build scenario expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --parallel 8 --target ${ARGN}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${scenario}: build failed (${_result})\n${_output}\n${_error}")
    endif()
    foreach(_program IN LISTS ARGN)
        execute_process(COMMAND "${_readelf}" -d "${build}/${_program}"
            RESULT_VARIABLE _result OUTPUT_VARIABLE _dynamic ERROR_QUIET)
        set(_runtime STATIC)
        if(_dynamic MATCHES "NEEDED[^\n]*libstdc\\+\\+\\.so")
            set(_runtime SHARED)
        endif()
        if(NOT _result EQUAL 0 OR NOT _runtime STREQUAL expected)
            fail("${scenario}: ${_program} links the C++ runtime ${_runtime}, expected ${expected}")
        endif()
    endforeach()
    set(_failures "${_failures}" PARENT_SCOPE)
endfunction()

set(_build "${CNA_CXX_RUNTIME_WORKDIR}/build")
set(_reset -G "Unix Makefiles" -DCMAKE_BUILD_TYPE= -DCMAKE_CXX_FLAGS= -DCMAKE_EXE_LINKER_FLAGS=
           -DCMAKE_EXE_LINKER_FLAGS_RELEASE= -DCNA_CXX_RUNTIME_LINKAGE=AUTO
           -DPROBE_STATIC_RUNTIME=OFF -DPROBE_LINKER= -DPROBE_FETCHED_CNA=OFF)
set(_shared_everywhere plain libgcc_option plain_shared config_libgcc_option bool_option
                       libgcc_policy_target system_library_target)
set(_static_everywhere libstdcxx_option both_options static_option static_pie_option
                       nostdlib_option link_flags link_libraries libstdcxx_shared directory_option
                       language_option policy_target policy_alias policy_through_module
                       flag_through_module release_linked_policy hidden_policy_target
                       hidden_plain_target)
set(_static_in_release config_option if_option list_option config_libraries release_policy_target)

configure("${_build}" default ${_reset})
expect("${_build}" default ON ${_shared_everywhere} ${_static_in_release} debug_option)
expect("${_build}" default OFF ${_static_everywhere})
if(NOT _output MATCHES "2 program\\(s\\) link imported targets defined in a subdirectory \\(hidden_policy, hidden_plain\\)[^\n]*CNA_CXX_RUNTIME_LINKAGE=SHARED")
    fail("default: no note of the programs whose imported targets CNA cannot read")
endif()

# CNA fetched into the build tree keeps its own directories.
configure("${_build}" fetched ${_reset} -DPROBE_FETCHED_CNA=ON)
expect("${_build}" fetched ON ${_shared_everywhere})
expect("${_build}" fetched OFF hidden_policy_target hidden_plain_target)
if(NOT _output MATCHES "2 program\\(s\\) link imported targets defined in a subdirectory \\(hidden_policy, hidden_plain\\)")
    fail("fetched: the note does not name exactly the two imported targets outside CNA")
endif()

configure("${_build}" debug ${_reset} -DCMAKE_BUILD_TYPE=Debug)
expect("${_build}" debug ON ${_shared_everywhere} ${_static_in_release})
expect("${_build}" debug OFF ${_static_everywhere} debug_option)

configure("${_build}" release ${_reset} -DCMAKE_BUILD_TYPE=Release)
expect("${_build}" release ON ${_shared_everywhere} debug_option)
expect("${_build}" release OFF ${_static_everywhere} ${_static_in_release})

configure("${_build}" bool ${_reset} -DPROBE_STATIC_RUNTIME=ON)
expect("${_build}" bool OFF bool_option)

configure("${_build}" static-libgcc ${_reset} -DCMAKE_EXE_LINKER_FLAGS=-static-libgcc)
expect("${_build}" static-libgcc ON plain)

configure("${_build}" static-libgcc+libstdc++ ${_reset}
    "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++")
expect("${_build}" static-libgcc+libstdc++ OFF plain)
expect("${_build}" static-libgcc+libstdc++ ON plain_shared)

configure("${_build}" release-flags ${_reset} -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE=-static-libstdc++)
expect("${_build}" release-flags OFF plain)

configure("${_build}" release-flags-in-debug ${_reset} -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE=-static-libstdc++)
expect("${_build}" release-flags-in-debug ON plain)

configure("${_build}" cxx-flags ${_reset} -DCMAKE_CXX_FLAGS=-static-libstdc++)
expect("${_build}" cxx-flags OFF plain plain_shared)

# The override decides for every program, whatever AUTO would read.
configure("${_build}" linkage-static ${_reset} -DCNA_CXX_RUNTIME_LINKAGE=STATIC)
expect("${_build}" linkage-static OFF ${_shared_everywhere} debug_option)

configure("${_build}" linkage-shared ${_reset} -DCNA_CXX_RUNTIME_LINKAGE=SHARED)
expect("${_build}" linkage-shared ON ${_static_everywhere} ${_static_in_release})
if(_output MATCHES "CNA_CXX_RUNTIME_LINKAGE=SHARED")
    fail("linkage-shared: the note of unread imported targets appears although SHARED was given")
endif()

# A toolchain file's initial flags, read once by a new build tree.
set(_toolchain "${CNA_CXX_RUNTIME_WORKDIR}/static-runtime-toolchain.cmake")
file(WRITE "${_toolchain}" "set(CMAKE_EXE_LINKER_FLAGS_INIT -static-libstdc++)\n")
configure("${CNA_CXX_RUNTIME_WORKDIR}/toolchain" toolchain -G "Unix Makefiles"
    "-DCMAKE_TOOLCHAIN_FILE=${_toolchain}")
expect("${CNA_CXX_RUNTIME_WORKDIR}/toolchain" toolchain OFF plain)

# A multi-configuration generator decides each configuration's link line separately.
find_program(_ninja NAMES ninja ninja-build)
if(_ninja)
    set(_multi "${CNA_CXX_RUNTIME_WORKDIR}/multi")
    configure("${_multi}" multi-config -G "Ninja Multi-Config" "-DCMAKE_MAKE_PROGRAM=${_ninja}")
    foreach(_config IN ITEMS Debug Release)
        file(READ "${_multi}/CMakeFiles/impl-${_config}.ninja" _rules)
        foreach(_program IN ITEMS config_option debug_option release_policy_target plain)
            string(REGEX MATCH "\nbuild ${_config}/${_program}:[^\n]*\n([ ]+[^\n]*\n)*" _edge "${_rules}")
            if(NOT _edge)
                message(FATAL_ERROR "multi-config: no ${_config} link of ${_program}")
            endif()
            set(_present OFF)
            if(_edge MATCHES "--push-state,--as-needed -lstdc\\+\\+ ")
                set(_present ON)
            endif()
            set(_expected ON)
            if((_config STREQUAL "Release" AND NOT _program MATCHES "^(debug_option|plain)$")
               OR (_config STREQUAL "Debug" AND _program STREQUAL "debug_option"))
                set(_expected OFF)
            endif()
            if(NOT _present STREQUAL _expected)
                fail("multi-config: ${_program} in ${_config} has the option ${_present}, expected ${_expected}")
            endif()
        endforeach()
    endforeach()
endif()

# Linked: the option never brings back a shared runtime the program did not ask for. GNU ld keeps a
# later -Bstatic runtime even after an as-needed shared one; lld and mold do not, so link with those.
find_program(_readelf NAMES readelf llvm-readelf)
set(_linkers "")
foreach(_linker IN ITEMS mold lld)
    find_program(_linker_path NAMES ${_linker} ld.${_linker})
    if(_linker_path)
        list(APPEND _linkers ${_linker})
    endif()
    unset(_linker_path CACHE)
endforeach()
if(_readelf AND _linkers)
    foreach(_linker IN LISTS _linkers)
        configure("${_build}" ${_linker}-debug ${_reset} -DCMAKE_BUILD_TYPE=Debug -DPROBE_LINKER=${_linker})
        expect_runtime("${_build}" ${_linker}-debug SHARED plain config_option libgcc_policy_target
            release_policy_target)
        expect_runtime("${_build}" ${_linker}-debug STATIC libstdcxx_option debug_option
            policy_target flag_through_module)
        configure("${_build}" ${_linker}-release ${_reset} -DCMAKE_BUILD_TYPE=Release -DPROBE_LINKER=${_linker})
        expect_runtime("${_build}" ${_linker}-release SHARED plain debug_option libgcc_policy_target)
        expect_runtime("${_build}" ${_linker}-release STATIC config_option if_option
            release_policy_target policy_target)
    endforeach()
else()
    message(STATUS "C++ runtime linkage: no readelf, mold or lld; the programs were not linked")
endif()

if(_failures)
    message(FATAL_ERROR "C++ runtime linkage:${_failures}")
endif()
message(STATUS "C++ runtime linkage: every program keeps the runtime it asked for (linked with: ${_linkers})")
