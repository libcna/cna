#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plan_gltf.md GLTF-405: opt-in, sparse access to Khronos reference models. This script does not
# make any model part of CNA and does not review or grant its model-specific licence.

set -euo pipefail

readonly SAMPLE_ASSETS_REPOSITORY="https://github.com/KhronosGroup/glTF-Sample-Assets"
readonly SAMPLE_ASSETS_REVISION="2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf"

usage()
{
    cat <<'EOF'
Usage:
  scripts/fetch-gltf-sample-assets.sh DEST MODEL [MODEL ...]
  scripts/fetch-gltf-sample-assets.sh --print-pin

Fetch only the named Models/<MODEL> directories from CNA's pinned Khronos
glTF-Sample-Assets revision into a new DEST directory. DEST must not exist.

Example:
  scripts/fetch-gltf-sample-assets.sh /tmp/cna-gltf-samples Box ChronographWatch

This is an opt-in developer reference checkout, never a build or CI dependency.
Before copying or redistributing any model, review its own README.md, LICENSE.md,
asset.copyright, and THIRD_PARTY_NOTICES.md's GLTF-018 procedure.
EOF
}

if [[ ${1:-} == "--help" || ${1:-} == "-h" ]]; then
    usage
    exit 0
fi

if [[ ${1:-} == "--print-pin" ]]; then
    if (( $# != 1 )); then
        printf '%s\n' "error: --print-pin takes no other arguments" >&2
        exit 2
    fi
    printf '%s@%s\n' "$SAMPLE_ASSETS_REPOSITORY" "$SAMPLE_ASSETS_REVISION"
    exit 0
fi

if (( $# < 2 )); then
    usage >&2
    exit 2
fi

readonly destination=$1
shift
readonly -a models=("$@")

if [[ -e "$destination" || -L "$destination" ]]; then
    printf "error: destination already exists; refusing to modify it: %s\n" "$destination" >&2
    exit 2
fi

declare -A seen_models=()
sparse_paths=()
for model in "${models[@]}"; do
    if [[ ! "$model" =~ ^[A-Za-z0-9._-]+$ ]]; then
        printf "error: model must be one Models/ directory name, without slashes: %s\n" \
            "$model" >&2
        exit 2
    fi
    if [[ -n ${seen_models[$model]+present} ]]; then
        printf "error: model was requested more than once: %s\n" "$model" >&2
        exit 2
    fi
    seen_models[$model]=1
    sparse_paths+=("Models/$model")
done
readonly -a sparse_paths

if ! command -v git >/dev/null 2>&1; then
    printf '%s\n' "error: git is required to fetch the pinned sample assets" >&2
    exit 1
fi

# Initialise a new checkout rather than cloning a moving branch. If a later operation fails, the
# partial new directory is deliberately retained for diagnosis; the script never removes data.
git init --quiet -- "$destination"
git -C "$destination" remote add origin "$SAMPLE_ASSETS_REPOSITORY"
git -c protocol.version=2 -C "$destination" fetch \
    --quiet --depth=1 --filter=blob:none --no-tags origin "$SAMPLE_ASSETS_REVISION"

readonly fetched_revision=$(git -C "$destination" rev-parse --verify 'FETCH_HEAD^{commit}')
if [[ "$fetched_revision" != "$SAMPLE_ASSETS_REVISION" ]]; then
    printf "error: fetched revision %s, expected %s\n" \
        "$fetched_revision" "$SAMPLE_ASSETS_REVISION" >&2
    exit 1
fi

for model in "${models[@]}"; do
    object_type=$(git -C "$destination" cat-file -t "$SAMPLE_ASSETS_REVISION:Models/$model" 2>/dev/null || true)
    if [[ "$object_type" != "tree" ]]; then
        printf "error: pinned revision has no Models/%s directory\n" "$model" >&2
        exit 2
    fi
done

git -C "$destination" sparse-checkout init --cone
git -C "$destination" sparse-checkout set "${sparse_paths[@]}"
git -C "$destination" checkout --quiet --detach "$SAMPLE_ASSETS_REVISION"

readonly checked_out_revision=$(git -C "$destination" rev-parse --verify HEAD)
if [[ "$checked_out_revision" != "$SAMPLE_ASSETS_REVISION" ]]; then
    printf "error: checkout resolved to %s, expected %s\n" \
        "$checked_out_revision" "$SAMPLE_ASSETS_REVISION" >&2
    exit 1
fi

printf "Fetched pinned reference models at %s:\n" "$checked_out_revision"
for model in "${models[@]}"; do
    printf "  %s/Models/%s\n" "$destination" "$model"
    find "$destination/Models/$model" -maxdepth 1 -type f \
        \( -name README.md -o -name LICENSE.md \) -print | sed 's/^/    licence metadata: /'
done
printf '%s\n' \
    "No model licence was reviewed by this fetch. Do not copy these files into CNA without the GLTF-018 review."
