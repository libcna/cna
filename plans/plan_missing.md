# Remaining XNA 4.0 Runtime API Surface Work

## 1. Purpose and snapshot

This is the handoff for the documented Microsoft XNA 4.0 **runtime** public/protected API members still absent from CNA. Runtime public type coverage is complete: **331/331 (100.00%)**. Member representation is **3,467/3,627 (95.59%)**. The objective is zero unexplained `MISSING` members and zero `NEEDS_REVIEW` records, without mistaking API representation for behavioral parity. The Content Pipeline assembly and its separate parity work are outside this plan.

| Snapshot item | Value |
| --- | --- |
| Audit date | 2026-09-20 (UTC) |
| Branch | `work` |
| Baseline HEAD | `9a42a7e82af7d7b0aeb40f8957baca5bb6aa93db` |
| Working tree before this plan | Clean |
| Fresh command | `python3 tools/audit_xna_runtime_surface.py --json > /tmp/xna_runtime_missing_plan_audit.json` |
| Audit script SHA-256 | `4e48cd437895bb0831d3ac6df06b4e09c9eb5222f93c5580b8d17e853a78207e` |
| Member-model SHA-256 | `e009e7e1d8040d8d19b26f41c529ac0df01425a37c10343e9e05872195e28df4` |
| Reference directory | `/rv/data/development/github.com/libcna/xna4-decomp/dlls` |
| Behavioral reference | `/rv/data/development/github.com/libcna/xna4-decomp/reference/xna4/decompiled/windows/` and `/rv/data/library/github.com/FNA-XNA/FNA/src/` |
| Sharp Runtime sibling | `/rv/data/development/github.com/libcna/sharp-runtime` |

The audit reads genuine Microsoft XML documentation and matching DLL metadata for `Microsoft.Xna.Framework`, `.Game`, `.Graphics`, `.Input.Touch`, `.Storage`, `.Video`, `.Xact`, `.GamerServices`, `.Net`, and `.Avatar`. It excludes `Microsoft.Xna.Framework.Content.Pipeline.xml`. Clang parses CNA public headers; `tools/xna_runtime_members.py` normalizes CLR/C++ signatures. The fresh per-member result was **identical** to `docs/xna-4-runtime-member-coverage.json`; `docs/xna-4-runtime-member-coverage-baseline.md` is the older 3,463/3,627 pre-fix baseline. `plans/plan_xna_runtime_surface.md` records the previous audit milestone and validation history. Re-run the audit before any future batch: this snapshot becomes stale as members land.

## 2. Current measured baseline

| Surface | Represented | Documented | Coverage |
| --- | ---: | ---: | ---: |
| Runtime public types | 331 | 331 | 100.00% |
| Constructors | 236 | 253 | 93.28% |
| Methods | 1,380 | 1,518 | 90.91% |
| Properties | 1,038 | 1,040 | 99.81% |
| Fields | 751 | 753 | 99.73% |
| Events | 62 | 63 | 98.41% |
| Operators (method subset) | 145 | 145 | 100.00% |
| Indexers (property subset) | 29 | 29 | 100.00% |
| Enum values (field subset) | 661 | 661 | 100.00% |
| Nested public types (type subset) | 6 | 6 | 100.00% |
| **Non-type runtime members** | **3,467** | **3,627** | **95.59%** |

Operators, indexers, and enum values are **subsets**, not extra denominator rows. Strict documented coverage is `(EXACT_EQUIVALENT + SEMANTIC_EQUIVALENT + HOST_LANGUAGE_SUBSTITUTION) / all documented members`. C++-applicable coverage removes only justified `NOT_APPLICABLE` entries from the denominator; here it is also 3,467/3,627 because there are none. `NEEDS_REVIEW` is never credited.

| Classification | Count |
| --- | ---: |
| `EXACT_EQUIVALENT` | 1,662 |
| `SEMANTIC_EQUIVALENT` | 1,747 |
| `HOST_LANGUAGE_SUBSTITUTION` | 58 |
| `MISSING` | 160 |
| `NOT_APPLICABLE` | 0 |
| `NEEDS_REVIEW` | 0 |

The 160 gaps are **153 Tier B, 7 Tier C, 0 Tier A**. Every one has a unique XML signature in Appendix A. The machine-readable record also gives assembly, declaring type, metadata visibility, return/type shape, CNA header, and tier. In particular, the three `PropertyDictionary` entries are documented explicit-interface members whose CLR metadata visibility is `nonpublic`; their documented interface contract is intentional inventory, not an audit leak.

## 2a. Implementation progress

This section is the live record; §2's tables remain the **2026-09-20 baseline** they were measured
as. Re-run `python3 tools/audit_xna_runtime_surface.py --write-reports` before trusting any row.

| Phase | Packages | Represented / documented | Strict coverage | `MISSING` | Status |
| --- | --- | ---: | ---: | ---: | --- |
| Baseline | — | 3,467 / 3,627 | 95.59% | 160 | measured at `9f4d2575d` |
| 1 — value surface | 001, 002, 003, 004, 020 | 3,574 / 3,627 | 98.54% | 53 | **DONE** |
| 2 — local subsystems | 015, 019, 018, 016, 017, 007, 009 | 3,596 / 3,627 | 99.15% | 31 | **DONE** |
| 3 — Graphics/Content Tier B | 005, 006, 008, 012 | | | | TODO |
| 4 — shared prerequisites, Tier C | 013, 014, 010, 011 | | | | TODO |

| Package | Status | Closed | Commit | Note |
| --- | --- | ---: | --- | --- |
| XNA-MISSING-001 PackedVector | **DONE** | 74 | `b6f127b15` | `std::any` settled as the boxed-object representation for 001–004/020. Shared `CNA::Internal::UnpackSNorm`/`UnpackUNorm` added; the SNORM minimum code now expands to exactly -1, as `PackUtils.UnpackSNorm` does. `ToVector4` is now an `IPackedVector` member, so `Color` overrides it too. |
| XNA-MISSING-002 Core / Math | **DONE** | 14 | `37f42e2e7` | Object overloads delegate to the existing typed `Equals`; no comparison duplicated, no layout or vptr change. |
| XNA-MISSING-003 Input / Touch | **DONE** | 12 | `abc1bcab4` | The four GamePad `ToString()` bodies are each different; button/DPad name order is the documented one, not the accessor declaration order. |
| XNA-MISSING-004 Graphics vertex | **DONE** | 5 | `1e97ae2e4` | Declaration-only change beside the existing typed `Equals`. |
| XNA-MISSING-020 GraphicsDeviceInformation | **DONE** | 2 | `1a5d93ffd` | Compares the adapter, the profile and exactly the ten presentation fields XNA compares; `GetHashCode` XORs the same set. |
| XNA-MISSING-015 Design | **DONE** | 5 | `7ea8096c2` | The two protected fields are renamed to XNA's spelling, not aliased, so there is one state source. The three `ConvertFrom` overrides forward to the base, as Microsoft's own bodies do. |
| XNA-MISSING-019 Media | **DONE** | 1 | `3fa04f9e8` | `cna_media` links SharpRuntime `Uri` PUBLIC. The typed overload passes `OriginalString`, not `AbsoluteUri`, which is what keeps a relative URI usable. |
| XNA-MISSING-018 GamerServices / Avatar | **DONE** | 4 | `6a3ba555e` | `Changed` is now per-instance, which changed `cna_avatar_description_subscribe_changed_ext` to take a description handle (exported symbol set unchanged). Microsoft's shipped Windows `PropertyDictionary` throws from *every* member; CNA's local dictionary contract is the established behaviour and the pair operations follow it. |
| XNA-MISSING-016 Audio | **DONE** | 2 | `484359479` | The collection overload refuses an empty collection itself: an empty `std::vector`'s `data()` is null, which the pointer overload rightly reads as a null array. |
| XNA-MISSING-017 XACT | **DONE** | 6 | `484359479` | `RendererDetail`'s typed equality compared the id alone, as FNA does; XNA compares name and id, so it had to change for `Equals(object)` to agree with it. Recorded in `plans/plan_bindings_upstream.md`. |
| XNA-MISSING-007 graphics exceptions | **DONE** | 3 | `b0fa4b002` | Re-based on `System::Exception` -- XNA's hierarchy and the base every other XNA exception in CNA uses -- rather than adding an ad hoc inner-cause field to `std::runtime_error`. |
| XNA-MISSING-009 GraphicsDevice disposal | **DONE** | 1 | `a1e47f004` | Lifecycle order unchanged; the flag adds XNA's guard that `Disposing` is raised only for an explicit disposal. |
| XNA-MISSING-005, 006, 008, 010–014 | TODO | | | See §5. |

