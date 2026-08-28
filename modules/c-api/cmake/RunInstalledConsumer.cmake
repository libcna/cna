# SPDX-License-Identifier: MS-PL
#
# plans/plan_binding.md CBIND-041: build and run the C example the way a consumer would.
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

# CBIND-046: the static half. Where the build produces the archive its absence is a failure, not a
# skip -- a consumer project that cannot find CNA::CApiStatic means the package lost it. Where the
# build was configured without it, this says so by name rather than testing half a package silently.
# Deliberately not pre-set to "": find_program does not search when its result variable is already
# defined, so initializing it would make the search silently return nothing.
if(CNA_EXPECT_STATIC)
    find_program(_static_program hello_cna_static PATHS "${_consumer}" NO_DEFAULT_PATH)
    if(NOT _static_program)
        message(FATAL_ERROR
            "The installed package offered no CNA::CApiStatic target, so nothing linked the archive.")
    endif()
else()
    message(STATUS
        "skipped: the static archive, because this tree was configured with "
        "CNA_C_API_BUILD_STATIC=OFF")
endif()

# The caller normally names the driver, and the suite's window-creating tests name the same one it
# does. When it names none, the renderer decides: SDL's dummy video driver has no OpenGL, so a GL
# renderer cannot create a window under it and the consumer dies before it reaches the graphics
# device this test exists to reach. Only a renderer that needs no GL context can be run headless
# that way; every other one is left to the environment's own driver.
if(NOT DEFINED CNA_CONSUMER_VIDEODRIVER OR CNA_CONSUMER_VIDEODRIVER STREQUAL "")
    if(NOT CNA_GRAPHICS_RENDERER OR
       CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER" OR
       CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS" OR
       CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR
       CNA_GRAPHICS_RENDERER STREQUAL "STUB")
        set(CNA_CONSUMER_VIDEODRIVER "dummy")
    endif()
endif()

# Deliberately no LD_LIBRARY_PATH: the consumer is run in an environment that knows nothing about
# where CNA or SDL live, which is the only way to prove the installed package stands on its own.
# That is a statement about library paths, not about the video driver. This used to force
# SDL_VIDEODRIVER=dummy, under which a renderer that needs a window cannot create one, so the
# consumer failed at cna_game_create long before it could demonstrate anything -- on every machine,
# with a message about the dummy driver rather than about the package. The driver now comes from
# the caller, which passes the same one the rest of the window-creating suite uses.
set(_consumer_env)
if(CNA_CONSUMER_VIDEODRIVER)
    list(APPEND _consumer_env "SDL_VIDEODRIVER=${CNA_CONSUMER_VIDEODRIVER}")
endif()
if(CNA_CONSUMER_DISPLAY)
    list(APPEND _consumer_env "DISPLAY=${CNA_CONSUMER_DISPLAY}")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
        ${_consumer_env}
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

# The static consumer runs through the same checks: the same source, linked the other way, must
# behave identically. A difference here is an ABI difference between the two halves of the package.
if(CNA_EXPECT_STATIC AND _static_program)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env ${_consumer_env} "${_static_program}"
    RESULT_VARIABLE _static_code
    OUTPUT_VARIABLE _static_output
    ERROR_VARIABLE _static_error)
if(NOT _static_code EQUAL 0)
    message(FATAL_ERROR
        "The statically linked consumer failed (${_static_code}):\n${_static_output}\n${_static_error}")
endif()
foreach(_expected IN ITEMS "CNA C ABI" "renderer:" "game type: Microsoft.Xna.Framework.Game"
        "hello_cna finished")
    string(FIND "${_static_output}" "${_expected}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR
            "The statically linked consumer never printed \"${_expected}\":\n${_static_output}")
    endif()
endforeach()
endif()

message(STATUS "The installed CNA package built and ran a standalone C consumer, shared and static.")
