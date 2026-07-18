# Audit: examples/easygl_move_semantics_test.cpp

## Metadata

- Source file: `examples/easygl_move_semantics_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — move-construct/move-assign resource-ownership test
- File type: `Game`-derived executable, CTest-registered as `cna_test_easygl_move_semantics` /
  `EasyGL_MoveSemantics` (`cmake/Tests/EasyGLTests.cmake:976-978`)
- XNA/FNA relevance: none directly — move semantics are a CNA/C++ implementation detail; XNA/FNA has
  no equivalent concept (C# uses GC + finalizers, not move-only ownership). Judged instead against
  this project's own `GraphicsResource`/backend-ownership conventions.
- Production sources cross-checked: `VertexBuffer.cpp`/`.hpp`, `IndexBuffer.cpp`/`.hpp`,
  `Texture2D.cpp`/`.hpp`, `GraphicsResource.hpp`

## Purpose

Verifies that `VertexBuffer`, `IndexBuffer`, and `Texture2D` correctly transfer backend-handle
ownership on move-construct and move-assign — `src.HasBackend()` becomes `false`, `dst.HasBackend()`
becomes/stays `true` — so that only one of the two ends up owning (and eventually freeing) the
backend handle, per its own header comment's stated goal of preventing a double-free.

## Executive Verdict

**Mostly healthy**, with one real asymmetry worth flagging: the three "covered types" the header
comment lists side-by-side do not actually share the same ownership model. `VertexBuffer`/
`IndexBuffer` hold their backend via `std::unique_ptr` (a type where move-then-destroy correctness is
close to a language guarantee), while `Texture2D` holds it via `std::shared_ptr` (where the
interesting double-free-shaped bugs, if any existed, would come from *copying*, not moving) — so the
test's implicit claim of testing "the same risk" across all three types is not quite accurate. The
test itself is correct for what it does check.

## Checklist Results

### API / XNA / FNA parity
N/A — move semantics are CNA-internal (`NOXNA`-flavored, though not literally under
`Microsoft::Xna`); `HasBackend()` is itself explicitly marked `NOXNA` in all three headers
(`VertexBuffer.hpp:274`, `IndexBuffer.hpp:153`, `Texture2D.hpp:309`).

### Behavioral correctness
Confirmed via source: `VertexBuffer::backend_` is `std::unique_ptr<IVertexBufferBackend>`
(`VertexBuffer.hpp:322`) and `VertexBuffer::VertexBuffer(VertexBuffer&&) noexcept = default;` /
`operator=(VertexBuffer&&) noexcept = default;` (`VertexBuffer.cpp:43-44`) — the implicit
member-wise move of a `unique_ptr` nulls the source and transfers the pointer, and
`unique_ptr::operator=(unique_ptr&&)` first releases whatever `dst` previously owned before taking
ownership of `src`'s pointer, so the move-assign case (`dst` already holding "handle1" before
`dst = std::move(src)`) frees handle1 and does not leak it — exactly what the test's own comment
(`// handle1 freed, src's handle transferred to dst`) claims. Identical structure and reasoning
applies to `IndexBuffer` (`backend_` is `std::unique_ptr<IIndexBufferBackend>`,
`IndexBuffer.hpp:198`; `IndexBuffer.cpp:42-43`).

`Texture2D::backend_` is instead `std::shared_ptr<ITextureBackend>` (`Texture2D.hpp:312`), with
`Texture2D(Texture2D&&) noexcept = default;` / `operator=(Texture2D&&) noexcept = default;`
(`Texture2D.hpp:72-73`). A defaulted move of a `shared_ptr` also nulls the source and transfers the
pointer+control-block reference — so `HasBackend()`'s pre/post-move states check out identically to
the `unique_ptr` cases, and the test's assertions pass for the right reason. However, `shared_ptr`
already makes a same-object double-free structurally very hard to produce via *move* alone (its
whole purpose is safe shared/transferred ownership via refcounting); the double-free-shaped risk
`shared_ptr` actually guards against is dangling-vs-refcount mismatches from *copying* a
`Texture2D` (or from a raw pointer obtained via `GetBackendRaw()`/`GetBackendWeak()` outliving the
last owning `shared_ptr`), neither of which this test exercises.

### Memory/resource lifetime
Each of the six blocks is scoped so both `src` and `dst` go out of scope at the end of the block,
triggering real destructor calls (`~VertexBuffer()`/`~IndexBuffer()`/`~Texture2D()`, all `= default`
in their respective `.cpp` files) — so this test's "no double-free" claim is actually verified only
*implicitly*, by the program not crashing/aborting through to
`std::printf("=== %d/%d PASS ===\n", ...)` and `Exit()`. There is no explicit second assertion (e.g.
an ASan build, or an explicit `src.Dispose()`/`dst.Dispose()` call performed twice) that would turn a
silent heap corruption into a definite, attributable test failure rather than an unrelated crash
elsewhere. This is a common, acceptable pattern for this kind of smoke test but is worth naming
explicitly (see Missing or Weak Tests).

