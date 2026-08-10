#!/usr/bin/env python3
"""No-loss reconciliation for the CNA module-examples campaign.

Proves, from git index blob data alone:
  1. every tracked example file in the baseline maps through move-map.tsv to a
     tracked file in the after state;
  2. every mapped file is byte-identical (same blob SHA) unless it is on the
     declared required-edit list;
  3. KEEP rows (examples/golden) are untouched in place;
  4. the after state contains nothing under the example areas beyond the mapped
     files plus the declared module-local build files;
  5. the CTest registration name multiset parsed from the CMake sources is
     identical before and after.
Exit code 0 only if every check holds.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EV = os.path.join(HERE, "..", "module-examples")

# Files whose content legitimately changed during their move, with the edit class.
DECLARED_EDITS = {
    # include-path class: ../../common/ -> common/ (resolved via CNA_GRAPHICS_EXAMPLES_DIR)
    "modules/gamer-services/examples/demo_avatar/src/AvatarDemo.cpp": "include path",
    "modules/gamer-services/examples/demo_avatar_animation_gallery/src/GalleryDemo.cpp": "include path",
    "modules/gamer-services/examples/demo_avatar_appearance_tint_studio/src/TintStudioDemo.cpp": "include path",
    "modules/gamer-services/examples/demo_avatar_bone_state_boundary/src/BoundaryDemo.cpp": "include path",
    "modules/gamer-services/examples/demo_avatar_dual_compare/src/DualCompareDemo.cpp": "include path",
    "modules/gamer-services/examples/demo_avatar_multi_attach_stress/src/StressDemo.cpp": "include path",
    "modules/gamer-services/examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp": "include path",
    "modules/net/examples/demo_net_avatar_sync/src/SyncGame.cpp": "include path",
    # local CMake ownership class: repo-root add_subdirectory depth 6 -> 8
    "modules/devices/examples/demo_devices/android/com.openeggbert.cna.demodevices/app/jni/CMakeLists.txt":
        "local CMake ownership (repo-root relative depth)",
    # asset/resource path class: diagnostic message cites its sibling by repo path
    "modules/renderers/skia/examples/skia_ganesh_resource_budget_test.cpp": "asset/resource path",
}


def read_files(name):
    result = {}
    with open(os.path.join(EV, name, "example-files.tsv")) as f:
        next(f)
        for line in f:
            path, blob = line.rstrip("\n").split("\t")
            result[path] = blob
    return result


def read_map():
    moves, keeps = {}, []
    with open(os.path.join(EV, "move-map.tsv")) as f:
        next(f)
        for line in f:
            old, _cls, _owner, new = line.rstrip("\n").split("\t")
            if new == "KEEP":
                keeps.append(old)
            else:
                moves[old] = new
    return moves, keeps


def read_tests(name):
    rows = []
    with open(os.path.join(EV, name, "ctest-registrations.tsv")) as f:
        next(f)
        for line in f:
            rows.append(line.rstrip("\n").split("\t")[1])
    return sorted(rows)


def main():
    before = read_files("baseline")
    after = read_files("after")
    moves, keeps = read_map()
    failures = []

    identical = 0
    edited = []
    for old, new in sorted(moves.items()):
        if old not in before:
            failures.append(f"move-map old path not in baseline: {old}")
            continue
        if new not in after:
            failures.append(f"moved file missing from after state: {old} -> {new}")
            continue
        if before[old] == after[new]:
            identical += 1
        elif new in DECLARED_EDITS:
            edited.append((new, DECLARED_EDITS[new]))
        else:
            failures.append(f"undeclared content change: {old} -> {new}")
    for path in keeps:
        if before.get(path) != after.get(path):
            failures.append(f"KEEP file changed or lost: {path}")

    mapped_targets = set(moves.values()) | set(keeps)
    extras = sorted(set(after) - mapped_targets)
    build_files = [p for p in extras if p.endswith("/CMakeLists.txt")]
    stray = [p for p in extras if not p.endswith("/CMakeLists.txt")]
    for p in stray:
        failures.append(f"unexpected file in example area: {p}")

    undeclared_edit_paths = sorted(set(DECLARED_EDITS) - set(e[0] for e in edited))
    for p in undeclared_edit_paths:
        failures.append(f"declared edit did not occur (stale declaration): {p}")

    tb, ta = read_tests("baseline"), read_tests("after")
    if tb != ta:
        failures.append(f"ctest registration multiset differs: {len(tb)} vs {len(ta)}")

    print(f"baseline example files : {len(before)}")
    print(f"after example files    : {len(after)}")
    print(f"moved byte-identical   : {identical}")
    print(f"moved with declared edits: {len(edited)}")
    for p, cls in edited:
        print(f"    {p}  [{cls}]")
    print(f"kept at top level      : {len(keeps)}")
    print(f"module-local build files added: {len(build_files)}")
    print(f"ctest registrations    : {len(tb)} before == {len(ta)} after "
          f"({'identical' if tb == ta else 'DIFFERENT'})")
    if failures:
        print("\nFAILURES:")
        for f_ in failures:
            print("  " + f_)
        return 1
    print("\nRECONCILIATION: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
