# CNA C API Coverage Matrix

## Coverage rule

The final C API covers every public CNA symbol. This matrix is the authoritative evidence for that
claim. A row identifies the source C++ type/member/constant/event, its C-native mapping, owning C
header, implementation/test locations, capability limitations and status.

The matrix began empty at Phase B0. The initial adapter evidence below is deliberately a small
implemented slice, not a substitute for the complete generated inventory required by `CBIND-033`.
Any public symbol without a later inventory row remains **unimplemented**, never implicitly
unsupported or complete.

## Implemented initial adapter slice

| Source header and symbol | C mapping | C header/declaration | Ownership and thread rule | C-only evidence | Status |
|---|---|---|---|---|---|
| `Game` constructor, `RunOneFrame`, `Run`, `Exit`, `Dispose` | Owned generation-checked game handle | `runtime.h`: `cna_game_create`, `cna_game_run_one_frame`, `cna_game_run`, `cna_game_request_exit`, `cna_game_destroy` | One active game; creation-thread calls; destroy is non-reentrant | `LifecycleSmoke.c` | Implemented slice |
| `Game` lifecycle hooks | Copied C callback table and context | `runtime.h`: `CNA_GameCallbacks`, `CNA_CallbackError` | Callback-scoped handle; callback error copied before return | `LifecycleSmoke.c` | Implemented slice |
| `GameTime` getters | Fixed-layout value snapshot | `runtime.h`: `CNA_GameTime` | Borrowed callback value | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |
| `GameWindow::setTitleProperty` | UTF-8 string view | `runtime.h`: `cna_game_set_window_title` | Creation thread; copies text | `LifecycleSmoke.c` | Implemented slice |
| `GraphicsDevice::Clear(Color)` | `CNA_Color` POD + game operation | `core.h`, `runtime.h`: `CNA_Color`, `cna_game_clear` | Creation thread; callback-safe | `AbiHeaderC.c`, `AbiHeaderCpp.cpp`, `LifecycleSmoke.c` | Implemented slice |

## Source inventory boundary

`CBIND-033` will generate the complete inventory from public framework headers under:

```text
modules/*/include/Microsoft/**
modules/*/include/CNA/**
```

The scanner excludes `CNA/Internal/**`, generated/test-only artifacts and the C API's own headers.
Any header exposed to a normal CNA application that is not matched by this rule must be added to the
generator input deliberately. The review output records the exact source commit and header/member
count.

For each inventory entry, one of these C mappings is mandatory:

| Mapping status | Required evidence |
|---|---|
| Implemented | C declaration, adapter implementation, C-only positive/negative tests and ABI layout/export tests where applicable. |
| Planned | Target C header/function family, required mapping form and blocking predecessor. |
| Native limitation | A callable C API reports the exact existing CNA renderer/platform limitation; it has a test and owner-approved documentation. |

`Not applicable` is not a valid status for a public CNA symbol. C++ syntax differences are solved by
a C mapping, not by dropping the symbol.

## Required row fields

```text
Source header and symbol
Public family
C mapping (POD / handle / function set / callback / count-copy)
C header and declaration(s)
Ownership and thread rule
Error/capability behavior
C-only test(s)
ABI test(s)
Status
```

`CBIND-043` will make the inventory comparison a required build/CI gate. Adding a new public CNA
symbol must add its coverage row and matching C API work in the same task; deleting or changing a
public symbol must update the mapping intentionally.
