# XNA 4.0 collection enumerator reference (XNA-ENUM-001)

This record was made before the CNA implementation. The source is the original Windows
Microsoft XNA 4.0 assemblies under `xna4-decomp/reference/xna4/original/windows/`,
inspected with `monodis`, and their matching Microsoft XML documentation under
`xna4-decomp/dlls/`. The decompiled C# was used to read the method bodies.

The complete runtime XML corpus has **331** distinct documented `T:` entries after
excluding the build-time `Microsoft.Xna.Framework.Content.Pipeline.xml`. The
earlier 329 denominator collapsed two CLR generic/non-generic name pairs by
stripping the arity-one suffix: `ContentTypeReader`/`ContentTypeReader<T>` and
`IPackedVector`/`IPackedVector<T>`. They are four CLR types, so the audit
counts all four. CNA represents the non-generic content reader as
`ContentTypeReaderBase` and the generic packed-vector interface as
`IPackedVectorT` because C++ cannot overload type names by generic arity.
On the starting HEAD, the namespace/owner-aware audit finds **326/331**
declarations: the four graphics nested types and the touch nested type are
absent. `GamerCollection<T>::GamerCollectionEnumerator` already exists as a
nested struct, so it counts for type existence, but its interfaces and
state/disposal behavior need this task's correction. The older 323/329
mechanical figure treated all six as absent and collapsed the two arity pairs.

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
default constructor for that value; its array/list backing is null until an
owner's `GetEnumerator()` creates a usable cursor.

For the three model array enumerators and Touch, the cursor starts at `-1`,
`MoveNext()` increments it and clamps it to the collection count on exhaustion,
and `Reset()` sets it back to `-1`. `Current` indexes the array/collection directly:
before start and after end it throws an index exception. `Dispose()` is empty.
The model arrays have fixed length after construction; CNA records the count
at enumerator creation because its model storage is a vector and the
`CNAEXT` `ModelBone::AddChild` helper can extend it later. Existing element
pointers are still read from canonical storage. The Touch enumerator holds a
value copy, so later changes to the source collection are not observed.
Neither uses mutation version tracking.

The Effect and Gamer enumerators forward to `List<T>.Enumerator`. Their typed
`Current` returns its current field, which is `null` before the first successful
`MoveNext()` and after exhaustion for these reference element types. The explicit
non-generic `Current` checks cursor state and throws `InvalidOperationException`
outside a valid element. `MoveNext()` advances once per element, returns false
repeatedly at the end, and detects mutation of the underlying list through its
version. `Reset()` also checks that version. `Dispose()` delegates to the list
enumerator, whose disposal is a no-op. The `ModelEffectCollection` and
`GamerCollection<T>` backing lists can be mutated by their assembly code; the
other four collections expose no equivalent resize path to the enumerator.

In CNA, model and gamer reference objects are held as non-owning pointers, and
Touch holds `TouchLocation` values. Sharp Runtime already supplies all three
required interfaces. Its generic `IEnumerator<T>::Current()` returns a const
reference and its non-generic `getCurrentProperty()` returns a boxed `std::any`;
these are the established C++ host-language substitutions for the two CLR
property getters. The explicit CLR `Reset` maps to the public virtual `Reset()`
required by Sharp Runtime's `IEnumerator` interface.
