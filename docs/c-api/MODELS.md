# C Model and Animation Contract

## Scope and common rules

[`models.h`](../../modules/c-api/include/CNA/C/models.h) exposes model bones, parts, meshes,
collections, aggregate models, morph targets, GPU-skinned models, SkinningData and animation
players, plus model/import metadata extensions. Handles validate registry kind, generation and
creation thread. Release each owned handle through its matching destroy route; copying a token
does not create another ownership reference. See [`HANDLES.md`](HANDLES.md).

Device-taking constructors accept independently owned devices or currently valid borrowed devices,
including pre-run loans. Same-device checks use the resource's creating-device identity. Meshes
created on a game device count as game children until their last retaining alias is released;
standalone-device meshes do not count as game children. Keep the creating device alive while its
models/resources are in use.

Matrices and vectors cross as copied PODs. Nested descriptors borrow caller arrays only during
the call; the adapter copies them into native storage. Names use validated UTF-8; model/bone/clip
construction generally refuses embedded NUL. Text count/copy routes exclude a terminator and
perform no partial string write on insufficient capacity.

Capacity-checked array copies protect the destination array on capacity refusal, while their
separate required-count outputs may change. Earlier argument/handle failures may precede count
assignment. This is not a universal guarantee that every output is unchanged on error; consult
[`ERRORS.md`](ERRORS.md) and each route's header contract.

## Effect collection cursors

ABI 0.59 adds `cna_model_effect_collection_create_enumerator` and
`cna_model_effect_enumerator_*`. An invalid source creates the native ownerless default. A bound
cursor retains its mesh/collection and pins the cached effect against destruction. Native mutation
versions make MoveNext/Reset refuse after Add/Remove, including same-count remove/add; cached
Current remains unchanged on refusal. Current returns a borrowed effect token, or a successful null
before start/after exhaustion. Keep original effect handles alive while using borrowed tokens.
Destroy cursors separately. Indexed access continues to inspect the live collection and is independent
of cursor version checks.

## Bones and collections

`CNA_ModelBoneHandle` owns a stable adapter node over a native bone. Default construction gives
an empty name, index zero, Identity transform, no parent and no children. Named construction
copies the name and preserves the signed index. Transform mutation is shared by aliases.

AddChild performs the native relationship operation and retains the child node. The C boundary
rejects self-parenting and ancestor cycles. Parent links in the adapter are weak; a separately
retained standalone child reports no parent once that parent is no longer retained. A returned
parent handle is a new owned alias. Missing parent returns false and an invalid handle.

Bone collections can be empty standalone collections, live child views or model bone snapshots.
Count/index replace iteration; exact-name Find returns false/invalid on absence. Indexed results
are owned aliases that survive release of their source collection. Contains uses adapter-node
identity, not matching names or indices. Distinct content-load mirror graphs need not share that
adapter identity even when native content is cached.

## Parts, meshes and effect collections

A mesh part holds signed vertex-count, primitive-count, start-index and vertex-offset fields.
Effect, VertexBuffer and IndexBuffer assignments are optional, retain their C resource records,
and must share one device identity. Assigned disposed resources are refused. Active part
references block typed resource destruction and generic graphics-resource disposal until those
references are removed. Content-manager unload has its own disposal authority, described below.

`CNA_ModelMeshPartTag` is a shared 64-bit C sidecar; it is separate from the native object tag used
by morph data. Part routes additionally expose primitive topology and per-PBR-slot sampler-state
copies. Part collection construction retains a snapshot of caller parts; a mesh's part collection
is a live view retaining that mesh.

Mesh creation retains its parts. A part can belong to only one live ModelMesh and cannot at the
same time belong to a SkinnedModelEXT entry. Assigned resource identities must match the mesh's
device. Meshes expose name, copied bounding sphere, optional retained parent bone and a 64-bit tag.
Part/effect collection handles retain the mesh. When its last owner disappears, surviving
independent parts switch to synchronized detached native values before the mesh is destroyed.

Automatic part effects seed a unique effect set in part order. Effect reassignment updates that
set. Explicit effect-collection Add can preserve duplicates and Remove erases the first match.
Entries retain their effects and block premature destruction/disposal. Effect getters return
the existing retained resource tokens; they do not create new release obligations.

