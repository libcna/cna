# plans/plan_sdlgpu_modern_graphics.md SMG-0039: the persistent SDL install is reused only while
# its build manifest matches, so a changed vendored source, a changed CNA patch series, changed
# configure arguments or a changed toolchain cannot leave an old library linked under a tree that
# claims the new one.
#
# Drives the real functions cmake/ThirdPartySDL.cmake uses, on a small fixture instead of SDL
# itself -- the decision is the same, and a real SDL rebuild would cost minutes per case.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> -P cmake/Tests/SdlPrebuiltFingerprint.cmake

cmake_minimum_required(VERSION 3.20)

include("${CNA_SOURCE_DIR}/cmake/SdlPrebuiltFingerprint.cmake")

function(_expect_current expected label)
    cna_sdl_prebuilt_is_current(_current _reason
        LIBRARY "${_lib}" STAMP "${_stamp}" MANIFEST "${_manifest}")
    if(expected AND NOT _current)
        message(FATAL_ERROR "SdlPrebuiltFingerprint: ${label}: expected reuse, got a rebuild (${_reason})")
    endif()
    if(NOT expected AND _current)
        message(FATAL_ERROR "SdlPrebuiltFingerprint: ${label}: expected a rebuild, the old install was reused")
    endif()
    message(STATUS "ok: ${label}")
endfunction()

# The staging case runs in a child process because a non-applying patch is a FATAL_ERROR.
if(DEFINED CNA_STAGE_CASE)
    cna_sdl_stage_patched_source(
        SOURCE "${CNA_STAGE_SOURCE}" DESTINATION "${CNA_STAGE_DESTINATION}"
        PATCHES ${CNA_STAGE_PATCHES})
    return()
endif()

set(_work "${CNA_WORK_DIR}")
file(REMOVE_RECURSE "${_work}")
# An enclosing repository around everything below, as the default prebuilt root has in CNA's own
# checkout: staging must apply the patch to the staged tree, not resolve it against this one.
file(MAKE_DIRECTORY "${_work}")
find_program(_git git REQUIRED)
execute_process(COMMAND "${_git}" init -q "${_work}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "SdlPrebuiltFingerprint: git init failed")
endif()

set(_src "${_work}/vendored/SDL")
file(WRITE "${_src}/CMakeLists.txt" "project(fixture C)\n")
file(WRITE "${_src}/src/gpu/barrier.c" "int barrier(void)\n{\n    return 1;\n}\n")
file(WRITE "${_src}/.git/HEAD" "ref: refs/heads/main\n")   # must not count as source

set(_patch1 "${_work}/patches/fixture-0001-return-two.patch")
file(WRITE "${_patch1}" "diff --git a/src/gpu/barrier.c b/src/gpu/barrier.c
--- a/src/gpu/barrier.c
+++ b/src/gpu/barrier.c
@@ -1,4 +1,4 @@
 int barrier(void)
 {
-    return 1;
+    return 2;
 }
")
set(_patch2 "${_work}/patches/fixture-0002-add-file.patch")
file(WRITE "${_patch2}" "diff --git a/src/gpu/extra.c b/src/gpu/extra.c
new file mode 100644
--- /dev/null
+++ b/src/gpu/extra.c
@@ -0,0 +1 @@
+int extra(void) { return 3; }
")

set(_root "${_work}/prebuilt")
set(_lib "${_root}/install/lib/libfixture.so")
set(_stamp "${_root}/fixture.cna-build-manifest.txt")
set(_args "-DCMAKE_BUILD_TYPE=Release" "-DFIXTURE_SHARED=ON")
file(WRITE "${_lib}" "old library\n")

cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
set(_built "${_manifest}")

# 1. An install made before manifests existed has no stamp: nothing says what it was built from.
_expect_current(FALSE "legacy install without a manifest is rebuilt")

# 2. The install the current inputs produced is reused.
file(WRITE "${_stamp}" "${_built}")
_expect_current(TRUE "unchanged inputs reuse the install")

# 3. The same inputs a second time produce the same manifest (deterministic).
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
_expect_current(TRUE "recomputed manifest is identical")

# 4. The negative control the stale-cache hazard is about: the patch changes, the library is still
#    there, and it must not be reused.
file(READ "${_patch1}" _patch1_text)
string(REPLACE "return 2;" "return 22;" _patch1_edited "${_patch1_text}")
file(WRITE "${_patch1}" "${_patch1_edited}")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
_expect_current(FALSE "edited patch content forces a rebuild")
file(WRITE "${_patch1}" "${_patch1_text}")

# 5. A patch added to the series.
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}"
    PATCHES "${_patch1}" "${_patch2}" ARGS ${_args})
_expect_current(FALSE "added patch forces a rebuild")

# 6. The same patches in another order.
file(WRITE "${_stamp}" "${_manifest}")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}"
    PATCHES "${_patch2}" "${_patch1}" ARGS ${_args})
