# Audit: scripts/run-wine-dxvk.sh

## Metadata
- Source file: `scripts/run-wine-dxvk.sh` (65 lines)
- Audit status: AUDITED (full read)
- Subsystem: `scripts` shard
- File type: Bash script (Wine/DXVK test-execution wrapper for the D3D11 backend)
- XNA/FNA relevance: N/A (developer/CI tooling)
- Main related tests: wraps every D3D11 CTest binary invocation (`cmake/Tests/D3D11Tests.cmake`)

## Purpose
Runs a Windows cross-compiled D3D11 `.exe` under Wine with DXVK, using a dedicated Wine prefix, and
**asserts** (not just hopes) that DXVK itself — not a silent WineD3D fallback — actually handled the
run, by grepping the captured output for DXVK's own distinguishing "DXVK: &lt;version&gt;" log line.

## Executive Verdict
Excellent — this is a genuinely important correctness safeguard, not boilerplate: without the
DXVK-engagement gate, a broken/missing DXVK install could silently degrade every D3D11 pixel test
to validating WineD3D's own different Direct3D implementation instead, and a green CTest run would
never reveal it. The comment explicitly attributes this gap to a real project-owner-flagged concern
("pouhé spuštění pod Wine nestačí" — "merely running under Wine is not enough").

## Checklist Results
- `CNA_D3D11_ALLOW_WINED3D=1` (deliberate bypass for a one-off non-DXVK diagnostic run) and
  `CNA_D3D11_SKIP_DXVK_GATE=1` (for a binary that legitimately never opens a D3D11 device, e.g.
  `D3D11_Common`'s pure-function tests) are both clearly distinguished, documented escape hatches —
  not a single blanket disable flag that could be misused to hide a real regression.
  `cmake/Tests/D3D11Tests.cmake`'s own `D3D11_Common` registration (audited in `build-cmake-tests`)
  correctly uses only the skip-gate variant, consistent with this script's documented contract.
- `wine "$@" 2>&1 | tee "$logFile"` + `wineExit="${PIPESTATUS[0]}"` correctly captures the real
  exit code of `wine` itself, not `tee`'s — a common pipeline-exit-code pitfall correctly avoided.
- Missing/uninitialized `WINEPREFIX` fails loudly with the exact remediation commands
  (`wineboot --init` + `dxvk-setup install`), not a generic Wine error.

## Detailed Findings
None.

## Cross-File Observations
`run-wine-dxvk9.sh` (D3D9) explicitly mirrors this exact gate design (same DXVK-version-line
signal, same two-tier escape-hatch naming convention) — a consistent, intentionally-duplicated
pattern across the two DXVK-backed wrappers (documented as deliberately NOT a shared-code edit,
per D3D9's own file header, to keep each backend's wrapper independent).

## Missing or Weak Tests
N/A (this IS the test-execution mechanism, not itself under test).

## Positive Findings
The DXVK-engagement gate is a genuinely valuable, non-obvious correctness safeguard against a real,
previously-identified risk (silent WineD3D fallback masquerading as a passing DXVK-backed test).

## Final Assessment
No findings.
