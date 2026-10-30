#!/usr/bin/env python3
"""Hold CNA's Windows CMake preset contract in one mechanically checked place.

The presets are the shared entry point for Visual Studio, VS Code, local shells and
Windows CI.  This check intentionally reads the JSON rather than configuring a
Windows build, so every host can catch a renamed preset, a retired renderer or a
silently weakened smoke set before the Windows job runs.
"""

from __future__ import annotations

import json
import pathlib
import sys
from typing import Any


REPO = pathlib.Path(__file__).resolve().parent.parent
PRESETS_PATH = REPO / "CMakePresets.json"
WORKFLOW_PATH = REPO / ".github" / "workflows" / "windows-msvc-ci.yml"
VSCODE_SETTINGS_PATH = REPO / ".vscode" / "settings.json"
VSCODE_EXTENSIONS_PATH = REPO / ".vscode" / "extensions.json"

WINDOWS_CONFIGURE_PRESETS = {
    "windows-msvc-headless-debug",
    "windows-msvc-sdlgpu-debug",
    "windows-msvc-sdlgpu-relwithdebinfo",
    "windows-vs2022-sdlgpu",
}

SMOKE_TARGETS = {
    "cna_test_sdlgpu_smoke",
    "cna_test_sdlgpu_2d",
    "cna_conformance_clear_options_matrix_sdlgpu",
    "cna_conformance_effect_orientation_sdlgpu",
    "cna_conformance_texture2d_fromstream_sdlgpu",
    "cna_conformance_spritebatch_draw_overloads_sdlgpu",
    "cna_conformance_multisample_count_clamp_sdlgpu",
    "cna_conformance_disposed_guards_sdlgpu",
}

SMOKE_BUILD_PRESETS = {
    "windows-msvc-sdlgpu-debug",
    "windows-msvc-sdlgpu-relwithdebinfo",
    "windows-vs2022-sdlgpu-debug",
    "windows-vs2022-sdlgpu-relwithdebinfo",
}

EXPECTED_TEST_PRESETS = {
    "windows-msvc-headless-debug",
    *SMOKE_BUILD_PRESETS,
}

RETIRED_WINDOWS_RENDERERS = {"SDL_RENDERER", "DIRECTX11", "DIRECTX12", "VULKAN"}


def _by_name(data: dict[str, Any], key: str) -> dict[str, dict[str, Any]]:
    return {entry["name"]: entry for entry in data.get(key, [])}


def _parents(entry: dict[str, Any]) -> list[str]:
    inherited = entry.get("inherits", [])
    return [inherited] if isinstance(inherited, str) else list(inherited)


