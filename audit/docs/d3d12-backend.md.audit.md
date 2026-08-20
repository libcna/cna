# Audit: docs/d3d12-backend.md

## Metadata
- Source file: `docs/d3d12-backend.md` (215 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown backend status document
- XNA/FNA relevance: describes the native Direct3D 12 graphics backend

## Purpose
Documents the D3D12 backend's status, Wine+vkd3d-proton dev-loop, test-writing conventions, and known
limitations, explicitly re-audited (per its own header) against `plans/plan_dx.md`'s actual `DX-100`-`DX-148`
row status since "most of this section's earlier revisions predated Phase DX13/DX14/DX15 landing and
were significantly stale."

## Executive Verdict
The strongest self-auditing discipline found in this entire docs batch: the "Known limitations"
header explicitly states its own prior revisions were stale and this version was "re-derived from
source, not copy-edited." It then follows through — a dedicated bullet ("The following are all real
and closed now, despite earlier revisions of this section claiming otherwise") lists 6 specific
features, explicitly warning the reader to "re-verify against `plans/plan_dx.md`'s own row status before
trusting any *other* specific claim in this file."

## Checklist Results
- The plain-Wine-vs-Proton swap-chain distinction (`CreateSwapChainForHwnd` crashes under plain Wine,
  works under a properly Proton-managed launch) is a precise, falsifiable technical claim with a
  specific root cause (a null-pointer read inside Wine's `dxgi.dll`, architecture mismatch between
  system `dxgi.dll` and vkd3d-proton's overridden `d3d12.dll`) — correctly labeled "not a CNA bug."
- `D3D12_Smoke`'s "212/212 passing" claim and the check-lettering convention note ("through double
  letters, AA-NN") is consistent with the level of specificity seen in this project's other backend
  test documentation (`D3D9`'s own `D3D9_XNA_Diff` pixel-count claims).
- Consistently distinguishes what's real-and-CTest-proven from what's real-but-only-manually-verified
  (the Proton swap-chain diagnostic) throughout.

## Detailed Findings
None found against this document's own claims — its self-correcting re-audit methodology is exactly
the practice this session's own audit would recommend to less-diligently-maintained sibling documents
(e.g. `docs/coverage.md`, `docs/d3d9-backend.md`'s stale setenv bullet).

## Cross-File Observations
Shares the identical `D3DConstantBuffers` struct and `hlsl_shaders.hpp` DXBC bytecode with `D3D11`
(explicitly noted as "design decision 5's reuse bootstrap, not a re-derivation") — consistent with
`docs/d3d11-backend.md`'s own description of the shared `D3DCommon` infrastructure.

## Missing or Weak Tests
The document itself names several genuine, honestly-scoped gaps (per-target MSAA-resolve-on-unbind for
N>1 MRT not attempted; device-removed detection *trigger* untestable in this environment) — these are
disclosed gaps, not silently-missing coverage.

## Positive Findings
This document's explicit self-warning about its own prior staleness, followed through with a
re-derived-from-source correction, is the single best documentation-hygiene practice found in this
entire `docs` shard — a template other stale documents in this corpus (`docs/coverage.md`,
`docs/d3d9-backend.md`'s setenv bullet) would benefit from following.

## Final Assessment
No findings. Exemplary self-auditing documentation practice.
