# AUDIT_EXCEPTIONS.md — BLOCKED Files

Files that entered (or briefly entered) the `BLOCKED` state, why, and how/whether they were later unblocked.
`BLOCKED` is meant to be temporary per the audit prompt; this log exists so a blocked item is never quietly
forgotten. Empty at audit start — updated as work proceeds.

| Path | Shard | Blocked reason | Status | Resolution |
|---|---|---|---|---|
| _(none yet)_ | | | | |

## Anticipated (not yet encountered) categories of blocker

- Windows-only backends (D3D9/D3D11/D3D12/Dx3) cannot be *built or run* on this Linux sandbox — this is not a
  `BLOCKED` manifest state (the source is still fully readable and auditable statically), but per D-P4 those
  reports must explicitly say "not runtime-verified in this sandbox" rather than imply test evidence that wasn't
  actually gathered.
- External sibling-repo dependencies (`easy-gl`, `free-direct`, D-6) — not blockers either; the CNA-side adapter is
  still fully auditable, only the external library's own internals are out of scope by design.