Phase 2 evidence: 22 records closed, 0 regressed, 0 `NEEDS_REVIEW`. Closed by audit subsystem:
Design 5, XACT 6, GamerServices 3, Graphics 4, Audio 2, Avatar 1, Media 1. Two behavioural
corrections fell out of the work and are recorded where they were made: `RendererDetail`'s identity
(`plans/plan_bindings_upstream.md`) and the `PropertyDictionary` doc comment that described the
reference assembly wrongly.

Phase 1 evidence: 107 records closed, 0 regressed, 0 `NEEDS_REVIEW`, and no represented record
moved back to `MISSING`. Closed by audit subsystem: Core / Math 15, Graphics 78, Input 11,
Runtime 2, Touch 1. Per package that is 001 = 74 (73 Graphics + the `IPackedVector` record the
audit attributes to Core / Math, since `IPackedVector.hpp` lives in `modules/math`), 002 = 14,
003 = 12, 004 = 5, 020 = 2.

**Audit hygiene learned here:** do not run the audit while editing headers. The audit's Clang pass
walks 331 types over several minutes, so an edit landing mid-run is picked up for some types and
not others. The first post-001 run reported +81 instead of +74 for exactly that reason; a
record-level diff of the JSON against the previous run identified the seven contaminated entries.
Attribute a delta by diffing `xna_signature` classifications between runs, not by the summary
counters.


## 3. Definition of done and implementation rules

The API milestone is 331/331 runtime public types, zero unexplained `MISSING`, zero `NEEDS_REVIEW`, and ideally 3,627/3,627 strict documented member representation. If a member truly has no meaningful C++ shape, document its reason individually and show strict versus C++-applicable totals; do not use `NOT_APPLICABLE` to erase work. Behavior, service availability, and renderer parity remain separate measures.

Microsoft XML/DLL metadata controls public/protected **shape**, static ownership, overloads, generic arity, and enum values. Inspect the decompiled Microsoft source under `xna4-decomp/reference/xna4/decompiled/windows/` for behavior and edge cases; FNA under `/rv/data/library/github.com/FNA-XNA/FNA/src/` is an additional behavioral guide and cannot silently change the Microsoft surface. Preserve `getXProperty`/`setXProperty`, `System::EventHandler<T>`, C++ ref/out conventions, and existing useful C++ APIs. Place .NET facilities in Sharp Runtime, not ad hoc CNA substitutes. A typed `Equals(T)` does not cover `Equals(System.Object)`; a pointer/count or string overload does not automatically cover an array or `System.Uri` overload. Public XNA declarations need Doxygen blocks per `AGENTS.md` and focused tests per overload. Review `CHECKLIST.md` for any source file ported in full.

Do not close records with empty stubs, unconditional throws, audit ignore lists, unrelated renames, or unsupported `HOST_LANGUAGE_SUBSTITUTION` claims. A local unsupported-service behavior can be valid for historical online APIs if its contract is intentional and tested; it is not Xbox Live parity. Keep commits small and subsystem focused. This document proposes work only; it changes no production code.

## 4. Remaining gaps by subsystem

| Audit subsystem | Missing | Tier B | Tier C | Likely size and main issue | Main CNA location | Focused target |
| --- | ---: | ---: | ---: | --- | --- | --- |
| Graphics | 100 | 97 | 3 | Large count, mostly 73 repeated PackedVector contracts; two architectural presentation/device items | `modules/graphics`, `modules/math/include/.../PackedVector` | `CnaGraphicsTests`, `CnaMathTests` |
| Core / Math | 15 | 15 | 0 | 14 object overloads plus one packed interface method | `modules/math` | `CnaMathTests` |
| Content | 11 | 9 | 2 | Reader/manager subclass seams; resources and serialization | `modules/content` | `CnaContentTests` |
| Input | 11 | 11 | 0 | Value object overloads and four formatters | `modules/input` | `CnaInputModuleTests` |
| XACT | 6 | 6 | 0 | Value contracts and protected disposal hooks | `modules/audio` | `CnaAudioTests` |
| Design | 5 | 5 | 0 | Three inherited converter overrides; two protected field names | `modules/design` | `CnaDesignTests` |
| GamerServices | 3 | 3 | 0 | Explicit `ICollection<KeyValuePair<...>>` operations | `modules/gamer-services` | `CnaGamerServicesTests` |
| Audio | 2 | 2 | 0 | Listener array and protected disposal | `modules/audio` | `CnaAudioTests` |
| Runtime | 2 | 2 | 0 | GraphicsDeviceInformation equality/hash | `modules/runtime` | `CnaRuntimeTests` |
| Avatar | 1 | 1 | 0 | `Changed` exists with wrong static ownership | `modules/gamer-services` | `CnaGamerServicesTests` |
| Touch | 1 | 1 | 0 | Object equality overload | `modules/input` | `CnaInputModuleTests` |
| Media | 1 | 1 | 0 | `System.Uri` overload | `modules/media` | `CnaMediaTests` |
| Net | 1 | 0 | 1 | Serialization payload | `modules/net` | `CnaNetTests` |
| Storage | 1 | 0 | 1 | Serialization constructor | `modules/storage` | `CnaStorageTests` |
| **Total** | **160** | **153** | **7** | | | |

PackedVector-related work spans two audit subsystems: **73 Graphics records + 1 Core / Math record = 74**. Package totals below are disjoint; cross-references do not add gaps twice. Package counts are `001–004: 74+14+12+5`, `005–009: 5+4+3+6+1`, `010–014: 2+1+9+1+3`, and `015–020: 5+2+6+4+1+2`; these sum to **160**.

## 5. Detailed work packages

Every package had status **TODO** when this snapshot was taken; §2a carries the live status and the commit that closed each one. Counts are the expected audit delta, not a promise of behavior parity. Unless stated otherwise, its Microsoft signatures are the exact Appendix A entries for the named types.

### XNA-MISSING-001 — PackedVector value and interface parity

**Priority:** high. **Tier:** B. **Expected delta:** 74 `MISSING` → 0 (68 object/value contracts, 5 two/three-component conversions, 1 interface conversion). **Dependencies:** none for packed equality; settle an object-boxing convention with 002 before object overloads.

| Concrete type in `Microsoft.Xna.Framework.Graphics.PackedVector` | Missing members | Count |
| --- | --- | ---: |
| Alpha8 | `Equals(Alpha8)`, `Equals(System.Object)`, `GetHashCode`, `ToString` | 4 |
| Bgr565 | same four with `Bgr565`, plus `ToVector3` | 5 |
| Bgra4444, Bgra5551, Byte4, HalfSingle, HalfVector2, HalfVector4 | the four value/object methods per type | 24 |
| NormalizedByte2 | four value/object methods plus `ToVector2` | 5 |
| NormalizedByte4 | four value/object methods | 4 |
| NormalizedShort2 | four value/object methods plus `ToVector2` | 5 |
| NormalizedShort4 | four value/object methods | 4 |
| Rg32 | four value/object methods plus `ToVector2` | 5 |
| Rgba1010102, Rgba64 | four value/object methods per type | 8 |
| Short2 | four value/object methods plus `ToVector2` | 5 |
| Short4 | four value/object methods | 4 |
| Non-generic `IPackedVector` | `ToVector4` interface method | 1 |

