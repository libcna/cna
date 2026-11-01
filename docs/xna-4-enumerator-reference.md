# XNA 4.0 collection enumerator reference (XNA-ENUM-001)

This reference combines the original pre-implementation census with a current CNA comparison.
Its Microsoft sources are the original Windows XNA 4.0 assemblies in the sibling checkout
`../xna4-decomp/reference/xna4/original/windows/`, inspected with `monodis`, and matching XML
documentation under `../xna4-decomp/dlls/`. Decompiled C# helps read the bodies; original IL settles
the call targets. Rechecked on 2026-11-01; source inspection is distinguished from fresh runtime
execution below.

The complete runtime XML corpus has **331** distinct documented `T:` entries after
excluding the build-time `Microsoft.Xna.Framework.Content.Pipeline.xml`. The
earlier 329 denominator collapsed two CLR generic/non-generic name pairs by
stripping the arity-one suffix: `ContentTypeReader`/`ContentTypeReader<T>` and
`IPackedVector`/`IPackedVector<T>`. They are four CLR types, so the audit
counts all four. CNA represents the non-generic content reader as
`ContentTypeReaderBase` and the generic packed-vector interface as
`IPackedVectorT` because C++ cannot overload type names by generic arity.
Before XNA-ENUM-001, the namespace/owner-aware census found **326/331** declarations: four
graphics nested types and the touch nested type were missing. The gamer nested struct already
existed but needed interface/state corrections. The older 323/329 figure treated all six as absent
and collapsed the arity pairs. Those are historical counts. The current full audit represents
**331/331** documented types, including all six enumerators; see
[`current member coverage`](xna-4-runtime-member-coverage.md). Declaration coverage does not prove
the state behaviour below, and one current mismatch is recorded explicitly.

| Declaring collection | Nested type | CLR kind and visibility | Generic element | Constructor (all assembly/internal) | Backing state |
| --- | --- | --- | --- | --- | --- |
| `Graphics.ModelBoneCollection` | `Enumerator` | public struct | `ModelBone` | `ModelBone[]` | array reference, position |
| `Graphics.ModelEffectCollection` | `Enumerator` | public struct | `Effect` | `List<Effect>` | `List<Effect>.Enumerator` |
| `Graphics.ModelMeshCollection` | `Enumerator` | public struct | `ModelMesh` | `ModelMesh[]` | array reference, position |
| `Graphics.ModelMeshPartCollection` | `Enumerator` | public struct | `ModelMeshPart` | `ModelMeshPart[]` | array reference, position |
| `Input.Touch.TouchCollection` | `Enumerator` | public struct | `TouchLocation` | `TouchCollection` | value copy of collection, position |
| `GamerServices.GamerCollection<T>` | `GamerCollectionEnumerator` | public struct | `T : Gamer` | `List<T>.Enumerator` | `List<T>.Enumerator` |

Each struct implements `System.Collections.Generic.IEnumerator<T>`,
`System.Collections.IEnumerator`, and `System.IDisposable`. Each has a public typed
`Current` getter returning the element by value (a reference for the five reference
element types), public `bool MoveNext()`, public `void Dispose()`, and explicit
non-generic `IEnumerator.Current` and `IEnumerator.Reset`. No public constructors,
fields, setters, or events appear in the metadata. `GetEnumerator()` on each owner
returns its nested struct by value. Copies have independent cursor state. As
CLR value types, all six also have an implicit zero-initialized `default`
value despite lacking a public declared constructor. CNA exposes a public
default constructor for that value. Default model array/list and gamer enumerators have no owner;
their MoveNext calls fail with NullReferenceException. Touch is different: the CLR collection holds
inline touch values and a count, and CNA holds a copied vector. Its default cursor is empty,
MoveNext returns false and Current throws ArgumentOutOfRangeException. Do not infer a null array
backing for Touch from the three model-array types.

For the three model array enumerators and Touch, the cursor starts at `-1`,
`MoveNext()` increments it and clamps it to the collection count on exhaustion,
and `Reset()` sets it back to `-1`. `Current` indexes the array/collection directly:
before start and after end the model array cursors throw IndexOutOfRangeException and Touch throws
ArgumentOutOfRangeException. `Dispose()` is empty.
The model arrays have fixed length after construction; CNA records the count
at enumerator creation because its model storage is a vector and the
`CNAEXT` `ModelBone::AddChild` helper can extend it later. Existing element
pointers are still read from canonical storage. The Touch enumerator holds a
value copy, so later changes to the source collection are not observed.
Neither uses mutation version tracking.

The Effect and Gamer enumerators wrap `List<T>.Enumerator`. Their typed
`Current` returns its current field, which is `null` before the first successful
`MoveNext()` and after exhaustion for these reference element types. The XNA wrappers' explicit
non-generic getters also call the **typed** Current path, not List's checked non-generic getter.
They therefore return that null value outside a valid element. `MoveNext()` advances once per element, returns false
repeatedly at the end, and detects mutation of the underlying list through its
version. `Reset()` also checks that version. `Dispose()` delegates to the list
enumerator, whose disposal is a no-op. The `ModelEffectCollection` and
`GamerCollection<T>` backing lists can be mutated by their assembly code; the
other four collections expose no equivalent resize path to the enumerator.

### Current boxed-Current mismatch

Original Graphics IL method 962 implements `ModelEffectCollection.Enumerator`'s non-generic
getter by calling its typed `get_Current()`. GamerServices IL method 64 directly calls
`List<T>.Enumerator.get_Current()` and boxes its result. Neither calls List's non-generic
`IEnumerator.Current` state check. This corrects the earlier text that attributed that check to
the XNA wrapper.

Current CNA `ModelEffectCollection::Enumerator::getCurrentProperty()` and
`GamerCollection<T>::GamerCollectionEnumerator::getCurrentProperty()` instead test `hasCurrent_`
and throw InvalidOperationException before start/after end/reset. Existing tests assert that CNA
behaviour, so passing them is not proof of parity for this getter. The mismatch is recorded in
[`handoff.md`](../handoff.md) §5.1; this documentation audit does not change the implementation or
replace its regression expectations. The IL call-target check was repeated locally, without a new
Windows/XNA execution campaign.

In CNA, model and gamer reference objects are held as non-owning pointers, and
Touch holds `TouchLocation` values. Sharp Runtime already supplies all three
required interfaces. Its generic `IEnumerator<T>::Current()` returns a const
reference and its non-generic `getCurrentProperty()` returns a boxed `std::any`;
these are the established C++ host-language substitutions for the two CLR
property getters. The explicit CLR `Reset` maps to the public virtual `Reset()`
required by Sharp Runtime's `IEnumerator` interface.

Keep the collection and pointed-to model/gamer objects alive while using their cursors. CNA's
borrowed owner pointer does not retain a CLR array/list lifetime. Typed Current references also
have the host interface's validity window; copy the pointer/value when advancing the cursor.
`std::any` owns its boxed copy, not the lifetime of an object pointed to by that copy. Native
begin/end iteration is a separate C++ path and does not provide these XNA cursor/version checks.

## Verification

The existing `CnaTests` target builds. On 2026-11-01 all 12 tests in the six model, touch and gamer
enumerator suites passed privately with OPENGLES3/dummy audio. They cover default/empty/single/
multiple elements, copies, reset, disposal and applicable mutation/snapshot cases. The effect/gamer
tests currently pin the mismatching boxed-Current exception described above; they must not be
reported as twelve independent Microsoft parity proofs. The fresh runtime surface audit also
confirms the declarations. No new genuine-XNA execution or lifetime/sanitizer campaign was run.
