#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# Clones CNA's sibling repositories (sharp-runtime, easy-gl, meta-gl, ...) next to the CNA
# checkout, where a developer's tree has them.
#
# Each sibling is cloned on the first of the candidate branches it actually has, and on
# `develop` when it has none of them. Pinning `develop` for every sibling made every CI build of
# CNA `next` fail at configure: `next` asks for sharp-runtime components that exist only on
# sharp-runtime's own `next` (plans/plan_native_platform_validation.md, "Next steps", step 1).
#
# Usage: scripts/ci/clone_siblings.sh "<candidate branches, space separated>" <repo> [<repo> ...]
#
# In a workflow the candidates are "${{ github.head_ref }} ${{ github.base_ref }} ${{ github.ref_name }}":
# a push tries the pushed branch; a pull request tries its source branch, then its target.
set -euo pipefail

candidates="${1:-}"
shift
cd "${GITHUB_WORKSPACE:-$(pwd)}/.."

for repo in "$@"; do
    url="https://github.com/openeggbert/${repo}.git"
    chosen=develop
    for branch in ${candidates}; do
        if git ls-remote --exit-code --heads "${url}" "${branch}" >/dev/null 2>&1; then
            chosen="${branch}"
            break
        fi
    done
    echo "${repo}: cloning branch ${chosen}"
    git clone --depth 1 --branch "${chosen}" "${url}" "${repo}"
done
