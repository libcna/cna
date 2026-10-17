# Regression for explicit wgpu-native package discovery under a cross-toolchain
# CMAKE_FIND_ROOT_PATH policy. No compiler, network access, or GPU is required.
foreach(_required IN ITEMS MODULE PROBE_SOURCE WORKDIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORKDIR}")
set(_package "${WORKDIR}/package")
set(_sysroot "${WORKDIR}/unrelated-sysroot")
file(MAKE_DIRECTORY "${_package}/include/webgpu" "${_package}/lib" "${_sysroot}")
file(WRITE "${_package}/include/webgpu/webgpu.h" "/* discovery fixture */\n")
file(WRITE "${_package}/lib/libwgpu_native.a" "discovery fixture\n")
file(WRITE "${_package}/lib/wgpu_native.dll" "discovery fixture\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${PROBE_SOURCE}"
        -B "${WORKDIR}/build"
        -DTHIRDPARTY_WEBGPU=${MODULE}
        -DPACKAGE_ROOT=${_package}
        -DUNRELATED_SYSROOT=${_sysroot}
    RESULT_VARIABLE _configure_rc
    OUTPUT_VARIABLE _configure_out
    ERROR_VARIABLE _configure_err)

if(NOT _configure_rc EQUAL 0)
    message(FATAL_ERROR
        "WebGPU cross-root discovery probe failed (${_configure_rc})\n"
        "stdout:\n${_configure_out}\n"
        "stderr:\n${_configure_err}")
endif()

message(STATUS "WebGPU explicit-package cross-root discovery passed")
