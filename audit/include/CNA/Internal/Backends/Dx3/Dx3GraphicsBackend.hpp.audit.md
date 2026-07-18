# Audit: include/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp`
- Audit status: AUDITED (static analysis only, per D-P4 — Windows/DirectDraw-only, not buildable here)
- Subsystem: `backend-dx3` shard
- File type: C++ header (148 lines)
- Related header/implementation: `src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp` (audited separately —
  substantive finding F1 lives in that report)
- XNA/FNA relevance: see `.cpp` report
- Graphics backend relevance: declares the DirectDraw-based 2D-only backend
- FNA reference: N/A directly
- Main related tests: `examples-tests-dx3` (9 files, not yet audited, not runnable on this sandbox)

## Purpose

Declares `Dx3GraphicsBackend` as a pimpl (`struct Impl` forward-declared, defined entirely in the `.cpp`) —
deliberately keeping `<ddraw.h>` (and the `<windows.h>` compatibility shim it pulls in from `free-api`, which
globally `#define`s `fopen` → `free_api_fopen`) fully contained to the `.cpp`, going further than the D3D11/D3D12
backends' own precedent of allowing native headers in their own headers. This is a well-reasoned, explicitly
justified containment decision (lines 6-12) — the macro-leak risk cited is concrete and real (any `.cpp` across
the whole project that happened to transitively include this header would have `fopen` silently rewritten
project-wide), not a hypothetical.

## Executive Verdict

**Healthy.** Accurate declarations, excellent containment discipline, and — notably — an honest, corrected history
visible in a single comment (`CreateOcclusionQuery`'s deliberate non-override, explained as a Phase X7 fix of an
earlier, inconsistent throwing override). No independent defects found in this file; all substantive analysis is
in the paired `.cpp` report.

## Checklist Results

### API / XNA / FNA parity
N/A directly (see `.cpp` report).

### Behavioral correctness / Logic
`SupportsDepthStencil() const override { return false; }` and `SupportsCapability(...) const override { return
false; }` (lines 111-117) are both correctly, unconditionally `false` — the same honest, negotiable-capability
pattern already praised in the SdlRenderer header audit, and a good counter-example to `IGraphicsBackend.hpp`'s
own audited risk of backends relying on a silently-`true` default.

### Memory/resource lifetime
The pimpl (`std::unique_ptr<Impl> impl_`, line 145) correctly gives `Dx3GraphicsBackend` a compiler-generated
move-only lifecycle appropriate for a type wrapping non-copyable native resources — and the class explicitly
`= delete`s its copy constructor/assignment (lines 56-57) rather than relying on the implicit deletion a
`unique_ptr` member alone would trigger, making the non-copyable intent self-documenting at the declaration site
rather than only discoverable via a compiler error.

### C++ correctness
`final` is correctly applied (line 50) — consistent with the `final`-everywhere pattern from Headless/Software,
unlike the SdlRenderer header's own noted inconsistency.

### Performance / Thread safety / Portability
N/A — see `.cpp` report.

### Architecture
The pimpl-for-header-containment technique here is a stronger, more deliberate application of the same principle
`IGraphicsBackend.hpp`'s own audit flagged as an existing minor architecture wart (`SDL_Texture*`/`SDL_Window*`
leaking into the shared interface) — this file shows what the more disciplined version of that containment
instinct looks like when applied consistently.

### Maintainability
148 lines, proportionate; the class-level Doxygen comment (lines 20-49) is a genuinely excellent design summary —
concise enough to read in one sitting, complete enough to explain the shadow-backbuffer rationale, the real vs.
stub feature split, and forward-references to where the real logic lives, without needing to open the `.cpp` first
to understand the shape of the design.

### Robustness / Testing
See `.cpp` report.

## Detailed Findings

None specific to this file — see `.cpp` report's F1.

## Cross-File Observations

See `.cpp` report.

## Missing or Weak Tests

See `.cpp` report.

## Positive Findings

- Exceptional header-containment discipline with a concrete, correctly-identified real-world risk as
  justification, not just precedent-following.
- The class-level doc comment is a model example of "explain the design, not just the API" documentation.
- Honest correction history visible in-line (`CreateOcclusionQuery`'s Phase X7 fix comment).

## Final Assessment

No issues. Clean, well-justified, well-documented header.
