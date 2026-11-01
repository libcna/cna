# Microsoft.Xna.Framework.Design support

## Status and inventory

CNA implements the public types of XNA 4.0's `Microsoft.Xna.Framework.Design` namespace
in the opt-in `CNA::Design` module (target `cna_design`, source under `modules/design/`):

| XNA type | Properties exposed, in order | String input |
|---|---|---|
| `MathTypeConverter` | configured by derived converters | enabled by default |
| `PointConverter` | X, Y | yes |
| `RectangleConverter` | X, Y, Width, Height | no |
| `Vector2Converter` | X, Y | yes |
| `Vector3Converter` | X, Y, Z | yes |
| `Vector4Converter` | X, Y, Z, W | yes |
| `QuaternionConverter` | X, Y, Z, W | yes |
| `MatrixConverter` | Translation, then M11 through M44 row-major | no |
| `ColorConverter` | R, G, B, A | yes |
| `BoundingBoxConverter` | Min, Max | no |
| `BoundingSphereConverter` | Center, Radius | no |
| `PlaneConverter` | Normal, D | no |
| `RayConverter` | Position, Direction | no |

No additional public Design types exist in the inspected XNA 4.0 assembly. Its
three property-descriptor helper classes are non-public implementation details;
CNA likewise keeps descriptor implementations internal.

## Reference audit

The original audit's public inventory, inheritance, constructor signatures, conversion flags,
property order, and converter IL were recovered from a local Microsoft XNA 4.0
`Microsoft.Xna.Framework.dll`. FNA was used as a secondary source for type names
and value construction, but its Design source still contains incomplete
descriptor and `InstanceDescriptor` paths, so it was not treated as the behavior
oracle for this namespace.

The 2026-11-01 cleanup independently rechecks local assembly type metadata: thirteen public
Design converters and three non-public descriptor helpers agree with the current header
inventory. It also checks current sources/tests; it does not repeat every original IL comparison.

The audit established several easy-to-miss XNA rules:

- the hierarchy is `TypeConverter` → `ExpandableObjectConverter` →
  `MathTypeConverter` → the concrete converter;
- all converters expose properties and support `CreateInstance`;
- all concrete converters convert to an executable `InstanceDescriptor`;
- only Point, Color, Quaternion, and Vector2/3/4 parse component-list strings;
- Rectangle, Matrix, BoundingBox, BoundingSphere, Plane, and Ray reject string
  input but still convert *to* string using the value's `ToString()`;
- component lists split on `CultureInfo.TextInfo.ListSeparator`, format with one
  following space, and convert each component using its registered converter;
- malformed text or the wrong component count produces `ArgumentException`.

Matrix's asymmetric behavior is deliberate: XNA disables string input for it,
but the base TypeConverter path still permits string output. Its descriptor list
contains `Translation` plus all sixteen fields, while `CreateInstance` and the
`InstanceDescriptor` use only the sixteen constructor fields.

## Architecture

`CNA::Design` depends on `CNA::Math` and SharpRuntime's `ComponentModel` module.
The generic registry remains in SharpRuntime and has no CNA dependency. Including a concrete
converter header (or the Design umbrella) references an inline initialization hook, which pulls
the once-only registration translation unit when linked with the module. It associates twelve
XNA value types with converter factories; the converters supply the ordered descriptors.
The MathTypeConverter base header alone does not install that hook. Applications do not call a
registration API.

The module is not part of the `CNA` runtime umbrella. Tools that need it link:

```cmake
target_link_libraries(my_tool PRIVATE CNA::Design)
```

This keeps converter registration and ComponentModel implementation code out of
games that never use design-time conversion.

Descriptors explicitly name known XNA fields, the Matrix `Translation` property,
and Color accessors. Construction
metadata uses SharpRuntime's small callable `ConstructorInfo` abstraction.
Neither layer scans C++ classes or implements general .NET Reflection.

## Culture and reconstruction behavior

Parsing and formatting use the supplied `CultureInfo`, or current culture when
the caller supplies null. Tests cover invariant culture, `en-US`, and `cs-CZ`;
the Czech path proves that decimal comma and semicolon list separator remain
distinct. Descriptor edits mutate a boxed value, while the XNA property-grid
workflow recreates the complete value through `CreateInstance`.

Every concrete converter emits an `InstanceDescriptor` containing the exact
constructor arguments in XNA order. Invoking the descriptor reconstructs an
equal value, including nested Vector3-bearing values and all sixteen Matrix
elements. Matrix's additional `Translation` descriptor is an alternate view of
M41, M42, and M43 rather than a seventeenth constructor argument.

## Compatibility boundary

CNA follows its established C++ mapping: CLR `object` is `std::any`, CLR `Type`
is `System::Type`, and property dictionaries use SharpRuntime's string-keyed
`Hashtable`. Converter discovery is explicit static metadata rather than CLR
custom-attribute reflection. `ITypeDescriptorContext` and Reflection expose only
the coherent subset required for conversion and reconstruction.

Within those representation boundaries, no conversion, descriptor, or
reconstruction difference is known in the implemented XNA 4.0 Framework.Design
surface. Exact localized CLR exception text is not reproduced. Visual Studio
PropertyGrid UI, CodeDOM serialization, and arbitrary runtime reflection are
outside this module's scope. Public/protected shape includes MathTypeConverter's
propertyDescriptions and supportStringConvert fields, checked through a derived test converter;
this does not turn them into public game properties.

## Verification

On 2026-11-01, CnaDesignTests builds in the existing multi-renderer tree and all 25 tests pass
through the private runner: converter discovery, culture/invalid input, descriptor editing,
constructor reconstruction and the protected surface. No Visual Studio PropertyGrid or arbitrary
reflection integration was run, and no public API or implementation was changed.
