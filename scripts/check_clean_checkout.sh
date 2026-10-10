#!/bin/sh
# SPDX-License-Identifier: MS-PL
#
# Fails when validation left the checkout dirty: a tracked file modified or deleted, or a new
# untracked file that .gitignore does not cover. Build trees (/build*/, cmake-build-*) and the
# other ignored outputs are legitimate and never reported.
#
#   scripts/check_clean_checkout.sh
#       The checkout must be clean now -- the form for CI, which starts from a fresh clone.
#
#   scripts/check_clean_checkout.sh -- <command> [arguments...]
#       Snapshots the checkout, runs the command, and fails if the command changed it. Changes
#       that were already there before the command ran are not blamed on it, so this works in a
#       working copy with local edits. e.g.
#         scripts/check_clean_checkout.sh -- tools/platform/run_gpu_tests_private.sh cmake-build-headless
#
# Exit status: the command's own status when it failed; otherwise 1 if the checkout changed,
# 0 if not. Nothing is ever reverted -- the offending paths are printed for a person to look at.

set -u

REPO=$(git -C "$(dirname "$0")" rev-parse --show-toplevel) || exit 2

# One line per changed path (tracked changes and untracked, non-ignored files), followed by a
# hash of the tracked content, so an edit to an already-modified file is still noticed.
snapshot() {
    git -C "$REPO" status --porcelain=v1 --untracked-files=all
    git -C "$REPO" diff HEAD --binary | cksum
}

if [ "$#" -eq 0 ]; then
    DIRTY=$(git -C "$REPO" status --porcelain=v1 --untracked-files=all)
    if [ -n "$DIRTY" ]; then
        echo "check_clean_checkout: the checkout is not clean:" >&2
        echo "$DIRTY" >&2
        exit 1
    fi
    echo "check_clean_checkout: checkout is clean"
    exit 0
fi

if [ "$1" != "--" ] || [ "$#" -lt 2 ]; then
    echo "usage: $0 [-- <command> [arguments...]]" >&2
    exit 2
fi
shift

BEFORE=$(snapshot)
"$@"
STATUS=$?
AFTER=$(snapshot)

if [ "$BEFORE" != "$AFTER" ]; then
    echo "check_clean_checkout: '$1' changed the checkout. Status before:" >&2
    echo "$BEFORE" | sed 's/^/  /' >&2
    echo "after:" >&2
    echo "$AFTER" | sed 's/^/  /' >&2
    [ "$STATUS" -ne 0 ] && exit "$STATUS"
    exit 1
fi
exit "$STATUS"
