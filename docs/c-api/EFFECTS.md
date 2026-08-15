# C Effect Metadata Contract

## Identities

`effects.h` exposes the native `EffectParameterClass` and `EffectParameterType` ordinals as
fixed-width `uint32_t` identities. All five class values and ten storage-type values are frozen by
strict C17 and C++23 assertions. These identities are metadata only; they do not expose native
shader or renderer objects.

## Annotation values

`CNA_EffectAnnotationHandle` owns an immutable native annotation built from copied UTF-8 name,
semantic, cached string and raw float-storage input. Row and column metadata are preserved verbatim,
matching the native constructor. The adapter validates only ABI structure prefixes, identity
domains, UTF-8 and caller-buffer safety.

Boolean and Int32 annotations preserve CNA's native representation: the first four bytes of the
first float slot are interpreted as an `int`. Callers construct such input by copying an `int32_t`
bit pattern into a float slot; no numeric float-to-integer conversion occurs. Single/vector values
consume ordinary floats. Matrix storage is native column-major annotation storage and the returned
`CNA_Matrix` is the established row-major value. Missing data preserves native defaults: false,
zero, zero vectors and identity Matrix.

Names, semantics and cached strings use exact count/copy operations with no terminator and no
partial write on insufficient capacity. Typed getters return copied C values; annotation handles
have no native pointers or mutable backing storage.

## Collections and evidence

`CNA_EffectAnnotationCollectionHandle` owns a mutable collection of annotation value copies.
Adding copies the source annotation. Index and exact-name lookup return new owned immutable copies;
absence is a successful false result with an invalid output handle. This collapses mutable/const
native index overloads and begin/end iterators into stable count/lookup operations. Returned copies
remain valid after the source annotation or collection is destroyed.

`EffectAnnotationSmoke.c` covers all metadata and typed getters, raw Boolean/Int32 storage, empty
fallbacks, exact strings, collection add/count/index/name behavior, copy independence, capacity
atomicity and invalid/stale/wrong-kind/wrong-thread handles. It runs unchanged under HEADLESS and
SDL_RENDERER, while a focused HEADLESS run is checked with ASan+UBSan. C17/C++23 translation tests
freeze both handles and the version-one descriptor layouts.
