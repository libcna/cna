# plans/plan_sdlgpu_modern_graphics.md SMG-0039: the persistent SDL install is reused only while
# its build manifest matches, so a changed vendored source, a changed CNA patch series or changed
# configure arguments cannot leave an old library linked under a tree that claims the new one.
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

file(REMOVE_RECURSE "${_work}")
message(STATUS "SdlPrebuiltFingerprint: all cases passed")