_expect_current(FALSE "reordered patch series forces a rebuild")

# 7. A patch dropped (the day the pin moves past the upstream fix).
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" ARGS ${_args})
_expect_current(FALSE "dropped patch forces a rebuild")

# 8. The vendored source moves -- edited in place, as a submodule update does.
file(WRITE "${_stamp}" "${_built}")
file(APPEND "${_src}/src/gpu/barrier.c" "/* upstream moved */\n")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
_expect_current(FALSE "changed vendored source forces a rebuild")
file(WRITE "${_src}/src/gpu/barrier.c" "int barrier(void)\n{\n    return 1;\n}\n")

# 9. A file added to the vendored source.
file(WRITE "${_src}/src/gpu/new.c" "int n;\n")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
_expect_current(FALSE "added vendored file forces a rebuild")
file(REMOVE "${_src}/src/gpu/new.c")

# 10. .git metadata is not source.
file(WRITE "${_src}/.git/HEAD" "ref: refs/heads/other\n")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
_expect_current(TRUE ".git metadata does not count as source")

# 11. Changed configure arguments.
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}"
    ARGS "-DCMAKE_BUILD_TYPE=Release" "-DFIXTURE_SHARED=OFF")
_expect_current(FALSE "changed configure arguments force a rebuild")

# 12. The library itself is gone.
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
file(REMOVE "${_lib}")
_expect_current(FALSE "missing library forces a rebuild")

# 13. Staging applies the series to a copy and leaves the vendored tree untouched.
set(_staged "${_root}/fixture/source")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" -DCNA_STAGE_CASE=1
            "-DCNA_STAGE_SOURCE=${_src}" "-DCNA_STAGE_DESTINATION=${_staged}"
            "-DCNA_STAGE_PATCHES=${_patch1}\\;${_patch2}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "SdlPrebuiltFingerprint: staging failed:\n${_out}${_err}")
endif()
file(READ "${_staged}/src/gpu/barrier.c" _staged_text)
file(READ "${_src}/src/gpu/barrier.c" _pristine_text)
if(NOT _staged_text MATCHES "return 2;" OR NOT EXISTS "${_staged}/src/gpu/extra.c")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: the staged tree is not patched:\n${_staged_text}")
endif()
if(NOT _pristine_text MATCHES "return 1;" OR EXISTS "${_src}/src/gpu/extra.c")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: staging modified the vendored tree")
endif()
if(EXISTS "${_staged}/.git")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: staging copied .git")
endif()
message(STATUS "ok: staging patches a copy and leaves the vendored tree pristine")

# 14. A patch that does not apply stops the configure and leaves no half-patched tree behind.
file(WRITE "${_src}/src/gpu/barrier.c" "int barrier(void)\n{\n    return 7;\n}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" -DCNA_STAGE_CASE=1
            "-DCNA_STAGE_SOURCE=${_src}" "-DCNA_STAGE_DESTINATION=${_staged}"
            "-DCNA_STAGE_PATCHES=${_patch1}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
if(_rc EQUAL 0)
    message(FATAL_ERROR "SdlPrebuiltFingerprint: a non-applying patch was accepted")
endif()
if(NOT _err MATCHES "does not apply")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: the refusal does not name the patch:\n${_err}")
endif()
if(EXISTS "${_staged}")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: a failed staging left ${_staged} behind")
endif()
message(STATUS "ok: a non-applying patch is refused and leaves nothing staged")

# 15. A cross build's toolchain file edited in place -- same path, another compiler (2026-10-26
#     dependency audit C-09): the path in ARGS is unchanged, the content is not.
set(_toolchain "${_work}/toolchain.cmake")
file(WRITE "${_toolchain}" "set(CMAKE_C_COMPILER /fixture/compiler-A)\n")
set(_tc_args ${_args} "-DCMAKE_TOOLCHAIN_FILE=${_toolchain}")
set(_compiler "GNU 14.2.0 /fixture/bin/c++")
file(WRITE "${_lib}" "old library\n")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_tc_args}
    TOOLCHAIN_FILE "${_toolchain}" COMPILER "${_compiler}")
file(WRITE "${_stamp}" "${_manifest}")
_expect_current(TRUE "toolchain build with unchanged inputs reuses the install")
file(WRITE "${_toolchain}" "set(CMAKE_C_COMPILER /fixture/compiler-B)\n")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_tc_args}
    TOOLCHAIN_FILE "${_toolchain}" COMPILER "${_compiler}")
_expect_current(FALSE "toolchain file edited in place forces a rebuild")

