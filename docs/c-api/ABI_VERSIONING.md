# CNA C API ABI and Versioning Contract

## ABI identity

The initial ABI is `0.1.0`. Its packed representation is a `uint32_t`:

```text
bits 31..16  major
bits 15..8   minor
bits  7..0   patch
```

The public header will provide `CNA_ABI_VERSION_ENCODE(major, minor, patch)`,
`CNA_ABI_VERSION_MAJOR`, `CNA_ABI_VERSION_MINOR`, `CNA_ABI_VERSION_PATCH`, and
`cna_get_abi_version()`. A consumer must reject a different major and may require a minimum minor.

ABI `0.x` is experimental: an incompatible change requires a minor-version increment, release
notes and a regenerated ABI baseline. ABI `1.x` and later permit only additive, backward-compatible
changes within a major. Removing or changing an existing function, numeric constant, struct field,
field meaning, ownership rule, error rule or callback rule requires a new ABI major.

## Naming and linkage

The public export macro will be named `CNA_C_API`. On Windows it expands to `__declspec(dllexport)`
while building the C API and `__declspec(dllimport)` for a shared-library consumer. On ELF and
Mach-O targets it uses default visibility while building the library and has no import annotation
for consumers. Static consumers use the documented `CNA_C_API_STATIC` definition.

Every exported function has C language linkage in its C++ implementation and uses the `cna_`
prefix. `CNA_*` is reserved for types, constants, macros and compile definitions. Public symbols
must appear in the checked export allowlist; the C++ implementation and Sharp Runtime symbols are
not ABI promises.

## Fixed public representations

The C API uses only these primitive representations:

| Meaning | Representation |
|---|---|
| Signed integers | `int8_t`, `int16_t`, `int32_t`, `int64_t` |
| Unsigned integers | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` |
| Boolean | `CNA_Bool` = `uint8_t`, with only `CNA_FALSE` (0) and `CNA_TRUE` (1) valid |
| Result/category/flag/set identifiers | typedef of `uint32_t` plus named integer constants, never a C `enum` field |
| Length/count/capacity/offset | `uint64_t` |
| Floating point | IEEE-754 binary32 `float` or binary64 `double`, verified on every supported build |
| Handle | `CNA_Handle` = `uint64_t` |

`long`, `unsigned long`, `wchar_t`, `size_t`, raw C++ `bool`, a compiler-selected enum underlying
type and bit-fields are prohibited in ABI parameters, ABI fields and callback signatures. Native
adapters must validate narrowing conversion to host `size_t` or CNA integer aliases and return
`CNA_RESULT_OVERFLOW` on failure.

## POD structs

All public structs are standard C POD-like layouts made only from the representations above and
pointers to such values. Extensible input/output structs begin with:

```text
uint32_t struct_size;
uint32_t struct_version;
```

The caller initializes `struct_size` to `sizeof(the exact known struct)` and `struct_version` to the
documented version. CNA accepts only the fields contained in `struct_size`; new fields are appended
at the end. A too-small mandatory prefix returns `CNA_RESULT_INVALID_ARGUMENT`; a future larger
struct is accepted only after its known prefix is validated.

Every public ABI struct receives C and C++ `sizeof`, `alignof` and `offsetof` tests. Public structs
may not rely on packing pragmas. The API uses explicit fields rather than native compiler bit-field
layout. A C API value struct is converted field-by-field to its C++ counterpart; it is never
`reinterpret_cast` to an XNA/Sharp Runtime value type.

## API evolution

New capabilities use one of these paths:

1. Add a new function with a new name.
2. Append fields to a versioned struct.
3. Add a new named `uint32_t` constant after checking numeric stability.
4. Add an explicitly labelled experimental header/function family before ABI 1.0.

Changing an existing meaning is forbidden. Deprecated functions continue to export through their
ABI major and direct consumers to their replacement in documentation. The C API's version is
independent of the CNA project version and independent of Sharp Runtime's version.
