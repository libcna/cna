# C Model and Animation Contract

## Model bones and collections

`CNA_ModelBoneHandle` owns a stable model-bone node. Default creation preserves an empty name,
index zero, identity transform, no parent and no children. Named creation copies validated UTF-8
and preserves the signed native index. Names use exact byte-count/copy operations, and transforms
cross the ABI as copied row-major `CNA_Matrix` values.

Adding a child invokes the native relationship operation and retains the child node. Parent
queries return a new owned stable view; absence is a successful false result with an invalid
handle. The C boundary rejects self-parenting and ancestor cycles because raw-pointer cycles would
otherwise defeat deterministic ownership. Parent metadata is weak: when no parent or child-view
handle retains the parent, an independently retained child safely reports no parent instead of
exposing the native dangling pointer that standalone C++ misuse could create.

`CNA_ModelBoneCollectionHandle` owns either an empty standalone collection or a live child view.
Count and indexed access replace native iteration; exact UTF-8 find replaces the throwing name
indexer and `TryGetValue`, returning a successful false result when absent. Returned bone views
share mutation and remain valid after their source collection handle is released. Contains uses
native object identity rather than matching names or indices.

`ModelBoneSmoke.c` covers both constructors, exact UTF-8 names, capacity atomicity, indices,
identity and mutated transforms, optional parents, live collection growth, index/name/contains
operations, multiple aliases, transitive hierarchy lifetime, expired parents, cycle refusal and
invalid, stale, wrong-kind and wrong-thread calls. The same strict-C source runs under HEADLESS and
SDL_RENDERER plus focused ASan+UBSan; C17/C++23 assertions freeze both handle widths.

## Model mesh parts and collections

`CNA_ModelMeshPartHandle` owns stable mutable mesh-part state. Both the default and parameterized
native constructors are represented; the four geometry fields preserve signed 32-bit values
verbatim. Effect, VertexBuffer and IndexBuffer assignments are optional and must share one game
and graphics device. Each non-null assignment retains the C resource and blocks both its typed
destroy operation and generic graphics-resource disposal until every referencing part is cleared
or released.

The native `System::Object*` tag does not cross the ABI. A part instead carries a C-owned opaque
`CNA_ModelMeshPartTag` value with fixed 64-bit storage. This sidecar value is shared by all handles
that refer to the same part.

`CNA_ModelMeshPartCollectionHandle` can own a snapshot created from caller handles. The snapshot
retains the shared part objects, so indexed handles remain valid and share mutation even after an
original part handle or the collection is released. Count/index operations replace all native
iterator types; the same representation is reserved for ModelMesh-owned live views added by the
next model slice.

`ModelMeshPartSmoke.c` covers both constructors, all scalar getters/setters, opaque tags,
optional resource state, snapshot count/index and alias lifetime, invalid arrays, stale,
wrong-kind and wrong-thread handles. It also proves that retained effects and supported buffers
cannot be disposed or destroyed early. HEADLESS and SDL_RENDERER run the same strict-C source;
backend buffer refusal remains an explicit `NOT_SUPPORTED` result.

## Model meshes and aggregate collections

`CNA_ModelMeshHandle` is an owned game child created from a callback-scoped graphics device and
an array of retained mesh parts. Unnamed and copied UTF-8 named constructors are distinct. A part
can belong to only one live mesh; its assigned graphics resources must match that mesh's device.
Bounding spheres cross as copied `CNA_BoundingSphere` values, parent bones are optional retained
stable nodes, and the native `System::Object*` tag is replaced by an opaque 64-bit C tag.

Part and effect properties return owned live collection views that retain the mesh. Part aliases
share mutation. If all mesh handles/views are released while an independent part handle remains,
the adapter switches that part to a synchronized detached native value before destroying the
mesh, so later part mutation never follows the native dangling parent pointer. Meshes count as
game children until their last direct or collection alias is released.

The live `CNA_ModelEffectCollectionHandle` mirrors native identity behavior. Part effect changes
maintain the unique automatic set; explicit Add preserves duplicates and Remove erases the first
match. Every entry retains its same-device Effect and blocks destroy/dispose. Count/index replace
native iterators. `CNA_ModelMeshCollectionHandle` owns a retained snapshot with count/index,
exact UTF-8 find and object-identity contains operations; its returned handles share mesh state.

`cna_model_mesh_draw` calls native `ModelMesh::Draw` only when the device reports 3D support and
otherwise returns `CNA_RESULT_NOT_SUPPORTED` before native mutation. `ModelMeshSmoke.c` covers
both constructors, all properties, both collections, duplicate effects, transitive lifetime,
safe surviving parts, draw capability behavior and error/thread paths under HEADLESS and
SDL_RENDERER, plus a focused ASan+UBSan run.

## Top-level models

`CNA_ModelHandle` owns either an empty standalone model or a device-associated aggregate that
retains its stable bone nodes and same-device meshes. The simple constructor selects the first
bone as root. The extended constructor accepts an arbitrary root index plus either no mesh-parent
array or one nullable parent per mesh. Bone and mesh properties return owned immutable collection
views whose aliases keep their elements alive after the original handles or model are released.

The native `System::Object*` tag becomes a fixed 64-bit C tag. The CNA extension taking
`std::shared_ptr<void>` is represented without a C++ ABI leak by an opaque C context and required
release callback. Replacement, explicit clearing and final model destruction synchronously
release exactly one retained context.

Bulk transform APIs first report the bone count. Local and absolute copies are atomic on
insufficient capacity, and local input is fully copied before mutating native bones. Absolute
composition delegates to the native parent/index algorithm. Draw accepts copied world/view/
projection matrices and returns `CNA_RESULT_NOT_SUPPORTED` before native mutation when a non-empty
model targets a renderer without 3D support.

`ModelSmoke.c` covers all constructors and properties, nullable root/parents, retained collections,
owned-context releases, all transform routes and capacity failures, transitive lifetime, renderer
draw behavior and thread/handle errors under HEADLESS and SDL_RENDERER plus ASan+UBSan.
