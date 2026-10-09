#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# Clones CNA's sibling repositories (sharp-runtime, easy-gl, meta-gl, ...) next to the CNA
# checkout, where a developer's tree has them.
#
# Each sibling is cloned on the branch scripts/ci/sibling_branch.sh chooses: the first of the
# candidate branches it actually has; failing that `next`, CNA's integration branch that topic
# branches (`x11`, ...) are cut from; and `develop` when it has neither. Pinning `develop` for every sibling made every CI build of
# CNA `next` fail at configure: `next` asks for sharp-runtime components that exist only on
# sharp-runtime's own `next` (plans/plan_native_platform_validation.md, "Next steps", step 1).
#
# Usage: scripts/ci/clone_siblings.sh "<candidate branches, space separated>" <repo> [<repo> ...]
#
# In a workflow the candidates are "${{ github.head_ref }} ${{ github.base_ref }} ${{ github.ref_name }}":
# a push tries the pushed branch; a pull request tries its source branch, then its target.
set -euo pipefail

# Resolved before the `cd` below: the script may have been invoked by a relative path.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
candidates="${1:-}"
shift
cd "${GITHUB_WORKSPACE:-$(pwd)}/.."

for repo in "$@"; do
    chosen="$("${script_dir}/sibling_branch.sh" "${candidates}" "${repo}")"
    echo "${repo}: cloning branch ${chosen}"
    git clone --depth 1 --branch "${chosen}" "https://github.com/libcna/${repo}.git" "${repo}"
done
