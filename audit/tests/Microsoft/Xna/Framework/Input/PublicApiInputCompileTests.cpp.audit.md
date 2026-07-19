# Audit: tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp` (189 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test) — compile/link-time guard, not a runtime behavior test
- XNA/FNA relevance: Header-hygiene/namespace/GetTypeName-policy guard for the entire public
  `Microsoft::Xna::Framework::Input`/`Input::Touch` API surface
- Main related tests: N/A (this IS a test file)

## Purpose
A compile-and-link-time (not run-time) guard with three distinct properties enforced purely via
`#include`s, an `#error` guard, and `static_assert`s: (1) every public Input header is
self-contained and usable from a consumer including only public headers; (2) no public Input
header transitively leaks `<SDL3/SDL.h>` into a consumer's translation unit; (3) every public Input
type correctly lives in its mirrored namespace, and (per INPUT-API-029) that no public Input type
currently derives from `System::Object` (which would require a `GetTypeName()` override this file
doesn't provide).

## Executive Verdict
No findings. This is a well-designed, low-maintenance-cost guard against three real classes of
regression (header self-containment breakage, accidental SDL leakage into the public API, and
namespace/GetTypeName-policy drift) that would otherwise only surface as a downstream consumer's
build failure or a silently-violated project convention.

## Checklist Results
- The SDL-leak guard (`#if defined(SDL_MAJOR_VERSION) || defined(SDL_h_)` -> `#error`) is a
  concrete, currently-enforced regression test for a specific prior fix (INPUT-MOUSE-018's removal
  of `MouseCursor.hpp`'s own SDL include) — not just a design aspiration in a comment.
- `not_object_v<T>` static_asserts across all 18 public Input types plus `GetTypeName()`-exemption
  reasoning are a real, load-bearing enforcement of this project's own CLAUDE.md policy: if any
  type is changed to derive from `System::Object` in the future, this stops compiling, forcing the
  required `GetTypeName()` override to be added at that exact point.
- `ns_placement_guard`'s fully-qualified `sizeof(...)` assertions correctly pin each type's
  namespace, not just its existence — a type moved to the wrong namespace fails to compile here
  even if its unqualified name and behavior are otherwise unchanged.
- `UsePublicInputApi()`'s own comment correctly explains why it must never be executed (would need
  live SDL/input state and break `--gtest_shuffle` order-independence) while still being ODR-used
  (`&UsePublicInputApi`) to force real compilation and linking.

## Detailed Findings
None.

## Cross-File Observations
This file is the input shard's counterpart to a now-familiar cross-shard pattern (public-API
compile/self-containment guards) — worth cross-referencing against
`PublicApiInputSignatureFreezeTests.cpp`, which likely covers the complementary
signature-stability concern for the same API surface.

## Missing or Weak Tests
None — this file's scope (compile-time header/namespace/policy guards) is narrow and fully
achieved within that scope.

## Positive Findings
The three-guard design (self-containment, SDL-leak `#error`, namespace/`GetTypeName` policy
`static_assert`s) packs a lot of real regression protection into a file that costs nothing at
runtime and needs no maintenance beyond keeping its type list current.

## Final Assessment
No findings.