The concrete headers are `modules/graphics/include/Microsoft/Xna/Framework/Graphics/PackedVector/{Alpha8,Bgr565,Bgra4444,Bgra5551,Byte4,HalfSingle,HalfVector2,HalfVector4,NormalizedByte2,NormalizedByte4,NormalizedShort2,NormalizedShort4,Rg32,Rgba1010102,Rgba64,Short2,Short4}.hpp`; the non-generic interface is `modules/math/include/Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp`. These currently have packed storage, constructors, `PackFromVector4`, `ToVector4`, and `==`/`!=`; for example `Bgr565.hpp` can unpack via `ToVector4()` but lacks `ToVector3()`. The audit did **not** find additional missing `PackFromVector*` records; do not invent them.

Use the Microsoft class bodies/XML for exact `ToString` formatting and `GetHashCode` values, not a blanket `std::hash`/`std::to_string` rule. Typed equality should compare the documented packed value; object equality must reject null/wrong type and delegate to typed equality. Preserve the packing oracle and existing rounding/NaN behavior. A private helper may remove repetition without moving public XNA members into CNA-only APIs. Add `ToVector4()` to the non-generic interface and implement it for every derived packed type, including `Color` and any non-17 implementers; otherwise polymorphic calls would break. Tests: `modules/graphics/tests/Microsoft/Xna/Framework/Graphics/PackedVector/{PackedVectorTests,XnaFrameworkPackingTests}.cpp`, `modules/graphics/tests/PackedVectorGolden.md`, and Math interface probes. Test equal/unequal/wrong-type/null, equal-hash consistency, exact strings, channel endpoints/round trips, and virtual dispatch. Validate `CnaGraphicsTests`, `CnaMathTests`, audit.

### XNA-MISSING-002 — Core / Math object equality

**Priority:** high. **Tier:** B. **Expected delta:** 14. **Members:** `Equals(System.Object)` on BoundingBox, BoundingFrustum, BoundingSphere, Color, CurveKey, Matrix, Plane, Point, Quaternion, Ray, Rectangle, Vector2, Vector3, and Vector4 (Appendix A). All have nearby typed equality or operators; do not duplicate their comparisons.

Inspect `modules/math/include/Microsoft/Xna/Framework/{type}.hpp` and matching `modules/math/src/{type}.cpp`, notably `BoundingBox::Equals(const BoundingBox&)` and `Vector3::Equals(const Vector3&)`. Sharp Runtime `modules/core/include/System/Object.hpp` supports `virtual Equals(const Object*)`, but C++ value structs are not uniformly `System::Object`. The current audit maps both `std::any` and `System::Object` to CLR `System.Object`; a value-struct `Equals(const std::any&)` can inspect `any_cast<T>` without adding a vptr or changing packed layout, while `BoundingFrustum` can use the inherited object contract directly. Validate the chosen convention with a compile-time and behavioral probe before applying it to 001/003/004. Verify null/empty object, wrong type, and equal/unequal values against Microsoft implementations; maintain hash/equality consistency. Update corresponding `modules/math/tests/Microsoft/Xna/Framework/*Tests.cpp`; run `CnaMathTests`, `CnaCoreTests`, audit.

### XNA-MISSING-003 — Input and Touch value contracts

**Priority:** high. **Tier:** B. **Expected delta:** 12 (11 Input, 1 Touch). **Members:** `Equals(System.Object)` and `ToString` on GamePadButtons, GamePadDPad, GamePadThumbSticks, GamePadTriggers; `Equals(System.Object)` on GamePadState, KeyboardState, MouseState, and TouchLocation. This is value-surface work; no GamePad/Keyboard/Mouse/TouchPanel platform polling method is missing in the current audit.

Headers/sources are `modules/input/include/Microsoft/Xna/Framework/Input/{GamePadButtons,GamePadDPad,GamePadThumbSticks,GamePadTriggers,GamePadState,KeyboardState,MouseState}.hpp`, `.../Input/Touch/TouchLocation.hpp`, and their source files. Existing `Equals(T)` and GetHashCode implementations provide comparisons; use package 002's object-boxing convention. Do not assume every `ToString()` is a field dump: check each decompiled Microsoft struct. Test wrong type/null, equal/unequal snapshots, default states, and exact formatting in `modules/input/tests/Microsoft/Xna/Framework/Input/{GamePadButtonsTests,GamePadStateTests,GamePadThumbSticksTests,GamePadTriggersTests,Touch/TouchLocationStateTests}.cpp` plus new targeted DPad/Keyboard/Mouse cases. Run `CnaInputModuleTests` with the documented haptic fixture caveat, audit.

### XNA-MISSING-004 — Graphics vertex value equality

**Priority:** high. **Tier:** B. **Expected delta:** 5. **Members:** `Equals(System.Object)` on VertexElement, VertexPositionColor, VertexPositionColorTexture, VertexPositionNormalTexture, VertexPositionTexture. Typed equality and value/hash tests exist; add only the object overload using package 002's convention, with no vertex layout changes. Files: `modules/graphics/include/Microsoft/Xna/Framework/Graphics/{VertexElement,VertexPositionColor,VertexPositionColorTexture,VertexPositionNormalTexture,VertexPositionTexture}.hpp` and matching `.cpp`. Tests: `VertexElementTests.cpp`, `VertexPosition*Tests.cpp`, `VertexValueHashTests.cpp`. Test wrong type/null and hash consistency. Run `CnaGraphicsTests`, audit.

### XNA-MISSING-005 — Stock effect protected copy constructors

**Priority:** medium. **Tier:** B. **Expected delta:** 5. **Members:** protected `#ctor(same effect type)` on AlphaTestEffect, BasicEffect, DualTextureEffect, EnvironmentMapEffect, SkinnedEffect. Microsoft decompiled sources show these constructors feeding `Clone()`; CNA currently exposes GraphicsDevice constructors and clone behavior but not the protected subtype constructor. Work in matching `modules/graphics/include/Microsoft/Xna/Framework/Graphics/*Effect.hpp` and `modules/graphics/src/Xna/*Effect.cpp`. Delegate to existing base/effect clone state machinery, copy all public state and resource references with XNA ownership semantics, and avoid copying renderer handles blindly. Tests: corresponding `*EffectTests.cpp` using a small derived test type to exercise protected visibility and independence after source mutation/disposal. Run `CnaGraphicsTests`, audit; check HEADLESS effect support before interpreting skips.

### XNA-MISSING-006 — Buffer constructors accepting System.Type

**Priority:** medium. **Tier:** B. **Expected delta:** 4. **Members:** `DynamicIndexBuffer`, `DynamicVertexBuffer`, `IndexBuffer`, `VertexBuffer` constructors `(GraphicsDevice,System.Type,System.Int32,BufferUsage)`. Current overloads use `IndexElementSize` or `VertexDeclaration`, so name-only matching would be wrong. Files: corresponding graphics headers/sources, `VertexDeclaration.hpp`, and Sharp Runtime `System/Type.hpp` if a type-descriptor helper is genuinely missing. Resolve index element types and `IVertexType`/vertex declaration exactly as the Microsoft constructors do; reject invalid types/counts with matching exceptions. Prefer forwarding into current constructors once validated. Tests: `IndexBufferTransferContractTests.cpp`, `VertexBufferTransferContractTests.cpp`, `DynamicVertexBufferGenericSetDataTests.cpp`, and new signature/invalid-type cases. Run `CnaGraphicsTests`, audit. Likely GraphicsCore-only; renderer interface changes should be unnecessary if forwarding suffices.

### XNA-MISSING-007 — Graphics exception inner-cause constructors

**Priority:** medium. **Tier:** B. **Expected delta:** 3. **Members:** `(System.String,System.Exception)` constructors for DeviceLostException, DeviceNotResetException, NoSuitableGraphicsDeviceException. Their graphics headers currently derive from `std::runtime_error`, which has no inner-cause field; inspect Sharp Runtime `System/Exception.hpp` and either adopt the project's established XNA-exception base or retain a compatible cause explicitly. Files: `modules/graphics/include/Microsoft/Xna/Framework/Graphics/{DeviceLostException,DeviceNotResetException,NoSuitableGraphicsDeviceException}.hpp` and matching sources if needed. Preserve message and cause; avoid dropping the second argument merely to gain a signature match. Tests: `modules/graphics/tests/Microsoft/Xna/Framework/Graphics/GraphicsExceptionTests.cpp` for message, inner cause, and null cause. Run `CnaGraphicsTests`, audit.