# 16. The same toolchain file resolving to another compiler (an SDK or NDK chosen from outside it).
file(WRITE "${_toolchain}" "set(CMAKE_C_COMPILER /fixture/compiler-A)\n")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_tc_args}
    TOOLCHAIN_FILE "${_toolchain}" COMPILER "Clang 18.1.0 /fixture/ndk-r27/bin/clang++")
_expect_current(FALSE "another resolved compiler forces a rebuild")

# 16b. A toolchain whose settings come from a file it includes: editing only that file leaves the
#      toolchain file's hash and the resolved CXX compiler as they were, and must still force a
#      rebuild -- an ABI flag, a Release flag, a sysroot, the C compiler (2026-10-26 remediation
#      review R3-02, R8-02, R10-01), each value byte for byte, so two sysroots differing only in a
#      space differ too (R10-02). The settings come from a `cmake -P` evaluation of the toolchain,
#      exactly as production reads them, so nothing in the configuring scope changes them (R10-03).
set(_abi "${_work}/abi.cmake")
file(WRITE "${_toolchain}" "include(\"${_abi}\")\n")
function(_toolchain_manifest out abi_text)
    file(WRITE "${_abi}" "${abi_text}")
    cna_sdl_toolchain_settings(_settings "${_toolchain}")
    cna_sdl_build_manifest(_m NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_tc_args}
        TOOLCHAIN_FILE "${_toolchain}" COMPILER "${_compiler}" SETTINGS ${_settings})
    set(${out} "${_m}" PARENT_SCOPE)
endfunction()
set(_base "set(CMAKE_C_FLAGS_INIT \"-m64\")\nset(CMAKE_SYSROOT \"/fixture/SDK name\")\nset(CMAKE_C_COMPILER /fixture/bin/gcc)\nset(CMAKE_C_FLAGS_RELEASE_INIT \"-m64\")\n")
_toolchain_manifest(_manifest "${_base}")
if(NOT _manifest MATCHES "toolchain-setting CMAKE_SYSROOT=/fixture/SDK name\n")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: the toolchain's settings were not recorded:\n${_manifest}")
endif()
file(WRITE "${_stamp}" "${_manifest}")
_toolchain_manifest(_manifest "${_base}")
_expect_current(TRUE "toolchain with included settings, unchanged, reuses the install")
foreach(_edit IN ITEMS
        "CMAKE_C_FLAGS_INIT \"-m64\"|CMAKE_C_FLAGS_INIT \"-m32\"|an ABI flag"
        "CMAKE_C_FLAGS_RELEASE_INIT \"-m64\"|CMAKE_C_FLAGS_RELEASE_INIT \"-m32\"|a Release ABI flag"
        "/fixture/SDK name|/fixture/SDK-B|a sysroot"
        "/fixture/SDK name|/fixture/SDK  name|a sysroot differing in a space"
        "/fixture/bin/gcc|/fixture/bin/clang|the C compiler")
    string(REPLACE "|" ";" _edit "${_edit}")
    list(GET _edit 0 _from)
    list(GET _edit 1 _to)
    list(GET _edit 2 _what)
    string(REPLACE "${_from}" "${_to}" _changed "${_base}")
    _toolchain_manifest(_manifest "${_changed}")
    _expect_current(FALSE "${_what} edited in a file the toolchain includes forces a rebuild")
endforeach()
# Final flags a toolchain sets directly, as the Android NDK's does (R11-03).
_toolchain_manifest(_manifest "set(CMAKE_C_FLAGS_RELEASE \"-DFIXTURE_ABI=64\")\n")
file(WRITE "${_stamp}" "${_manifest}")
_toolchain_manifest(_manifest "set(CMAKE_C_FLAGS_RELEASE \"-DFIXTURE_ABI=32\")\n")
_expect_current(FALSE "a final Release flag edited in a file the toolchain includes forces a rebuild")
# A list-valued -D argument reaches the evaluation whole, and a list value stays one record (R11-02);
# IN_LIST works as in a project (R11-01's policy case).
set(_arch_text "if(\"arm64\" IN_LIST CMAKE_OSX_ARCHITECTURES)\n  set(CMAKE_C_FLAGS_RELEASE_INIT \"-DFIXTURE_ARM=1\")\nendif()\nset(CMAKE_OSX_SYSROOT \"a;b\")\n")
file(WRITE "${_abi}" "${_arch_text}")
cna_sdl_toolchain_settings(_settings "${_toolchain}" "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_tc_args}
    TOOLCHAIN_FILE "${_toolchain}" COMPILER "${_compiler}" SETTINGS ${_settings})
