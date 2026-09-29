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
    # plan_vulkan.md VULKAN-408: the Vulkan validation output gate, on every registration this
    # helper makes -- ~600 of them across every renderer's examples, and a no-op in a configuration
    # where no VkDevice can exist. This is the hook rather than a root-level sweep because
    # set_tests_properties() is directory-scoped: from the root it cannot touch a test another
    # directory registered, and the DIRECTORY-qualified set_property() form that could is CMake
    # 3.28 against this project's 3.20 floor. Escaped like ENVIRONMENT/LABELS above, for the same
    # reason: the value goes into a list that list(APPEND) would otherwise flatten.
    set(_cna_vk_gate_regex "")
    cna_append_vulkan_validation_gate_pattern(_cna_vk_gate_regex ${T_NAME})
    if(_cna_vk_gate_regex)
        string(REPLACE ";" "\\;" _cna_vk_gate_escaped "${_cna_vk_gate_regex}")
        list(APPEND _cna_test_props FAIL_REGULAR_EXPRESSION "${_cna_vk_gate_escaped}")
    endif()
    if(_cna_test_props)
        set_tests_properties(${T_NAME} PROPERTIES ${_cna_test_props})
    endif()
endfunction()

# plan_vulkan.md VULKAN-393 / VULKAN-408 -- the Vulkan validation output gate.
#
# `VulkanRenderer`'s debug messenger prints "[Vulkan Validation] <message>" to stderr for every
# message the Khronos layer reports at severity WARNING or above. A CTest whose output contains
# that line has produced a validation message, and this gate turns that into a failure.
#
# It is an OUTPUT gate rather than an in-process assertion for a reason that cost two defects to
# learn: VULKAN-405's leak and VULKAN-407's were both reported DURING vkDestroyDevice -- after the
# last statement a test could execute, out of the very object whose message list an in-process
# check would have read. Object tracking is where leaks are named, and only the process's output
# carries them.
#
# It is deliberately the whole "[Vulkan Validation]" prefix and not the
# "VUID-|Validation Error|Validation Warning|SYNC-HAZARD" pattern several registrations already
# carry: an Object Tracking report contains none of those words, so that pattern let VULKAN-405's
# message straight through.
#
# WHERE IT IS APPLIED, and why here rather than in a tree-wide sweep. VULKAN-408 tried the sweep
# first -- a recursive walk of every directory's TESTS property, called once from the root
# CMakeLists the way cna_apple_configure_all_bundles() is. It does not work: set_tests_properties()
# is directory-scoped, so from the root it cannot touch a test another directory registered
# ("Can not find test to add properties to: ContentLostProbe"). The DIRECTORY-qualified
# set_property() form that could is CMake 3.28, and this project requires 3.20. The two hooks that
# ARE always in the right scope are this function, which every renderer example registration
# already goes through, and gtest_discover_tests(PROPERTIES ...) at its own call site in
# cmake/UnitTests.cmake for the ~8 700 PRE_TEST-discovered cases that do not exist at configure
# time at all. Together they are every CTest that can create a VkDevice.
#
# THE ALLOWLIST. VULKAN-393 asked for "a documented allowlist of driver/layer messages with a
# reason each". Measured after VULKAN-404, -405, -406 and -407: the whole 9 117-test configuration
# emits ZERO messages, and not one of the four defects found was a driver or layer artefact -- all
# four were ours. There is nothing to allow. What exists instead is this per-TEST exemption list,
# empty, in one place: adding a name to it requires writing the exact message and the reason beside
# it, and plan_vulkan.md VULKAN-477 requires that no entry is ever CNA-attributable.
set(CNA_VULKAN_VALIDATION_GATE_EXEMPT ""
    CACHE INTERNAL "plan_vulkan.md VULKAN-393: CTests exempt from the Vulkan validation gate")

# True when a VkDevice can exist in this configuration at all -- the gate is meaningless otherwise
# and is not applied, so the other renderer configurations are untouched.
function(cna_vulkan_validation_gate_applies out)
    if(CNA_GRAPHICS_RENDERER STREQUAL "VULKAN" OR "VULKAN" IN_LIST CNA_GRAPHICS_RENDERERS)
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Appends the gate to a list of FAIL_REGULAR_EXPRESSION patterns, preserving what is already there:
# the property is a LIST of regexes, so a registration's own VUID-/SYNC-HAZARD pattern keeps working
# alongside this one instead of being replaced by it.
function(cna_append_vulkan_validation_gate_pattern list_var test_name)
    cna_vulkan_validation_gate_applies(_cna_vk_gate)
    if(NOT _cna_vk_gate)
        return()
    endif()
    if(test_name IN_LIST CNA_VULKAN_VALIDATION_GATE_EXEMPT)
        return()
    endif()
    set(_cna_patterns "${${list_var}}")
    if(NOT "\\[Vulkan Validation\\]" IN_LIST _cna_patterns)
        list(APPEND _cna_patterns "\\[Vulkan Validation\\]")
    endif()
    set(${list_var} "${_cna_patterns}" PARENT_SCOPE)
endfunction()

# Re-applies the gate over every test registered in the CALLING directory, merging with whatever
# FAIL_REGULAR_EXPRESSION each already has. Needed because a registration is free to call
# set_tests_properties(... FAIL_REGULAR_EXPRESSION ...) AFTER cna_register_renderer_test(), which
# replaces the property rather than adding to it -- nine registrations in the Vulkan examples do
# exactly that. Call it LAST in a directory that does so; it is a no-op where the pattern survived.
macro(cna_apply_vulkan_validation_gate)
    cna_vulkan_validation_gate_applies(_cna_vk_dir_gate)
    if(_cna_vk_dir_gate)
        get_property(_cna_vk_dir_tests DIRECTORY PROPERTY TESTS)
        foreach(_cna_vk_t IN LISTS _cna_vk_dir_tests)
            get_test_property(${_cna_vk_t} FAIL_REGULAR_EXPRESSION _cna_vk_regex)
            if(NOT _cna_vk_regex)
                set(_cna_vk_regex "")
            endif()
            cna_append_vulkan_validation_gate_pattern(_cna_vk_regex ${_cna_vk_t})
            if(_cna_vk_regex)
                set_tests_properties(${_cna_vk_t} PROPERTIES
                    FAIL_REGULAR_EXPRESSION "${_cna_vk_regex}")
            endif()
        endforeach()
    endif()
endmacro()

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
