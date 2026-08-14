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

## Point and Rectangle operations

`math.h` completes the public Point and Rectangle families. POD initialization represents their
constructors; `cna_point_get_zero` and `cna_rectangle_get_empty` represent both named constants and
their property getters. Named operations replace C++ operators and overloads use descriptive
suffixes. The out-result forms in the C++ surface share the same C function as their value-returning
counterparts because every fallible C function already uses an output pointer.

The implementation performs integer addition, subtraction and multiplication through unsigned
bit patterns, preserving unchecked 32-bit wraparound without triggering C++ signed-overflow
undefined behavior. Point division reports `CNA_RESULT_INVALID_ARGUMENT` for a zero component and
`CNA_RESULT_OVERFLOW` for `INT32_MIN / -1`; the output remains unchanged. Rectangle containment
uses half-open right and bottom edges, touching rectangles do not intersect, and no-overlap
intersection returns the canonical empty rectangle.

Point and Rectangle strings use the native formats and the standard count/copy contract: byte
counts exclude a terminator, insufficient capacity writes no prefix, and the required count is
still returned.

## MathHelper

The stateless MathHelper class maps to the `cna_math_*` prefix rather than an artificial C object.
Its eight public scalar constants are exact `CNA_MATH_*` float macros, and its 15 callable members
use the usual result-plus-output convention. Interpolation, clamp, distance, degree/radian, angle
wrap and epsilon comparisons delegate to the canonical CNA implementation, including its NaN and
infinity behavior.

`cna_math_closest_msaa_power` accepts the meaningful nonnegative sample-count domain. It computes
the largest power of two at or below the input with unsigned operations, including the complete
positive `int32_t` range; one maps to zero because it is not multisampling. A negative input returns
`CNA_RESULT_INVALID_ARGUMENT` and preserves the caller's output instead of entering the native
helper's signed-shift/overflow edge case.

## Vector operations

`vectors.h` provides the complete Vector2, Vector3 and Vector4 surfaces. Constructors and named constants become
initialization/get operations; value-returning and out-ref native overload pairs collapse to one
fallible C result-plus-output operation. Named C operations also represent every arithmetic
operator. Vector3 additionally exposes all XNA direction constants and cross products. Float
division retains canonical IEEE infinity/NaN behavior, including zero divisors,
and zero-vector normalization likewise follows the native result rather than inventing validation.

Matrix and quaternion transformations support single values and raw-array ranges; Vector2/3 also
provide normal transforms, while Vector4 accepts explicitly typed Vector2/3/4 single-value inputs.
The bulk form takes source/destination counts, indices and a length, validates the complete range
before writing anything, and permits the same sequential aliasing behavior as the native indexed
loop. A zero-length operation accepts null zero-count arrays. No `std::vector` layout crosses the C
ABI.

## Quaternion operations

`quaternion.h` exposes both constructors, Identity, member and static arithmetic, named operator
equivalents, axis-angle/rotation-matrix/yaw-pitch-roll factories, concatenation, inversion and
normalized linear or spherical interpolation. Value-returning and out-ref native overloads share
one fallible result-plus-output C operation. Normalization and inversion preserve canonical IEEE
behavior rather than introducing a C-only zero-length rejection.

Quaternion strings follow the same exact UTF-8 count/copy contract as the vector surfaces.

## Matrix operations

`matrix.h` exposes zero and 16-component construction, Identity, the seven XNA direction and
translation properties, decomposition/determinant, exact equality/hash/string routes, all
billboard/rotation/view/projection/scale/shadow/translation/reflection/world factories and every
arithmetic or quaternion-transform operation. Matrices stay row-major and use the fixed-layout
`CNA_Matrix` value directly.

Nullable `CNA_Vector3` pointers represent the optional billboard forward vectors. Native argument
exceptions from perspective factories become `CNA_RESULT_INVALID_ARGUMENT` without changing the
output. Decomposition reports its native Boolean separately while returning scale/Identity
rotation/translation even for singular scale; singular inversion retains canonical IEEE
non-finite components.

## Plane and ray operations

`geometry.h` begins with complete Plane and Ray surfaces. Plane construction, dot products,
normalization, box/sphere/frustum classification and matrix/quaternion transforms use fixed-layout
values and fallible outputs. Ray construction, equality/hash/string and box/sphere/plane/frustum
intersection are likewise value-only operations.

Native `std::optional<float>` ray distances cross the ABI as `out_hit` plus `out_distance`. A miss
is a successful query with `CNA_FALSE` and a deterministic zero distance; both outputs are required
and validated before either is written.

## BoundingBox operations

`geometry.h` exposes complete BoundingBox construction, containment and intersection overloads,
equality/hash/string routes and factories from points, spheres or merged boxes. Ray intersections
reuse the explicit `out_hit` plus deterministic-distance convention above.

`CNA_BOUNDING_BOX_CORNER_COUNT` fixes the eight-corner contract. Corner copying always reports the
required count, rejects insufficient capacity without writing a prefix and preserves the canonical
native corner order. Point-array construction validates the complete byte count before copying;
an empty point set is invalid and leaves the caller's output unchanged. No C++ collection layout
crosses the ABI.

## BoundingSphere operations

`geometry.h` exposes complete BoundingSphere construction, matrix transformation, containment and
intersection overloads, equality/hash/string routes and factories from boxes, frusta, point arrays
or merged spheres. Matrix transformation preserves CNA's maximum-axis scale rule, including
nonuniform transforms. Ray intersections use the same explicit hit-plus-distance convention.

Point-array construction uses the checked raw-array contract: the complete element count is
validated before copying, an empty set is invalid and failures leave the output unchanged. Box,
frustum and merge factories operate entirely on fixed-layout values; no native collection or
cached frustum representation crosses the ABI.

## BoundingFrustum operations

`CNA_BoundingFrustum` keeps its defining matrix as its complete value identity. `geometry.h`
provides construction, all six derived clipping planes, containment/intersection operations,
equality/hash/string routes and all eight corners through the fixed
`CNA_BOUNDING_FRUSTUM_CORNER_COUNT` plus an atomic caller-capacity copy. Equal matrix values are
treated as the same frustum value for self-containment.

Ray intersections preserve the canonical CNA/FNA behavior: origins inside the frustum succeed at
distance zero and disjoint origins report no hit. The native boundary-origin branch is explicitly
unimplemented; the exception barrier maps it to `CNA_RESULT_NOT_SUPPORTED` and leaves both outputs
unchanged. This is a reported capability boundary, not an omitted API route.
