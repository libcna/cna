include_guard(GLOBAL)

# Collapses the add_test() + set_tests_properties() pair repeated at every one of CNA's
# ~600 per-renderer CTest registrations into a single call. Each renderer keeps its own
# cna_<renderer>_test(target src) macro for add_executable()+target_link_libraries() --
# linking differs enough per renderer (Wine/DXVK wrapping, -Wl,--start-group circular-
# dependency links, extra libs) that unifying it isn't worth the risk -- this only
# unifies the registration half, which is identical logic across every renderer.
#
# cna_register_renderer_test(NAME <ctest-name> COMMAND <command...>
#                            [TIMEOUT <seconds>] [LABELS <label...>]
#                            [ENVIRONMENT <NAME=value;...>] [WORKING_DIRECTORY <dir>]
#                            [SKIP_REGULAR_EXPRESSION <pattern>])
#
# ENVIRONMENT/LABELS values routinely contain literal semicolons (e.g.
# "SDL_VIDEODRIVER=x11;DISPLAY=:0", "GraphicsSmoke;WebGPU"). Those must be
# \;-escaped before going into the intermediate _cna_test_props list, otherwise
# list(APPEND) flattens them into extra list elements and the final
# set_tests_properties(... PROPERTIES ${_cna_test_props}) call receives a
# corrupted, unbalanced argument list.
function(cna_register_renderer_test)
    cmake_parse_arguments(T "" "NAME;TIMEOUT;WORKING_DIRECTORY;SKIP_REGULAR_EXPRESSION" "COMMAND;LABELS;ENVIRONMENT" ${ARGN})

    # WEBGPU-150: opt-in launcher for display/GPU-backed tests. When CNA_RENDERER_GPU_TEST_LAUNCHER is
    # set (currently only by the WebGPU examples), any single-executable test whose ENVIRONMENT selects
    # a DISPLAY is routed through that launcher, which returns the CTest skip code (77) when the display
    # or GPU adapter is unavailable and retries a transient X11 error -- so a headless run SKIPs those
    # tests instead of aborting. Tests with no DISPLAY env (pure-CMake unit tests) and multi-command
    # tests are registered unchanged. SKIP_RETURN_CODE 77 is already applied directory-wide.
    list(LENGTH T_COMMAND _cna_cmd_len)
    set(_cna_wrap_gpu FALSE)
    # Only reference ${T_COMMAND} inside the single-element guard: a multi-token command (e.g. the
    # native smoke test's `cmake -D... -P ...`) would make `if(TARGET ${T_COMMAND})` a parse error.
    if(DEFINED CNA_RENDERER_GPU_TEST_LAUNCHER AND _cna_cmd_len EQUAL 1 AND "${T_ENVIRONMENT}" MATCHES "DISPLAY=")
        if(TARGET ${T_COMMAND})
            set(_cna_wrap_gpu TRUE)
        endif()
    endif()
    if(_cna_wrap_gpu)
        add_test(NAME ${T_NAME} COMMAND sh "${CNA_RENDERER_GPU_TEST_LAUNCHER}" "$<TARGET_FILE:${T_COMMAND}>")
    else()
        add_test(NAME ${T_NAME} COMMAND ${T_COMMAND})
    endif()

    set(_cna_test_props "")
    if(DEFINED T_TIMEOUT)
        list(APPEND _cna_test_props TIMEOUT ${T_TIMEOUT})
    endif()
    if(T_LABELS)
        string(REPLACE ";" "\\;" _cna_labels_escaped "${T_LABELS}")
        list(APPEND _cna_test_props LABELS "${_cna_labels_escaped}")
    endif()
    if(T_ENVIRONMENT)
        string(REPLACE ";" "\\;" _cna_environment_escaped "${T_ENVIRONMENT}")
        list(APPEND _cna_test_props ENVIRONMENT "${_cna_environment_escaped}")
    endif()
    if(DEFINED T_WORKING_DIRECTORY)
        list(APPEND _cna_test_props WORKING_DIRECTORY "${T_WORKING_DIRECTORY}")
    else()
        # Historical default, made explicit: every renderer/example test used to be
        # registered at root scope, where ctest's implicit working directory is the top
        # build directory -- and Content-loading tests (e.g. the demo_2d smoke tests)
        # resolve their assets CWD-relative next to the executable there. Registrations
        # are module-local now, and ctest's implicit CWD would otherwise become the
        # registering module's own binary subdirectory (found for real: the Vulkan demo_2d
        # smoke test aborted on Content lookup after the move). Pin the historical CWD for
        # every registration that does not choose one explicitly.
        list(APPEND _cna_test_props WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    endif()
    if(T_SKIP_REGULAR_EXPRESSION)
        string(REPLACE ";" "\\;" _cna_skip_regex_escaped "${T_SKIP_REGULAR_EXPRESSION}")
        list(APPEND _cna_test_props SKIP_REGULAR_EXPRESSION "${_cna_skip_regex_escaped}")
    endif()
    if(_cna_test_props)
        set_tests_properties(${T_NAME} PROPERTIES ${_cna_test_props})
    endif()
endfunction()

# Task 470's headless-safe CTest skip convention, extended to subdirectory scopes.
# The root CMakeLists applies SKIP_RETURN_CODE 77 in one shot to every test registered at
# root scope (cmake/UnitTests.cmake); a DIRECTORY TESTS property only sees its own
# directory's registrations, so each module-local example/test registration file calls this
# at its end to give its own tests the identical convention. Purely additive, exactly like
# the root one-shot: a test that never exits 77 is completely unaffected.
function(cna_apply_skip_convention)
    get_property(_cna_dir_tests DIRECTORY PROPERTY TESTS)
    if(_cna_dir_tests)
        set_tests_properties(${_cna_dir_tests} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()
