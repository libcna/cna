#!/usr/bin/env bash
# loc.sh - C++ lines-of-code report for CNA, broken down per module and per
# category (include / src / tests / examples).
#
# Counts only real code lines: blank lines and comment-only lines (// line
# comments and /* block comments */, including multi-line blocks) are
# excluded. A line that mixes code with a trailing comment still counts as
# code.
#
# Usage: ./loc.sh
#
# Always recomputes from the current working tree and prints the table to
# the terminal. Self-contained: no external tools (cloc, ...) required,
# only python3.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULES_DIR="$ROOT_DIR/modules"

if [[ ! -d "$MODULES_DIR" ]]; then
    echo "error: modules directory not found at $MODULES_DIR" >&2
    exit 1
fi

python3 - "$MODULES_DIR" <<'PYEOF'
import os
import sys

MODULES_DIR = sys.argv[1]
EXTENSIONS = (".cpp", ".hpp", ".cc", ".cxx", ".h", ".ipp", ".inl", ".mm")
CATEGORIES = ("include", "src", "tests", "examples")


def count_file(path):
    """Return (code_lines, total_lines) for a C++ source file.

    Blank lines and comment-only lines (//..., /*...*/ possibly spanning
    multiple lines) are excluded from code_lines. String/char literals are
    tracked so that '//' or '/*' inside them is not mistaken for a comment.
    """
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return 0, 0

    in_block_comment = False
    code_lines = 0

    for line in lines:
        n = len(line)
        i = 0
        has_code = False

        while i < n:
            if in_block_comment:
                idx = line.find("*/", i)
                if idx == -1:
                    i = n
                else:
                    in_block_comment = False
                    i = idx + 2
                continue

            c = line[i]

            if c == "/" and i + 1 < n and line[i + 1] == "/":
                i = n
                continue

            if c == "/" and i + 1 < n and line[i + 1] == "*":
                in_block_comment = True
                i += 2
                continue

            if c == '"' or c == "'":
                quote = c
                has_code = True
                i += 1
                while i < n and line[i] != quote:
                    if line[i] == "\\":
                        i += 1
                    i += 1
                i += 1
                continue

            if not c.isspace():
                has_code = True
            i += 1

        if has_code:
            code_lines += 1

    return code_lines, len(lines)


def scan_dir(path):
    """Return (files, code_lines) for all C++ sources under path."""
    if not os.path.isdir(path):
        return 0, 0
    files = 0
    code_lines = 0
    for dirpath, _dirnames, filenames in os.walk(path):
        for name in filenames:
            if name.endswith(EXTENSIONS):
                cl, _tl = count_file(os.path.join(dirpath, name))
                files += 1
                code_lines += cl
    return files, code_lines


def discover_modules(modules_dir):
    """Leaf module roots: dirs with their own CMakeLists.txt that are not
    'examples' build subdirs (those are counted as the examples category of
    their owning module, not as modules in their own right)."""
    roots = []
    for dirpath, _dirnames, filenames in os.walk(modules_dir):
        if "CMakeLists.txt" not in filenames:
            continue
        if dirpath == modules_dir:
            continue
        rel = os.path.relpath(dirpath, modules_dir)
        parts = rel.split(os.sep)
        if "examples" in parts:
            continue
        roots.append(rel)
    return sorted(roots)


def fmt(n):
    return f"{n:,}"


def main():
    modules = discover_modules(MODULES_DIR)

    rows = []  # (name, {cat: (files, loc)}, total_files, total_loc)
    for mod in modules:
        mod_path = os.path.join(MODULES_DIR, mod)
        cat_stats = {}
        total_files = 0
        total_loc = 0
        for cat in CATEGORIES:
            files, loc = scan_dir(os.path.join(mod_path, cat))
            cat_stats[cat] = (files, loc)
            total_files += files
            total_loc += loc
        if total_files == 0:
            continue
        rows.append((mod.replace(os.sep, "/"), cat_stats, total_files, total_loc))

    name_w = max([len("MODULE")] + [len(r[0]) for r in rows]) + 2
    cat_w = 16

    header = f"{'MODULE':<{name_w}}" + "".join(
        f"{c.upper():>{cat_w}}" for c in CATEGORIES
    ) + f"{'TOTAL':>{cat_w}}"
    sep = "-" * len(header)

    print()
    print("CNA C++ lines-of-code report (code lines only: no blank lines, no comments)")
    print(f"modules dir: {MODULES_DIR}")
    print()
    print(header)
    print(sep)

    grand_cat_files = {c: 0 for c in CATEGORIES}
    grand_cat_loc = {c: 0 for c in CATEGORIES}
    grand_files = 0
    grand_loc = 0

    for name, cat_stats, total_files, total_loc in rows:
        cells = []
        for cat in CATEGORIES:
            files, loc = cat_stats[cat]
            grand_cat_files[cat] += files
            grand_cat_loc[cat] += loc
            cells.append(f"{fmt(loc):>{cat_w - 9}} ({files:>4})" if files else f"{'-':>{cat_w}}")
        line = f"{name:<{name_w}}" + "".join(
            c if c.startswith(" ") and len(c) == cat_w else f"{c:>{cat_w}}" for c in cells
        )
        line += f"{fmt(total_loc):>{cat_w - 9}} ({total_files:>4})"
        print(line)
        grand_files += total_files
        grand_loc += total_loc

    print(sep)
    total_cells = []
    for cat in CATEGORIES:
        total_cells.append(
            f"{fmt(grand_cat_loc[cat]):>{cat_w - 9}} ({grand_cat_files[cat]:>4})"
        )
    total_line = f"{'TOTAL':<{name_w}}" + "".join(f"{c:>{cat_w}}" for c in total_cells)
    total_line += f"{fmt(grand_loc):>{cat_w - 9}} ({grand_files:>4})"
    print(total_line)
    print()
    print(f"modules: {len(rows)}   files: {fmt(grand_files)}   code lines: {fmt(grand_loc)}")
    print()


main()
PYEOF
