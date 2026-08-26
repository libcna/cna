#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Build a static C API archive that keeps the same promise the shared library keeps.

A static CNA was refused for a long time for a good reason: an archive carries every object it
swallowed, so `ar`-ing the C API together with `cna_core`, `cna_runtime` and Sharp Runtime would
publish tens of thousands of C++ symbols into a consumer's program, and the ABI's whole claim --
2,720 `cna_*` names and nothing else -- would stop meaning anything.

The way to have both is not to skip the archiving but to finish it. This tool:

1. reads the link line CMake already computed for the shared library, so the set of objects and
   archives is exactly the one that produces the working `.so` rather than a hand-maintained list;
2. partially links all of it into **one relocatable object** (`ld -r --whole-archive`);
3. localizes every global symbol that is not part of the ABI (`objcopy --keep-global-symbols`);
4. verifies the result -- and this is the step that makes the configuration honest;
5. archives that single object.

What survives step 4 is `cna_*` and nothing else, with one measured exception: symbols GCC emits as
`STB_GNU_UNIQUE` (function-local statics in inline and template code) cannot be localized by
`objcopy`, by design, because their uniqueness is what makes them correct. They are mangled C++
names that no C program can collide with, they are never callable API, and this tool fails if any
non-`cna_*` symbol of any *other* binding survives -- so the exception cannot quietly widen.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# Relative to the *module's* binary directory, not the top-level one. Those are the same
# directory only when CNA is the top-level project; a consumer that does
# add_subdirectory(<cna> CNA) puts it at <build>/CNA/modules/c-api instead, which is why the
# module binary directory is passed in rather than reconstructed from the build root.
LINK_LINE = Path("CMakeFiles/cna_c_api.dir/link.txt")


def require(tool: str) -> str:
    found = shutil.which(tool)
    if found is None:
        raise SystemExit(f"{tool} is required to build the static archive.")
    return found


def run(command: list[str], description: str) -> str:
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode != 0:
        raise SystemExit(f"{description} failed:\n{completed.stderr.strip()}")
    return completed.stdout


def ninja_link_line(build_dir: Path, module_dir: Path) -> str | None:
    """The same link line, asked of Ninja, which writes no link.txt.

    `link.txt` is a Makefile-generator artifact. Under Ninja the link command lives in
    build.ninja, and `ninja -t commands` prints it; the last command needed to produce the
    library is the link itself. Its paths are relative to the build root rather than to the
    module directory, which is why the caller is told which root to resolve against.
    """
    ninja = shutil.which("ninja")
    if ninja is None or not (build_dir / "build.ninja").exists():
        return None
    try:
        relative = (module_dir / "libcna_c_api.so").resolve().relative_to(build_dir)
    except ValueError:
        return None
    completed = subprocess.run(
        [ninja, "-C", str(build_dir), "-t", "commands", str(relative)],
        capture_output=True, text=True)
    if completed.returncode != 0 or not completed.stdout.strip():
        return None
    return completed.stdout.strip().splitlines()[-1]


def read_link_line(
    module_dir: Path, build_dir: Path) -> tuple[list[str], list[str], list[str]]:
    """Split CMake's own link line into objects, archives and external libraries."""
    path = module_dir / LINK_LINE
    if path.exists():
        working = module_dir.resolve()
        tokens = path.read_text(encoding="utf-8").split()
    else:
        command = ninja_link_line(build_dir, module_dir)
        if command is None:
            raise SystemExit(
                f"{path} does not exist and no Ninja link line could be read; build the "
                "cna_c_api target before the static archive.")
        # Ninja wraps the link in a shell command; its paths are build-root relative.
        working = build_dir.resolve()
        tokens = command.split()
    objects: list[str] = []
    archives: list[str] = []
    external: list[str] = []
    for token in tokens:
        if token.startswith("-") or token.endswith("link.txt"):
            if token.startswith("-l"):
                external.append(token)
            continue
        # Shell punctuation and the compiler driver itself are not inputs. Ninja's form is
        # ": && /usr/bin/c++ ... && :", and neither generator names an input this way.
        if token in {":", "&&", "cd"} or token.endswith("/c++") or token.endswith("/cc"):
            continue
        resolved = token if Path(token).is_absolute() else str((working / token).resolve())
        if token.endswith(".o"):
            objects.append(resolved)
        elif token.endswith(".a"):
            # CMake repeats archives to satisfy cyclic dependencies. Repeating them under
            # --whole-archive would pull every member twice and every definition would collide.
            if resolved not in archives:
                archives.append(resolved)
        elif ".so" in token:
            if resolved not in external and not token.endswith("libcna_c_api.so"):
                external.append(resolved)
    if not objects or not archives:
        raise SystemExit("the link line yielded no objects or no archives; its format changed")
    return objects, archives, external


