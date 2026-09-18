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
  # Asked of git rather than tested as a directory: in a linked worktree `.git` is a file.
  git -C "$local_root" rev-parse --git-dir >/dev/null 2>&1 \
    || die "$local_root is not a git repository"

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
    # A bundle records REFS, not commits: `git bundle create f ^A <sha>` names no ref at all and
    # git refuses it as empty. The branch is bundled by name, with the guest's commit as the
    # prerequisite, so only the new objects travel.
    git -C "$local_root" bundle create "$bundle" "^$have" "refs/heads/$branch" \
      || die "bundle failed"
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
# .sdl-prebuilt-* is the persistent prefix the SDL sub-build installs into; wiping it on
# every sync would mean rebuilding SDL from source each time, which is minutes, not seconds.
git -C \$d clean -qfdx -e 'cmake-build-*' -e '.cna-keep' -e 'vendor' -e 'third_party' -e '.sdl-prebuilt-*'
# A payload shipped from a submodule checkout carried its gitlink file, which points nowhere in
# the guest and makes every git command in the checkout fail. Payloads no longer ship one; this
# removes any left by an earlier sync, before git is asked about the tree.
foreach (\$p in '$PAYLOADS'.Split(' ')) { Remove-Item -Force -Recurse \"\$d/\$p/.git\" -ErrorAction SilentlyContinue }
\"HEAD  = \$(git -C \$d rev-parse HEAD)\"
# Counting the lines of a failed status would report a tree git could not even read as clean.
\$porcelain = @(git -C \$d status --porcelain)
if (\$LASTEXITCODE -ne 0) { throw \"git status failed with exit \$LASTEXITCODE; the checkout cannot be verified clean\" }
\"clean = \$(\$porcelain.Count -eq 0)\"
" || die "guest-side checkout of $name failed"
}

# --- vendored payloads -------------------------------------------------------------------------
# The submodule paths (vendor/googletest, third_party/SDL, ...) hold real content in this working
# copy but are gitlinks in the history, so a bundle carries none of it and a fresh guest checkout
# would be missing googletest entirely. They are shipped as a tar stream instead, and only when
# their content has actually changed -- the stamp is a hash of the tree, so a re-sync of an
# unchanged payload costs one comparison rather than a transfer.
#
# CNA_WIN_PAYLOADS overrides the list. The default is what a Win32 + SDL-OFF build needs and no
# more: the SDL submodules are hundreds of megabytes that this configuration deliberately never
# compiles.
PAYLOADS="${CNA_WIN_PAYLOADS:-vendor/googletest}"

payload_stamp() {
  # .git is excluded here as in the archive: a submodule checkout (a linked worktree has one) holds
  # a gitlink file there that means nothing in the guest.
  ( cd "$CNA_ROOT/$1" && find . -name .git -prune -o -type f -printf '%P %s\n' 2>/dev/null \
      | LC_ALL=C sort | sha1sum | cut -c1-40 )
}

push_payload() {
  local rel="$1"
  [ -d "$CNA_ROOT/$rel" ] || { say "payload $rel: not present locally, skipped"; return 0; }
  local stamp guest_stamp
  stamp="$(payload_stamp "$rel")"
  guest_stamp="$("$EXEC" -- "\$p='C:/src/cna/$rel/.cna-payload-stamp'; if (Test-Path \$p) { (Get-Content \$p -Raw).Trim() } else { '' }" 2>/dev/null | tr -d '\r' | tail -1)"
  if [ "$stamp" = "$guest_stamp" ]; then
    say "payload $rel: already current ($stamp)"
    return 0
  fi
  say "payload $rel: shipping ($(du -sh "$CNA_ROOT/$rel" | cut -f1))"
  tar --exclude=.git -C "$CNA_ROOT" -czf "$WORK/payload.tgz" "$rel" || die "could not archive $rel"
  win_exec "New-Item -ItemType Directory -Force -Path C:/cna/payloads | Out-Null" >/dev/null
  "$EXEC" --push "$WORK/payload.tgz" "C:/cna/payloads/payload.tgz" >/dev/null || die "scp of $rel failed"
  win_exec "
\$ErrorActionPreference = 'Stop'
Remove-Item -Recurse -Force 'C:/src/cna/$rel' -ErrorAction SilentlyContinue
tar -xzf C:/cna/payloads/payload.tgz -C C:/src/cna
Set-Content -Path 'C:/src/cna/$rel/.cna-payload-stamp' -Value '$stamp'
\"$rel = \$((Get-ChildItem -Recurse -File 'C:/src/cna/$rel' | Measure-Object).Count) files\"
" || die "guest-side extraction of $rel failed"
  rm -f "$WORK/payload.tgz"
}

sync_one cna "$CNA_ROOT" "C:/src/cna"
if [ "${1:-}" != "--cna-only" ]; then
  sync_one sharp-runtime "$SR_ROOT" "C:/src/sharp-runtime"
fi
for p in $PAYLOADS; do push_payload "$p"; done

rm -rf "$WORK"
say "done"
