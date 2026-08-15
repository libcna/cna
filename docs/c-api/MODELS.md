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