Mesh collections are retained snapshots with count/index, exact-name Find and adapter-identity
Contains. `cna_model_mesh_draw` refuses a renderer without ThreeD support with
`CNA_RESULT_NOT_SUPPORTED` before native drawing. An aggregate model's corresponding guard
applies when it has meshes; an empty model can complete without a draw.

## Aggregate models, transforms and tags

A model is either an empty standalone value or a device-associated aggregate retaining bone nodes
and same-device meshes. The simple constructor chooses the first bone as root when present.
The extended form selects a root index and accepts either no parent array or one nullable parent
per mesh. Bone/mesh collection handles and root aliases retain their elements after the model
handle is released. Bone indices/hierarchy remain the native transform algorithm's inputs.

Local/absolute transform copies report bone count after argument/handle validation and check
capacity before writing. SetBoneTransforms requires enough inputs, copies the full bone range
before native mutation and ignores extra input matrices. Absolute composition delegates to the
native hierarchy/index algorithm.

The C model tag is a 64-bit sidecar. A separate owned-resources extension accepts an opaque
context plus release callback: both must be non-null together, or both null to clear. Replacing or
clearing releases the model's previous shared ownership reference; the callback runs when that
bundle's last native owner releases it. It must remain callable for that lifetime.

Content-written tags have their own accessors. A dictionary result is a newly owned dictionary
handle retaining native model storage. A foreign-object result returns its registered opaque
object pointer without transferring ownership. Neither result is the numeric C model tag.

## Loading and content-resource lifetime

`cna_content_manager_load_model` uses the ordinary content resolver, including supported XNB,
CNB and direct glTF/GLB paths. Asset names must be nonempty valid UTF-8 without embedded NUL.
Missing/malformed/wrong-root content maps to the content IO contract. Cache hits republish a C
mirror graph over the cached native model rather than rereading the file; see
[`CONTENT.md`](CONTENT.md) and [`CNB.md`](CNB.md) for format-specific boundaries.

The mirror exposes bones, parent links, meshes, parts and their materialized effects/buffers.
C handles protect native object storage after manager unload/destruction. They do **not** preserve
GPU usability: the manager disposes loaded effects, buffers and content-owned textures.
Drawing a materialized mesh or applying its disposed effect then returns
`CNA_RESULT_INVALID_STATE`; inspection and ordinary handle cleanup remain possible.

The model publishes effect/buffer tokens for loaded parts and releases those tokens on
`cna_model_destroy`. They are model-owned handles: do not destroy them or keep them past that
boundary. A surviving part/mesh view does not extend those published tokens' registry lifetime.
A model-owned effect also refuses independent destruction. This is separate from the lifetime
of newly owned bone, mesh, part and collection views.

## Import reports, cameras, skins and material variants

Versioned glTF report/diagnostic routes copy counters and diagnostic metadata, with separate
count/copy access to code, subject, message and detail strings. Set/add operations validate the
descriptor/identity boundary and copy the supplied report data; these operations do not import a
file or prove that caller-provided counters describe one.
Replacing a report requires its derived diagnostic/count/loss fields to be zero and starts with
no diagnostics; add diagnostic records separately, then query the recomputed totals.

Camera routes enumerate, copy/add and clear camera records. Numeric fields, projection/world
matrices and camera flags use fixed descriptors; names use separate UTF-8 accessors. The infinite
perspective projection helper is also exposed.

Skin routes enumerate names, skeleton presence and associated mesh indices. A created skeleton
handle is an owned alias retaining SkinningData, including skeletons reached from imported models.
AddSkin retains an optional caller skeleton and checks mesh indices; ClearSkins releases the
model's retained references. Format-specific skin publication is governed by content loading,
not by the existence of the getter alone.

Material variants expose names, current selection and a setter delegating native validation.
The optional model bounding-sphere query writes its availability flag and a zero sphere when
absent. These metadata routes do not imply renderer support for every imported feature.

## Morph targets

Morph descriptors copy base vertex bytes, per-target position/optional normal deltas, weights and
a weight track. Shape validation requires complete vertices, one weight per target, position
deltas covering every vertex and either empty or equally sized normal deltas.

Accepted strides come from the shared inferred-layout table, not the former 32/52/56-byte list.
This includes tangent/PBR layouts such as 48 and 68 bytes. Native blend uses that table's semantic
offsets; stride acceptance alone does not validate arbitrary caller bytes as a vertex declaration.

