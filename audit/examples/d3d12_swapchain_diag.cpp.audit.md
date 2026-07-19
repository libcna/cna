# Audit: examples/d3d12_swapchain_diag.cpp

## Metadata
- Source file: `examples/d3d12_swapchain_diag.cpp` (93 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-d3d12` shard
- File type: standalone manual diagnostic executable — NOT registered as a CTest (explicitly, per
  its own header comment)
- XNA/FNA relevance: exercises `D3D12GraphicsBackend`'s real windowed swap-chain path (CNA-internal
  backend implementation, underlying the public `GraphicsDevice` API)

## Purpose
One-time, honest manual diagnostic for D3D12's real (window-attached) swap-chain path — proving
swap-chain creation and a real multi-frame `Clear()`+`Present()` cycle work end-to-end under a
properly Proton-managed launch, distinct from vanilla Wine's own `dxgi.dll` (which crashes on
`DXGI_SWAP_EFFECT_FLIP_DISCARD` with a D3D12 command queue, per the file's cited DX-100/DX-102
finding).

## Executive Verdict
Honest and well-engineered as a manual diagnostic, explicitly modeled on this project's own
`cna_diag_software` precedent for "real executable, deliberately not wired into the default CTest
green suite because the required environment (here, a full Proton bootstrap launch) is too
heavy/slow for normal CTest runs."

## Checklist Results
- File-based logging (`C:\d3d12_swapchain_diag.log`) is explicitly justified: this diagnostic also
  runs under Proton's own launcher (`STEAM_COMPAT_*`), whose stdout isn't reliably captured by a
  wrapping shell — a genuinely correct, non-obvious cross-environment logging concern.
- The 10-frame `Clear()`+`Present()` loop uses a different color each frame specifically so a human
  reviewing the log (or watching the window on real Windows) can tell each frame genuinely reached
  the screen, rather than the same clear silently repeating — a real anti-false-positive design
  choice for a human-reviewed diagnostic.
- `IsSwapChainAvailableEXT()` is checked before attempting the `Clear()`/`Present()` loop, with a
  distinct, honest log line ("Swap chain unavailable -- skipping...") rather than crashing or
  silently no-op'ing if the swap chain isn't available.

## Detailed Findings
None.

## Cross-File Observations
Shares the "not a CTest, needs a heavier/real environment, explicitly documented as such" pattern
with `cna_diag_software`/`canvas_smoke_test.cpp` (also audited in this batch) — consistent,
repeated honest disclosure of manual-verification-only paths across multiple backends rather than
each backend inventing its own undocumented convention.

## Missing or Weak Tests
This file IS the (manual, non-CTest) test for this exact scenario; not applicable to ask for
"missing tests" within it. The genuine gap it discloses (no automated CTest coverage of the real
windowed swap-chain path, since a full Proton bootstrap is impractical for routine CI) is already
disclosed candidly rather than hidden, and the file's own comment states the actual verification
path is this diagnostic plus DX-114's real Windows hardware pass.

## Positive Findings
The Wine-vs-Proton distinction (a real, previously-encountered environment-specific crash under
plain Wine's `dxgi.dll`, now correctly worked around by using Proton's launcher instead) is
documented precisely enough that a future maintainer investigating a similar D3D12/DXGI oddity
would immediately know which environment to test in.

## Final Assessment
No findings.
