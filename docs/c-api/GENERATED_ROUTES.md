# Maintaining the C API routes

Every `cna_*` function is declared in a hand-written public header under
`modules/c-api/include/CNA/C/`. Its implementation is either **generated** from a short spec line or
**hand-written** C++. Generated routes are the mechanically regular ones: a property read or write,
a value computation, a text size/copy pair. Everything with real semantics — creation and
destruction, ownership and borrowing, callbacks and events, bulk transfers, multi-object
operations, thread affinity — is hand-written, and the generation exists so that this code stands
out.

## Where things live

| What | Where |
|---|---|
| Public ABI: declarations and their documentation | `modules/c-api/include/CNA/C/*.h` (hand-written) |
| What each generated route does | `modules/c-api/spec/<Family>.toml` |
| Generated route definitions | `modules/c-api/src/generated/<Family>.inc` — never edit |
| Hand-written routes and the family's lookups | `modules/c-api/src/CnaCApi<Family>.cpp`, which ends with `#include "generated/<Family>.inc"` |
| Exception barrier, error recording, `CNA_Bool` rules, handle registry, `LookupHandle` | `modules/c-api/src/CnaCApiDetail.hpp` |
| Output, text and array copy protocols | `modules/c-api/src/CnaCApiOutput.hpp` |
| C ↔ C++ value conversions (`ToNative`, `ToC`) | `modules/c-api/src/CnaCApiMarshal.hpp` |
| The generator | `tools/c-api/generate_routes.py` |

The contracts these helpers implement are in [`ERRORS.md`](ERRORS.md),
[`HANDLES.md`](HANDLES.md), [`OWNERSHIP.md`](OWNERSHIP.md) and
[`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md).

## Adding a simple route

1. Declare it in the public header, with its Doxygen comment, as for any route.
2. Add one line to the family's spec:

   ```toml
   [basic_effect]
   lookup = "GetBasicEffect"   # CNA_Result GetBasicEffect(CNA_Handle, BasicEffect**)
   object = "BasicEffect*"     # the local that lookup fills

   [basic_effect.routes]
   cna_basic_effect_get_alpha = { get = "object->getAlphaProperty()" }
   cna_basic_effect_set_alpha = { call = "object->setAlphaProperty(value)" }
   ```

3. Run `python3 tools/c-api/generate_routes.py`, build, and add a test that calls the route
   (`CApiRouteTestCoverage` fails otherwise).

A spec line is a C++ expression evaluated in the family's `.cpp` file, so it may use that file's
using-declarations and helpers. The route kinds are:

| Kind | Header shape | Generated body |
|---|---|---|
| `value` | `(args..., T* out)` | `*out` = the expression |
| `get` | `(handle, args..., T* out)` | look up, then `*out` = the expression |
| `call` | `(handle, args...)` | look up, then run the expression |
| `size` | `([handle,] args..., uint64_t* out_bytes)` | byte count of the text the expression yields |
| `copy` | `([handle,] args..., char* destination, uint64_t capacity, uint64_t* out_bytes)` | the size-then-copy protocol |

The generator takes the signature from the header and the conversions from the C types:

- a parameter whose struct type `CnaCApiMarshal.hpp` converts is available to the expression under
  its header name as the native value (`ToNative`); a `CNA_Bool` is first refused unless it is
  `CNA_FALSE` or `CNA_TRUE`, then available as `bool`; every other parameter is passed as declared;
- the result is converted by the output's type: `ToC` for a marshaled struct, `ToCBool` for
  `CNA_Bool`, `static_cast` for an enumeration or a number.

Every generated route runs inside `CallWithExceptionBarrier`, refuses a null output with
`CNA_RESULT_INVALID_ARGUMENT` before anything else, validates `CNA_Bool` inputs before the lookup,
and returns the lookup's own result for a refused handle.

A group's `lookup` is an ordinary hand-written function in the family's `.cpp`:
`CNA_Result Name(<handle>, <object>*)`. Most of them are one line over `LookupHandle`, which
records "The <kind> handle is invalid for this call." for a null, stale, released or wrong-kind
handle. A group without `lookup` holds value routes; a lookup that only validates (such as
`ValidateActiveGameHandle`) is written without `object`.

## Adding a route the generator does not fit

Write it in the family's `.cpp` like the routes already there: put the body in
`CallWithExceptionBarrier`, refuse bad arguments with `InvalidArgument`, look handles up through
the family's lookup functions, and use the shared helpers rather than new local copies:

```cpp
CNA_Result cna_sound_effect_instance_apply_3d(...)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        // validate, look up, convert with ToNative/ToC, call, write outputs
    });
}
```

Prefer hand-written code whenever generating a route would need a special case in the generator.

## Regenerating and checking

```bash
python3 tools/c-api/generate_routes.py          # rewrite src/generated/*.inc
python3 tools/c-api/generate_routes.py --check  # fail if a generated file is stale
python3 tools/c-api/generate_routes.py --list   # every generated route and its kind
```

`--check` runs as the CTest test `CApiGeneratedRoutesCurrent` and in the
`c-api-generated-routes` workflow. Generation is deterministic and needs only Python 3.11+.

## Changing the generator

Keep the emitted code the code a person would write by hand, so a debugger and a compiler
diagnostic lead somewhere readable. To add a route kind or a conversion, change
`generate_routes.py`, regenerate, and read the diff of `src/generated/`: a change meant for one
kind must not touch the others. Then build the C API and run its tests.
