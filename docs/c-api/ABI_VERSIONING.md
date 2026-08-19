# CNA C API ABI and Versioning Contract

## ABI identity

The ABI is `0.2.0`; `0.1.0` was the initial one. The minor moved when the routes recorded in
`plan_binding.md` CBIND-054 through CBIND-058 were added -- every one of them additive, so a
consumer built against `0.1.0` still links and behaves identically. Its packed representation is
a `uint32_t`:

```text
bits 31..16  major
bits 15..8   minor
bits  7..0   patch
```

The public header will provide `CNA_ABI_VERSION_ENCODE(major, minor, patch)`,
`CNA_ABI_VERSION_MAJOR`, `CNA_ABI_VERSION_MINOR`, `CNA_ABI_VERSION_PATCH`, and
`cna_get_abi_version()`. A consumer must reject a different major and may require a minimum minor.
The installed CMake package enforces exactly that: its version file is `SameMajorVersion`, so
`find_package(CNA 0.1 CONFIG)` accepts `0.1` and everything additive after it, and rejects a `1.x`.

ABI `0.x` is experimental: an incompatible change requires a minor-version increment, release
notes and a regenerated ABI baseline. ABI `1.x` and later permit only additive, backward-compatible
changes within a major. Removing or changing an existing function, numeric constant, struct field,
field meaning, ownership rule, error rule or callback rule requires a new ABI major.

### What a `CNA_Bool` outside {0, 1} actually does

The table above states the contract, and `CBIND-066` measured how evenly it is enforced: **94
routes take a `CNA_Bool` by value, and 29 of them refuse a byte outside {0, 1}** with
`CNA_RESULT_INVALID_ARGUMENT` — a refusal `EffectTechniqueSmoke.c` asserts as documented
behaviour. The other 65 accept it, and what happens next is not uniform: the implementation reads a
`CNA_Bool` as `!= CNA_FALSE` in 97 places and as `== CNA_TRUE` in 77, so a byte of `9` means
**true** in one route and **false** in another.

So: pass only `CNA_FALSE` or `CNA_TRUE`. Anything else is a caller error that some routes catch and
some do not, and the ones that do not disagree with each other about what it meant. Do not read
this as a licence to pass `!!x`-style values expecting C's usual any-nonzero-is-true rule — that
rule holds for barely more than half of it.

This paragraph exists because the sentence above it promised a rule the library only partly
enforces, and a contract that is documented but unevenly enforced is worse than one that says where
it stops. Making the 94 uniform is a behavioural change to published routes and is recorded as an
open decision in `plan_binding.md` `CBIND-066`, with the counts to size it.

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
| Boolean | `CNA_Bool` = `uint8_t`, with only `CNA_FALSE` (0) and `CNA_TRUE` (1) valid — see below |
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

## The recorded baseline

The rule above is only worth what enforces it, and a rule about numbers nobody reads needs
something that reads them. `tools/c-api/abi_baseline.json` is a checked-in snapshot of what the ABI
**actually is**, produced by `tools/c-api/generate_abi_baseline.py`:

| Recorded | Measured how |
|---|---|
| Every struct's size and alignment, and every field's offset and size | `sizeof`, `_Alignof` and `offsetof` in a generated probe |
| Every scalar typedef's width and alignment | the same probe |
| Every integer, string and named-color constant's value | printed by the probe, so a macro's *expansion* is what is recorded |
| The ABI version the headers declare, and the one the library reports | `CNA_ABI_VERSION` against `cna_get_abi_version()` |
| Every `cna_*` symbol the shared object exports | `nm -D --defined-only` |

The point is not that these numbers are asserted — the assertion walls in
`tests/pure_c/AbiHeaderC.c` and `tests/cpp/AbiHeaderCpp.cpp` already pin the ones each slice
remembered to pin — but that a change to **any** of them arrives as a reviewable diff instead of a
silently different binary.

When the check disagrees with the build it says which kind of difference it found:

- **Additions** — a new struct, field, constant or export. Permitted by the four evolution paths
  above; re-record them with `--write` and the diff shows exactly what was added.
- **ABI breaks** — a struct that changed size or alignment, a field that moved or was resized, a
  constant whose value changed, an export that vanished, or a library whose reported version
  disagrees with its headers. Each is named individually. These are what this section forbids.

Two gates run it:

- `CApiAbiHeaderBaseline` measures the headers alone. It needs no build, so it runs in the ordinary
  build and in the `c-api-abi-baseline` CI job, which is where a moved field is most likely to be
  introduced.
- `CApiAbiBaseline` adds the two halves only a built library can answer — its own ABI version and
  its export list — and runs in each C API build tree.

What one cannot see, it skips **by name**: a header-only run reports the export list and the
runtime version as skipped rather than passing over them in silence.

Recording the baseline needs the library:

```sh
python3 tools/c-api/generate_abi_baseline.py --write --library <build>/modules/c-api/libcna_c_api.so
```

All four build configurations export the same 2,852 symbols. That is itself part of the contract:
the ABI **surface** does not vary with the renderer or with `CNA_DEVICES` — only the answers do. A
route whose backend is absent exists and refuses, rather than disappearing from the library.
