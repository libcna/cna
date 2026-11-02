# Math and 3D Value C API

## Scope and representation

The C API exposes math operations, geometry, curves, color/packed conversion and vertex values.
It also provides owned curve/collection and vertex-declaration handles. The relevant headers live
under [`modules/c-api/include/CNA/C/`](../../modules/c-api/include/CNA/C/):

| Headers | Boundary |
|---|---|
| `core.h`, `math_values.h` | Fixed-layout values and stable numeric identities |
| `math.h`, `vectors.h`, `quaternion.h`, `matrix.h` | Scalar, Point/Rectangle, vector and matrix/quaternion operations |
| `geometry.h` | Plane, Ray, bounding-volume queries and factories |
| `curve.h` | CurveKey values and owned curve/collection handles |
| `color.h`, `named_colors.h`, `packed_vectors.h` | Color channels/constants and format-tagged packing |
| `vertex_values.h`, `vertex_resources.h` | Packed vertex values, declarations and binding descriptors |

C structures contain fixed-width integers, floats and other public PODs. They are converted
field by field; they are not binary aliases of CNA's polymorphic C++ objects. `CNA_Matrix`
stores `m11` through `m44` in row-major order. `CNA_BoundingFrustum` stores only its defining
matrix: native planes/corners are reconstructed when an operation consumes it.

POD values belong to the caller and have no handle lifetime or creation-thread affinity. This
does not promise allocation-free execution: text formatting, frustum reconstruction and
point-array factories can allocate native temporary storage. Curve/collection and declaration
handles do have generation, kind and creation-thread checks; see [`HANDLES.md`](HANDLES.md).

The headers freeze 32-bit enum identities for containment, plane intersection, curves and vertex/
draw vocabulary. Acceptance belongs to the consuming route. For example,
`CNA_PRIMITIVE_POINT_LIST_EXT` names a topology without promising that every renderer supports it.
Current member mapping is tracked in [`COVERAGE.md`](COVERAGE.md).

## Outputs, arrays and floating-point behavior

Native value-returning and out-ref overload pairs normally share one result-plus-output C route;
operators become named functions. Required outputs are checked before mutation. Routes using
native IEEE arithmetic can succeed with NaN or infinity: zero-vector/quaternion normalization,
zero float divisors and singular matrix inversion do not acquire a C-only finite-value check.
Explicit packed-vector validation is described below.

Text size/copy routes return UTF-8 byte counts excluding a terminator. Insufficient capacity
reports the required count without writing a partial string. Array copies similarly protect the
destination array on capacity refusal, but their separate required-count output can change.
Earlier pointer, identity, index or handle failures can occur before that count is assigned;
“always reports the count” is not a universal rule.

Bulk vector transforms take source/destination element counts, indices and a length. Invalid
pointers/windows return `CNA_RESULT_INVALID_ARGUMENT` before writes; counts not representable
as native `size_t` return `CNA_RESULT_OVERFLOW`. An empty range permits null zero-count arrays.
The loop reads and writes in increasing index order without staging the source. Overlapping
ranges therefore have sequential aliasing behavior: writing forward into a later source element
changes what a subsequent iteration reads. Use separate arrays when original-source semantics
are needed.

Hash routes expose the current native hash behavior. Vector2/3/4, Quaternion, Matrix and CurveKey
regressions cover equal hashes for both zero signs while retaining nonzero float bit patterns.
Hash results should not be used as persistent resource identities. General output/error rules are
in [`ERRORS.md`](ERRORS.md) and [`ABI_VERSIONING.md`](ABI_VERSIONING.md).

## Point, Rectangle and MathHelper

`math.h` provides initialization, named constants, arithmetic/property operations, comparisons,
hashes and text for Point and Rectangle. Integer add/subtract/multiply and Rectangle boundary
calculations use defined unsigned-bit arithmetic for unchecked 32-bit wraparound.
Point division rejects a zero divisor component with `CNA_RESULT_INVALID_ARGUMENT` and
`INT32_MIN / -1` with `CNA_RESULT_OVERFLOW`, preserving the result on either refusal.

Rectangle point containment uses half-open right/bottom edges; touching positive-area rectangles
do not intersect. No-overlap intersection returns the empty rectangle. Native empty-state
semantics test all four fields against zero rather than treating every zero-area rectangle as
the named Empty value.

MathHelper maps to `cna_math_*`, with eight `CNA_MATH_*` constants and result-plus-output
operations for interpolation, bounds, distance, angle conversion/wrapping and epsilon comparison.
Native NaN/infinity and comparison-order behavior is preserved.
`cna_math_closest_msaa_power` handles the nonnegative Int32 domain with unsigned operations:
zero/one map to zero, and larger values map to the largest power of two at or below the input.
Negative input is invalid and leaves the result unchanged.

