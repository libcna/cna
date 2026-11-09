<!-- SPDX-License-Identifier: MS-PL -->
# XNA 4 runtime exception compatibility

This page describes exception diagnostic selection at the XNA Game host boundary. General API
representation is tracked separately in
[`xna-4-runtime-member-coverage.md`](xna-4-runtime-member-coverage.md); current defects and platform
qualification gaps are in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## Message profile

Sharp Runtime supplies the process-wide AppContext switch
`SharpRuntime.UseNetFrameworkArgumentExceptionMessages`. In the `Game` constructor body, CNA sets
it to `true` only if no switch value has been registered. An explicit `false` or `true` is retained.
Standalone Sharp Runtime defaults to modern messages when the switch is absent. A content compiler
or serializer that does not construct a Game therefore does not automatically select the XNA profile.

For `System::ArgumentException("bad", "value")`:

| Profile | Message |
|---|---|
| Framework/XNA (`true`) | `bad` followed by a newline and `Parameter name: value` |
| Modern (`false` or absent without a Game) | `bad (Parameter 'value')` |

The setting affects diagnostic text through Sharp Runtime; it does not turn an unsupported API
into a supported one or establish matching validation and exception types everywhere. Messages
are composed when exceptions are constructed. Setting the switch later does not rewrite existing
exceptions, and initialization failures before the Game constructor body need not use its default.
Game disposal does not reset this process-wide setting.

An application that deliberately wants the modern profile sets it before constructing Game:

```cpp
#include "System/AppContext.hpp"

System::AppContext::SetSwitch("SharpRuntime.UseNetFrameworkArgumentExceptionMessages", false);
```

## Substring validation

Sharp Runtime implements the `System::String::Substring` checks shared by CNA consumers. In the
two-index overload, validation proceeds through negative start, start beyond the string, negative
length, and length beyond the remaining range. The final check subtracts the validated start from
the string size rather than adding signed start/length, avoiding that overflow. The profile also
selects the Framework wording for negative arguments; this is not a CNA-local string patch.

The runtime regression checks `Substring("echo", 5)` after Game construction and expects
ArgumentOutOfRangeException with `startIndex cannot be larger than length of string.` followed by
the Framework parameter line. The two Game tests do not exhaust every Substring overload or input.

## Evidence and verification

[`GameExceptionCompatibilityTests.cpp`](../modules/runtime/tests/Microsoft/Xna/Framework/GameExceptionCompatibilityTests.cpp)
contains the default-profile and explicit-modern-override regressions. Run them as separate
CTest-discovered processes, so a previous Game cannot preselect the global switch:

```bash
cmake --build cmake-build-multi --target CnaRuntimeTests --parallel 4
tools/platform/run_gpu_tests_private.sh cmake-build-multi \
    -R '^GameExceptionCompatibility\.' --output-on-failure
```

The original SAMPLE-104 campaign (2026-09-28) recorded 193 runtime cases: 191 passed and two
platform skips, plus seven matching static sample frames. Its local evidence path was
`/rv/tmp/samples/SAMPLE-104-PerformanceUtility_4_0/evidence/implementation-20260928/`; that external
artifact is historical evidence, not a portable prerequisite or a current full-suite result.
Dictionary layout changes belonged to the separate Sharp Runtime entry-order repair and are not
an ABI promise made by this exception profile. No native-Windows, browser transport or cross-
renderer qualification follows from the two diagnostic tests.

Rechecked on 2026-11-01 against Game, Sharp Runtime's exception formatter/AppContext helper and
Substring implementation. The existing `CnaRuntimeTests` target builds; both named regressions
passed privately with OPENGLES3/dummy audio. The historical 193-case/sample-image campaign was
not rerun for this documentation edit.
