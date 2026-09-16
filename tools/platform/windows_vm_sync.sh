#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Put an exact CNA (and sharp-runtime) commit into the native Windows validation VM.
#
# The transport is a git bundle copied over SSH, so the VM needs no network access
# and no credentials, and the result is a real git repository in the guest -- the
# commit under test is verifiable there with `git rev-parse HEAD`, not asserted by
# whoever copied the files. The first sync ships a full bundle; every later sync
# ships only the commits the guest is missing, which is normally a few kilobytes.
#
# The guest checkouts are plain NTFS directories (C:\src\cna, C:\src\sharp-runtime),
# never a VirtualBox shared folder: shared folders do not have Windows filesystem
# semantics and are far too slow to build in.
#
# Usage:
#   windows_vm_sync.sh                 # sync both repositories at their current HEAD
#   windows_vm_sync.sh --cna-only
#   windows_vm_sync.sh --status        # report what the guest currently has
#
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXEC="$HERE/windows_vm_exec.sh"
CNA_ROOT="$(cd "$HERE/../.." && pwd)"
SR_ROOT="${CNA_SHARP_RUNTIME_ROOT:-$(cd "$CNA_ROOT/.." && pwd)/sharp-runtime}"
WORK="${TMPDIR:-/tmp}/cna-win-sync.$$"

die() { echo "windows_vm_sync: $*" >&2; rm -rf "$WORK"; exit 2; }
say() { echo "windows_vm_sync: $*" >&2; }

win_exec() { "$EXEC" -- "$@"; }

# guest_head <guest-dir> -> prints the SHA, or nothing when there is no repo yet
guest_head() {
  "$EXEC" -- "if (Test-Path '$1/.git') { git -C '$1' rev-parse HEAD } else { '' }" 2>/dev/null \
    | tr -d '\r' | grep -E '^[0-9a-f]{40}$' | head -1
}

if [ "${1:-}" = "--status" ]; then
  "$EXEC" --script /dev/null >/dev/null 2>&1 || true
  for d in C:/src/cna C:/src/sharp-runtime; do
    h="$(guest_head "$d")"
    echo "$d : ${h:-<absent>}"
  done
  exit 0
fi

mkdir -p "$WORK" || die "cannot create $WORK"

sync_one() {
  local name="$1" local_root="$2" guest_dir="$3"
  [ -d "$local_root/.git" ] || die "$local_root is not a git repository"

  local head branch dirty
  head="$(git -C "$local_root" rev-parse HEAD)"
  branch="$(git -C "$local_root" rev-parse --abbrev-ref HEAD)"
  dirty="$(git -C "$local_root" status --porcelain --untracked-files=no | wc -l)"
  [ "$dirty" -eq 0 ] || say "WARNING: $name has $dirty uncommitted tracked change(s); only committed state is synced"

  local have bundle
  have="$(guest_head "$guest_dir")"
  if [ "$have" = "$head" ]; then
    say "$name already at $head in the guest"
    return 0
  fi

  bundle="$WORK/$name.bundle"
  if [ -n "$have" ] && git -C "$local_root" cat-file -e "$have^{commit}" 2>/dev/null; then
    say "$name: ${have:0:12} -> ${head:0:12} (incremental bundle)"
    git -C "$local_root" bundle create "$bundle" "^$have" "$head" --branches="$branch" 2>/dev/null \
      || git -C "$local_root" bundle create "$bundle" "^$have" "$head" || die "bundle failed"
  else
    say "$name: full bundle (guest has ${have:-nothing})"
    git -C "$local_root" bundle create "$bundle" --all || die "bundle failed"
  fi
  say "$name bundle: $(du -h "$bundle" | cut -f1)"

  win_exec "New-Item -ItemType Directory -Force -Path C:/cna/bundles | Out-Null" >/dev/null \
    || die "cannot create C:/cna/bundles"
  "$EXEC" --push "$bundle" "C:/cna/bundles/$name.bundle" >/dev/null || die "scp of $name bundle failed"

  win_exec "
\$ErrorActionPreference = 'Stop'
\$b = 'C:/cna/bundles/$name.bundle'
\$d = '$guest_dir'
if (-not (Test-Path \"\$d/.git\")) {
    New-Item -ItemType Directory -Force -Path (Split-Path \$d) | Out-Null
    git clone --quiet \$b \$d
    git -C \$d remote remove origin
}
git -C \$d fetch --quiet \$b '+refs/heads/*:refs/remotes/bundle/*'
git -C \$d checkout --quiet --detach $head
git -C \$d reset --quiet --hard $head
git -C \$d clean -qfdx -e 'cmake-build-*' -e '.cna-keep'
\"HEAD  = \$(git -C \$d rev-parse HEAD)\"
\"clean = \$(( git -C \$d status --porcelain | Measure-Object ).Count -eq 0)\"
" || die "guest-side checkout of $name failed"
}

sync_one cna "$CNA_ROOT" "C:/src/cna"
if [ "${1:-}" != "--cna-only" ]; then
  sync_one sharp-runtime "$SR_ROOT" "C:/src/sharp-runtime"
fi

rm -rf "$WORK"
say "done"
