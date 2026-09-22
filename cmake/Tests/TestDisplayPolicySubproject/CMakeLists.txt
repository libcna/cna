# SPDX-License-Identifier: MS-PL
#
# CnaTestDisplayPolicyAsSubproject: a consumer that adds CNA with add_subdirectory(), the way
# cna-street and the templates do. cmake/TestDisplayPolicy.cmake defers its sweep to the end of the
# TOP-LEVEL directory, where CNA's own directory variables are not visible; this configures only
# when everything the sweep needs reaches it anyway.
cmake_minimum_required(VERSION 3.28)
project(CnaTestDisplayPolicyConsumer NONE)
enable_testing()
add_test(NAME consumer_own_test COMMAND ${CMAKE_COMMAND} -E true)
add_subdirectory(framework)
cmake_language(DEFER CALL cna_test_display_policy_subproject_check)
function(cna_test_display_policy_subproject_check)
    get_test_property(framework_gpu_test ENVIRONMENT DIRECTORY framework _env)
    get_test_property(framework_gpu_test ENVIRONMENT_MODIFICATION DIRECTORY framework _mod)
    get_test_property(consumer_own_test ENVIRONMENT_MODIFICATION _own)
    if("DISPLAY=" IN_LIST _env)
        message(FATAL_ERROR "the framework test kept its empty DISPLAY: ${_env}")
    endif()
    if(NOT WIN32 AND NOT "WAYLAND_DISPLAY=string_append:" IN_LIST _mod)
        message(FATAL_ERROR "the framework test has no Wayland guard: ${_mod}")
    endif()
    if(_own)
        message(FATAL_ERROR "the sweep rewrote the consumer's own test: ${_own}")
    endif()
endfunction()
