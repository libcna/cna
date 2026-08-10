#!/usr/bin/env python3
"""No-loss inventory capture for the CNA module-examples campaign.

Captures a machine-readable inventory of every tracked example file (index blob
SHA included, so byte-identity of moves is provable from git data alone), every
CMake reference to an example path, every example-referencing CMake target
definition, and every CTest registration in example-owning CMake files.
Deterministic, sorted output; run before and after the move campaign and
reconcile with reconcile_examples.py.

Usage:
  example_inventory.py --out <dir> [--roots examples modules]
"""
import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# CMake files whose example registrations this campaign owns. audit/ is a
# frozen historical record and is deliberately not scanned.
CMAKE_GLOBS = ["CMakeLists.txt", "cmake/**", "modules/**"]

EXAMPLE_TOKEN = re.compile(r"(?<![A-Za-z0-9_/.-])((?:examples|modules/[A-Za-z0-9_./-]*?/examples)/[A-Za-z0-9_./@-]*[A-Za-z0-9_-])")


def sh(args, cwd=REPO):
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True, text=True).stdout


def tracked(globs):
    out = subprocess.run(["git", "ls-files", "-z", "--"] + globs, cwd=REPO,
                         check=True, capture_output=True).stdout
    return sorted(p.decode("utf-8") for p in out.split(b"\0") if p)


def tracked_with_blobs(globs):
    """path -> index blob sha (mode 100644/100755 entries only)."""
    out = sh(["git", "ls-files", "-s", "--"] + globs)
    result = {}
    for line in out.splitlines():
        mode, sha, _stage, path = line.replace("\t", " ", 1).split(" ", 3)
        if mode.startswith("100"):
            result[path] = sha
    return result


def strip_cmake_comments(text):
    lines = []
    for line in text.splitlines():
        # good enough for this codebase: '#' never appears inside a needed path
        idx = line.find("#")
        if idx >= 0:
            line = line[:idx]
        lines.append(line)
    return "\n".join(lines)


def cmake_commands(text):
    """Yield (command, argstring) for every top-level command invocation."""
    text = strip_cmake_comments(text)
    for m in re.finditer(r"(?m)^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(", text):
        cmd = m.group(1)
        depth = 1
        i = m.end()
        while i < len(text) and depth:
            c = text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        yield cmd, text[m.end():i - 1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    # 1) Every tracked example file, with its index blob SHA.
    blobs = tracked_with_blobs(["examples/**", "examples/*",
                                "modules/*/examples/**",
                                "modules/renderers/*/examples/**"])
    with open(os.path.join(args.out, "example-files.tsv"), "w") as f:
        f.write("path\tblob\n")
        for path in sorted(blobs):
            f.write(f"{path}\t{blobs[path]}\n")

    # 2) Every example-path token in the owned CMake surface.
    refs = []
    targets = []
    tests = []
    cmake_files = [p for p in tracked(CMAKE_GLOBS)
                   if p.endswith(".cmake") or p.endswith("CMakeLists.txt")]
    for cf in cmake_files:
        with open(os.path.join(REPO, cf), encoding="utf-8") as fh:
            raw = fh.read()
        stripped = strip_cmake_comments(raw)
        for tok in sorted(set(EXAMPLE_TOKEN.findall(stripped))):
            refs.append((cf, tok))
        for cmd, argstr in cmake_commands(raw):
            argv = argstr.split()
            lower = cmd.lower()
            if lower == "add_executable" and argv:
                ex_sources = [a for a in argv[1:] if "examples/" in a]
                if ex_sources:
                    targets.append((cf, argv[0], ";".join(ex_sources)))
            elif re.fullmatch(r"cna_[a-z0-9_]*_test", lower) and len(argv) >= 2 \
                    and any("examples/" in a for a in argv):
                # per-renderer macro: cna_<family>_test(<target> <src...>)
                targets.append((cf, argv[0],
                                ";".join(a for a in argv[1:] if "examples/" in a)))
            elif lower == "add_test":
                m = re.search(r"NAME\s+(\S+)", argstr)
                if m:
                    tests.append((cf, m.group(1)))
            elif lower == "cna_register_renderer_test":
                m = re.search(r"NAME\s+(\S+)", argstr)
                if m:
                    tests.append((cf, m.group(1)))
            elif lower == "cna_register_skia_display_test" and argv:
                tests.append((cf, argv[0]))

    with open(os.path.join(args.out, "cmake-example-refs.tsv"), "w") as f:
        f.write("cmake_file\texample_token\n")
        for cf, tok in sorted(set(refs)):
            f.write(f"{cf}\t{tok}\n")

    with open(os.path.join(args.out, "example-targets.tsv"), "w") as f:
        f.write("cmake_file\ttarget\texample_sources\n")
        for row in sorted(set(targets)):
            f.write("\t".join(row) + "\n")

    with open(os.path.join(args.out, "ctest-registrations.tsv"), "w") as f:
        f.write("cmake_file\ttest_name\n")
        for row in sorted(set(tests)):
            f.write("\t".join(row) + "\n")

    # 3) Aggregate per example source: which cmake files reference it.
    by_path = {}
    for cf, tok in refs:
        by_path.setdefault(tok, set()).add(cf)
    with open(os.path.join(args.out, "example-consumers.tsv"), "w") as f:
        f.write("example_path\tconsumer_cmake_files\n")
        for tok in sorted(by_path):
            f.write(f"{tok}\t{','.join(sorted(by_path[tok]))}\n")

    print(f"captured {len(blobs)} example files, {len(set(refs))} cmake refs, "
          f"{len(set(targets))} example target rows, {len(set(tests))} ctest rows -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
