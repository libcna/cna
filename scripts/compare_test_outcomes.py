#!/usr/bin/env python3
r"""Compare two CnaTests runs by OUTCOME, not by count (plan_runtimerenderer.md RTR-P9-27).

Usage:
    scripts/compare_test_outcomes.py before.log after.log "label"

Exit codes: 0 when no test lost its pass, 1 otherwise.

Why by outcome:

RTR-P9-27 originally said "single-renderer test counts unchanged". That criterion is wrong for this
phase, and following it would have hidden the work rather than checked it: converting a compile-time
gate to a runtime skip makes tests EXIST that previously did not, so the count necessarily rises.

What must hold is stronger and narrower: every test that PASSED before must still pass. A test that
newly appears as skipped is the intended gain; a test that stops passing is a regression, whether it
now fails outright or has silently vanished from the binary.

It also reports counts of UNIQUE test names. `grep -c '\[  SKIPPED \]'` double-counts, because
gtest prints each skipped test once inline and again in its trailing summary -- which is how a real
figure of 54 got reported as 197 during this phase.
"""
import re, sys

def _names(path, marker):
    names = set()
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            m = re.match(r'\[' + marker + r'\] ([\w/]+\.[\w/]+)', line)
            if m:
                names.add(m.group(1))
    return names


def passing(path):
    return _names(path, '       OK ')

def skipped(path):
    return _names(path, '  SKIPPED ')

before_p, after_p, label = sys.argv[1], sys.argv[2], sys.argv[3]
b, a = passing(before_p), passing(after_p)
bs, asz = skipped(before_p), skipped(after_p)

lost = sorted(b - a)
gained = sorted(a - b)
newly_skipped = sorted((asz - bs) & b)   # used to pass, now skipped -- the dangerous case

print(f"{label}:")
print(f"  passing before {len(b)}  after {len(a)}")
print(f"  skipped before {len(bs)}  after {len(asz)}")
print(f"  no longer passing: {len(lost)}")
for t in lost[:12]: print(f"      - {t}")
print(f"  newly passing:     {len(gained)}")
for t in gained[:6]: print(f"      + {t}")
if newly_skipped:
    print(f"  !! used to pass, now SKIPPED: {len(newly_skipped)}")
    for t in newly_skipped[:12]: print(f"      ! {t}")
sys.exit(1 if lost else 0)
