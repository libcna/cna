# Audit: include/CNA/Internal/Backends/Common/NotYetImplemented.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Common/NotYetImplemented.hpp`
- Audit status: AUDITED
- Subsystem: Graphics backend abstraction contract (shared, `backend-common` shard)
- File type: C++ header, single inline free function
- Related header/implementation: sibling of `IGraphicsBackend.hpp` in the same directory; used by D3D9/D3D12
  backend skeleton code per its own header comment (grep-confirmed usage exists in the D3D9/D3D12 backend shards,
  to be cross-checked in detail when those shards are audited)
- XNA/FNA relevance: N/A — CNA-internal helper, outside `Microsoft::Xna`.
- Graphics backend relevance: shared utility, used by backends that haven't finished implementing a given method.
- FNA reference: N/A
- Main related tests: none directly (a 4-line throwing helper); indirectly exercised whenever a test calls into an
  unimplemented backend method and expects a `std::runtime_error`.

## Purpose

A single `[[noreturn]] inline` free function, `NotYetImplemented(backendName, what)`, that throws a
`std::runtime_error` with a message naming both the backend and the missing capability. Exists purely to avoid
duplicating this "loud, explicit failure" one-liner across backend skeleton code — its own header comment
correctly documents the history (D3D12 originally had a private copy, lifted here for D3D9 to share, with a
deliberate, explicitly-scoped decision to leave D3D12's original private copy alone rather than churn unrelated
code). Correctly placed under `CNA::Internal::Backends`, matching `IGraphicsBackend.hpp`.

## Executive Verdict

**Healthy.** A trivial, correct, single-purpose utility with no defects found.

## Checklist Results

### API / XNA / FNA parity
N/A — not an XNA-namespace type.

### Behavioral correctness
Always throws (the `[[noreturn]]` attribute is honest — there is no path that returns). Message format
(`"<backend> backend: <what> not yet implemented"`) is consistent and grep-friendly for anyone debugging a
missing-feature crash.

### Logic
Trivial; no branches, no edge cases to misjudge.

### Memory/resource lifetime
`std::string(backendName) + " backend: " + what + " not yet implemented"` constructs one temporary `std::string`
that's immediately handed to `std::runtime_error`'s copying constructor — correct, unremarkable, and irrelevant on
a throw-then-terminate-the-current-operation path.

### C++ correctness
`[[noreturn]] inline void` is the correct attribute/linkage combination for a header-defined function called from
multiple translation units (avoids ODR issues; `inline` is required since this is a non-template, non-`static`
free function defined in a header). `const char*` parameters are fine for two short, always-string-literal-in-
practice call-site arguments.

### Performance
N/A — only called on an already-exceptional (unimplemented-feature) path, never a hot path.

### Thread safety
N/A — no shared/static state.

### Architecture
Good, minimal, single-purpose shared helper — exactly the kind of small cross-backend consolidation the project's
backend layer should have more of (see `IGraphicsBackend.hpp`'s own audit F3 for the one place that *doesn't* use
this helper where it plausibly could).

### Maintainability
4 lines of actual logic; nothing to simplify further.

### Portability
N/A.

### Robustness
Correct behavior for its stated purpose — turns a silent gap into a loud, descriptive failure.

### Testing
No dedicated test, and none is really warranted for a 4-line throw helper — its correctness is adequately
exercised indirectly wherever a backend actually calls it (to be confirmed per-backend during the D3D9/D3D12 shard
audits).

## Detailed Findings

None.

## Cross-File Observations

- Referenced from `IGraphicsBackend.hpp`'s own audit report (Finding F3) as a missed-reuse opportunity for that
  file's own inline default-throw bodies (`ReadBackbuffer`, `DrawInstancedPrimitivesEx`) — not a defect in *this*
  file, just a place this file's stated purpose could be applied more consistently elsewhere.
- To be cross-checked when auditing `backend-d3d9`/`backend-d3d12`: confirm the header comment's claim that D3D9
  uses this shared helper while D3D12 still has its own private, un-migrated copy, and note whether that's still
  accurate or has drifted.

## Missing or Weak Tests

None warranted given the triviality of the function; not flagged as a gap.

## Positive Findings

Exactly the right size and scope for what it does; the header comment's explanation of *why* it exists and what
was deliberately left alone (D3D12's private copy) is a good model of the "document intentional deviations" rule
from the project's own contribution guidelines.

## Final Assessment

No issues. Small, correct, well-documented shared utility.
