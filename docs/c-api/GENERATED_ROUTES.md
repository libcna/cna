# Maintaining the C API routes

Every `cna_*` function is declared in a hand-written public header under
`modules/c-api/include/CNA/C/`. Its implementation is either **generated** from a one-line spec
entry or **hand-written** C++. Generated routes are the mechanically regular ones: a property read
or write, a command, a value computation, a text size/copy pair. Everything with real semantics is
hand-written, and generation exists so that this code stands out.

## Where things live

| What | Where |
|---|---|
| Public ABI: declarations and their documentation | `modules/c-api/include/CNA/C/*.h` (hand-written) |
| What each generated route does | `modules/c-api/spec/<Family>.toml` |
| Generated route definitions — never edit | `modules/c-api/src/generated/<Family>.inc` |
| Hand-written routes and the family's lookups | `modules/c-api/src/CnaCApi<Family>.cpp`, which ends with `#include "generated/<Family>.inc"` |
| Exception barrier, error recording, `CNA_Bool` rules, text input (`ReadText`), the handle registry with `LookupHandle`, `CreateHandle` and `ReleaseHandle` | `modules/c-api/src/CnaCApiDetail.hpp` |
| Output pointers, versioned structures, text and array size-then-copy | `modules/c-api/src/CnaCApiOutput.hpp` |
| C ↔ C++ value conversions (`ToNative`, `ToC`) and C enum range checks | `modules/c-api/src/CnaCApiMarshal.hpp` |
| The generator | `tools/c-api/generate_routes.py` |
| Generated contract test — never edit | `modules/c-api/tests/pure_c/GeneratedRoutesSmoke.c` |

Every file except the two marked "never edit" (and `tests/pure_c/BoolContractSmoke.c`, which
`tools/c-api/generate_bool_contract_test.py` writes) is edited by hand. The contracts the helpers
implement are in [`ERRORS.md`](ERRORS.md), [`HANDLES.md`](HANDLES.md),
[`OWNERSHIP.md`](OWNERSHIP.md) and [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md).

## Adding a simple route

1. Declare it in the public header, with its Doxygen comment, as for any route.
2. Add one line to the family's spec:

   ```toml
   [basic_effect]
   lookup = "GetStockEffect<BasicEffect>"   # hand-written in CnaCApiEffects.cpp
   object = "BasicEffect*"                  # the local the lookup fills

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
| `get` | `(first, args..., T* out)` | look up `first`, then `*out` = the expression |
| `call` | `(first, args...)` | look up `first`, then run the expression |
| `size` | `([first,] args..., uint64_t* out_bytes)` | byte count of the text the expression yields |
| `copy` | `([first,] args..., char* destination, uint64_t capacity, uint64_t* out_bytes)` | the size-then-copy protocol |

The generator takes the signature from the header and the conversions from the C types:

- a parameter whose struct type `CnaCApiMarshal.hpp` converts is available to the expression under
  its header name as the native value (`ToNative`); a `CNA_Bool` is first refused unless it is
  `CNA_FALSE` or `CNA_TRUE`, then available as `bool`; every other parameter is passed as declared;
- the result is converted by the output's type: `ToC` for a marshaled struct, `ToCBool` for
  `CNA_Bool`, `static_cast` for an enumeration or a number.

Every generated route runs inside `CallWithExceptionBarrier`, refuses a null output with
`CNA_RESULT_INVALID_ARGUMENT` before anything else, validates `CNA_Bool` inputs before the lookup,
and returns the lookup's own result when the lookup refuses.

A group's `lookup` is an ordinary hand-written function in the family's `.cpp`,
`CNA_Result Name(<first parameter>, <object>*)`, which receives the route's first parameter
unconverted — usually a handle, sometimes a struct or string view it validates. Most lookups are
one line over `LookupHandle`, which records "The <kind> handle is invalid for this call." for a
null, stale, released or wrong-kind handle. A group without `lookup` holds value routes; a lookup
that only validates (`ValidateActiveGameHandle`) is written without `object`.

## What stays hand-written

- creation and destruction, and any route that returns a handle: they decide ownership, parent
  objects and the game's resource counts (`OWNERSHIP.md`);
- events, callbacks and subscriptions;
- bulk transfers, arrays and versioned info structures;
- routes that combine several objects or validate one argument against another;
- routes whose order of checks differs from a kind's, for example a lookup before the output
  check: generating them would change which error a double fault reports.

Write such a route like the ones around it: the body in `CallWithExceptionBarrier`, bad arguments
refused with `InvalidArgument`, handles resolved through the family's lookups, conversions through
`ToNative`/`ToC`, outputs through the `CnaCApiOutput.hpp` helpers. Prefer a hand-written route to a
special case in the generator.

## Regenerating and checking

```bash
python3 tools/c-api/generate_routes.py          # rewrite the generated files
python3 tools/c-api/generate_routes.py --check  # fail if a generated file is stale
python3 tools/c-api/generate_routes.py --list   # every generated route and its kind
```

`--check` runs as the CTest test `CApiGeneratedRoutesCurrent` and in the
`c-api-generated-routes` workflow. Generation is deterministic and needs only Python 3.11+.

The same run writes `GeneratedRoutesSmoke.c` (CTest `CApi_GeneratedRoutesSmoke`), which holds every
generated route to the contracts above: a null output is refused, a null, wrong-kind or destroyed
handle answers `CNA_RESULT_INVALID_HANDLE`, and each value text pair keeps the size-then-copy
protocol. What a route computes is tested by the family's own hand-written smoke test.

## Changing the generator

Keep the emitted code the code a person would write by hand, so a debugger and a compiler
diagnostic lead somewhere readable. To add a route kind or a conversion, change
`generate_routes.py`, regenerate, and read the diff of `src/generated/`: a change meant for one
kind must not touch the others. Then build the C API and run its tests.

The public headers are deliberately not generated: they are the ABI and its documentation, the
part a reviewer and a binding author read, and the generator takes its signatures from them.