### XNA-MISSING-008 — GraphicsDevice generic user-array draw overloads

**Priority:** medium. **Tier:** B. **Expected delta:** 6. **Members:** two `DrawUserPrimitives``1` overloads (with/without VertexDeclaration) and four `DrawUserIndexedPrimitives``1` overloads (`Int16[]`/`Int32[]`, each with/without VertexDeclaration), with the exact offsets/counts in Appendix A. Current `GraphicsDevice.hpp` has typed/pointer-count and object-staging draw paths, but the audit rejects them as the documented generic array shapes. Add public template array/span-compatible entry points that retain vertex and index element types and forward to existing validated staging paths. Check range, primitive count, vertex declaration inference, and 16/32-bit index behavior against decompiled XNA and FNA. Keep renderer modifications out unless a real contract gap is found. Files: `GraphicsDevice.hpp`, possibly `GraphicsDevice.cpp`; tests: `GraphicsDeviceValidationTests.cpp`, `GraphicsDeviceRendererTests.cpp`, `BufferDataBindingContractTests.cpp`. Test all six signatures and invalid ranges; run `CnaGraphicsTests`, audit. Risk: templates can compile while misrepresenting array length or generic arity, so verify both compile-time and runtime paths.

### XNA-MISSING-009 — GraphicsDevice protected disposal hook

**Priority:** medium. **Tier:** B. **Expected delta:** 1. **Member:** `GraphicsDevice.Dispose(System.Boolean)` (protected instance). `Dispose()` already exists. Add a protected virtual hook that the public disposer/destructor use coherently; preserve Disposing event order, resource cascade, idempotence, and use-after-dispose behavior. Files: `modules/graphics/include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp`, `modules/graphics/src/Xna/GraphicsDevice.cpp`. Tests: `GraphicsDeviceStatusTests.cpp`, `GraphicsDeviceRendererTests.cpp`, and a derived test probe. Run `CnaGraphicsTests`, audit. Similar hooks in Content/Audio/XACT belong to 012, 016, 017 so commits stay subsystem scoped.

### XNA-MISSING-010 — GraphicsAdapter historical device-selection flags

**Priority:** low, architectural. **Tier:** C. **Expected delta:** 2. **Members:** static read/write Boolean properties `GraphicsAdapter.UseNullDevice` and `UseReferenceDevice`. CNA already has **instance** `get/setUseNullDeviceProperty()` and `get/setUseReferenceDeviceProperty()` plus instance state in `GraphicsAdapter.hpp/.cpp`; these are *not* the Microsoft static properties. Decompiled XNA stores process-wide flags; `UseNullDevice` selects Direct3D9 NULL device (device type 4) before `UseReferenceDevice` selects REF device (type 2), otherwise HAL (type 1). Selection also affects capability/profile probes. A mere static getter/setter that is ignored during creation would represent state but not its documented device-selection intent.

First specify CNA's coherent policy for `HEADLESS` and software/reference-like renderers, then decide whether flags select supported renderer modes or fail explicitly at device creation when unavailable. Preserve existing instance APIs as CNA extensions if needed, without silently conflating them with static state. Inspect `GraphicsAdapter`, `GraphicsDeviceManager`, renderer selection in `cmake/RendererIdentities.cmake`, and platform creation paths. Tests: `GraphicsAdapterTests.cpp`, `GraphicsDeviceCapabilityTests.cpp`, and targeted creation/profile probes for static ownership, precedence, reset, and unavailable modes. Do not redesign renderer selection in this API task. Tier C detail in §6.

### XNA-MISSING-011 — GraphicsDevice rectangular/override-window Present

**Priority:** low, architectural. **Tier:** C. **Expected delta:** 1. **Member:** `GraphicsDevice.Present(System.Nullable{Microsoft.Xna.Framework.Rectangle},System.Nullable{Microsoft.Xna.Framework.Rectangle},System.IntPtr)`; C++ representation should preserve two optional rectangles and an `IntPtr` window handle. XNA's XML says null source/destination mean full backbuffer/client area, clips oversized rectangles, and uses `DeviceWindowHandle` when the override is zero. Decompiled XNA converts `(x,y,width,height)` to native edge rectangles and calls D3D9 `Present(source,destination,HWND)` after disposed/render-target checks, pass completion, and lazy clear. FNA wraps a nonzero foreign window and passes rectangles/handle to `FNA3D_SwapBuffers`; its no-arg overload uses the normal device window.

CNA currently has `GraphicsDevice::Present()` → `IGraphicsRenderer::Present()` with no rectangles/handle, then viewport synchronization. `IPlatformSurfacePresenter::Present(SurfaceFrame)` is likewise tied to a surface; target-window ownership is not a portable `IntPtr` mapping. Inspect `GraphicsDevice.hpp/.cpp`, `IGraphicsRenderer.hpp`, `PresentationParameters.hpp`, and `modules/platform/include/CNA/Platform/IPlatformSurfacePresenter.hpp`. A shared signature change would affect the current `Present()` implementations in `modules/renderers/{canvas,direct2d,directx9,directx11,directx12,easygl,fna3d,freedirect,gdi,headless,html-dom,metal,opengl4,portablegl,sdl-gpu,sdl-renderer,software,stub,svg-dom,vulkan,webgpu}`; `common` owns the interface. Prefer an additive capability only where backend swap/present APIs can accept source/destination/foreign target. Do not redirect to an arbitrary native handle or silently ignore nonzero overrides. Cover null-null-zero equivalence to `Present()`, rectangle clipping/empty bounds, target ownership, disposed/bound-target exceptions, and supported versus unsupported renderer behavior in `GraphicsDevicePlatformWindowTests.cpp`, `GraphicsDeviceRendererTests.cpp`, and platform presenter tests. Tier C detail in §6.

### XNA-MISSING-012 — Content manager/reader contracts

**Priority:** medium. **Tier:** B. **Expected delta:** 9. **Members:** `ContentLoadException.#ctor`; `ContentManager.Dispose(Boolean)`, `OpenStream(String)`, `ReadAsset``1(String,Action{IDisposable})`; `ContentReader.ReadRawObject``1` and `ReadRawObject``1(``0)`; `ContentTypeReader``1.#ctor` and `Read(ContentReader,Object)`; `ContentTypeReaderManager.GetTypeReader(System.Type)`.

`ContentManager` already loads XNB/CNJ, caches assets, and owns disposables, but its stream/read path is not the documented protected virtual seam; `ResourceContentManager` currently has a separate `OpenStream` that always throws. `ContentReader` has `ReadRawObject<T>(ContentTypeReaderBase&)` variants, which do not cover no-reader overloads. `ContentTypeReader<T>` has a constructor requiring target type text and a typed `Read(ContentReader&,optional<T>)`, plus `ReadUntyped(any)`; its Microsoft protected constructor and object bridge need deliberate C++ representations. `ContentTypeReaderManager` uses registered factories but lacks type lookup. Work in `modules/content/include/Microsoft/Xna/Framework/Content/{ContentLoadException,ContentManager,ContentReader,ContentTypeReader,ContentTypeReaderManager}.hpp`, `modules/content/src/Xna/{ContentManager,ContentReader}.cpp`, and matching source. Reuse Sharp Runtime `System::Type`/`Action` concepts where present; specify ownership of returned stream and recorded disposable before coding. Verify virtual dispatch, exact reader-table consumption, existing-instance handling, cache/disposal order, and missing-reader exceptions against decompiled XNA/FNA. Tests: `ContentManagerXnbTests.cpp`, `ContentReaderTests.cpp`, `CustomContentTypeReaderTests.cpp`, `ContentTypeReaderManagerTests.cpp`, plus derived-manager/reader probes. Run `CnaContentTests`, audit. This package precedes 013, which depends on the stream seam.

### XNA-MISSING-013 — ResourceContentManager resource-backed constructor

