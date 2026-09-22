# SPDX-License-Identifier: MS-PL
# Stands in for CNA's root: includes the policy and registers one test the way CNA's GPU tests are.
include("${CNA_SOURCE_ROOT}/cmake/TestDisplayPolicy.cmake")
add_test(NAME framework_gpu_test COMMAND ${CMAKE_COMMAND} -E true)
set_tests_properties(framework_gpu_test PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=x11;DISPLAY=")
