#!/usr/bin/env bash
# plan_apple.md APPLE-1/APPLE-4: host-portable smoke check for cmake/ApplePlatform.cmake.
#
# The Apple platform layer is the one part of the build that a Linux or Windows developer can
# never reach by configuring the project — every line of it is behind if(APPLE). That makes it
# exactly the kind of file that rots unnoticed. This script parses it in cmake script mode and
# exercises the branches that do not need an Apple host: the allow-list accept path, the
# escape-hatch path, the refusal path, and the two non-Apple no-ops.
#
# Usage: scripts/check-apple-platform-cmake.sh
# Exit status: 0 when every expectation holds.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

script="${work_dir}/probe.cmake"

# ---------------------------------------------------------------------------
# 1. Non-Apple host: the file must define its API and do nothing else.
# ---------------------------------------------------------------------------
cat > "${script}" <<EOF
set(CMAKE_SOURCE_DIR "${repo_root}")
include("${repo_root}/cmake/ApplePlatform.cmake")
if(CNA_APPLE)
    message(FATAL_ERROR "PROBE: CNA_APPLE must be OFF when APPLE is not set")
endif()
if(NOT CNA_APPLE_TARGET STREQUAL "NONE")
    message(FATAL_ERROR "PROBE: CNA_APPLE_TARGET must be NONE, got '\${CNA_APPLE_TARGET}'")
