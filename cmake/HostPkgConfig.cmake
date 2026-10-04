# SPDX-License-Identifier: MS-PL
include_guard(GLOBAL)

# Plain pkg-config describes the build host unless the caller supplies a target-aware wrapper and
# sysroot policy. CNA's automatic optional-dependency probes use the plain host route, so they must
# remain disabled in every cross configuration.
function(cna_can_probe_host_pkg_config output_variable)
    if(CMAKE_CROSSCOMPILING)
        set(${output_variable} FALSE PARENT_SCOPE)
    else()
        set(${output_variable} TRUE PARENT_SCOPE)
    endif()
endfunction()