**Priority:** low, architectural. **Tier:** C. **Expected delta:** 1. **Member:** `ResourceContentManager.#ctor(System.IServiceProvider,System.Resources.ResourceManager)`. XNA rejects null manager, stores it, and `OpenStream(assetName)` calls `ResourceManager.GetObject`, expects `byte[]`, throws `ContentLoadException` when absent/wrong type, and returns a memory stream over bytes. CNA has a one-argument constructor and an `OpenStream` that always throws. **Correction to the prior report's broad note:** sibling Sharp Runtime *does* have `modules/resources/include/System/Resources/ResourceManager.hpp`, but only a string/culture `GetString` API backed by an AOT callback; it has no `GetObject` or byte resource path. Determine CNA's linkage to that module and add the smallest binary-object resource facility there if needed. Then use it in `ResourceContentManager.hpp/.cpp`; no fake CNA-specific manager. Tests: Sharp Runtime `ResourceManagerTests.cpp` for binary lookup/fallback and `CnaContentTests` for null manager, missing/wrong-type/valid bytes, and end-to-end XNB stream load. Dependency: 012's virtual `OpenStream`/`ReadAsset` route. Tier C detail in §6.

### XNA-MISSING-014 — Shared CLR exception serialization contracts

**Priority:** low, architectural. **Tier:** C. **Expected delta:** 3. **Members:** protected `ContentLoadException.#ctor(SerializationInfo,StreamingContext)`, public `NetworkSessionJoinException.GetObjectData(SerializationInfo,StreamingContext)`, protected `StorageDeviceNotConnectedException.#ctor(SerializationInfo,StreamingContext)`. These are the **one Net** and **one Storage** gaps; do not schedule duplicate Net/Storage fixes elsewhere. Sharp Runtime has `modules/runtime/include/System/Runtime/Serialization/{SerializationInfo,StreamingContext}.hpp`, but both are documented minimal stubs: `SerializationInfo` has no value map/AddValue/GetInt32, `StreamingContext` is empty, and `System::Exception` lacks the legacy serialization constructor/path. XNA's join exception writes the `joinError` integer after base state and restores it in its serialization constructor. CNA already has that protected constructor, but `modules/net/src/Xna/NetworkSessionJoinException.cpp` currently leaves `joinError_` at its default; correct the state restoration as part of this package even though it will not change the audit delta. A signature-only constructor would discard meaningful state.

Design one minimal Sharp Runtime serialization-value contract and exception state round trip, not BinaryFormatter or a broad CLR serializer. Then implement the three CNA members in `modules/content/include/.../ContentLoadException.hpp`, `modules/net/include/.../NetworkSessionJoinException.hpp` and source, `modules/storage/include/.../StorageDeviceNotConnectedException.hpp` and source. Test null info, message/inner-cause state, `joinError` round trip, and protected constructor access via derived probes. Relevant tests: `NetworkSessionJoinExceptionTests.cpp`, `StorageDeviceTests.cpp`, Content exception tests; run `CnaContentTests`, `CnaNetTests`, `CnaStorageTests`, Sharp Runtime unit tests, audit. If faithful support proves out of scope, keep these as `MISSING` with this reason; do not mark them `NOT_APPLICABLE` by convenience. Tier C detail in §6.

### XNA-MISSING-015 — Design converter overrides and protected fields

**Priority:** medium. **Tier:** B. **Expected delta:** 5. **Members:** `ConvertFrom(ITypeDescriptorContext,CultureInfo,Object)` on BoundingBoxConverter, BoundingSphereConverter, RayConverter; protected instance fields `MathTypeConverter.propertyDescriptions` (`PropertyDescriptorCollection`) and `supportStringConvert` (`Boolean`). Decompiled XNA's three overrides forward to base; their public shape is still documented. CNA `MathTypeConverter` already implements conversion and has **protected** `propertyDescriptions_`/`supportStringConvert_`, but those C++ names do not expose the exact fields to subclasses. Add the overrides forwarding through existing Sharp Runtime `ExpandableObjectConverter`/MathTypeConverter behavior; expose canonical protected field names with one state source (rename the two fields within Design or use synchronized aliases if ABI constraints demand). Do not create two independent copies. Files: `modules/design/include/Microsoft/Xna/Framework/Design/{BoundingBoxConverter,BoundingSphereConverter,RayConverter,MathTypeConverter}.hpp`, matching `.cpp`; Sharp Runtime ComponentModel already contains the core descriptor machinery. Tests: `modules/design/tests/Microsoft/Xna/Framework/Design/FrameworkDesignTests.cpp` with derived field-access compile probes, string/non-string conversion, culture/context forwarding. Run `CnaDesignTests`, audit.

### XNA-MISSING-016 — Audio listener array and disposal hook

**Priority:** medium. **Tier:** B. **Expected delta:** 2. **Members:** `SoundEffectInstance.Apply3D(AudioListener[],AudioEmitter)` and protected `Dispose(Boolean)`. CNA has single-listener `Apply3D` and pointer/count handling, but the documented managed-array shape is not accepted by the audit. Add a safe C++ collection overload preserving order/count and forwarding to existing multi-listener processing; validate empty/null semantics and disposed state using Microsoft `SoundEffectInstance.cs`. Route public `Dispose()` through a protected virtual hook without altering audio ownership or dynamic subclass disposal. Files: `modules/audio/include/Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp`, `modules/audio/src/Xna/SoundEffectInstance.cpp`; tests: `SoundEffectInstanceTests.cpp`, `DynamicSoundEffectInstanceTests.cpp` for one/many/empty listeners, spatial result, and derived disposal order. Run `CnaAudioTests`, audit.

### XNA-MISSING-017 — XACT value contracts and disposal hooks

**Priority:** medium. **Tier:** B. **Expected delta:** 6. **Members:** `AudioCategory.Equals(System.Object)`, `AudioCategory.ToString`, `RendererDetail.Equals(System.Object)`, and protected `Dispose(Boolean)` on AudioEngine, SoundBank, WaveBank. Typed equality already exists for the value types; use 002's object representation and check decompiled formatting. Public disposal exists, but protected hooks must preserve engine/bank dependency order, event/lifetime behavior, and idempotence. Files: `modules/audio/include/Microsoft/Xna/Framework/Audio/{AudioCategory,RendererDetail,AudioEngine,SoundBank,WaveBank}.hpp` and matching `.cpp`; tests: `{AudioCategory,RendererDetail,AudioEngine,SoundBank,WaveBank}Tests.cpp`, with derived disposal probes. Run `CnaAudioTests`, audit. This does not imply XACT engine/audio-bank behavioral parity beyond the added contracts.

### XNA-MISSING-018 — GamerServices collection and Avatar event ownership

**Priority:** medium. **Tier:** B. **Expected delta:** 4 (3 GamerServices, 1 Avatar). **Members:** explicit `ICollection<KeyValuePair<String,Object>>` `Add`, `Contains`, `Remove` on PropertyDictionary; `AvatarDescription.Changed` event. CNA has `PropertyDictionary.Add(key,value)` and `Remove(key)` but not the pair overloads. Use Sharp Runtime `System/Collections/Generic/KeyValuePair.hpp`; pair `Contains`/`Remove` must compare both key and value as XNA's dictionary interface does. Preserve duplicate-key and missing-pair behavior. `AvatarDescription.hpp/.cpp` already declares/defines `Changed` as **static** `EventHandler<EventArgs>`; Microsoft XML/metadata and decompiled XNA show an **instance** event, with delivery to the affected signed-in gamer's description. Move ownership to each instance and adjust raising/subscription paths, without claiming Xbox Live backend parity. Tests: `GamerServicesCollectionsTests.cpp`, `AvatarDescriptionTests.cpp` for pair equality and add/remove, event add/remove, sender/args, per-instance isolation and order. Run `CnaGamerServicesTests`, audit.

### XNA-MISSING-019 — Media Song Uri factory

**Priority:** medium. **Tier:** B. **Expected delta:** 1. **Member:** static `Song.FromUri(System.String,System.Uri)`. CNA's `FromUri(const std::string&,const std::string&)` is a useful existing C++ overload but does not provide the documented Sharp Runtime Uri type. `System/Uri.hpp` exists in Sharp Runtime. Add the typed overload and forward through validated existing path only when URI kind/scheme/escaping semantics match Microsoft `Song.cs`; keep the string overload as extension. Files: `modules/media/include/Microsoft/Xna/Framework/Media/Song.hpp`, `modules/media/src/Xna/Song.cpp`; tests: `SongTests.cpp` for valid file URI, invalid/relative URI, name, and path escaping. Run `CnaMediaTests`, audit.

