# Audit: examples/easygl_shadereffect_custom_vertex_layout_test.cpp

## Metadata

- Source file: `examples/easygl_shadereffect_custom_vertex_layout_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 1080: genuinely custom `VertexDeclaration`-driven vertex layout
  through `ShaderEffect`'s 3D draw path (not one of the 5 fixed byte-strides EasyGL's `ApplyLayout()` switch
  hardcodes)
- File type: raw `Game`-derived executable, two checks (A/B), each drawn from a separately-uploaded vertex buffer
- XNA/FNA relevance: `VertexDeclaration`/`VertexElement`/`VertexElementFormat`/`VertexElementUsage` are all
  genuine XNA API types; `VertexBuffer::SetDataRaw()` and the generic `IVertexBufferBackend::SetVertexDeclaration()`
  seam are `NOXNA` CNA-internal plumbing enabling it.
- Related production files: `VertexDeclaration.hpp`/`.cpp`, `VertexElement.hpp`/`.cpp`, `VertexBuffer.cpp`
  (`SetDataRaw`), `EasyGLGraphicsBackend.cpp` (`EasyGLVertexBufferBackend::SetVertexDeclaration()`/`ApplyLayout()`,
  `DescribeVertexElementFormat()`).

## Purpose

Proves that a `VertexDeclaration` with 5 elements at non-standard offsets (Position/Normal/Tangent/TexCoord/Color,
stride 48 — matching none of EasyGL's 5 built-in fixed-stride cases: 16/20/24/32/52) binds and reads back correctly
through `ShaderEffect`'s custom-program 3D draw path, generalizing Task 1079's proof beyond fixed stock vertex
types.

## Executive Verdict

**Healthy.** Every offset, format, and expected-pixel derivation in the file's header comment was independently
re-checked against the real `EasyGLVertexBufferBackend::ApplyLayout()`/`DescribeVertexElementFormat()`
implementation and found to match exactly, including the not-entirely-obvious detail that attribute location is
assigned by the element's *index within the declaration list*, not by its `VertexElementUsage`.

## Checklist Results

### API / XNA / FNA parity
`VertexDeclaration(48, { VertexElement(0, Vector3, Position, 0), VertexElement(12, Vector3, Normal, 0), … })`
(lines 186-192) uses the real `VertexElement(offset, format, usage, usageIndex)` constructor and
`VertexElementFormat::Vector3/Vector2/Color` — all genuine XNA enum values, used correctly (Color as 4 normalized
bytes, matching XNA's own `VertexElementFormat.Color` semantics). `VertexBuffer(device, decl, 4, BufferUsage::None)`
matches the real `VertexBuffer(GraphicsDevice&, VertexDeclaration, int, BufferUsage)` constructor signature.

### Behavioral correctness
Confirmed against `EasyGLVertexBufferBackend::ApplyLayout()` (`EasyGLGraphicsBackend.cpp` lines 2195-2227): when
`declarationElements_` is non-empty (populated via `VertexBuffer::SetDataRaw()` → `backend_->SetVertexDeclaration()`,
`VertexBuffer.cpp` line 388), the backend binds attribute `location = i` (the element's own index in the vector) at
`offset = element.getOffsetProperty()`, using `DescribeVertexElementFormat()` to resolve component count/GL type/
normalization — this exactly matches the file's own vertex shader's `layout(location = 0..4)` declarations and its
own comment's claimed convention ("attribute location = the element's own index within the declaration").
`DescribeVertexElementFormat(Color)` returns `{4, UnsignedByte, normalized=true, isInteger=false}` (line 2177),
confirming `Color.r=200` is read back into the shader as a *normalized* float `200/255≈0.7843`, matching the
expected-alpha derivation (`A=200` — the readback re-quantizes the float back to a byte, `0.7843*255≈200`).

### Logic
Independently re-derived both checks against `DescribeVertexElementFormat` + the fragment shader
(`FragColor = vec4(Normal.x, Tangent.y, TexCoord.x, Color.r)`):
- Check A: `Normal.x=0.2 → byte≈51`; `Tangent.y=0.3 → byte≈77` (`0.3*255=76.5`, rounds to 77 as expected); `TexCoord.x=0.5`
  at the quad's screen centre by symmetry (standard 0..1 corner UVs, `World=Identity`, on-axis camera) → `byte≈128`;
  `Color.r=200` passes through the normalize/re-quantize round-trip unchanged. All four match `(51,77,128,200)`.
- Check B: `Normal.x=0.9→byte≈230`; `Tangent.y=0.85→byte≈217`; same `TexCoord.x=0.5→byte≈128`; `Color.r=30` unchanged.
  All four match `(230,217,128,30)`.
- Distinct R/G/A between the two checks (drawn from two separately-uploaded `VertexBuffer`s, `MakeQuad()` called
  twice) is genuine evidence that Normal/Tangent/Color are each read from their own correct offset, not aliased.

### Memory/resource lifetime
`MakeQuad()` returns a fresh `std::unique_ptr<VertexBuffer>` per call — both `vbA`/`vbB` stay alive across both
`DrawOnce()` calls (declared in `Draw()`'s own scope, line 258-264), so no premature destruction risk. Temp
directory (`cvl.vert.glsl`/`cvl.frag.glsl`/`cvl.cnj`) written in `Initialize()` and never cleaned up — see F1
(consistent with every `.cnj`-based file in this batch).

### C++ correctness
`#pragma pack(push, 1)` on `CustomVertex` (lines 99-109) with `static_assert(sizeof(CustomVertex) == 48)` correctly
guards against compiler-inserted padding silently breaking the hand-specified offsets — a real, load-bearing check,
not decorative. Field order (`px,py,pz, nx,ny,nz, tx,ty,tz, u,v, cr,cg,cb,ca`) matches the `VertexDeclaration`'s
offsets exactly (`0,12,24,36,44`).

