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

Mach-O (plans/plan_apple_m4.md AM4-213) takes the same five steps with ld64's tools: `ld -r
-all_load` for the partial link and `nmedit -s` -- which turns every global not on its list into a
static symbol -- for the localization, and the verification is the same. Mach-O has no
`STB_GNU_UNIQUE`, so there the survivors must be exactly the `_cna_*` names.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


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


def link_line_tokens(module_dir: Path, build_dir: Path,
                     target: str = "cna_c_api") -> tuple[list[str], Path]:
    """The link command for cna_c_api, plus the directory its relative paths are based on.

    Only the Makefile generator writes `link.txt`. Under Ninja the link command lives in
    `build.ninja`, and reading it needs `ninja -t commands` -- which is why this used to fail the
    whole build with "link.txt does not exist; build the cna_c_api target before the static
    archive" on a Ninja tree where cna_c_api had in fact just been built. The two generators also
    differ in what their relative paths are relative to: link.txt's are relative to the module's
    binary directory, build.ninja's to the build root, so the base is returned alongside.
    """
    # Relative to the *module's* binary directory, not the top-level one: they differ when CNA is
    # consumed with add_subdirectory(<cna> CNA), which is why the module directory is passed in.
    path = module_dir / "CMakeFiles" / f"{target}.dir" / "link.txt"
    if path.exists():
        return path.read_text(encoding="utf-8").split(), module_dir.resolve()

    if not (build_dir / "build.ninja").exists():
        raise SystemExit(
            f"{path} does not exist and {build_dir / 'build.ninja'} does not either; "
            "build the cna_c_api target before the static archive.")
    ninja = shutil.which("ninja") or shutil.which("ninja-build")
    if ninja is None:
        raise SystemExit(
            "this is a Ninja build tree, so the link line comes from build.ninja, but no ninja "
            "executable was found to read it.")
    # The link is the last command Ninja reports for the target; CMake wraps it as ": && <cmd> && :".
    output = run([ninja, "-C", str(build_dir), "-t", "commands", target],
                 f"reading the {target} link command from build.ninja")
    lines = [line for line in output.splitlines() if line.strip()]
    if not lines:
        raise SystemExit(f"ninja reported no commands for {target}; build it first.")
    return lines[-1].split(), build_dir.resolve()


def read_link_line(module_dir: Path, build_dir: Path,
                   target: str = "cna_c_api") -> tuple[list[str], list[str], list[str]]:
    """Split CMake's own link line into objects, archives and external libraries."""
    tokens, working = link_line_tokens(module_dir, build_dir, target)
    objects: list[str] = []
    archives: list[str] = []
    external: list[str] = []
    previous = ""
    for token in tokens:
        option, previous = previous, token
        # ld64 spells a framework as two words; its name is an input the consumer needs too.
        if option == "-framework":
            if f"framework:{token}" not in external:
                external.append(f"framework:{token}")
            continue
        # The values of these options are not inputs: the image's own name and architecture.
        if option in {"-o", "-install_name", "-arch", "-compatibility_version", "-current_version"}:
            continue
        if token.startswith("-") or token.endswith("link.txt"):
            if token.startswith("-l"):
                external.append(token)
            continue
        # Shell punctuation and the compiler driver itself are not inputs. Ninja's form is
        # ": && /usr/bin/c++ ... && :", and neither generator names an input this way.
        if (token in {":", "&&", "cd"} or token.endswith("/c++") or token.endswith("/cc")
                or token.endswith("/em++") or token.endswith("/emcc")):
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
        elif token.endswith(".dylib") and not token.startswith("@"):
            if resolved not in external and not Path(token).name.startswith("libcna_c_api"):
                external.append(resolved)
        elif token.endswith(".tbd"):
            # An SDK text stub (`<sdk>/usr/lib/libcurl.tbd`): its path belongs to one Xcode
            # install, so the consumer names the library and its own SDK supplies the stub.
            library = Path(token).name.removeprefix("lib").removesuffix(".tbd")
            if f"-l{library}" not in external:
                external.append(f"-l{library}")
    if not objects or not archives:
        raise SystemExit("the link line yielded no objects or no archives; its format changed")
    return objects, archives, external


def link_architecture(module_dir: Path, build_dir: Path, target: str = "cna_c_api") -> str | None:
    """The `-arch` the dylib was linked for, which ld64's partial link must be told as well."""
    tokens, _ = link_line_tokens(module_dir, build_dir, target)
    for option, value in zip(tokens, tokens[1:]):
        if option == "-arch":
            return value
    return None


def macho_globals(nm: str, path: Path) -> list[tuple[str, str]]:
    """Defined external symbols of a Mach-O object, as (type letter, name with its underscore)."""
    output = run([nm, "-gU", str(path)], f"reading symbols from {path.name}")
    symbols = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            symbols.append((fields[-2], fields[-1]))
    return symbols


def build_macho(module_dir: Path, build_dir: Path, work: Path, output: Path) -> tuple[int, list[str], int]:
    """ld64's version of the partial link, localization and verification. Returns the number of
    exported symbols, the external inputs, and the archive count."""
    linker, nmedit, archiver, nm = (require(tool) for tool in ("ld", "nmedit", "ar", "nm"))
    objects, archives, external = read_link_line(module_dir, build_dir)
    architecture = link_architecture(module_dir, build_dir)

    combined = work / "cna_c_api_combined.o"
    run([linker, "-r", *(["-arch", architecture] if architecture else []), "-all_load",
         *archives, *objects, "-o", str(combined)], "the partial link")

    keep = sorted({name for _, name in macho_globals(nm, combined) if name.startswith("_cna_")})
    if not keep:
        raise SystemExit("the combined object exports no cna_* symbols at all")
    keep_file = work / "cna_c_api_exports.txt"
    keep_file.write_text("\n".join(keep) + "\n", encoding="utf-8")

    localized = work / "cna_c_api_localized.o"
    run([nmedit, "-s", str(keep_file), str(combined), "-o", str(localized)],
        "localizing the internal symbols")

    leaked = [(kind, name) for kind, name in macho_globals(nm, localized)
              if not name.startswith("_cna_")]
    if leaked:
        listing = "\n  ".join(f"{kind} {name}" for kind, name in leaked[:20])
        raise SystemExit(
            f"{len(leaked)} non-ABI symbols survived localization:\n  {listing}\n"
            "A static archive that publishes them is not the same ABI as the shared library.")

    if output.exists():
        output.unlink()
    run([archiver, "crs", str(output), str(localized)], "archiving")
    combined.unlink(missing_ok=True)
    localized.unlink(missing_ok=True)
    return len(keep), external, len(archives)


def global_symbols(nm: str, path: Path) -> list[tuple[str, str]]:
    output = run([nm, "-g", "--defined-only", str(path)], f"reading symbols from {path.name}")
    symbols = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2:
            symbols.append((fields[-2], fields[-1]))
    return symbols


def merge_archives(module_dir: Path, build_dir: Path, target: str, archiver: str,
                   output: Path) -> int:
    """Emscripten's static archive: the closure merged into one archive, nothing partially linked.

    Under Emscripten `cna_c_api` is itself a static library, so its "link" is an `ar` of its own
    objects; the closure is on the link line of the module that consumes it. Its objects are
    WebAssembly, which the host `ld -r` and `objcopy` cannot read, and neither step is needed there:
    what a wasm binary exports is decided when the final module links. So this collects that
    module's archives and merges them with the archiver's MRI script, which keeps members with the
    same name apart.
    """
    _, archives, _ = read_link_line(module_dir, build_dir, target)
    if output.exists():
        output.unlink()
    script = "".join([f"CREATE {output}\n", *(f"ADDLIB {archive}\n" for archive in archives),
                      "SAVE\nEND\n"])
    completed = subprocess.run([archiver, "-M"], input=script, text=True, capture_output=True)
    if completed.returncode != 0:
        raise SystemExit(f"merging the archives failed:\n{completed.stderr}")
    print(f"wrote {output.name}: {len(archives)} archives merged from the {target} link line")
    return 0


def advertised_dylib(entry: str) -> str:
    """The path a dylib advertises as its install name, when that is an absolute path that exists.

    The link line names whatever path CMake resolved, which for a package manager's library can be
    a versioned directory that the next upgrade deletes; the install name is the path the library
    promises to stay at (it is what LC_LOAD_DYLIB records in every image linked against it)."""
    otool = shutil.which("otool")
    if otool is None:
        return entry
    lines = run([otool, "-D", entry], f"reading the install name of {Path(entry).name}").splitlines()
    name = lines[-1].strip() if len(lines) >= 2 else ""
    return name if name.startswith("/") and Path(name).exists() else entry


def write_macho_targets(path: Path, output: Path, external: list[str]) -> None:
    """The Mach-O targets file: the same imported target, with ld64's link interface."""
    lines = [
        "# SPDX-License-Identifier: MS-PL",
        "# Generated by tools/c-api/generate_static_archive.py. Do not edit.",
        "#",
        "# The static half of the package on macOS. It is a single relocatable object in an archive,",
        "# with every symbol that is not part of the ABI made static by nmedit, so linking it",
        "# publishes the same names the shared library exports and no others.",
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
    seen: set[str] = set()
    for entry in external:
        if entry.startswith("/") and "libSDL3" not in entry:
            entry = advertised_dylib(entry)
        if entry in seen:
            continue
        seen.add(entry)
        if entry.startswith("framework:"):
            lines.append(f"    list(APPEND _cna_static_interface \"-Wl,-framework,{entry[10:]}\")")
        elif entry.startswith("-l"):
            lines.append(f"    list(APPEND _cna_static_interface \"{entry[2:]}\")")
        elif "libSDL3" in entry:
            # SDL ships inside this package (AM4-210), so the interface names the installed copy.
            lines.append(
                f"    list(APPEND _cna_static_interface \"${{_cna_package_lib_dir}}/{Path(entry).name}\")")
        else:
            lines.append(f"    list(APPEND _cna_static_interface \"{entry}\")")
    lines += [
        "    list(APPEND _cna_static_interface c++)",
        "    set_property(TARGET CNA::CApiStatic PROPERTY",
        "        INTERFACE_LINK_LIBRARIES \"${_cna_static_interface}\")",
        "endif()",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


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
    parser.add_argument(
        "--merge-from",
        help="Emscripten: merge the archives on this target's link line instead of partially "
             "linking the C API's own")
    parser.add_argument("--archiver", help="the archiver to merge with (Emscripten's llvm-ar)")
    parser.add_argument(
        "--object-format", choices=("elf", "macho"),
        default="macho" if sys.platform == "darwin" else "elf",
        help="the host's object format; the partial-link and localization tools differ")
    arguments = parser.parse_args()

    build_dir = Path(arguments.build_dir).resolve()
    module_dir = (Path(arguments.module_binary_dir).resolve()
                  if arguments.module_binary_dir
                  else build_dir / "modules" / "c-api")
    output = Path(arguments.output).resolve()
    work = Path(arguments.work_dir).resolve() if arguments.work_dir else output.parent
    work.mkdir(parents=True, exist_ok=True)

    if arguments.merge_from:
        if not arguments.archiver:
            raise SystemExit("--merge-from needs --archiver")
        result = merge_archives(module_dir, build_dir, arguments.merge_from, arguments.archiver,
                                output)
        if arguments.targets_file:
            Path(arguments.targets_file).write_text("\n".join([
                "# SPDX-License-Identifier: MS-PL",
                "# Generated by tools/c-api/generate_static_archive.py. Do not edit.",
                "#",
                "# Emscripten's static half: the C API and its whole closure in one archive.",
                "",
                "if(NOT TARGET CNA::CApiStatic)",
                "    add_library(CNA::CApiStatic STATIC IMPORTED)",
                "    set_target_properties(CNA::CApiStatic PROPERTIES",
                f"        IMPORTED_LOCATION \"${{_cna_package_lib_dir}}/{output.name}\"",
                "        INTERFACE_INCLUDE_DIRECTORIES \"${_cna_package_include_dir}\"",
                "        INTERFACE_COMPILE_DEFINITIONS \"CNA_C_API_STATIC\"",
                "    )",
                "endif()",
                "",
            ]), encoding="utf-8")
        return result

    if arguments.object_format == "macho":
        exported, external, archive_count = build_macho(module_dir, build_dir, work, output)
        if arguments.targets_file:
            write_macho_targets(Path(arguments.targets_file), output, external)
        print(f"wrote {output.name}: {exported} exported cna_* symbols, "
              f"{archive_count} archives combined")
        return 0

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
