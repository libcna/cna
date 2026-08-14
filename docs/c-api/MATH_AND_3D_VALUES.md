# Math and 3D Value ABI

CBIND-035A establishes the C-safe value and identity vocabulary required by later 3D APIs. It is a
layout contract, not an implementation claim for math, geometry, packed conversion or rendering
operations.

## Fixed-layout values

`math_values.h` exposes caller-owned POD values for `CNA_Point`, `CNA_Vector4`, `CNA_Quaternion`,
`CNA_Matrix`, `CNA_Plane`, `CNA_Ray`, `CNA_BoundingBox`, `CNA_BoundingSphere` and
`CNA_BoundingFrustum`. They contain only fixed-width integers, `float` values and other public CNA
PODs. `CNA_Matrix` stores `m11` through `m44` contiguously in row-major order. A bounding frustum
stores its defining matrix; later operations derive planes and corners from that value rather than
exposing native cached storage.

`CNA_VertexElement` is a 16-byte value containing a byte offset, format, semantic usage and usage
index. It is only a declaration element: vertex-declaration ownership and buffer submission remain
future CBIND-035C/F work.

The 17 `CNA_Packed*` names are raw unsigned storage aliases with the same 8-, 16-, 32- or 64-bit
width as the corresponding public PackedVector value. They represent packed-value get/set storage
without exposing C++ interfaces. Construction, float conversion, equality, hashing and formatting
remain CBIND-035B work.

## Stable identities

The headers freeze unsigned 32-bit constants for containment, plane intersection, curve
continuity/loop/tangent, buffer usage, index element size, primitive topology, SetData options,
vertex element format and vertex element usage. `CNA_PRIMITIVE_POINT_LIST_EXT` preserves CNA's
public point-list extension identity; it does not imply that every renderer currently draws it.

## ABI evidence and boundary

The strict C17 and C++23 header translation tests assert the exposed sizes, field offsets and
numeric ordinals. These values have no handles, allocation or thread affinity. Passing an
unrecognized numeric identity to a future fallible operation will be rejected by that operation;
declaring the identity itself does not advertise renderer support.

The generated coverage inventory maps only source types, directly represented fields/properties
and enum identities to this foundation. Constructors, named constants, methods, operators,
collections, intersections, transforms and packed conversions remain explicitly planned rather
than being inferred from binary layout compatibility.