if(NOT _manifest MATCHES "toolchain-setting CMAKE_C_FLAGS_RELEASE_INIT=-DFIXTURE_ARM=1\n" OR
   NOT _manifest MATCHES "toolchain-setting CMAKE_OSX_SYSROOT=a%3Bb\n")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: a list-valued argument or setting was split:\n${_manifest}")
endif()
message(STATUS "ok: a list-valued -D argument and a list setting each stay whole")
# A toolchain that cannot be evaluated outside a project records why, the same way every time.
_toolchain_manifest(_manifest "add_compile_options(-DFIXTURE)\n")
_toolchain_manifest(_again "add_compile_options(-DFIXTURE)\n")
if(NOT _manifest MATCHES "toolchain-setting unavailable=" OR NOT _manifest STREQUAL _again)
    message(FATAL_ERROR "SdlPrebuiltFingerprint: an unevaluable toolchain is not recorded stably:\n${_manifest}")
endif()
message(STATUS "ok: an unevaluable toolchain is recorded as such, stably")
# What the configuring project's scope holds -- a C compiler CMake cached once some project enabled
# C, the defaults enabling C adds to the *_INIT flags -- changes nothing.
_toolchain_manifest(_manifest "${_base}")
file(WRITE "${_stamp}" "${_manifest}")
set(CMAKE_C_COMPILER /usr/bin/cc)
set(CMAKE_C_FLAGS_RELEASE_INIT "-m64 -O3 -DNDEBUG")
set(CMAKE_C_FLAGS_INIT " -m64 ")
_toolchain_manifest(_manifest "${_base}")
_expect_current(TRUE "the configuring project's own language state leaves the install current")
unset(CMAKE_C_COMPILER)
unset(CMAKE_C_FLAGS_RELEASE_INIT)
unset(CMAKE_C_FLAGS_INIT)

# 17. Without a toolchain file the manifest has no toolchain lines, so native installs built before
#     these fields existed stay current.
cna_sdl_build_manifest(_plain NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args})
cna_sdl_build_manifest(_manifest NAME fixture SOURCE "${_src}" PATCHES "${_patch1}" ARGS ${_args}
    TOOLCHAIN_FILE "" COMPILER "${_compiler}" SETTINGS "CMAKE_C_FLAGS_INIT=-m64")
if(NOT _manifest STREQUAL _plain OR _manifest MATCHES "toolchain")
    message(FATAL_ERROR "SdlPrebuiltFingerprint: a native manifest records a toolchain:\n${_manifest}")
endif()
message(STATUS "ok: a native manifest is unchanged by the toolchain fields")

# 18. Another tree rebuilding the shared install rewrites its stamp; a tree that watches the stamp
#     re-runs its configure on its next build instead of linking that install unnoticed
#     (2026-10-26 dependency audit C-08). Needs a generator that can say whether it would rerun.
find_program(_ninja ninja)
if(_ninja)
    set(_tree "${_work}/watching-tree")
    set(_shared_stamp "${_work}/shared-root/fixture.cna-build-manifest.txt")
    file(WRITE "${_shared_stamp}" "manifest A\n")
    file(WRITE "${_tree}/src/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.20)\nproject(watch NONE)\n"
        "include(\"${CNA_SOURCE_DIR}/cmake/SdlPrebuiltFingerprint.cmake\")\n"
        "cna_sdl_prebuilt_watch(\"${_shared_stamp}\")\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_tree}/src" -B "${_tree}/build" -G Ninja
        "-DCMAKE_MAKE_PROGRAM=${_ninja}" RESULT_VARIABLE _rc OUTPUT_QUIET)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "SdlPrebuiltFingerprint: the watching fixture tree did not configure")
    endif()
    execute_process(COMMAND "${_ninja}" -C "${_tree}/build" -n OUTPUT_VARIABLE _plan)
    if(_plan MATCHES "Re-running CMake")
        message(FATAL_ERROR "SdlPrebuiltFingerprint: a freshly configured tree wants to reconfigure")
    endif()
    # Another tree's configure: a later timestamp and another manifest.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
    file(WRITE "${_shared_stamp}" "manifest B\n")
    execute_process(COMMAND "${_ninja}" -C "${_tree}/build" -n OUTPUT_VARIABLE _plan)
    if(NOT _plan MATCHES "Re-running CMake")
        message(FATAL_ERROR
            "SdlPrebuiltFingerprint: a tree did not notice its SDL install being rebuilt:\n${_plan}")
    endif()
    message(STATUS "ok: a rebuilt shared install makes the other tree reconfigure")
else()
    message(STATUS "skipped: no ninja, so case 18 cannot ask whether a tree would reconfigure")
endif()

file(REMOVE_RECURSE "${_work}")
message(STATUS "SdlPrebuiltFingerprint: all cases passed")