def global_symbols(nm: str, path: Path) -> list[tuple[str, str]]:
    output = run([nm, "-g", "--defined-only", str(path)], f"reading symbols from {path.name}")
    symbols = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            symbols.append((fields[-2], fields[-1]))
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument(
        "--module-binary-dir",
        help="the C API module's own binary directory; defaults to <build-dir>/modules/c-api, "
             "which is only correct when CNA is the top-level project")
    parser.add_argument("--output", required=True, help="the static archive to produce")
    parser.add_argument("--targets-file", help="a CMake file describing how to consume it")
    parser.add_argument("--work-dir", help="where the intermediate object goes")
    arguments = parser.parse_args()

    build_dir = Path(arguments.build_dir).resolve()
    module_dir = (Path(arguments.module_binary_dir).resolve()
                  if arguments.module_binary_dir
                  else build_dir / "modules" / "c-api")
    output = Path(arguments.output).resolve()
    work = Path(arguments.work_dir).resolve() if arguments.work_dir else output.parent
    work.mkdir(parents=True, exist_ok=True)

    linker, objcopy, archiver, nm = (require(tool) for tool in ("ld", "objcopy", "ar", "nm"))
    objects, archives, external = read_link_line(module_dir, build_dir)

    combined = work / "cna_c_api_combined.o"
    run([linker, "-r", "--whole-archive", *archives, "--no-whole-archive", *objects,
         "-o", str(combined)], "the partial link")

    keep = sorted({name for _, name in global_symbols(nm, combined) if name.startswith("cna_")})
    if not keep:
        raise SystemExit("the combined object exports no cna_* symbols at all")
    keep_file = work / "cna_c_api_exports.txt"
    keep_file.write_text("\n".join(keep) + "\n", encoding="utf-8")

    localized = work / "cna_c_api_localized.o"
    run([objcopy, f"--keep-global-symbols={keep_file}", str(combined), str(localized)],
        "localizing the internal symbols")

    # The verification that makes this configuration worth offering.
    leaked = [(binding, name) for binding, name in global_symbols(nm, localized)
              if not name.startswith("cna_") and binding != "u"]
    if leaked:
        listing = "\n  ".join(f"{binding} {name}" for binding, name in leaked[:20])
        raise SystemExit(
            f"{len(leaked)} non-ABI symbols survived localization:\n  {listing}\n"
            "A static archive that publishes them is not the same ABI as the shared library.")
    unique = sum(1 for binding, name in global_symbols(nm, localized)
                 if not name.startswith("cna_"))

    if output.exists():
        output.unlink()
    run([archiver, "crs", str(output), str(localized)], "archiving")
    # Both intermediates are the size of the archive itself. Keeping them would triple the cost of
    # every relink for no benefit -- the archive is the artifact, and it is reproducible from the
    # link line at any time.
    combined.unlink(missing_ok=True)
    localized.unlink(missing_ok=True)

    if arguments.targets_file:
        lines = [
            "# SPDX-License-Identifier: MS-PL",
            "# Generated by tools/c-api/build_static_archive.py. Do not edit.",
            "#",
            "# The static half of the package. It is a single relocatable object in an archive, with",
            "# every symbol that is not part of the ABI localized, so linking it publishes the same",
            "# names the shared library exports and no others.",
            "",
            "if(NOT TARGET CNA::CApiStatic)",
            "    add_library(CNA::CApiStatic STATIC IMPORTED)",
            "    set_target_properties(CNA::CApiStatic PROPERTIES",
            f"        IMPORTED_LOCATION \"${{_cna_package_lib_dir}}/{output.name}\"",
            "        INTERFACE_INCLUDE_DIRECTORIES \"${_cna_package_include_dir}\"",
            "        INTERFACE_COMPILE_DEFINITIONS \"CNA_C_API_STATIC\"",
            "    )",
            "    set(_cna_static_interface \"\")",
        ]
        for entry in external:
            if entry.startswith("-l"):
                lines.append(f"    list(APPEND _cna_static_interface \"{entry[2:]}\")")
            elif "libSDL3" in entry:
                # SDL ships inside this package, so the interface points at the installed copy
                # rather than at wherever it happened to be built.
                soname = Path(entry).name.split(".so")[0] + ".so"
                lines.append(
                    f"    list(APPEND _cna_static_interface \"${{_cna_package_lib_dir}}/{soname}\")")
            else:
                lines.append(f"    list(APPEND _cna_static_interface \"{entry}\")")
        lines += [
            "    list(APPEND _cna_static_interface stdc++ m pthread dl)",
            "    set_property(TARGET CNA::CApiStatic PROPERTY",
            "        INTERFACE_LINK_LIBRARIES \"${_cna_static_interface}\")",
            "endif()",
            "",
        ]
        Path(arguments.targets_file).write_text("\n".join(lines), encoding="utf-8")

    print(f"wrote {output.name}: {len(keep)} exported cna_* symbols, "
          f"{unique} unlocalizable C++ statics, {len(archives)} archives combined")
    return 0


if __name__ == "__main__":
    sys.exit(main())