def main() -> int:
    data = json.loads(PRESETS_PATH.read_text(encoding="utf-8"))
    configure = _by_name(data, "configurePresets")
    build = _by_name(data, "buildPresets")
    tests = _by_name(data, "testPresets")
    errors: list[str] = []

    minimum = data.get("cmakeMinimumRequired", {})
    if (minimum.get("major", 0), minimum.get("minor", 0)) < (3, 21):
        errors.append("preset schema version 3 requires cmakeMinimumRequired 3.21 or newer")

    missing_configure = WINDOWS_CONFIGURE_PRESETS - configure.keys()
    if missing_configure:
        errors.append(f"missing Windows configure presets: {sorted(missing_configure)}")

    def has_windows_condition(name: str, seen: set[str] | None = None) -> bool:
        seen = set() if seen is None else seen
        if name in seen or name not in configure:
            return False
        seen.add(name)
        entry = configure[name]
        condition = entry.get("condition", {})
        if (condition.get("type") == "equals"
                and condition.get("lhs") == "${hostSystemName}"
                and condition.get("rhs") == "Windows"):
            return True
        return any(has_windows_condition(parent, seen) for parent in _parents(entry))

    for name in sorted(WINDOWS_CONFIGURE_PRESETS):
        if name in configure and not has_windows_condition(name):
            errors.append(f"{name} is not host-filtered to Windows")

    msvc_base = configure.get("windows-msvc-base", {})
    if msvc_base.get("cacheVariables", {}).get("CMAKE_CXX_COMPILER") != "cl.exe":
        errors.append("windows-msvc-base must select cl.exe explicitly")

    headless = configure.get("windows-msvc-headless-debug", {}).get("cacheVariables", {})
    expected_headless = {
        "CNA_PLATFORM": "HEADLESS",
        "CNA_AUDIO_PLATFORM": "NULL",
        "CNA_GRAPHICS_RENDERER": "HEADLESS",
        "CNA_ENABLE_SDL": "OFF",
        "CNA_BUILD_TESTS": "ON",
    }
    for key, expected in expected_headless.items():
        if headless.get(key) != expected:
            errors.append(f"windows-msvc-headless-debug must set {key}={expected}")

    sdlgpu = configure.get("windows-msvc-sdlgpu-debug", {}).get("cacheVariables", {})
    expected_sdlgpu = {
        "CNA_PLATFORM": "SDL3",
        "CNA_AUDIO_PLATFORM": "SDL3",
        "CNA_GRAPHICS_RENDERER": "SDL_GPU",
        "CNA_ENABLE_SDL": "ON",
        "CNA_BUILD_TESTS": "ON",
        "CNA_BUILD_EXAMPLES": "ON",
    }
    for key, expected in expected_sdlgpu.items():
        if sdlgpu.get(key) != expected:
            errors.append(f"windows-msvc-sdlgpu-debug must set {key}={expected}")

    vs_base = configure.get("windows-vs2022-base", {})
    if vs_base.get("generator") != "Visual Studio 17 2022":
        errors.append("windows-vs2022-base must use the Visual Studio 17 2022 generator")
    for name in ("windows-vs2022-base", "windows-vs2022-sdlgpu"):
        if "CMAKE_BUILD_TYPE" in configure.get(name, {}).get("cacheVariables", {}):
            errors.append(f"{name} must remain multi-configuration (no CMAKE_BUILD_TYPE)")

    missing_build = ({"windows-msvc-headless-debug"} | SMOKE_BUILD_PRESETS) - build.keys()
    if missing_build:
        errors.append(f"missing Windows build presets: {sorted(missing_build)}")
    for name in sorted(SMOKE_BUILD_PRESETS):
        actual = set(build.get(name, {}).get("targets", []))
        if actual != SMOKE_TARGETS:
            errors.append(
                f"{name} smoke targets differ: missing={sorted(SMOKE_TARGETS - actual)}, "
                f"extra={sorted(actual - SMOKE_TARGETS)}")

    for name, entry in build.items():
        if name.startswith("windows-") and entry.get("jobs", 1) > 8:
            errors.append(f"{name} exceeds the repository's eight-job build limit")

    missing_tests = EXPECTED_TEST_PRESETS - tests.keys()
    if missing_tests:
        errors.append(f"missing Windows test presets: {sorted(missing_tests)}")

    serialized = json.dumps(data, sort_keys=True)
    present_retired = sorted(name for name in RETIRED_WINDOWS_RENDERERS if name in serialized)
    if present_retired:
        errors.append(f"Windows presets name retired renderers: {present_retired}")

    if not WORKFLOW_PATH.exists():
        errors.append("missing automatic .github/workflows/windows-msvc-ci.yml baseline")
    else:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for trigger in ("push:", "pull_request:"):
            if trigger not in workflow:
                errors.append(f"Windows MSVC workflow lacks the automatic {trigger[:-1]} trigger")
        for command in (
                "cmake --preset windows-msvc-headless-debug",
                "cmake --build --preset windows-msvc-headless-debug",
                "ctest --preset windows-msvc-headless-debug",
                "cmake --preset windows-msvc-sdlgpu-debug",
                "cmake --build --preset windows-msvc-sdlgpu-debug",
                "ctest --preset windows-msvc-sdlgpu-debug"):
            if command not in workflow:
                errors.append(f"Windows MSVC workflow does not consume the shared contract: {command}")
        if "grep -q 'Skipped'" not in workflow:
            errors.append("Windows SDL_GPU workflow must reject CTest skips")

    if not VSCODE_SETTINGS_PATH.exists() or not VSCODE_EXTENSIONS_PATH.exists():
        errors.append("missing shared VS Code CMake configuration")
    else:
        vscode_settings = json.loads(VSCODE_SETTINGS_PATH.read_text(encoding="utf-8"))
        if vscode_settings.get("cmake.useCMakePresets") != "always":
            errors.append("VS Code must consume CMakePresets.json")
        if vscode_settings.get("C_Cpp.default.configurationProvider") != "ms-vscode.cmake-tools":
            errors.append("VS Code C++ configuration must come from CMake Tools")
        extensions = json.loads(VSCODE_EXTENSIONS_PATH.read_text(encoding="utf-8"))
        recommendations = set(extensions.get("recommendations", []))
        for extension in ("ms-vscode.cmake-tools", "ms-vscode.cpptools"):
            if extension not in recommendations:
                errors.append(f"VS Code does not recommend {extension}")

    if errors:
        print("Windows IDE preset contract failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Windows IDE preset contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