## Vectors, Quaternion and Matrix

`vectors.h` exposes Vector2/3/4 construction, named constants, arithmetic, interpolation,
normalization, comparisons, hash and text; Vector3 includes direction constants and cross products.
Transforms accept a matrix or quaternion, with normal transforms for Vector2/3 and explicitly
typed Vector2/3/4 single-value inputs for Vector4. Bulk ranges follow the aliasing rules above.

`quaternion.h` exposes component/vector constructors, Identity, arithmetic, conjugation,
normalization/inversion, concatenation, axis-angle/matrix/yaw-pitch-roll factories and linear/
spherical interpolation. In-place conjugation and normalization are separate mutation routes.

`matrix.h` exposes zero/16-component construction, Identity, six direction properties plus
translation, determinant/decomposition, arithmetic, transpose/inversion, quaternion transforms,
comparisons, hash/text and billboard/view/projection/rotation/scale/shadow/reflection/world
factories. Nullable vector pointers represent optional billboard forward vectors. Perspective
factory argument exceptions map to `CNA_RESULT_INVALID_ARGUMENT` before result assignment.

Decomposition returns its native Boolean through `out_decomposed`, independently of the C result.
All scale/rotation/translation outputs are required and written after a completed native call.
A native false result retains scale/translation and supplies Identity rotation. Singular scale
does not invariably mean false: the zero matrix returns true with zero scale/translation and NaN
rotation, as the current XNA-compatible regression requires. Callers needing finite rotation must
check it separately.

## Geometry and bounding volumes

`geometry.h` exposes construction, comparisons, hashes, text, transforms and applicable
containment/intersection overloads for Plane, Ray, BoundingBox, BoundingSphere and BoundingFrustum.
Plane additionally has dot products and normalization. Boxes and spheres have merge and
point-array factories; spheres also have box/frustum factories and maximum-axis-scale matrix
transformation.

Optional ray results use two required outputs: `out_hit` and `out_distance`. A miss is a
successful query with false and zero. Both pointers are checked before either is written.
Box/frustum corner copies preserve native order and require eight entries, named by
`CNA_BOUNDING_BOX_CORNER_COUNT` and `CNA_BOUNDING_FRUSTUM_CORNER_COUNT`. After argument validation,
undersized destinations receive the required count and no partial corner array.
Point-array factories validate the pointer/count byte range before copying; an empty set is invalid
and leaves the result unchanged.

Frustum equality uses the defining matrix. The C adapter treats equal matrix values as the same
frustum for self-containment. Geometry predicates inherit the current measured XNA behavior;
[`xna-fna-behavior-decisions.md`](../xna-fna-behavior-decisions.md) records the reference policy
and geometry evidence.

Both `cna_ray_intersects_frustum` and `cna_bounding_frustum_intersects_ray` support outside-origin
entry clipping. A contained origin, including the boundary, returns a hit at zero; an outside
ray can hit or miss according to its plane interval. The direction is not normalized, so the
result is a ray parameter: with an identity frustum, origin `(0,0,-3)` and direction `(0,0,2)`
hit at 1.5. Parallel-plane thresholds remain native behavior. There is no current
unimplemented-origin/`NOT_SUPPORTED` branch.

## Curve values and owned collections

`CNA_CurveKey` is a 20-byte value containing position, value, tangents and continuity.
`cna_curve_key_*` provides construction, property access/mutation, clone, comparison and
equality/hash. Consumed keys must have recognized continuity. Float fields retain native behavior,
including the current position comparison result of 1 when either operand is NaN.

Independent collections use `CNA_CurveKeyCollectionHandle`. Add orders by position; replacing a
key with a different position removes/reinserts it. Equal-position adds retain insertion order.
Clone creates an independent collection. Copy-to overwrites at the destination index and reports
the collection key count, rather than the final destination length, once its argument and handle
checks pass. Insufficient capacity writes no partial key range.

`CNA_CurveHandle` supports creation, deep clone, constant-state queries, pre/post-loop modes,
evaluation and all single/both-tangent variants. Unknown loop/tangent identities and invalid key
indices are refused through the normal error contract.

Each `cna_curve_get_keys` result is a separately owned mutable collection view. It retains the
native curve after the original curve handle is destroyed; edits through any view affect the same
curve's evaluation. Destroy every returned collection handle independently. These handles do not
require a game.

## Color and packed vectors