### C++ correctness
`VertexBuffer`/`IndexBuffer`'s move ctor/assignment are declared `noexcept` (`VertexBuffer.cpp:43-44`,
`IndexBuffer.cpp:42-43`) — correct, since a `unique_ptr` member's move is itself `noexcept` and the
class has no other throwing move operations; `Texture2D`'s are likewise `noexcept`
(`Texture2D.hpp:72-73`). `GraphicsResource` (the shared base of all three) declares both a
user-provided copy constructor/assignment (which reset `isDisposed_` and drop event-handler
subscribers, `GraphicsResource.hpp:67-70`) *and* an explicit `GraphicsResource(GraphicsResource&&) =
default;` (`GraphicsResource.hpp:74-75`) — because the move special members are explicitly
requested rather than left to be implicitly suppressed by the user-declared copy members, all three
derived classes' own `= default` move operations correctly compose down to a working, non-throwing
move chain (verified by reading the base class declarations directly, not assumed).

### Architecture
Correctly distinguishes "the handle transferred" (`HasBackend()`) as the observable proxy for
"ownership moved," which is the right level of abstraction for a black-box move-semantics test — it
does not reach into backend internals.

### Testing
Six symmetric blocks (move-construct ×3 types, move-assign ×3 types) with a shared `check()` helper
that both prints a labelled PASS/FAIL line and tallies `pass_`/`fail_`; `getResult()` maps any
failure to exit code 1. Every one of the three named types gets both a fresh-`dst` (move-construct)
and an already-owning-`dst` (move-assign) case — the latter is the more interesting one (it is the
case that could leak or double-free `dst`'s prior handle), and it is present for all three types.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — "Covered types" list conflates two different ownership models

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / documentation accuracy
- Location/symbol: file header comment (line 9: "Covered types: VertexBuffer, IndexBuffer,
  Texture2D"); `VertexBuffer.hpp:322` (`unique_ptr`) vs. `Texture2D.hpp:312` (`shared_ptr`)
- Evidence: `VertexBuffer`/`IndexBuffer` hold their backend via `std::unique_ptr` (an ownership model
  where a buggy hand-written move constructor is essentially the only way to create a double-free);
  `Texture2D` holds its backend via `std::shared_ptr` (an ownership model designed around safe
  sharing, where the analogous risk class is copy-related refcount mismatches, not move-related
  double-frees).
- Why it matters: a reader relying on this file's header comment to believe "the same double-free
  risk was checked for all three types identically" would be mildly misled — the `Texture2D` case is
  a strictly weaker check of a different property (that the defaulted `shared_ptr` move behaves like
  a `shared_ptr` move, which is close to tautological) than the `VertexBuffer`/`IndexBuffer` cases
  (that the class's hand-declared `= default` move actually composes correctly through a
  non-trivial base class).
- Suggested future action (not implemented by this audit): either note the ownership-model
  difference explicitly in the header comment, or add a `Texture2D` copy-related test (e.g. two
  `Texture2D`s legitimately sharing one backend via copy, then independently disposing/destroying
  each, confirming no premature free while any owner remains) to actually probe `shared_ptr`'s real
  risk surface.

## Cross-File Observations

- `GraphicsResource`'s copy constructor explicitly does **not** copy `isDisposed_` or the `Disposing`
  event-handler list (`GraphicsResource.hpp:67-70` comment) — this is unrelated to the move path this
  file tests, but is the kind of subtlety a future `Texture2D` copy-semantics test (see F1's
  suggested action) would need to account for.

## Missing or Weak Tests

- No case explicitly calls `Dispose()` on a moved-from object (relying only on the implicit
  destructor call at scope-exit) — an explicit `src.Dispose()` immediately after the move, confirming
  it safely no-ops (backend already null) rather than double-freeing, would make the "no
  double-free" claim in the header comment an explicit assertion rather than an implicit
  "didn't crash" inference.
- No `Texture2D` *copy* test exists in this file to probe `shared_ptr`'s actual risk surface (see F1).

## Positive Findings

- Correctly covers both move-construct and move-assign for all three types, including the more
  interesting "`dst` already owns a handle" move-assign case for each.
- The base-class move-composition question (does an explicit `GraphicsResource(GraphicsResource&&) =
  default` actually compose correctly given the class also user-declares copy special members) was
  worth independently re-verifying against `GraphicsResource.hpp`, and it checks out.

## Final Assessment

Correctly verifies handle-ownership transfer for all three types as far as its own `HasBackend()`
proxy goes; the "no double-free" framing is accurate for `VertexBuffer`/`IndexBuffer` but slightly
overstated for `Texture2D`, whose `shared_ptr`-based backend makes this particular test a much
weaker check of a much-less-likely failure mode for that one type.
