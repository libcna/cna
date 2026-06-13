# CNA Port Checklist

Use this checklist for every `.cs` file ported from FNA to CNA.

---

## Per-file checklist

### Headers / boilerplate
- [ ] `// SPDX-License-Identifier: MS-PL` present in `.hpp`
- [ ] `// SPDX-License-Identifier: MS-PL` present in `.cpp`
- [ ] `#include "CNA/CNAHelper.hpp"` present in `.hpp` if `NOXNA` is used anywhere

### Doxygen documentation
- [ ] Every public method, constructor, operator, property getter/setter, and constant in the `.hpp` has a `/** @brief … */` Doxygen block comment
- [ ] Methods with parameters have `@param` for each parameter
- [ ] Non-void methods have `@return`
- [ ] No public member is left undocumented
- [ ] No bare `///` comments on public API declarations (only `/** */` blocks allowed)

### API surface (compare line-by-line with FNA source)
- [ ] All public fields / constants present
- [ ] All public properties mapped to `getXProperty()` / `setXProperty()`
- [ ] All public methods present with correct signatures
- [ ] All public static methods present
- [ ] All events present as `System::EventHandler<TArgs>` fields
- [ ] All `ref`/`out` overloads present as value-ref pairs
- [ ] `operator==` / `operator!=` present if FNA defines them

### Inheritance
- [ ] All interfaces from FNA implemented (e.g. `IEquatable<T>` → `System::IEquatable<T>`, `IComparable<T>` → `System::IComparable<T>`, `IDisposable` → `System::IDisposable`)
- [ ] Methods that implement interfaces have `override` keyword

### NOXNA markers
- [ ] Every method / field / type alias **not** in the XNA 4.0 API surface is marked `NOXNA`
- [ ] C++ iterator support (`begin`, `end`, `size_type`, …) marked `NOXNA`
- [ ] `GetTypeName()` marked `NOXNA` (applies to classes that inherit `System::Object`)

### GetTypeName()
- [ ] Concrete classes that inherit `System::Object` override `GetTypeName()` with `NOXNA`
- [ ] Return value is the fully-qualified .NET name, e.g. `"Microsoft.Xna.Framework.Foo"`

### Logic verification (method by method vs FNA)
- [ ] Each method body compared line-by-line with the FNA equivalent
- [ ] Every intentional deviation from FNA logic has a `//` comment explaining why
- [ ] Null/range guard differences between C# and C++ documented where relevant

### Tests
- [ ] Test file exists at `tests/Microsoft/Xna/Framework/<ClassName>Tests.cpp`
- [ ] Every public method has at least one test
- [ ] Edge cases covered: boundary values, empty collections, null/nullptr inputs where applicable
- [ ] `ref`/`out` overloads tested separately
- [ ] Event firing verified with a lambda subscriber
- [ ] `GetHashCode()`: equal objects → equal hash; different objects → (typically) different hash
- [ ] `ToString()`: format spot-checked against FNA output

### Classes that cannot be unit-tested
If the class depends on `Game` / SDL / graphics backend, document it and skip tests:
- [ ] Add comment in test suite directory or `// No tests: requires SDL/Game` at the top of a stub file

---

## Known acceptable C++ deviations from FNA/XNA

| Deviation | Reason |
|---|---|
| `GetHashCode()` returns `std::size_t` instead of `int` | C++ hash size is platform-native |
| `ref`/`out` params become value-reference pairs | No C# ref/out in C++ |
| `IEnumerable<T>` replaced by `begin()`/`end()` (NOXNA) | C++ iterator idiom |
| `Type`-based service lookup uses `typeid` / templates | No C# reflection |
| Type-assignability check in `AddService` omitted | No runtime reflection in C++ |
| `Equals(object obj)` override omitted | No `object` base in C++ structs/value types |
| `DeviceCreated`/`DeviceDisposing` event hookup simplified | Service always available in CNA |
| `IsAssignableFrom` check in `GameServiceContainer` omitted | No runtime reflection |