### Performance
N/A — single-shot test.

### Robustness
Both `DrawOnce()` calls use `RasterizerState::CullNone` and disable depth testing — appropriate for a
single-quad, single-frame readback test with no risk of unintended culling.

### Testing
Both checks were independently re-derived by this audit down to the exact expected byte values, not merely
transcribed from the header comment — all match.

### Cross-file consistency
`EasyGLGraphicsBackend.cpp`'s own comment at the `ApplyLayout()` custom-declaration branch (lines 2203-2209)
independently corroborates this file's own claim about the location-by-index convention — the two files describe
the same mechanism consistently.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 160-172
- Evidence: no `std::filesystem::remove_all` call anywhere in the file.
- Why it matters: same low-priority housekeeping gap already noted in sibling reports across this shard
  (`easygl_billboard_shader_test.cpp.audit.md`).
- Suggested future action (not implemented by this audit): clean up on success or failure.

## Cross-File Observations

- Establishes the "custom `VertexDeclaration` → location-by-index" pattern that `easygl_shattereffect_shader_test.cpp`
  (56-byte stride) directly reuses — a defect in `EasyGLVertexBufferBackend::ApplyLayout()`'s declaration branch
  would affect both files identically.
- The `#pragma pack(push,1)` + `static_assert(sizeof(...) == N)` pattern used here for `CustomVertex` is repeated
  verbatim in `easygl_shattereffect_shader_test.cpp` for its own 56-byte `ShatterVertex` — a good, consistently
  applied safety idiom across this batch.

## Missing or Weak Tests

- No test in this file (or apparently elsewhere in this batch) covers a `VertexDeclaration` whose element list is
  set *after* the `VertexBuffer` already has data uploaded via the typed `SetData()` overloads (i.e. mixing the
  generic-declaration path and the fixed-stride path on the same buffer instance) — likely an intentionally
  unsupported combination, but not explicitly tested as rejected/ignored either way.

## Positive Findings

- The offset math, format-to-GL-type mapping, and expected-byte derivations were all independently verified by
  this audit against the real backend code and found exact — a genuinely well-constructed, non-trivial test.
- Using two separately-uploaded vertex buffers with distinct Normal/Tangent/Color values (rather than one buffer
  and a uniform toggle) is a stronger discriminator against per-offset aliasing bugs than a single-buffer approach
  would be.

## Final Assessment

A rigorous, independently-verified proof of Task 1080's generic `VertexDeclaration` capability; the offset/format
math is exact, and the only observation is the shared, low-priority temp-file cleanup gap (F1) common to this
shard.
