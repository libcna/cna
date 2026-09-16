# CNA Win32 backend — native Windows validation and hardening

> **Status: IN PROGRESS.** This workstream takes the `CNA_PLATFORM=WIN32` backend that
> [`plan_win32.md`](plan_win32.md) built and validated *under Wine on Linux*, and re-establishes
> every one of its claims on **real Windows 10 with the real Microsoft toolchain**. Where Wine and
> Windows disagree, Windows is authoritative and the difference is a defect to fix, not a note to
> write.
>
> **Goal:** leave CNA with a Win32 backend whose evidence is native-Windows evidence, and with
> reusable infrastructure (`tools/platform/windows_vm_exec.sh`, `windows_vm_sync.sh`,
> `validate_win32_native.ps1`) that lets any later session repeat the whole run from Linux with one
> command.
>
> **Non-goal:** redesigning `Win32Platform`, adding capabilities that are deliberately `false`
> (IME, gamepad, tray, …), or treating a VirtualBox virtual-GPU limitation as a CNA defect.

---

## 0. Baseline

| Fact | Value |
|---|---|
| Baseline commit | `4cf33c2b` (`docs(WAYLAND-…): the final audit`), the tip of `next` |
| Branch | `win32-native-validation`, created from that commit |
| Working tree at start | clean |
| Host | Debian 13, Linux 6.12, x86-64 |
| sharp-runtime | sibling checkout, branch `next`, `88c12f15` |

### The laboratory

| Fact | Value |
|---|---|
| VirtualBox | 7.2.8 r173730 |
| VM name / UUID | `win10_local` / `6de53d51-f55d-4bf5-af20-9033279f6617` |
| Guest | Microsoft Windows 10 Home 22H2, build 19045.2965, x64 |
| vCPU / RAM | 4 / 8192 MB (raised from 4096 for this workstream) |
| Graphics | VBoxSVGA (WDDM), 256 MB VRAM, 3D acceleration on |
| Guest Additions | 7.2.8 |
| Network | NAT, with loopback-only forwards `127.0.0.1:2222 → 22` and `127.0.0.1:5985 → 5985` |
| Automation | OpenSSH Server (Windows capability `OpenSSH.Server~~~~0.0.1.0`), **public-key only**, default shell PowerShell 5.1 |
| Snapshot | `clean-ssh-no-devtools` — the guest with SSH configured and no development tools |
| Disk before toolchain | 85.9 GB free of 100 GB (77.3 GB after the 8 GB page file grew with the RAM increase) |

**The VM has a virtual GPU.** Everything this workstream records is therefore one of two different
things, and they are never conflated:

* **native Windows API validation** — `user32`, `gdi32`, `ole32`, `shell32`, the real message
  loop, the real clipboard, the real MSVC ABI. A VM does not weaken any of this; it *is* Windows.
* **virtual GPU validation** — D3D11/D3D12/WGL through VBoxSVGA. Useful integration evidence,
  and *not* evidence about a physical Windows GPU driver.

Physical-Windows-GPU validation remains **not done** and is recorded as such.

---

## 1. Tasks

| ID | Task | Status | Evidence |
|---|---|---|---|
| WINNATIVE-0001 | Discover VirtualBox and the Windows VM; record its configuration | ✅ | §0 |
| WINNATIVE-0002 | Boot the VM autonomously and reach a logged-in desktop | ✅ | headless start, autologon, screenshot |
| WINNATIVE-0003 | Establish reproducible remote automation from Linux (SSH, key auth, no stored password) | ✅ | `tools/platform/windows_vm_exec.sh` |
| WINNATIVE-0004 | Windows environment inventory | ✅ | §0 |
| WINNATIVE-0005 | Pre-toolchain snapshot | ✅ | `clean-ssh-no-devtools` |
| WINNATIVE-0006 | Install the minimum native toolchain (VS 2022 Build Tools C++, Windows 10 SDK, ASan, CMake, Ninja, Git, Python) | ⬜ | |
| WINNATIVE-0007 | Reproducible exact-commit source sync Linux → VM | ⬜ | `tools/platform/windows_vm_sync.sh` |
| WINNATIVE-0008 | First MSVC configure and build of the platform module | ⬜ | |
| WINNATIVE-0009 | Full integrated CNA + sharp-runtime MSVC build | ⬜ | |
| WINNATIVE-0010 | Win32 platform test suite on native Windows | ⬜ | |
| WINNATIVE-0011 | `PlatformConformanceTests` on native Win32 | ⬜ | |
| WINNATIVE-0012 | Full `CnaTests` on native Windows | ⬜ | |

*(the table is extended as the work proceeds; every defect found gets its own row)*

---

## 2. Findings

*(none recorded yet)*

---

## 3. How to repeat this

From a Linux checkout, with the VM present in VirtualBox:

```bash
tools/platform/windows_vm_exec.sh --start     # boot and wait for SSH
tools/platform/windows_vm_sync.sh             # put this exact commit into C:\src\cna
tools/platform/windows_vm_validate.sh         # configure, build, test, report
```
