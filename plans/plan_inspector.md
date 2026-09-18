# CNA Inspector plan

Status: complete (INSP-0001, 2026-09-17)

Follow-up fixes from the production-readiness audit are recorded in
[`plan_diagnostics_inspector_audit.md`](plan_diagnostics_inspector_audit.md)
(`AUD-DIAG-INSP-0001` to `AUD-DIAG-INSP-0004`).

## Immutable foundation

The diagnostics/profiler foundation commit
`b7e856221b61c273e0ac085ec67d103e0ac7184a` is the immutable input. Inspector uses
`IDiagnosticsProvider::InterfaceVersion == 1`, `CaptureSnapshot()`, cursor-based `ReadEvents()`,
and `ResolveName()` without changing `modules/diagnostics`.

## Architecture decisions

1. Inspector is an optional physical module and is not in the `CNA` umbrella. Normal builds keep
   `CNA_BUILD_INSPECTOR=OFF`; opted-in games link `CNA::Inspector` and explicitly start an agent.
2. The application-side agent owns one background thread and a compact authenticated binary
   protocol. It performs no HTTP, JSON, HTML, or browser work and services provider requests only
   after authentication.
3. `cna-inspector` is a separate process. It is the binary client and a localhost-only HTTP/JSON
   bridge for locally embedded browser assets. Refreshing or crashing it cannot restart or crash
   the game.
4. Version 1 negotiates wire/provider versions and capabilities, carries accuracy classifications
   unchanged, and retains every event discontinuity counter. Payloads, collection counts, request
   rates, pending previews, browser history, and socket waits are bounded.
5. Overview and event pulls are modest and cursor-based. Resource metadata is sent only on manual
   refresh. The provider's internally owned snapshot remains the accepted version-1 contract; no
   arbitrary engine-object enumeration or whole-state JSON exists.
6. Preview readback is a separate optional non-blocking source. It is manual, asynchronous,
   throttled, size-limited, and absent by default. No renderer behavior or diagnostics core was
   changed to pretend that readback exists.
7. The endpoint is disabled by default, loopback-only by default, token-authenticated, strictly
   parsed, and has no memory-read, code-execution, scripting, buffer-dump, or input-injection API.

## Task

| ID | Deliverable | Status |
|---|---|---|
| INSP-0001 | Optional agent, protocol, separate bridge/browser UI, tests, benchmarks, security and user documentation | Complete |

## Completion evidence

- Focused protocol/agent/frontend suite: 24/24 passed on Linux, including real loopback auth,
  negotiation, malformed clients, reconnect, request-rate backpressure, resource lifetime,
  discontinuity preservation, and preview unavailable/pending/ready/failure paths.
- The same 24/24 pass under AddressSanitizer and UndefinedBehaviorSanitizer; the unchanged
  diagnostics provider regression suite passes 17/17.
- Native Debug and Release `cna-inspector` builds passed with HEADLESS/NULL and FULL diagnostics.
- MinGW-w64 desktop cross-build validates the Winsock path.
- Embedded JavaScript passed Node 20 syntax validation; the asset test and live HTTP smoke test
  verify offline, bounded, demand-driven behavior plus CSP, Host, and UI-token gates.
- Seven-run Release medians, including compiled-out and inactive gates, are recorded in
  [`../docs/inspector-benchmark.md`](../docs/inspector-benchmark.md).
- Architecture, protocol, activation, security, UI, supported platforms, expensive operations,
  troubleshooting, limitations, and extension rules are documented in
  [`../docs/inspector.md`](../docs/inspector.md).
