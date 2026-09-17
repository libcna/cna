#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Run a command inside the native Windows validation VM from Linux.
#
# The VM is a VirtualBox guest reached over SSH through a NAT port forward on
# the loopback interface only; nothing is exposed to the network. Authentication
# is by key -- no password is stored here or anywhere else in the repository.
#
# Usage:
#   windows_vm_exec.sh [options] -- <powershell command>
#   windows_vm_exec.sh [options] --script <file.ps1> [args...]
#   windows_vm_exec.sh --stdin < script.ps1   # quoting-proof: the preferred form
#   windows_vm_exec.sh --start            # boot the VM and wait for SSH
#   windows_vm_exec.sh --wait             # just wait for SSH
#   windows_vm_exec.sh --stop             # ACPI shutdown and wait for poweroff
#   windows_vm_exec.sh --reboot           # clean guest reboot, wait for SSH
#   windows_vm_exec.sh --push <src> <dst> # scp a file or directory into the VM
#   windows_vm_exec.sh --pull <src> <dst> # scp a file or directory out of the VM
#   windows_vm_exec.sh --info             # print the VM/SSH configuration
#
# Environment overrides:
#   CNA_WIN_VM        VirtualBox VM name          (default: win10_local)
#   CNA_WIN_SSH_PORT  host-side forwarded port    (default: 2222)
#   CNA_WIN_USER      guest user                  (default: vboxuser)
#   CNA_WIN_KEY       private key                 (default: ~/.ssh/cna_win10_vm)
#   CNA_WIN_VM_TYPE   headless | gui | separate   (default: headless)

set -uo pipefail

VM="${CNA_WIN_VM:-win10_local}"
PORT="${CNA_WIN_SSH_PORT:-2222}"
USER_NAME="${CNA_WIN_USER:-vboxuser}"
KEY="${CNA_WIN_KEY:-$HOME/.ssh/cna_win10_vm}"
VM_TYPE="${CNA_WIN_VM_TYPE:-headless}"
HOST=127.0.0.1

SSH_OPTS=(
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
  -o BatchMode=yes
  -o ConnectTimeout=8
  -o ServerAliveInterval=30
  -o ServerAliveCountMax=10
  -i "$KEY"
)

die() { echo "windows_vm_exec: $*" >&2; exit 2; }

# VBoxManage starts VBoxSVC, and VBoxSVC starts VBoxHeadless; each inherits the caller's working
# directory, and VBoxHeadless writes a <date>-VBoxHeadless-<pid>.log register dump there when the
# guest powers off. Run every VBoxManage call from a state directory so that dump never lands in
# the checkout the script was invoked from.
VBOX_STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/cna-windows-vm"
vbox() { mkdir -p "$VBOX_STATE_DIR" && (cd "$VBOX_STATE_DIR" && VBoxManage "$@"); }

vm_state() { vbox showvminfo "$VM" --machinereadable 2>/dev/null | sed -n 's/^VMState="\(.*\)"$/\1/p'; }

ssh_ok() {
  timeout 20 ssh -n "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" "exit 0" >/dev/null 2>&1
}

wait_ssh() {
  local limit="${1:-300}" waited=0
  while (( waited < limit )); do
    ssh_ok && { echo "windows_vm_exec: SSH ready after ${waited}s" >&2; return 0; }
    sleep 5; waited=$((waited + 5))
  done
  return 1
}

ensure_running() {
  local state; state="$(vm_state)"
  [ -n "$state" ] || die "VM '$VM' not found (VBoxManage list vms)"
  if [ "$state" != "running" ]; then
    echo "windows_vm_exec: starting '$VM' ($state -> running, --type $VM_TYPE)" >&2
    vbox startvm "$VM" --type "$VM_TYPE" >&2 || die "could not start '$VM'"
  fi
  wait_ssh 420 || die "VM is running but SSH never came up on port $PORT"
}

