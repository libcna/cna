# Audit: scripts/run-proton-vkd3d.sh

## Metadata
- Source file: `scripts/run-proton-vkd3d.sh` (146 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (Proton-managed D3D12 test-execution wrapper, alternative to `run-wine-vkd3d.sh`)
- XNA/FNA relevance: N/A (developer/CI tooling)
- Main related tests: intended for D3D12 swap-chain-dependent scenarios (e.g. `examples/d3d12_swapchain_diag.cpp`) where the plain system-Wine + vkd3d-proton overlay approach fails

## Purpose
Runs a D3D12 `.exe` through Steam Proton's own launcher in a dedicated Proton-managed prefix,
solving a real, root-caused `CreateSwapChainForHwnd` crash that occurs when vkd3d-proton's
`d3d12.dll`/`d3d12core.dll` are overlaid onto a foreign system-Wine prefix (the approach
`run-wine-vkd3d.sh` uses) — Proton's own self-consistent DLL set (its own `dxgi.dll`/`wined3d.dll`/
`libvkd3d-*.dll`) is required for the vkd3d-proton handoff to actually work.

## Executive Verdict
**Exceptional — the standout file in this entire `scripts` shard.** This script documents and
defends against a real, serious incident: on 2026-07-14, an earlier iteration of this exact
investigation ran Proton's `wine` binary without `WINEPREFIX` set, which fell back to the
developer's own personal `~/.wine` prefix. Because Proton ships a newer Wine version than the
system Wine, this silently triggered an automatic prefix upgrade that rewrote the personal prefix's
registry files, then hung forever on a "found new hardware"/`install_mono` wizard — leaving two
zombie Wine process trees running for **9.5 hours** and visible desktop "not responding" popups.
The script's current safety measures are a direct, well-engineered response to that real incident:
1. A hard-refusal guard (lines 82-89) that unconditionally aborts if the resolved prefix path ever
   resolves to `~/.wine` or a subdirectory of it — explicitly described as "the seatbelt in case a
   future edit ever drops one of [the WINEPREFIX assignments] again," i.e. defense-in-depth, not a
   single point of failure.
2. Every Wine/Proton invocation is wrapped in `timeout --kill-after=15` (bootstrap) or
   `timeout --kill-after=5 60` (registry edits), specifically because the same setup wizard hang
   could recur even in the correct, isolated prefix — with `kill_this_prefix()` scoped strictly to
   the CNA-owned `WINEPREFIX`, so cleanup can never reach into the developer's personal session or
   any other prefix's processes.

## Checklist Results
- The bootstrap-then-overlay sequencing (run once with Proton's default D3D12 support to create
  `system32`, discard that result, then copy the vkd3d-proton DLLs and register overrides) is
  correctly explained as necessary because a hand-built `wineboot --init` hits the same first-run
  wizard hang under Proton's own DLL set.
- Proton's unreliable stdout forwarding for the child process is explicitly documented (write
  results to a file from within the target `.exe`, not stdout) — a real, non-obvious operational
  gotcha correctly flagged for anyone consuming this script's target binary's output.
- Dummy `SteamAppId`/`SteamGameId` values are clearly marked as placeholders with no meaningful
  value outside Steam's own catalog/overlay integration — not left unexplained as "magic" env vars.

## Detailed Findings
None — every non-obvious or risky-looking piece of this script (the prefix guard, the timeouts, the
bootstrap-discard sequencing) is explained by a real, specific incident or root-caused technical
constraint, not asserted without evidence.

## Cross-File Observations
Directly complements `run-wine-vkd3d.sh`: both scripts' own comments describe the exact same
technical relationship (vkd3d-proton's `d3d12.dll`/`d3d12core.dll` need either a fully
Proton-consistent DLL environment or don't work at all for swap-chain creation) from each side,
consistently.

## Missing or Weak Tests
N/A (this IS a test-execution mechanism, not itself under test).

## Positive Findings
This is one of the strongest examples of defense-in-depth incident-response engineering found in
this entire audit session: a real, costly incident (9.5 hours of hung processes touching a
developer's personal environment) produced a specific, well-reasoned, multi-layered fix (hard
guard + bounded timeouts + scoped cleanup), each layer explicitly justified by what could still go
wrong if only the other layers existed.

## Final Assessment
No findings. Exemplary incident-driven safety engineering.
