# Audit: tests/Microsoft/Xna/Framework/Graphics/VertexBufferBindingTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Graphics/VertexBufferBindingTests.cpp` (100 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-graphics` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `VertexBufferBinding.hpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `VertexBufferBinding`'s three constructors (default, one-arg, two-arg, three-arg) and
their `VertexOffset`/`InstanceFrequency` default values and round-tripping, plus a
copy-preserves-values check.

## Executive Verdict
Correct as a pure storage/getter test, but every test treats `VertexOffset` as an opaque integer
(`4`, `8`, `12`) without ever consuming it in an actual draw call or vertex-fetch computation. This
means the file cannot confirm or refute the sibling `buffers`-shard production-code fork's own
already-raised question about whether CNA's `VertexOffset` is interpreted in *vertex* units
(matching FNA's `VertexOffset` semantics) or *byte* units anywhere it is actually consumed
downstream (e.g. `GraphicsDevice::SetVertexBuffers`/`DrawIndexedPrimitives`) — a pure
"round-trips the number I gave it" test is blind to a unit-interpretation bug the same way the
Item-3 Matrix round-trip tests were blind to the transpose-convention bug.

## Checklist Results
No issues found within this file's own narrow scope (storage-only).

## Detailed Findings
None new; see Executive Verdict for the scope limitation relative to the sibling production-code
fork's already-raised open question.

## Cross-File Observations
Same methodological gap as `EffectParameterTests.cpp`'s round-trip-only Matrix tests: testing that
a stored value comes back unchanged cannot reveal a wrong *interpretation* of that value elsewhere
in the pipeline. Whether `VertexOffset`'s unit semantics (vertex vs. byte) are correct where it's
actually consumed remains untested by this file and should be checked directly in whatever
`GraphicsDevice`/backend code path reads `VertexBufferBinding::VertexOffset`.

## Missing or Weak Tests
No test constructs a `VertexBufferBinding` with a nonzero `VertexOffset`, feeds it through
`GraphicsDevice::SetVertexBuffers` and an actual draw call (even against a mock/recording backend,
per the `RecordingSpriteBatchBackend` pattern used elsewhere), and confirms the effective vertex
read offset — the only way to settle the sibling fork's open unit-semantics question empirically
from the test suite.

## Positive Findings
Correct, minimal coverage of the storage contract itself.

## Final Assessment
No new findings within this file's scope; flags that its round-trip-only design cannot settle the
already-open `VertexOffset` unit-semantics question raised elsewhere.
