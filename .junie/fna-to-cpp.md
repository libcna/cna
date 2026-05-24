# FNA to C++ CNA Porting Guidelines

These instructions describe how to port C# XNA/FNA code into the C++ CNA project.

## Goal

Port C# XNA/FNA code to C++ while preserving the XNA 4.0 public API as closely as possible.

CNA should expose the same class, struct, enum, method, property, and operator names as the original XNA/FNA API, adapted only where C++ requires a different form.

## Source Reference

Use the local FNA source tree as the behavioral and API reference:

```text
/rv/data/library/github.com/FNA-XNA/FNA
```

Do not treat old CNA code or generated AI code as authoritative if it conflicts with the reference API.

## Namespaces

Original XNA types must stay in the matching XNA namespace.

Examples:

```cpp
Microsoft::Xna::Framework::Color
Microsoft::Xna::Framework::Vector3
Microsoft::Xna::Framework::Graphics::Texture2D
Microsoft::Xna::Framework::Graphics::SpriteBatch
```

Do not move original XNA API types into the `CNA` namespace.

The `CNA` namespace is for project-specific extensions, helpers, or non-XNA additions.

## API Names Must Match

Class names, struct names, enum names, method names, operator names, and constant/static member names must match the XNA/FNA API.

Do not rename API members to make them more C++-like if that would make the API different from XNA/FNA.

## C# Properties

C# properties must use the existing CNA C++ property convention:

```cpp
getXProperty()
setXProperty(...)
```

Example:

```csharp
public byte R { get; set; }
```

becomes:

```cpp
Byte getRProperty() const;
void setRProperty(Byte value);
```

Do not replace C# properties with public fields unless that is already the established style for the specific type.

## Type Names

When the C# source uses a .NET type name, preserve the corresponding type name in the C++ API where practical.

Examples:

```csharp
UInt32
Int32
Byte
Single
String
```

The C++ project should provide matching aliases in `sharp-runtime` or another shared runtime layer.

Example:

```cpp
using UInt32 = std::uint32_t;
using Int32 = std::int32_t;
using Byte = std::uint8_t;
using Single = float;
using String = std::string;
```

If such a type does not exist yet, add a minimal stub/alias in CNA or sharp-runtime.

For ChatGPT Plus tasks that focus on one file only, do not implement broad dependency stubs unless asked. Instead, list the missing stubs that must be added manually.

## Comments and Documentation

Public comments from the C# source should also exist in the C++ CNA public API, preferably in the `.hpp` file.

Do not copy comments word-for-word if not needed. Rephrase them naturally while preserving their meaning.

Do not add comments such as:

```text
taken from FNA
copied from FNA
based on FNA source
```

The `.hpp` and `.cpp` files must not contain notes saying that the code came from FNA or was copied from FNA.

## Visibility

Do not automatically make every C# member public in C++.

Map visibility intentionally:

- `public` -> public C++ API
- `internal` -> private, protected, detail/internal namespace, or omit if it only supports debugger/display behavior
- `private` -> private implementation detail

Example: a C# `internal DebugDisplayString` property should not automatically become public C++ API.

## Interfaces and Inheritance

If the C# type implements interfaces, preserve that relationship as much as practical in C++.

Example:

```csharp
Color : IEquatable<Color>, IPackedVector, IPackedVector<uint>
```

The C++ port should provide equivalent methods and, when reasonable, minimal interface/base-class stubs.

If an exact C# interface mapping is not practical, implement the equivalent behavior and document the intentional deviation outside the source code or in the task report.

## Missing Dependencies

If the port needs a class, enum, interface, or type alias that does not exist yet:

For Junie:
- add a minimal correctly named stub in the correct namespace when needed for compilation
- do not implement large unrelated systems

For ChatGPT Plus:
- when asked to port only one `.cs/.hpp/.cpp` unit, generate that unit and list missing dependencies
- the user may add the stubs manually later

Stubs must use the correct final namespace and name from XNA/FNA.

## No Backward Compatibility Hacks

Do not add old CNA aliases or shortcuts just to keep older demos compiling.

Incorrect:

```cpp
inline const Color CornflowerBlue(...);
inline const Color White(...);
```

Correct:

```cpp
Color::CornflowerBlue
Color::White
```

If old game code breaks after the API is corrected, that is acceptable. The game code will be fixed separately.

## File Structure

Use normal C++ split:

- declarations and public documentation in `.hpp`
- implementation in `.cpp`

Avoid putting large non-template implementations in headers.

## Behavior

Match XNA/FNA behavior over personal preference.

Preserve:

- packed value layouts
- clamping behavior
- integer cast behavior
- operator behavior
- method overloads
- default values
- exception behavior where practical

Do not redesign behavior just because another C++ design would be cleaner.

## Order of Members

Keep the C++ member order close to the C# source order where practical.

This makes review and comparison much easier.

## Build and Report

After making changes, try to build or at least compile the affected files.

Report:

- changed files
- added files/stubs
- missing dependencies
- intentional deviations
- build result
- remaining errors, if any