### XNA-MISSING-020 — Runtime GraphicsDeviceInformation equality/hash

**Priority:** high. **Tier:** B. **Expected delta:** 2. **Members:** `GraphicsDeviceInformation.Equals(System.Object)` and `GetHashCode`. CNA derives from `System::Object`, has properties and `Clone()`, but inherits reference equality/hash. Decompiled Microsoft source compares adapter, profile, and a specific list of PresentationParameters fields: backbuffer size/format, depth format, multisampling, orientation, interval, usage, window handle, and fullscreen; hash combines the same state. Use the existing `System::Object` override and preserve equal-object/equal-hash consistency. Files: `modules/runtime/include/Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp`, `modules/runtime/src/GraphicsDeviceInformation.cpp`; tests: `GraphicsDeviceInformationTests.cpp` for equal clones, each distinguishing field, wrong type/null, and hash. Run `CnaRuntimeTests`, `CnaGraphicsTests`, audit. Review default adapter/null behavior against XNA before comparison.

## 6. Tier C / architectural compatibility work

These **seven** records are already counted in 010, 011, 013, and 014. This table makes their individual blocker and validation path explicit; it is not an extra task list.

| Exact Microsoft member | Why Tier C / prerequisite | Affected modules and viable strategy | Uncertainty, tests, risk |
| --- | --- | --- | --- |
| `Microsoft.Xna.Framework.Content.ContentLoadException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` | Base `System::Exception` lacks serialization restore; info/context are placeholders. | Sharp Runtime serialization + exception; CNA Content protected constructor, then round-trip state. | Legacy serialization state and inner-cause representation; derived probe/round-trip. Medium-high. |
| `Microsoft.Xna.Framework.Content.ResourceContentManager.#ctor(System.IServiceProvider,System.Resources.ResourceManager)` | ResourceManager **type exists** but lacks `GetObject`/binary bytes; CNA stream path currently throws. | Sharp Runtime Resources + CNA Content stream seam from 012; store manager and open byte-resource stream. | Culture fallback, ownership and wrong-type resource; null/missing/wrong-type/binary tests. High. |
| `Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseNullDevice` | Static global D3D9 NULL selection, while CNA currently has instance flags. | CNA Graphics/Runtime and renderer creation policy; static property with explicit supported/unsupported behavior. | No universal null renderer/device; state/precedence/device-create tests. High. |
| `Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseReferenceDevice` | Static global D3D9 REF selection, not CNA instance flag. | Same shared policy as preceding row; test when both flags true and profile probes. | Software renderer is not automatically D3D9 REF; state/selection tests. High. |
| `Microsoft.Xna.Framework.Graphics.GraphicsDevice.Present(System.Nullable{Microsoft.Xna.Framework.Rectangle},System.Nullable{Microsoft.Xna.Framework.Rectangle},System.IntPtr)` | Renderer and Platform present contracts lack rect/foreign-window target. | CNA Graphics, `IGraphicsRenderer`, Platform/window presenters, supported renderer families; layer an overload only after capability design. | Native handle ownership, clipping, renderer differences, event/exception order; focused window/present tests per supported renderer. Very high. |
| `Microsoft.Xna.Framework.Net.NetworkSessionJoinException.GetObjectData(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` | Needs writable info + base exception state; XNA adds `joinError`. | Sharp Runtime serialization and CNA Net; write base fields then integer. | Round-trip and null-info exceptions; Net exception test without socket dependency. Medium-high. |
| `Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` | Needs base exception restoration. | Sharp Runtime serialization and CNA Storage protected constructor. | Message/inner state and null info; derived probe/round-trip. Medium-high. |

The `Present` overload is the only Tier C item likely to require renderer contract changes; inspect actual compiled renderer identities rather than assuming every backend can target a foreign window. Platform `IntPtr` needs an explicit window-handle/ownership mapping. The device flags require a documented effect on device creation or an intentional, tested unsupported mode; storing Booleans alone would produce misleading compatibility. Sharp Runtime serialization is intentionally skeletal, while ResourceManager already exists but is string-only, so those are **different** prerequisites.

## 7. Recommended order, milestones, and commits

1. **Value-surface foundation:** agree on object-boxing shape; implement 001 (PackedVector), 002 (Core/Math), 003 (Input/Touch), 004 (graphics vertex), 020 (Runtime). Split by subsystem into several small commits, and re-run the audit after each. These 107 entries require no renderer work.
2. **Local subsystem work:** 015 Design, 019 Media, 018 GamerServices/Avatar, 016 Audio, 017 XACT, 007 graphics exceptions, and 009 device disposal. Implement paired protected disposal hooks within their owning subsystem, with tests for extensibility.
3. **Graphics/Content Tier B:** 005 effect copy constructors, 006 buffer Type constructors, 008 generic draw arrays, 012 Content reader/manager contracts. Commit separately by package. Check that current renderer and XNB paths remain valid.
4. **Shared prerequisites, then Tier C:** implement the narrow Sharp Runtime binary resource and serialization support needed by 013/014; then tackle 010 device-selection policy and 011 Present contract. Keep each architecture decision and backend validation reviewable.
5. **Closure:** rerun audit/tests, reconcile every Appendix A checkbox with current `MISSING`, verify type/enum/event counts, and update the coverage report with strict/applicable totals. Do not mark behavioral parity complete.

| Checkpoint | Represented / documented | Strict coverage | Assumption |
| --- | ---: | ---: | --- |
| Current | 3,467 / 3,627 | 95.59% | Fresh audit |
| **Measured** after 001 | **3,541 / 3,627** | **97.63%** | Exactly 74 closed, as predicted |
| **Measured** after 001, 002, 003, 004, 020 | **3,574 / 3,627** | **98.54%** | Exactly 107 closed, as predicted |
| **Measured** after phase 2 (015, 019, 018, 016, 017, 007, 009) | **3,596 / 3,627** | **99.15%** | Exactly 22 closed, as predicted |
| After 001 PackedVector | 3,541 / 3,627 | 97.63% | Exactly 74 records close |
| After 001, 002, 003, 004, 020 | 3,574 / 3,627 | 98.54% | Those packages close 107 disjoint records |
| After **all Tier B** | 3,620 / 3,627 | 99.81% | All 153 Tier B close; seven Tier C remain |
| After Tier C | 3,627 / 3,627 | 100.00% | All seven are faithfully represented; no denominator change |

If the audit corpus or classification changes, recompute these figures and explain the change rather than preserving a convenient target. Every implementation commit should list the exact Microsoft signatures it closes and the before/after audit delta.

## 8. Validation and environmental boundaries

After each batch, run `python3 -m unittest discover -s tools -p test_audit_xna_runtime_surface.py -v`, `python3 tools/audit_xna_runtime_surface.py --write-reports`, the package's focused target, and the appendix verifier below. This plan-only snapshot passed **19/19 audit regression tests**; the fresh `--json` audit passed and did not alter tracked reports. The final closure should build and test at least `CnaCoreTests`, `CnaMathTests`, `CnaRuntimeTests`, `CnaGraphicsTests`, `CnaInputModuleTests`, `CnaAudioTests`, `CnaMediaTests`, `CnaContentTests`, `CnaStorageTests`, `CnaGamerServicesTests`, `CnaNetTests`, and `CnaDesignTests` in the broadest practical HEADLESS configuration. Build targets are defined in `cmake/UnitTests.cmake`; use `cmake --build <build-dir> --target <name>`, then the binary/CTest as appropriate.

The prior XNA-MEMBER-002 run, recorded in `plans/plan_xna_runtime_surface.md`, observed HEADLESS graphics skips; six Content failures in unsupported cube/3D/DXT paths; 68 Net failures when sandboxed ENet/UDP sockets could not bind; `FakeHapticTest.*` fixture failures; and unavailable `SDL_shadercross.h` for an optional SDL_GPU syntax check. On this snapshot's `build-probe` (`CNA_GRAPHICS_RENDERER=HEADLESS`), a focused Content cube test still fails because HEADLESS cannot store a complete cube face, and `ENetBackendTest.StartHostingIsIdempotent` still fails creating an ENet host. **The prior haptic caveat no longer applies here:** all 35 `FakeHapticTest.*` tests pass. `pkg-config` still does not find `SDL3_shadercross`. The old aggregate Content/Net failure counts were not remeasured in this plan-only task. Reproduce and separate environmental failures from changed-member regressions in each implementation batch; never use these notes as waivers for a new failure.

