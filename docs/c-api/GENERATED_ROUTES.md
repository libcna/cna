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
| C ↔ C++ value conversions (`ToNative`, `ToC`), C enum range checks and value checks (`IsFinite`, reserved bytes) | `modules/c-api/src/CnaCApiMarshal.hpp` |
| The generator | `tools/c-api/generate_routes.py` |
| Generated contract test — never edit | `modules/c-api/tests/pure_c/GeneratedRoutesSmoke.c` |

Every file except the two marked "never edit" (and `tests/pure_c/BoolContractSmoke.c`, which
`tools/c-api/generate_bool_contract_test.py` writes) is edited by hand. The contracts the helpers
implement are in [`ERRORS.md`](ERRORS.md), [`HANDLES.md`](HANDLES.md),
[`OWNERSHIP.md`](OWNERSHIP.md) and [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md).

## Quick answers

| Question | Answer |
|---|---|
| How do I add a simple C property? | Declare it in the header, add one spec line, regenerate — [below](#adding-a-simple-route). |
| How do I add a complex hand-written function? | Write it in `src/CnaCApi<Family>.cpp` against the support layer — [What stays hand-written](#what-stays-hand-written). |
| Where do C/C++ conversions live? | `ToNative`/`ToC` and the enum and value checks in `src/CnaCApiMarshal.hpp`. |
| Where are handles defined? | Handle types in the public headers; the kinds (`ObjectKind`) and the registry in `src/CnaCApiDetail.hpp`; the contract in [`HANDLES.md`](HANDLES.md). A new kind is one `ObjectKind` value plus a lookup in its family. |
| How do I specify ownership? | `CreateHandle`/`ReleaseHandle` publish and release; who owns what is in [`OWNERSHIP.md`](OWNERSHIP.md). A destroy with nothing to undo is a `release` spec line; anything else is hand-written. |
| How are strings represented? | UTF-8 `CNA_StringView` in, size-then-copy out (`size`/`copy` kinds, `ReportTextSize`/`CopyText` in `src/CnaCApiOutput.hpp`) — [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md). |
| How are arrays copied? | `CopyElements`/`CopyBytes` in `src/CnaCApiOutput.hpp`: the full count always, nothing written on a short buffer. |
| How are errors translated? | `CallWithExceptionBarrier` (`src/CnaCApiDetail.hpp`) turns every exception into a result code and error record; `InvalidArgument`/`InvalidState`/`NotSupported` refuse — [`ERRORS.md`](ERRORS.md). |
| How do I regenerate the C API? | `python3 tools/c-api/generate_routes.py`. |
| How do I verify generated files are current? | `python3 tools/c-api/generate_routes.py --check` (CTest `CApiGeneratedRoutesCurrent`, and CI). |
| Which files may I edit by hand? | All but `src/generated/*.inc` and `tests/pure_c/GeneratedRoutesSmoke.c` (and `BoolContractSmoke.c`, from its own script). |
| How do I extend the schema safely? | [Changing the generator](#changing-the-generator): change `generate_routes.py`, regenerate, read the diff of `src/generated/`, build, test. |

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
| `call` | `([first,] args...)` | look up `first` if the group has a lookup, then run the expression |
| `size` | `([first,] args..., uint64_t* out_bytes)` | byte count of the text the expression yields |
| `copy` | `([first,] args..., char* destination, uint64_t capacity, uint64_t* out_bytes)` | the size-then-copy protocol |
| `release` | `(handle)` | look up, run the statement (`true` for none), release the handle: a destroy with no bookkeeping |
| `result` | anything | look up, then return the expression, a `CNA_Result`: the route is a call into a hand-written helper |

The generator takes the signature from the header and the conversions from the C types:

- a parameter whose struct type `CnaCApiMarshal.hpp` converts is available to the expression under
  its header name as the native value (`ToNative`); a `CNA_Bool` is first refused unless it is
  `CNA_FALSE` or `CNA_TRUE`, then available as `bool`; a `CNA_StringView` is read by `ReadText`
  into a `std::string` and refused with `CNA_RESULT_ENCODING` unless it is UTF-8 (a route that
  also forbids an embedded NUL lists the parameter in `refuse_nul = ["name"]`); a lookup's own
  parameters and a `result` route's parameters are passed unconverted; every other parameter is
  passed as declared;
- the result is converted by the output's type: `ToC` for a marshaled struct, `ToCBool` for
  `CNA_Bool`, `static_cast` for an enumeration or a number; any other C structure is assigned as
  the expression builds it (`MapInfo(object)`). Handle outputs are never a `value` or `get`: they
  decide ownership, so they are a `result`;
- a group that says `raw = true` works on the C structures themselves — Point and Rectangle
  arithmetic, which wraps like XNA's integers, or a snapshot built from C fields: every struct
  parameter, including one with no conversion, is passed as declared and a struct result is
  assigned as the expression builds it.

An output that is a versioned structure (`struct_size`, `struct_version`) says `versioned = true`,
and is then refused unless it is a version 1 structure large enough for this build. A long
expression may span lines in a `'''` string; the generated route keeps its layout:

```toml
cna_texture_get_info = { versioned = true, get = '''
CNA_TextureInfo{
    sizeof(CNA_TextureInfo),
    StructureVersion,
    static_cast<uint32_t>(object.value->getLevelCountProperty()),
    static_cast<CNA_SurfaceFormat>(object.value->getFormatProperty())
}''' }
```

An argument the route refuses is written beside the kind, as the condition under which it is
invalid, over the parameters exactly as the caller passed them:

```toml
cna_sound_effect_instance_set_pan = { call = "object->value->setPanProperty(pan)", invalid_if = "!std::isfinite(pan) || pan < -1.0F || pan > 1.0F" }
```

The route then answers `CNA_RESULT_INVALID_ARGUMENT`, with the condition in the message.
`invalid_if` takes one condition or a list of them. A check that needs its own result code or
message is written as a call to a hand-written validator instead; whatever it returns other than
success is the route's answer:

```toml
cna_keyboard_state_equals = { value = "NativeKeyboardState(*left).Equals(NativeKeyboardState(*right))", validate = ["ValidateKeyboardState(left)", "ValidateKeyboardState(right)"] }
```

A check that reads the looked-up object -- an index against its count -- is a `require`, the
same kind of call made after the lookup:

```toml
cna_cnb_model_get_bone_name_size = { size = "object->value->bones[static_cast<std::size_t>(index)].name", require = 'RequireIndex(index, object->value->bones.size(), "The bone index is out of range.")' }
```

A structure the route reads through a const pointer is accepted only when an `invalid_if` or a
`validate` names it, because that check is what refuses a null or malformed one
(`invalid_if = "!IsPresentationParameters(source)"`); the contract test checks that a null one
is refused.

Every generated route runs inside `CallWithExceptionBarrier` and checks in one fixed order: a null
output is refused with `CNA_RESULT_INVALID_ARGUMENT` first (for `copy`, a null `out_bytes` or a
null destination with a capacity; for `versioned`, a structure of the wrong size or version; for
`result`, every non-const `out_*` pointer, and a handle
output is then set to `CNA_INVALID_HANDLE`), `CNA_Bool` inputs next, then the
`invalid_if` conditions and `validate` calls, then text inputs, then the lookup (and `other`'s),
whose own result is returned when it refuses, then the `require` calls. So a call with two faults always reports the null output.

A `result` route keeps its real work in a hand-written helper of the family's `.cpp` — publishing
a child handle, filling an info structure, copying an array — and the spec line only calls it:

```toml
cna_effect_material_create = { result = "CreateEffectHandle(std::make_shared<EffectMaterial>(*object->value), object->parentGame, out_effect)" }
```

A group's `lookup` is an ordinary hand-written function in the family's `.cpp`,
`CNA_Result Name(<first parameter>, <object>*)`, which receives the route's first parameter
unconverted — usually a handle, sometimes a struct or string view it validates, or an enumerator
it maps (then `object` is the canonical value, say a `PlayerIndex`). Most lookups are
one line over `LookupHandle`, which records "The <kind> handle is invalid for this call." for a
null, stale, released or wrong-kind handle. A lookup that hands back a pointer into a resource it
must keep alive names that resource as `owner` and is called `Lookup(<first>, &owner, &object)`:

```toml
[model_camera]
lookup = "GetModelCamera"                   # CNA_Result GetModelCamera(model, index, &owner, &object)
object = "const ModelCameraEXT*"
owner = "std::shared_ptr<ModelResource>"    # keeps the model, and so the camera, alive
arguments = 2
```

A route over two objects — equals, contains, index_of, add — takes the second handle right after
the lookup's parameters, and its group says how to reach it; the generated route looks it up after
the first, into `other` (a parameter itself named `other` becomes `c_other` in the definition):

```toml
other = { lookup = "BorrowAchievement", object = "std::shared_ptr<AchievementResource>" }
```

A lookup that only validates (`ValidateActiveGameHandle`) is written without `object`, and a group
without `lookup` holds the routes that need no object: value computations, static queries,
commands such as `CnbLoaderRegistry::Clear()`. `arguments = 2` passes the lookup the route's
first two parameters (a handle and an index), and `accepts_null_handle = true` records that the
lookup reads the null handle as "none" (the contract test then refuses only real handles). A group
whose routes exist only in builds with an optional module says so, and what the routes answer
without it:

```toml
feature = "CNA_DEVICES"                 # the real routes compile under #if defined(CNA_DEVICES)
unavailable = "ExtensionUnavailable()"  # otherwise each returns this, clearing handle outputs
```

## What stays hand-written

- the logic itself: what a creation publishes and counts, what a destroy undoes, how an event is
  subscribed, how a bulk transfer or info structure is filled. It lives in the family's helpers,
  which `result` routes call (`OWNERSHIP.md` has the ownership rules they implement);
- routes with more lookups than an object and an `other`, or with several statements of work
  after them;
- routes whose checks against the object are interleaved with the work, or that need an output
  that may be null;
- routes whose text is a path, a name to resolve, or a key the lookup itself takes: they follow
  the rules of what the text names (`RelativePath`, `ReadImportPath`), not only the UTF-8 rule.

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
handle answers `CNA_RESULT_INVALID_HANDLE` and leaves handle outputs invalid, and each value text
pair keeps the size-then-copy protocol. What a route computes is tested by the family's own
hand-written smoke test.

## Changing the generator

Keep the emitted code the code a person would write by hand, so a debugger and a compiler
diagnostic lead somewhere readable. To add a route kind or a conversion, change
`generate_routes.py`, regenerate, and read the diff of `src/generated/`: a change meant for one
kind must not touch the others. Then build the C API and run its tests.

The public headers are deliberately not generated: they are the ABI and its documentation, the
part a reviewer and a binding author read, and the generator takes its signatures from them.
