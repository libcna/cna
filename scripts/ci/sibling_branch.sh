#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# Prints the branch of one CNA sibling repository (sharp-runtime, easy-gl, meta-gl, ...) that a CI
# build of CNA uses: the first of the candidate branches the sibling actually has; failing that
# `next`, CNA's integration branch; and `develop` when the sibling has neither. This is the one
# place that choice is made -- scripts/ci/clone_siblings.sh clones on it, and a workflow that checks
# a sibling out with actions/checkout passes it as `ref`. A workflow that pinned a sibling commit
# instead drifted from what CNA asks of it: four workflows failed at configure on a sharp-runtime
# that predated the components CNA requests (plans/plan_apple_m4.md AM4-234).
#
# Usage: scripts/ci/sibling_branch.sh "<candidate branches, space separated>" <repo>
set -euo pipefail

candidates="${1:-}"
repo="${2:?usage: sibling_branch.sh \"<candidates>\" <repo>}"
url="https://github.com/libcna/${repo}.git"

for branch in ${candidates} next; do
    if git ls-remote --exit-code --heads "${url}" "${branch}" >/dev/null 2>&1; then
        echo "${branch}"
        exit 0
    fi
done
echo develop
