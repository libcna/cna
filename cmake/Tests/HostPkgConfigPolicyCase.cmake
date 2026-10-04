# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_HOST_PKG_CONFIG_POLICY_FILE)
    message(FATAL_ERROR "CNA_HOST_PKG_CONFIG_POLICY_FILE is required")
endif()

include("${CNA_HOST_PKG_CONFIG_POLICY_FILE}")

set(CMAKE_CROSSCOMPILING FALSE)
cna_can_probe_host_pkg_config(_native_allowed)
if(NOT _native_allowed)
    message(FATAL_ERROR "Native configuration unexpectedly rejected the host pkg-config probe")
endif()

set(CMAKE_CROSSCOMPILING TRUE)
cna_can_probe_host_pkg_config(_cross_allowed)
if(_cross_allowed)
    message(FATAL_ERROR "Cross configuration unexpectedly accepted the host pkg-config probe")
endif()