case "${1:-}" in
  --info)
    echo "vm=$VM state=$(vm_state) ssh=$USER_NAME@$HOST:$PORT key=$KEY type=$VM_TYPE"
    exit 0
    ;;
  --start)   ensure_running; exit 0 ;;
  --wait)    wait_ssh "${2:-420}" || die "SSH did not come up"; exit 0 ;;
  --stop)
    [ "$(vm_state)" = "running" ] || { echo "already off"; exit 0; }
    # A guest-side shutdown is the reliable one: this Windows 10 Home guest
    # ignores the ACPI power button, which is only kept as a fallback.
    if ssh_ok; then
      ssh "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" \
          "shutdown /s /t 0 /d p:0:0" >/dev/null 2>&1
    else
      vbox controlvm "$VM" acpipowerbutton >/dev/null 2>&1
    fi
    for _ in $(seq 1 60); do [ "$(vm_state)" = "poweroff" ] && { echo "poweroff"; exit 0; }; sleep 5; done
    die "guest did not power off within 300s"
    ;;
  --reboot)
    ensure_running
    ssh "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" "shutdown /r /t 0" >/dev/null 2>&1
    sleep 25
    wait_ssh 420 || die "VM did not come back after reboot"
    exit 0
    ;;
  --push)
    [ $# -ge 3 ] || die "--push needs <src> <dst>"
    ensure_running
    exec scp "${SSH_OPTS[@]}" -P "$PORT" -r "$2" "$USER_NAME@$HOST:$3"
    ;;
  --pull)
    [ $# -ge 3 ] || die "--pull needs <src> <dst>"
    ensure_running
    exec scp "${SSH_OPTS[@]}" -P "$PORT" -r "$USER_NAME@$HOST:$2" "$3"
    ;;
  --script)
    [ $# -ge 2 ] || die "--script needs a file"
    script="$2"; shift 2
    [ -f "$script" ] || die "no such script: $script"
    ensure_running
    remote="C:/cna/tmp/$(basename "$script")"
    ssh "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" \
        "New-Item -ItemType Directory -Force -Path C:/cna/tmp | Out-Null" >/dev/null || die "cannot create C:/cna/tmp"
    scp "${SSH_OPTS[@]}" -P "$PORT" "$script" "$USER_NAME@$HOST:$remote" >/dev/null || die "scp failed"
    exec ssh "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" \
        "powershell -NoProfile -ExecutionPolicy Bypass -File $remote $*"
    ;;
  --stdin)
    # Read the PowerShell from stdin. This is the one form that survives a here-doc unchanged,
    # so any script with quotes in it should use it rather than the argument form.
    # Read stdin FIRST: ensure_running probes over ssh, and ssh without -n would eat it.
    encoded="$(iconv -f UTF-8 -t UTF-16LE | base64 -w0)" || die "could not encode stdin"
    [ -n "$encoded" ] || die "--stdin got an empty script"
    ensure_running
    exec ssh "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" \
         "powershell -NoProfile -EncodedCommand $encoded"
    ;;
  --) shift ;;
  "") die "nothing to do; see the header of this script for usage" ;;
esac

ensure_running

# The guest's SSH default shell is PowerShell, and everything between here and it -- this shell,
# ssh's own argument joining, and PowerShell's re-parse of the resulting single string -- gets a
# turn at eating quotes. -EncodedCommand takes the command as UTF-16LE base64, so none of them
# can: what is written here is exactly what PowerShell parses.
# The progress stream has no terminal to draw on over SSH; left on, PowerShell serialises it as
# CLIXML into stdout and corrupts every captured result.
encoded="$(printf '%s' "\$ProgressPreference='SilentlyContinue'; $*" \
           | iconv -f UTF-8 -t UTF-16LE | base64 -w0)" \
  || die "could not encode the command (is iconv available?)"
exec ssh "${SSH_OPTS[@]}" -p "$PORT" "$USER_NAME@$HOST" \
     "powershell -NoProfile -EncodedCommand $encoded"
