#!/usr/bin/env python3
"""Check deterministic CNA build-performance policy invariants."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


SOURCE_DIR = Path(__file__).resolve().parents[2]


def resolve_presets(document: dict[str, Any]) -> dict[str, dict[str, str]]:
    presets = {preset["name"]: preset for preset in document["configurePresets"]}
    resolved: dict[str, dict[str, str]] = {}
    resolving: set[str] = set()

    def resolve(name: str) -> dict[str, str]:
        if name in resolved:
            return resolved[name]
        if name in resolving:
            raise ValueError(f"configure preset inheritance cycle at {name!r}")
        if name not in presets:
            raise ValueError(f"configure preset {name!r} does not exist")
        resolving.add(name)
        values: dict[str, str] = {}
        inherited = presets[name].get("inherits", [])
        if isinstance(inherited, str):
            inherited = [inherited]
        for parent in reversed(inherited):
            values.update(resolve(parent))
        values.update(presets[name].get("cacheVariables", {}))
        resolving.remove(name)
        resolved[name] = values
        return values

    for preset_name in presets:
        resolve(preset_name)
    return resolved


def check_presets(errors: list[str]) -> None:
    document = json.loads((SOURCE_DIR / "CMakePresets.json").read_text(encoding="utf-8"))
    try:
        presets = resolve_presets(document)
    except ValueError as error:
        errors.append(str(error))
        return

    required = {
        "dev": {
            "CMAKE_BUILD_TYPE": "Debug",
            "CNA_GRAPHICS_RENDERER": "STUB",
            "CNA_BUILD_TESTS": "OFF",
            "CNA_BUILD_EXAMPLES": "OFF",
            "CNA_BUILD_C_API": "OFF",
            "CNA_ENABLE_NET": "OFF",
            "CNA_ENABLE_VIDEO": "OFF",
            "CNA_ENABLE_DRACO": "OFF",
        },
        "unit": {
            "CMAKE_BUILD_TYPE": "Debug",
            "CNA_GRAPHICS_RENDERER": "STUB",
            "CNA_BUILD_TESTS": "ON",
            "CNA_BUILD_EXAMPLES": "OFF",
            "CNA_BUILD_C_API": "OFF",
            "CNA_ENABLE_NET": "OFF",
            "CNA_ENABLE_VIDEO": "OFF",
            "CNA_ENABLE_DRACO": "OFF",
        },
        "release-modules": {
            "CMAKE_BUILD_TYPE": "Release",
            "CNA_GRAPHICS_RENDERER": "STUB",
            "CNA_BUILD_TESTS": "OFF",
            "CNA_BUILD_EXAMPLES": "OFF",
            "CNA_BUILD_C_API": "OFF",
            "CNA_ENABLE_NET": "OFF",
            "CNA_ENABLE_VIDEO": "OFF",
            "CNA_ENABLE_DRACO": "OFF",
        },
        "dev-fast-debug": {"CNA_DEBUG_INFO": "LINE_TABLES"},
        "unit-pch": {"CNA_ENABLE_PCH": "ON"},
        "unit-unity": {"CNA_ENABLE_UNITY_BUILD": "ON"},
        "release-ipo": {"CNA_ENABLE_IPO": "ON"},
    }
    for preset_name, expected in required.items():
        actual = presets.get(preset_name)
        if actual is None:
            errors.append(f"missing required configure preset {preset_name!r}")
            continue
        for variable, value in expected.items():
            if actual.get(variable) != value:
                errors.append(
                    f"preset {preset_name!r} must set {variable}={value}; "
                    f"effective value is {actual.get(variable)!r}"
                )

    opt_in_variables = {"CNA_ENABLE_PCH", "CNA_ENABLE_UNITY_BUILD", "CNA_ENABLE_IPO"}
    for preset_name in ("base-ninja", "dev", "unit", "release-modules"):
        for variable in opt_in_variables:
            if presets.get(preset_name, {}).get(variable) == "ON":
                errors.append(f"baseline preset {preset_name!r} enables opt-in {variable}")


def check_default_off_options(errors: list[str]) -> None:
    checks = {
        "cmake/UnitTests.cmake": ("CNA_ENABLE_PCH",),
        "cmake/BuildPerformance.cmake": ("CNA_ENABLE_IPO", "CNA_ENABLE_UNITY_BUILD"),
    }
    for relative, option_names in checks.items():
        text = (SOURCE_DIR / relative).read_text(encoding="utf-8")
        for option_name in option_names:
            pattern = rf"option\(\s*{option_name}\b.*?\bOFF\s*\)"
            if not re.search(pattern, text, flags=re.DOTALL):
                errors.append(f"{relative} must declare {option_name} with default OFF")

    unit_tests = (SOURCE_DIR / "cmake/UnitTests.cmake").read_text(encoding="utf-8")
    if 'CNA_ENABLE_PCH AND _cna_test_group STREQUAL "content"' not in unit_tests:
        errors.append("the PCH pilot is no longer limited to the content test group")

    performance = (SOURCE_DIR / "cmake/BuildPerformance.cmake").read_text(encoding="utf-8")
    for required_fragment in (
        "set(_cna_unity_targets cna_core cna_math)",
        "set(_cna_unity_test_targets cna_core_test_objects cna_math_test_objects)",
        "if(EMSCRIPTEN OR CMAKE_CROSSCOMPILING)",
    ):
        if required_fragment not in performance:
            errors.append(f"missing build-performance scope guard: {required_fragment}")


def cmake_policy_files() -> list[Path]:
    files = [SOURCE_DIR / "CMakeLists.txt"]
    for root in (SOURCE_DIR / "cmake", SOURCE_DIR / "modules", SOURCE_DIR / "tools"):
        for path in root.rglob("*"):
            if not path.is_file() or (
                path.suffix != ".cmake" and path.name != "CMakeLists.txt"
            ):
                continue
            relative_parts = path.relative_to(SOURCE_DIR).parts
            if "toolchains" in relative_parts or "tests" in relative_parts:
                continue
            files.append(path)
    return sorted(set(files))


def check_no_global_raw_flags(errors: list[str]) -> None:
    raw_flag = re.compile(
        r"\bCMAKE_(?:C|CXX|EXE_LINKER|SHARED_LINKER|MODULE_LINKER|STATIC_LINKER)_FLAGS"
        r"(?:_[A-Z0-9_]+)?\b"
    )
    global_option = re.compile(r"\badd_(?:compile|link)_options\s*\(", re.IGNORECASE)
    allowed_consumer_lines = {
        'list(APPEND _configure_arguments "-DCMAKE_C_FLAGS=-fsanitize=${CNA_SANITIZE}")',
        'list(APPEND _configure_arguments "-DCMAKE_EXE_LINKER_FLAGS=${_linker_flags}")',
    }

    for path in cmake_policy_files():
        relative = path.relative_to(SOURCE_DIR).as_posix()
        for line_number, original_line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            line = original_line.split("#", 1)[0].strip()
            if not line:
                continue
            if relative == "modules/c-api/cmake/RunInstalledConsumer.cmake":
                if line in allowed_consumer_lines:
                    continue
            if raw_flag.search(line):
                errors.append(f"{relative}:{line_number}: raw global flag policy: {line}")
            if global_option.search(line):
                errors.append(f"{relative}:{line_number}: global build option policy: {line}")


def main() -> None:
    errors: list[str] = []
    check_presets(errors)
    check_default_off_options(errors)
    check_no_global_raw_flags(errors)
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        raise SystemExit(1)
    print("Build-performance policy checks passed: presets, opt-in scope, and raw flags.")


if __name__ == "__main__":
    main()