Tangent deltas can be set separately per target, empty or one per vertex. Triangle-index setters
require a count divisible by three, and the flat-normal recomputation flag is independently
available. These setters do not constitute complete topology or finite-component validation.

Track descriptors require canonical flags, consistent weight/tangent counts, representable finite
times and nondecreasing keyframe order. LINEAR interpolation, STEP hold, CUBICSPLINE tangents and
endpoint behavior delegate to native evaluation. Matrix/vector/weight components do not receive a
blanket finite-value check merely because times do.

Native blending adds weighted position/normal/tangent deltas, normalizes direction channels and
preserves unrelated base-vertex bytes such as UVs. Blend builds the complete result before copying
it to a sufficiently large destination. Attaching
morph data retains it and occupies the part's native object tag while leaving the numeric C tag
independent. Part weight upload requires attached morph data and a sufficiently large VertexBuffer;
native upload can still refuse an unusable resource. Destroying the original data handle does not
break a retained attachment.

## GPU-skinned models

`CNA_SkinnedModelEXTHandle` owns skeleton arrays, named animation clips and an ordered retained
part list. Skeleton arrays contain one parent index, local bind matrix and inverse global bind
matrix per bone. Bind matrices must be finite; parent indices must be -1 or precede the child. Clip/track/keyframe descriptors
are deeply copied; names are unique at construction and enumerate in lexicographic order.

ComputeBoneTransforms validates the clip name and TimeSpan-representable finite time, checks
destination capacity and delegates interpolation, clamp/loop and inverse-bind composition to
native code. The count output can be assigned on capacity refusal.

An added entry requires VertexBuffer, IndexBuffer and ModelMeshPart, with optional Texture2D.
Resources must be usable and share the model's device identity. Adapter retention replaces
native unique-pointer ownership; graphics resource guards block premature disposal/destruction.
Part aliases can be released independently because the entry retains the part object.

AttachParts requires matching bone counts and compatible device identities. With incoming parts,
parent/local-bind/inverse-bind arrays must also be equal, before replacing any same-named entry
or transferring resources. Validation refusal preserves both part lists and their retention counts.
An empty source remains a no-op after self/count checks. Joint names are not stored, so numeric
array equality does not replace the caller's common-rig requirement. Remove releases every matching entry.
Move construction/assignment transfer the sidecar and native state. Part lookup creates an owned
part alias but returns the existing optional texture token. Each entry starts with white
DiffuseColor; indexed mutation and attach/move preserve its color for the caller's draw path.

## SkinningData, AnimationPlayer and scene clips

SkinningData descriptors copy topologically ordered parents, bind/inverse-bind matrices, an
optional root-prefix array and named clips. Root-prefix count must be zero or BoneCount. Sorted
clip access, declared skeleton-root metadata and clip target-space routes accompany the arrays.

AnimationPlayer retains SkinningData after the original data handle is released. StartClip uses an
exact copied UTF-8 name and refuses a SceneNode clip; Update accepts TimeSpan-representable finite seconds and canonical
relative/absolute and loop/clamp flags. Position/current-clip queries and local/world/skin-matrix
copies expose state without native containers.

`CNA_ModelAnimationsEXTHandle` is a separate named clip set for model scene animation, with sorted
name/info access and target-space mutation. Scene-node indices and joint-palette indices are
distinct. ApplyClipToBones refuses a palette-space clip rather than posing unrelated scene bones.
ApplyBindPoseBoneTransforms exposes the native model/SkinningData helper and reports how many
applicable skinned effects it updated. See [`skinned-model-ext.md`](../skinned-model-ext.md) for
the native extension behavior.

## Validation evidence

The strict-C model suites are ModelBoneSmoke, ModelMeshPartSmoke, ModelMeshSmoke, ModelSmoke,
MorphTargetSmoke, SkinnedModelSmoke and AnimationPlayerSmoke, registered with the `CApi_` prefix.
ContentModelEffectSmoke, CnbModelSmoke and relevant content tests cover loaded-resource paths.
They exercise aliasing/retention, malformed inputs, capacity/handle/thread failures and successful
or refused renderer operations. Newer metadata/target-space cases live in their corresponding
model/animation tests.

The current audit builds and runs selected suites with SDLGPU through the private display runner,
plus C17/C++23 ABI header builds and generation/coverage/export gates. It does not establish a fresh
HEADLESS, ASan/UBSan or broad renderer run. A passing API smoke is not evidence that every glTF
feature renders on every renderer.
