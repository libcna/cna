# SPDX-License-Identifier: MS-PL
cmake_minimum_required(VERSION 3.30)
project(SharedRuntimeInstalledConsumer LANGUAGES CXX)
include("${CNA_PREFIX}/lib/cmake/fixture/features.cmake")
include("${CNA_PREFIX}/lib/cmake/fixture/FixtureTargets.cmake")
add_executable(installed_consumer "${CMAKE_CURRENT_LIST_DIR}/../data_consumer.cpp")
target_link_libraries(installed_consumer PRIVATE CNA::CNA)