endif()
cna_apple_validate_renderer("VULKAN")
cna_apple_configure_all_bundles()
message(STATUS "PROBE-OK non-apple")
EOF
cmake -P "${script}" 2>&1 | grep -q "PROBE-OK non-apple" || {
    echo "FAIL: ApplePlatform.cmake did not stay inert on a non-Apple host" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# 2. iOS + allow-listed renderer: accepted.
# ---------------------------------------------------------------------------
cat > "${script}" <<EOF
set(CMAKE_SOURCE_DIR "${repo_root}")
include("${repo_root}/cmake/ApplePlatform.cmake")
set(CNA_APPLE_IOS ON)
cna_apple_validate_renderer("SDL_RENDERER")
message(STATUS "PROBE-OK allowed")
EOF
cmake -P "${script}" 2>&1 | grep -q "PROBE-OK allowed" || {
    echo "FAIL: an allow-listed iOS renderer was rejected" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# 3. iOS + renderer outside the allow-list: refused, and the message says how to override.
# ---------------------------------------------------------------------------
cat > "${script}" <<EOF
set(CMAKE_SOURCE_DIR "${repo_root}")
include("${repo_root}/cmake/ApplePlatform.cmake")
set(CNA_APPLE_IOS ON)
cna_apple_validate_renderer("DIRECTX11")
message(STATUS "PROBE-REACHED-END")
EOF
if refusal="$(cmake -P "${script}" 2>&1)"; then
    echo "FAIL: DIRECTX11 was accepted for iOS" >&2
    exit 1
fi
grep -q "CNA_APPLE_ALLOW_UNVALIDATED_RENDERER" <<< "${refusal}" || {
    echo "FAIL: the iOS refusal does not mention the documented override" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# 4. iOS + escape hatch: warns, does not fail.
# ---------------------------------------------------------------------------
cat > "${script}" <<EOF
set(CMAKE_SOURCE_DIR "${repo_root}")
include("${repo_root}/cmake/ApplePlatform.cmake")
set(CNA_APPLE_IOS ON)
set(CNA_APPLE_ALLOW_UNVALIDATED_RENDERER ON)
cna_apple_validate_renderer("METAL")
message(STATUS "PROBE-OK escape-hatch")
EOF
cmake -P "${script}" 2>&1 | grep -q "PROBE-OK escape-hatch" || {
    echo "FAIL: CNA_APPLE_ALLOW_UNVALIDATED_RENDERER did not downgrade the refusal" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# 4b. A renderer stays allow-listed only while the real Apple workflow builds it. SDL_GPU needs
#     an iOS libshaderc and EasyGL needs two additional sibling repositories, neither of which is
#     currently supplied by that workflow; accepting them would only postpone a known failure.
# ---------------------------------------------------------------------------
for renderer in SDL_GPU OPENGLES2 OPENGLES3 HEADLESS SOFTWARE STUB; do
    cat > "${script}" <<EOF
set(CMAKE_SOURCE_DIR "${repo_root}")
include("${repo_root}/cmake/ApplePlatform.cmake")
set(CNA_APPLE_IOS ON)
cna_apple_validate_renderer("${renderer}")
message(STATUS "PROBE-ACCEPTED ${renderer}")
EOF
    if cmake -P "${script}" >/dev/null 2>&1; then
        echo "FAIL: ${renderer} was accepted although Apple CI does not build it" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# 5. The Info.plist templates the bundle configuration points at must exist and be well-formed
#    enough to name the CMake substitutions the target properties provide.
# ---------------------------------------------------------------------------
for plist in AppleInfo.iOS.plist.in AppleInfo.macOS.plist.in; do
    path="${repo_root}/cmake/${plist}"
    [ -f "${path}" ] || { echo "FAIL: missing ${path}" >&2; exit 1; }
    for key in MACOSX_BUNDLE_EXECUTABLE_NAME MACOSX_BUNDLE_GUI_IDENTIFIER MACOSX_BUNDLE_BUNDLE_NAME; do
        grep -q "\${${key}}" "${path}" || {
            echo "FAIL: ${plist} never substitutes ${key}" >&2
            exit 1
        }
    done
done

# ---------------------------------------------------------------------------
# 6. Static-only iOS means all three SDL projects must receive their own build-system switch.
#    SDL3 uses SDL_SHARED/SDL_STATIC; SDL_image and SDL_mixer independently default
#    BUILD_SHARED_LIBS to ON on Apple platforms, so merely changing SDL3 is insufficient.
# ---------------------------------------------------------------------------
third_party_sdl="${repo_root}/cmake/ThirdPartySDL.cmake"
for required in \
    '-DSDL_SHARED=${_sdl_shared}' \
    '-DSDL_STATIC=${_sdl_static}' \
    '-DBUILD_SHARED_LIBS=${_sdl_shared}' \
    '-DSDLIMAGE_DEPS_SHARED=${_sdl_shared}' \
    '-DSDLMIXER_DEPS_SHARED=${_sdl_shared}'; do
    grep -Fq -- "${required}" "${third_party_sdl}" || {
        echo "FAIL: ThirdPartySDL.cmake does not propagate ${required}" >&2
        exit 1
    }
done

[ -f "${repo_root}/cmake/FixupMacOSBundle.cmake" ] || {
    echo "FAIL: missing macOS runtime-library bundle fixup" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# 7. iOS orientation changes happen at two distinct times: the documented initial hint must exist
#    before the video subsystem starts, while later XNA GraphicsDeviceManager changes need a UIKit
#    invalidation after the window exists. Both halves live in the platform module, which is the
#    one place allowed to touch a native windowing API; keep both connected to the build.
# ---------------------------------------------------------------------------
platform_impl="${repo_root}/modules/platform/src/Sdl3/Sdl3Platform.cpp"
orientation_adapter="${repo_root}/modules/platform/src/Sdl3/Sdl3AppleOrientation.mm"
platform_cmake="${repo_root}/modules/platform/CMakeLists.txt"

grep -q 'SDL_GetHint(SDL_HINT_ORIENTATIONS)' "${platform_impl}" || {
    echo "FAIL: the pre-video-init mobile orientation default is missing" >&2
    exit 1
}
grep -q 'setNeedsUpdateOfSupportedInterfaceOrientations' "${orientation_adapter}" || {
    echo "FAIL: the iOS orientation adapter does not invalidate UIKit orientation state" >&2
    exit 1
}
grep -q 'src/Sdl3/Sdl3AppleOrientation.mm' "${platform_cmake}" || {
    echo "FAIL: the iOS orientation adapter is not part of cna_platform" >&2
    exit 1
}
grep -q 'SetSupportedOrientations' "${repo_root}/modules/platform/include/CNA/Platform/IPlatformWindow.hpp" || {
    echo "FAIL: the platform window contract does not carry the supported-orientation request" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# 8. sharp-runtime uses Apple's floating-point std::to_chars overloads. Xcode marks those APIs
#    unavailable below macOS 13.3 / iOS 16.3, so advertising lower deployment floors produces a
#    deterministic compile failure (or a loader failure if availability checking is suppressed).
# ---------------------------------------------------------------------------
grep -Fq 'set(CNA_MACOS_DEPLOYMENT_TARGET "13.3"' "${repo_root}/cmake/ApplePlatform.cmake" || {
    echo "FAIL: macOS deployment target is below the std::to_chars availability floor" >&2
    exit 1
}
grep -Fq 'set(CNA_IOS_DEPLOYMENT_TARGET "16.3"' "${repo_root}/cmake/ApplePlatform.cmake" || {
    echo "FAIL: iOS deployment target is below the std::to_chars availability floor" >&2
    exit 1
}

echo "OK: Apple CMake layer parses; renderer gate, deployment floors, plist templates, static iOS SDL, bundle fixup and orientation bridge are present."
