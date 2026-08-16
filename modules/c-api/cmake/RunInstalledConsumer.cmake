# SPDX-License-Identifier: MS-PL
#
# plan_binding.md CBIND-041: build and run the C example the way a consumer would.
#
# Every other test in this suite builds inside the CNA source tree, where the headers, the target
# and the library are all right there. That proves nothing about the thing a newcomer actually
# does, which is install CNA and then `find_package` it from a project that knows nothing about
# this repository. This script does that end to end:
#
#   1. install the CNACApi component into a staging prefix;
#   2. configure modules/c-api/examples/c as a standalone project against that prefix only;
#   3. build it;
#   4. run it.
#
# A failure at step 2 means the installed package is not a package. A failure at step 3 means the
# installed headers or the exported target are wrong. A failure at step 4 means the installed
# library does not load. Each is a different defect and none of them is visible from inside the
# build tree.

cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS CNA_BUILD_DIR CNA_EXAMPLE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} was not provided.")
    endif()
endforeach()

set(_stage "${CNA_WORK_DIR}/stage")
set(_consumer "${CNA_WORK_DIR}/build")

function(cna_run_step description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "${description} failed (${_code}):\n${_out}\n${_err}")
    endif()
    set(cna_last_output "${_out}" PARENT_SCOPE)
endfunction()

# Only the C API component: the full install is 113 MB of SDL and GoogleTest headers a C consumer
# has no use for, and staging it would misrepresent what the package is.
cna_run_step("Installing the CNACApi component"
    ${CMAKE_COMMAND} --install "${CNA_BUILD_DIR}" --component CNACApi --prefix "${_stage}")

# The consumer is configured with CMAKE_PREFIX_PATH and nothing else pointing at CNA. If
# find_package(CNA CONFIG) needed a source path, that would be the defect this test exists to find.
set(_configure_arguments
    -S "${CNA_EXAMPLE_DIR}"
    -B "${_consumer}"
    -DCMAKE_PREFIX_PATH=${_stage}
    -DCMAKE_BUILD_TYPE=Release
)
if(DEFINED CNA_C_COMPILER AND NOT CNA_C_COMPILER STREQUAL "")
    list(APPEND _configure_arguments -DCMAKE_C_COMPILER=${CNA_C_COMPILER})
endif()
if(DEFINED CNA_C_COMPILER_LAUNCHER AND NOT CNA_C_COMPILER_LAUNCHER STREQUAL "")
    list(APPEND _configure_arguments -DCMAKE_C_COMPILER_LAUNCHER=${CNA_C_COMPILER_LAUNCHER})
endif()
# CBIND-045: nothing about SDL is passed here any more, and that is the assertion. The package
# installs the SDL3 libraries beside libcna_c_api.so and the library's INSTALL_RPATH is $ORIGIN, so
# a consumer needs no -rpath-link to link it and no LD_LIBRARY_PATH to run it. If either becomes
# necessary again, this test is where it shows up.
set(_linker_flags "")
# A sanitized library refuses to load into a consumer that was not built the same way, so the
# sanitized tree hands its own flags to the consumer rather than being excluded from the gate. This
# is a property of the tree, not advice for a real consumer: an ordinary CNA is not sanitized.
if(DEFINED CNA_SANITIZE AND NOT CNA_SANITIZE STREQUAL "")
    list(APPEND _configure_arguments "-DCMAKE_C_FLAGS=-fsanitize=${CNA_SANITIZE}")
    set(_linker_flags "${_linker_flags} -fsanitize=${CNA_SANITIZE}")
endif()
if(NOT _linker_flags STREQUAL "")
    list(APPEND _configure_arguments "-DCMAKE_EXE_LINKER_FLAGS=${_linker_flags}")
endif()

cna_run_step("Configuring the standalone consumer" ${CMAKE_COMMAND} ${_configure_arguments})
cna_run_step("Building the standalone consumer"
    ${CMAKE_COMMAND} --build "${_consumer}" --parallel 3)

find_program(_consumer_program hello_cna PATHS "${_consumer}" NO_DEFAULT_PATH)
if(NOT _consumer_program)
    message(FATAL_ERROR "The consumer built but produced no hello_cna program in ${_consumer}.")
endif()

# Deliberately no LD_LIBRARY_PATH: the consumer is run in an environment that knows nothing about
# where CNA or SDL live, which is the only way to prove the installed package stands on its own.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
        "SDL_VIDEODRIVER=dummy"
        "${_consumer_program}"
    RESULT_VARIABLE _run_code
    OUTPUT_VARIABLE _run_output
    ERROR_VARIABLE _run_error)
message(STATUS "${_run_output}")
if(NOT _run_code EQUAL 0)
    message(FATAL_ERROR "The consumer program failed (${_run_code}):\n${_run_output}\n${_run_error}")
endif()

# The program prints what it proved. Requiring the lines is what stops it from "passing" by exiting
# zero without having reached the graphics device at all.
foreach(_expected IN ITEMS "CNA C ABI" "renderer:" "game type: Microsoft.Xna.Framework.Game"
        "hello_cna finished")
    string(FIND "${_run_output}" "${_expected}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR
            "The consumer program never printed \"${_expected}\":\n${_run_output}")
    endif()
endforeach()

message(STATUS "The installed CNA package built and ran a standalone C consumer.")