## 9. API representation versus behavior

Even 3,627/3,627 would mean only that the documented runtime **member surface** has accepted C++ representations. It would not establish XNA behavioral parity. Exception type/message details, boundary behavior, renderer and pixel parity, event delivery, XACT content/runtime semantics, network transport, historical Xbox Live services, and host/platform behavior need separate measurements. Test the behavior directly needed by each added member, but do not turn this plan into a renderer, backend, or Content Pipeline program.

## Appendix A — complete missing-member checklist

**Expected unchecked entries at this snapshot: 160; after phase 1: 53.** The verifier below compares unchecked entries against the current audit, so it is the authority, not this number. Each line below is one `MISSING` `xna_signature` from the fresh JSON, grouped by audit subsystem and declaring type. Change `- [ ]` to `- [x]` only after a member is implemented, tested, and the audit no longer reports it. A future agent should regenerate `/tmp/xna_runtime_missing_plan_audit.json` before trusting the checklist.

The verification command compares the **signature multiset**, not just the line count; it also recomputes the subsystem and tier totals. It intentionally compares only unchecked entries, so checked work is allowed to disappear from the current audit:

```bash
python3 tools/audit_xna_runtime_surface.py --json > /tmp/xna_runtime_missing_plan_audit.json
python3 - <<'PY'
from collections import Counter
from pathlib import Path
import json, re

audit = json.loads(Path('/tmp/xna_runtime_missing_plan_audit.json').read_text())
missing = [r for r in audit['member_coverage']['findings'] if r['classification'] == 'MISSING']
appendix = Path('plans/plan_missing.md').read_text().split('## Appendix A — complete missing-member checklist', 1)[1]
unchecked = re.findall(r'^- \[ \] (.+)$', appendix, re.MULTILINE)
assert Counter(unchecked) == Counter(r['xna_signature'] for r in missing)
assert len(unchecked) == len(missing)
print('Unchecked checklist:', len(unchecked), '/', len(missing))
print('By subsystem:', dict(sorted(Counter(r['subsystem'] for r in missing).items())))
print('By tier:', dict(sorted(Counter(r['gap_tier'] for r in missing).items())))
PY
```

### Audio

#### Microsoft.Xna.Framework.Audio.SoundEffectInstance

- [x] Microsoft.Xna.Framework.Audio.SoundEffectInstance.Apply3D(Microsoft.Xna.Framework.Audio.AudioListener[],Microsoft.Xna.Framework.Audio.AudioEmitter)
- [x] Microsoft.Xna.Framework.Audio.SoundEffectInstance.Dispose(System.Boolean)

### Avatar

#### Microsoft.Xna.Framework.GamerServices.AvatarDescription

- [x] Microsoft.Xna.Framework.GamerServices.AvatarDescription.Changed

### Content

#### Microsoft.Xna.Framework.Content.ContentLoadException

- [ ] Microsoft.Xna.Framework.Content.ContentLoadException.#ctor
- [ ] Microsoft.Xna.Framework.Content.ContentLoadException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)

#### Microsoft.Xna.Framework.Content.ContentManager

- [ ] Microsoft.Xna.Framework.Content.ContentManager.Dispose(System.Boolean)
- [ ] Microsoft.Xna.Framework.Content.ContentManager.OpenStream(System.String)
- [ ] Microsoft.Xna.Framework.Content.ContentManager.ReadAsset``1(System.String,System.Action{System.IDisposable})

#### Microsoft.Xna.Framework.Content.ContentReader

- [ ] Microsoft.Xna.Framework.Content.ContentReader.ReadRawObject``1
- [ ] Microsoft.Xna.Framework.Content.ContentReader.ReadRawObject``1(``0)

#### Microsoft.Xna.Framework.Content.ContentTypeReaderManager

- [ ] Microsoft.Xna.Framework.Content.ContentTypeReaderManager.GetTypeReader(System.Type)

#### Microsoft.Xna.Framework.Content.ContentTypeReader`1

- [ ] Microsoft.Xna.Framework.Content.ContentTypeReader`1.#ctor
- [ ] Microsoft.Xna.Framework.Content.ContentTypeReader`1.Read(Microsoft.Xna.Framework.Content.ContentReader,System.Object)

#### Microsoft.Xna.Framework.Content.ResourceContentManager

- [ ] Microsoft.Xna.Framework.Content.ResourceContentManager.#ctor(System.IServiceProvider,System.Resources.ResourceManager)

### Core / Math

#### Microsoft.Xna.Framework.BoundingBox

- [x] Microsoft.Xna.Framework.BoundingBox.Equals(System.Object)

#### Microsoft.Xna.Framework.BoundingFrustum

- [x] Microsoft.Xna.Framework.BoundingFrustum.Equals(System.Object)

#### Microsoft.Xna.Framework.BoundingSphere

- [x] Microsoft.Xna.Framework.BoundingSphere.Equals(System.Object)

#### Microsoft.Xna.Framework.Color

- [x] Microsoft.Xna.Framework.Color.Equals(System.Object)

#### Microsoft.Xna.Framework.CurveKey

- [x] Microsoft.Xna.Framework.CurveKey.Equals(System.Object)

#### Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4

#### Microsoft.Xna.Framework.Matrix

- [x] Microsoft.Xna.Framework.Matrix.Equals(System.Object)

#### Microsoft.Xna.Framework.Plane

- [x] Microsoft.Xna.Framework.Plane.Equals(System.Object)

#### Microsoft.Xna.Framework.Point

- [x] Microsoft.Xna.Framework.Point.Equals(System.Object)

#### Microsoft.Xna.Framework.Quaternion

- [x] Microsoft.Xna.Framework.Quaternion.Equals(System.Object)

#### Microsoft.Xna.Framework.Ray

- [x] Microsoft.Xna.Framework.Ray.Equals(System.Object)

#### Microsoft.Xna.Framework.Rectangle

- [x] Microsoft.Xna.Framework.Rectangle.Equals(System.Object)

#### Microsoft.Xna.Framework.Vector2

- [x] Microsoft.Xna.Framework.Vector2.Equals(System.Object)

#### Microsoft.Xna.Framework.Vector3

- [x] Microsoft.Xna.Framework.Vector3.Equals(System.Object)

#### Microsoft.Xna.Framework.Vector4

- [x] Microsoft.Xna.Framework.Vector4.Equals(System.Object)

### Design

#### Microsoft.Xna.Framework.Design.BoundingBoxConverter

- [x] Microsoft.Xna.Framework.Design.BoundingBoxConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)

#### Microsoft.Xna.Framework.Design.BoundingSphereConverter

- [x] Microsoft.Xna.Framework.Design.BoundingSphereConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)

#### Microsoft.Xna.Framework.Design.MathTypeConverter

- [x] Microsoft.Xna.Framework.Design.MathTypeConverter.propertyDescriptions
- [x] Microsoft.Xna.Framework.Design.MathTypeConverter.supportStringConvert

#### Microsoft.Xna.Framework.Design.RayConverter

- [x] Microsoft.Xna.Framework.Design.RayConverter.ConvertFrom(System.ComponentModel.ITypeDescriptorContext,System.Globalization.CultureInfo,System.Object)

### GamerServices

#### Microsoft.Xna.Framework.GamerServices.PropertyDictionary

- [x] Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Add(System.Collections.Generic.KeyValuePair{System.String,System.Object})
- [x] Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Contains(System.Collections.Generic.KeyValuePair{System.String,System.Object})
- [x] Microsoft.Xna.Framework.GamerServices.PropertyDictionary.System#Collections#Generic#ICollection{T}#Remove(System.Collections.Generic.KeyValuePair{System.String,System.Object})

### Graphics

#### Microsoft.Xna.Framework.Graphics.AlphaTestEffect

