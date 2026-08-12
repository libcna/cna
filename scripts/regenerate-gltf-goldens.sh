#!/usr/bin/env bash
# plan_gltf.md GLTF-020: the one command that regenerates the glTF conformance corpus.
#
# The corpus under tests/assets/gltf/ is GENERATED, never hand-edited: a fixture's .gltf, its .glb
# twin, its .expected.json manifest and its L5 vertex/index goldens all come from one Python
# description, which is what stops them drifting apart. Editing an asset by hand produces a corpus
# that disagrees with its own generator and passes nothing.
#
# This script exists so that regenerating is one command with no arguments to get wrong, and so the
# post-condition the plan's acceptance names is checked rather than assumed: after writing, it runs
# the generator's own --check and then, if this is a git worktree, reports whether the tree changed.
# On an unchanged tree the whole run is a zero diff.
#
# Usage:  scripts/regenerate-gltf-goldens.sh [--check]
#           (no argument)  regenerate, then verify
#           --check        verify only, write nothing -- what CI runs
#
# Exit codes: 0 ok · 1 the corpus does not match its generator · 2 the environment is unusable.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORPUS="tests/assets/gltf"
CHECK_ONLY=0

case "${1:-}" in
    --check) CHECK_ONLY=1 ;;
    "")      ;;
    *)       echo "usage: $(basename "$0") [--check]" >&2; exit 2 ;;
esac

cd "$REPO_ROOT"

if ! command -v python3 >/dev/null 2>&1; then
    echo "regenerate-gltf-goldens: python3 is required (the generator is stdlib-only)." >&2
    exit 2
fi
if [ ! -d "tools/gltf_fixtures" ]; then
    echo "regenerate-gltf-goldens: tools/gltf_fixtures is missing -- run this from a CNA checkout." >&2
    exit 2
fi

export PYTHONPATH="tools${PYTHONPATH:+:$PYTHONPATH}"

if [ "$CHECK_ONLY" -eq 0 ]; then
    python3 -m gltf_fixtures --out "$CORPUS"
fi

# The generator's own comparison: every emitted byte against what is on disk. This is the assertion,
# not the write above -- a generator that wrote nothing at all would still "succeed" without it.
python3 -m gltf_fixtures --check "$CORPUS"

# The second half of GLTF-020's acceptance, and the one a human actually reads: on an unchanged tree
# the regeneration must leave the working tree untouched. Reported rather than enforced, because a
# corpus change is a legitimate reason to run this -- but it must be visible, and a golden that
# changed for a reason nobody stated is exactly what GLTF-410 asks to be reviewable.
if git -C "$REPO_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    if git -C "$REPO_ROOT" diff --quiet -- "$CORPUS" && \
       [ -z "$(git -C "$REPO_ROOT" ls-files --others --exclude-standard -- "$CORPUS")" ]; then
        echo "regenerate-gltf-goldens: working tree unchanged -- the corpus already matched its generator."
    else
        echo "regenerate-gltf-goldens: the corpus CHANGED. Review the diff and say why in the commit:"
        git -C "$REPO_ROOT" status --short -- "$CORPUS"
    fi
fi