`CNA_Color` is four RGBA bytes; native packed values use AABBGGRR. `color.h` exposes byte/int/float
and vector constructors, channel/packed representation through POD fields and packed mutation,
Vector3/4 conversion, normal/debug text, comparisons/hash, Lerp, multiplication and
non-premultiplied conversion. Constructor clamping and packed-vector truncation remain distinct
native operations. Integer premultiplication uses unchecked 32-bit product bits before division
by 255, avoiding signed-overflow UB in the adapter.

`named_colors.h` provides 141 `CNA_COLOR_*` value expressions. The strict-C suite independently
checks their packed values, and C17/C++23 header builds check their aggregate use.

The 17 `CNA_Packed*` aliases are raw 8/16/32/64-bit unsigned storage. `packed_vectors.h` selects
the format for pack/unpack and equality/inequality through `CNA_PackedVectorFormat`; the generic
carrier is `uint64_t`. Zero/raw initialization represents default/raw constructors. Narrow
formats reject high bits on unpack/comparison with `CNA_RESULT_INVALID_ARGUMENT` and
`CNA_ERROR_CATEGORY_RANGE`; undefined formats are invalid.

Integer-backed packing rejects non-finite components it consumes; unused components are ignored.
Alpha8 consumes W, two-component formats consume X/Y, Bgr565 consumes X/Y/Z and four-component
formats consume all channels. The three half formats and `cna_half_*` helpers delegate to native
XNA packed-half conversion. Exponent 31 is finite: `0x7c00`, `0x7e00` and `0x7fff` expand to
65536, 98304 and 131008. Packing binary32 magnitude bits above `0x47ffefff`, including infinity
and NaN, saturates to `sign | 0x7fff`. Signed zero is preserved. This representation differs
from IEEE binary16; see the [packed half conversion decision](../xna-fna-behavior-decisions.md).
The helpers accept a float or its binary32 bit pattern and expose the reverse conversion to float.

## Vertex values, declarations and bindings

`vertex_values.h` defines seven built-in `VertexPosition*` PODs matching packed GPU streams,
including byte-packed color/blend indices. Their layouts do not match the native polymorphic
objects. `CNA_VertexType` selects the active `CNA_VertexValue` union member for default,
equality/inequality, hash and text routes. Parameterized values use aggregate initialization.
Every built-in default is all-zero, including transparent packed-zero color for
VertexPositionColor.

Stride and element-copy routes expose built-in declarations without a native interface pointer.
`CNA_VertexElement` is a 16-byte offset/format/usage/usage-index value. Its value operations
validate format and usage identities; structural declaration checks are stricter.

`vertex_resources.h` exposes standalone declarations through owned
`CNA_VertexDeclarationHandle` values. The empty extension has zero stride/no elements.
Array creation requires at least one element and computes the stride or accepts an explicit
positive stride. The adapter checks identities, nonnegative offsets/usage indices and computed
Int32 stride overflow. Native validation additionally enforces four-byte stride/offset alignment,
elements within stride, no overlap and unique usage/index pairs. Invalid creation leaves the
output handle invalid. Queries and destruction retain creation-thread affinity; declaration
destruction calls native Dispose and releases the handle.

`CNA_VertexBufferBinding` is a 16-byte buffer-token/vertex-offset/instance-frequency descriptor.
`{CNA_INVALID_HANDLE, 0, 0}` is the default. `cna_vertex_buffer_binding_init` checks only a
nonzero token and nonnegative integers; it does not look up or retain a buffer.
`cna_graphics_device_set_vertex_buffers` performs live resource, disposal and device checks
when consuming bindings. Vertex buffers and submission already exist; see
[`VERTEX_BUFFERS.md`](VERTEX_BUFFERS.md) and [`GRAPHICS_DEVICE.md`](GRAPHICS_DEVICE.md).

## Validation evidence

The focused C suites are MathValuesSmoke, MathValueHashSmoke, VectorSmoke, QuaternionSmoke,
MatrixSmoke, GeometrySmoke, CurveSmoke, ColorSmoke, PackedVectorSmoke, VertexValueSmoke,
VertexResourceSmoke and VertexBufferSmoke, registered with the `CApi_` prefix.
They cover the documented value/handle boundary, including IEEE cases, sequential transform
aliasing, retained curve views, exact packed/default values, frustum entry rays, malformed inputs
and capacity/handle/thread failures. C17/C++23 header builds assert ABI identities/layouts.
The current audit uses the private SDLGPU runner; this is not a fresh cross-renderer or sanitizer
claim. Renderer draw support remains separate from CPU value operations.
