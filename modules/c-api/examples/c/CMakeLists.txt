# SPDX-License-Identifier: MS-PL
#
# A standalone consumer project. It is deliberately NOT part of the CNA build: it is configured
# against an *installed* CNA, which is the only way to prove that what this project installs is
# usable from outside its own source tree. Nothing here may reference a CNA source path.
#
# Build it the way a consumer would:
#
#   cmake -S modules/c-api/examples/c -B build-consumer \
#         -DCMAKE_PREFIX_PATH=<the install prefix>
#   cmake --build build-consumer -j3
#
# The CApi_InstalledConsumer test does exactly that against a staged install of the C API component.

cmake_minimum_required(VERSION 3.20)

project(cna_c_examples LANGUAGES C)

# One package, one target. Requesting the version is the CMake half of the runtime check the
# example makes with cna_get_abi_version(): the package version *is* the C ABI version, and the
# installed CNAConfigVersion.cmake accepts a same-minor match.
find_package(CNA 0.1 CONFIG REQUIRED)

add_executable(hello_cna hello_cna.c)

# C99 is the floor the public headers are held to, and building the example at exactly that floor
# is what keeps the claim honest for the code a consumer copies.
set_target_properties(hello_cna PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_compile_options(hello_cna PRIVATE
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4;/WX>
)

target_link_libraries(hello_cna PRIVATE CNA::CApi)
