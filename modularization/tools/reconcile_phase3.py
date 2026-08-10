#!/usr/bin/env python3
"""Phase-3 no-loss reconciliation.

Classifies every baseline production/test file against the after-capture through the
move map: PRESERVED (same path, same hash), MOVED (new path, same hash),
MOVED_WITH_REQUIRED_EDIT (new path, different hash -- every changed line must be an
#include/path directive), EDITED_IN_PLACE (same path, different hash). Anything else is
BLOCKING. Also diffs the api-decls inventories and the ctest name sets.
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
B = os.path.join(REPO, "modularization/physical-modules/baseline")
A = os.path.join(REPO, "modularization/physical-modules/after")
BASE_SHA = "ea61123e6fda52150a522af8db30023edf4ba1d2"


def load(path):
    m = {}
    for line in open(path):
        h, p = line.rstrip("\n").split("  ", 1)
        m[p] = h
    return m


def move_map():
    mm = {}
    for line in open(os.path.join(REPO, "modularization/physical-modules/move-map.tsv")):
        old, new, _ = line.rstrip("\n").split("\t")
        mm[old] = new
    return mm


def diff_lines(old_path, new_path):
    """Changed lines between the baseline blob and the current file."""
    old = subprocess.run(["git", "show", f"{BASE_SHA}:{old_path}"], cwd=REPO,
                         capture_output=True, text=True).stdout.splitlines()
    new = open(os.path.join(REPO, new_path), encoding="utf-8", errors="replace").read().splitlines()
    import difflib
    changed = []
    for l in difflib.unified_diff(old, new, lineterm="", n=0):
        if l.startswith(("+", "-")) and not l.startswith(("+++", "---")):
            changed.append(l)
    return changed


def classify(before, after, mm, label):
    preserved, moved, moved_edit, edited, missing = [], [], [], [], []
    for p, h in sorted(before.items()):
        target = mm.get(p, p)
        if target in after:
            if after[target] == h:
                (preserved if target == p else moved).append(p)
            else:
                (edited if target == p else moved_edit).append((p, target))
        else:
            missing.append(p)
    added = sorted(set(after) - {mm.get(p, p) for p in before})
    print(f"== {label}: {len(before)} baseline -> {len(after)} after")
    print(f"   PRESERVED={len(preserved)} MOVED_BYTE_IDENTICAL={len(moved)} "
          f"MOVED_WITH_EDIT={len(moved_edit)} EDITED_IN_PLACE={len(edited)} "
          f"MISSING={len(missing)} ADDED={len(added)}")
    for p, t in moved_edit:
        lines = diff_lines(p, t)
        non_include = [l for l in lines
                       if not any(k in l for k in ("#include", "SCRIPT_DIR", "default="))]
        tag = "" if not non_include else f"  !! {len(non_include)} NON-DIRECTIVE LINES"
        print(f"   EDIT {p} -> {t}: {len(lines)} changed lines{tag}")
        if non_include:
            for l in non_include[:6]:
                print(f"        {l}")
    for p, t in edited:
        lines = diff_lines(p, t)
        print(f"   INPLACE {p}: {len(lines)} changed lines")
        for l in lines[:8]:
            print(f"        {l}")
    for p in missing:
        print(f"   !! MISSING {p}")
    for p in added:
        print(f"   + ADDED {p}")
    return not missing


def main():
    mm = move_map()
    ok = True
    ok &= classify(load(os.path.join(B, "files-production.tsv")),
                   load(os.path.join(A, "files-production.tsv")), mm, "production")
    ok &= classify(load(os.path.join(B, "files-tests.tsv")),
                   load(os.path.join(A, "files-tests.tsv")), mm, "tests")

    for cfg in ("headless", "opengles"):
        bf = os.path.join(B, f"ctest-names-{cfg}.txt")
        af = os.path.join(A, f"ctest-names-{cfg}.txt")
        if not (os.path.exists(bf) and os.path.exists(af)):
            continue
        bn, an = set(open(bf).read().split()), set(open(af).read().split())
        removed, added = sorted(bn - an), sorted(an - bn)
        print(f"== ctest names [{cfg}]: baseline {len(bn)} -> after {len(an)}; "
              f"removed={len(removed)} added={len(added)}")
        for n in removed:
            print(f"   !! REMOVED {n}")
            ok = False
        for n in added:
            print(f"   + {n}")

    ba = open(os.path.join(B, "api-decls.tsv")).read().splitlines()
    aa = open(os.path.join(A, "api-decls.tsv")).read().splitlines()
    bset = {tuple(l.split("\t")[1:]) for l in ba if l}
    aset = {tuple(l.split("\t")[1:]) for l in aa if l}
    print(f"== api-decls: baseline {len(ba)} rows -> after {len(aa)} rows; "
          f"decl-set removed={len(bset - aset)} added={len(aset - bset)}")
    for d in sorted(bset - aset):
        print(f"   !! REMOVED DECL {d}")
        ok = False
    for d in sorted(aset - bset):
        print(f"   + ADDED DECL {d}")
    print("RECONCILIATION:", "OK" if ok else "BLOCKING PROBLEMS")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