- [ ] Microsoft.Xna.Framework.Graphics.AlphaTestEffect.#ctor(Microsoft.Xna.Framework.Graphics.AlphaTestEffect)

#### Microsoft.Xna.Framework.Graphics.BasicEffect

- [ ] Microsoft.Xna.Framework.Graphics.BasicEffect.#ctor(Microsoft.Xna.Framework.Graphics.BasicEffect)

#### Microsoft.Xna.Framework.Graphics.DeviceLostException

- [x] Microsoft.Xna.Framework.Graphics.DeviceLostException.#ctor(System.String,System.Exception)

#### Microsoft.Xna.Framework.Graphics.DeviceNotResetException

- [x] Microsoft.Xna.Framework.Graphics.DeviceNotResetException.#ctor(System.String,System.Exception)

#### Microsoft.Xna.Framework.Graphics.DualTextureEffect

- [ ] Microsoft.Xna.Framework.Graphics.DualTextureEffect.#ctor(Microsoft.Xna.Framework.Graphics.DualTextureEffect)

#### Microsoft.Xna.Framework.Graphics.DynamicIndexBuffer

- [ ] Microsoft.Xna.Framework.Graphics.DynamicIndexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)

#### Microsoft.Xna.Framework.Graphics.DynamicVertexBuffer

- [ ] Microsoft.Xna.Framework.Graphics.DynamicVertexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)

#### Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect

- [ ] Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect.#ctor(Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect)

#### Microsoft.Xna.Framework.Graphics.GraphicsAdapter

- [ ] Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseNullDevice
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseReferenceDevice

#### Microsoft.Xna.Framework.Graphics.GraphicsDevice

- [x] Microsoft.Xna.Framework.Graphics.GraphicsDevice.Dispose(System.Boolean)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int16[],System.Int32,System.Int32)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int16[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int32[],System.Int32,System.Int32)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserIndexedPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,System.Int32[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.DrawUserPrimitives``1(Microsoft.Xna.Framework.Graphics.PrimitiveType,``0[],System.Int32,System.Int32,Microsoft.Xna.Framework.Graphics.VertexDeclaration)
- [ ] Microsoft.Xna.Framework.Graphics.GraphicsDevice.Present(System.Nullable{Microsoft.Xna.Framework.Rectangle},System.Nullable{Microsoft.Xna.Framework.Rectangle},System.IntPtr)

#### Microsoft.Xna.Framework.Graphics.IndexBuffer

- [ ] Microsoft.Xna.Framework.Graphics.IndexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)

#### Microsoft.Xna.Framework.Graphics.NoSuitableGraphicsDeviceException

- [x] Microsoft.Xna.Framework.Graphics.NoSuitableGraphicsDeviceException.#ctor(System.String,System.Exception)

#### Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Alpha8.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.ToString
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgr565.ToVector3

#### Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra4444.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Bgra5551.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.Byte4

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Byte4)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Byte4.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfSingle.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector2.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.HalfVector4.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.ToString
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte2.ToVector2

#### Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedByte4.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.ToString
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort2.ToVector2

#### Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.NormalizedShort4.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.Rg32

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rg32)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.ToString
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rg32.ToVector2

#### Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba1010102.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Rgba64.ToString

#### Microsoft.Xna.Framework.Graphics.PackedVector.Short2

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short2.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Short2)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short2.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short2.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short2.ToString
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short2.ToVector2

#### Microsoft.Xna.Framework.Graphics.PackedVector.Short4

- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short4.Equals(Microsoft.Xna.Framework.Graphics.PackedVector.Short4)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short4.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short4.GetHashCode
- [x] Microsoft.Xna.Framework.Graphics.PackedVector.Short4.ToString

#### Microsoft.Xna.Framework.Graphics.SkinnedEffect

- [ ] Microsoft.Xna.Framework.Graphics.SkinnedEffect.#ctor(Microsoft.Xna.Framework.Graphics.SkinnedEffect)

#### Microsoft.Xna.Framework.Graphics.VertexBuffer

- [ ] Microsoft.Xna.Framework.Graphics.VertexBuffer.#ctor(Microsoft.Xna.Framework.Graphics.GraphicsDevice,System.Type,System.Int32,Microsoft.Xna.Framework.Graphics.BufferUsage)

#### Microsoft.Xna.Framework.Graphics.VertexElement

- [x] Microsoft.Xna.Framework.Graphics.VertexElement.Equals(System.Object)

#### Microsoft.Xna.Framework.Graphics.VertexPositionColor

- [x] Microsoft.Xna.Framework.Graphics.VertexPositionColor.Equals(System.Object)

#### Microsoft.Xna.Framework.Graphics.VertexPositionColorTexture

- [x] Microsoft.Xna.Framework.Graphics.VertexPositionColorTexture.Equals(System.Object)

#### Microsoft.Xna.Framework.Graphics.VertexPositionNormalTexture

- [x] Microsoft.Xna.Framework.Graphics.VertexPositionNormalTexture.Equals(System.Object)

#### Microsoft.Xna.Framework.Graphics.VertexPositionTexture

- [x] Microsoft.Xna.Framework.Graphics.VertexPositionTexture.Equals(System.Object)

### Input

#### Microsoft.Xna.Framework.Input.GamePadButtons

- [x] Microsoft.Xna.Framework.Input.GamePadButtons.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Input.GamePadButtons.ToString

#### Microsoft.Xna.Framework.Input.GamePadDPad

- [x] Microsoft.Xna.Framework.Input.GamePadDPad.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Input.GamePadDPad.ToString

#### Microsoft.Xna.Framework.Input.GamePadState

- [x] Microsoft.Xna.Framework.Input.GamePadState.Equals(System.Object)

#### Microsoft.Xna.Framework.Input.GamePadThumbSticks

- [x] Microsoft.Xna.Framework.Input.GamePadThumbSticks.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Input.GamePadThumbSticks.ToString

#### Microsoft.Xna.Framework.Input.GamePadTriggers

- [x] Microsoft.Xna.Framework.Input.GamePadTriggers.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Input.GamePadTriggers.ToString

#### Microsoft.Xna.Framework.Input.KeyboardState

- [x] Microsoft.Xna.Framework.Input.KeyboardState.Equals(System.Object)

#### Microsoft.Xna.Framework.Input.MouseState

- [x] Microsoft.Xna.Framework.Input.MouseState.Equals(System.Object)

### Media

#### Microsoft.Xna.Framework.Media.Song

- [x] Microsoft.Xna.Framework.Media.Song.FromUri(System.String,System.Uri)

### Net

#### Microsoft.Xna.Framework.Net.NetworkSessionJoinException

- [ ] Microsoft.Xna.Framework.Net.NetworkSessionJoinException.GetObjectData(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)

### Runtime

#### Microsoft.Xna.Framework.GraphicsDeviceInformation

- [x] Microsoft.Xna.Framework.GraphicsDeviceInformation.Equals(System.Object)
- [x] Microsoft.Xna.Framework.GraphicsDeviceInformation.GetHashCode

### Storage

#### Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException

- [ ] Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)

### Touch

#### Microsoft.Xna.Framework.Input.Touch.TouchLocation

- [x] Microsoft.Xna.Framework.Input.Touch.TouchLocation.Equals(System.Object)

### XACT

#### Microsoft.Xna.Framework.Audio.AudioCategory

- [x] Microsoft.Xna.Framework.Audio.AudioCategory.Equals(System.Object)
- [x] Microsoft.Xna.Framework.Audio.AudioCategory.ToString

#### Microsoft.Xna.Framework.Audio.AudioEngine

- [x] Microsoft.Xna.Framework.Audio.AudioEngine.Dispose(System.Boolean)

#### Microsoft.Xna.Framework.Audio.RendererDetail

- [x] Microsoft.Xna.Framework.Audio.RendererDetail.Equals(System.Object)

#### Microsoft.Xna.Framework.Audio.SoundBank

- [x] Microsoft.Xna.Framework.Audio.SoundBank.Dispose(System.Boolean)

#### Microsoft.Xna.Framework.Audio.WaveBank

- [x] Microsoft.Xna.Framework.Audio.WaveBank.Dispose(System.Boolean)
