# Audit: scripts/run-wine-vkd3d.sh

## Metadata
- Source file: `scripts/run-wine-vkd3d.sh` (61 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (Wine/vkd3d-proton test-execution wrapper for the D3D12 backend)
- XNA/FNA relevance: N/A (developer/CI tooling)
- Main related tests: wraps every D3D12 CTest binary invocation (`cmake/Tests/D3D12Tests.cmake`)

## Purpose
D3D12 counterpart to `run-wine-dxvk.sh`/`run-wine-dxvk9.sh`, using a dedicated Wine prefix with
vkd3d-proton's `d3d12.dll`/`d3d12core.dll` natively overridden, and an analogous engagement gate
(vkd3d-proton's own "vkd3d-proton - applicationVersion: &lt;version&gt;" log line).

## Executive Verdict
Correct, with an honestly-scoped explanation of why this gate matters less than its DXVK
counterparts: WineD3D never implemented D3D12 at all, so there is no silent same-API-different-
implementation fallback risk the way DXVK/WineD3D's Direct3D 11 both existed side-by-side — the
comment correctly characterizes this gate as "mainly guards against a stale/misconfigured prefix
rather than a silent wrong-backend substitution."

## Checklist Results
- Correctly uses a separate dedicated prefix (`~/.wine-cna-d3d12`) from D3D11's, with an explicit
  rationale (D3D11/D3D12 need different translation layers with different native-DLL overrides;
  sharing one prefix would require constant re-registration).
- Same `PIPESTATUS[0]` exit-code-capture correctness as its D3D11/D3D9 siblings.
- `CNA_D3D12_SKIP_VKD3D_GATE=1` escape hatch is present "for consistency" even though the comment
  notes no such binary exists yet — a reasonable forward-looking parity choice with its siblings,
  not dead code.

## Detailed Findings
None.

## Cross-File Observations
Directly referenced by `run-proton-vkd3d.sh`'s own extensive comment explaining WHY a second,
Proton-based D3D12 launch path was needed at all (`CreateSwapChainForHwnd` crashes under this
script's hand-built system-Wine-prefix approach due to a genuine ABI/integration mismatch between
system `dxgi.dll` and vkd3d-proton's separately-overridden `d3d12.dll`) — the two scripts' own
comments are mutually consistent about this real, empirically-root-caused limitation.

## Missing or Weak Tests
N/A (this IS the test-execution mechanism, not itself under test).

## Positive Findings
The honest self-assessment of this gate's own reduced necessity (compared to the DXVK gates) is a
good example of not overstating a safeguard's value just because a similar-looking one exists
elsewhere.

## Final Assessment
No findings.
