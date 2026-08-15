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
