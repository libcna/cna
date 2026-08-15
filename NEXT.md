# NEXT.md

## C BINDING / C ABI — CBIND-035 CLOSED (2026-08-15)

> `plan_binding.md` is the single implementation plan for CNA's native C API. It was derived from
> the read-only `analysis_binding.md` and `analysis_binding_sharp_runtime.md` design analyses.
> The owner authorized implementation and requires eventual coverage of the **entire public CNA
> API** through C-native mappings. `CBIND-001`–`034`, `CBIND-035A`–`035E` and
> `CBIND-035F1`–`035F7` and `CBIND-035G` are complete, closing parent `CBIND-035`:
> `docs/c-api/` defines the
> contract and the opt-in `modules/c-api/` builds a C17 `libcna_c_api` with public `cna_*` exports,
> the error/handle substrate and a C-owned `Game` lifecycle slice tested under HEADLESS and
> `SDL_RENDERER`. The work deliberately contains no C#, .NET, JavaScript, Rust, Python, Java, Zig,
> Go, Swift, or other
> language-binding work.

> `CBIND-021` passes the same strict-C lifecycle source against `SDL_RENDERER` using SDL's dummy
> video driver and software renderer. A single clean serial build proved that the earlier missing
> archive report came from overlapping verification builds, not a CNA archive defect. B4's
> callback-scoped graphics-device handle and canonical renderer capability queries are now
> complete. Owned Color `Texture2D` handles now support versioned create/info, full-level bulk
> RGBA8 upload/readback, explicit destroy and enforced child-before-game teardown. The C
> `SpriteBatch` slice adds all native sort/effect identities, a single-transition POD command array,
> fixed default XNA state, retained texture lifetime and cancel-safe destruction. The keyboard C
> API now captures a fresh 256-key POD snapshot, maps all 160 canonical `Keys` identities and keeps
> repeated key/count/copy queries local to that value. Full backbuffer RGBA8 readback now proves
> honest `NOT_SUPPORTED` behavior under HEADLESS and exact uploaded texture/SpriteBatch/clear
> pixels under SDL_RENDERER. `FEATURE_MATRIX.md` now freezes the exact initial supported surface,
> backend evidence, ownership/error rules and explicit omissions. B5 now owns a native content
> manager, controls its UTF-8 root/cache and loads Color Texture2D assets into independent C-owned
> handles. Expanded input now adds fresh mouse, four-player GamePad and fixed-capacity touch POD
> snapshots, exact three-mode dead-zone behavior, all current button bits and thread-independent
> local helpers, tested from strict C under HEADLESS and SDL_RENDERER. Minimal audio now owns
> copied PCM16LE effects and controllable instances with explicit creation-thread and
> instance-before-effect-before-game shutdown rules. Content now proves a real valid-UTF-8 fixture
> path, and an isolated invalid-audio-driver process proves repeatable unavailable capability
> snapshots, `NOT_SUPPORTED` creation and clean shutdown. Stable C queries now cover renderer and
> all canonical graphics features, touch availability and real native audio playback availability;
> applications need not infer them from backend/platform names. `CBIND-033` now establishes the
> complete public-header baseline: a Doxygen-backed generator inventories 414 public headers and
> 6,415 public/protected declarations, excluding 95 explicit `Internal`/`Detail` headers. Every
> symbol has a stable ID, C mapping, test obligation, status and owner task. `CBIND-034` adds
> complete fixed-layout Blend/DepthStencil/Rasterizer/Sampler descriptors and presets, device and
> sampler state round-trips, explicit-state SpriteBatch Begin, display/adapter/presentation
> snapshots and queries, owned RenderTarget2D/RenderTargetCube handles and copied-glyph SpriteFont
> handles with enforced texture retention. The same strict-C test passes against HEADLESS and
> SDL_RENDERER, including real SDL 2D target binding and honest unavailable target/state paths.
> CBIND-035 is split into independently reviewable CBIND-035A–G slices. CBIND-035A now freezes the
> C layouts for Point, Vector4, Quaternion, Matrix, Plane, Ray, bounding volumes, all 17
> PackedVector raw values and VertexElement, plus containment/curve/buffer/draw/vertex enum
> identities. Strict C17 and C++23 assertions cover sizes, offsets and ordinals; no constructor,
> numeric operation or resource behavior is claimed by this layout-only foundation. The inventory
> is now 983 implemented, 21 partial, 5,341 planned and 70 explicitly deleted/not applicable.
> Deterministic `--check` detects drift; wiring it into CI remains reserved for CBIND-043.
> CBIND-035B is further split into B1–B7. B1 now maps every Point and Rectangle constructor,
> constant, property, overload, operator, hash and exact UTF-8 count/copy string through 37 C
> operations. Its defined unsigned-bit calculations preserve unchecked 32-bit behavior without
> signed-overflow UB; division failures preserve output. The strict-C suite passes under HEADLESS,
> SDL_RENDERER and ASan+UBSan. The inventory is now 1,027 implemented, 21 partial, 5,297 planned
> and 70 N/A. B2 is split into MathHelper plus Vector2/3/4 slices. B2a now exposes all eight exact
> scalar constants and all 15 MathHelper operations, including canonical NaN/infinity/epsilon
> behavior and a defined full-positive-int32 MSAA-power calculation. The inventory is now 1,051
> implemented, 21 partial, 5,273 planned and 70 N/A. B2b now maps all 75 remaining Vector2 rows
> through 41 C operations, including all constants/constructors/math/operators plus exact strings
> and preflight-validated matrix/quaternion/normal bulk ranges. Every entry point is called by the
> strict-C suite under HEADLESS, SDL_RENDERER and ASan+UBSan. The inventory is now 1,126
> implemented, 21 partial, 5,198 planned and 70 N/A. B2c now maps all 87 remaining Vector3 rows
> through 50 C operations, including direction constants, cross products and the full
> matrix/quaternion/normal single and bulk transform families. Every entry point is covered by
> strict-C normal, IEEE, alias, string, null and range-atomicity cases. The inventory is now 1,213
> implemented, 21 partial, 5,111 planned and 70 N/A. B2d now maps all 81 remaining Vector4 rows
> through 46 C operations, including Vector2/3/4 input transforms and validated Vector4 bulk
> ranges. All entry points have strict-C normal, IEEE, alias, string, null and range-atomicity
> coverage. The complete B2 MathHelper/Vector2/3/4 slice is closed; the inventory is now 1,294
> implemented, 21 partial, 5,030 planned and 70 N/A. B3 is split into Quaternion and Matrix slices;
> B3a now maps all 50 remaining Quaternion rows through 28 C operations, including factories,
> concatenation, inverse, normalized/spherical interpolation and exact strings. Every entry point
> has strict-C normal, IEEE, alias and failure coverage. The inventory is now 1,344 implemented,
> 21 partial, 4,980 planned and 70 N/A. B3b now maps all 98 remaining Matrix rows through 57 C
> operations: construction, seven direction/translation properties, decomposition, determinant,
> every factory and complete operator math. Strict-C tests cover row-major layout, optional
> billboard inputs, valid and singular decomposition/inversion, projection rejection without
> output mutation, exact strings and all entry points. Parent B3 is closed; the inventory is now
> 1,442 implemented, 21 partial, 4,882 planned and 70 N/A. B4 is split into Plane/Ray, BoundingBox,
> BoundingSphere and BoundingFrustum slices. B4a maps all 42 remaining Plane/Ray rows through 31 C
> operations with explicit hit-plus-distance optional intersections. Every entry point has strict-C
> classification, transform, hit/miss, exact-string and failure coverage. The inventory is now
> 1,484 implemented, 21 partial, 4,840 planned and 70 N/A. B4b maps all 31 remaining BoundingBox
> rows through one corner-count constant and 20 C operations. Every entry point has strict-C
> containment, intersection, canonical-corner, capacity-atomicity, factory, string and failure
> coverage. The inventory is now 1,515 implemented, 21 partial, 4,809 planned and 70 N/A.
> B4c maps all 31 remaining BoundingSphere rows through 21 C operations. Every entry point has
> strict-C transform, containment, intersection, hit/miss, factory, merge, exact-string and failure
> coverage. The inventory is now 1,546 implemented, 21 partial, 4,778 planned and 70 N/A.
> B4d maps all 31 remaining BoundingFrustum rows through one corner-count constant and 22 C
> operations, including six planes, atomic corner copies and an explicit `NOT_SUPPORTED` result for
> the canonical unimplemented boundary-origin ray case. Every entry point has strict-C normal and
> failure coverage. Parent B4 is closed; the inventory is now 1,577 implemented, 21 partial, 4,747
> planned and 70 N/A. B5 is split into CurveKey, CurveKeyCollection and Curve/evaluation slices.
> B5a maps all 19 CurveKey rows through a fixed 20-byte POD and 17 C operations with complete ABI,
> property, construction, comparison, equality/hash, invalid-enum and null-output coverage. The
> inventory is now 1,596 implemented, 21 partial, 4,728 planned and 70 N/A. CBIND-035B5b
> maps all 26 CurveKeyCollection rows through a validated owned handle and 14 C operations.
> Ordering/repositioning, clone independence, count/index/copy, atomic capacity failure and
> invalid/stale/wrong-thread handles are covered. The inventory is now 1,622 implemented, 21
> partial, 4,702 planned and 70 N/A. CBIND-035B5c maps all 15 Curve rows through 14 C operations,
> including retained mutable key views, both loop properties, deep clone, evaluation and every
> tangent overload. All five loop modes, clone/view lifetime and invalid/stale/wrong-thread cases
> are covered. Parent B5 is closed; the inventory is now 1,637 implemented, 21 partial, 4,687
> planned and 70 N/A. CBIND-035B6 is split into value operations and named constants. B6a maps
> all 25 previously planned non-constant Color rows through the existing four-byte POD, direct
> channels and 24 C operations. All constructors, packed values, conversions, exact/debug strings,
> interpolation, premultiplication, multiplication, equality/hash and packed-vector mutation are
> covered. The inventory is now 1,662 implemented, 21 partial, 4,662 planned and 70 N/A.
> CBIND-035B6b maps all 141 named colors to directly usable C17/C++23 `CNA_COLOR_*` value
> expressions. The strict-C test independently checks every expression against its canonical
> AABBGGRR packed literal, and public headers compile in both language modes. Parent B6 is closed;
> the inventory is now 1,803 implemented, 21 partial, 4,521 planned and 70 N/A. CBIND-035B7 maps
> all 132 remaining concrete PackedVector, HalfTypeHelper and IPackedVector rows through 17 stable
> format identities, four generic pack/unpack/equality operations and three half conversions.
> Exact packed bits for every format, half NaN/infinity/signed-zero behavior, storage-width and
> output-atomicity failures are covered in strict C, with C/C++ identity assertions. Parent
> CBIND-035B is closed; the inventory is now 1,935 implemented, 21 partial, 4,389 planned and 70
> N/A. CBIND-035C is partitioned into seven dependency-ordered slices totaling exactly 402 rows.
> CBIND-035C1 maps its first 104 rows: all seven built-in `VertexPosition*` values, the remaining
> `VertexElement` operations and the `IVertexType` declaration route now use fixed-layout PODs,
> stable type identities and generic default/equality/hash/string/stride/element-copy operations.
> Strict-C tests cover exact native strings and every canonical packed GPU declaration, while
> C17/C++23 assertions freeze all layouts. The inventory is now 2,039 implemented, 21 partial,
> 4,285 planned and 70 N/A. CBIND-035C2 maps all 14 VertexDeclaration and VertexBufferBinding
> rows through standalone owned declaration handles, copied arrays and a fixed 16-byte binding
> descriptor. Empty/computed/explicit construction, exact type names, atomic copies and
> wrong-kind/stale/wrong-thread lifetime behavior are strict-C tested. The inventory is now 2,053
> implemented, 21 partial, 4,271 planned and 70 N/A. CBIND-035C3 then maps all 21
> GraphicsResource rows through generic validated operations for callback-scoped device identity,
> disposal state/events, exact UTF-8 Name/ToString and a C-owned opaque tag. Standalone and
> device-owned resources, generic and typed destruction, event lifetime, UTF-8/capacity failures,
> stale/wrong-kind/wrong-thread handles and registry tag reset are covered. The inventory is now
> 2,074 implemented, 21 partial, 4,250 planned and 70 N/A. CBIND-035C4 then completes all 134
> unfinished Texture/Texture2D rows and upgrades the two inherited partial Texture properties.
> Standalone and game-owned factories, all 18 typed full/mip/rectangle transfer representations,
> common/2D/storage properties and PNG/JPEG memory/file routes are strict-C tested under HEADLESS
> and SDL_RENDERER; SDL's native mip-upload limitation is an explicit `NOT_SUPPORTED` result and
> the focused ASan+UBSan run is clean. The inventory is now 2,210 implemented, 19 partial, 4,116
> planned and 70 N/A. CBIND-035C5 then maps all 40 Texture3D/TextureCube rows through owned
> handles, versioned dimension/region descriptors, complete Color box/face/mip transfer, raw
> Texture3D upload and copied-memory DDS decode. HEADLESS and SDL_RENDERER prove exact capability
> refusal, six-face validation, common Texture/GraphicsResource behavior and lifecycle. The
> inventory is now 2,250 implemented, 19 partial, 4,076 planned and 70 N/A.
>
> CBIND-035C6 and CBIND-035C7 then close parent CBIND-035C with owned VertexBuffer and IndexBuffer
> handles, copied declarations, both index widths, caller-window transfers and ContentLost
> registration. CBIND-035D1–D9 close parent CBIND-035D: effect-parameter identities, annotations,
> parameters, techniques/passes, the Effect/ShaderEffect/EffectMaterial/SpriteEffect lifecycle and
> the BasicEffect, AlphaTest/DualTexture/EnvironmentMap, Skinned, ColorMatrix and PBR stock
> families, all through owned game-child effect handles with retained texture slots and bounded
> bone palettes. CBIND-035E1–E7 close parent CBIND-035E: model bones, mesh parts, meshes, Model
> aggregates, morph-target extension data, SkinnedModelEXT and the SkinningData/AnimationPlayer
> pair, through stable handles, deep-copied descriptors and deterministic count/copy transfers.
> CBIND-035F is partitioned into seven slices; CBIND-035F1 maps all 49 Viewport, ClearOptions,
> GraphicsDeviceStatus, Unsupported3DGraphicsCallBehavior and SpriteEffects-operator rows through a
> fixed 24-byte viewport POD with complete construction/property/transform/string operations plus
> fixed-width identities asserted against their native ordinals at the adapter boundary. The
> inventory is now 3,215 implemented, 19 partial, 3,111 planned and 70 N/A. CBIND-035F2 then maps
> all 51 device lifetime, state, event, event-args, service and device-exception rows through the
> borrowed device handle, owned subscriptions with fixed payload structures and a shared
> exception-firewall conversion of the three canonical device exceptions. The inventory is now
> 3,259 implemented, 23 partial, 3,060 planned and 73 N/A. CBIND-035F3 then maps all 8
> TextureCollection and device texture-collection rows through stage-addressed slot reads, binds
> and unbinds, with a slot-count assertion against the native constant and no native collection
> reference crossing the ABI. The inventory is now 3,267 implemented, 23 partial, 3,052 planned and
> 73 N/A. CBIND-035F4 then maps all 21 clear, present, reset, back-buffer-window and
> buffer-binding rows through versioned descriptors, a nullable adapter index and caller-owned
> binding arrays, deciding readback capacity in C instead of surfacing a generic native failure.
> The inventory is now 3,288 implemented, 23 partial, 3,031 planned and 73 N/A. CBIND-035F5 then
> maps all 49 draw-submission and device-extension rows: two descriptor-driven calls replace the
> twenty-nine canonical user-primitive overloads, built-in vertex sources are converted because the
> native structures embed a polymorphic Color, and every draw route refuses a backend without 3D
> support as `NOT_SUPPORTED`. No planned GraphicsDevice.hpp row remains. The inventory is now
> 3,337 implemented, 23 partial, 2,982 planned and 73 N/A. CBIND-035F6 then maps all 21 SpriteBatch
> text/mesh and OcclusionQuery rows: one versioned text command covers every canonical DrawString
> overload, mesh colors and positions are converted because the native Color carries a vtable, and
> the owned query handle is capability-gated and behaves as an ordinary graphics resource. The
> inventory is now 3,358 implemented, 23 partial, 2,961 planned and 73 N/A. CBIND-035F7 then maps
> all 118 `graphics-ext` rows: seven identities at their native ordinals, two settings-bag PODs
> with canonical-default initializers, and CRT/depth/ASCII post-process effects. Because the
> extended layer is an opt-in build option, every declaration exists in every build and the effect
> routes report `NOT_SUPPORTED` without it, so the exported ABI never changes shape. The inventory
> is now 3,476 implemented, 23 partial, 2,843 planned and 73 N/A and **no planned CBIND-035 row
> remains**. CBIND-035G then closes the parent with `Draw3DSmoke.c`: deterministic refusal on a
> backend without the 3D capability, and observable pixel change through converted user primitives,
> indexed user primitives, buffered indexed geometry and a full owned Model draw on the CPU-raster
> SOFTWARE backend. Pixel readback is treated as a capability separate from 3D, so HEADLESS draws
> without claiming pixel evidence. Adding the third tree exposed three suites that branched on
> renderer *identity* instead of capability; they now probe actual behavior, which also turned
> SOFTWARE's real cube storage, mip upload and exact drawn texels into new positive evidence. All
> three trees run the same 47 tests green.
>
> CBIND-036 is partitioned into five dependency-ordered slices totaling exactly 406 rows.
> CBIND-036A closes the first 42: the whole `storage` module. Storage is independent of the
> graphics device and of the `Game` lifecycle, so its handles are not game children; ownership
> instead nests device -> container -> stream, and each level refuses destruction while it
> still has a live child. The canonical fake-async `BeginShowSelector`/`EndShowSelector` and
> `BeginOpenContainer`/`EndOpenContainer` pairs complete before `Begin` returns, so each maps
> to one synchronous C call that still invokes the completion callback -- no `IAsyncResult`
> or invented operation handle. `System::IO::Stream` stays behind the adapter: a C stream
> handle exposes read/write/seek/position/length/set-length/flush/capability/close only, and
> wider-than-Int32 counts are refused instead of truncated. Directory and file listings are a
> count plus an indexed copy because the canonical getters rebuild an unordered vector per
> call. Three boundary conversions were added centrally while doing it --
> `filesystem_error` and `System::IO::IOException` to `CNA_RESULT_IO`, and
> `StorageDeviceNotConnectedException` to `CNA_RESULT_INVALID_STATE` -- all three proven in
> the adapter test rather than inferred. All three trees run the same 48 tests green.
>
> CBIND-036B is split in two at the boundary between the manager a C consumer drives and the XNB
> reader pipeline only C++ type readers can join. CBIND-036B1 closes the manager half (40 rows):
> resolved asset path and normalized cache key, built-in loader registration, service-provider
> presence, graphics-device get/set validated by borrowed-handle re-validation plus pointer
> identity, the manifest and `.xnb` reader-usage snapshots as fixed PODs plus count/indexed copy,
> and typed Texture2D, TextureCube and SoundEffect load routes returning independently owned
> handles. Two boundaries are recorded rather than papered over: `System::IServiceProvider` may
> never cross the ABI, so the two service-provider constructors and the property stay `partial` with
> presence-only observability; and `Load<T>`/`RegisterTypeReader<T>`/`RegisterCnjLoader<T>` are
> `not-applicable` because C cannot name an arbitrary C++ type — the C API adds a typed route per
> asset type instead of an invented untyped registration. The manifest scans a whole directory tree,
> so `ContentSmoke.c` builds its own content root through the storage API rather than pointing the
> root at the working directory.
>
> CBIND-036B2 then closes the reader half and with it parent CBIND-036B. An owned
> `CNA_ContentReaderHandle` is built over an owned storage stream plus an optional manager handle,
> both borrowed through new adapter records so neither `System::IO::Stream` nor `ContentManager`
> crosses the ABI. Two lifetime rules come straight from the canonical types: the borrow blocks
> closing the stream, and destroying the reader closes it, because the canonical reader derives
> from a binary reader that owns its stream by default. Type erasure is where the mapping stops,
> and the inventory says so instead of inventing an untyped operation: the two untyped read routes
> are `partial` because a type-erased C++ object has no C representation, and every typed reader
> template, `LooseFileContentTypeReader<T>` and factory registration is `not-applicable`.
> `ContentLoadException` gained a central boundary conversion to `CNA_RESULT_IO`, which is what
> made the reader's own limit and truncation failures land correctly. `ContentReaderSmoke.c` builds
> a compiled-asset fixture through the storage API and asserts the whole protocol byte-exactly. The
> inventory is now 3,582 implemented, 28 partial, 2,704 planned and 101 N/A, with no planned
> `content` or `storage` row left; all three trees are green at 49/49.
>
> CBIND-036C then maps the 98 network identity, value and packet rows. Four things are worth
> keeping: `SendDataOptions` is exposed as discrete identities, not a bit set, because the canonical
> enumeration is marked as flags but uses plain sequential values; the canonical packet color
> asymmetry (writer emits four bytes, reader consumes four floats) is preserved and proved in both
> directions rather than corrected; the canonical `NetworkSessionProperties` forwards `Insert` and
> `RemoveAt` to its backing vector with no bounds check and its enumerator dereferences before its
> first advance, so the C routes decide both cases themselves instead of passing undefined behavior
> through; and a join failure's join error -- the one canonical payload a diagnostic message cannot
> carry -- is recorded per thread by the firewall and read back through
> `cna_net_get_last_join_error`, cleared by any later failure so it can never go stale. Two `_ext`
> routes move packet bytes, because the canonical API hands buffers straight to send/receive and
> never exposes them, which would otherwise leave the whole packet surface untestable. The inventory
> is now 3,665 implemented, 29 partial, 2,606 planned and 115 N/A; all three trees are green at
> 50/50.
>
> CBIND-036D then maps gamers, machines and the event-argument types, and its slice boundary needed
> one correction: `LocalNetworkGamer` moved to CBIND-036E, because its receive and send paths
> dereference the owning session and so it cannot exist before sessions do — the 65/104 split became
> 47/122. The four CNA extension setters on a gamer keep an `_ext` suffix so a consumer can see
> which state the canonical API otherwise leaves permanently fixed. A machine's roster is a count
> plus borrowed views that block the machine's release, and the canonical always-throwing
> roster-removal placeholder is reported as `NOT_SUPPORTED` rather than faked. The seven
> event-argument types become fixed descriptions with validating initializers, delivered by value
> like every other C API event payload. The inventory is now 3,711 implemented, 29 partial, 2,559
> planned and 116 N/A; all three trees stay green at 50/50.
>
> CBIND-036E is partitioned into five slices by what each part needs to exist: discovered sessions
> first, then the session object's own state, its ten events, the creation/discovery/join surfaces
> whose fake-async pairs need the session object to exist, and the local gamer last. CBIND-036E1
> closes the first 17 rows. Two canonical limits shape it: the quality-of-service type accepts only
> a round-trip sample, so that is all a C caller can supply; and a collection element is copied out
> rather than aliased, so it survives the collection it came from — which is also how the canonical
> factory treats its own input. The inventory is now 3,728 implemented, 29 partial, 2,542 planned
> and 116 N/A; all three trees stay green at 50/50. CBIND-036E2 (session identity, state and gamer
> management, 57 rows) is next.

## ELEVEN-LANE RENDERER INTEGRATION ON `11branches` (2026-08-11)

> Ten frozen feature lanes integrated one at a time into `11branches`, which started at exactly the
> public `develop` head `fb3728267`. `develop` itself is untouched, no source lane was moved,
> rebased or squashed, and every lane is merged by a real signed `--no-ff` merge commit of its
> exact recorded SHA.
>
> | # | Lane | Frozen SHA | Scope |
> |---:|---|---|---|
> | 1 | `feature/bigcommit` | `e52b6a02b` | docs/history analysis (commit.md) |
> | 2 | `feature/gltf` | `37f461f72` | glTF correctness |
> | 3 | `feature/direct2dcomplete` | `fee726818` | DIRECT2D completion |
> | 4 | `feature/asciieffect` | `b51d894b1` | ASCII renderer -> post-process effect |
> | 5 | `feature/opengles2` | `a43adcf90` | OPENGLES2 GL profile |
> | 6 | `feature/blend2d` | `4ee952cba` | BLEND2D renderer |
> | 7 | `feature/fna3d` | `0e804a064` | FNA3D renderer |
> | 8 | `feature/svgdom` | `99c8040be` | SVG_DOM renderer |
> | 9 | `feature/openvg` | `20b840116` | OPENVG renderer |
> | 10 | `feature/portablegl` | `14b2cc5b4` | PORTABLEGL renderer |
>
> **Merge order was derived, not taken from the request's list order.** The three lanes with no
> renderer-registry contact went first (`bigcommit` has zero path overlap with anything; `gltf` and
> `direct2dcomplete` share one file each and change no identity), so the branch was exercised
> before any registry work. `asciieffect` — the only subtractive/structural lane — went next, so the
> "ASCII effect exists, ASCII renderer does not" architecture was established once and then
> *defended* at each of the six following merges, rather than being unpicked out of six
> already-reconciled registry lines at the end. The six additive renderer lanes followed, least
> entangled first (`opengles2` adds no implementation family and never touches
> `modules/CMakeLists.txt`), and `portablegl` went last because its `SetRenderTarget` refactor
> **supersedes** the inline guard `openvg` adds to the same method.
>
> **Renderer identity arithmetic (mechanically counted, not asserted):**
>
>     41  public identities at the 2026-08-10 pre-expansion promotion
>     -1  ASCII (renderer identity removed; logic migrated to CNA::Graphics::AsciiPostProcessEffect)
>     +6  OPENGLES2, BLEND2D, FNA3D, SVG_DOM, OPENVG, PORTABLEGL
>     ==
>     46  public identities   (scripts/check_renderer_identities.py: OK, both registries agree)
>
> Implementation families: 38 -> 42 (ascii removed; blend2d, fna3d, svg-dom, openvg, portablegl
> added; OPENGLES2 adds none — EasyGL now backs five GL profiles instead of four).
>
> **Cross-lane defects fixed as integration consequences** (none of them wrong on their own lane):
> the enum/`STRINGS`/docstring/selection-guard surfaces in `cmake/RendererSelection.cmake` were
> regenerated from the authoritative identity table because no single lane's snapshot carried all
> 46; `GraphicsRendererTypeTests.cpp` needed its `static_assert` at 46 and the three name arms
> (`Fna3d`/`SvgDom`/`OpenVg`) that later lanes' snapshots did not carry; the `SVG_DOM`, `OPENVG` and
> `PORTABLEGL` `option()`/enabled-arm/selector branches were re-grafted after list-line conflict
> resolution dropped them; and the shared instancing suites now gate on **both**
> `GraphicsCapability::ThreeD` (OpenVG's requirement) and `GraphicsCapability::Instancing`
> (OPENGLES2's), since taking either lane's side alone silently un-skipped the other renderer.
>
> `commit.md` (the `bigcommit` lane) asked for one check its shallow clone could not run; it was run
> here against a full clone and the document now records the verified result — the build-artifact
> bloat is real and reachable from `develop`, but under commit `77cf76302`, not the `c9f05a687` the
> analysis names (identical tree, different lineage).

## PHASE-2 RENDERER EXPANSION STARTED: OPENGLES2 ADDED (2026-08-10, feature/renderer-opengles2)

> The first Phase-2 addition (FUTURE.md "Planned additions" #1) is implemented on the dedicated
> branch **`feature/renderer-opengles2`**, rooted exactly at the pre-expansion promotion head
> `4c93f185c` (not merged into `develop`; no other renderer was started). Public renderer
> identity count **41 -> 42**: `GraphicsRendererType::OpenGLES2` / `OPENGLES2` /
> `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_OPENGLES2` -- the **fifth public GL-family profile**
> on the shared EasyGL implementation (implementation-family count unchanged at 38), pairing
> WEBGL1's GLSL ES 1.00 shader dialect with a native GLES 2.0 context request and genuine
> ES 2.0-only GL mechanics (per-texture sampler state, baseVertex attribute-pointer emulation,
> combined-FBO readback, unsized RGBA storage, split depth+stencil attach; MSAA/MRT/occlusion/
> Texture3D/instancing/multi-stream truthfully refused). `check_renderer_identities.py` = 42;
> OPENGLES1 and OPENGLES3 are untouched, so the family finally reads OPENGLES1/OPENGLES2/
> OPENGLES3 exactly as `docs/RendererNamingMigration.md` §3 reserved. Plan: `plan_opengles2.md`;
> capability boundary and runtime-extension gates: `docs/opengles2-renderer.md`; registry:
> `docs/renderer-registry.md` (42 rows). The full EasyGL example/pixel suite registers and runs
> under this profile (first live GLSL ES 1.00 driver execution in this project), with OPENGLES3
> and OPENGL33 re-run as regression controls.
## FNA3D RENDERER LANE — VALIDATED ON OPENGL, DRIVER MATRIX OPEN (2026-08-11)

> **Validated on FNA3D's OpenGL driver; the driver matrix is still open (FNA3D-34).** An external
> audit (2026-08-11) found several tasks marked **done** on the strength of code existing rather
> than a test proving it — sharpest case FNA3D-10, which claimed RenderTargetCube + mips + MRT
> while `Fna3d_RenderTarget` renders none of the three. The conformance phase FNA3D-26..35 closed
> every one of those on this driver: cube faces, MRT and render-target mips now render and read
> back (`Fna3d_RenderTarget_Advanced`), multi-stream input and `SetDataOptions` are pixel-checked
> (`Fna3d_Buffers`), sampler filtering and all three address modes are covered (`Fna3d_Sampler`),
> negative/lifetime behaviour is covered (`Fna3d_Lifetime`), and the 39-scene XNA oracle corpus
> runs as a registered CTest (`Fna3d_XNA_Oracle`) — the only coverage DualTexture,
> EnvironmentMap and Skinned have.
>
> **The corpus found a real defect** the targeted tests had all missed: `SetMatrix4x3Array` dropped
> the translation row of every bone matrix, silently turning translation bones into identity bones.
> Fixed (FNA3D-27a) and pinned by `Fna3dMatrixPackingTests`; all six skinned scenes are now at or
> better than the EasyGL baseline. See `docs/fna3d-parity-report.md`.
>
> Two shared-layer gaps are **reported, not patched from this lane**: `Texture2D::SetData` has no
> `isDisposed_` guard, and CNA has no destination-offset `SetData` overload. Both affect every
> renderer and need their own commit with cross-renderer regression coverage.


> The first renderer-expansion lane. **`FNA3D` is CNA's 42nd public renderer identity**
> (`GraphicsRendererType::Fna3d`, `-DCNA_GRAPHICS_RENDERER=FNA3D`, `CNA_RENDERER_FNA3D`,
> `modules/renderers/fna3d`, `cna_renderer_fna3d`, `CNA::Internal::Renderers::Fna3d::Fna3dRenderer`),
> implemented on `claude/renderer-fna3d-q1rsyc` / `feature/renderer-fna3d`. **Not merged into
> `develop`** — integration is the owner's call.
>
> **What it is.** FNA3D (https://github.com/FNA-XNA/FNA3D, pinned at release **26.08** = `3240147`,
> zlib) is the graphics library FNA itself renders through; its device API *is* XNA 4.0's, and every
> enumeration it exposes is numerically the XNA enumeration CNA already ports — pinned by
> `static_assert` rather than transcribed. Like `LLGL`/`DILIGENT`/`SOKOL`/`BGFX` it names a portable
> middleware layer, not a native API: it selects SDL_GPU, Direct3D 11 or OpenGL at runtime. Its only
> dependency is SDL 3.2.0+, i.e. exactly the SDL3 CNA already vendors.
>
> **What is new about it.** This is the only CNA renderer that executes **XNA's actual compiled
> stock effects** (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`,
> `SkinnedEffect`, `SpriteEffect`) through MojoShader, selecting variants with XNA's own integer
> `ShaderIndex` arithmetic, rather than a reimplementation of them. It is also the only renderer
> whose `MultiStreamVertexInput` is native rather than emulated: `FNA3D_ApplyVertexBufferBindings`
> takes an array of real per-stream `VertexDeclaration`s.
>
> **Truthful boundaries, each with a matching refusal.** `CustomEffects` is **false** —
> `FNA3D_CreateEffect` takes a *compiled* D3D9 Effect binary and nothing in FNA3D compiles shader
> source, so `CreateEffectRenderer` returns null. `Instancing` is **false** — FNA3D instances fine,
> but the stock effects declare no per-instance vertex input (in real XNA that needs a custom
> Effect), so `DrawInstancedPrimitivesEx` refuses by name instead of stacking every instance on
> record 0. Both are structural, not deferred. See `docs/fna3d-renderer.md` and `plan_fna3d.md`.
>
> **Validation.** Native runtime on Linux/Xvfb/Mesa llvmpipe through FNA3D's OpenGL driver: twelve
> `Fna3d_*` CTest binaries, all pixel oracles, plus 41 device-free unit tests in the corpus. The
> full `CnaTests` corpus under `CNA_GRAPHICS_RENDERER=FNA3D` is **6106 passed / 0 failed / 7
> skipped**, and the `HEADLESS` control is unchanged. Clean under ASan/UBSan apart from a
> pre-existing upstream signed-overflow in MojoShader's own `mojoshader_common.c` string parser.
> FNA3D's SDL_GPU and Direct3D 11 drivers are external gates (no Vulkan ICD and no Windows here).
> Two existence-gate spikes are committed under `fna3d-spike/`; the second one is what measured
> FNA3D's driver-dependent sub-rectangle `ReadBackbuffer` origin, which the renderer works around
> by cropping in CNA.
>
> **Follow-up batch (FNA3D-19..25).** Format-correct transfer sizing for block-compressed formats,
> driver limit queries (`SupportsDXT1`/`S3TC`/`BC7`/`SRGBRenderTargets`, `GetMaxTextureSlots`) with
> refusals by name, a compressed-readback probe so `GetData` never reports an untouched buffer as
> read, `NoOverwrite` gated on `FNA3D_SupportsNoOverwrite`, `FNA3D_SetTextureName`, and a linked-vs-
> compiled version check. Four FNA3D entry points remain unreachable without a shared-contract
> change and are documented rather than faked: `SetTextureDataYUV`, `Get{Vertex,Index}BufferData`,
> `CloneEffect`, `VerifyVertexSampler`.
>
> **Shared CNA surface touched** (each minimal and renderer-guarded): the identity registries and
> their validators (41 → 42), one `#ifdef CNA_RENDERER_FNA3D` block in
> `GraphicsDevice::getRendererWindowFlags()`, the `CnaTests` glob filter for the renderer-local test
> directory (Wicked/Magnum precedent), and renderer arms in three shared tests that enumerate
> renderers by name. No renderer behavior, module boundary or SharpRuntime mapping changed for any
> existing identity.

## PRE-RENDERER-EXPANSION NORMALIZATION PROMOTED AND PUBLISHED (2026-08-10)

> `develop` was fast-forwarded to the accepted combined descendant
> `25db3ccbe` → **`675e04c7a`**, tree **`9766eb2f0`** — byte-identical to the accepted
> `feature/module-examples` tree. The fast-forward was proven read-only first
> (`git merge-tree --write-tree` produced exactly `9766eb2f0`), so the promotion changed no
> implementation content. No merge commit (`git merge --ff-only`); merge-base `25db3ccbe`;
> 0 behind / 17 ahead; all 17 commits retained and `git verify-commit`-good. Both accepted
> branches keep their own endpoints and are published there:
> **`feature/renderer-naming-normalization` = `16f76cf1a`** (the naming campaign's historical
> endpoint — deliberately not moved forward) and **`feature/module-examples` = `675e04c7a`**.
>
> **Two owner-directed pre-expansion campaigns are now public.**
> *Renderer terminology normalization* (`25db3ccbe..16f76cf1a`): graphics "backend" →
> **renderer** (`CNA_GRAPHICS_RENDERER`, `IGraphicsRenderer`, `cna_renderer_*`,
> `cmake/RendererSelection.cmake`), the 11 historical DX*/D3D* public identities →
> **DIRECTX1..DIRECTX12** (no DIRECTX4), **OPENGLES → OPENGLES3** (OPENGLES2 deliberately not
> created yet), and **NOXNA → CNAEXT** / **CNA_NOXNA → CNA_CNAEXT** (umbrella `CNA::CnaExt`).
> Mapping: `docs/RendererNamingMigration.md`; evidence: `modularization/renderer-naming/`.
> *Module-owned examples* (`16f76cf1a..675e04c7a`): all **1373** tracked example files now live
> with their owning module — 1346 moved byte-identical, 10 with declared edits, 17 shared golden
> images deliberately kept at `examples/golden/` — registered by **44** module-local
> `examples/CMakeLists.txt` files, with **1756 → 1756** identical CTest registrations. Evidence:
> `modularization/module-examples/`; layout: `docs/physical-modules.md`. Renderer identity count
> is unchanged at **41**; renderer behavior, module boundaries and SharpRuntime component
> mappings are untouched.
>
> **Owner-local `develop` work was preserved first**, not stashed. The same four items
> (the two ad hoc xvfb registrations, untracked `AGENTS.md` and
> `examples/xvfb_screenshot_demo.cpp`) were proven byte-exact against the existing unpushed
> signed snapshot `owner/pre-develop-promotion-20260810-physical-modules` (`ea84f4537`) by
> SHA-256, so no new snapshot branch was needed; an independent out-of-repository byte copy plus
> patch was taken as well. Unlike the previous promotion, **re-application was required**: this
> descendant deletes `cmake/Tests/{EasyGL,SdlRenderer}Tests.cmake`, so the owner's +4/+4 intent
> was migrated to the new owning files
> `modules/renderers/{easygl,sdl-renderer}/examples/CMakeLists.txt` — same macros
> (`cna_easygl_test` / `cna_sdl_test`), same target names
> (`cna_xvfb_screenshot_demo_easygl` / `cna_xvfb_screenshot_demo`), same insertion points, with
> the source spelled `${CMAKE_SOURCE_DIR}/examples/xvfb_screenshot_demo.cpp` (the file
> deliberately stays a repository-level cross-renderer diagnostic) and the comment vocabulary
> moved to "renderer". The demo's one build-breaking include
> (`"../examples/common/ScreenshotEXT.hpp"`) became `"common/ScreenshotEXT.hpp"`, resolved
> through `CNA_GRAPHICS_EXAMPLES_DIR` exactly like every other migrated example. `AGENTS.md` is
> untouched. **All of it stays local and uncommitted**; the obsolete `cmake/Tests` copies were
> not resurrected. Owner-local functional gate: the OPENGLES3 target configures, builds and runs
> on the `:96` Xvfb, writing a real 400×300 RGBA PNG whose scene is exactly the intended one
> (green clear + blue 100×100 texture + 20×20 red marker); the SDL_RENDERER target configures
> and its translation unit compiles under `-DCNA_RENDERER_SDL_RENDERER` (its full link needs a
> cold whole-framework SDL_RENDERER build and was deliberately not run).
>
> **Bounded post-promotion gate** (a ref move is not a code rewrite — the full campaign matrices
> were deliberately not rerun). On the promoted committed tree: physical source-partition
> validator green on every configure; legacy global `src/`/`include/` roots absent (0 tracked
> paths, absent on disk); `check_renderer_identities.py` = **41**; all 11 canonical `DIRECTX<N>`
> selectors present, no `DIRECTX4`, and all 12 old selectors (`DX1..DX8`, `D3D9..D3D12`,
> `OPENGLES`) rejected; `OPENGLES1` + `OPENGLES3` both live; active `NOXNA`/`CNA_NOXNA`
> preprocessor uses **0**; `check_include_reachability.py` clean; `reconcile_examples.py`
> **RECONCILIATION: PASS**. Runtime: module probe + link-closure fleet **34/34 HEADLESS**
> (CNAEXT off) and the CNAEXT-on matrix **5/5** (`StrictXnaApiSurfaceCheck_Compile_Run`,
> `StrictXnaApiSurfaceLeakCheck_MustFailToCompile`, `ModuleProbe_probe_cnaext`,
> `ModuleLinkClosure_CnaExtComposition`, `CNAEXT_Settings_Compile_Run`); HEADLESS representative
> contract slice 24/24 with the two expected **Skips** (the campaign's
> `cna_apply_skip_convention()` fix holding); OPENGLES3 full incremental build 410 targets /
> 0 errors, **6564** tests registered, EasyGL representative slice 5/5 including the
> golden-image tests that read `examples/golden/` from the repo-root working directory; VULKAN
> configure green with **6462** tests registered; DIRECTX5 MinGW cross-configure green (the
> accepted green DX boundary); DIRECTX11/DIRECTX12/DIRECTX9 configures green. D3D link graph
> re-verified: `cna_renderer_d3dcommon` is linked by directx11 and directx12 only — directx9 and
> directx10 are independent. Live OPENGLES3 target set is **identical** to the campaign's
> `after-targets-opengles.txt` capture (1681/1681). Active-scope terminology audit: **0**
> occurrences of `GraphicsBackend`, `CNA_GRAPHICS_BACKEND`, `CNA_BACKEND_`, `backend_graphics`,
> `BackendSelection`, `cna_register_backend_test` or `CNA::Internal::Backends` anywhere under
> `modules/`, `cmake/`, `scripts/`, `tools/`, `tests/` or the root build configuration.
>
> **All pre-renderer-expansion preparation is complete and public**: target modularization
> COMPLETE AND PUBLIC; physical module/package modularization COMPLETE AND PUBLIC; modular
> sharp-runtime consumption COMPLETE AND PUBLIC (sibling `develop` `81624983`, untouched here);
> renderer terminology normalization COMPLETE AND PUBLIC; module-owned examples COMPLETE AND
> PUBLIC. **Renderer expansion is NEXT** (FUTURE.md Phase 2, 41 → 55: OPENGLES2 plus the 13 new
> renderer implementations) — **not started**, and it needs its own explicit owner instruction.
> Its common base is the public `develop` head produced by this promotion (the implementation
> head `675e04c7a` plus this documentation commit); no future renderer, XNA-sample or glTF work
> began in the promotion session.
>
> **Known residuals recorded, not fixed here** (neither introduced by these two campaigns, and
> both outside a promotion session's mandate): `.gitattributes` still exempts
> `src/CNA/Internal/Backends/Bgfx/shaders/bgfx_shaders.hpp` from whitespace checks — a path that
> ceased to exist at the *physical-modules* promotion (`3ecbbce72`), so the generated header at
> its real location `modules/renderers/bgfx/src/shaders/bgfx_shaders.hpp` no longer gets the
> exemption; and `GraphicsDevice::GetGraphicsRendererName()`'s Doxygen prose still cites
> `"EASYGL"`/`"D3D9"` as example values although the function returns the canonical identity
> names. `tools/xna-oracle/CnaOracleRender.cpp` intentionally keeps its short `D3D9`/`D3D11`/
> `D3D12` **stdout tags** (`CNA-XNA-ORACLE-OK renderer=...`) while selecting on the new
> `CNA_RENDERER_DIRECTX<N>` macros, so the checked-in oracle corpus comparisons stay valid.


## PHYSICAL MODULARIZATION PROMOTED — **CNA MODULARIZATION CAMPAIGN CLOSED** (2026-08-10)

> `develop` was fast-forwarded to the accepted Phase-3 head: `ea61123e6` → **`3ecbbce72`**, tree
> **`a116280e0`** — byte-identical to the accepted `feature/physical-modules` tree. The
> fast-forward was proven read-only first (`git merge-tree --write-tree` produced exactly
> `a116280e0`), so the promotion changed no implementation content. No merge commit
> (`git merge --ff-only`); merge-base `ea61123e6`; 0 behind / 19 ahead; all 19 campaign commits
> retained and `git verify-commit`-good. `feature/physical-modules` keeps pointing at the
> implementation result. Promotion evidence: `MODULARIZATION_PLAN.md` §11.2.
>
> **Owner-local `develop` work was preserved first**, not stashed. The same four items as the
> previous promotion (the two ad hoc xvfb registrations in
> `cmake/Tests/{EasyGL,SdlRenderer}Tests.cmake`, untracked `AGENTS.md` and
> `examples/xvfb_screenshot_demo.cpp`) were captured in the new unpushed signed snapshot
> `owner/pre-develop-promotion-20260810-physical-modules` (`ea84f4537`, parent `ea61123e6`). A
> new snapshot was required: the existing `owner/pre-develop-promotion-20260810` carries the same
> intent but different bytes, because the Phase-1 promotion rewrote those two files' base blobs.
> No re-application was needed afterwards — both cmake files are byte-identical across the
> promoted range and neither untracked path exists on the feature branch, so the fast-forward
> touched none of the four; all four verified byte-identical after the ref moved. No owner-local
> work entered the modularization history.
>
> **Bounded post-promotion gate** (a ref move is not a code rewrite — the full Phase-3 matrix was
> deliberately not rerun). Configure-time, run from the promoted `develop` worktree: HEADLESS,
> OPENGLES and VULKAN all configure clean (0 CMake warnings/errors), so the physical
> source-partition/ownership validator passes on all three; legacy global `src/` and `include/`
> roots absent (0 tracked paths); 14 framework modules each with
> `CMakeLists.txt`+`include`+`src`+`tests`; 38 renderer families + `renderers/common/d3d`;
> `check_renderer_identities.py` = **41**; `check_include_reachability.py` clean; header
> self-containment 542 checked / 540 pass — the 2 failures are the Windows-only Glide ABI pair
> (`<windows.h>`/`HMODULE`), host-inherent and reproduced identically by the byte-identical
> script on the feature branch. No-loss: `reconcile_phase3.py` **RECONCILIATION: OK**
> (production 1357 → 1357, tests 483 → 492, api-decls 1300 → 1301 with **zero removed** — the
> single addition is the relocated `D3D9ShaderConstantSlot`, ctest names HEADLESS 6120 → 6143 and
> OPENGLES 6527 → 6546 with **zero removals, zero renames**), and a fresh
> `capture_inventory.py` run against the promoted worktree is **byte-identical** to the committed
> `modularization/physical-modules/after/` evidence for production, tests and api-decls.
> Runtime gates on the identical tree: module probe/link-closure fleet **35/35 HEADLESS**,
> **30/30 OPENGLES**, **31/31 VULKAN** (including `ModuleLinkClosure_NoXnaComposition`,
> the DevicesExt/GraphicsExt probes, the four HEADLESS native-SDK-free gates and
> `ModuleLinkClosure_VulkanRendererClosure`); `RendererIdentityRegistry` green; builds 0 errors /
> 0 warnings. HEADLESS `-L Headless` 48 = 45 pass + 2 skip + the accepted `Headless_Smoke`
> residual; EasyGL 293 on the dedicated `:96` Xvfb = 291 pass + 1 skip + exactly the documented
> `EasyGL_GraphicsDevice_ReferenceStencil` failure (Task 872); VULKAN 211 = 210 pass + the
> accepted `Vulkan_DepthBias` llvmpipe residual (one `-j4` contention flake,
> `Vulkan_BoundTargetLifetime`, passed standalone). `git diff --check` clean. D3D link graph
> re-verified: `cna_backend_graphics_d3dcommon` is consumed by d3d11 and d3d12 only — d3d10 and
> d3d9 are independent.
>
> **The CNA modularization campaign is closed.** Target modularization COMPLETE; physical
> module/package modularization COMPLETE AND PROMOTED; modular sharp-runtime consumption ACTIVE
> AND PUBLIC (sibling `develop` `81624983`); sharp-runtime post-audit remediation CONTINUES
> INDEPENDENTLY on its own branch and merges in later increments. **Renderer expansion is NEXT**
> (FUTURE.md Phase 2, 41 → 55) — **not started**, and it needs its own explicit owner
> instruction. Its common base is the public `develop` head produced by this promotion (the
> implementation head `3ecbbce72` plus this documentation commit).

## MODULAR SHARP-RUNTIME LIVE — **CNA CONSUMES THE PUBLIC MODULAR sharp-runtime `develop`** (2026-08-10)

> The §9.7 external gate is closed by owner decision (integrate the current remediation
> snapshot now; do not wait for the post-audit campaign to finish). The exact snapshot
> **`7888a29f`** of `claude/remediation-batch-1804-namespace-b1yjh5` was merged into
> sharp-runtime `develop` with a history-preserving GPG-signed merge commit **`81624983`**
> (parents `1e51c2d8` + `7888a29f`; 5 textual conflicts resolved semantically) and published:
> `origin/develop == 81624983`. FINAL-STAB-001 survived intact — the native-`__int128` probe
> with `SHARP_RUNTIME_HAS_NATIVE_INT128` published on the `sharp_runtime_headers` INTERFACE,
> `Decimal.cpp` leaving the Core.Base source list without native `__int128`, BitConverter
> keeping BOTH the audit bounds checks and the int128 guard, and the i686 MinGW boundary
> regression rebuilt against the modular graph (256-step cross build, `Decimal.cpp.obj`
> proven absent, binary also runs under Wine). Gate on the merged develop: boundary/seam/
> fixture validators green, build **0 warnings / 0 errors**, **16,341 tests passed across 37
> executables (0 failures locally)**, Int128/UInt128 81 + Decimal 185 + BitConverter 113
> focused green, selective components **10/10**. The remediation branch itself was not
> touched and keeps advancing (observed `5c8e057f`, 3 commits beyond the integrated
> snapshot); later increments merge separately.
>
> CNA side — branch `feature/sharp-runtime-modular-adaptation` from `60c363a7`: the
> documented Net-only adaptation is now **APPLIED**. `NetworkSessionProperties` implements
> the ticket-#1791 `IList<T>` contract: the non-const `operator[]` returns the tracked
> `System::Collections::detail::ElementReference` proxy (FNA auto-append preserved at
> indexing time, since a proxy binds an existing slot), new `getItem`/`setItem` accessors
> carry real XNA indexer semantics (`get` throws, `set` appends past the end), and a
> `MutationCounter` advances on every effective mutation. The four proxy-vs-`std::optional`
> `EXPECT_EQ` sites in `NetDiscoveryProtocolTests` moved to `getItem` — exactly the 4
> compile errors / 1 root cause the merge preview predicted; no other CNA code was affected
> (`NetDiscoveryProtocol.cpp:96`'s `properties[index] = value;` is the spelling the proxy
> exists to keep). 8 new proxy/getItem/setItem regression tests. The dual-mode seam is
> unchanged in mechanism; against the sibling public sharp-runtime develop it selects
> **MODULAR mode in every configuration** (monolithic fallback retained for old checkouts).
> Per-module component sets re-derived from actual includes == the declared sets, unchanged.
>
> Retaken matrix on the modular combination (all four trees rebuilt): partition validator
> green everywhere; module gates **12/12 HEADLESS** (probe_math link line =
> `libcna_math.a + libsharp_runtime_core.a` only — the math-only closure proven at link
> level; `GraphicsNativeSdkFree` green), **11/11 OPENGLES**, **11/11 VULKAN**; renderer
> identities **41**; registrations **6138 / 6545 / 6442** (= promoted baseline 6130/6537/6434
> + the 8 new Net tests); HEADLESS `-L Headless` 48 = 45 pass + 2 skip + the accepted
> `Headless_Smoke` residual; EasyGL 293 on a dedicated Xvfb (`CNA_TEST_DISPLAY=:96`) =
> **291 pass + 1 skip + 1 fail** — `EasyGL_GraphicsDevice_ReferenceStencil` (Task 872,
> unchanged); VULKAN focused **222 = 221 + the accepted `Vulkan_DepthBias`** llvmpipe
> residual; from the repo root: Net 195, NetworkSessionProperties 28, Content 247 (Decimal
> XNB readers **5 = 5**), Audio 561, Input 412, Runtime 78, GamerServices 397 — all green.
> ASan+UBSan (`address,undefined,float-cast-overflow`): all five probes clean under
> `check_initialization_order=1:strict_init_order=1`; strict curated corpus **650 = 647
> pass + 3 skip, zero reports** (REMED-GFX-221 coverage retained); the adapted Net paths
> (38 tests) sanitizer-clean; the only leak reachable in the wider Net family is the
> documented pre-existing `NetworkSession::EndCreate` P6 finding (NetworkSession.cpp:762),
> not re-opened. No-loss: production **1357 → 1357** (content changes only in the two
> NetworkSessionProperties files), `api-decls.tsv` **byte-identical at 1300**, tests
> **483 → 483** (content changes only in the two Net test files).
>
> After this branch's fast-forward promotion, the resulting CNA `develop` head is the
> renderer-expansion base (FUTURE.md Phase 2) — which remains **not started** and needs its
> own owner instruction.

## MODULARIZATION PROMOTED — **CNA MODULARIZATION COMPLETE AND PROMOTED TO `develop`** (2026-08-10)

> `develop` was fast-forwarded to the accepted modularization head: `5f2c4e941` →
> **`41028e995`**, tree **`d2a9ea265`** — byte-identical to the accepted `feature/modularization`
> tree, so the promotion itself changed no content. No merge commit (`git merge --ff-only`);
> merge-base `5f2c4e941`; 0 behind / 20 ahead; all 20 campaign commits retained with their GPG
> signatures. Promotion evidence: `MODULARIZATION_PLAN.md` §10.
>
> **Owner-local `develop` work was preserved first**, not stashed: the two ad hoc xvfb test
> registrations in `cmake/Tests/{EasyGL,SdlRenderer}Tests.cmake` plus untracked `AGENTS.md` and
> `examples/xvfb_screenshot_demo.cpp` were captured in the unpushed signed snapshot branch
> `owner/pre-develop-promotion-20260810` and re-applied to the worktree after the fast-forward.
> No owner-local work entered the modularization history.
>
> **Bounded post-promotion gate** (a ref move is not a code rewrite — the full Phase-2 matrix was
> deliberately not rerun): HEADLESS, OPENGLES and VULKAN configures green, so the source-partition
> validator passes on all three; module gates 12/12 HEADLESS (including the math-only probe and
> `ModuleLinkClosure_GraphicsNativeSdkFree`), 11/11 OPENGLES, 11/11 VULKAN — the Vulkan run
> re-proving the selected-backend-only graphics closure; `RendererIdentityRegistry` green on all
> three plus `scripts/check_renderer_identities.py` = **41**; registrations unchanged at 6130
> HEADLESS / 6537 OPENGLES / 6434 VULKAN (211 `Vulkan_*` + 11 gates in the focused set). HEADLESS
> `-L Headless`: 48 tests, 47 pass + 2 skip + the accepted `Headless_Smoke` primitive-range
> residual, which the preserved **pristine pre-modularization control binary reproduces
> identically**. OPENGLES/EasyGL focused suite (293) on a deterministic Xvfb: 291 pass, 1 skip,
> 1 fail = the documented known failure `EasyGL_GraphicsDevice_ReferenceStencil` (Task 872,
> AUDIT.md:128, carried visible); the seven failures first seen on the owner's live `:0` desktop
> were re-run individually — six pass on a clean display (occluded-window readback artifacts) and
> the seventh is that same Task 872 failure. No-loss re-verified against the promoted tree:
> production 1357 → 1357 (674 moves = 662 R100 + 12 whose only changed lines are 15 `#include`
> directives; 0 added, 0 deleted), `api-decls.tsv` **byte-identical** to the pristine baseline at
> 1300 declarations, tests 478 → 483 (the 5 module probes; 0 baseline test lost).
> `git diff --check` clean.
>
> **Still open, and unrelated:** the sharp-runtime audit-remediation merge gate (§9.7 of the plan,
> summarized below). It does **not** make CNA modularization incomplete. The next CNA feature
> phase would be the FUTURE.md renderer expansion, which has **not** begun and needs its own owner
> instruction.

## MODULARIZATION PHASE 2 — **physical layout + architecture hardening complete on `feature/modularization`** (2026-08-10)

> Continuation of the campaign below, same branch, from Phase-1 head `b072f0da6`. Authoritative
> record: **`MODULARIZATION_PLAN.md` §9**; machine-readable evidence:
> `modularization/after-phase2-layout/` (incl. the 674-row `move-map.tsv`); item-by-item
> classification: `modularization/RECONCILIATION.md` (Phase-2 section).
>
> **Done and proven:** the implementation tree now mirrors module ownership —
> `src/<Module>/{Xna,Internal,NoXna}/` per subsystem, all 38 renderer directories under
> `src/Graphics/Backends/` — via five pure `git mv` commits (674/674 files R100 byte-identical)
> plus one path-update commit; the only source-content changes anywhere are 15 generated-shader
> include directives in 12 backend files rewritten to the includer-relative form Bgfx/Llgl
> already used (old-blob/new-blob diffs: zero non-`#include` lines) and one example TU's
> directive. Public `include/` is untouched — `api-decls.tsv` byte-identical, consumer include
> paths stable. Include hygiene: no target exposes `src/` as an include root any more (one
> documented scoped exception: `cna_test_d3d9_shadercache`). A new nm-based symbol-edge audit
> over the built archives found exactly one undeclared edge — FrameworkDispatcher's
> FNA-mandated TouchPanel pump (audio→input), previously resolving through another module's
> `$<LINK_ONLY>` closure — now declared explicitly; post-fix: 35/35 cross-module symbol edges
> declared/reachable. The three declared cycles were re-reviewed with exact symbol/FNA evidence
> and all three classified **ACCEPT_INTENTIONAL** (dossiers: plan §9.4). Retakes on the new
> layout: OPENGLES full-suite A/B vs the preserved pristine Phase-1 binary — identical per-test
> outcome sets (6218 universe; the single in-run failure is a diagnosed A/B-harness env-var
> interaction that passes on both binaries under Phase-1's env); HEADLESS 6130 = control + the
> 12 gates with exactly the control's 2 accepted deterministic residuals after the serial flake
> rerun; VULKAN 222 = 211 + 11 gates with the single accepted `Vulkan_DepthBias` llvmpipe
> residual on the identical arm; module probes + closure gates + RendererIdentityRegistry (41)
> green on OPENGLES, HEADLESS and VULKAN; modular sharp-runtime seam tree green with Decimal
> readers present 5 = 5.
>
> **sharp-runtime gate (external, still open):** remediation branch observed at `832726e0`
> (moved again during this session — still active); its module registry is byte-identical to
> the Phase-1-studied `e8340b33`, so the CNA seam needed no change; sharp-runtime develop
> untouched at `1e51c2d8`; the Net-only `NetworkSessionProperties::operator[]` adaptation
> remains documented and deliberately unapplied.
>
> **Incidental pre-existing finding (recorded, not fixed):** running `CnaTests` with a CWD other
> than the repo root leaves `tests/assets/…` unresolvable and `MediaLibraryTestFixture` then
> fails into a pre-existing index-out-of-range + segfault path in the empty-media-library
> state — reproduced identically with the pristine Phase-1 binary, absent under ctest (correct
> per-test working directories). A future MediaLibrary robustness ticket of its own.

## MODULARIZATION CAMPAIGN — **target-graph modularization complete on `feature/modularization`** (2026-08-10)

> FUTURE.md Phase 1 work, on branch `feature/modularization` from base `5f2c4e941`. The
> authoritative plan, evidence ledger, and deferred-scope record are in
> **`MODULARIZATION_PLAN.md`**; the machine-readable no-loss baseline is in
> `modularization/baseline/`.
>
> **Done and proven:** the monolithic CNA library is split into twelve subsystem STATIC modules
> (+ the existing GamerServices/Net) with explicit derived link edges, `CNA::<Name>` aliases, a
> configure-time source-partition validator, and `CNA` preserved as the compatible INTERFACE
> umbrella — no file moved, no header changed, public behavior parity proven against pristine
> controls on OPENGLES (full-corpus A/B, identical per-test outcomes), HEADLESS (canonical
> suite; identical deterministic residuals) and VULKAN (focused Xvfb suite; the single
> llvmpipe DepthBias residual reproduces identically with a pristine-built binary). All 41
> public renderer identities are pinned by the new `RendererIdentityRegistry` gate; five
> minimal-link probes + link-closure gates make each module's real dependency closure a
> permanent contract, including the HEADLESS proof that graphics-core carries no native
> renderer SDK. Every CNA module declares its specific sharp-runtime components
> (`cmake/SharpRuntimeConsumption.cmake` seam: monolithic archive today, per-component against
> the modular runtime), validated against a local read-only merge preview of the completed
> sharp-runtime modularization — `probe_math`'s sharp closure is the single
> `libsharp_runtime_core.a`, and the Decimal XNB readers survive (the FINAL-STAB-001 INT128
> define ported to the modular headers surface).
>
> **Gated remaining work:** the history-preserving merge of
> `claude/remediation-batch-1804-namespace-b1yjh5` into sharp-runtime develop waits for that
> branch's audit-remediation campaign to reach an accepted checkpoint (it moved three times
> during this session); the merge-preview resolution recipe and the one Net-only CNA
> adaptation (`NetworkSessionProperties::operator[]` vs the new `IList<T>` ElementReference
> contract) are recorded in MODULARIZATION_PLAN.md. Physical source moves are deliberately
> deferred (plan §6). The pre-existing environmental residuals recorded during control runs
> (REMED-GFX-133, `Headless_Smoke` primitive-range abort, llvmpipe `Vulkan_DepthBias`) are
> unchanged by modularization and remain open findings of their own.

## FINAL RECONCILIATION — **READY FOR DEVELOP MERGE** (2026-08-09)

The post-audit integration campaign remains complete at **21/21 accepted lanes, 0 pending**, with
Batch 0 through Batch 6 checkpoint history intact and **41 public renderer identities**.
**`FINAL-STAB-001` is complete:** sharp-runtime commit `1e51c2d869697fd827af7ca342ffabf77d30faf8`
publishes the native-128 capability boundary and CNA uses it for Decimal XNB support; the full
Glide i686 graph and its PE32 fake-ABI control pass; HEADLESS now declares its real reverse link to
CNA so the canonical sanitizer harness graph links without a generated-tree rescan; and
`Vector2::Zero` plus its sibling constants are truly constant-initialized, closing
`REMED-GFX-221` without a `GestureDetector` special case.

The bounded retake passes the strict, unsuppressed ASan+UBSan+float-cast-overflow corpus, affected
audio harnesses/tests, Glide portable/shared/ABI controls, native HEADLESS shared core, and serial
OPENGLES/EasyGL smoke/corpus. The current OPEN-finding inventory contains **no HIGH/P1 mandatory
final-development blocker**; `REMED-GFX-217/-218` remain HIGH/OPEN only for their accepted deferred
full-translator scope after the dangerous checkpoint paths were guarded. Other accepted
nonblocking residuals and external native-runtime gates retain their recorded scope. `develop`
remains the candidate's ancestor, so the committed branch is a fast-forward shape, but the actual
merge was not performed and the owner's pre-existing dirty `develop` files must be preserved
first. The exact owner-controlled next action is to preserve those files, fast-forward `develop`
to the signed integration tip, then run the bounded post-merge smoke gate. The external
MetaGL/EasyGL rewrite is accepted; no further history rewrite or force-push is planned.
Modularization and future renderer expansion remain future work and have not begun. Complete
evidence is in `integration/FINAL_RECONCILIATION.md`.

## CURRENT — **`metal` READY / INTEGRATED ✅ · BATCH 6 CHECKPOINT COMPLETE ✅** · 21 integrated / 0 pending (2026-08-09, `debian`)

> **Metal landed as signed `--no-ff` merge `012b158eb8246ce267887acbd4fc7a2468d89e52`**
> with parents `4ac696c748fb18eef7dd06cca82a0486549bcd5d` and
> `e2ffe7290ddf5aab5c211b1fc2c00f0e09bd42f1`; the merge and adaptation trees are byte-identical
> at `31200b608cd2a4c8ccd0f7cb9d6325540cec9458`. The unchanged original local and remote
> `feature/metal` head `48928d113cb864f78d754256d2d559d914d4f1a7`, based at `ac3aaaeb`, remains
> preserved by the sole signed annotated `archive/preintegration/metal-20260804` tag (object
> `43f6eab8d40c6006265cd4e19223cdd3d68c1fc3`, GPG-good). Full record:
> `integration/lanes/metal.md`.
>
> **History and adaptation.** The 99-row history map retains 88 original commits as signed
> chronological replays and records 11 omissions: five temporary diagnostics, two superseded
> changes, two changes already integrated, and two session handoffs. Replay range-diff is
> **76 `=` / 12 `!` / 11 omitted**. Six post-audit commits close or conservatively disable
> `METAL-258` through `METAL-281`, producing 94 signed adaptation commits at `e2ffe7290`.
> Together with the merge, the final Metal range is 95/95 GPG-good and Robert-authored/committed;
> attribution and trailer sweeps are empty.
>
> **Identity and support boundary.** `METAL` is CNA's genuine 41st public backend, direct native
> Objective-C++/MSL over Metal/QuartzCore/Foundation with SDL3 used only for the macOS window,
> high-pixel-density Metal view, and `CAMetalLayer`. There is no SDL_Renderer, SDL_GPU, or other
> renderer fallback. The supported target is macOS only; iOS/tvOS are unvalidated and rejected,
> and no minimum macOS deployment target is claimed. ThreeD, DepthStencilBuffer,
> AnisotropicFiltering, WireFrame, Texture3D storage, StencilBuffer, and AdditiveBlending report
> true. MSAA, MRT, OcclusionQuery, CustomEffects, MultiStreamVertexInput, and Instancing report
> false and reject or clamp deterministically. Backbuffer readback throws rather than returning
> the historically known-wrong clear-only pixels.
>
> **No-Mac evidence boundary.** Historical Actions run `29814126178` built the latest production
> commit `e0f424268` on macOS 14/Xcode 15.4 with Metal validation and passed **136/143**. Six
> failures returned only the clear color; `Metal_RenderTarget2D_MSAA` separately applied four
> samples but produced a binary edge. That run predates the adapted interfaces and support
> narrowing, so it is historical evidence only. No adapted Objective-C++ compile, native link,
> MSL compile, resource-lifetime run, or pixel result exists. A fresh successful macOS workflow
> remains the external support-confidence boundary, not an integration blocker under the
> authorized source-continuity policy.
>
> **Current validation.** Stable HEADLESS/ccache builds pass **206/206** unique portable Metal
> tests and **207/207** `ctest -R '^Metal'` registrations (the aggregate is the extra entry), with
> `DISPLAY` unset and no Objective-C++ in the graph. GNU 14.2 ASan+UBSan passes 206/206 with no
> sanitizer diagnostic in the complete log. Non-Darwin `METAL` configure rejects at the intended
> macOS-only gate. Post-merge OPENGLES/EasyGL controls are **124 pass + 1 intentional skip** and
> **2/2** real render-target/readback/viewport-scissor checks; LLGL continuity is **48/48 + 3/3**.
> Every build used stable in-repository directories, ccache, vendored job limit two, and `-j4` or
> lower.
>
> **Findings and safety.** `METAL-258/-259/-266` close by truthful feature disablement;
> `METAL-260` through `-265`, `-267` through `-280`, and retained-resource boundary `-281` close
> through implementation plus portable policy/oracle coverage. Native proof remains external
> where the lane card says so. Public Texture wrappers are not claimed safe after their
> `GraphicsDevice`; the lifetime guarantee is limited to independently retained backend handles
> and native resources. `audit/` remains tree `168c9b668763b78e63106e27d942a76d2457f41d`; the
> four protected stash objects and all original lane refs/tags remain unchanged; nothing was
> pushed.
>
> **Batch 6 / campaign boundary.** Group G is now **4/4** (Skia, Direct2D, LLGL, Metal) and the
> authoritative inventory is **21/21 integrated, 0 pending**. Technical stabilization is READY.
> The fresh checkpoint retake passed and signed annotated tag
> `integration/checkpoint-batch6-20260809` was created once without force with exact message
> `CNA integration Batch 6 checkpoint`. Annotated tag object
> `8d347c933a3da3c39f22711e40e80cf7a29c4682` peels to `012b158e`; `git tag -v` exits 0 with a Good
> signature from Robert Vokac under fingerprint
> `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. The tag is local only and nothing was pushed.
> **Batch 6 checkpoint status: COMPLETE.** This does not itself claim final campaign or `develop`
> readiness; that is a separate owner decision after the external Metal boundary and campaign-wide
> review.

## Previous — **`llgl` READY / INTEGRATED ✅** · 20 integrated / 1 pending (2026-08-09, `debian`)

> **LLGL landed as signed `--no-ff` merge `4ac696c748fb18eef7dd06cca82a0486549bcd5d`**
> with parents `21b1fcd1` and `c74fbaeb`. The unchanged original `feature/llgl` head
> `fa26e72dcda612de2a8cff814e748c7479e45836`, based at `1eb22c11`, remains preserved by the sole
> signed annotated `archive/preintegration/llgl-20260804` tag. `adapt/llgl` contains 69 signed
> commits: all 68 meaningful historical commits replayed chronologically plus one stabilization
> commit, head `c74fbaebb93745de08130d050e11230639df3259`. Range-diff accounts every commit 1:1
> (20 `=`, 48 `!`); 47 non-equal pairs include patch/context adaptation, 47 include required author
> cleanup, and 46 overlap. Author dates and technical subjects match, and attribution/trailer
> sweeps are empty. Full record: `integration/lanes/llgl.md`.
>
> **The i686 blocker is classification A, non-gating.** The exact compiler failure is
> `sharp-runtime/include/System/Int128.hpp:31:9: error: expected unqualified-id before '__int128'`
> from `i686-w64-mingw32-g++`, first reached through `BitConverter.cpp`. The only concrete preserved
> CNA route is Glide's required x86 ABI probe. Historical CNA LLGL has no i686/Windows/MinGW route,
> test, option or public contract; it is Linux/X11 native x86_64. Upstream LLGL's Win32/MSVC support
> does not create one. Owner disposition: preserve the record, leave sharp-runtime unchanged, and
> validate LLGL on its truthful x86_64 route.
>
> **Identity and runtime.** `LLGL` is CNA's genuine 40th public backend identity. The supported
> chain is CNA LLGL -> pinned LLGL `Release-v0.04b`
> (`1e78d8fa497f5cab76b231ba13f4d6249dac0e7e`) OpenGL RenderSystem -> native OpenGL/GLX on
> Linux/X11 x86_64. OpenGL is required and is the only automatic/supported renderer. Vulkan remains
> compile coverage but explicit selection rejects after native validation exposed descriptor,
> image-layout and teardown violations. Null is explicit lifecycle diagnostics only. There is no
> silent fallback or extra public identity for an internal LLGL module.
>
> **Truthful boundary.** 3D, depth, MRT in the documented 2-4 `RenderTarget2D` SpriteBatch/custom-
> effect scope, anisotropy where reported, occlusion, custom SpriteBatch effects, Texture3D
> transfer, wireframe where reported and Additive are supported. Back-buffer MSAA, stencil,
> multistream, instancing, constant blend factor and non-zero depth bias are false/rejected.
> TextureCube is exact transfer-only storage; cube sampling and RenderTargetCube reject. Mip-MRT
> and cube-face MRT compositions reject. The current stream arrays remain authoritative; the one
> geometry stream honours `VertexOffset`, `vertexStart`, `startIndex` and `baseVertex`.
>
> **Validation.** Debug x86_64/ccache builds compile LLGL Null/OpenGL/Vulkan at `-j4` maximum.
> Dedicated LLGL CTest on Xvfb `:98` is **145 registered / 137 passed / 8 disabled / 0 failed**;
> full `CnaTests` is **5210 / 5203 passed / 7 skipped / 0 failed**. ASan+UBSan are proven linked and
> pass **9/9** strict focused controls; LeakSanitizer's 482104 bytes/2147 allocations root in pinned
> LLGL/SDL/Mesa GLX visual selection, not CNA. OPENGLES/EasyGL passes **9/9 + 15/15**, including
> cache isolation; accepted Direct2D passes **4/4**. Every final runtime gate used dedicated Xvfb
> with explicit X11. An earlier accidental `DISPLAY=:0` run opened windows on the owner's desktop;
> it was acknowledged, discarded as final evidence and not repeated.
>
> **Findings and carried state.** `LLGL-48/-52` are resolved; `LLGL-53/-54` close by measured
> implementation or deterministic narrowing. New `LLGL-57` (first-frame swap-chain extent drift)
> and `LLGL-58` (zero-count clear-value pointer reaching pinned LLGL UB) are resolved without a
> dependency patch. `REMED-GFX-223` remains resolved; `REMED-GFX-224` remains MEDIUM/OPEN;
> `REMED-CONTENT-007/-008/-011`, `REMED-BUILD-019`, and `D2D-134/-135/-136` retain their accepted
> states. `audit/` is untouched.
>
> **Group result.** Batch 6 / Group G remains exactly Skia, Direct2D, LLGL and Metal. The first
> three are integrated, so Group G is **3/4** and the inventory is **20/21**. Metal alone remains
> pending and did not begin. No Group G checkpoint is defined or eligible while Metal remains, and
> none was created. Recommended next action: Metal, respecting its no-Mac validation boundary. Do
> not begin it from this record.

## Previous — **`direct2d` READY / INTEGRATED ✅** · 19 integrated / 2 pending (2026-08-08, `debian`)

> **Direct2D landed as signed `--no-ff` merge `7af760bee`** with parents `c805fd73` and
> `1b740d96`; the merge and `adapt/direct2d` trees are byte-identical. The unchanged original
> `feature/direct2d` head `9b17e783`, based at `a7a49e3d`, remains preserved by the sole signed
> `archive/preintegration/direct2d-20260804` tag. The adaptation contains 55 signed commits:
> 48 chronological replays plus seven current-contract, test, documentation, and finding commits.
> Range-diff is 44 exact pairs plus four explained semantic adaptations; original author, email,
> date, subject, and body sequences match. Signed documentation-only `D2D-54` precision commit
> `21b1fcd17` is the current integration head. Full record: `integration/lanes/direct2d.md`.
>
> **Bounded owner authorization.** The historical owner freeze is preserved as history and the
> original ref was not modified. Its exact frozen plan recount was 128 rows = 32 complete + 35
> yellow + 61 blank, therefore **96 incomplete**, not the stale 88. Recorded freeze reasons are
> classified and disposed on the lane card; no unrecorded motive is invented. The remaining plan
> rows are explicitly native/external evidence, stronger fault coverage, or nonblocking
> process/performance/refactor work—not a concealed supported-path production defect.
>
> **Implementation.** `DIRECT2D` is CNA's genuine 39th public backend identity: Windows-only
> Direct2D 1.1 `ID2D1DeviceContext` application drawing, with D3D11 BGRA and DXGI 1.2 used only for
> device/surface/swap-chain presentation. There is no GDI, Software, SDL Renderer, EasyGL, D3D, or
> other-backend fallback; DirectWrite and WIC are unused. SDL3 supplies the HWND. Sprite, viewport,
> and scissor coordinates are logical framebuffer pixels, not Windows DIPs; forced 96 DPI makes
> D2D units numerically equal to target pixels, then presentation maps them to client pixels.
>
> **Truthful boundary.** Only `AnisotropicFiltering` is true in the exhaustive 13-capability
> switch. Direct2D remains 2D/SpriteBatch-only. Unsupported 3D, depth/stencil, MSAA, MRT,
> wireframe, queries, custom effects, Texture3D, multistream, instancing, Additive, mipmapped RTs,
> unsupported formats, and active-RT presentation reject deterministically. Texture2D authored
> mips/MipLinear, single Color RT level zero, RGBA↔BGRA pitch-aware upload/readback, supported
> alpha modes, viewport/scissor, transforms, resize, recovery, and lifetime are covered.
>
> **Validation.** MinGW GCC 14 x64 Release built the four Direct2D targets at `--parallel 2`.
> Wine 10.0/Xvfb passed **4/4**: Smoke 2.43 s, Parity 4.04 s, Lifetime 2.67 s, Unit 1.97 s;
> unit subset **19/19**. Post-D2D-136 focus passed 26/26. OPENGLES/EasyGL identity/capability and
> exact textured-pixel controls, GDI Wine smoke, and HTML DOM host **57/57** passed. Native
> Windows built-in-effect/composite, physical display/DPI/capture, adapter/debug-layer/live-object,
> and longer performance/soak evidence remain external. Wine is not called physical Windows.
>
> **Findings and carried state.** `REMED-BUILD-019`, `D2D-134`, `D2D-135`, and `D2D-136` are
> resolved. D2D-82's flipped-origin transform is fixed with an eight-point pixel oracle;
> D2D-78's flawed NPOT RT mip generator is removed behind tested rejection. `REMED-GFX-223`
> remains resolved and shared Texture2D authority code is unchanged. `REMED-GFX-224` remains
> **MEDIUM/OPEN**. `REMED-CONTENT-007/-008/-011` remain DONE; `audit/` is untouched.
>
> **Group result.** Authoritative Batch 6 / Group G remains exactly `direct2d`, `llgl`, `metal`,
> `skia` (4 lanes / 356 historical commits). Skia and Direct2D are integrated, so it is **2/4**.
> Pending is exactly `llgl` and `metal`; neither began. Direct2D alone does not complete the group,
> `INTEGRATION_ORDER.md` defines no Batch 6/per-Direct2D checkpoint, and **no checkpoint was
> created**. Next action: owner decision on the next remaining lane. Do not begin it from this
> record.

## Previous — **BATCH 5 CHECKPOINT READY ✅ · SIGNED LOCAL TAG TAKEN** · 18 integrated / 3 deferred pending (2026-08-08, `debian`)

> **The required checkpoint retake passed.** `REMED-CONTENT-007` and `REMED-CONTENT-008` are DONE
> with independent Song, Video, and ContentManager public-caller evidence; bounded same-pattern
> finding `REMED-CONTENT-011` is also DONE. Signed integration test/fix commits `2d795473` and
> `c805fd73` move `integration/post-audit-phase1` from `24bf4786` to `c805fd73` without changing a
> Graphics/backend/audit path.
>
> **Containment result.** Embedded/manifest references must be non-empty and relative; POSIX
> absolute, Windows drive/root/UNC, deep traversal, sibling-prefix, and existing-symlink escapes
> reject before file access. `.`, repeated/mixed separators, and `..` that normalizes inside remain
> accepted; ordinary filenames containing `..` remain valid. In-root paths retain normalized
> root-relative cache identity. Explicit external Content/media bundles retain their established
> API but are confined to their own bundle. Existing-symlink checking is not claimed as a
> race-proof filesystem sandbox.
>
> **Validation.** Final integration HEAD passes containment **46/46**, relevant
> Content/Song/Video **116/116**, Glide portable **78/78**, and the exact HTML DOM native host target
> **57/57**, with ASan + UBSan linked and LeakSanitizer enabled. No CNA-originating report. The
> accepted GDI 19/19 Wine record remains intact; no lane was reopened. Four stash IDs and the
> `audit/` tree hash are unchanged; 18 first-parent lane merges remain; no nineteenth lane began.
>
> **Checkpoint.** Local signed annotated tag `integration/checkpoint-batch5-20260808`, object
> `307c9ad511015c64ce55184cdf0d5ebd7b1cb575`, peels to
> `c805fd737f4321568fba378e8d1b8fe5b5270666` and verifies Good. Nothing was pushed. Full records:
> `integration/BATCH_5_STABILIZATION.md` §7 and `remediation/REMEDIATION_PROGRESS.md`.
>
> **Next in the authoritative order is Direct2D, but Batch 6 / Group G remains unscheduled and
> requires the recorded owner decision.** Do not begin Direct2D, LLGL, or Metal without that
> decision. None was begun by this retake.

## Previous — **`html-dom` ACCEPTED ✅ · BATCH 5 COMPLETE 3/3 · CHECKPOINT BLOCKED** · 18 integrated / 3 pending (2026-08-08, `debian`)

> **HTML DOM landed as signed `--no-ff` merge `24bf4786`** with parents `ba5fa601` and
> `a32977f3`; the merge and `adapt/html-dom` trees are byte-identical. Original branch
> `claude/html-dom-cna-backend-xefzwf` remains unchanged at `8e4e4293`, based at `f5645c64`; its
> sole signed annotated archive still peels exactly to that head. The adaptation is 50 signed
> linear commits: 49 chronological meaningful replays plus one post-audit stabilization commit.
> Six pure Canvas commits and the Canvas-only hunk of one mixed commit were omitted and fully
> accounted for. Full records: **`integration/lanes/html-dom.md`** and
> **`integration/BATCH_5_STABILIZATION.md`**.
>
> **Implementation.** Public identities move token-exact **37 → 38**, adding only genuine
> `HTML_DOM`. This Emscripten-only 2D backend uses pooled browser `<div>` elements and CSS to
> composite the backbuffer over the SDL canvas, private Canvas2D surfaces for bounded
> `RenderTarget2D`, and handwritten `EM_JS` glue. It is not a Canvas, WebGL, EasyGL, Software, or
> Stub alias and has no fallback. Unsupported resource, 3D, effect, render-target, state, sampler,
> and blend variants reject deterministically.
>
> **Runtime and validation.** This host has no Emscripten or Node toolchain, so no adapted-browser
> rebuild/run is claimed. Current native host contracts pass **57/57**, including **57/57** with
> linked ASan/UBSan and leak detection. The unchanged implementation retains the original lane's
> recorded real-browser smoke 69/69, pixel 35/35, stress 10/10, dispose 17/17, host-integration
> 2/2, memory 6/6, and GTest 54/54 evidence. OPENGLES/EasyGL continuity is **109 pass + 1
> intentional skip**; seven focused GDI executables exit 0 through Wine/Xvfb; Glide unit,
> capability, and i686 fake-ABI controls pass; Diligent, Skia, and Sokol changed capability units
> compile under their own backend definitions. `REMED-GFX-223` cache controls remain green.
>
> **Semantics and lifetime.** CNA top-left coordinates map to CSS pixels; per-batch viewport,
> scissor regions, shared sorting, Immediate per-draw flush, z-order, transforms, presentation
> modes, resize, texture pitch, RGBA8 conversion, alpha/blend subset, owner-scoped texture-variant
> LRU, sprite-pool shrink/regrowth, multi-device teardown, and event-listener cleanup are explicitly
> bounded and documented. There is no direct DPR query, so DPR>1 is not independently claimed.
> Custom effects/blends, live-DOM backbuffer readback, depth/stencil, MSAA, MRT, 3D, instancing,
> multistream, wireframe, and anisotropic filtering remain unsupported and truthful.
>
> **Findings.** `HTMLDOM-121/-122/-123` are resolved; no HTML DOM supported-path defect remains.
> `REMED-GFX-223` remains resolved, `REMED-GFX-224` remains MEDIUM/OPEN, and all other carried
> findings keep their existing IDs/states. No shared Texture2D authority code changed.
>
> **Batch result.** Batch 5 remains exactly `glide` → `gdi` → `html-dom`; all three are accepted
> and technical stabilization passes. The inventory is **18/21**, with only `direct2d`, `llgl`, and
> `metal` pending. No nineteenth lane began.
>
> **Checkpoint decision: BLOCKED, no tag.** `INTEGRATION_ORDER.md` explicitly requires the still-
> open HIGH/P1 `REMED-CONTENT-007/-008` path-containment findings to close before the Batch 5
> checkpoint. They are outside this lane and this session's allowed scope, so
> `integration/checkpoint-batch5-20260808` was not created. **Exactly one next task:** close those
> two findings together as the existing bounded Content safety task, then retake the checkpoint
> decision. Do not start that task or any later graphics lane from this record.

## Previous — **`gdi` ACCEPTED ✅ · BATCH 5 OPEN 2/3** · 17 integrated / 4 pending (2026-08-08, EliteBook 840 G9)

> **GDI landed as signed `--no-ff` merge `ba5fa601`** with parents `677f4c59` and `625f4ad5`;
> the merge and `adapt/gdi` trees are byte-identical. The unchanged original `feature/gdi` head is
> `adc9cc2a`, based at `a7a49e3d`; its sole signed annotated archive
> `archive/preintegration/gdi-20260804` still peels exactly to that head. The adaptation is 43
> signed linear commits: 34 chronological replays plus 9 follow-ups. All 34 map 1:1 by range-diff
> (18 `=`, 16 `!`) with matching author/name/email/date/subject metadata and no omission. Full
> record: **`integration/lanes/gdi.md`**.
>
> **Implementation.** This is the Windows-only private CPU Software-2D core presented to a Win32
> `HWND` through classic `SetDIBitsToDevice`/`StretchDIBits`. It has no GDI+, D2D, D3D, GL, or SDL
> Renderer fallback. True capabilities are exactly `StencilBuffer`, `WireFrame`, and
> `MultiSampleAntiAliasing`; 3D, aggregate depth/stencil, MRT, anisotropic filtering, occlusion,
> custom effects, Texture3D, multistream, and instancing remain false. PBR is unsupported and is not
> a capability-enum member.
>
> **Validation.** Historical and current focused GDI matrices both pass **19/19** through Wine;
> current evidence is x64 MinGW GCC 14 Release plus Wine 10/Xvfb. The genuine PE32 i386 allocation
> planners pass **12/12**; this is planner-only i686 evidence. Native Software ASan/UBSan selected
> 151 tests (**149 pass + 2 intentional skips**, zero CNA report), with standalone effects 7/7,
> Additive 29/29, scissor 44/44, render-target 102/102, viewport 19/19, and Texture2D 40/40.
> Leak detection was disabled only because the supervising ptrace boundary makes LeakSanitizer
> unusable. EasyGL runtime controls pass 8/8 and `CnjCacheIsolationTest` 2/2; DX3 passes 1/1; Sokol
> passes 3/3; Diligent, Skia, and Glide current-source probes are compile-only and pass. No physical
> Windows or native-MSVC result is claimed. All compilation was explicitly bounded at two jobs or
> fewer; final current-tree runs used one job under `CPUQuota=50%`.
>
> **Pixel and lifetime result.** Tight top-down RGBA8 reaches a negative-height 32-bit
> `BI_BITFIELDS` DIB with explicit RGB masks. Odd widths, pitch, asymmetric channels, orientation,
> dirty clipping, and short-stride retention pass. Alpha is consumed by CPU blending and the final
> presentation is opaque `SRCCOPY`. No persistent HDC/HBITMAP is retained; repeated cleanup and
> injected `GetDC`/`CreateDIBSection`/`SelectObject` failures pass. `GetGuiResources` skipped under
> Wine, so physical-Windows kernel-object leak absence is not claimed.
>
> **Findings.** New `REMED-GFX-229` through `-233`, `REMED-BUILD-017/-018`, and GDI-054 lifetime
> hardening are resolved for their automated scope. GFX-233 was pre-existing at the integration
> base and is closed by the narrow empty-declaration persistent-buffer fallback; declared,
> multistream, and instanced authority remains intact. No unresolved supported-path GDI defect
> remains. `REMED-GFX-223` is preserved; `REMED-GFX-224` remains MEDIUM/OPEN; `-225` through
> `-228` remain resolved; `REMED-CORE-015` and `REMED-CONTENT-010` remain LOW/OPEN.
>
> **Batch status.** Batch 5 remains exactly `glide` → `gdi` → `html-dom`: Glide and GDI are accepted,
> HTML DOM is pending, and no Batch 5 checkpoint exists. Pending lanes are `html-dom`, `direct2d`,
> `llgl`, and `metal`. **Next is Batch 5 / HTML DOM; do not start it from this reconciliation.**

## Previous — **`glide` ACCEPTED ✅ · BATCH 5 OPEN 1/3** · 16 integrated / 5 pending (2026-08-08, EliteBook 840 G9)

> **Glide landed as signed `--no-ff` merge `677f4c59`**; integration moved
> `0a51f8647` → `677f4c59`. `adapt/glide` is `e891e105` (32 chronological signed replays plus one
> signed stabilization commit) and its tree is byte-identical to the merge. `feature/glide` remains
> unchanged at `2f9b47e1`; annotated archive `archive/preintegration/glide-20260804` still peels to
> that exact head and verifies good. Nothing was pushed; `audit/` and the four stash objects remain
> unchanged. Full record: **`integration/lanes/glide.md`**.
>
> **Identity and runtime.** Public identities move token-exact **35 → 36**, adding only `GLIDE`.
> This is CNA's native 32-bit 3dfx Glide 3.x ABI path, dynamically loading a caller-supplied
> `glide3x.dll`; no OpenGL, EasyGL, SDL_Renderer, or software fallback exists. CNA has no linked
> Glide dependency and pins no emulator. No physical Voodoo hardware or compatibility runtime was
> available, so production rendering is truthfully **build-only/runtime unavailable**. The x86
> fake DLL is a 39-export ABI test double under Wine, not an emulator or rendering oracle.
>
> **Adaptation.** Three conflict stops produced 10 file-conflict events across 9 unique files.
> Current stream-array `GpuDrawParams`, folded ordinary `VertexOffset`, indexed `startIndex`/
> `baseVertex`, declaration validation, duplicate-semantic rejection, deferred lifetime, and
> Texture2D cache authority were preserved. Shared production changes are additive default sampler
> mip and depth/stencil-plane hooks plus their `GraphicsDevice` routing and `ClearOptions`
> complement. `Texture2D` is byte-identical to the prior integration tree.
>
> **Findings.** `REMED-GFX-226` (TMU1 ignored sampler slot 1), `REMED-GFX-227` (deferred sprites
> could survive texture/device teardown), and `REMED-GFX-228` (TMU1 preparation could evict the
> already-validated TMU0 texture) are MEDIUM and resolved in-lane. `REMED-GFX-224` remains OPEN;
> `REMED-GFX-225` remains RESOLVED; `REMED-CORE-015` and `REMED-CONTENT-010` remain LOW/OPEN.
>
> **Validation.** Historical baseline: 65/65 portable tests plus the fake ABI contract; full i686
> CNA configuration blocked in the sibling dependency path (missing i686 ZLIB, then the accepted
> `sharp-runtime` `__int128` target limitation). Adapted: 78/78 portable, 13/13 shared contracts,
> 39-export ABI contract, whole-backend i686 syntax, 78/78 with linked ASan/UBSan and leak detection,
> and five serial OPENGLES Xvfb pixel/state controls. No Glide image result is claimed.
>
> **Batch status.** Batch 5 membership remains exactly `glide` → `gdi` → `html-dom`; GDI and HTML
> DOM remain pending, so no Batch 5 checkpoint was created. No seventeenth lane began.
>
> **Procedural reconciliation.** The historical SDL helper violated the explicit bounded-parallelism
> rule by invoking `cmake --build … --parallel` without a job count; actual parallelism at or below
> eight cannot be claimed. Classification B is accepted as a recorded process deviation because
> the operation supplied only original-branch dependency/configure baseline evidence. Later
> monitored `-j4` builds independently supplied every final Glide engineering gate. The next lane
> is **GDI**; do not start it from this reconciliation and do not start HTML DOM first.

## Previous — **`gl` INTEGRATED ✅ · BATCH 4 CHECKPOINT ✅ READY** · 15 integrated / 6 pending (2026-08-07, EliteBook 840 G9)

> **The cross-repository `feature/gl` lane landed as merge `0a51f8647`** (signed, `--no-ff`,
> merged tree byte-identical to `adapt/gl` — 30 signed commits, worktree `cnaintegration-gl`
> retained), and **Batch 4 was stabilized and its checkpoint taken: local signed annotated tag
> `integration/checkpoint-batch4-20260807` → `0a51f8647`, verified.**
> `integration/post-audit-phase1` moved `1381ff93` → `0a51f864`. **Nothing pushed in any
> repository. `audit/` untouched. The four user stashes are intact** (restored byte-identically
> from dangling objects after an accidental owner-side `git stash clear` mid-session). Full
> records: **`integration/lanes/gl.md`**, **`integration/BATCH_4_STABILIZATION.md`**.
>
> **All nine inventory-§7.4 steps ran in order under direct owner instruction.** MetaGL develop
> `d51fcd7f` → **`c964e736`** and EasyGL develop `62c0a248` → **`9b831dee`** — both as
> **trailer-stripped replays** (trees byte-identical to `d5bc155f`/`b52f6713`): the HISTORY CLEAN
> classification missed `Co-authored-by: Junie` trailers on 15/16 + 5/5 completed commits, the
> cleanliness guarantee is owner-scoped to the newly integrated ranges, published ancestry keeps
> its historical Claude trailers un-rewritten, and the owner's global MetaGL/EasyGL history
> rewrite is a separate post-integration operation. The missing EasyGL archive tag exists
> (`archive/preintegration/easygl-rvc-20260807`). GLB-38 done — a configure references no
> `easy-glrvc`; final chain `CNA -> ../easy-gl @ 9b831dee -> ../meta-gl @ c964e736`.
>
> **Public identities 32 → 35.** `EASYGL` withdrawn from public selection (EasyGL stays internal,
> §7.0); `OPENGLES` (Linux default), `OPENGL33`, `WEBGL1`, `WEBGL2` (Emscripten default) added as
> the four public GL-family backends over one shared implementation. **REMED-GFX-219 RESOLVED**
> (WireFrame true, backed by the shared pixel oracle's own measurement; the designed tripwire arm
> moved as its message instructs). **REMED-GFX-224 remains OPEN and visible.** New pre-existing
> findings, control-proven at the previous head: **`REMED-CORE-015`** (Vector3/Matrix GetHashCode
> signed-overflow UB) and **`REMED-CONTENT-010`** (vendored cgltf misaligned load).
>
> **Validation.** OPENGLES corpus **5906 · 5900 · 0 failed · 6 truthful skips** (first principal
> run of the campaign with zero failures); OPENGL33 corpus identical; the lane's 236/241
> instrument (grown to 293) at **292/293** on both native profiles, the single failure documented
> pre-existing (plan_graphics Task 872); pre-adaptation baseline at the fork reproduced 237/241
> against the recorded 236/241 with the difference explained; WEBGL1/WEBGL2 native rejection
> proven, runtime truthfully unavailable (no Emscripten SDK on this host); 15 per-backend compile
> probes all OK; Sokol focused control 37/37; ASan/UBSan zero lane-originating findings with
> leaks 100 % `libGLX_mesa`-rooted.
>
> **Next: Batch 5 opens with `glide`** (then `gdi`, `html-dom`). Remaining 6: `glide`, `gdi`,
> `html-dom`, `direct2d`, `llgl`, `metal`.

## Previous — **`skia` INTEGRATED ✅** · 14 integrated / 7 pending (2026-08-07, EliteBook 840 G9)

> **`skia` landed as merge `1381ff93`** — signed, `--no-ff`, merged tree byte-identical to
> `adapt/skia` (**151** signed commits, head `a071e1e2`, worktree `cnaintegration-skia` retained).
> CNA's **thirty-second** public backend identity and the first deliberately **2D-only** one.
> **`integration/post-audit-phase1` moved `aa9f3fb5` → `1381ff93`. Nothing was pushed. `audit/`
> untouched. The four user stashes are untouched.** Full record: **`integration/lanes/skia.md`**.
>
> **This is the first lane to be blocked, recorded as blocked, and then merged.** The BLOCKED event
> is preserved above, not rewritten. Two defects stopped it — both in **shared** code, both fixed
> here, and **neither reachable from any Skia test**:
>
> **`REMED-GFX-223` (HIGH, RESOLVED, `9dbdd4cf`).** `Texture2D::gpuOnlyContent_` conflates two
> claims: the weak *"an absent CPU shadow is normal here — fall back to the backend"* and the strong
> *"the live backend is the sole authority, never trust the shadow"*. At the head it only ever meant
> the weak one — **both** of its readers sat inside an `if (!cpuPixels_)` branch — and
> `ReconstructFromCache`, which borrows `RenderTarget2D`'s constructor, inherited it for an ordinary
> cached texture. First bad transition: `gpuOnlyContent_ = true` at `Texture2D.cpp:356` reached from
> `:2703`. **The previous session's diagnosis was right but incomplete:** it did not identify that
> the lane's in-place `SetData` writes through a backend `ContentManager`'s weak cache **shares**
> with every other wrapper — the CNB-33 aliasing `CnjCacheIsolationTests` exists to pin, whose own
> header comment predicted it verbatim. **Clearing the flag alone would have turned both tests green
> and left that aliasing live.** The repair is two-sided: clear the flag in `ReconstructFromCache`,
> and gate the in-place backend update to real render targets. 31 insertions / 17 deletions,
> 13 new regression tests.
>
> **`REMED-GFX-225` (HIGH, RESOLVED, `8bd8bc09`).** `SKIA-149` added
> `virtual int GetSizeEXT() const noexcept` to `ITextureCubeBackend`; four cube backends already had
> a same-named accessor **without** `noexcept`, so each silently became an override with a looser
> exception specification. **`CNA_GRAPHICS_BACKEND=SOKOL` did not build at all.** Fixed with
> `noexcept override` on Sokol, D3D11, D3D12 and D3D9 (the three D3D ones by inspection — none
> builds on this host). **No test run could have found this**: it is a compile error in a backend
> nobody had compiled.
>
> **`REMED-GFX-224` (MEDIUM, OPEN, not a blocker).** An EasyGL render target silently discards
> `SetData`, because `ITextureBackend::UpdatePixels` is a defaulted no-op
> `EasyGLRenderTargetBackend` never overrides. Pre-existing; masked at the head by `SetData`
> replacing a render target's backend and leaving a shadow `GetData` read first.
>
> **Controls, all from the adapted sources.** Skia focused **172/172**; row-stride **8/8**;
> `CnaTests` under `SKIA` **5746 · 5611 · 124 failed** against the fork point's **125** — the figure
> the previous session left explicitly unmeasured, now measured and *better*: **8 fixed, 7 new**,
> and the 7 are exactly the `Ordinary`/`InstancedDrawMultiStream` rows already classified as the
> shared 2D-only class. EasyGL principal control **5912 registered · 5911 passed** (the one failure
> is `easy-gl-resource-smoke-tests`, a sibling-repo binary with **zero** CNA symbols). Sokol
> **37/37** + **34/34**. Diligent **169/169**. **ASan + UBSan: 0 and 0** — the gate the previous
> session never reached — with all 15 382 736 leaked bytes accounted for by three `libGLX_mesa`
> blocks containing no CNA, Skia or SDL frame.
>
> **No checkpoint was taken.** `INTEGRATION_ORDER.md` §3 gives Batch 6 / Group G **four** members —
> `direct2d`, `llgl`, `metal`, `skia` — and three remain, so Skia alone does not complete it. **No
> Batch 4 checkpoint was created or considered:** Batch 4 is `feature/gl` alone and has not been
> started.
>
> **Next: `feature/gl` (Batch 4), not started.** Remaining 7: `direct2d`, `gl`, `gdi`, `glide`,
> `llgl`, `html-dom`, `metal`.

## Previous — **`skia` ADAPTED, VALIDATED, ⛔ NOT MERGED** · still 13 integrated / 8 pending (2026-08-07, EliteBook 840 G9)

> **The Skia lane adapts cleanly and its own backend is in good shape; it is not merged because it
> regresses a control backend.** 141 commits replayed onto `adapt/skia` with **zero interface
> drift** — the third lane after `wicked`/`magnum` to need none, because it forked at `a7a49e3d`,
> 707 commits past the stale-fork set — and the full Skia suite passes **172/172**. Running the
> shared `CnaTests` corpus under **`EASYGL`** from the adapted sources, which this lane had **never**
> been subjected to, found **two regressions in shared `Texture2D` code that merely branches on a
> Skia condition**. One is fixed and verified. The other, **`REMED-GFX-223`**, is open and blocks
> the merge. Full record: **`integration/lanes/skia.md`**.
>
> **`integration/post-audit-phase1` is untouched at `aa9f3fb5`. No merge commit was created.
> Nothing was pushed. `audit/` untouched. The four user stashes are untouched.**
>
> **`REMED-GFX-223` (HIGH, OPEN).** `ContentManager`'s texture cache reconstructs through
> `Texture2D::ReconstructFromCache`, which builds via the `RenderTarget2D` constructor and so
> inherits `gpuOnlyContent_ = true` for an ordinary content texture — a **pre-existing mislabel at
> the head** that was inert because both readers consulted the CPU shadow first. This lane's new
> `SetData` branch now resets that shadow and its reordered `GetData` sends the read to a backend
> that cannot serve it, so `CnjCacheIsolationTest` fails under `EASYGL`. Traced on both trees with
> `CNA_TEXTURE_TRANSFER_TRACE=1`: the head reads `source=cpuPixels_` and passes, the lane reads
> `source=backend` and throws. Recorded with its measurement in `plan_skia.md` rather than repaired
> unverified — the repair changes shared semantics every backend depends on.
>
> **Fixed in-lane:** `IsColorTransferFormatEXT` had narrowed the `Color*` overloads from "any
> 4-byte format" to `SurfaceFormat::Color` alone on every non-Skia backend, withdrawing the working
> `ColorSrgbEXT` route `MouseCursor::FromTexture2D` uses. `MouseCursorTest` is 14/14 again. Also
> `bb8e6430`: the three-way merge had cleanly, silently reverted `REMED-GFX-222`'s own fixture
> update, because the lane rewrote the same region — a conflict resolved correctly by content and
> wrong by meaning, exposed only by running the suite.
>
> **SKIA = a CPU-raster 2D identity, the 32nd, aliasing nothing.** Raster `SkSurface` presented
> through an SDL streaming texture; `ctest -N -L Accelerated` reports **0**. Dependency **Skia
> `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`** (BSD-3-Clause), GN-built outside the tree, six static
> archives, **nothing vendored and no carried patch**. The Ganesh artifact is carried but has no
> `IGraphicsBackend` and is unreachable from backend selection — it is **not** a second identity.
> All three §1.1 obligations paid: an exhaustive **eleven-member** capability switch with no
> `default` arm, truthful `WireFrame=false` whose refusal half is satisfied more strongly than the
> rule asks, and a declaration guard **decided** not-applicable on `stub`'s precedent.
>
> **Sanitizers were never reached** — the gate stops at the EasyGL control. Everything is preserved:
> `adapt/skia` (**148** signed commits, head `75b1b903`), worktree `cnaintegration-skia`, and the
> `cmake-build-skia`/`-pre`/`-easygl` trees.
>
> **Note on batch membership.** `INTEGRATION_ORDER.md` §3 defines **Batch 4 as `feature/gl` alone**
> (cross-repository, gated by C1) and places `skia` in **Batch 6 / Group G**, deferred, "best
> sequenced after modularization". Skia was integrated ahead of that order by direct instruction.
> **No Batch 4 checkpoint was created or considered**, since Batch 4's own member has not been
> started and Skia is not part of it.

## Previous — **BATCH 3 CHECKPOINT ✅ READY · `diligent` INTEGRATED (2 of 2)** · 13 integrated / 8 pending (2026-08-07, EliteBook 840 G9)

> **`diligent` landed by ADAPTATION as merge `aa9f3fb5`** (signed, `--no-ff`, the thirteenth lane;
> adaptation head `27f7dcef`, **70** signed commits, worktree `cnaintegration-diligent` retained),
> and **Batch 3 was stabilized and its checkpoint taken: OUTCOME READY — local signed annotated tag
> `integration/checkpoint-batch3-20260807` → `aa9f3fb51`, created and verified.** **Nothing was
> pushed; no fourteenth lane was begun; `audit/` untouched.** Full records:
> **`integration/lanes/diligent.md`** and **`integration/BATCH_3_STABILIZATION.md`**.
>
> **DILIGENT = the 31st public identity, and the first backend whose native graphics API is chosen
> at RUN TIME**, not by the CMake option — DiligentCore is itself an abstraction over
> D3D11/D3D12/Vulkan/OpenGL/Metal, so CNA sits on two stacked abstraction layers. Selection walks
> `D3D12 → Vulkan → D3D11 → OpenGL` filtered to the engines built, `CNA_DILIGENT_DEVICE` pins one,
> and an unrecognised value **throws** rather than falling back. Measured here on **both** device
> types the platform builds: Vulkan on lavapipe and OpenGL 4.5 on llvmpipe. Dependency
> **DiligentCore `v2.5.6`**, fetched and built from source, **nothing vendored and no carried
> patch**; `~/deps/DiligentCore` at that exact tag serves offline builds. Registration union #10 —
> the campaign's widest, 17 conflicted files on the first commit alone.
>
> **This lane started from a proven baseline.** A pre-adaptation build at its own fork point
> returned **61 registered / 53 passed / 7 failed / 1 skipped**, the seven being precisely the
> lane's own documented open set. **Interface drift against the head was one reference** to the
> removed `GpuDrawParams::instanceVb`, and nothing else.
>
> **Three §1.1 obligations paid at adaptation.** REMED-GFX-201/202: `MultiStreamVertexInput`
> **false**, `Instancing` **true**, exhaustive eleven-member switch with no `default` arm; the
> instanced route reads `FirstInstanceStream()` and honours `InstanceFrequency` (into
> `LayoutElement::InstanceDataStepRate` *and* the pipeline key) and each binding's own
> `VertexOffset`. REMED-GFX-DECL-GUARD: the shared `RequireFaithfulVertexDeclaration()` is
> **reused**, not re-derived — unlike `sokol`, this backend genuinely does infer its layout from the
> buffer stride, which is the mechanism that helper models. REMED-GFX-209: `WireFrame` reported from
> the live device's own `DeviceFeatures::WireframeFill` and measured against the shared oracle.
>
> **Validation, all green:** dedicated suites **78 registered / 69 passed / 8 failed / 1 skipped**,
> with **7 of the 8 being the pre-adaptation baseline's exact set — zero regressions** — and the 8th
> the new instancing test's OpenGL variant joining the same already-root-caused Mesa divisor class
> (`DILIGENT-69`); corpus **5816 / 5800 passed / 8 failed / 7 truthful skips**, with **no
> non-Diligent failure in the run**; sanitizers **0 ASan, 0 CNA-originating UBSan, 0 CNA-owned
> leaks** over a representative 13-harness subset, `detect_leaks=0` control 23/25; **Sokol focused
> control 37/37**; EasyGL continuity **5894 executed, 5893 passed**, its single failure the known
> networking flake, **3/3 green in isolation**.
>
> **Four defects found and fixed in-lane, all adaptation-owned**: a dropped `endif()` in the
> registration union (broke `cmake` configure in *every* configuration), two orphaned `#endif`s
> caught by an explicit balance check at conflict resolution, and a wrong expected-check count in
> the session's own new test. **No lane-owned supported-path defect was found.**
>
> **`SOKOL` was missing from the in-repo `CLAUDE.md` backend list** — the twelfth lane's own
> registration-union gap, found while adding `DILIGENT` beside it and corrected here as a genuine
> Batch 3 stabilization fact. `MAGNUM`'s absence from `README.md` is Batch 2's and was left alone.
>
> **Thermal note for future sessions on this host.** `powerprofilesctl launch -p power-saver -- cmd`
> holds the profile **only for that command**; between commands the machine reverts to `balanced`,
> where this i7-1260P reaches **95–100 °C within ~2 minutes** of load. Every excursion above 84 °C
> this session happened in exactly such a gap. Use a **persistent** `powerprofilesctl set
> power-saver` instead — under it, `-j6`/`-j8` sustain **48–60 °C**. Max parallelism used **`-j8`**,
> once, for the sanitizer build only; eight was never exceeded.
>
> **Next task, exactly one, not begun: open Batch 4.** Eight lanes remain — `direct2d` (owner-frozen),
> `gl` (cross-repository), `gdi`, `glide`, `llgl` (blocked), `skia`, `html-dom`, `metal` (unbuildable
> here). `INTEGRATION_ORDER.md` §2 puts `gdi`/`glide`/`html-dom` in the high-conflict Group F.

## Earlier — **BATCH 3 OPEN · `sokol` INTEGRATED (1 of 2)** · 12 integrated / 9 pending (2026-08-07, EliteBook 840 G9)

> **`sokol` landed by ADAPTATION as merge `37066e45`** (signed, `--no-ff`, the twelfth lane;
> adaptation head `9fb83a99`, **44** signed commits, worktree `cnaintegration-sokol` retained).
> **Nothing was pushed; no other lane was begun; `audit/` untouched; the Batch 3 checkpoint was NOT
> taken** — Batch 3 is `sokol` → `diligent`, and the checkpoint belongs after `diligent`. Full
> record: **`integration/lanes/sokol.md`**.
>
> **SOKOL = the 30th public identity** — sokol_gfx on a **desktop OpenGL 4.1-core** context created
> through `SDL_GL_CreateContext` on the window CNA already owns (`sokol_app` deliberately unused).
> Dependency pinned at `27b49604b19be8cee0dcc6b2bbfe803dd9517585` (zlib/libpng, 2026-07-30),
> **fetched and never vendored, no carried patch**; `~/deps/sokol` at that exact commit serves
> offline builds through CMake's own `FETCHCONTENT_SOURCE_DIR_SOKOL`. `CNA_SOKOL_API=GLCORE` is the
> default and the only implemented value, so backend selection is deterministic. Registration union
> #9: 29 existing identities kept token-exact.
>
> **This lane started from a proven baseline, not an unknown.** Unlike `wicked`/`magnum`, a
> pre-adaptation build straight from the historical worktree at `261ea700` reproduced the lane's own
> recorded results exactly — `Sokol_Smoke` 13/13, `Sokol_2D` 15/15, `Sokol_3D` 10/10, `Sokol_Lit3D`
> 10/10, **48 checks / 0 failures** — so everything measured afterwards is attributable to
> adaptation. **Interface drift against 376 commits of head movement was two references** to the
> removed `GpuDrawParams::instanceVb`, and nothing else.
>
> **Three §1.1 obligations paid at adaptation, one of which changed an answer.**
> REMED-GFX-201/202: `MultiStreamVertexInput` **false**, `Instancing` **true**, exhaustive
> eleven-member switch with no `default` arm; both routes refuse a split declaration or a second
> per-instance stream outright (`InstancedDrawMultiStreamTest`+`OrdinaryDrawMultiStreamTest` **8/8**,
> both deterministic-rejection cases included). REMED-GFX-DECL-GUARD: a header-only draw-time guard
> that **deliberately does not reuse** the shared stride-inferring helper — this backend programs its
> layout from the declaration's own offsets and formats, so the stride-table rule would refuse
> correct draws. **REMED-GFX-209: `WireFrame` corrected `false` → `true` on measurement** — the
> shared asymmetric-triangle oracle read `interior 0/1089` under WireFrame against `1089/1089` under
> Solid with all three edge probes lit; it does not copy EasyGL's `false`, the one report this
> repository already records as wrong (`REMED-GFX-219`).
>
> **Validation on Mesa 25.0.7 llvmpipe (LLVM 19.1.7), a real GL 4.5 core context on Xvfb `:101`,
> all green:** dedicated suites **37/37**; corpus **5776 registered · 5768 passed · 1 failed · 7
> truthful skips · 0 aborts** (697 s), the one failure being the pre-existing networking Outcome-C
> flake (**3/3 green** re-run in isolation, and passed outright in the discovery run); sanitizers
> **0 ASan + 0 UBSan** across all 37 suites with every leak rooted in `libGLX_mesa` and
> `detect_leaks=0` control **37/37**; EasyGL control from the adapted sources **6190 registered,
> 5894 executed, 5894 passed, 0 genuine failures, 7 skips**.
>
> **Three defects found and fixed in-lane**: a dropped `#endif` in a conflict resolution (adaptation-
> owned, caught by the first `CnaTests` build); a missing `ExpectedNameFor()` arm in the identity
> guard (adaptation-owned, caught by the first corpus run — the same gap class D3D9/DX2/OPENGL1/
> WICKED already record); and a raw-`new`ed `GraphicsDeviceManager` in one harness (lane-owned,
> caught by ASan, the only file in `examples/` not using the `unique_ptr` idiom).
>
> **Two pre-existing conditions recorded, neither caused by this lane and neither fixed here:** the
> EasyGL *dedicated harnesses* do not compile in this environment (`PixelTestGame.hpp` cannot
> resolve `SDL3/SDL.h` under the EasyGL configuration) — **control-proven identical** on the
> pre-Sokol tree at the integration head, which is why the EasyGL instrument is its gtest corpus;
> and **`MAGNUM` is missing from `README.md`'s `CNA_GRAPHICS_BACKEND` list**, a gap in the eleventh
> lane's own registration union noticed while adding the SOKOL entry beside it.
>
> **Build trees (owner-designated partition `/media/robertvokac/claude/tmp/cna/`):** created
> `cmake-build-sokol-pre` (711 M, the pre-adaptation baseline), `cmake-build-sokol` (2.9 G),
> `cmake-build-sokol-asan` (6.4 G) and `cmake-build-sokol-easygl` (control); reused the shared
> `ccache/` and `~/deps` (sokol cloned once at its pin, 11 M). Max parallelism **`-j6`**; the 8-job
> ceiling was never approached. Package id 0 stayed **50–61 °C** throughout under
> `powerprofilesctl launch -p power-saver`; RAM never dropped below ~10.8 G available and swap use
> never moved off its 521 M starting value.
>
> **Next task, exactly one, not begun: the `diligent` lane** — Batch 3's second and last. Only after
> it does the Batch 3 checkpoint become due.

## Previous — **BATCH 2 CHECKPOINT ✅ ACCEPTED (reconciled) · `WICKED-80` RESOLVED** · 11 integrated / 10 pending (2026-08-06, EliteBook 840 G9)

> **`WICKED-80` is resolved and the Batch 2 checkpoint was retaken: OUTCOME READY — local signed
> annotated tag `integration/checkpoint-batch2-20260806` created and verified, nothing pushed.**
> Full record: **`integration/BATCH_2_STABILIZATION.md` §12** (the §11 BLOCKED decision stands
> unchanged as history). First CNA session on the migrated HP EliteBook 840 G9; migration
> validation passed every git-integrity item (HEADs `cbdab0c5`/`7dc2be5b`, 11 lane merges, tags +
> signatures Good, four stashes untouched, deps pins exact). **Deviation: no CNA build tree
> survived the migration** — both preserved reproducer directories included; the probes were
> rebuilt from their documented specs. Fresh trees live on the owner-designated build partition
> **`/media/robertvokac/claude/tmp/cna/`** (`cmake-build-wicked` + `wicked-repro/` evidence,
> `cmake-build-wicked-asan`, `cmake-build-magnum`, `cmake-build-noxna`, shared `ccache/` — export
> `CCACHE_DIR=/media/robertvokac/claude/tmp/cna/ccache` or everything recompiles cold). The
> in-repo `.sdl-prebuilt-Linux-x86_64` survived and is reused.
>
> **Ownership settled by the prescribed raw control: classification B — pinned upstream Wicked
> defect.** `GraphicsDevice_Vulkan::CreateTexture` sizes UPLOAD/READBACK staging buffers tight
> (`ComputeTextureMemorySizeInBytes`) while the mapped layout and `CopyTexture` addressing consume
> `optimalBufferCopyRowPitchAlignment`-aligned pitches (128 here) — every narrow staging transfer
> addressed out of bounds (`VUID-…CopyBufferToImage-pRegions-00171`/`…ImageToBuffer-00183`,
> CNA-free on lavapipe AND Intel ANV; 5×5×3 = 300-byte buffer vs 1920-byte footprint). Corruption
> was a suballocation-adjacency lottery — which also explains WICKED-79's "two staged copies
> interfere" and covered narrow 2D/cube/mip staging. **Fix: the third carried patch
> `cmake/patches/wicked-staging-footprint.patch`** (applies cleanly on pristine `27c0df16` after
> SDL3+teardown; auto-applied/marker-checked by `ThirdPartyWicked.cmake`). **Regression:
> `Wicked_Texture3DStagedTransfer`** — byte-exact index-encoded matrix; its sequenced narrow leg
> fails 3/3 on the pre-fix engine (fixture-per-test legs pass pre-fix — measured, so the sequenced
> leg is the load-bearing discriminator); 11/11 ×3 with the fix.
>
> **Validation, all green:** WICKED corpus **5789 · 5783 · 0 failed · 6 skips · 0 aborts**
> (1700 s — the campaign's first zero-failure corpus on this backend; networking + audio classes
> passed naturally, classifications unchanged); W77 5/5 · W78 6/6 · W79 carriers 13/13 · W80
> 11/11; probes CNA 14/14 ×3 and raw 18/18 post-fix (zero under-allocation, zero OOB VUIDs; 3
> lavapipe-only pre-existing `pRegions-00173` overlap reports recorded); Magnum controls **8/8**;
> EasyGL continuity **5910 · run 1: 5903/1/6 (the failure isolated 3/3 green — §6.1 blip class) ·
> run 2: 5904/0/6**; sanitizers **0 ASan + 0 UBSan CNA-originating**, driver-rooted leaks only,
> `detect_leaks=0` controls 5/5. Registration arithmetic: 5789 = official 5780 + 1 (GFX-222
> ceiling, name-verified) + 12 (W80 suite, name-verified) − 4; continuity 5910 = 5913 + 1 − 4 —
> the identical −4 in both configs from git-identical sources is a host-conditional discovery
> delta of the migrated instrument (ThinkPad name lists did not survive; this host's lists are
> preserved). Residual classes unchanged: networking Outcome C, wall-clock audio, Task 872,
> easy-gl sibling, `REMED-GFX-221` LOW, **`CONTENT-007`/`-008` OPEN HIGH/P1**.
>
> **EliteBook host notes:** cooling is healthy but HP's `balanced` profile turbos P-cores to
> 95–100 °C at any load — run every heavy command under
> `powerprofilesctl launch -p power-saver -- …` (plain `set` reverted once mid-session); `-j6`
> then holds 50–76 °C. Max parallelism used `-j6`; the 8-job ceiling was never exceeded; peak RAM
> comfortable (≥11 G available throughout); GPU tests serial on Xvfb `:101` + lavapipe.
>
> **Nothing was pushed; no twelfth lane begun; `audit/` untouched; published history not
> rewritten; the four user stashes untouched. Direct2D stays owner-frozen for later completion
> before modularization; OpenVG + SVG DOM remain planned texture-only 2D backends; FNA3D remains
> a proposed additional public wrapper backend, not yet tasked; `feature/gl` remains
> MetaGL → EasyGL → CNA with EasyGL hidden; modularization only after all required integrations,
> backend completion/expansion and final stabilization.**
>
> **Migration-gate reconciliation (later the same day, code-free): Batch 2 ACCEPTED WITH RECORDED
> MIGRATION DEVIATION.** Full record: **`integration/BATCH_2_STABILIZATION.md` §13**. The Phase 0
> gate that opened the retake session required the retained `WICKED-80` reproducer and retained raw
> evidence and prescribed `MIGRATION BLOCKED` + stop on failure; both directories were absent, so
> **that instruction should have caused MIGRATION BLOCKED at that moment** rather than a self-granted
> deviation — recorded permanently as process non-conformance. Separately adjudicated: the code is
> correct on its own merits. **No unique source was lost** — everything defining the checkpoint
> (`wicked-staging-footprint.patch`, the `cna_wicked_check_staging_footprint_fix` gate, the 362-line
> `WickedTexture3DStagedTransferTest.cpp`, the docs) is committed in `ebd04ae3`; the losses were
> untracked derived build output, with the ThinkPad raw logs the only genuinely lost evidence class
> (**C**). The retained EliteBook evidence **independently proves `WICKED-80`** — the CNA-free raw
> control, both VUIDs, the 300-vs-1920 arithmetic, the pre-fix 3/3 sequenced discriminator, the
> post-fix 18/18 raw control, the 0-failure corpus and the CNA-clean sanitizer logs are all
> post-migration artifacts, none routed through ThinkPad material. Two sub-items are **C**: the
> Intel ANV transcript and the standalone post-fix CNA-probe log — the ANV fact was re-verified
> instead (`optimalBufferCopyRowPitchAlignment = 0x80` on **both** Iris Xe/ANV and llvmpipe, so the
> under-allocation arithmetic holds on ANV by construction), and the post-fix CNA leg is superseded
> by the committed 11/11 regression inside the zero-failure corpus. **The existing tag was not
> moved, deleted or recreated, and no replacement tag was created.** Caution: the retained logs live
> on the untracked build partition and remain exposed to the same loss class — commit load-bearing
> raw observations when measured.
>
> **Next task, exactly one, not begun: Batch 3 — the `sokol` lane** (then `diligent`).
> **Recommended: Opus first** for the sokol lane read/adaptation, **escalate to Fable on
> evidence** (the campaign's standing model guidance for lane work; this session's Fable+max was
> for the copy-footprint arithmetic and fresh-host gate).

## Previous — **BATCH 2 STABILIZED · checkpoint BLOCKED on `WICKED-80`** · 11 integrated / 10 pending (2026-08-06)

> **The Batch 2 stabilization ran to completion on the integrated HEAD and its checkpoint
> decision is OUTCOME B — BLOCKED, no tag created.** Full record:
> **`integration/BATCH_2_STABILIZATION.md`**. Nothing was pushed; no twelfth lane was begun;
> `audit/` untouched; published history not rewritten; the four user stashes untouched.
>
> **Everything except one gate passed.** Both lanes' provenance re-verified clean (10→17 and
> 13→19, all `U`, range-diffs 1:1, merged trees byte-identical, archive tags and original refs
> unchanged; the wicked card's citation sweep found zero stale SHAs). Zero Batch 2 integration
> regressions on every instrument: Wicked corpus **5780 · 5771 · 3 failed (networking flip +
> two audio-class) · 6 skips · 0 aborts**; Magnum corpus **5843 · 5830 · 7 failed (networking +
> six audio-class) · 6 skips**; EasyGL full ctest **6212 · 6196 · 9 failed · 7 skips** and the
> 5913-case continuity instrument **5913 · 5906 · 1 failed (networking) · 6 skips — the exact
> Batch-1-comparable set, zero regressions**. The 5913→6212 delta is an instrument change,
> derived exactly (5913 gtest cases + 292 `EasyGL_*` + 3 easy-gl sibling + 4 named others).
> Probes 17/17 and 4/4; sanitizers zero CNA-originating findings with driver-rooted leaks and
> `detect_leaks=0` controls green.
>
> **The three newly measured EasyGL failures were adjudicated individually:**
> `EasyGL_DeviceValidation` = **REMED-GFX-222** (GFX-039 over-reached its own "per FNA" charter;
> **discovered and resolved in-stabilization** — fix on the integration branch, plan_postaudit.md
> §19); `EasyGL_GraphicsDevice_ReferenceStencil` = the pre-existing **documented known failure
> Task 872** (AUDIT.md:128), carried visible; `easy-gl-resource-smoke-tests` = **upstream easy-gl
> defect** (its own mock-GL assert, `SmokeResourceTests.cpp:336`, repo unmoved since 07-19),
> recorded with reproducer, sibling untouched. The wall-clock audio class spent the afternoon in
> a measured environmental failure window (PipeWire sink SUSPENDED; same binaries passed the same
> tests in the morning run and in-process) — classification unchanged, tests unmodified.
>
> **The blocker: `WICKED-80`** — the stabilization's new sanitized narrow-width transfer probe
> found `Texture3D` staged transfers corrupting dimension-dependent tail rows (recycled staging
> bytes where the copy never wrote; deterministic per allocation sequence; invisible to all 13
> corpus transfer tests; ASan-silent because the bytes are in-bounds). Latent lane content, not
> an integration regression — but an open production defect on a supported path, so the tag was
> withheld under the same literal criterion that blocked Batch 1 on REMED-GFX-220. Reproducer
> preserved in `cnaintegration/cmake-build-wicked/wicked-repro/`; OPEN row in `plan_wicked.md`.
>
> **Next task, exactly one, not begun — owner-decided: resolve `WICKED-80` in the next session
> on the new machine** (the /rv tree migrates to the HP EliteBook 840 G9 first; the T14 goes to
> cooling service). First step: the raw-`wi::graphics` control from the preserved reproducer to
> settle CNA-versus-upstream ownership, then the bounded fix, probe + Wicked corpus re-run, and
> re-take the Batch 2 checkpoint decision. After it: Batch 3 (`sokol` → `diligent`).
> **Recommended: Fable, effort max** for the WICKED-80 session (GPU copy-footprint arithmetic
> plus a fresh-host validation gate); Batch 3's `sokol` afterwards: Opus first, escalate on
> evidence.

## Previous — **BATCH 2 ✅ COMPLETE · `magnum` INTEGRATED (2 of 2)** · 11 integrated / 10 pending (2026-08-06)

> **`magnum` landed by ADAPTATION as merge `e7d46c4c`** (signed, `--no-ff`, eleventh lane;
> adaptation head `b7fe9b24`, 19 signed commits, worktree `cnaintegration-magnum` retained).
> **Nothing was pushed; no other lane was begun; `audit/` untouched; Batch 2 stabilization NOT
> begun.** Full record: **`integration/lanes/magnum.md`**.
>
> **The lane's first-ever build and execution resolved UNKNOWN to green.** MAGNUM = the 29th
> public identity, Magnum::GL typed wrappers over desktop GL 3.3 core on the shared SDL3 window;
> Corrade pin `783e4e48` + Magnum pin `5a742464`, both MIT and both proven = upstream master tips
> (`~/deps` clones; offline `FETCHCONTENT_SOURCE_DIR_*` route verified). Compile probe: **zero
> drift** (same post-drift fork as wicked). Registration union #8: 28 identities kept token-exact,
> MAGNUM added. §1.1 proven at runtime: 17/17 guard/capability probe, WICKED-78 teardown class
> measured absent (4/4 lifecycle legs).
>
> **Two real defects found by validation, both fixed in-lane:** the adaptation's own
> declaration-guard compared split multi-stream declarations in the wrong space (armed shared
> oracles caught it: 9 failures → 9/9); and **MAGNUM-65** — the sprite flush sized its Corrade
> `ArrayView<const void>` views in bytes where the typed-pointer constructor takes ELEMENT counts
> and scales by `sizeof(T)`, a `sizeof(Vertex)`-fold heap overread on every flush since the
> lane's first commit that **rendered correctly everywhere** (GL stored the oversized copy) until
> radeonsi's allocator faulted it (deterministic Guide-test SIGSEGV, three coredumps) and ASan
> flagged it on the first sanitized flush. **Pixel oracles cannot see a defect that renders
> correctly; the sanitizer leg caught it.**
>
> **Official corpus (fixed content): 5843 · 5835 · 2 failed · 6 truthful skips · 0 aborts**; both
> failures control-classified pre-existing (networking Outcome C; the wall-clock audio class —
> 2/12 shuffling control failures on the pre-Magnum principal binary). ASan+UBSan 8/8 suites:
> zero findings post-fix, leaks 100 % `libGLX_mesa`-rooted, `detect_leaks=0` controls green.
> **Principal EasyGL control at the merged head: 6212 · 6203 · 5 failed · 4 skipped — zero
> regressions in the baseline-comparable range** (its two failures = the known networking and
> audio classes). The fresh reconfigure surfaced +299 never-measured EasyGL dedicated
> registrations; three of them fail for EasyGL-owned/upstream reasons orthogonal to this lane
> (`EasyGL_DeviceValidation` SetVertexBuffers(16) not throwing,
> `EasyGL_GraphicsDevice_ReferenceStencil` reference-override not rejected,
> `easy-gl-resource-smoke-tests` asserting inside the easy-gl sibling project) — **all three are
> Batch 2 stabilization inbox items**, alongside the citation sweep the wicked card handed over.
> Session thermal note: the machine could not hold one full-boost core
> (88.8 °C in 19 s); all heavy work ran under a proven `systemd-run` CPUQuota=40 % scope
> (58–73 °C), max parallelism `-j2` momentarily, `-j1`+quota otherwise; corpus run 1 unwittingly
> inherited `DISPLAY=:0` for tests without a display property (recorded as a breach; runs 2/3
> forced `:101`).
>
> **Next task, exactly one, not begun: the Batch 2 stabilization checkpoint** — re-derive the
> integrated/pending counts, run the consolidated baseline, decide and take the
> `integration/checkpoint-batch2-<date>` tag. The `ext`-renumbering citation sweep handed to this
> checkpoint by the wicked card and the "rebase 24" phrasing corrections are its inbox. After it:
> Batch 3 (`sokol` → `diligent`). **Recommended: Fable, effort max** for the checkpoint (it
> weighs evidence across eleven lanes); Batch 3's `sokol` is a mixed-history medium backend —
> Opus first, escalate on evidence.

## Previous — **BATCH 2 · `wicked` ✅ INTEGRATED (1 of 2)** · 10 integrated / 11 pending (2026-08-05)

> **The two blockers below were repaired in-lane the same day, and the lane merged as
> `683a00a5`** (signed, `--no-ff`, tenth lane; adaptation head `97d5a644`, 17 signed commits).
> **Nothing was pushed; Magnum was not begun; `audit/` untouched.** Full record:
> **`integration/lanes/wicked.md` §15**.
>
> **`WICKED-77`** was the instanced route dropping the geometry stream's whole `VertexOffset` at
> bind time (that route deliberately does not fold it into `baseVertex`); one binding-site fix,
> five-case regression pinned. **`WICKED-78` was upstream twice over** at pin `27c0df16`: the
> Vulkan device destructor never destroys its three null images (VMA asserts on every
> never-rendering device) and never frees its pooled command lists (a drawing device leaked its
> whole `VkInstance`/`VkDevice`/allocator instead — masking the assertion). Fixed by a second
> carried dependency patch, `wicked-device-teardown.patch`, proven with a CNA-free reproducer.
>
> **The first full corpus run this unblocked found two more, also fixed in-lane:** `WICKED-79`
> (staged uploads smeared at narrow widths — upstream repacks initial data at tight pitches while
> `CopyTexture` reads the aligned mapped ones; staging is now written through its own mapped
> layout, one submit per staged upload) and the lane's backend-local test directory globbed into
> every OTHER backend's `CnaTests` (caught by the principal control; excluded by the glob file's
> own convention). Plus three shared contract tables gained truthful WICKED arms.
>
> **Validation at the merged content:** corpus **5780 · 5774 · 0 failed · 6 truthful skips**
> (no abort — the corpus was UNRUNNABLE under this backend before `WICKED-78`); dedicated suites
> 14/14 · 6/6 · 5/5 · smoke; ASan+UBSan **zero CNA-originating findings** (vptr kept via
> upstream's `WICKED_ENABLE_RTTI=ON`), leaks 100 % `libvulkan_lvp`-rooted with `detect_leaks=0`
> controls; principal EasyGL **5913/5907/0/6 — exactly the Batch 1 baseline**. Networking
> Outcome C: incidentally passed; remains open as a class. Hardware/real-display verification
> (`WICKED-18`/`74`) remains the lane's declared open boundary — everything ran on lavapipe.
>
> **Next task, exactly one, not begun: `magnum`** — Batch 2's second lane (13 own commits,
> `GD`+`IGB`, total history recreation, NEEDS VALIDATION). Re-fetch and re-derive its row first;
> expect the C2 behind-count to have grown past 230. **Recommended: Fable, effort max** — the lane
> is small but touches two shared interfaces and has never been built; the Wicked precedent says
> first execution is where the real defects surface.

## Previous — **BATCH 2 · `wicked` ⛔ MERGE BLOCKED** · still 9 integrated / 12 pending (2026-08-05)

> **The adaptation is complete and clean. The backend is not ready, and it was not merged.**
> `adapt/wicked` holds **12 signed commits** (10 replayed originals + 1 post-audit obligation + 1
> oracle arming), worktree `cnaintegration-wicked`, all preserved. **No merge, no push, no tenth
> lane. `audit/` untouched.** Full record: **`integration/lanes/wicked.md`**.
>
> **The "24-commit history recreation" classification below was wrong, and the correction matters.**
> Measured: the lane has **10** own commits (`2338b44f..91d8587e`). The 24 is the *behind-count* to
> the phase-1 checkpoint — the `behind` half of `git rev-list --left-right --count`, read as if it
> were work. Full arithmetic: **755 inherited (already integrated) + 10 own = 765 ahead of
> `develop`**. And 24 is itself stale: after Batch 0 and Batch 1 the lane is **230 behind the head
> it merges into**, not 24.
>
> **What this session established that nobody had.** The backend had never been checked out, built
> or run. It now **builds against a real Wicked Engine (MIT, pin `27c0df16`, verified = upstream
> master tip), creates a real Vulkan device, and compiles all 22 of its HLSL shaders** — falsifying
> the original commits' own "nothing run on real hardware / no shader has seen a compiler" caveat.
> `Wicked_PipelineKey` **14/14**; `cna_demo_2d --smoke 3` **4/4**; a bounded 1-TU probe **14/14**
> proving the declaration guard refuses correctly, does **not** over-refuse, and that all 11
> capabilities answer truthfully.
>
> **Then two independent production defects appeared on first contact with the shared oracles:**
>
> | ID | Severity | What |
> |---|---|---|
> | **`WICKED-77`** | HIGH | Instanced draws ignore the geometry `VertexOffset` — the 4th record's quad never appears, while the identical data renders correctly through the ordinary indexed route |
> | **`WICKED-78`** | HIGH, **validation-blocking** | `GraphicsDevice` teardown leaves GPU allocations live; Wicked's VMA assertion fires and **aborts the process**. Deterministic on a single device. Every shared test constructs a device, so **`CnaTests` cannot complete under this backend at all** |
>
> **`WICKED-78` is why this is BLOCKED rather than merged with a boundary.** It is not a missing
> measurement — it is a reproducible abort that contradicts the resource-disposal contract.
>
> **A second, unrelated boundary is declared rather than hidden:** `CnaTests` reached only 244 of
> 1047 objects. The machine carried sustained external load (load ≈ 2 with this session's build
> fully stopped); a thermal regulator over this session's own process group still saw **90.5 °C**
> peaks, and finishing would have meant knowingly exceeding the 84 °C ceiling for ~2 hours. No
> sanitizer tree and no principal control were run. **None of it is claimed.**
>
> **Next task, exactly one, not begun:** decide `wicked`'s disposition — fix `WICKED-77`/`-78` in
> the preserved worktree, or move the lane to **Group G** per this card's own criterion 8. Either
> needs a machine with real thermal headroom. **Recommended: Fable, effort max** — the remaining
> work is GPU resource-lifetime debugging, not the bounded replay this session performed.

## Previous — **BATCH 1 COMPLETE AND STABILIZED · ✅ READY** · 9 integrated / 12 pending (2026-08-05)

> **`REMED-GFX-220` is fixed; the Batch 1 checkpoint decision was retaken and the tag created.**
> Signed annotated tag **`integration/checkpoint-batch1-20260805`** on
> `integration/post-audit-phase1`, **local only, not pushed**. No tenth lane was begun; `audit/`
> untouched; no history was rewritten. Full record: **`integration/BATCH_1_STABILIZATION.md` §11**
> (the earlier BLOCKED decision remains in §10, unrevised).
>
> **The blocker is closed, and it was never ours.** A static initialization order fiasco:
> `BlendState.cpp`'s four namespace-scope presets copied `Color::White` from another translation
> unit. Introduced **2026-06-06** by `2345f8fc`, proven an ancestor of the phase-1 checkpoint.
> Fixed by constructing the value in place — `blendFactor_(255, 255, 255, 255)`, which packs to the
> byte-identical `0xFFFFFFFF`. Ticket: `plan_postaudit.md` §17.
>
> **Two things the original ticket got wrong, found by not trusting it.** Its suggested fix
> `Color(UInt32{0xFFFFFFFFU})` **does not compile** — that constructor is private. And the wrong
> *value* it recorded as "not proven" **is now proven**: a probe that forces the link order (the same
> source linked twice, `BlendState.cpp.o` ahead of and behind `Color.cpp.o`) reads `0x00000000` from
> all four presets pre-fix and `0xFFFFFFFF` post-fix. No `gdb` required. Decoding `.init_array`
> showed the hazard sits in **exactly one of 38** OpenGL 1 binaries, which is why one latent defect
> produced one UBSan line.
>
> **Re-validated:** GL1 reproducer clean **with its hazardous link order still intact**; GL1 matrix
> 38/38 with **0 UBSan (was 1)**, 0 ASan, all 114 leaks in `libGLX_mesa.so.0`; Batch 1 dedicated
> **119/119**; principal EasyGL **5913 / 5907 / 6 / 0** — exactly +1 registered/+1 passed versus the
> 5912 baseline, being the one new regression test; plus two sanitizer controls, one of them the
> fixed code under ASan+UBSan *in the hazardous order itself*.
>
> **Networking is unchanged.** It **passed** this run — which is exactly what a ~50 % coin flip does
> half the time and is **not** evidence the cause was fixed. Still **Outcome C**, unresolved, not
> modified, skipped or weakened.
>
> **One new finding, non-blocking: `REMED-GFX-221`** (`plan_postaudit.md` §18) — `GestureDetector.cpp`
> statics copy `Vector2::Zero` cross-unit, found by the same-pattern scan. LOW: `Vector2` is not
> polymorphic so no sanitizer can see it, and `Vector2::Zero` is `(0,0)`, which is what the zeroed
> `.bss` already holds — latent UB with no reachable wrong value. Deliberately not folded into
> `REMED-GFX-220`.
>
> **Next task, exactly one, not begun:** the **Wicked** lane (re-fetch and re-derive it first).
> Recommended: **Opus, effort max, fresh context, no ultracode** — escalate to Fable only if fresh
> inspection proves it broader than its current 24-commit history-recreation classification.
>
> **Superseded 2026-08-05 — and the classification in that sentence was wrong.** The lane is a
> **10**-commit history recreation; 24 was the behind-count to the phase-1 checkpoint (now 230
> against the current head). Opus was the right call and the escalation trigger never fired: the
> lane is bounded and additive, and its compile probe found **zero** interface drift. It is
> nonetheless **MERGE BLOCKED** on two runtime defects — see the CURRENT section above.

---

## Previous — **BATCH 1 ✅ COMPLETE · `opengl2` INTEGRATED (5 of 5)** · 9 integrated / 12 pending (2026-08-05)

> **STATUS: DONE for this lane — and Batch 1 closes.** `feature/opengl2` landed by
> **ADAPTATION** as merge **`9e6d62ed`** (signed, `--no-ff`, parents `c0876fca` + `289410a6`).
> **Nothing was pushed.** Full record: **`integration/lanes/opengl2.md`**.
>
> ### HISTORY CLEAN re-verified — and the narrative sweep needed to go multiline
>
> 40/40 maintainer PGP, zero trailers, zero attribution, exactly as inventoried. **Nine** commit
> bodies carried session narrative (opengl1 had three) and **two were caught only by a
> multiline-aware sweep** — `this\nsession` wrapped across a line defeats a line-based grep.
> Reworded at replay, patches untouched; "(plan_opengl2.md session N)" subject citations kept
> (the plan is organized by Session-N headings — factual references, not narration). Adapted as
> **47 signed commits**: 40 replayed (27 byte-identical by range-diff, 0 lost, all 40
> TRANSFERRED) + interface adaptation + capability + shared-table arming + build fix + two
> harness-contract adaptations + the production fix below + docs.
>
> ### The probe found 14 drifts — two of them the lane's OWN interface additions
>
> The familiar set (descriptor SetRenderTargets, BlendWriteState, six void→bool readbacks,
> preserveContents, VertexDeclaration propagation, fc0dd2a2 unified instanced transport, FNA
> fog vector consumed directly — shader backend, post-skin in the two skinned programs) plus
> `IGraphicsBackend::GetDefaultViewportRect` (its real Letterbox/Overscan/Stretch machinery)
> and **`GraphicsCapability::Instancing` — the enum's 11th member**, restored by the lane's own
> replayed commits. Growing the enum obligated **four truthful arms in other backends**:
> OPENGL4 true, OPENGLES1 false (its surviving `default: return true` would have falsely
> claimed instancing — the ES1 hazard's fourth appearance), OPENGL1 false, Software false; all
> 22 other identities measured already-truthful by shape. Also paid: **software base-vertex**
> (GL 2.1 has no glDrawElementsBaseVertex; the head folds real offsets into baseVertex, which
> the lane's own comment admitted silently falling back to 0) and §1.1's guard satisfied **by
> translation** (this backend name-binds custom declarations faithfully — its tested Task-1080
> capability — so the refusal-style guard would delete working draws).
>
> ### The campaign's first genuine new production finding — found by the armed oracle
>
> **Both RenderTarget2D round trips rendered vertically flipped** (GetData AND
> sampled-RT-to-screen — the post-processing round trip every XNA game uses), masked by the
> lane's orientation-insensitive RT assertions, exposed by the newly-armed shared wireframe
> pixel oracle ("edge BC missing" → frame dump → two probe measurements). Fixed in-lane
> (`289410a6`) as FNA's own convention: render-time clip-Y flip while a 2D target is bound,
> glFrontFace winding compensation, direct viewport/scissor/ReadBackbuffer mapping, cube faces
> deliberately excluded. **The three prior lanes' oracle-arming precedent is what caught it.**
>
> **The registration union, a sixth time** — all 26 identities kept exact token counts,
> `OPENGL2` added as the 27th (+5 tokens in BackendSelection.cmake).
>
> **Validation** on the real llvmpipe `4.5 (Compatibility Profile)` context (`:101`) — the
> backend's own path is GL 2.1 entry points + GLSL 1.10 **by construction** (zero `#version`
> directives): **48/48** lane suites (first run 44/48 — the predicted GFX-165 harness class);
> `CnaTests` **5737 · 5730 · 6 skipped · 1 failed** (the networking flake — now **2/6 in
> isolation**, a worsening trend for the stabilization watch item); **ASan/UBSan over eleven
> suites: zero findings**, every leak libGLX_mesa-rooted, detect_leaks=0 control all-green.
> **EasyGL principal control at the merged head: 5912 · 5904 · 6 · 2 — zero regressions**
> (both failures are the two documented environmental classes; the x11 blip's victim 3/3 in
> isolation).
>
> ### Next — **Batch 1 stabilization checkpoint. SELECTED, NOT BEGUN.**
>
> The four carry-forward items now have owners-in-waiting: the README compact-selector
> `OPENGLES1` omission, the `NameMatchesTypeForEveryBackend` missing arms (every backend since
> SDL_GPU), the networking flake (now with a measured worsening trend: ES1-era 3/3 → GL1-era
> 2/3 → today 2/6 in isolation), and the consolidated Batch-1 full-tree baseline across all
> five GL-family backends plus a signed `integration/checkpoint-batch1-<date>` tag.
> **Model recommendation: Fable** — the checkpoint is measurement, table-arming and
> documentation, with no history recreation and no contested design; reserve Opus for Batch 2's
> rebase-first lanes (`wicked`/`magnum`) if their NEEDS-VALIDATION status turns contentious.
>
> **Carried forward unchanged:** `REMED-CONTENT-007`/`-008` **OPEN, HIGH/P1** — `opengl2` adds
> no path-resolution code and touches none of their files. Direct2D **OWNER-FROZEN, FROZEN
> INCOMPLETE/EXPERIMENTAL**. `feature/gl` order MetaGL → EasyGL → CNA, EasyGL
> **internal/hidden**, public backends exactly OpenGL ES 3 / OpenGL 3 / WebGL 1 / WebGL 2 —
> the four integrated GL lanes (`OPENGLES1`/`OPENGL4`/`OPENGL1`/`OPENGL2`) are independent
> public backends, not part of that set. Modularization only after all 21 lanes are integrated
> and the tree is stabilized.

---

## Previous — **BATCH 1 · `opengl1` INTEGRATED (4 of 5)** · 8 integrated / 13 pending (2026-08-05)

> **STATUS: DONE for this lane.** `feature/opengl1` landed by **ADAPTATION** as merge
> **`c0876fca`** (signed, `--no-ff`, parents `bc29a976` + `91344935`). **Nothing was pushed.**
> Full record: **`integration/lanes/opengl1.md`**.
>
> ### The first HISTORY CLEAN lane of Batch 1 — and it still could not direct-merge
>
> The 31/31 maintainer-PGP classification re-verified exactly at the object level (zero SSH,
> zero unsigned, zero trailers, zero attribution). Direct merge still failed on: **three commit
> bodies with session narrative** (reworded at replay, patches untouched) and **10
> probe-established content drifts** (the ES1 set plus the lane's own
> `ITextureCubeBackend::ShareCpuPixels` interface hook the head never adopted — restored by the
> lane's own replayed commit). `fc0dd2a2` cost zero; `alphaTest` byte-identical fork→head.
> Adapted as **37 signed commits** — 31 replayed (24 byte-identical by range-diff, 0 lost,
> all 31 TRANSFERRED) + interface adaptation + explicit exhaustive capability switch + shared
> test arming + fog oracle + harness adaptation + docs.
>
> ### The capability default hazard broke its streak — three lanes for four
>
> The fork-era switch had **no default case and a trailing `return false`**, so `Texture3D` and
> `MultiStreamVertexInput` already fell through to a truthful false by accident of shape. Made
> explicit as the ten-member no-default GL4-convention switch regardless.
>
> ### Fog: the ES1 inversion verbatim, now with a three-pair oracle
>
> Scale recovered by projecting the fog vector onto the modelview eye-Z row; the degenerate
> `{0,0,0,1}` lands on the fully-fogged ramp. The oracle's three pairs — before-ramp, **exact
> mid-ramp ~50/50**, degenerate — each pin their own expected value; monotonicity alone cannot
> tell a wrong sign from a right one.
>
> ### A new failure class: the lane's own harnesses vs post-fork device contracts
>
> First run 35/38; every failure mechanism-diagnosed with production code proven correct:
> **REMED-GFX-081** (SpriteBatch::Begin's FNA-faithful CullCounterClockwise **persists after
> End()** — real XNA semantics — silently culling 3D quads authored under a one-time CullNone;
> instrumented to zero-fragments-with-perfect-state before touching the test), **GFX-165**
> (GetBackBufferData validates against PresentationParameters, which raw SDL_SetWindowSize
> deliberately does not update; the test now reads via the backend's own ReadBackbuffer), and a
> **control-proven environment regression** (the sandbox's GLX no longer exposes swap control —
> a CNA-independent raw-SDL probe fails for interval 0 and 1 alike; the vsync check is now an
> honest skip). **Expect this class on every remaining stale-fork lane.**
>
> **The registration union, a fifth time** — six files, token-verified: all 25 pre-existing
> identities keep exact counts, `OPENGL1` adds 13 tokens as the 26th.
>
> **Validation** on the real `4.5 (Compatibility Profile) Mesa 25.0.7` llvmpipe context (`:101`)
> — reported as the driver's identity, not the backend's API level; the fixed-function path is
> proven by construction (zero shader entry points, immediate-mode emission): **38/38** lane
> suites; `CnaTests` **5737 run · 5692 passed · 44 skipped · 1 failed** (the 44 = 39 positive
> `Texture3DTest` + 4 sensor + 1 WireFrame-refusal skip, all correct for this backend; the 1 =
> the known pre-existing `TwoProcessLoopbackTest.HostMigration…` networking flake, 2/3 in
> isolation). **ASan/UBSan over nine representative suites: zero findings**, every leak
> `libGLX_mesa`-rooted, `detect_leaks=0` control all-green. **Principal EasyGL control at the merged head: 5912 · 5904 · 6 · 2 — zero regressions**; both
> failures are the two documented environmental flakes (networking + the transient x11 blip,
> 3/3 in isolation), nothing new.
>
> ### Next — **Batch 1, fifth and final lane `opengl2`. NOT BEGUN.**
>
> 40 commits, 63 files, 11 drifted, all three shared interfaces (`GraphicsDevice.cpp` +
> `IGraphicsBackend.hpp` + `GraphicsCapability.hpp` — the batch's first `GraphicsCapability`
> lane, so the registration union grows a file). History class **HISTORY CLEAN — 40/40
> maintainer PGP** per the inventory (re-derive at the object level regardless; expect the
> session-narrative sweep to matter — it caught three bodies on this equally-clean lane). A
> shader-era-adjacent desktop GL 2.x backend: derive the fog treatment from what the backend
> actually is (consume the vector directly if it has shaders, invert if fixed-function), check
> the capability switch against the current ten members, and expect the harness-vs-post-fork
> contract collisions this lane just catalogued. **Batch 1 stabilization follows `opengl2`.**
> **Model: Fable** — the playbook is now proven on both fixed-function shapes and no history
> recreation is needed.
>
> **Carried forward unchanged:** `REMED-CONTENT-007`/`-008` **OPEN, HIGH/P1** — `opengl1` adds
> no path-resolution code and touches none of their files. Direct2D **OWNER-FROZEN, FROZEN
> INCOMPLETE/EXPERIMENTAL**. `feature/gl` order MetaGL → EasyGL → CNA, EasyGL
> **internal/hidden**, public backends exactly OpenGL ES 3 / OpenGL 3 / WebGL 1 / WebGL 2 —
> `OpenGL1` and `OpenGL4` are independent public backends, not part of that set. Modularization
> only after all 21 lanes are integrated and the tree is stabilized.

---

## Previous — **BATCH 1 · `opengl4` INTEGRATED (3 of 5)** · 7 integrated / 14 pending (2026-08-05)

> **STATUS: DONE for this lane.** `feature/opengl4` landed by **ADAPTATION** as merge
> **`bc29a976`** (signed, `--no-ff`, parents `df6b7cc6` + `3f1035de`). **Nothing was pushed.**
> Full record: **`integration/lanes/opengl4.md`**.
>
> ### The history half alone was disqualifying
>
> All 28 commits authored **and** committed under a non-human identity, all 28 SSH-signed by the
> campaign's known non-maintainer key — **0 PGP, 0 genuinely unsigned**, the inventory row
> re-verified with `git cat-file -p`. The first 8 carry both prohibited trailers across two
> session IDs. Four per-session `NEXT.md` status commits were **OMITTED with justification**
> (session narrative; `plan_opengl4.md` carries the technical record); the other 24 replayed with
> 32/41 files byte-identical at the replay boundary, 0 missing.
>
> ### The probe earned its place a third time — 23 errors, 13 drifts, two paid first here
>
> The `opengles1` drift set recurred (seven `void→bool` readbacks, pure-virtual
> `SetVertexDeclaration`/`SetRenderTargets`, `preserveContents`, `BlendWriteState`), plus two no
> prior lane had to pay: the **`fc0dd2a2` unified instanced transport** (this lane's GL4-33
> hardware instancing read the removed `instanceVb`; rewritten to `FirstInstanceStream()`, the
> divisor = the stream's own `InstanceFrequency`, offsets per GFX-211) and the **FNA fog vector
> across ten GLSL programs** — consumed directly, no inversion (shader backend, unlike the
> fixed-function ES1 lane), skinned programs dotting the POST-skin position as FNA's `Skin()`
> order requires.
>
> ### The capability default hazard recurred, exactly as predicted
>
> `SupportsCapability` was never overridden — inherited `true` for enum members the fork
> predates. Now an exhaustive ten-member switch with **no default case**; nine truthful `true`s,
> `MultiStreamVertexInput` false (shared negative oracles pass without trusting the answer),
> `AnisotropicFiltering` from the driver-granted ceiling (core only in GL 4.6). **Check the
> catch-all default on every remaining lane** — three for three so far.
>
> **The registration union was needed a fourth time** — six files; token-by-token, all 24
> pre-existing identities keep their exact counts, `OPENGL4` only added.
>
> **Validation on a real `OpenGL 4.5 (Core Profile)` context** (Mesa llvmpipe, GLSL 4.50,
> `:101`, reported honestly as software rasterization): **25/25** dedicated pixel suites;
> `CnaTests` **5737 run · 5730 passed · 6 skipped · 1 failed** (transient x11-connect blip,
> different victim each run, 3/3 in isolation); **principal EasyGL control at the merged head
> 5912 · 5905 · 6 · 1** — zero regressions, the 1 being the pre-existing
> `TwoProcessLoopbackTest.HostMigration…` networking flake (measurably flakier under load now:
> 1-of-3 clean isolation batches — watch at the Batch-1 checkpoint). ASan/UBSan over nine
> representative suites: **zero findings**; all leaks control-classified to Mesa driver or
> harness-owned allocations. WireFrame is now **pixel-oracle-proven** in the shared suite.
> `docs/opengl4-backend.md` + README entries added (README's compact list was found to omit
> `OPENGLES1` — pre-existing, handed to the Batch-1 checkpoint, not widened into this lane).
>
> ### Next — **Batch 1, fourth lane `opengl1`. NOT BEGUN.**
>
> 31 commits, 43 files, 11 drifted, `GraphicsDevice.cpp` + `IGraphicsBackend.hpp`. History class
> **HISTORY CLEAN — 31/31 maintainer PGP** per the inventory (re-derive at the object level
> regardless). A fixed-function desktop GL 1.x backend, so expect the ES1-shaped adaptation:
> the same 13-drift set, the **fog inversion** (not direct vector consumption), and the
> capability-default check. **Model: Fable** — no history recreation is needed and the
> adaptation playbook is now twice-proven on both the shader (opengl4) and fixed-function
> (opengles1) shapes; nothing here turns on contested design.
>
> **Carried forward unchanged:** `REMED-CONTENT-007`/`-008` **OPEN, HIGH/P1** — untouched by
> `opengl4`, which adds no path-resolution code and touches none of their files. Direct2D
> **OWNER-FROZEN, FROZEN INCOMPLETE/EXPERIMENTAL**. `feature/gl` order MetaGL → EasyGL → CNA,
> EasyGL **internal/hidden**, public backends exactly OpenGL ES 3 / OpenGL 3 / WebGL 1 /
> WebGL 2 — and `OpenGL4` is an independent public backend, not part of that set. Modularization
> only after all 21 lanes are integrated and the tree is stabilized.

---

## Previous — **BATCH 1 · `opengles1` INTEGRATED (2 of 5)** · 6 integrated / 15 pending (2026-08-05)

> **STATUS: DONE for this lane.** `feature/opengles1` landed by **ADAPTATION** as merge
> **`df6b7cc6`** (signed, `--no-ff`, parents `99ae7d11` + `b811d76d`). **Nothing was
> pushed.** Full record: **`integration/lanes/opengles1.md`**.
>
> ### It failed the direct-merge conditions on BOTH halves
>
> **History.** 26 own commits: **24 maintainer-PGP with zero trailers**, and **2 SSH-signed by a
> non-maintainer key**, authored *and* committed under a non-human identity with two prohibited
> trailers each. **Zero genuinely unsigned.** Those two are the lane's **first** commits, so all 24
> clean commits descend from them — there was no way to take the good history without the bad.
> `%G?` reported `N` for both; the `gpg.ssh.allowedSignersFile` error firing exactly twice is the
> real tell. The `ext` lesson held a second time.
>
> **Content.** Forked at `ac3aaaeb`, **835 commits behind**. Source inspection found the two
> now-pure virtuals (`SetVertexDeclaration`, `SetRenderTargets`, both also changed parameter type).
> **The compile probe found four more that no grep could have** — `GetData`/`SetData` changing
> return type `void` → `bool` on four interfaces — plus `CreateRenderTargetCube`'s
> `preserveContents`, `ApplyBlendState`'s `BlendWriteState`, and `fogStart`/`fogEnd` becoming the
> FNA fog vector. `IGraphicsBackend.hpp` grew **1023 → 1788 lines** across the gap.
>
> **Lesson for every remaining lane: a probe is the only thing that sees a changed return type.**
> A signature grep, however careful, cannot.
>
> ### The fog inversion
>
> A fixed-function pipeline has no dot product to evaluate a fog vector with, so `glFog` needs the
> scalars back. They are **recovered exactly, not approximated** — the vector is built from those
> scalars and the same `world*view` matrix the draw loads immediately before `ApplyFog`, so
> projecting it back onto that matrix's eye-Z row recovers them. Proven by **three distinct
> `FogStart`/`FogEnd` pairs** each producing their own correct result on a real driver, XNA's
> degenerate `FogStart == FogEnd` among them.
>
> ### One independent production defect — found by the drift, not introduced by it
>
> `GraphicsCapability` grew from **8 members to 10** after the fork, and `SupportsCapability` ends
> in `default: return true` — so **`Texture3D` and `MultiStreamVertexInput` were both answered
> `true`**. `Texture3DUnsupportedBackendTest`, which exists precisely to catch this, **skipped**:
> the false claim silenced its own detector. Fixed in-lane. **Check any catch-all `default` against
> the current enum on every remaining lane** — all of them fork from bases the head has outgrown.
>
> ### `TextureCube` readback implemented rather than declared absent
>
> The backend genuinely owns cube pixels, so inheriting "cannot read a cube face" was untrue. Level
> 0 reads back exactly; above it is a real ES 1.1 boundary (`GL_OES_framebuffer_object` requires an
> attachment's level to be 0). The readback deliberately does **not** flip Y, because this class's
> `SetData` works in GL's bottom-up space — measured, not assumed: the flip failed three
> sub-rectangle tests that pass without it.
>
> ### §1.1 decided on evidence
>
> The **declaration guard applies** — this backend dispatches by vertex stride, exactly the case it
> exists for — and is wired into all four draw routes, header-only, asymmetric. Truthful `WireFrame`
> was already satisfied and genuinely implemented via `GL_LINES` re-expansion.
>
> **The registration union was needed a third time.** Taking the incoming side of
> `BackendSelection.cmake` would have deleted `STUB`, `FREEDIRECT`, `DX1/2/5/6/7/8` and `D3D10` —
> and the lane's own `DX3` token means *free-direct*, not the head's real DirectX 3.
>
> **Validation on a real `OpenGL ES-CM 1.1` driver** (Mesa 25.0.7 softpipe, `DISPLAY=:101`), never a
> desktop-GL fallback: **63/63** across all seven lane harnesses; `CnaTests` **5733 run · 5689
> passed · 43 skipped · 1 failed** *(corrected 2026-08-05 — the "87 skipped" first recorded here was
> a log-line count, each skip printing twice plus the aggregate header: 43 × 2 + 1 = 87; see
> `integration/lanes/opengles1.md` §12.1 for the per-run reconciliation and the explicit sanitizer
> disposition, which is: sanitizers did not run for that lane)*. Failures went **22 → 1** — the last
> is a two-process networking test that times out under load and passes 3/3 in isolation. Seven shared **test** files were armed (the
> `stub` precedent); no production defect in any of them.
>
> ### Next — **Batch 1, third lane `opengl4`. NOT BEGUN.**
>
> 28 commits, 41 files, 9 drifted, and the batch's first lane to touch **two** shared interfaces
> (`GraphicsDevice.cpp` **+ `IGraphicsBackend.hpp`**). History class is **total cleanup — 0 PGP, 28
> SSH** per the inventory, which `opengles1` has just shown must be **re-derived with
> `git cat-file -p`** rather than trusted: "28 unsigned" and "28 SSH-signed by a non-maintainer key"
> need the same action but are not the same fact. **Model: Opus** — every commit needs re-authoring,
> it is the first two-interface lane, and both Batch 1 lanes so far have broken their cheap-looking
> classification on contact.
>
> **Carried forward unchanged:** `REMED-CONTENT-007`/`-008` **OPEN, HIGH/P1** — untouched by
> `opengles1`, which adds no path-resolution code and touches none of their files. Direct2D
> **OWNER-FROZEN, FROZEN INCOMPLETE/EXPERIMENTAL**. `feature/gl` order MetaGL → EasyGL → CNA, EasyGL
> **internal/hidden**, public backends exactly OpenGL ES 3 / OpenGL 3 / WebGL 1 / WebGL 2 — and
> `OpenGLES1` is **not** an EasyGL alias, it is its own public enum member and build option, with
> zero EasyGL/MetaGL files touched. Modularization only after all 21 lanes are integrated and the
> tree is stabilized.

---

## Previous — **BATCH 1 OPEN · `stub` INTEGRATED (1 of 5)** · 5 integrated / 16 pending (2026-08-04)

> **STATUS: DONE for this lane.** `feature/stub` landed by **ADAPTATION** as merge **`99ae7d11`**
> (signed, `--no-ff`, parents `990d6b8a` + `c29ef117`). **Nothing was pushed.** Full record:
> **`integration/lanes/stub.md`**.
>
> ### The direct-merge classification was re-derived and did not hold
>
> Batch 0's closeout named `stub` a **direct-merge candidate**. Every *history* claim behind that
> re-verified true — 5 own commits, 5/5 maintainer-authored **and** committed, 5/5 maintainer PGP,
> zero attribution, zero merges, zero WIP, `GpuDrawParams` cost zero. **The lane needed no history
> work at all.**
>
> **Its content did.** The lane forked at `ac3aaaeb` and is 827 commits behind, predating two
> `IGraphicsBackend` members that are now **pure virtual** — `SetVertexDeclaration` and
> `SetRenderTargets`, both pure *by design* so a new backend cannot inherit a wrong default. A
> compiler probe (not an inference) proved `StubVertexBufferBackend` and `StubGraphicsBackend`
> **abstract** against the current tree: **a direct merge would have produced a non-compiling
> integration head.** Direct-merge conditions 7, 8 and 10 fail; the rest hold.
>
> **Lesson worth carrying to every remaining lane: a clean history is not a compatible tree.** All
> 16 pending lanes fork from a base that is now hundreds of commits stale. Re-derive *content*
> compatibility separately from *history* class — a compile probe is cheap and decisive.
>
> ### §1.1 was decided on evidence
>
> - **Truthful `WireFrame` reporting — already satisfied, and stricter than Headless**, which
>   reaches `true` only by inheriting the default. Stub reports `false` for everything.
> - **Declaration-fidelity guard — no subject.** The obligation is scoped to backends with a native
>   vertex layout; `RequireFaithfulDeclarationEXT` is measurably called only by those. Stub, like
>   Headless, has none, and takes Headless's explicit empty override.
> - **"Refuse polygon topologies" — does not apply.** It exists to stop a backend returning a frame
>   that silently lies. Stub returns no frame; refusing would break its own contract that a `Game`
>   loop completes without throwing.
>
> ### Two shared tests needed a STUB arm, neither a production defect
>
> `WireFrameCapabilityReportIsThisBackendsOwn`'s default arm expects `true`, and
> `SetRenderTargets_{One,Four}Target*` default to expecting no throw. Stub reports `false` and
> refuses render-target binds it cannot honour — **both correct** — so the contracts are now asserted
> rather than left as standing reds. `#elif defined(CNA_BACKEND_STUB)` only; no other backend
> affected.
>
> ### The predicted "boilerplate" conflict required a union
>
> `cmake/BackendSelection.cmake` conflicted twice. Taking the incoming side would have **deleted
> `dxold`'s eight backends and reverted the `DX3 → FREEDIRECT` rename** — and the lane's own `DX3`
> token means *free-direct*, a different backend from the head's real DirectX 3. Verified by counting
> every `dxold` token before and after: all unchanged.
>
> **Validation:** `Stub_Smoke` **7/7 with no display present**; `CnaTests` **5693/5737 (99 %)** under
> `CNA_GRAPHICS_BACKEND=STUB`. All 44 failures classified, none a regression: 5 are the lane's own
> pre-existing capability residuals (already failing at `ac3aaaeb`), 14 are the documented
> `TextureCube`/custom-`Effect` gap, 24 are environment-dependent audio/network/media, 1 not run.
>
> **New findings: none.** No independent production defect was found in this lane.
>
> ### Next — **Batch 1, second lane `opengles1`. NOT BEGUN.**
>
> 26 commits, 23 files, 6 drifted, `GraphicsDevice.cpp` only, mixed history (**24 PGP, 2 SSH** — the
> SSH pair must be resolved, not counted as either). **Model: Opus** — unlike `stub` this lane has a
> genuine history-class decision (the two SSH-signed commits) on top of a real backend, and `stub`
> has just shown that the cheap-looking classification is the one that breaks. Reserve nothing here.
>
> **Carried forward unchanged:** `REMED-CONTENT-007`/`-008` **OPEN, HIGH/P1** — untouched by `stub`,
> which adds no path-resolution code and touches none of their files. Direct2D **OWNER-FROZEN,
> FROZEN INCOMPLETE/EXPERIMENTAL**. `feature/gl` order MetaGL → EasyGL → CNA, EasyGL internal/hidden,
> public backends exactly OpenGL ES 3 / OpenGL 3 / WebGL 1 / WebGL 2. Modularization only after all
> 21 lanes are integrated and the tree is stabilized.

---

## Previous — **BATCH 0 CLOSED · FINAL FOUR-LANE CHECKPOINT TAKEN** · 4 integrated / 17 pending (2026-08-04)

> **STATUS: DONE.** Batch 0 is closed and verified. Signed annotated tag
> **`integration/checkpoint-batch0-complete-20260804`** → **`990d6b8a`**, **local only, not pushed**.
> Full record: **`integration/BATCH_0_COMPLETE.md`**.
>
> **Closeout only — no lane integrated, no production/test file modified, `audit/` untouched.**
>
> ### Batch 0 has TWO checkpoints. They are not interchangeable.
>
> | | Tag | Target | Lanes | Record |
> |---|---|---|---|---|
> | **A — intermediate** | `integration/checkpoint-batch0-20260804` | `e0332214` | **3** | `integration/BATCH_0_STABILIZATION.md` |
> | **B — final** | `integration/checkpoint-batch0-complete-20260804` | `990d6b8a` | **4** | `integration/BATCH_0_COMPLETE.md` |
>
> **A is correct and must never be moved, recreated, retargeted or deleted.** It marks the state
> after `depthcrt` + `gltf` + `ext` and deliberately predates `dxold`, which was sequenced after it.
> Read its §11 *"Batch 0 complete"* as scoped to the batch's **process-validation objective**, not
> its lane set — the same document names `dxold` as the remaining lane five sections earlier.
>
> | Gate | Result |
> |---|---|
> | Fetch | `--all --prune --tags` exit 0, **nothing moved** |
> | Four lanes | `depthcrt` `61bd1a1b` · `gltf` `722a2f5a` · `ext` `8a374b9f` · `dxold` `990d6b8a` — all four original heads unmoved, all four archive tags verify good, each merge's tree delta is exactly its own lane's scope |
> | Ancestry | all six anchors verified — `d79214e7`, `e0332214`, and all four lane merges |
> | Signatures | **48 / 48 `U`** over `d79214e7..HEAD` — 41 adapted + 4 merges + 2 stabilization + 1 direct-merged original. Zero `N`, zero `E` |
> | Attribution | **zero** prohibited hits. One sweep match, the tracked filename `docs(CLAUDE.md):` — explicitly non-violating (policy §2.1); body read in full and clean. One author, one committer, **no trailers anywhere** |
> | `dxold` losslessness | **210 / 210 lane-added files byte-identical at the replay boundary** (`c0cad202`), 0 missing at the adapted head — the card's claim verified where it is made |
> | `dxold` backend delta | **+8** measured: CMake selectors 14 → 22, enum 14 → 22. `FREEDIRECT` is the **renamed** free-direct backend, not a ninth. No live `DX30` selector, option or enumerator survives |
> | No fifth lane | verified two ways — no pending lane head is an ancestor; no pending lane marker path in the tree; exactly three `adapt/*` branches for the three *adapted* lanes |
> | Worktrees · `git diff --check` · `audit/` | clean · clean · **0 files changed** |
>
> ### One campaign-wide correction: **"204 unsigned" is wrong**
>
> `INTEGRATION_BRANCH_INVENTORY.md` §5's *"204 carry no signature at all"* was re-derived at the
> object level **for all 21 lanes** (the inventory had flagged three as owing this). Of 796 own
> commits: **592 maintainer PGP · 204 SSH · 0 genuinely unsigned**, and all 204 carry the *identical*
> non-maintainer `ssh-ed25519` key first seen on `ext`. **No required action changes** — an SSH
> signature from a foreign key never satisfied policy A4 — but the `SIGNATURE-ONLY CLEANUP` label is
> a misnomer for `html-dom`'s 17, and `%G?` must never be used to derive a signature class again.
>
> ### Corrections recorded, not silently applied
>
> - **`990d6b8a`'s merge body says "32 signed commits"; the range is 35.** The merge object is
>   **deliberately left unmodified** — signed, published-in-spirit integration history. Authoritative
>   count 35.
> - **`integration/lanes/dxold.md` enumerated only 34 of its 35** — `618afbcf` was missing from the
>   mapping table and the record paragraph. **Corrected in place** (a lane card is living doc).
> - **A false positive, dismissed:** `docs/graphics-backend-feature-matrix.md` lists none of the
>   eight new backends, but it is byte-identical across `develop`/checkpoint/HEAD and its title
>   scopes it to *established* backends. Never covered old `DX3` either. Not a `dxold` gap.
>
> ### Next — **Batch 1, first lane `stub`. SELECTED, NOT BEGUN.**
>
> The proposed order (`stub` → `opengles1` → `opengl4` → `opengl1` → `opengl2`) was **re-derived
> against the current head, not assumed, and `stub` holds**: it is the batch's only lane needing no
> history work (5/5 Robert-authored, 5/5 maintainer PGP — the `gltf` class, so **test it against the
> nine direct-merge conditions first**), its `GpuDrawParams` cost is **zero** (newly measured — it
> is a fifth zero-cost lane, not a fourth), and it is smallest by every measure (5 commits, 15 files,
> +685/−9). `opengles1` has marginally less drift (6 vs 8) but 5× the commits and a mixed history.
>
> **`dxold` raised `stub`'s conflict surface**: 5 of its 8 drifted files drifted *because of* `dxold`
> — `CMakeLists.txt`, `README.md`, `GraphicsBackendType.hpp` and the two backend-registration test
> files. That is C4 behaving as predicted, and an argument for taking `stub` **now**.
>
> **Must not be waved through:** `stub` adds a backend, so the §1.1 post-audit obligations apply.
> Its final commit is already *"`SupportsCapability` should return false"*. Decide and **record**
> what truthful `WireFrame=false` reporting and the declaration-fidelity guard mean for a backend
> whose draw routes are all no-ops — do not skip it because nothing renders.
>
> **Model: Fable** for `stub` — small, clean, mechanical, conflicts confined to registration
> boilerplate `dxold` just demonstrated on the same files. **Reserve Opus for `opengl4`** (28/28
> non-maintainer-signed total history recreation across two shared interfaces).
>
> Carried forward unchanged: **`REMED-CONTENT-007`/`-008` OPEN HIGH/P1** — re-checked, none of the
> four integrated lanes touches any file they live in; required before any public security-clean
> claim; best run as a **parallel safety lane during Batch 0–1**. **Direct2D OWNER-FROZEN
> INCOMPLETE @ `9b17e783`** (corrected recount: 96 of 128 incomplete; this historical record
> originally said 88). **`feature/gl`** cross-repository sequence —
> EasyGL **internal and hidden**, public backends exactly **OpenGL ES 3, OpenGL 3, WebGL 1,
> WebGL 2**; its one open provenance gap (the EasyGL `rvc` archive tag) is blocked only on a
> one-line worktree cleanup. **Nothing was pushed.**


## Previous — **BATCH 0 COMPLETE: `dxold` LANDED (4 of 4 lanes)** · 4 integrated / 17 pending (2026-08-04)

> **STATUS: DONE.** The `dxold` lane — the legacy DirectX backend family — is integrated.
> Merge **`990d6b8a`** (signed, `--no-ff`, parents `e0332214` + `9256e606`), extending the Batch 0
> checkpoint. Full record: **`integration/lanes/dxold.md`**.
>
> | Gate | Result |
> |---|---|
> | Original provenance | `feature/dxold` and `archive/preintegration/dxold-20260804` both still `36289bb2`, tag verifies good |
> | Adapted range | **35 commits on `adapt/dxold`**, 35/35 GPG-signed `U`, 35/35 Robert-authored, zero prohibited attribution; 28 replayed originals (210/210 added files byte-identical), 1 interface-adaptation commit, 2 owner-ordered rename commits, 3 validation-driven completion fixes |
> | Public backends added | **+8, Historical class:** `DX1 DX2 DX3 DX5 DX6 DX7 DX8 D3D10` — Route B (real MinGW-w64 headers + era-correct COM + Wine/DXVK); era-accurate capability reporting; REMED-GFX-DECL-GUARD on all six stride-dispatching backends |
> | **Naming transition (owner instruction, live)** | executed inside the lane: free-direct `DX3` → **`FREEDIRECT`** (`8a1e801e`), then `DX30` → **`DX3`** (`dd4806f0`). Task IDs `DX3-*`/`DX30-*` kept verbatim; `audit/`/`remediation/` untouched. The DirectX 3 generation now has two distinct public implementations |
> | Validation | dedicated Wine suites **137/137** (DX1 10, DX2 19, DX3 19, DX5 19, DX6 20, DX7 20, DX8 20, D3D10 10, on `:99`); FREEDIRECT native **19/20** on `:101` — the one red (`FreeDirect_SpriteBatch`) reproduces identically on the checkpoint's own pre-rename binaries (known pre-existing defect pair, deliberately untouched); D3D9 modern-control cross-build exit 0; post-merge EasyGL principal suite green on the merged tree |
> | Batch 0 ancestry | phase-1 checkpoint, `integration/checkpoint-batch0-20260804`, and all three prior lane merges verified ancestors; merged tree byte-identical to `adapt/dxold` |
>
> **Next recommended action: Batch 1** (`stub` → `opengles1` → `opengl4` → `opengl1` → `opengl2`,
> `INTEGRATION_ORDER.md` §3). `stub` is the natural opener: 5 commits / 15 files, GraphicsDevice.cpp
> only. Model recommendation: **Sonnet or Fable** for `stub`/`opengles1` (small, mechanical);
> reserve Opus-class reasoning for `opengl4` (28/28 total history recreation, per-object signature
> re-derivation needed first — the `%G?` blind spot is now confirmed on two lanes).
>
> Carried forward unchanged: **`REMED-CONTENT-007`/`-008` OPEN HIGH/P1** (dxold touches no
> path-resolution code — re-checked); **Direct2D OWNER-FROZEN INCOMPLETE @ `9b17e783`**;
> **`feature/gl`** cross-repository sequence (EasyGL hidden; public backends exactly OpenGL ES 3,
> OpenGL 3, WebGL 1, WebGL 2). Nothing was pushed by this session.


## Previous — **BATCH 0 STABILIZATION CHECKPOINT TAKEN** · Batch 0 was 3 of 4 lanes + checkpoint (2026-08-04)

> **STATUS: OUTCOME A — READY.** Signed annotated tag
> **`integration/checkpoint-batch0-20260804`**, **local only, not pushed**.
> Full record: **`integration/BATCH_0_STABILIZATION.md`**.
>
> | Gate | Result |
> |---|---|
> | Provenance, all three lanes | clean — 21 archive tags verify, originals unmoved, each merge's tree delta is exactly its lane's scope |
> | Full `CnaTests` at the integration HEAD | **5912 run · 5906 passed · 6 skipped · 0 failed** (was 5904/13/**2** at `61bd1a1b`) |
> | depthcrt lane tests · demos · screenshots | 19/19 · both demos exit 0 · 18 PNGs, 18 distinct, visually verified |
> | `XnbContainerFuzzTest` | **resolved, test-only** — and the exit record's `REMED-GFX-DECL-GUARD` attribution is **wrong**, see below |
> | NOXNA cross-references | 4 repaired on the integration branch; 3 under frozen `audit/` deliberately untouched |
> | Signatures / attribution | all `U`, zero attribution hits |
>
> **The next lane is `dxold`** (28 commits, 225 files) — it closes Batch 0. **Not begun.**
>
> **One correction worth carrying forward.** `remediation/REMEDIATION_EXIT.md:238` names
> `REMED-GFX-DECL-GUARD` as the cause of the `XnbContainerFuzzTest` failure. It is not. The throw is
> `System::ArgumentException` from `VertexBuffer::SetData` (`VertexBuffer.cpp:183`) — XNA-layer
> argument validation at **upload**, during `ContentManager::Load<Model>()`. The decl guard lives in
> backend **draw** paths and throws `System::NotSupportedException`. The exit record's *conclusion*
> (stale test expectation, not a production defect) was right; its named mechanism was not. The exit
> record is deliberately left unmodified — it is the frozen statement attached to
> `cna-post-audit-remediation-phase1`.

## Previous — **third integration lane LANDED: `ext`** · Batch 0 is 3 of 4 (2026-08-04)

> **Remediation phase-1 checkpoint: TAKEN. Direct2D: FROZEN INCOMPLETE (owner-confirmed).
> Integration bootstrap: COMPLETE. Three feature lanes integrated: `depthcrt` (adapted),
> `gltf` (direct merge) and `ext` (adapted).**
>
> Signed annotated tag **`cna-post-audit-remediation-phase1`** → `d79214e7` on `feature/audit`,
> **local only, not pushed**. **This is phase 1, not the completion of all CNA work.**

**Read first:** `integration/INTEGRATION_BRANCH_INVENTORY.md` (authoritative lane inventory,
**18 pending lanes** after `depthcrt`, `gltf` and `ext`, re-derive after a fetch),
`integration/INTEGRATION_ORDER.md` (batches and lane selection),
`integration/INTEGRATION_HISTORY_POLICY.md` (how commits are adapted),
`integration/lanes/depthcrt.md`, `integration/lanes/gltf.md` and `integration/lanes/ext.md` (the
three completed lanes' full records), and `remediation/REMEDIATION_EXIT.md` (authoritative
remediation exit record).

`remediation/INTEGRATION_BRANCH_INVENTORY.md` is **superseded** — retained as a labelled historical
snapshot.

### State

| Item | Value |
|---|---|
| Integration branch | **`integration/post-audit-phase1`** @ **`8a374b9f`** — checkpoint + the `depthcrt`, `gltf` and `ext` merges |
| Integration worktree | **`/rv/data/development/github.com/openeggbert/cnaintegration`** — clean |
| Adaptation branches / worktrees | `adapt/depthcrt` @ `3cca0b19` · `.../cnaintegration-depthcrt` and `adapt/ext` @ `c6a28036` · `.../cnaintegration-ext` — both **retained** for post-merge review, not deleted. `gltf` needed none |
| `feature/direct2d` | **FROZEN INCOMPLETE / EXPERIMENTAL @ `9b17e783`** — not actively developed, not automatically integration-ready, does **not** block other lanes |
| Archive tags | **22**, all annotated, GPG-signed, verified, **local only** — 21 CNA lanes + MetaGL `feature/followup-audit`. The 23rd (EasyGL `rvc`) is outstanding and is blocked only on a one-line worktree cleanup — see `INTEGRATION_BRANCH_INVENTORY.md` §7.5 |
| Feature lanes integrated | **3 of 21 — `depthcrt`** (adapted, merge `61bd1a1b`), **`gltf`** (direct merge `722a2f5a`) and **`ext`** (adapted `c6a28036`, merge `8a374b9f`) |
| Pending lanes | **18** — freshly derived 2026-08-04 after `git fetch --all --prune --tags`, with all 21 lane heads confirmed unmoved; re-derive after any fetch |
| Signatures over `d79214e7..8a374b9f` | **10 / 10 `U`** — no `N`, no `E` |
| Pushed | **this session pushed nothing.** But the blanket "nothing is pushed" claim is now **false** and was corrected: `feature/audit` (`047c254a`), `integration/post-audit-phase1` (`61bd1a1b`) and `adapt/depthcrt` (`3cca0b19`) are **on `origin`**, pushed by the owner at 2026-08-04 16:54:40. **All 21 archive tags remain local only.** The `gltf` merge `722a2f5a`, the `ext` merge `8a374b9f`, `adapt/ext`, and this documentation commit are local. Measure with `git ls-remote` — do not restate this line forward |

### Do not infer lane completion from ancestry

The campaign now uses **both** integration paths. A direct merge preserves the original commit
object, so `feature/gltf`'s head **is** an ancestor of the integration branch. An adaptation replays
commits as new objects, so `feature/depthcrt`'s head is **not** — correctly.

A `git merge-base --is-ancestor <lane-head> integration/post-audit-phase1` sweep over all 21 lanes
therefore reports **1 integrated, 20 pending** — only `gltf`, the one direct merge. That is a
property of the measurement. Read lane completion from the lane cards and `INTEGRATION_ORDER.md` §3.
**The correct count is 3 integrated, 18 pending.**

### What landed — `ext`

`NOXNA.md` rewritten as the authoritative extended-graphics design (`+568, −241`, one file). Its
central contribution is a boundary that had never been written down: the **always-compiled
`NOXNA`/`*EXT` marker convention** in `Microsoft::Xna::Framework::Graphics` versus the
**`CNA_NOXNA`-gated `CNA::Graphics` engine layer**. It records what already ships, corrects four
stale claims, specifies the remaining classes/enums/backend virtuals, and renumbers the backlog.

**Adapted, not direct-merged.** The original `05ab5d3d` needed four of the five cleanup classes at
once: author, committer, trailers and signature. Landed as **one** GPG-signed, Robert-authored commit
`c6a28036` with both prohibited trailers stripped and the technical body preserved verbatim; signed
`--no-ff` merge `8a374b9f`. `range-diff` is 38 lines and shows only author, trailers/message, and the
one recorded adaptation.

**Documentation only — nothing was built, and that is measured rather than assumed.** The commit
changes no build input; tree-hash equality outside `NOXNA.md` proves every other tracked path is
byte-identical. Every testable claim the document makes was checked against the integration head and
**holds**. Full record: `integration/lanes/ext.md`.

#### Two lessons this lane produced

1. **A one-file lane is not a zero-conflict lane.** `ext` renumbers the same `NOXNA.md` backlog table
   `depthcrt` had appended `N26`–`N29` to, and was authored against a base predating them. Resolving
   in favour of the incoming side would have **silently deleted four rows of already-integrated
   work**. They were preserved verbatim — the renumbering leaves `N26`–`N29` free, so they kept both
   their numbers and their position. `depthcrt`'s *diff the lane against the checkpoint, per shared
   file* lesson is what caught it.
2. **`%G?` cannot distinguish "unsigned" from "SSH-signed and uncheckable".** `05ab5d3d` is **not**
   unsigned as the inventory records — it carries an SSH signature from a non-maintainer key, and
   `%G?` still reports `N` (not the `E` the policy predicts). The required action was unchanged, but
   `opengl4` (28), `magnum` (13) and `wicked` (10) should be re-derived with `git cat-file -p`.

#### One consequence deliberately left open — for the Batch 0 checkpoint

The renumbering **invalidates four cross-references from files outside the lane**:
`include/CNA/Graphics/PbrMaterial.hpp:19` and `noxna_devices.md:93` (cite `N11`, which no longer
means `PbrEffect`), `docs/surface-format-support.md:184,220` (cite `N20` for float render targets,
now `N11`/`N12`), and `plan_postaudit.md:1572-74` (quotes the old `N50`/`N51`/`N52` titles and an old
section number). Two more live under `audit/`, which is frozen. `DitherMode.hpp:14`'s `N70` still
resolves.

**Not fixed in the lane, on purpose** — it means editing four files outside a one-file lane and
touching `audit/`. **No remediation ticket was opened: this is not an independent production defect**
(no compiled behaviour changes, and it is *caused by* this integration rather than independent of
it). Owner: the Batch 0 stabilization checkpoint.

### What landed — `gltf`

One root-level document, `gltfissues.md` (`+451, −0`): a dated (2026-07-28) root-cause analysis of
incorrect glTF rendering, attributing each defect to a layer — discarded glTF node transforms, lost
`baseColorFactor` with a map-gated PBR selection condition, black PBR surfaces from absent default
lighting, ignored `KHR_materials_transmission`, plus sampler, multi-UV-set, rigid-node-animation,
material-variant and sRGB losses.

**Integrated by DIRECT MERGE — the first of the campaign.** Its single commit `86ada7a7` is
`HISTORY CLEAN` (authored *and* committed by Robert Vokac, GPG-signed, empty body, empty trailer
set) and its merge base was already an ancestor, so all nine direct-merge conditions held. The
commit was **preserved as the same object, not recreated**; losslessness is structural rather than
measured. Signed `--no-ff` merge `722a2f5a`.

**This lane implements nothing.** The document's *Recommended Repair Order* (P0–P2) and its twelve
*Missing Regression Tests* are proposals; none exists at the integration head. It closes no ticket
and adds no glTF capability — it adds a description of open defects.

Staleness was measured, not assumed: **eight of the nine source files it cites are byte-identical**
between its own declared baseline `32639a13` and the integration head, so its findings still
describe current behaviour. The ninth, `EasyGLGraphicsBackend.cpp`, was rewritten by the remediation
campaign and its cited PBR shader line `4121` now sits at `4948`; the quoted expression is unchanged,
so the citation was **retained as historical** rather than rewritten to a line that did not exist on
the analysis date. Full record: `integration/lanes/gltf.md`.

### `feature/gl` adds four public backends — EasyGL is not one of them

**OpenGL ES 3, OpenGL 3, WebGL 1, WebGL 2.** EasyGL is internal and hidden, a support library rather
than a user-selectable CNA backend. Do not count or expose it as a fifth (inventory §7.0).

The `easy-glrvc` worktree's uncommitted `CMakeLists.txt` redirect is **temporary local build
configuration** — not feature work, and **not provenance to preserve**. Restore or remove it
(`git restore` / `git checkout --`; **never `git stash`**), then create the outstanding EasyGL `rvc`
archive tag. The earlier "discard or commit — the owner's call" framing was wrong and is corrected in
inventory §7.5; owner-only still applies to the cross-repository *merges* and `GLB-38`, not to this
file.

### What landed — `depthcrt`

`CNA::Graphics::DepthEffect` (colour-depth reduction, Bayer dithering, Palette256/Palette16) and
`CNA::Graphics::CRTEffect` (scanlines, RGB sub-pixel mask, curvature, vignette), both NOXNA,
EasyGL-targeted, with 18 unit tests and two manual verification demos.

Adapted from `archive/preintegration/depthcrt-20260804` (`f4804469`) as **5 GPG-signed,
human-authored commits** plus one signed `--no-ff` merge `61bd1a1b`. The five replayed patches are
**byte-identical** to the originals; `git range-diff` differs only in author, trailers and the one
dropped commit. `f05e07c8` was dropped as superseded by `REMED-BUILD-005` after direct comparison,
not on the plan's word.

Validation: 19/19 effect tests; full `CnaTests` 5904/5912 with both failures reproduced on the
checkpoint **without** the lane; both demos render and capture correctly under Xvfb; ASAN+UBSAN
clean apart from a Mesa GLX driver leak baseline also present without the lane. Full record:
`integration/lanes/depthcrt.md`.

### One test failing on the integration base itself — owner triage

`XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly` fails on
`integration/post-audit-phase1`, and failed on the checkpoint before `depthcrt` landed. The escaping
exception is the `REMED-GFX-DECL-GUARD` rejection — *"The VertexDeclaration contains an element
outside the uploaded vertex stride"* — which is **correct production behaviour**; the fuzz test's
expected-exception set simply predates the guard. A test-side gap, not a production defect, so no
remediation ticket was opened for it. It deserves an owner decision on whether to ticket it in
`plan_postaudit.md`.

**Still open and unchanged after `gltf` and `ext`.** Neither lane was its cause or its fix: each
changes one Markdown file and no compiled source, so neither can have affected this test in either
direction. Neither ran it, because a documentation-only lane gives nothing to run it against. The
residual remains exactly as recorded above — a pre-existing property of the checkpoint. **It is a
named item in the Batch 0 checkpoint scope below.**

`TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`
also fails on the base — a 30 s timeout in a real two-process networking test, environment-dependent.

### The next single task — **Batch 0 stabilization / provenance checkpoint**, not another lane

Batch 0 has landed 3 of 4 lanes and has now exercised **all three principal history classes**:

| Class | Lane | Path |
|---|---|---|
| Reconstructed multi-commit lane | `depthcrt` | 6 originals → 5 adapted, 1 dropped as superseded |
| Clean direct-merge lane | `gltf` | original object preserved, nothing recreated |
| Single-commit metadata-cleanup lane | `ext` | 1 → 1, author + committer + trailers + signature |

The process the batch exists to validate is proven. **Checkpoint it before taking `dxold`** (28
commits, 225 files — the last Batch-0 lane, and an order of magnitude larger than anything landed so
far). Recommended checkpoint scope:

- **A full build + `CnaTests` run on `8a374b9f`.** No lane since `depthcrt` has compiled anything —
  `gltf` and `ext` are both documentation-only — so the last *measured* build state of the
  integration branch is `61bd1a1b`. Two documentation merges later, that should be re-established
  rather than assumed.
- **The `NOXNA.md` cross-reference repair** `ext` deliberately left open, including the two `audit/`
  citations, which need an owner decision a lane session cannot make.
- **An owner decision on `XnbContainerFuzzTest`**, failing on the base since before `depthcrt`.
- **A signed checkpoint tag** (`integration/checkpoint-batch0-<date>`, `INTEGRATION_ORDER.md` §5).

`INTEGRATION_ORDER.md` §4 carries `depthcrt`'s five process lessons and §4.2 carries `ext`'s two;
read both before starting the next lane.

**Direct2D is deliberately NOT first.** It is frozen *mid-backlog* — `plan_direct2d.md` records 128
`D2D-*` rows with **96 not complete** (32 complete + 35 yellow + 61 blank; corrected from the
historical stale count 88). Frozen is not complete. It sits in the deferred group pending
an owner scope decision (`integration/lanes/direct2d.md`).

### One open provenance gap

**EasyGL `rvc` @ `b52f671379…` has no archive tag**, because the `easy-glrvc` worktree carries an
uncommitted `CMakeLists.txt` MetaGL redirect and Phase 2's precondition requires a clean worktree.
Deliberate omission, not an oversight. Closed by one owner decision — discard or commit the redirect.
`feature/gl` step 5 must not begin before it is closed.

### The strongest *substantive* next task — `REMED-CONTENT-007` / `-008`

Two **HIGH/P1 path-containment findings**, re-verified as still present in current source by the
exit reconciliation:

- `SongContentTypeReader.cpp` / `VideoContentTypeReader.cpp` each define a private
  `ResolveRelativeFilePath()` with **no containment check**, fed by the `.xnb`'s own embedded
  filename;
- `ContentManager.cpp` has **zero** `PathContainment` calls while joining 8 manifest-supplied path
  fields onto the content root raw.

`include/CNA/Internal/PathContainment.hpp` already exists and the neighbouring `sourceFile` field is
already hardened via `CnjSourceFile.hpp` — the fix is mechanical. These do **not** block the
checkpoint (they fall outside its blocker classes — `REMEDIATION_EXIT.md` §2.1, §4.4) but they are
the **highest-severity open items in the entire inventory**. This checkpoint does not claim security
is clean.

**Carried into the integration campaign, still open (`INTEGRATION_ORDER.md` §6):**

- **HIGH / P1 security follow-up.** Non-blocking for the phase-1 checkpoint that already exists.
- **Required before any public security-clean claim or release.**
- **Recommended placement: a parallel safety lane during Batch 0–1.** The fix touches `Content/`
  only, and **no integration lane touches any file either finding lives in** — not
  `ContentManager.cpp`, `ContentReader.cpp`, `SongContentTypeReader.cpp`,
  `VideoContentTypeReader.cpp` or `PathContainment.hpp` — so it cannot conflict with the campaign.
  The alternative is the first post-integration stabilization batch.
- **Conditional integration blocker.** Five lanes touch Content-*adjacent* files (`skia` in
  production, `html-dom`/`gdi`/`sokol`/`diligent` in tests only). `skia`'s
  `Texture2DContentTypeReader.cpp` change was inspected and contains no path-resolution logic, so it
  is not a blocker today — but re-check per lane at adaptation time rather than trusting this
  sentence.
- **Must be closed before the Batch-5 stabilization checkpoint.**
- **Re-checked per lane, three times so far, and still not blockers.** `depthcrt` touches no content
  path resolution; `gltf` and `ext` change no code at all. All three re-checks are recorded on their
  lane cards. `gltf` is the case worth naming explicitly: `gltfissues.md` *discusses* the glTF import
  path at length, but discussing a code path introduces no path-resolution code — and `ext` is the
  same shape, a design document that names `IGraphicsBackend` additions without adding any. **Both
  findings remain OPEN, HIGH / P1, and visible.**

### `WEBGPU-115` — closed and re-verified in source

```
before:  [ GFX-209 ] WebGPU wireframe: total=18176 interior=1089/1089   (byte-identical to Solid)
after:   [ GFX-209 ] WebGPU wireframe: REJECTED, target total=0 interior=0/1089
```

`SupportsCapability(WireFrame)` returns `false`; `RequireSupportedFillModeEXT` is the first statement
of all five public 3D draw entry points, so nothing is queued, keyed, created, encoded or submitted.
Line and point topologies are deliberately still accepted. `WebGpuWireFrameContract.*` **9/9**.

### Corrected at exit reconciliation — three stale records

1. **`REMED-GFX-172` is DONE** (fix `25bb5ecc`, closure `92546670`), not OPEN as the progress table
   and the previous exit record both had it.
2. **`REMED-GFX-137`/`-139` were never actually classified** despite a note claiming they were.
   Both now classified, neither blocks.
3. **`REMED-GFX-132`'s scope was too narrow** — the tool link failure reproduces under **bgfx** as
   well as ASCII. Pre-existing, tool-only, blocker NO.

### Everything else is clear

- `WEBGPU-115` **DONE**. `REMED-GFX-209` **DONE**; `-211`/`-212`/`-213`/`-215`/`-216` **DONE**;
  `REMED-GFX-DECL-GUARD` **DONE**.
- `REMED-GFX-217`/`-218` checkpoint blockers **RESOLVED** (translators still deferred).
- `REMED-GFX-203`…`-208`, `-210`, `-214` **DEFERRED**; `REMED-GFX-219` **OPEN, LOW/P3, blocks
  nothing, and was deliberately left untouched by `WEBGPU-115`** — GFX-219 *under*-reports a
  capability EasyGL genuinely has, `WEBGPU-115` *over*-reported one WebGPU lacks. Opposite safety
  directions; still must not be bundled.
- `REMED-BUILD-012` platform-blocked (Wine/vkd3d-proton) — D3D12 is cross-build-only and is **not**
  called clean.
- `feature/gl` needs the **MetaGL → EasyGL → CNA** order first; EasyGL (`rvc` @ `b52f671`) and MetaGL
  (`feature/followup-audit` @ `d5bc155`) are **development-complete**, merely unmerged into their own
  `develop` branches. Both heads re-verified unchanged after a fresh fetch in each repository
  (16 and 5 commits ahead of their `develop`, 0 behind, neither merged). One correction: the MetaGL
  redirect lives in an **uncommitted** working-tree edit to `easy-glrvc/CMakeLists.txt`, not in
  committed history — reconcile it at step 7 of the inventory §6.2 sequence.
- **Magnum and Wicked are audit-stacked lanes**, not develop-forked backends: both fork from
  `feature/audit` @ `2338b44f7` and add only **13** and **10** commits of their own, so each needs a
  22-commit rebase onto the checkpoint base before anything else. Neither is established as
  integration-ready.

---

# NEXT.md — `feature/graphics` session handoff (2026-07-18)

> **This section is current for `feature/graphics` (this checkout).** Everything below the
> `---` divider that follows is stale, D3D9-branch-scoped content (`feature/dx9`) that ended up
> merged into this file's history via `develop` — it is NOT about this branch and should not be
> read as current status here. Left in place rather than deleted since it may still be a useful
> historical record for the D3D9 work itself; just don't confuse it with the section below.
>
> ## Session summary (2026-07-17 → 2026-07-18) — read this first on a clean context
>
> Three large, sequential efforts landed this session, each fully merged into both
> `feature/graphics` and `develop`, pushed to `origin`:
>
> 1. **`plan_cnj.md` Phase 14 completed in full** — glTF import gaps (multi-UV-set diagnostics,
>    morph target CLI/.cnj serialization, CUBICSPLINE interpolation, DualTextureEffect occlusion
>    fix, Draco mesh compression, angle-weighted tangent generation, `KHR_texture_transform`/
>    `KHR_lights_punctual`/`KHR_materials_emissive_strength`), then **PBR (`PbrEffect`/
>    `SkinnedPbrEffect`) + `SkinnedEffect.VertexColorEnabled` ported to all 7 remaining graphics
>    backends** (Vulkan, Bgfx, SdlGpu, WebGPU unskinned-only, D3D9/D3D11/D3D12 compile-verified
>    only). See `plan_cnj.md`'s own top banner and Phases 14A–14J for full detail — this file does
>    not duplicate it.
> 2. **`plan_webgpu.md` grew substantially** — render state (blend/rasterizer/cull/wireframe/
>    scissor/viewport/sampler/depth-stencil), `RenderTarget2D`/`RenderTargetCube`, MSAA,
>    `EnvironmentMapEffect`, real instancing, `Texture3D`, `Texture2D`/`TextureCube` `GetData()`
>    readback, and real linear-filtered mip generation. **`plan_webgpu.md`'s own top banner has a
>    full, current "Remaining work" summary — read it directly, don't re-derive it here.** Only
>    genuinely open item found requiring cross-backend design work: compressed (DXT/BC) texture
>    upload needs a shared `ImageData`/`Texture2D.cpp` change, not a backend-local fix (no CNA
>    backend anywhere does real native compressed upload today).
> 3. **`plan_graphics.md` Task 863 closed** — `Texture3D`/`TextureCube` now inherit `Texture`
>    (matching FNA), closing the "cannot be sampled by any shader" architectural gap. EasyGL-only
>    implementation (`BindTexture3D`, mirroring the existing `BindTextureCube`/Task 1081 path);
>    Vulkan/Bgfx/WebGPU/SDL_Renderer/D3D9-12 are explicitly deferred follow-ups, not started.
>    **Found and logged a genuine, unrelated pre-existing bug while independently re-verifying
>    this task**: see Task 1115 below.
>
> **Operational notes worth knowing before continuing on this machine:**
> - Run test/graphics-window commands with `DISPLAY=:99` (a dedicated Xvfb display), never `:0` —
>   check `CNA_TEST_DISPLAY` in each build dir's `CMakeCache.txt` isn't stale before trusting a
>   `ctest` run; `ctest --test-dir <dir>` also changes each test's CWD, breaking fixture-relative
>   paths — run `CnaTests` directly from the repo root instead when in doubt.
> - This machine is shared with other concurrent Claude Code agents — cap build parallelism at
>   `-j4`, never `-j$(nproc)`; pause starting new heavy work at CPU Tctl ≥85°C (resume at ≤75°C,
>   but always finish in-flight work regardless of temperature); low free RAM alone isn't urgent
>   if swap still has headroom.
>
> **Remaining open items in `plan_graphics.md`** (35 `⬜` + 1 `🟨`, as of 2026-07-18 — re-grep
> `plan_graphics.md` for `⬜|🟨` to confirm this hasn't drifted before trusting it blindly):
>
> **New, found this session, no architecture decision needed:**
> - **Task 1115** — `EasyGL_AvatarRenderer_TintRouting`/`cna_test_avatar_tint_routing` fails with
>   an almost-exact "2x expected brightness, then clamped to 255" pixel pattern — a real, specific,
>   currently-unexplained defect (probably some tint/color value applied twice in
>   `AvatarRenderer`'s real tint path), NOT the same bug Task 908 already fixed (that one was "no
>   rendered content at all" from CCW culling). Confirmed pre-existing and unrelated to anything
>   from this session (reproduces byte-identically before/after Task 863's merge). Not
>   investigated further — needs its own dedicated task to trace where the doubling happens.
>
> **Real bugs on specific backends (each independently scoped, no architecture decision needed):**
> - **869** — `GraphicsDevice` state properties (`BlendState`/`DepthStencilState`/`RasterizerState`)
>   use value semantics instead of FNA's reference semantics.
> - **872** — `GraphicsDevice.ReferenceStencil` isn't a real, independent, backend-connected
>   override (Task 319 finding).
> - **890** — `EnvironmentMapEffect` doesn't forward `DirectionalLight1`/`DirectionalLight2` on any
>   of the 3 backends.
> - **893** — `SkinnedEffect` doesn't forward `DirectionalLight1`/`DirectionalLight2` either.
> - **894** — `SkinnedEffect` has no real specular highlights.
> - **895** — `SkinnedEffect.WeightsPerVertex` is a complete GPU no-op on all 3 backends.
> - **933** — EasyGL: a full-backbuffer `SpriteBatch` draw before any 3D draw in the same frame
>   breaks that frame's 3D rendering entirely (see `DEFERRED.md`).
> - **952** — Bgfx: `RenderTargetCube`'s depth buffer doesn't gate face draws (surfaced fixing
>   Task 951's `Bgfx_RenderTargetCube_DepthFormat` crash).
> - **917** — Bgfx occlusion queries can't measure true scene-depth visibility (shares a view/depth
>   buffer with other submitted geometry).
> - **1113** — `GraphicsDevice::Clear(ClearOptions, ...)` masks `DepthBuffer`/`Stencil` out of the
>   request incorrectly depending on the active target's depth-stencil support.
> - **1114** — Bgfx: `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` unconditionally
>   throw via `ThrowNo3DState()` in cases where they shouldn't.
>
> **Large, standalone sub-project (10 tasks, not a quick pick-up):**
> - **10200–10209** — MojoShader: vendor `third_party/mojoshader`, a C++ wrapper around
>   `mojoshader_effects.c`'s parser (compiled `.fxb` → technique/pass/parameter reflection), then
>   wire it through EasyGL (GLSL)/Vulkan (needs a vendored GLSL→SPIR-V compiler, `10203`
>   feasibility not yet decided)/Bgfx (needs its own `shaderc` investigation, `10205`), plus
>   `Effect::Clone()` (folds in Task 883's `EffectPass::owner_` re-binding hazard), real compiled
>   test fixtures, and docs. This is a full compiled-XNA-effect-bytecode feature, comparable in
>   scope to a phase of its own.
>
> **Verification/content tasks (need real test assets, not just code):**
> - **938/943/944** — skinned-model verification via `Content.Load<Model>` (SplitScreen,
>   SkinningSample) and a real FBX/X skeletal-animation → CNA-schema conversion tool.
> - **474/475/477/478** — DEFERRED, generate reference values for `BasicEffect` defaults/lighting
>   constants and `SpriteFont.MeasureString`, plus reference screenshots for SpriteBatch/BasicEffect
>   — all share the same documented prerequisite blocker (see each row for detail).
>
> **Infrastructure / lower-priority:**
> - **919** — wire the `GraphicsSmoke` CTest label into real CI (e.g. a
>   `.github/workflows/graphics-smoke-ci.yml`).
> - **920** — 2 Android-NDK build regressions in the sibling `sharp-runtime` repo blocking the
>   entire CNA/Android cross-compile.
> - **1108** — Software backend: real per-vertex-lit CPU rasterizer path.
> - **1109** (🟨) — regenerate/update every existing lit-scene pixel-test baseline across all
>   touched backends once their own dispatch honors the real default.
> - **1110** — decide scope: which `SurfaceFormat` values justify oracle coverage, and whether
>   `Texture2D` needs new construction/`SetData` paths for them.
>
> ---

# NEXT.md — CNA Project Handoff (`feature/dx9` branch — Direct3D 9 backend only)

> **This `NEXT.md` is scoped to the D3D9 backend only, per explicit project-owner instruction
> (2026-07-14).** This branch (`feature/dx9`, worktree `cnadx9`) is a parallel effort to the
> established EasyGL/Vulkan/Bgfx/SDL_Renderer/WebGPU/Headless/Software/D3D11/D3D12 backends, all of
> which are developed on other branches (`develop` and friends) and are **not tracked here**. For
> their status, see `plan_graphics.md`, `plan_dx.md`, `plan_webgpu.md`, `plan_software.md`,
> `plan_headless.md`, and `git log` on those branches — this file will not duplicate it, and will
> not be updated for non-D3D9 work. Full D3D9 task-by-task detail and history lives in
> **`plan_dx9.md`** (`D9-0`–`D9-140`); this file is a short current-state index, the same relationship
> `plan_dx.md`/`NEXT.md` had for D3D11/D3D12 before this branch existed.
>
> **Status (2026-07-14): implementation authorized, Phase D9-0 spikes closed, no backend code written
> yet.** The project owner has authorized implementation through Phase D9-13 (`plan_dx9.md`'s own
> "Boundaries" still require asking before Phase D9-11 "custom `ShaderEffect`"; Phase D9-14 needs real
> Windows hardware and is `needs_human`). The plan's one architectural blocker — the
> `IGraphicsBackend`/`GraphicsBackendCreateArgs` boundary problem — is also resolved: an additive
> extension (new optional presentation-parameter fields + a narrow device-event notification channel)
> is approved, unblocking `D9-30`/`D9-32`/`D9-33`/`D9-34`. See `plan_dx9.md`'s top banner and "The
> `IGraphicsBackend` boundary problem" section for the full record.

---

> **Separate, unrelated track — `plan_graphics.md` Phase 78 (DEFERRED.md item #11, HLSL→GLSL sample
> shader conversion) is now FULLY COMPLETE, as of 2026-07-16 (EasyGL only).** This is completely
> independent of the D3D work above — it unblocks samples catalogued in `plan_samples.md`
> (`../cna-samples`' own 153-sample re-audit), not `plan_dx.md`. **Task 945 decided** (project
> owner, 2026-07-16): manual line-by-line HLSL→GLSL porting, no `SPIRV-Cross`/`dxc` pipeline — every
> HLSL construct hit across every shader ported turned out to be a mechanical 1:1 substitution.
> **Task 947 is now 13/13 — every sample originally blocked purely by DEFERRED.md #11 has its
> shader(s) ported and pixel-verified**: `NetRumble`, `PerPixelLighting`, `VertexLighting`,
> `DistortionSample`, `NonPhotoRealistic`, `ShadowMapping`, `NormalMapping`, `BillboardSample`,
> `ShatterEffect`, `Particles3D`, `XmlParticles`, `ShipGame`, `InstancedModel` (`BloomSample`, the
> 14th sample under the same DEFERRED.md #11 umbrella, was already closed earlier via Task 946).
> Along the way, 4 new backend capabilities were added and closed, all EasyGL-only, all additive
> (Vulkan/Bgfx/SDL_Renderer untouched): **Task 1079** (wires `ShaderEffect` into `GraphicsDevice`'s
> 3D draw path, not just `SpriteBatch`), **Task 1080** (genuinely custom vertex layouts for that
> path, not just the 5 fixed byte-strides), **Task 1081** (`TextureCube` sampling for custom
> shaders), **Task 1082** (real GPU hardware instancing — `glVertexAttribDivisor`-driven per-instance
> vertex streams). **What remains is explicitly NOT `cna_graphics` scope**: the actual sample ports
> (`.cpp`/`.hpp`/`Content/` under `../cna-samples/samples/<Name>/`) for these 13 (now-unblocked)
> samples still need to be written in the sibling `../cna-samples` repo, tracked in that repo's own
> plan file, not here or in `plan_graphics.md`/`plan_samples.md`. `plan_samples.md` also still has
> ~88 other `⬜` rows unrelated to this shader-conversion track (re-verification passes, other
> DEFERRED.md items, etc.) — untouched by this work, standing backlog. Full detail: `plan_graphics.md`
> Task 947's own row (chronological per-shader history, discriminating-power mutation testing for
> every one) and Tasks 1079–1082's own rows; `plan_samples.md` for the per-sample CNA-gap tracking.

> **Separate, unrelated track — `feature/input` branch, `audit_input.md` remediation + full
> phase-by-phase FNA-parity audit, in progress as of 2026-07-17.** Completely independent of the D3D9
> work below (this `NEXT.md`/`plan_dx9.md` pair is D3D9-only) — tracked in full in `plan_input.md`,
> not duplicated here. **Status: this plan is now CLOSED as of 2026-07-17** — Phases 0-10, 12, and 13
> are fully closed and pushed (`P0-001..020`, `P1-001..045`, `P2-001..060`, `P3-001..045`,
> `P4-001..070`, `P5-001..045`, `P6-001..045`, `P7-001..040`, `P8-001..040`, `P9-001..035`,
> `P10-001..025`, `P12-001..015`, `P13-001..006` — 490/505 tasks total, 15/505 correctly `[!]`
> Blocked (all of Phase 11, hardware-gated, never marked done speculatively), 0 remaining `[ ]`;
> latest pushed commit `1746df1e` on `feature/input`). **Merge recommendation (P12-014): merge the
> audit work itself; do not yet declare "Input stable"** per `docs/input-pre-merge-checklist.md`'s
> own release gate, which requires real-hardware validation (0/15 Phase 11 checks performed) —
> final decision is the user's. If further work on this track is wanted: Phase 11's 15 tasks need an
> actual human at a real keyboard/mouse/controller/touchscreen (see
> `docs/input-manual-verification-results.md`'s recording template); everything else is done. Phases
> 8/9 left 4 persistent verification build directories in place — `cmake-build-input-easygl/`,
> `cmake-build-input-vulkan/`, `cmake-build-input-bgfx/`, `cmake-build-input-asan/`
> (`-DCNA_SANITIZE=address,undefined`), plus Phase 12 added `cmake-build-input-sdlrenderer/` — all
> already anticipated in `.gitignore`, alongside the pre-existing default `cmake-build-debug/`
> (`SDL_RENDERER`); reuse these directly for any future non-default-backend/sanitizer check.
>
> **IMPORTANT — separate, out-of-Input-scope finding from P9-031 (2026-07-17):** running the full
> unfiltered `CnaTests` binary (not the Input-filtered subset) crashes reproducibly with `double free
> or corruption (fasttop)` (SIGABRT) inside the Net subsystem's `ENetBackendTest` suite. Confirmed via
> isolation testing this is **not an Input bug**: `ENetBackendTest.*` passes cleanly run alone; the
> corruption requires ~800 preceding tests' allocation history to manifest, consistent with heap
> corruption originating earlier and only detected when the allocator's consistency check next fires.
> Every Input-filtered run this session (9 phases, dozens of invocations, including under
> AddressSanitizer+UndefinedBehaviorSanitizer) has been 100% clean. This is a real, separate memory-
> safety defect needing dedicated cross-subsystem bisection — flagged, not fixed, since it is unrelated
> to and out of scope for this track. See `plan_input.md`'s P9-031 Result for full reproduction detail.
> Each phase closes with a
> checkpoint task (`P{N}-0XX — Phase N checkpoint and summary`) recording pass/fail counts, files
> changed, and follow-ups — read the **last completed phase's checkpoint Result** for the most
> efficient overview, then check `plan_input.md`'s Phase overview table for the next open phase's
> starting task ID. Commits are per-phase (one `git commit` per closed phase); `git log --oneline` on
> `feature/input` is the index. A whole-file status/Result consistency check (see any recent commit's
> diff for the Python snippet, run before every commit) is a standing safety net — a prior session hit
> an unexplained checkbox-revert bug once, never repeated since. Later phases (4-6) found dramatically
> fewer gaps than Phases 1-3 (Phase 4 and most of Phase 5 needed **zero** code/test changes — the
> pre-existing GamePad/Touch test suites were already exhaustive from earlier session work); when a
> phase like that produces no diff beyond `plan_input.md` itself, that is a genuine, verified outcome
> (each task still gets independent evidence — re-derived FNA cross-checks, not just re-reading old doc
> claims), not a shortcut. One recurring authoring mistake to avoid: writing multi-paragraph Result text
> by hand (via a direct `Edit` call rather than the batch Python script) has twice left stray `"`
> line-wrap artifacts in the text — always grep `^"` after a manual multi-line edit and fix before
> committing. Thermal pacing rule in effect: pause new heavy work (builds, large audits) at CPU Tctl
> >=85°C, resume at <=75°C (`sensors | grep Tctl`). Test-verification note: this session's cumulative
> `xvfb-run` usage (dozens of invocations) has caused elevated-but-non-failing `GTEST_SKIP` counts on
> video-dependent tests in later phases (host X11/Xvfb resource pressure, not a code regression —
> confirmed via isolated single-test sanity checks each time); zero `[  FAILED  ]` lines have appeared
> in any run this session. If resuming this track: read `plan_input.md`'s Phase overview + the last
> `[x]`-marked checkpoint task's Result for the exact stopping point, not this file.

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend layer. This branch
adds a **Direct3D 9** backend — see `plan_dx9.md` for the full plan. Unlike every other CNA backend,
this one is not a coverage/parity effort: its stated goal (set by the project owner) is that a CNA
game running on D3D9 be **indistinguishable** from the same game running on the original XNA 4.0
runtime, verified against a real XNA 4.0 oracle running under Wine (Phase D9-A), not just "renders
plausibly."

- **Key decisions already made** (see `plan_dx9.md` design decisions 1–17 for the full rationale):
  - Plain `Direct3DCreate9`, **not** D3D9Ex — `D3DPOOL_MANAGED` for user resources so they survive
    `Reset()`, and the real XNA device-lost lifecycle (`DeviceLost`/`DeviceResetting`/`DeviceReset`)
    is implemented for real, for the first time in this project.
  - Microsoft's own XNA 4.0 Stock Effects HLSL (`BasicEffect.fx` and 5 siblings, from the FNA tree)
    are **vendored verbatim** and compiled by CNA itself (`D3DCompile`, `vs_2_0`/`ps_2_0`) — not
    reimplemented, not ported. The `.fxb` shipped bytecode is a verification oracle only.
  - `D3DCommon` (shared with D3D11/D3D12) is **not** expanded — D3D9 gets its own
    `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations`.
  - Render state, not state objects (`SetRenderState`/`SetSamplerState` sequences — no D3D9 state
    objects exist to cache).
  - This is the **only** CNA backend that can natively answer `GraphicsAdapter::IsProfileSupported()`
    for real (`D3DCAPS9`) — Phase D9-10.
- **A cross-cutting finding, not this plan's to fix**: taking XNA seriously as the spec surfaced six
  confirmed CNA-vs-XNA divergences that exist on **every** CNA backend today (worst: CNA always
  lights per-pixel; XNA's default is per-vertex, and CNA has no per-vertex lighting shader anywhere).
  This plan measures and reports them (Phase D9-A6, `D9-81`); it does **not** fix them — that is a
  `plan_graphics.md`-level, project-owner decision. See `plan_dx9.md`'s "CNA's divergences from XNA
  4.0" section before touching any of this.

---

## 2. Current status

### Build status

| Build dir | Backend | Status |
|---|---|---|
| `cmake-build-d3d9` | D3D9 (Windows cross-compile, MinGW-w64) | **Verified clean 2026-07-15**: `cmake -DCNA_GRAPHICS_BACKEND=D3D9 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_BUILD_TESTS=ON` configures; `CNA`/`cna_backend_graphics_d3d9` and all 11 D3D9 test binaries build clean. `D3D9_Common` 29/29 + `D3D9_ShaderDispatch` 23/23 + `D3D9_Smoke` 55/55 + `D3D9_Draw` 3/3 + `D3D9_DrawEx` 17/17 + `D3D9_ShaderCache` 6/6 + `D3D9_Instanced` 4/4 + `D3D9_BlendState_Opaque`/`D3D9_BlendState_AlphaBlend`/`D3D9_DepthStencilState_StencilEnable`/`D3D9_RasterizerState_CullMode` (1 check each, reused EasyGL sources) all pass via `ctest --test-dir cmake-build-d3d9 -L D3D9` (11 CTests). A real device now creates, clears, presents, reads back pixels, resizes, recovers from a (simulated) device-lost event, round-trips real vertex/index buffer data, round-trips real 2D/cube/volume texture data (including a genuinely non-power-of-two texture), creates/binds/clears/reads back real 2D/cube/MSAA render targets, binds a real 2-target MRT set, runs a real occlusion query, applies real sampler state, creates all 66 real Microsoft stock-effect shaders through a live device, correctly replicates XNA's own shader-permutation selection logic for all 5 effects, draws its first real 3D triangle (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`), draws real effect-aware geometry for **all 5 XNA Stock Effects** (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` via `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` — textured, vertex-color, multi-light and one-light vertex-lit, fog, alpha-test clip pass/fail, two-sampler doubling-blend, cube-map env-map blend, per-vertex bone-matrix skinning, all pixel-exact against hand-computed expected colors), draws real hardware-instanced geometry (`DrawInstancedPrimitivesEx` via `SetStreamSourceFreq`, CNA's own NOXNA instancing shader, two genuinely distinct per-instance transforms proven pixel-exact in one draw call), genuinely toggles the depth test/write via `SetDepthTestEnabled`/`SetDepthWriteEnabled` (a real 2026-07-15 bug fix, proven by a near/far occlusion discriminator), and reuses the same backend-agnostic EasyGL blend/depth-stencil/rasterizer-state pixel tests D3D11/Vulkan already share, all through the actual public `Game`/`GraphicsDeviceManager`/`GraphicsDevice` API (or, for the shader cache/dispatch, the backend's own real device handle or pure functions). |

### Phase D9-0 — feasibility spikes: CLOSED 2026-07-14

| Task | Status |
|---|---|
| `D9-1` — real Microsoft `d3dcompiler_47.dll` compiles all 66/66 stock-effect entry points | ✅ |
| `D9-73` — 61/66 byte-identical to Microsoft's shipped `.fxb`; decision made (CNA compiles its own) | 🟨 (decided; 5 `PixelLighting` variants still need oracle-proof, `D9-73`'s own obligation) |
| `D9-A1`/`D9-A2` — real XNA 4.0 runs under Wine and renders a verified `CornflowerBlue` triangle | ✅ |
| `D9-2` — confirm minimum link set (`d3d9` alone, no `dxguid`) | ✅ |
| `D9-3` — Wine+DXVK D3D9 loop end-to-end: exact pixel round-trip + full `D3DCAPS9` dump | ✅ |
| `D9-4` — `D3DPOOL_MANAGED` genuinely `LockRect`-readable and survives `Reset()` intact | ✅ |
| `D9-5` — `scripts/run-wine-dxvk9.sh` (new script, DXVK-marker gate, positive+negative proven) | ✅ |

**Phase D9-0 is fully closed.** Next up: Phase D9-1 (CMake integration + backend skeleton).

### Phase D9-A — the XNA 4.0 oracle: D9-A1–A4 closed, D9-A5 started (31 scenes, all 5 Stock Effects + fog + all 8 AlphaTestEffect.AlphaFunction values (COMPLETE) + EnvironmentMapEffect fresnel + SkinnedEffect all 3 WeightsPerVertex values (COMPLETE) + SpriteBatch core draw path + address modes + 3 of 5 SpriteSortMode values + multi-texture batching + ALL 4 PrimitiveType values (COMPLETE)), D9-A6 CLOSED 2026-07-16 (EasyGL measured: 10/31 pixel-perfect, 21/31 diverge — see `docs/d3d9-divergence-report.md`)

| Task | Status |
|---|---|
| `D9-A1` — stand up real XNA 4.0 under Wine | ✅ |
| `D9-A2` — minimal XNA 4.0 reference app, no content pipeline | ✅ |
| `D9-A3` — byte-for-byte equivalent CNA app, shared declarative scene format | ✅ |
| `D9-A4` — `scripts/xna-diff.py`, DXVK-into-XNA-prefix prerequisite | ✅ |
| `D9-A5` — growing scene corpus | 🟨 (31 scenes, all 5 XNA Stock Effects + `IEffectFog` + ALL 8 `AlphaTestEffect.AlphaFunction` values (`Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/`Always` on `PSAlphaTestLtGt`, `Equal`/`NotEqual` on `PSAlphaTestEqNe` — `AlphaTestEffect` compare-function coverage COMPLETE) + `EnvironmentMapEffect.FresnelFactor` + `SkinnedEffect` ALL 3 `WeightsPerVertex` values (`1`/`2`/`4` — `SkinnedEffect` weighting coverage COMPLETE) + `SpriteBatch` core draw path, address modes, 3 of 5 `SpriteSortMode` values, and multi-texture `FlushBatch()`-on-texture-change batching (`D9-90`/`D9-91`/`D9-92`/`D9-93` all CLOSED; `D9-93` covers `Deferred`/`BackToFront`/`FrontToBack`, `Immediate`/`Texture` explicitly scoped out — see `plan_dx9.md` D9-93's own closure note) + ALL 4 `PrimitiveType` values (`TriangleList`/`TriangleStrip`/`LineList`/`LineStrip` — `PrimitiveType` coverage COMPLETE) represented: `colored3d`, `textured_quad`, `lit_textured_quad`, `alphatest_quad`, `alphatest_less_quad`, `alphatest_equal_quad`, `alphatest_notequal_quad`, `alphatest_greaterequal_quad`, `alphatest_lessequal_quad`, `alphatest_never_quad`, `alphatest_always_quad`, `dualtexture_quad`, `envmap_quad`, `envmap_fresnel_quad`, `skinned_quad`, `skinned_twobone_quad`, `skinned_fourbone_quad`, `multilight_textured_quad`, `fog_gradient_quad`, `sprite_basic_quad`, `sprite_rotated_quad`, `sprite_flipped_quad`, `sprite_wrap_quad`, `sprite_mirror_quad`, `sprite_sortmode_deferred_quad`, `sprite_sortmode_backtofront_quad`, `sprite_sortmode_fronttoback_quad`, `sprite_multitexture_quad`, `colored_trianglestrip_quad`, `colored_linelist_quad`, `colored_linestrip_quad`, all pixel-perfect) |
| `D9-A6` — run the corpus against CNA's other backends too | ✅ (EasyGL: 10/31 pixel-perfect, 21/31 diverge; Vulkan/D3D11 not yet measured) |

Closed 2026-07-15 (`D9-A3`/`D9-A4`): built the shared declarative scene format `D9-A3`'s own text
demanded (`tools/xna-oracle/scenes/*.scene`, minimal `key=value` text, no JSON library needed
since the XNA-side build environment is GAC-only .NET 4.0 with no NuGet), parsed identically by
both a rewritten, scene-driven `tools/xna-oracle/Oracle.cs` (moved from `dx9-spike/xna-oracle/`)
and a new `tools/xna-oracle/CnaOracleRender.cpp` (built via CNA's real public `Game`/
`GraphicsDeviceManager`/`GraphicsDevice`/`BasicEffect` API, `cna_oracle_render` CMake target, not
a CTest — no pass/fail of its own). Installed DXVK into the XNA oracle's own Wine prefix
(`~/.wine-cna-xna40`, `dxvk-setup install`, 32-bit `dxvk-wine32`) — `D9-A4`'s own critical
prerequisite, confirmed by the adapter string flipping from WineD3D's spoofed `ATI Radeon HD 5600
Series` to the real `AMD Radeon 780M (RADV PHOENIX)`, so both sides now execute through the same
DXVK D3D9→Vulkan path. New `scripts/xna-diff.py` (needs Pillow), `--tolerance` defaults to `0`,
mutation-verified (a 1-off-mutated PNG correctly fails at tolerance 0, correctly passes at
tolerance 1).

**Result: both oracle comparisons landed so far are pixel-perfect.** `colored3d` (`D9-A2`'s own
original triangle) and `textured_quad` (new: `BasicEffect.TextureEnabled=true`, a tiny 2×2
point-filtered checkerboard) each render **byte-identical** on both sides — `0/65536` pixels
differ, max per-channel delta `0`, confirmed by full sweeps not just spot checks (including the
exact UV=(0.5,0.5) point-filter texel-boundary pixel for `textured_quad` — both sides independently
pick the identical texel there). Extended the scene format to a second vertex shape
(`vertexformat=PositionColor`/`PositionTexture`) and inline procedural texture data on both sides
for `textured_quad`. **Real bug found and fixed while writing it, in `Oracle.cs` itself**: the
original `ParseBool` used a C# 6 expression-bodied member (`=> s == "true";"`), which the real
in-prefix `csc.exe` (.NET Framework 4.0-era, pre-C#-6) rejected outright (`CS1002`/`CS1519`) —
meaning `D9-A3`'s own original "pixel-perfect" claim had never actually been verified against the
rewritten, scene-driven `Oracle.cs`, only against the old hardcoded `dx9-spike` spike. Fixed
(ordinary block-bodied method), recompiled, re-ran `colored3d` through the real current `Oracle.cs`
and reconfirmed `0/65536` — closing that verification gap. This is the first evidence this
backend's `BasicEffect` TEXTURED dispatch is also genuinely indistinguishable from real XNA 4.0.
`D9-A5`'s corpus now has 3 scenes, by design ("growing with the plan," not attempted all at once)
— see `tools/xna-oracle/README.md`.

**3rd scene, `lit_textured_quad` (2026-07-15) — also pixel-perfect.** Extended the scene format
again to a third vertex shape (`vertexformat=PositionNormalTexture`, matching `VSInputNmTx`'s
Position+Normal+TexCoord shape and the existing stride-32 CNA vertex layout) plus
`ambientcolor`/`light0enabled`/`light0diffuse`/`light0direction` keys wired to
`BasicEffect.AmbientLightColor`/`DirectionalLight0` on both sides. Deliberately dimmed the light
(`diffuse=0.5`, no ambient) rather than a bright one — a first draft saturated to full intensity,
making the lit result indistinguishable from the raw texture and proving nothing about whether the
lighting math is genuinely applied; the dimmed version visibly halves the texture color
(`(255,0,0)`→`(128,0,0)`) and still matches real XNA exactly, `0/65536` pixels differ. First
evidence this backend's lit+textured `BasicEffect` dispatch (`D9-82b`'s own "lit+textured" checks,
previously only hand-verified against a hand-computed expected pixel) is genuinely
indistinguishable from real XNA 4.0.

**4th scene, `alphatest_quad` (2026-07-15) — also pixel-perfect, first non-`BasicEffect` Stock
Effect in the corpus.** Added `effect=BasicEffect`/`AlphaTestEffect` plus `alphafunction`/
`referencealpha` keys wired to `AlphaTestEffect.AlphaFunction`/`ReferenceAlpha`, reusing the
existing `PositionTexture` vertex shape. A 2×2 texture whose 4 texels straddle
`ReferenceAlpha=128` (alpha `255`/`0`/`255`/`64`, `AlphaFunction=Greater`) exercises `clip()`'s
real discard end to end — passing texels show their own color, failing texels show the clear
color through the discard, `0/65536` pixels differ.

**Real bug found and fixed live in `CnaOracleRender.cpp`, a dangling-pointer bug, not a backend
bug.** `GraphicsDevice::DrawUserPrimitives()` reads `GpuDrawParams` from `currentEffect_`, a raw
pointer `Effect::Apply()` sets. Adding the second effect type scoped the constructed
`BasicEffect`/`AlphaTestEffect` inside an `if`/`else` block, destroying it at the closing brace —
before the shared `DrawUserPrimitives()` call further down read the now-dangling pointer. Symptom:
`textured_quad`/`lit_textured_quad` (both previously passing, unrelated to this change) started
throwing with flags matching NEITHER scene's actual settings (stale stack memory); `colored3d`
happened to still pass by pure allocation-timing luck, not correctness. Fixed by declaring both
possible effect objects as `std::unique_ptr` at `Draw()`'s own top level so whichever gets
constructed survives every draw call in the function; re-verified all 4 scenes pixel-perfect
afterward. `Oracle.cs`'s own `Effect fx;` was never at risk (C# is GC-managed, not scope-based).

**5th scene, `dualtexture_quad` (2026-07-15) — also pixel-perfect, 2nd non-`BasicEffect` Stock
Effect, and the first scene needing a vertex shape NEITHER side had a built-in type for.** Added
`effect=DualTextureEffect` plus `texture2*`/`diffusecolor` keys, and a new
`vertexformat=PositionDualTexture` (Position+TexCoord0+TexCoord1, stride 28, `VSInputTx2`). Real
XNA has no built-in dual-UV vertex struct either, so both sides define their own custom
`IVertexType`/`VertexDeclaration` — exactly what a real game using `DualTextureEffect` has to do.
Two 1×1 solid-color textures (white, `(100,60,20)`) + `DiffuseColor=(0.5,0.5,0.5)`: the real
doubling-blend formula (`texture0 * texture1 * 2 * DiffuseColor`) makes the `*2*0.5` cancel out, so
the expected result is exactly `(100,60,20,255)` — hand-derived *before* running either side,
matching `D9-82d`'s own already-proven check value, then confirmed pixel-for-pixel, `0/65536`
differ. Added as a third `std::unique_ptr` alongside `alphaFx`/`basicFx`, correctly avoiding a
repeat of `alphatest_quad`'s own dangling-pointer bug — no new bug this time.

**6th scene, `envmap_quad` (2026-07-15) — also pixel-perfect, 3rd non-`BasicEffect` Stock Effect,
and a real API-surface finding (not a bug) this time.** Added `effect=EnvironmentMapEffect` plus
`environmentmap*` keys, reusing the existing `PositionNormalTexture` shape (no new vertex format
needed — `D9-82e`'s own finding). 1×1 base texture + 1×1 `TextureCube` (all 6 faces the same
color, `D9-82e`'s own `reflect()`-geometry-sidestep trick) + one dim light,
`EnvironmentMapAmount=0.5`: real formula `lerp(texture*diffuseSum, environmentMap,
environmentMapAmount)` produced exact `(164,114,89,255)` on both sides, `0/65536` differ.

**Real finding: real XNA/FNA's `EnvironmentMapEffect` implements `IEffectLights.LightingEnabled`
via explicit interface implementation** — invisible on the concrete class's public C# surface
(confirmed live: `emfx.LightingEnabled = ...` is a genuine `CS1061` against the real `csc.exe`;
FNA's own source: `set { if (!value) throw new NotSupportedException(...); }` — lighting is always
on, no game can disable it). CNA's own `setLightingEnabledProperty` already matches this exact
behavior faithfully (getter always `true`, setter throws given `false`) — only the *visibility*
differs (C++ has no explicit-interface-implementation hiding), not the behavior. Neither side
calls it for this effect now, matching what a real game actually can do.

**7th scene, `skinned_quad` (2026-07-15) — also pixel-perfect. MILESTONE: every one of XNA's 5
Stock Effects is now represented in the corpus, all pixel-perfect.** Added `effect=SkinnedEffect`
plus a fourth custom vertex shape (`vertexformat=PositionNormalTextureWeights`, stride 52,
`VSInputNmTxWeights`, matches the existing stride-52 layout byte-for-byte — no new CNA vertex
declaration needed, `D9-82f`'s own finding). Real XNA has no built-in skinned vertex struct either
(same category as `DualTextureEffect`'s dual-UV gap), so both sides define their own custom type.
Deliberately uses a single Identity bone at 100% vertex weight (`SetBoneTransforms(new[]
{Matrix.Identity})`, hardcoded, not yet scene-configurable) — the same simplification `D9-82f`'s
own CTest used: skinning is a mathematical no-op, so the expected math reduces to
`lit_textured_quad.scene`'s own already-established formula, while still genuinely exercising the
real per-vertex `BLENDWEIGHT0`/`BLENDINDICES0` upload end to end. Exact `(128,128,128,255)` on
both sides, `0/65536` differ. Same `LightingEnabled` explicit-interface-implementation carve-out
found for `SkinnedEffect` too (confirmed against FNA's own source) — not a new bug, a confirmation
the same real-XNA quirk applies to both of this project's `IEffectLights`-but-always-on effects.

**Also fixed proactively**: added `[StructLayout(LayoutKind.Sequential)]` to both
`VertexPositionDualTexture` (retroactively) and the new `VertexPositionNormalTextureWeights` on
the C# side — C#'s default "auto" struct layout does not formally guarantee field-declaration
order is preserved in memory, which `DrawUserPrimitives<T>`'s raw-byte marshalling against an
explicit-offset `VertexDeclaration` silently depends on. `VertexPositionDualTexture` had been
relying on this working out in practice (all-`Vector2`/`Vector3` fields); the newly-mixed
float+byte struct was a genuinely higher-risk case to leave unpinned. All 7 scenes re-verified
pixel-perfect afterward, not just the new one.

**8th scene, `multilight_textured_quad` (2026-07-15) — also pixel-perfect, first scene to
genuinely exercise `BasicEffect`'s multi-light SUMMATION formula.** `D9-82b`'s own "2-light-sum"
`ShaderIndex` bucket is a structurally different dispatch path from the "`OneLight`" bucket every
earlier lit scene exercises — two active lights (`DirectionalLight0` diffuse `0.3`,
`DirectionalLight1` diffuse `0.2`, same direction) sum to the exact same total dimming
`lit_textured_quad.scene`'s own single `0.5` light already produces, matching that scene's own
`(128,128,128,255)` byte-for-byte — proving the two lights are genuinely summed, not one silently
overwriting the other's constant register. `DirectionalLight2` is present but explicitly disabled
with a large nonzero diffuse (`0.9`) that must NOT contribute — confirmed it doesn't. Extended
`light1*`/`light2*` scene keys, applied uniformly to all three lit effects
(`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`). All 8 scenes re-verified pixel-perfect
afterward.

**9th scene, `fog_gradient_quad` (2026-07-15) — also pixel-perfect, first scene to exercise
`IEffectFog` (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`), shared by all 5 Stock Effects.** Fog
wiring added to all five effect-construction branches on both sides (`atfx`/`dtfx`/`emfx`/`skfx`/
`bfx` in `Oracle.cs`; `alphaFx`/`dualFx`/`envMapFx`/`skinnedFx`/`basicFx` in
`CnaOracleRender.cpp`), even though this scene itself only exercises `BasicEffect` — same
"wire to every effect that has it, exercise from one scene" discipline scene 8 already used for
`light1*`/`light2*`.

**Required two false starts before a genuinely correct, non-trivial gradient rendered identically
on both sides — a real finding about FogStart/FogEnd sign conventions, confirmed against FNA's own
`EffectHelpers.SetFogVector` and `Common.fxh`'s `ComputeFogFactor` (`saturate(dot(position,
FogVector))`).** With `World=View=Identity`: `fogVector.Z = worldView.M33*scale`,
`fogVector.W = fogStart*scale`, `scale = 1/(fogStart-fogEnd)`, so
`fogFactor = saturate(z*scale + fogStart*scale)`.
- **1st draft**: vertex `z=0`(near)→`z=1`(far), `FogStart=0`/`FogEnd=1` (the "obvious" reading) →
  `scale=-1` → `fogFactor=saturate(-z)`, which is `<=0` for all `z>=0` — every pixel clamps to 0%
  fog. Rendered **uniformly white** on **both** real XNA and CNA: an exact `0/65536` match that
  proved nothing, since fog was never actually applied on either side. Caught only by sampling
  interior pixels and noticing no gradient existed — the diff tool itself cannot detect "both
  sides agree but the feature isn't exercised."
- **2nd draft**: flipped far vertices to `z=-1` for a genuinely negative view-space Z (XNA's
  "camera looks down -Z" convention). Instead pushed the primitive outside D3D's valid
  post-projection depth range `[0,w]` (`Projection` is also `Identity` here, so clip-space z **is**
  the vertex z) — near-plane-clipped away entirely on **both** sides, rendering only clear color.
  Also an exact match, also proving nothing.
- **Working fix**: keep vertex z in the safe `[0,1]` range and solve for the `FogStart`/`FogEnd`
  pair giving `fogFactor=0` at `z=0`, `fogFactor=1` at `z=1`: `FogStart=0`, `FogEnd=-1`
  (**negative**) → `scale=1` → `fogFactor=z` directly. Produced a genuine monotonic white→grey→
  black gradient (`(249,249,249)` near → `(127,127,127)` center → `(8,8,8)` far, sampled on the
  real-XNA side) — confirmed **pixel-for-pixel identical** on CNA, `0/65536` differ. First evidence
  this backend's fog dispatch is genuinely indistinguishable from real XNA 4.0, with an actual
  varying gradient proving per-pixel computation rather than a saturated constant.

All 9 scenes re-verified pixel-perfect afterward (not just the new one); full `D3D9` CTest suite
re-run, 11/11 still green.

**10th scene, `alphatest_less_quad` (2026-07-15) — also pixel-perfect, first scene to exercise a
SECOND `AlphaTestEffect.AlphaFunction` value (`Less`), not just the single `Greater` value
`alphatest_quad.scene` covers.** Reuses the same 2×2 texture and `ReferenceAlpha=128` threshold,
only `AlphaFunction` changes — deliberately **flips** which texels pass vs. get discarded relative
to the `Greater` scene, proving the compare function itself is genuinely honored (a backend that
silently ignored `AlphaFunction` would still pass `alphatest_quad.scene` but fail this one). No
code changes needed on either side — `Less` was already a supported `CompareFunction` value in
both parsers.

**Real finding — a PNG-encoder quirk in the oracle tooling itself, not a rendering bug.** A first
draft used a texel with `alpha=0` for the passing top-right texel. The actual shader OUTPUT was
byte-identical on both sides (`RGBA=(255,255,255,0)`), yet the SAVED PNG differed: real XNA's
`Texture2D.SaveAsPng` wrote `RGB=(0,0,0)` for that exact-`alpha=0` pixel, while CNA's own PNG
writer preserved the raw `RGB=(255,255,255)` — confirmed specific to `alpha==0` (not a general
premultiply-before-encode behavior) because the adjacent `alpha=64` texel matched byte-for-byte on
both sides in the same run. Fixed by changing that texel's alpha from `0` to `1` (still exercises
the identical `Less` code path, sidesteps the encoder's fully-transparent-pixel edge case) —
re-verified `0/65536` differ. All 10 scenes re-verified pixel-perfect afterward; full `D3D9` CTest
suite re-run, 11/11 still green.

**11th scene, `alphatest_equal_quad` (2026-07-15) — also pixel-perfect, first scene to exercise
`AlphaFunction=Equal`, a STRUCTURALLY different pixel shader bucket from `Greater`/`Less`.**
Confirmed against FNA's own `AlphaTestEffect.cs` source: `Less`/`LessEqual`/`GreaterEqual`/
`Greater`/`Never`/`Always` all compile to the shared `PSAlphaTestLtGt` shader
(`clip((a < x) ? z : w)`), while `Equal`/`NotEqual` compile to the entirely separate
`PSAlphaTestEqNe` shader (`clip((abs(a - x) < y) ? z : w)`) — genuinely different comparison
logic. FNA's source also gives the exact tolerance: `threshold = 0.5f / 255f` (half of one 8-bit
integer step). The scene straddles that boundary with 4 texels: `alpha=128` (exact match to
`ReferenceAlpha=128`, PASSES), `alpha=127`/`alpha=129` (off by `1/255`, both FAIL), `alpha=1` (far
off, FAILS) — the pass/fail pattern was predicted before running either side, then confirmed
pixel-for-pixel identical, `0/65536` differ. No code changes needed (`Equal` was already
supported). All 11 scenes re-verified pixel-perfect afterward; full `D3D9` CTest suite re-run,
11/11 still green.

**12th scene, `envmap_fresnel_quad` (2026-07-15) — also pixel-perfect, first scene to genuinely
exercise `EnvironmentMapEffect.FresnelFactor` with a real per-vertex gradient. Also fixed a real
documentation-accuracy gap in `envmap_quad.scene` itself (not a rendering bug).** New
`fresnelfactor` scene key wired on both sides. **Real finding**: `envmap_quad.scene`'s own comment
claimed to test the "non-fresnel bucket", but neither side had ever actually set `FresnelFactor`
for it, and real XNA's `EnvironmentMapEffect` constructor defaults `FresnelFactor=1` (confirmed in
FNA's source, matched by CNA's own constructor) — meaning that scene had ACTUALLY been running the
fresnel-ENABLED bucket the whole time. Undetected because the geometry is coincidentally
degenerate for Fresnel: the quad sits in the same `z=0` plane as `EyePosition=(0,0,0)` (`View` is
always `Identity`), so `viewAngle=dot(eyeVector,normal)=0` at every vertex with `normal=(0,0,1)`,
and `pow(max(1-abs(0),0), anything)=1` regardless of the Fresnel exponent — enabled and disabled
Fresnel produce the IDENTICAL result for that geometry. Fixed with an explicit `fresnelfactor=0`;
re-verified `0/65536` differ, unchanged.

The new scene proves the real formula (`pow(max(1-abs(dot(eyeVector,worldNormal)),0),
FresnelFactor) * EnvironmentMapAmount`, computed per-vertex then Gouraud-interpolated). A second
trap surfaced designing it: any single normal shared by all 4 corners of this symmetric
origin-centered quad gives an IDENTICAL fresnelFactor everywhere (no gradient) — fixed by
deliberately assigning DIFFERENT per-vertex normals to the top vs. bottom edge (`(0,0,1)` top →
`fresnelFactor=1` exactly; `(1,0,0)` bottom → hand-derived `fresnelFactor≈0.29289`). With lighting
forced to `diffuseSum=0`, result reduces to exactly `fresnelFactor * environmentMapColor` —
sampled at the exact vertical center, predicted `≈(129.3,64.6,32.3)`, observed exactly
`(129,65,32)` on both real XNA and CNA. All 12 scenes re-verified pixel-perfect afterward; full
`D3D9` CTest suite re-run, 11/11 still green.

**13th scene, `skinned_twobone_quad` (2026-07-15) — also pixel-perfect, first scene to exercise a
REAL, non-degenerate 2-bone skinning blend. Also fixed the SAME category of documentation-accuracy
gap the Fresnel scene found, this time in `skinned_quad.scene` itself.** New
`weightspervertex`/`bone1translate` scene keys; the vertex line format extended from 10 to an
optional 12 columns (a second `boneindex,boneweight` pair), backward compatible with existing
10-column lines. **Real finding**: `skinned_quad.scene`'s own comment claimed `WeightsPerVertex=1`,
but that property was never actually set, and real XNA's `SkinnedEffect` defaults
`WeightsPerVertex=4` (confirmed in FNA's source, matched by CNA) — so that scene had ACTUALLY been
running the `FourBones` bucket the whole time, harmless only because its single-pair vertex data
leaves weights `[1..3]=0`. Fixed with an explicit `weightspervertex=1`, now genuinely exercising
the `OneBone` bucket; re-verified `0/65536` differ, unchanged.

The new scene's formula (confirmed against FNA's own `SkinnedEffect.fx`): `skinning = Σ
Bones[Indices[i]] * Weights[i]`, a literal weighted sum of raw bone matrices. Bone 0 = Identity,
Bone 1 = `Translate(0.4,0,0)`, weights `0.5/0.5` — since both bones share the same Identity
rotation/scale part, the blend is exactly `Translate(0.2,0,0)`, a pure rightward shift of the
whole quad by `0.2` NDC units, normal (and lighting) unaffected. Sampled at the predicted shifted
boundaries: the original left edge correctly shows clear color, the lit `(128,128,128,255)` color
begins exactly at the shifted position, and clear color resumes exactly past the shifted right
edge — confirmed identical on both sides. All 13 scenes re-verified pixel-perfect afterward; full
`D3D9` CTest suite re-run, 11/11 still green.

**14th scene, `alphatest_notequal_quad` (2026-07-15) — also pixel-perfect, first scene to exercise
`AlphaFunction=NotEqual`, the negation of `alphatest_equal_quad.scene` within the SAME
`PSAlphaTestEqNe` shader bucket.** Confirmed against FNA's own `AlphaTestEffect.cs` source:
`NotEqual` uses the identical `abs(a - x) < y` comparison as `Equal`, only the pass/fail branch
targets are swapped. Reuses `alphatest_equal_quad.scene`'s exact texture/threshold, only
`AlphaFunction` changes — deliberately flips every texel's pass/fail (the exact-match `alpha=128`
texel now FAILS; the three near/far-miss texels now PASS), confirmed pixel-for-pixel identical.
No code changes needed (`NotEqual` already supported). This completes coverage of both
compare-function directions on both real pixel shader buckets (`Greater`/`Less` on
`PSAlphaTestLtGt`, `Equal`/`NotEqual` on `PSAlphaTestEqNe`). All 14 scenes re-verified
pixel-perfect afterward; full `D3D9` CTest suite re-run, 11/11 still green.

**15th/16th scenes, `alphatest_greaterequal_quad`/`alphatest_lessequal_quad` (2026-07-15) — also
pixel-perfect, exercise `GreaterEqual`/`LessEqual`, which share the `PSAlphaTestLtGt` bucket with
`Greater`/`Less` but differ from them specifically at the EXACT boundary value.** Confirmed
against FNA's own `AlphaTestEffect.cs`: `GreaterEqual` sets `alphaTest.X = reference - threshold`
(vs. `Greater`'s `reference + threshold`); `LessEqual` sets `reference + threshold` (vs. `Less`'s
`reference - threshold`) — a texel whose alpha exactly equals `ReferenceAlpha` PASSES under the
`-Equal` variant but would be DISCARDED under the plain variant. Both scenes reuse
`alphatest_equal_quad.scene`'s own texture (`alpha=128,127,129,1`) specifically because it already
has a texel at the exact `128` boundary — `alphatest_quad.scene`'s own texture never lands exactly
on `128`, so it could not distinguish these pairs at all. Both scenes' pass/fail patterns were
predicted before running either side, then confirmed pixel-for-pixel identical, `0/65536` differ
each. No code changes needed. Together these complete coverage of all 4 alpha-value-dependent
`PSAlphaTestLtGt` values; only alpha-value-independent `Never`/`Always` remain unrepresented in
that bucket. All 16 scenes re-verified pixel-perfect afterward; full `D3D9` CTest suite re-run,
11/11 still green.

**17th/18th scenes, `alphatest_never_quad`/`alphatest_always_quad` (2026-07-15) — also
pixel-perfect, COMPLETE ALL 8 REAL XNA `AlphaTestEffect.AlphaFunction` VALUES IN THE CORPUS.**
Confirmed against FNA's own `AlphaTestEffect.cs`: `Never` sets both branch targets negative —
`clip((a < x) ? z : w)` evaluates to `clip(-1)` unconditionally, discarding every fragment
regardless of alpha; `Always` sets both targets positive, `clip(1)` unconditionally, never
discarding anything. Both scenes reuse `alphatest_quad.scene`'s exact texture
(`alpha=255,1,255,64`) unchanged — under `Never`, all 4 texels (including the `alpha=255` ones
that would normally pass `Greater`) are discarded, rendering pure clear color everywhere; under
`Always`, all 4 texels (including the `alpha=1`/`alpha=64` ones that would normally fail
`Greater`) survive and show their own raw color. Confirmed pixel-for-pixel identical, `0/65536`
differ each. No code changes needed. **This closes out `AlphaTestEffect`'s entire
compare-function surface**: `Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/`Always` on
`PSAlphaTestLtGt`, `Equal`/`NotEqual` on `PSAlphaTestEqNe` — all 8 real XNA `AlphaFunction`
values now independently verified against the real reference implementation. All 18 scenes
re-verified pixel-perfect afterward; full `D3D9` CTest suite re-run, 11/11 still green.

**19th scene, `skinned_fourbone_quad` (2026-07-15) — also pixel-perfect, first scene to exercise
a REAL, non-degenerate 4-bone skinning blend, completing coverage of all 3 real
`WeightsPerVertex` values.** Extended the vertex line format to an optional 16 columns (a 3rd/4th
`boneindex,boneweight` pair), backward compatible. New `bone2translate`/`bone3translate` scene
keys. All four bones are pure translations (same Identity rotation/scale trick
`skinned_twobone_quad.scene` used): `Bone 0=Identity` (weight `0.4`), `Bone 1=Translate(0.4,0,0)`
(weight `0.3`), `Bone 2=Translate(0,0.2,0)` (weight `0.2`), `Bone 3=Translate(0,-0.1,0)` (weight
`0.1`). Hand-derived blend: `0.4*(0,0,0)+0.3*(0.4,0,0)+0.2*(0,0.2,0)+0.1*(0,-0.1,0) =
(0.12,0.03,0)` exactly — a genuine TWO-AXIS shift (unlike the 2-bone scene's pure-X shift),
proving all four weighted terms are summed correctly. Sampled at the predicted shifted
boundaries in both X and Y, confirmed identical on both real XNA and CNA. All 19 scenes
re-verified pixel-perfect afterward; full `D3D9` CTest suite re-run, 11/11 still green.

**Update 2026-07-15: `SpriteBatch` is no longer an open candidate here** — Phase D9-9
(`D9-90`/`D9-91`/`D9-92`/`D9-93`) is now fully CLOSED (7 new scenes total:
`sprite_basic_quad`/`sprite_rotated_quad`/`sprite_flipped_quad`/`sprite_wrap_quad`/
`sprite_mirror_quad`/`sprite_sortmode_deferred_quad`/`sprite_sortmode_backtofront_quad`/
`sprite_sortmode_fronttoback_quad`, all pixel-perfect), see Phase D9-9's own section below for
the full record — including a real D3D9 backend bug (`BuildMatrixTransformEXT`'s Z-row clipping
away any nonzero `layerDepth` sprite) found and fixed via `D9-93`. Render targets are now a
documented BLOCKER (see §4's own "New blocker found 2026-07-15"), not a simple next candidate —
do not re-attempt until root-caused. A genuine `SurfaceFormat` sweep needs new CNA `Texture2D`
API surface (a generic `SetData<T>` matching real XNA's own, since the current C++ API is
`Color`-only) before non-`Color` formats can even be exercised through the oracle — also not a
simple "add a scene" task.

### Phase D9-1 — CMake integration and skeleton: CLOSED 2026-07-14

| Task | Status |
|---|---|
| `D9-10` — `D3D9` added to all 7 `CMakeLists.txt` `"D3D12"` sites, minus one real correction | ✅ |
| `D9-11` — `D3D9GraphicsBackend` skeleton (22 pure virtuals + 10 silently-empty ones handled) | ✅ |
| `D9-12` — `GraphicsDevice.cpp` `#ifdef` audit | ✅ (zero changes needed) |

**Phase D9-1 is fully closed.** `D9-10` found one real, worth-fixing gap in this plan's own text: it
described CMake line 288 as "a second Windows-only-related OR chain" needing a D3D9 sibling, but that
line is actually the `D3DCommon` shared-core conditional — adding D3D9 there would have violated
design decision 12 ("`D3DCommon` is not expanded"). Left untouched, with an explanatory comment;
`plan_dx9.md`'s own `D9-10` row now records the correction. Line 392 (the `CNA` circular-link `OR`
chain) was also deliberately left out of D3D9's `OR` chain — nothing calls back into a CNA-defined
symbol yet (that's `D9-112`, Phase D9-11, ask-first).

### Phase D9-2 — mapping layer: CLOSED 2026-07-14 (one row 🟨)

| Task | Status |
|---|---|
| `D9-20` — `D3D9FormatMapping` (`SurfaceFormat`/`DepthFormat` → `D3DFORMAT`) | ✅ |
| `D9-21` — `D3D9StateMapping` (7 state enums → D3D9 equivalents) | 🟨 (table done; `D3DCULL` pixel-proof against the oracle owed to `D9-84`) |
| `D9-22` — `D3D9VertexDeclarations` (stride-keyed `D3DVERTEXELEMENT9` arrays) | ✅ (COLOR0 element type corrected `D9-82`, see that row) |
| `D9-23` — `D3D9_Common` CTest, mutation-verified | ✅ (28/28 checks) |

**Phase D9-2 is closed** (one honestly-flagged partial, not a blocker). Two non-obvious findings
worth knowing before touching this code: **`SurfaceFormat::Color` → `D3DFMT_A8B8G8R8`, NOT
`D3DFMT_A8R8G8B8`** (D3D9's channel-order naming reads MSB→LSB, opposite DXGI's convention — get this
backwards and every Color-format texture samples with R/B swapped); and **`Rgba1010102` →
`D3DFMT_A2B10G10R10`, NOT the superficially-similar `D3DFMT_A2R10G10B10`** (that one has no DXGI
equivalent at all — different alpha-bit position). Both verified against Microsoft's own published
D3D9→DXGI legacy-format table, not derived by name resemblance. Next up: Phase D9-3 (device, present,
device-lost).

### Phase D9-3 — device, present, device-lost: ALL 5 rows closed (D9-32/D9-34 honestly 🟨)

| Task | Status |
|---|---|
| `D9-30` — real `Direct3DCreate9`/`GetDeviceCaps`/`CreateDevice` with real presentation parameters | ✅ |
| `D9-31` — `Clear` + all 6 `Clear*` combos + `Present` + `ReadBackbuffer`, each pixel-verified | ✅ (`D3D9_Smoke`) |
| `D9-32` — enforce `GraphicsProfile` floor at construction | 🟨 (shader-model floor real; full Reach/HiDef table is `D9-100`'s job) |
| `D9-33` — window resize via device `Reset()` | ✅ (mechanism + dedicated 64×64→96×80 test, Check L) |
| `D9-34` — XNA device-lost lifecycle | 🟨 (real mechanism + real event order proven via `DebugSimulateContextLoss`; genuine driver-triggered loss + event-payload-vs-real-XNA fidelity are `D9-A`/`D9-140`'s own jobs) |

**Phase D9-3 is now fully closed** (both 🟨 rows have named, honest, out-of-this-plan's-current-reach
gaps, not missed work). `D9-34`: `Present()` detects real `D3DERR_DEVICELOST`, fires `DeviceLost`;
`PollDeviceLost()` polls `TestCooperativeLevel()` until `D3DERR_DEVICENOTRESET`, then
`PerformResetRecovery()` fires `DeviceResetting`, calls a real `Reset()`, restores the viewport, fires
`DeviceReset`. Since DXVK will rarely lose the device naturally, the full sequence was exercised
deterministically via the pre-existing `DebugSimulateContextLoss()`/`DebugRestoreContext()` test
channel (`D3D9_Smoke` Check M, 8 new checks) — real event counts/order, a real `Clear()` throwing the
real XNA `DeviceLostException` while lost, a real `Reset()` call during recovery, and the device
genuinely rendering again afterward. Also fixed a separate, pre-existing gap found along the way:
`GraphicsDevice::getGraphicsDeviceStatusProperty()` was hardcoded `return
GraphicsDeviceStatus::Normal;` always — now tracks the real backend-reported state.

**Two real, unplanned findings surfaced while closing D9-30/D9-31, both fixed in place:**

1. **D3D9 rejects `SurfaceFormat::Color`'s own `D9-20` back-buffer format.** DXVK's D3D9
   implementation (correctly matching real D3D9 behavior) refused `D3DFMT_A8B8G8R8` as a *swap-chain*
   format — that format is legal for textures but D3D9 restricts the primary back buffer to a small
   set of display-compatible formats. Fixed with a back-buffer-specific substitution to `A8R8G8B8`
   (`ReadBackbuffer()` already handles both byte orders). Not a DXVK quirk — a real, confirmed D3D9
   API restriction, documented in `D3D9GraphicsBackend.cpp`.
2. **`GraphicsDevice::Reset()` never told an already-constructed backend about updated back-buffer/
   depth-stencil/fullscreen settings** — only virtual resolution and MSAA were re-pushed. This matters
   because `Game` typically constructs its `GraphicsDevice` (and backend) with *default*
   `PresentationParameters`, before `GraphicsDeviceManager.ApplyChanges()` ever applies the game's real
   preferences. Fixed with one more small additive `IGraphicsBackend` method,
   `UpdatePresentationFormatEXT()` (empty default; every other backend ignores it unchanged) — the
   same category of fix as the already-approved boundary-problem resolution, not a new architectural
   decision.

**A third finding forced Phase D9-6 (render states) in far earlier than planned.**
`GraphicsDevice`'s own constructor unconditionally pushes `BlendState::Opaque`/
`DepthStencilState::Default`/`RasterizerState::CullCounterClockwise` and the viewport (Task 896/955) —
meaning `ApplyBlendState`/`SetBlendFactor`/`ApplyDepthStencilState`/`SetReferenceStencil`/
`ApplyRasterizerState`/`SetViewport`/`SetScissorRect` could not stay `NotYetImplemented()` stubs for
*any* device to finish constructing, regardless of this plan's own phase ordering. All are now real
(`D3DRS_*` `SetRenderState()` sequences via the `D9-21` mapping tables — see §2's Phase D9-6 entry
below). Along the way, also found that `D9-11`'s own "10 silently-empty virtuals" count missed 4 more
(`ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState`) because their
`{}` defaults span multiple lines, invisible to a single-line `grep`; `ApplySamplerState` now throws
`NotYetImplemented()` like the original 10 (nothing forced it in early — no texture/sampler work
exists yet).

### Phase D9-6 — render states: ALL 5 rows closed (D9-60/D9-62 honestly 🟨)

| Task | Status |
|---|---|
| `D9-60` — `ApplyBlendState`/`SetBlendFactor` | 🟨 (real; `D3DRS_COLORWRITEENABLE` genuinely out of scope — see plan) |
| `D9-61` — `ApplyDepthStencilState`/`SetReferenceStencil` | ✅ |
| `D9-62` — `ApplyRasterizerState`/`SetScissorRect`/`SetViewport` | 🟨 (real; oracle pixel-proof owed to `D9-84`, same as `D9-21`'s own `D3DCULL` obligation) |
| `D9-63` — `ApplySamplerState` | ✅ |
| `D9-64` — reuse backend-agnostic state CTest sources | ✅ |

`D9-64` closed 2026-07-15: reused the same 4-test subset D3D11 established
(`easygl_blendstate_opaque_test.cpp`/`easygl_blendstate_alphablend_test.cpp`/
`easygl_depthstencilstate_stencil_enable_test.cpp`/`easygl_rasterizerstate_cullmode_test.cpp`,
verbatim, unmodified) as new `D3D9_BlendState_Opaque`/`D3D9_BlendState_AlphaBlend`/
`D3D9_DepthStencilState_StencilEnable`/`D3D9_RasterizerState_CullMode` CTests. **Found and fixed
two real, pre-existing D3D9 backend bugs along the way** (both mutation-verified, neither an
EasyGL-test workaround): `SetDepthTestEnabled`/`SetDepthWriteEnabled` were silent-throw stubs since
`D9-11`'s original skeleton, never wired up — same class of bug as D3D11's own 2026-07-14
`SetDepthTestEnabled` fix (commit `191c28f1`), now direct `SetRenderState(D3DRS_ZENABLE/
ZWRITEENABLE)` calls (`SetBlendEnabled` made a deliberate no-op, matching D3D11/D3D12); and
`UpdatePresentationFormatEXT()` deferred applying a changed `DepthStencilFormat` until the next
`Present()`, causing `Clear()` to fail with `D3DERR_INVALIDCALL` on any test that draws
depth/stencil content on the literal first frame (every pre-existing D3D9 test worked around this
with a `frame_++ < 1` skip; the reused EasyGL tests don't) — fixed by applying eagerly inside
`UpdatePresentationFormatEXT()` itself, within the interface's own documented allowance. New
`D3D9_Smoke` Check Z (2 checks, ported from D3D11's own identical near/far depth-test proof)
proves the `SetDepthTestEnabled` fix is real. Full D3D9 CTest suite: 11/11 binaries green.

Real, confirmed finding: D3D9's `D3DRS_DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` are floats, and XNA's own
float `DepthBias`/`SlopeScaleDepthBias` map through with **no unit conversion** (unlike D3D11, which
needs float→`INT` rounding) — `SetRenderState()` still takes a `DWORD` parameter, so the float bits
are reinterpreted (`std::bit_cast`), not numerically converted.

`D9-63` (`ApplySamplerState`, closed once `D9-50`'s real textures made it meaningful): plain
`SetSamplerState()` calls (design decision 11 — no D3D9 sampler state objects), using the `D9-21`
mapping tables. Slot bound-checked against the real `D3DCAPS9::MaxSimultaneousTextures`, not a
hardcoded 16. `D3DSAMP_SRGBTEXTURE` is genuinely out of scope — `IGraphicsBackend::ApplySamplerState()`'s
own signature carries no sRGB parameter at all, same category of pre-existing interface gap `D9-60`
already found for `D3DRS_COLORWRITEENABLE`. New `D3D9_Smoke` Check Y (2 checks): `SetSamplerState()`
values read back directly via `GetSamplerState()` (no draw call needed) confirm an exact match; an
out-of-range slot silently no-ops. Mutation-verified (hardcoded `D3DSAMP_ADDRESSU` to ignore the
requested value, confirmed exactly that assertion went red). `D3D9_Smoke` now 53/53.

### Phase D9-4 — buffers: D9-40/D9-41/D9-42 CLOSED

| Task | Status |
|---|---|
| `D9-40` — `D3D9VertexBufferBackend` | ✅ |
| `D9-41` — `D3D9IndexBufferBackend`, 16-bit and 32-bit, `CreateIndexBuffer32()` explicit | ✅ |
| `D9-42` — byte-exact round-trip tests | ✅ (folded into D9-40/41's own checks) |

Real architectural finding, not anticipated by this row's own plan text: `D3DUSAGE_DYNAMIC` requires
`D3DPOOL_DEFAULT` (D3D9 forbids `DYNAMIC` with `POOL_MANAGED`), so these buffers do **not** survive a
device `Reset()` the way ordinary `D3DPOOL_MANAGED` resources do. New `ID3D9DefaultPoolResourceEXT`
interface + a small registry on `D3D9GraphicsBackend` lets `D9-34`'s `PerformResetRecovery()` release
every live `D3DPOOL_DEFAULT` resource before `Reset()`; each recreates lazily on next use — real,
authentic D3D9/XNA behavior (a `DYNAMIC` buffer's content genuinely does not survive `DeviceReset` in
real XNA either). Mutation-verified: temporarily broke `CreateIndexBuffer32()` to build a 16-bit
buffer instead — caught immediately (a real, uncaught exception from the existing type-mismatch
guard), reverted, reconfirmed green. Also confirmed and fixed the exact "pointer-inequality is not
sound proof of recreation" false-negative this project's own D3D12 work already found once (see
`plan_dx9.md`'s `D9-40` row). `D3D9_Smoke` is now 30/30 checks.

### Phase D9-5 — textures/render targets/readback: FULLY CLOSED (all 7 rows)

| Task | Status |
|---|---|
| `D9-50` — `D3D9TextureBackend` (`IDirect3DTexture9`, `D3DPOOL_MANAGED`), mip levels, sub-rect `SetData` | ✅ |
| `D9-51` — `D3D9TextureCubeBackend`/`D3D9Texture3DBackend`, volume support gated on real `D3DCAPS9` | ✅ |
| `D9-52` — `GetData()` for 2D/cube/3D | ✅ (found empirically: 2D has none to implement — `Texture2D::GetData()` is CPU-shadow-based, same as D3D11; cube/3D genuinely delegate to the backend and are real `LockRect`/`LockBox` reads) |
| `D9-53` — `D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend` (`D3DUSAGE_RENDERTARGET`, `D3DPOOL_DEFAULT`, real MSAA) | ✅ |
| `D9-54` — MRT via `SetRenderTarget(i, surface)`, capped at `NumSimultaneousRTs`, over-request throws | ✅ |
| `D9-55` — `D3D9OcclusionQueryBackend` | ✅ |
| `D9-56` — NPOT handling driven by `D3DPTEXTURECAPS_POW2`/`NONPOW2CONDITIONAL` | ✅ |

New `include/`/`src/CNA/Internal/Backends/D3D9/D3D9Textures.hpp`+`.cpp`. All three texture backends use
`D3DFMT_A8B8G8R8`/`D3DPOOL_MANAGED` (RGBA8 storage only, same simplification D3D11 already documents —
`surfaceFormat` accepted for signature compatibility, not honored). Since `D3DPOOL_MANAGED` (not
`DEFAULT`), none of these register with the `D9-40` device-lost registry — they survive `Reset()`
automatically, same as `D9-4`'s own spike found. Cube-face order (0..5 = +X,-X,+Y,-Y,+Z,-Z) matches
D3D9's own native `D3DCUBEMAP_FACES` enum order, so no face-remapping table is needed. Volume-texture
creation is gated on `D3DCAPS9::MaxVolumeExtent > 0`, cube-map creation on
`D3DPTEXTURECAPS_CUBEMAP` — both report supported on this dev environment's DXVK device, so `D3D9_Smoke`
Check R exercises the real creation path for both, not just the capability-gate branch (the
unsupported/`nullptr` branch is exercised by construction but not provably reachable without
lesser-capable hardware — an honest gap, not a hidden one). Wired into `D3D9GraphicsBackend::CreateTexture()`/
`CreateTextureCube()`/`CreateTexture3D()` (previously stubs/inherited `nullptr` defaults). `D3D9_Smoke`
Checks Q/R (6 new checks) verify exact-byte round-trips via direct `LockRect`/`LockBox` on the
`D3DPOOL_MANAGED` resources themselves — no staging-texture copy needed, unlike D3D11's equivalent
check. Mutation-verified (see §3). `D3D9_Smoke` now 36/36.

New `include/`/`src/CNA/Internal/Backends/D3D9/D3D9RenderTargets.hpp`+`.cpp` (`D9-53`).
`D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend`, both `D3DPOOL_DEFAULT` and registered with the
`D9-40` device-lost registry (unlike the plain `D9-50` textures) — released before `Reset()`, lazily
recreated on the next `BindAsRenderTarget()`/`BindAsRenderTargetFace()` call. Real MSAA, clamped via
`IDirect3D9::CheckDeviceMultiSampleType()` (all-or-nothing, no step-down ladder — matches D3D11's own
precedent); an MSAA target resolves into its sampleable texture via `StretchRect` on unbind. Cube
render targets don't support MSAA (matches D3D11's own precedent). Mip auto-generation is NOT
implemented (named gap). Three real, unplanned findings, all fixed: (1) the resize path
(`EnsureDeviceSize()`) never released `D3DPOOL_DEFAULT` resources before `Reset()` — only the
device-lost path did; a real D3D9 requirement, invisible until this task actually created one during
a resize-adjacent test; (2) a cached depth-stencil-surface `ComPtr` is itself an app-held reference to
a losable resource, and must be released before every `Reset()` too (caught immediately by DXVK's own
"still has alive losable resources" diagnostic); (3) `IGraphicsBackend::SetRenderTargetCubeFace()`'s
inherited default never actually unbinds a cube target for real (it only knows the 2D-only
`currentCustomRT_` tracking) — fixed with an explicit `D3D9GraphicsBackend::SetRenderTargetCubeFace()`
override and a second `currentCustomCubeRT_` field. `D3D9_Smoke` Checks S/T/U (6 new checks): 2D
target, cube target, and MSAA target, each create/bind/Clear/readback (via `GetRenderTargetData()`,
since a render-target surface is not directly `Lockable`)/unbind-restores-back-buffer. Mutation-verified
(dropped the MSAA resolve `StretchRect` call — exactly Check U's resolve assertion went red, nothing
else). `D3D9_Smoke` now 43/43.

`D9-54` (MRT): real `D3D9GraphicsBackend::SetRenderTargets(rts, count)` (`SetRenderTarget(i, surface)`
for `i=0..count-1`, unused slots up to `NumSimultaneousRTs` explicitly disabled). Over-request throws
`std::runtime_error` naming both counts (design decision 13) — deliberately **not** matching
D3D11/D3D12's own silent-clamp precedent, the exact invisible-capability trap this authenticity-focused
backend does not accept. "Same bit depth"/"no independent blending" are trivially satisfied by this
project's existing simplifications (every target is `D3DFMT_A8B8G8R8`; blend state is one global
`SetRenderState()` sequence) — noted, not actively coded. Real, unplanned finding: an MRT bind is not
representable by the existing single-pointer `currentCustomRT_`/`currentCustomCubeRT_` tracking (same
gap D3D11's own `SetRenderTargets()` notes), so unbinding via `SetRenderTargets(nullptr, 0)` →
`SetRenderTarget2D(nullptr)` was silently relying on `UnbindAsRenderTarget()` to restore the back
buffer — which never fires when nothing was tracked. Fixed by making
`RestoreBackBufferRenderTargetEXT()` unconditional in the `!rt` branches of `SetRenderTarget2D()`/
`SetRenderTargetCubeFace()` (idempotent in the ordinary case, the real fix for MRT). New `D3D9_Smoke`
Check V (3 checks): a 2-target MRT bind + single `Clear()` writes the exact color into both targets'
own surfaces, unbind restores the back buffer, and over-request throws. Mutation-verified (disabled the
over-request guard, exactly that assertion went red). `D3D9_Smoke` now 46/46.

`D9-55` (occlusion queries): new `D3D9OcclusionQueryBackend` (`IDirect3DQuery9`,
`D3DQUERYTYPE_OCCLUSION`) — `Begin()`/`End()` → `Issue(D3DISSUE_BEGIN)`/`Issue(D3DISSUE_END)`;
`IsComplete()`/`PixelCount()` → `GetData()` (mirrors `D3D11OcclusionQueryBackend`'s shape). Gated on
the official D3D9 support-probe idiom (`CreateQuery(type, nullptr)`), not assumed. New `D3D9_Smoke`
Check W (3 checks): real query created, polled to complete within a bounded 30-iteration loop
(matches `D9-33`'s own resize-convergence convention), `PixelCount()` reads back `0` for a query
wrapping only a `Clear()` (no draw path exists yet, `D9-82` — a real, honest result, not a stand-in
for tested geometry). Mutation-verified (forced `IsComplete()` to always return `false`, confirmed
exactly that one assertion went red — the `PixelCount()==0` assertion correctly stayed green too,
since `GetData()` "not ready" and "genuinely 0 samples" both honestly return 0, not a masking bug).
`D3D9_Smoke` now 49/49.

`D9-56` (NPOT capability, **closes Phase D9-5 entirely — all 7 rows now done**): new NOXNA
`D3D9GraphicsBackend::RequiresPowerOfTwoTexturesEXT()`/`NonPowerOfTwoRequiresClampAddressingEXT()`
surface the real `D3DCAPS9::TextureCaps` `POW2`/`NONPOW2CONDITIONAL` bits rather than assuming a
value — the exact cap XNA's own `Reach` profile "no Wrap addressing on NPOT" restriction models.
This dev environment's DXVK device reports full, unconditional NPOT support (both helpers `false`),
matching `D9-3`'s own original caps dump. New `D3D9_Smoke` Check X (2 checks): asserts the exact
reported capability, then creates and round-trips a genuinely non-power-of-two (5×3)
`D3D9TextureBackend` for real, proving no artificial POW2 restriction exists on top of more-permissive
real hardware. Enforcing the `Reach`-profile restriction itself against a real `SamplerState`/draw
call is deferred to `D9-10`/`D9-82` (no draw/sampler path exists yet) — an honest gap, not hidden.
Mutation-verified (hardcoded `RequiresPowerOfTwoTexturesEXT()` to always return `true`, confirmed
exactly the capability assertion went red and the NPOT round-trip proof was consistently skipped).
`D3D9_Smoke` now 51/51.

### Phase D9-7 — Microsoft's stock effects: vendor, compile, embed: FULLY CLOSED (D9-73 honestly 🟨)

| Task | Status |
|---|---|
| `D9-70` — vendor the 10 Stock Effects HLSL sources verbatim | ✅ |
| `D9-71` — offline-compile all 66 entry points to `d3d9_shaders.hpp` | ✅ |
| `D9-72` — transcribe register annotations into `D3D9ShaderRegisters.hpp` | ✅ |
| `D9-73` — cross-check against Microsoft's shipped `.fxb` bytecode | 🟨 (already run in Phase D9-0: 61/66 exact; re-confirmed against the real checked-in header too; 5 `PixelLighting` variants owed to `D9-84`'s oracle proof) |
| `D9-74` — `D3D9ShaderCache` creates all 66 through a live device | ✅ |

`D9-70`: all 10 files (`BasicEffect.fx`, `AlphaTestEffect.fx`, `DualTextureEffect.fx`,
`EnvironmentMapEffect.fx`, `SkinnedEffect.fx`, `SpriteEffect.fx`, `Macros.fxh`, `Common.fxh`,
`Lighting.fxh`, `Structures.fxh`) copied byte-for-byte from the FNA tree into
`src/CNA/Internal/Backends/D3D9/shaders/xna/`, with `LICENSE` (Ms-PL), a provenance `README.md`
(66 entry points, each verified via `grep`, not hand-typed — an initial draft had 4 wrong names for
`EnvironmentMapEffect.fx`/`SkinnedEffect.fx`, caught by actually running the grep before publishing
it), and a specific `THIRD_PARTY_NOTICES.md` entry. New `scripts/verify-d3d9-stock-effects-vendored.sh`
mechanically diffs the vendored copies against the FNA tree; mutation-verified (appended a line to
the vendored `BasicEffect.fx`, confirmed the script reports `MISMATCH`/exit 1, reverted). Not a
CTest — depends on the FNA reference tree being present on the machine, same reasoning `D9-71`'s own
row gives for its own "run by hand" pipeline.

`D9-71`: new `src/CNA/Internal/Backends/D3D9/shaders/compile_shaders_sm2.py` — parses all 66 entry
points from the vendored `.fx` files' own `compile [vp]s_2_0 ...` statements via regex (not
hand-maintained), cross-builds `fxc_tool.cpp` (moved here unchanged from `dx9-spike/`, along with
`compare_against_fxb.py`) with MinGW-w64, invokes it via a bare `wine` call against
`~/.wine-cna-d3d9-spike` (not `run-wine-dxvk9.sh` — compiling never opens a device). **Real run:
66/66 compiled, 0 failures.** Output `d3d9_shaders.hpp` (381 KB, `k<EffectName>_<EntryPointName>`
array names) confirmed to compile clean as real C++; a second run produced a byte-identical header
(deterministic). Bonus verification: re-ran `compare_against_fxb.py` against the real checked-in
header's own bytecode — 61/66 exact matches, the identical 5 `PixelLighting` variants the Phase
D9-0 spike already found, confirming the real pipeline reproduces the spike's result exactly.
`dx9-spike/README.md` updated to reflect the move (only `xna-oracle/Oracle.cs` remains there).

`D9-72`: **a real, empirical finding changed this row's own original approach mid-task.** The plan
assumed a per-effect register table hand-derived from the `.fx` files' own `_vs(cN)`/`_ps(cN)`
annotations, with register COUNT inferred from each constant's declared HLSL type
(`float4x4`→4 registers, `float3x3`→3, etc.). **That assumption is provably wrong**: compiling
`EnvironmentMapEffect.fx`'s `VSEnvMap` and disassembling the real output (`D3DDisassemble()`) shows
`World` (declared `float4x4`) is allocated only **3** registers (`c16`-`c18`) by the real compiler
for this specific entry point — its `mul(vin.Position, World)` never reads `pos_ws.w`, so the
compiler drops the register that would compute it — while `WorldInverseTranspose` genuinely
occupies `c19`-`c21`, an apparent overlap with a naive 4-register `World` assumption that isn't
actually a conflict. **Register occupancy depends on what a given ENTRY POINT reads, not just a
constant's declared type.** Redesigned scope: new `extract_shader_registers.py` compiles **and
disassembles** each of the 66 shaders, parsing the compiler's own authoritative `// Registers:`
comment block directly (new `disasm_tool.cpp`, a small `D3DDisassemble()`-calling companion to
`fxc_tool.cpp`). Output: `D3D9ShaderRegisters.hpp` (627 lines, one array per shader:
`{name, space, registerIndex, registerCount}`). Compiles clean (`-Wall -Wextra -fsyntax-only`, zero
warnings); spot-checked against 3 independently-verified cases (`BasicEffect` `VSBasic`,
`EnvironmentMapEffect` `VSEnvMap`'s `World`/`WorldInverseTranspose` split, `SkinnedEffect`'s 72-bone
array at `c26` size 216 = 72×3 registers). No fixed-layout POD struct exists to `static_assert`
against (this row's own original wording) since occupancy varies per entry point — the generated
tables themselves are the verified ground truth. `D3DConstantBuffers.hpp` was checked and NOT
reused — different register scheme entirely (D3D11's own cbuffer reimplementation vs. D3D9's flat
register file), exactly as this row's own note anticipated.

`D9-74` (**Phase D9-7 now fully closed** — `D9-73` stays honestly 🟨, its own deferred obligation
unaffected): took option (a) from this row's own recommendation — `dxvk-setup install` run against
`~/.wine-cna-d3d9-spike` (same command `plan_dx.md`'s `DX-2` used for `~/.wine-cna-d3d11`), verified
for real (`d3d9.dll` now a DXVK symlink; `d3dcompiler_47.dll` untouched — confirmed by re-running the
full `D3D9_Smoke` suite against this prefix, 53/53 pass). New `D3D9ShaderCache` (`CreateVertexShader`/
`CreatePixelShader` per named entry point, e.g. `"BasicEffect_VSBasic"`, lazy-create-and-cache),
backed by a new `Shaders::kAllShaders[]` manifest (66 entries) appended to `compile_shaders_sm2.py`'s
own output — regenerated, not hand-typed. New `D3D9_ShaderCache` CTest (4 checks): all 66 shaders
(42 vertex + 24 pixel) create through a live device; a second lookup returns the identical cached
object; an unknown name throws; the lookup is stage-aware (a real VS name via `GetPixelShader()`
throws too, and vice versa). Runs clean against both the default CTest prefix and the newly-DXVK
-equipped compiler prefix. Mutation-verified (made `CreateAllEXT()` skip the first pixel shader,
confirmed exactly the count-dependent checks went red). Full 3-CTest D3D9 suite passes.

### Phase D9-8 — XNA shader dispatch: D9-80–D9-83 ALL CLOSED (all 5 Stock Effects real + instancing), only D9-84 open

| Task | Status |
|---|---|
| `D9-80` — replicate XNA's shader-permutation model (`VSIndices`/`PSIndices`/`ShaderIndex`) | ✅ |
| `D9-81` — audit `GpuDrawParams` vs. XNA's real `ShaderIndex` inputs, report the gaps | ✅ |
| `D9-82` — upload constants at Microsoft's registers; `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (non-effect-aware, BasicEffect-VertexColor-only scope) | ✅ |
| `D9-82b` — `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` entry point + `BasicEffect` dispatch | ✅ |
| `D9-82c` — `AlphaTestEffect` dispatch | ✅ |
| `D9-82d` — `DualTextureEffect` dispatch | ✅ |
| `D9-82e` — `EnvironmentMapEffect` dispatch | ✅ |
| `D9-82f` — `SkinnedEffect` dispatch | ✅ |
| `D9-83` — `DrawInstancedPrimitivesEx` via `SetStreamSourceFreq` | ✅ |
| `D9-84` — every draw path validated against the oracle | ⬜ |

`D9-81`: the audit's own findings were already fully written into the plan row when `plan_dx9.md`
was first authored (2026-07-14) — this closure is an independent RE-VERIFICATION against the
CURRENT source (not trusted from memory), via a forked agent that read every cited file directly.
**Result: all 4 gaps are still real, and 2 of the 4 turn out resolvable without any `GpuDrawParams`
change** — `oneLight` (`SkinnedEffect.cpp` already computes it from the real `Enabled` properties
internally) and `AlphaTestEffect`'s `isEqNe` (`alphaTest[1]` (tolerance) `> 0` is a **lossless**,
provably-exact recovery from `AlphaTestEffect.cs`'s own `alphaTest.Y = threshold` assignment, which
fires in exactly the `Equal`/`NotEqual` cases and nowhere else — not the "plausible inference, may
misfire" the plan's own original wording hedged). `PreferPerPixelLighting` and
`EnvironmentMapEffect`'s `specularEnabled` remain genuine, unresolved gaps needing a cross-cutting,
project-owner-level `GpuDrawParams` decision — reported, not fixed, per this row's own instruction.

`D9-80`: new `include/`/`src/CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.hpp`+`.cpp` — for all 5
effects, a `Compute<Effect>ShaderIndex()` ported line-for-line from that effect's own `OnApply()`
in the FNA `.cs` source, plus `Get<Effect>{Vertex,Pixel}ShaderNameEXT()` backed by the
`VSIndices`/`PSIndices`/`VSArray`/`PSArray` tables transcribed directly from the vendored `.fx`
file's own rows. Functions take the real XNA-shaped booleans as parameters, not `GpuDrawParams` —
sourcing them correctly (using `D9-81`'s own findings for `oneLight`/`isEqNe`) is `D9-82`'s job.
New `D3D9_ShaderDispatch` CTest (pure-function, no device needed), 23 checks. **Mutation-testing
found a real gap in the test's own first draft**: an initial "exhaustive sweep" only checked that
resolved names started with the right effect prefix — a deliberately-corrupted single `VSIndices`
table entry (mapped to a WRONG-but-still-real, same-prefixed name) was NOT caught by that weaker
check. Rewrote it as an exact-match sweep against a second, independently-typed expected-name array
in the test file; re-ran the same mutation, now correctly caught (exact mismatch reported); reverted,
reconfirmed 23/23 green. Full D3D9 CTest suite (4 binaries) passes.

`D9-82`: split from its own original single-row scope into `D9-82` (this narrow, non-effect-aware
"colored3d-equivalent" slice) + `D9-82b` (full effect-aware dispatch) — mirrors `plan_dx.md`'s own
`DX-61` vs. `DX-62..67` precedent exactly, same rationale (real, separate-scale work, not a
same-sitting extension). This backend's first real 3D triangle: new `D3D9ConstantUpload.hpp`+`.cpp`
(name-keyed register lookup + `Set{Vertex,Pixel}ShaderConstantF`, throws on a genuine
transcription-mismatch, silently no-ops against a variant with no named constants), real
`D3D9GraphicsBackend::DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (stride-16 only,
hardcoded to `BasicEffect` `ShaderIndex 3` = `"BasicEffect_VSBasicVcNoFog"`/`"BasicEffect_PSBasicNoFog"`,
chaining `D9-80`'s dispatch tables into `D9-74`'s shader cache), and a new stride-keyed
`IDirect3DVertexDeclaration9` cache. **A second real trap found and fixed live** (not the `D3DCULL`
one this row's own text predicted — a different one): `D9-22`'s original vertex declaration used
`D3DDECLTYPE_D3DCOLOR` for `COLOR0`, which Microsoft's own D3DDECLTYPE reference says expects
ARGB-packed memory bytes and swizzles them to RGBA — but XNA's own `Color.PackedValue` is R,G,B,A
ascending, so feeding it through `D3DDECLTYPE_D3DCOLOR` silently swaps R and B. Confirmed live before
fixing (fed opaque red, read back opaque blue), fixed by switching to `D3DDECLTYPE_UBYTE4N` (no
reorder), re-confirmed live (exact red). `D3D9_Common`'s own stride-16/24 assertions updated to
match. New `D3D9_Draw` CTest (real device draw): 3/3 (non-indexed paint, indexed paint, and a real
`WorldViewProj`-upload proof via an off-screen `World` translation). Mutation-verified: corrupted
`DiffuseColor`'s upload value, confirmed only the mutated (non-indexed) check went red while the
indexed/transform checks stayed green (correctly isolated blast radius); reverted, reconfirmed 3/3
green. Full D3D9 CTest suite (5 binaries) passes.

`D9-82b`: new `D3D9EffectDraw.cpp` — `DrawPrimitivesExImpl()` (the shared entry point, same
flag-priority-cascade shape `D3D11GraphicsBackend::DrawPrimitivesExImpl` already uses) +
`DrawBasicEffectEXT()`. New "soft" `TryUpload{Vertex,Pixel}ShaderConstantEXT()` (never throws on a
missing name) added to `D3D9ConstantUpload` — the generic dispatcher attempts EVERY constant
`BasicEffect` could ever declare and lets each variant's own real (`D9-72`) register table silently
filter out whichever don't apply.

**Real, honest scope-narrowing finding: only 10 of `BasicEffect`'s 32 `ShaderIndex` values are
actually drawable, not the 24 this row originally estimated.** `BasicEffect`'s remaining `VSInput`
shapes need vertex layouts this project's 5 established strides (16/20/24/32/52) simply don't
have (`VSInput` Position-only 12 bytes; `VSInputNm` Position+Normal 24 bytes — collides with the
EXISTING Position+Color+TexCoord 24-byte layout; `VSInputNmVc`/`VSInputNmTxVc` 28/36 bytes) — every
unsupported combination throws a named "no matching CNA vertex layout" error (same honest-gap
category as D3D11's own "`dual_texture_colored3d` not ported"), not a silent wrong-stride draw.

**`D9-81`'s `oneLight` finding corrected during real implementation** — its original text ("read
`SkinnedEffect.cpp`'s own internal `oneLight_` directly") turned out not actually reachable from
`IGraphicsBackend::DrawPrimitivesEx()`'s own `GpuDrawParams`-only input (no channel back to the
originating `Effect` object's private members). Real fix: a light with BOTH diffuse and specular
still `(0,0,0)` contributes exactly zero to `Lighting.fxh`'s `ComputeLights()` regardless of
`Enabled`, so `oneLight` is derivable losslessly from `GpuDrawParams`' own existing fields — no
`GpuDrawParams` extension needed after all (that row's own text updated to match).

Also found/derived live: the `EffectParameter.SetValue(Matrix)` register-transpose trick generalizes
correctly to a `float3x3`-declared constant (`WorldInverseTranspose`) as well as a truncated
`float4x4` (`World`, 3 of 4 registers — the same "entry point never reads `.w`" pattern `D9-72`
first found for `EnvironmentMapEffect`, now confirmed for `BasicEffect`'s lit path too); `EmissiveColor`
needed reconstruction from `GpuDrawParams`' separate `ambientColor`/`diffuseColor`/`emissiveColor`
fields (`emissiveColor + ambientColor*diffuseColor`, matching `Lighting.fxh` exactly).

New `D3D9_DrawEx` CTest (real device draw), 10/10 at the time — every expected pixel HAND-COMPUTED
from `BasicEffect.fx`/`Lighting.fxh`'s own real formulas: unlit+textured, unlit+vertexColor+textured,
lit+textured 2-light-sum (exact `(150,90,30)`), lit+textured 1-light/`OneLight` bucket (exact
`(80,48,16)` — deliberately different from the 2-light case so the pair together proves correct
bucket selection), fog fully-fogged (exact `FogColor` readback), an unsupported combo throws, and
`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` each throw their own
named not-yet-implemented (`D9-82c`/`d`/`e`/`f`). Mutation-verified: forced `oneLight` to always
`true`; exactly the 2-light check (the only one sensitive to a bucket-selection bug) went red,
everything else stayed green; reverted, reconfirmed 10/10. Full D3D9 CTest suite (6 binaries) passes.

`D9-82c`: new `D3D9GraphicsBackend::DrawAlphaTestEffectEXT()` (same file) — all 8 `ShaderIndex`
values real, no vertex-layout gap this time (`AlphaTestEffect`'s only two `VSInput` shapes map 1:1
onto the existing stride-20/24 layouts, unlike `BasicEffect`'s case). `GpuDrawParams::alphaTest` is
already exactly the real `{refVal,tolerance,passWeight,failWeight}` register layout
`AlphaTestEffect.fx`'s own `clip()` expressions expect — uploaded verbatim, no reconstruction
needed (confirmed directly against the `.fx` source). Factored `ComputeFogVectorEXT()` out of
`DrawBasicEffectEXT()` into a shared helper both effects now use. `D3D9_DrawEx` extended to 12/12:
3 new real checks (`Less` compare passes with an exact `texture*DiffuseColor` readback, `Less`
compare fails with the background genuinely left unpainted proving `clip()` really discards, `Equal`
compare passes on the vertex-color bucket). Mutation-verified: forced `isEqNe` to always `false`;
exactly the `Equal`-bucket check (the only one sensitive to a wrong PS selection) went red, the two
`Less`-bucket checks stayed green; reverted, reconfirmed 12/12. Full D3D9 CTest suite (6 binaries)
passes.

`D9-82d`: new `D3D9GraphicsBackend::DrawDualTextureEffectEXT()` (same file). **This row's own
original "4 ShaderIndex values, all unblocked" claim was wrong — corrected during real
implementation: only 2 of the 4 are actually drawable.** Real finding: `DualTextureEffect.fx`'s
real `VSInputTx2` needs a Position+TexCoord0+TexCoord1 vertex (28 bytes) with no equivalent at all
among the 5 layouts D3D9/D3D11/D3D12 previously shared (D3D11's own `dual_texture3d.vert.hlsl`
sidesteps this with a single shared UV set — a legitimate simplification for a custom
reimplementation, not an option here, since this backend draws Microsoft's real unmodified
compiled shader). **Resolved by adding a new, D3D9-only stride-28 vertex declaration**
(`D3D9VertexDeclarations.hpp`/`.cpp`) — safe and backend-local (design decision 12: this table
isn't a `D3DCommon` consumer, doesn't touch any other backend or `GpuDrawParams`). `D3D9_Common`
extended to 29/29 for the new layout. The vertex-color variant (`VSInputTx2Vc`, 32 bytes) still
collides with the existing Position+Normal+TexCoord layout and stays undrawable — same category as
`BasicEffect`'s own `D9-82b` gaps. `texture1`/`DiffuseColor`/`FogVector`/`FogColor` reuse
`D9-82b`/`D9-82c`'s exact formulas verbatim, including the now-3-effects-shared
`ComputeFogVectorEXT()`. `D3D9_DrawEx` extended to 13/13: a new real check (`texture0`=white,
`texture1`=`(100,60,20)`, `DiffuseColor=(0.5,...)` → exact `(100,60,20,255)`, proving the real
doubling-blend formula end to end). Mutation-verified: skipped the `DiffuseColor` upload; exactly
the new check went red (the shared `c0` constant slot retained the PRIOR draw's `AlphaTestEffect`
value, giving a visibly wrong-but-plausible result — a real regression this test genuinely
catches); reverted, reconfirmed 13/13. (A `texture0`/`texture1`-slot-swap mutation was considered
but isn't distinguishable by 1×1 uniform-color textures — the real formula is algebraically
symmetric for constant-color inputs; judged out of scope.) Full D3D9 CTest suite (6 binaries)
passes.

`D9-82e`: new `D3D9GraphicsBackend::DrawEnvironmentMapEffectEXT()` (same file). **This row's own
"8 unblocked" estimate was exactly right**, unlike `D9-82b`/`D9-82d`'s own first-pass estimates:
`specularEnabled` is always `false` in this backend's own dispatch (same category as `BasicEffect`'s
`PreferPerPixelLighting`), making the 8 specular `ShaderIndex` values structurally unreachable, not
merely unimplemented — no separate throw branch needed. `VSInputNmTx` matches the EXISTING stride-32
layout exactly — no new vertex declaration needed here (unlike `D9-82d`). Factored the `oneLight`
derivation out of `DrawBasicEffectEXT()` into a shared `ComputeOneLightEXT()`, now used by both
`BasicEffect` and `EnvironmentMapEffect`. `EmissiveColor` needed NO reconstruction here (unlike
`BasicEffect`) — `EnvironmentMapEffect::FillGpuDrawParams()` already pre-folds
`(emissiveColor+ambient*diffuse)*alpha` itself, uploaded verbatim. `D3D9_DrawEx` extended to 15/15:
2 new real checks mirroring `BasicEffect`'s own Check C/D discipline (non-fresnel buckets only, for
hand-computable exactness) — "basic" bucket/2 lights (exact `(100,130,35)`) and `OneLight`
bucket/1 light (exact `(90,124,33)`, different from the first, proving correct bucket selection).
New 1×1 cube-map textures (`CreateTextureCube(1,false,0)`, all 6 faces the same color, sidesteps
needing to hand-compute `reflect()` geometry). Mutation-verified: forced the newly-shared
`ComputeOneLightEXT()` to always return `true`; BOTH `BasicEffect`'s own 2-light check AND
`EnvironmentMapEffect`'s new 2-light check went red simultaneously — the correct blast radius for a
genuinely shared helper; reverted, reconfirmed 15/15. Full D3D9 CTest suite (6 binaries) passes.

`D9-82f`: new `D3D9GraphicsBackend::DrawSkinnedEffectEXT()` + a new `UploadBonesVS()` helper (same
file). **This row's own "12 unblocked" estimate was exactly right, same as `D9-82e`'s.**
`VSInputNmTxWeights` matches the EXISTING stride-52 layout byte-for-byte — no new vertex
declaration needed. `preferPerPixelLighting` is always `false` (same `D9-81` item-1 gap as
`BasicEffect`), making the 6 pixel-lighting `ShaderIndex` values structurally unreachable — no
separate throw branch needed, mirroring `D9-82e`'s own finding. `Skin(vin, boneCount)` only
transforms Position/Normal before delegating to the SAME `ComputeCommonVSOutputWithLighting()`
`BasicEffect`'s lit path already uses, so `DiffuseColor`/lighting/fog need identical handling (no
new derivation logic); `EmissiveColor` is pre-folded exactly like `EnvironmentMapEffect`'s case.
Genuinely new: `Bones[72]` (`float4x3`, 216 registers, 3/bone) uses the EXACT SAME "first 3 columns
of the transposed matrix" packing `UploadMatrixConstantVS` already established for
`World`/`WorldInverseTranspose` (verified against FNA's own `EffectParameter.SetValue(Matrix)`
`ColumnCount==4/RowCount==3` branch directly), just repeated per-bone into one large
`SetVertexShaderConstantF` call. `D3D9_DrawEx` extended to 17/17: 2 new real checks mirror the
established bucket-selection discipline (vertex-lighting/2 lights → exact `(100,60,20)`;
`OneLight`/1 light → exact `(80,48,16)`), deliberately using a single Identity bone at 100% weight
(skinning is a no-op) so the expected math reduces to the same lit-textured formulas already
established, while still genuinely exercising the full `Bones[72]` upload path. Mutation-verified:
commented out the entire `UploadBonesVS()` call; BOTH new `SkinnedEffect` checks went red (an
untouched `Bones[0]` register left a zero skinning matrix, degenerating the triangle to a point),
every other effect's checks stayed green; reverted, reconfirmed 17/17. Full D3D9 CTest suite (6
binaries) passes.

`D9-83`: new `D3D9InstancedDraw.cpp` — `DrawInstancedPrimitivesEx` via `SetStreamSourceFreq`. Real
XNA 4.0 has no per-instance-aware Stock Effect vertex shader at all, so (matching D3D11/Vulkan/Bgfx's
own identical precedent) this does NOT dispatch through `D3D9EffectDraw.cpp` — it uses a fresh,
hand-authored NOXNA `vs_2_0`/`ps_2_0` shader (`shaders/cna/Instanced3D.hlsl`), compiled via `D9-71`'s
`fxc_tool.exe` and register-verified against a real `D3DDisassemble()` (`D9-72`'s `disasm_tool.exe`)
since it isn't a vendored Microsoft `.fx` file. New stride-64 2-stream vertex declaration (stream 0 =
`POSITION` geometry, stream 1 = `TEXCOORD1-4` per-instance world rows, matching D3D11's own 64-byte
per-instance convention). `SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | instanceCount)` /
`SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1)` follows the MSDN convention exactly; both
are reset to `1` before returning, since D3D9 stream-frequency state persists on the device and every
other draw path here reuses stream 0. `params.instanceVb == nullptr` falls back to
`DrawIndexedPrimitivesEx()`, matching D3D11's own identical fallback.

**Real bug found and fixed — in the new CTest's own pixel-sample coordinates, not the instancing
logic.** The first version of `D3D9_Instanced` sampled `(16,32)`/`(48,32)`, which sit exactly on the
45°-diagonal hypotenuse of the small right-triangle test geometry (confirmed by hand-deriving both
triangles' edge equations) — a rasterization-boundary case, not a rendering failure. A full-backbuffer
scan during debugging showed correctly-shaped, correctly-colored, correctly-positioned geometry was
already painting; only the two single-pixel probes were ill-chosen. Fixed by resampling at
`(14,34)`/`(46,34)`, comfortably inside each triangle's fill region. Every D3D9 API call involved
returned `S_OK` throughout — the shader/declaration/frequency setup was correct from the first working
build.

New `D3D9_Instanced` CTest, 4/4: two distinct instances (translations `-0.5`/`+0.5`) paint the shared
`DiffuseColor` at their own distinct locations in one draw call; `instanceVb==nullptr` reaches real
`BasicEffect` dispatch (not a stub) and throws for the expected reason; a normal
`DrawIndexedColoredPrimitives()` call issued right after the instanced draw still paints correctly,
proving the frequency reset is real. Mutation-verified: hardcoded the instance-count frequency to `1`;
exactly the 2nd-instance check went red, the other 3 stayed green; reverted, reconfirmed 4/4. Full
D3D9 CTest suite: 7/7 binaries pass. XNA 4.0's own instancing is `HiDef`-only — that profile gate is
still not enforced (Phase D9-10 closed `Texture2D` size ceilings but explicitly left this one open,
see that phase's own closure note).

**This closes real, verified dispatch for all 5 XNA Stock Effects plus hardware instancing on this
backend** — only `D9-84` (oracle validation) remains open in Phase D9-8.

### Phase D9-9 — SpriteBatch: D9-90/D9-91/D9-92/D9-93 CLOSED (D9-93 covers 3 of 5 SpriteSortMode values)

| Task | Status |
|---|---|
| `D9-90` — `D3D9SpriteBatchBackend` driving Microsoft's own `SpriteEffect` | ✅ |
| `D9-91` — the half-pixel offset, verified against the oracle | ✅ |
| `D9-92` — sampler filter/address-mode wiring with discriminating probe pixels | ✅ |
| `D9-93` — tested through the public API, diffed across all `SpriteSortMode`s | ✅ (3 of 5: `Deferred`/`BackToFront`/`FrontToBack`; `Immediate`/`Texture` explicitly scoped out) |

**Closed 2026-07-15 (`D9-90`/`D9-91`).** New `D3D9SpriteBatchBackend` (`include/`/`src/CNA/
Internal/Backends/D3D9/D3D9SpriteBatch.{hpp,cpp}`). `SpriteEffect.fx` was already vendored
verbatim (byte-identical to FNA's own copy) with compiled bytecode and a register table already
present in `d3d9_shaders.hpp`/`D3D9ShaderRegisters.hpp` — a side effect of the general D9-71/D9-72
compile sweep that ran across every vendored `.fx` file, not new work this task did. Vertex
contract reuses the EXISTING stride-24 `D3D9VertexDeclarations` layout unchanged (matches FNA's
own `VertexPositionColorTexture4` per-corner shape exactly — resolves D9-22's own "future concern"
about a stride-32 collision as moot, since sprite vertices are stride 24). CPU-side quad geometry
(destination/source rect, origin, rotation, `SpriteEffects` flip) reuses D3D11SpriteBatchBackend's
own already-proven formula verbatim. The genuinely D3D9-specific piece: `MatrixTransform` bakes
BOTH the SpriteBatch's own transform AND a D3D9 half-pixel correction into one real `float4x4`
uniform (`projection.M41 += -0.5*projection.M11; projection.M42 += -0.5*projection.M22`, applied
to a `CreateOrthographicOffCenter(0,W,H,0,0,1)` base) — matching how real XNA/FNA's own
`SpriteBatch.cs` structures this (Microsoft's real `SpriteEffect.fx` has a genuine matrix uniform,
unlike D3D11's own CNA-invented shader which has none).

Verified via 3 new `D9-A5` oracle scenes (`sprite_basic_quad`/`sprite_rotated_quad`/
`sprite_flipped_quad`, all `0/65536` pixel-perfect against real XNA 4.0 on the first attempt) and
a new `D3D9_SpriteBatch` CTest (4/4 checks).

**Real, non-obvious finding surfaced while mutation-testing the half-pixel offset**: a 1×1-texture
scene and quadrant-CENTER sample points are BOTH structurally incapable of detecting this offset
at all — the classic D3D9 half-pixel bug shifts which TEXTURE CONTENT a screen pixel samples, not
where a rectangle's geometric edges land, and a 1×1 texture has only one texel regardless of any
sub-pixel shift. Discovered by commenting out the `M41`/`M42` lines and re-running: the boundary
check and quadrant-center checks stayed green even with the offset entirely removed (a false-
positive "closed" trap), while the SAME mutation made the two 2×2-four-color-texture oracle scenes
diverge from real XNA by `4800/65536` pixels — proving the offset genuinely matters. Fixed by
adding a dedicated CTest check sampling exactly on the internal texel boundary the flip scene's
own draw produces (confirmed this exact pixel shifts from `(130,125,0)` to `(128,128,0)` under the
mutation, every other check staying green); re-verified 4/4 green with the real implementation
restored, all 21 corpus scenes re-verified pixel-perfect, full `D3D9` CTest suite 12/12 green.

**Closed 2026-07-15 (`D9-92`).** `SetSamplerFilter()`/`SetSamplerAddressMode()` already plumbed
through to the real, already-proven `D9-63` `ApplySamplerState()` path — the missing piece was
purely test coverage, since `Begin()` with no arguments only ever exercises the default
`LinearClamp`. Extended the scene format with `spritesourcerect`/`spritesampler` keys and
`SpriteBatch::Begin(sortMode, blendState, samplerState, null, null)` wiring on both sides. Two new
`D9-A5` scenes, both pixel-perfect on the first attempt: `sprite_wrap_quad` (`PointWrap`, a 2×1
RED/GREEN texture sampled with a `sourceRectangle` DOUBLE the texture's own width — tiles the
pattern `RED,GREEN,RED,GREEN` across 4 destination bands) and `sprite_mirror_quad` (a manually
constructed Point/Mirror `SamplerState` — real XNA has no named `PointMirror` preset — same
texture/geometry, folds SYMMETRICALLY around the `U=1` boundary instead: `RED,GREEN,GREEN,RED`,
genuinely distinguishable from `Wrap`'s tiled pattern using identical source data). Both formulas
independently predicted before running either side, then confirmed pixel-for-pixel identical. 2
new checks added to `D3D9_SpriteBatch` (now 6/6). All 24 corpus scenes re-verified pixel-perfect;
full `D3D9` CTest suite 12/12 green.

**Closed 2026-07-15 (`D9-93`), for 3 of 5 `SpriteSortMode` values — `Immediate`/`Texture`
explicitly scoped out, not silently assumed.** Design: 2 overlapping `NonPremultiplied`-blended
sprites (RED tint `(255,0,0,128)` depth=0.0, GREEN tint `(0,255,0,128)` depth=1.0, identical
destination rectangle) — `AlphaBlend` expects premultiplied colors and would give the wrong math
for these raw alpha=128 tints regardless of draw order, so the multi-sprite path switches to
`NonPremultiplied`. 3 new `D9-A5` scenes: `sprite_sortmode_deferred_quad` (insertion order
RED-then-GREEN, GREEN ends up on top, green-dominant `(64,128,0,159)`),
`sprite_sortmode_backtofront_quad` (same insertion order, reordered far-to-near so RED ends up on
top instead, red-dominant `(128,64,0,159)`), `sprite_sortmode_fronttoback_quad` (insertion order
deliberately REVERSED to GREEN-then-RED, so the ascending reorder is genuinely discriminating — it
still puts GREEN on top, matching the Deferred scene's value despite the opposite insertion order,
proving the reorder is by `layerDepth` and not merely insertion order). All 3 pixel-perfect against
real XNA 4.0 once the bug below was fixed. 3 new checks added to `D3D9_SpriteBatch` (now 9/9),
exact-value assertions matching every other check in this file.

**A real, previously-undetected D3D9 backend bug was found and fixed via this task.** Every prior
D9-90/91/92 scene only ever drew with `layerDepth=0.0f`, so `D3D9SpriteBatchBackend::
BuildMatrixTransformEXT`'s own Z-row math was never exercised before now. Its projection used
`CreateOrthographicOffCenter(0,W,H,0,0, zFarPlane=1)`, giving `M33=1/(zNear-zFar)=-1, M43=0` — i.e.
`Z' = -layerDepth` — which maps ANY `layerDepth > 0` outside Direct3D 9's valid `[0,1]` clip-space
Z range, silently clipping the sprite away entirely (confirmed NOT a depth-test artifact:
`DepthStencilState.None` — `ZENABLE=FALSE` — was already correctly in effect; clip-space culling
is a separate, unconditional rasterizer stage). Root-caused by rendering the same scene through the
real XNA 4.0 oracle FIRST and finding it produced the fully-correct blended values while CNA
produced nothing but the RED sprite (the GREEN sprite was being clipped in every case, making
`Deferred` and `BackToFront` render identically — the actual first symptom noticed, before the
cause was understood). Fixed with `zFarPlane=-1` instead, giving an identity Z-row (`M33=1,
M43=0`, so `Z'=layerDepth`, unclipped) — only the Z row changes, the already-verified X/Y
half-pixel math (`D9-91`) depends solely on `M11`/`M22` and is unaffected. Mutation-tested
(reverted to `zFarPlane=1`, confirmed the sort-mode checks then extant FAILED while every other
check stayed green, then restored and reconfirmed all green) per this project's own established
discipline (`D9-91`'s own mutation-testing precedent). All 27 corpus scenes re-verified
pixel-perfect after the fix; full `D3D9` CTest suite 12/12 green.

**Explicitly NOT covered, not silently assumed**: `SpriteSortMode.Immediate` — its only real
behavioral difference from `Deferred` (per-`Draw()` GPU submission instead of batching until
`End()`) is not pixel-observable by this project's oracle methodology (a batched multi-quad draw
call and N individual draw calls in the same order produce the same final raster image), so a
scene for it would not add verification value beyond what `Deferred` already proves.
`SpriteSortMode.Texture` — confirmed NOT a viable oracle scene, not merely deferred: real FNA's
own `SpriteBatch.cs` `TextureComparer.Compare` sorts by `texture.GetHashCode()`, i.e. `Texture`'s
inherited default `Object.GetHashCode()` — an implementation-defined identity hash with no
documented, predictable ordering between two arbitrary textures. Any 2-distinct-texture test's
"which texture group ends up on top" result is genuinely non-deterministic from a black-box
test-authoring perspective (not just hard to hand-derive), so no scene for it can meet this
project's own `--tolerance 0` exact-match discipline reliably.

**Multi-texture batching / a genuine `FlushBatch()`-on-texture-change mid-batch CLOSED 2026-07-15**
(D9-90's own explicitly-named gap, closed as a `D9-A5` scene-corpus addition, not a new Phase D9-9
task ID): new `sprite_multitexture_quad.scene` — 3 non-overlapping sprites, interleaved
RED-texture/BLUE-texture/RED-texture (not RED-RED-BLUE, so the SECOND red draw genuinely forces a
SECOND rebind after the blue draw's own flush) — reuses the scene format's existing `texture2*`
keys plus a new optional trailing `textureIndex` column on `spritedraw=` lines. `0/65536`
pixel-perfect against real XNA 4.0 on the first attempt; mutation-verified (disabled the
texture-change flush trigger, confirmed the middle sprite's BLUE leaked into the third position —
`1600/65536` pixels wrong — then restored and reconfirmed green). New `D3D9_SpriteBatch` Check I
(now 10/10). All 28 corpus scenes re-verified pixel-perfect; full `D3D9` CTest suite 12/12 green.

### Phase D9-10 — `GraphicsProfile` made real: D9-100–D9-105 all CLOSED

| Task | Status |
|---|---|
| `D9-100` — research: XNA 4.0 Reach/HiDef capability floors, mapped onto `D3DCAPS9` fields | ✅ |
| `D9-101` — `GraphicsAdapter::IsProfileSupported()` real on D3D9 | ✅ |
| `D9-102` — `QueryRenderTargetFormat()`/`QueryBackBufferFormat()` real on D3D9 | ✅ |
| `D9-103` — profile enforced at resource creation (`Texture2D` size ceilings) | ✅ |
| `D9-104` — tests: a Reach-illegal request refused, the HiDef equivalent allowed | ✅ |
| `D9-105` — DXVK-synthesized `D3DCAPS9` caveat documented | ✅ |

**Closed 2026-07-15.** Researched via FNA's own `ProfileCapabilities.cs` (`D9-100`'s own table),
cross-checked against Shawn Hargreaves' (XNA team lead) official blog post
`reach-vs-hidef.html`. **Critical finding: `ProfileCapabilities` is dead code in FNA** —
referenced only inside its own file, never consulted by any `Texture2D`/`GraphicsDevice`
constructor or method anywhere in FNA. `GraphicsAdapter.IsProfileSupported()` is FNA's own
unconditional `return true;` with a literal `TODO` (flibit: "This method could be genuinely
useful!"). **FNA therefore has no enforcement BEHAVIOR to port — only the capability NUMBERS are
usable; the validation/throwing logic is this project's own design.** One real discrepancy found
and resolved: FNA's own HiDef table (`MaxTextureSize`/`MaxCubeSize`/`MaxVolumeExtent` =
8192/8192/2048, commented "DX10 min spec") is an *observed hardware ceiling*, not the Hargreaves
blog's own documented HiDef *floor* (4096/4096/256) — this implementation uses the documented
floor, since "capability floor" (the task's own title) means the minimum a profile guarantees,
not an observed ceiling on stronger-than-required hardware. No MSAA level table or instancing
requirement exists anywhere in XNA's own published Reach/HiDef spec — a genuine absence, not an
FNA omission; both stay hardware-queried, not profile-gated.

New `include/`/`src/CNA/Internal/Backends/D3D9/D3D9ProfileCapabilities.{hpp,cpp}`:
`QueryAdapterCapsEXT()` probes real `D3DCAPS9` via `IDirect3D9::GetDeviceCaps()` WITHOUT
constructing a live device (the same pre-device-creation pattern `D3D9GraphicsBackend`'s own
constructor already uses); `MeetsHiDefFloorEXT()` checks the hardware-queryable subset of the
table; `IsRenderTargetFormatSupportedByHardwareEXT()`/`IsBackBufferFormatSupportedByHardwareEXT()`
use `CheckDeviceFormat`/`CheckDeviceType` respectively (the swap chain has stricter
display-compatibility rules than an ordinary render-target texture, D9-30's own finding);
`ClampMultiSampleCountForFormatEXT()` reimplements `D3D9GraphicsBackend::
ClampMultiSampleCountEXT()`'s own `CheckDeviceMultiSampleType` pattern as a free function.
`GraphicsAdapter.cpp`'s three methods wired under `#ifdef CNA_BACKEND_D3D9` — backend-local, the
other 9 backends keep their honest `return true;`/hardcoded-fallback in the `#else` branch
(matches `GraphicsDevice.cpp`'s own `#ifdef CNA_BACKEND_BGFX` precedent for backend-conditional
code in a shared XNA-layer file). `Texture2D`'s two `GraphicsDevice&` constructors now throw
`System::NotSupportedException` (checked BEFORE any pixel allocation) when the requested size
exceeds the profile's own ceiling (2048 Reach / 4096 HiDef) — a profile CEILING, not a hardware
query, regardless of what the device could otherwise support.

**A real, previously-undetected bug was found and fixed along the way, in SHARED cross-backend
code, not D3D9-specific**: `Game`'s own `GraphicsDevice_` member is eagerly default-constructed
(hardcoded `GraphicsProfile::Reach`) BEFORE `GraphicsDeviceManager` even exists, and
`GraphicsDeviceManager::applyToExistingBackend()` threaded a requested profile change into a
transient `GraphicsDeviceInformation` but never wrote it back onto the already-live device before
`Reset()` — a real game's own `graphics.GraphicsProfile = GraphicsProfile.HiDef;
graphics.ApplyChanges();` had **no path to ever reach the actual device**, and
`GraphicsDevice.GraphicsProfile` silently kept reporting `Reach` regardless. This directly
blocked this whole phase's own premise (a profile distinction reachable through the real public
API, not just `D3D9_Smoke` Check K's own backend-direct construction). Fixed with a new NOXNA
`GraphicsDevice::SetGraphicsProfileEXT()`, called from `applyToExistingBackend()` right before
`Reset()`. Cross-backend regression-checked: EasyGL's own `CnaTests`
(`GraphicsAdapterTest`/`GraphicsDeviceDefaultStateTest`/`Texture2DTest`, 70 cases) all still pass
— the fix only makes `graphicsProfile_` correctly track what was requested; no backend besides
D3D9 branches on it at all.

New `D3D9_GraphicsProfile` CTest (`examples/d3d9_graphicsprofile_test.cpp`, 10/10 checks), through
the real public `GraphicsAdapter`/`Game`/`GraphicsDeviceManager`/`Texture2D` API. The required
"Reach-illegal request refused, HiDef equivalent allowed" pair uses
`QueryRenderTargetFormat(_, SurfaceFormat.Single, ...)` (genuinely HiDef-only per the researched
table) rather than this row's own suggested NPOT-wrap example — NPOT-wrap-on-`Reach` was
deliberately set aside (see below). All checks mutation-verified (the `SetGraphicsProfileEXT`
propagation fix and `MaxTextureSizeForProfileEXT`, each independently disabled, correctly failed
their own targeted checks and nothing else, then restored). Full `D3D9` CTest suite 13/13 green.

**D9-105's own honest caveat, restated (not just in `plan_dx9.md`)**: every `D3DCAPS9`-consuming
function here is REAL logic against a REAL `IDirect3D9`/`IDirect3DDevice9` — but in this Wine+DXVK
dev loop, the `D3DCAPS9` VALUES those calls return are DXVK's own synthesized capability set, not
what an authentic XNA-era (~2006-2013) Direct3D 9 driver would report. `IsProfileSupported(HiDef)`
returning `true` here proves the comparison LOGIC is correct, not that a real HiDef-class GPU
exists in this loop (it doesn't). Provisional until `D9-140` (real Windows hardware,
`needs_human`).

**Follow-up CLOSED 2026-07-15: `TextureCube`/`Texture3D` size ceilings and `MaxRenderTargets`
enforcement.** All three reuse `D3D9ProfileCapabilities`' own already-written helpers (no new
capability logic, just wiring): `TextureCube` throws past 512 (Reach)/4096 (HiDef);
`Texture3D` throws UNCONDITIONALLY under Reach (volume textures unsupported at all, not merely
size-capped) and past 256 under HiDef; `GraphicsDevice::SetRenderTargets()` throws past 1 target
under Reach (4 under HiDef) — a separate, lower, software-imposed ceiling from
`MAX_RENDERTARGET_BINDINGS` (XNA's general 4-target cap) and from `D9-54`'s own hardware-cap
enforcement inside the backend. 8 new checks (`D3D9_GraphicsProfile` now 19/19), same
"same request refused under Reach, allowed under HiDef" pattern, mutation-verified (disabled all
3 new profile-ceiling functions at once, confirmed exactly the 6 tied checks — 3 per profile —
went red, restored). Regression-checked again on EasyGL (150 relevant `CnaTests` cases, all
green). Full `D3D9` CTest suite 13/13 green.

**Explicitly NOT covered, not silently assumed**: hardware-instancing's own real HiDef-only gate
(noted as a Phase D9-10 follow-up back when `D9-83` closed, still open); and NPOT-wrap-on-`Reach`
(D9-56's own originally-deferred example) — real XNA's own enforcement timing/behavior here is
undocumented, and FNA implements none of it either (confirmed by `D9-100`'s own research), so
inventing one without a reference would risk asserting behavior this project cannot actually
verify against real XNA. Both real, honest follow-ups, not claimed done.

### Phase D9-11 — Custom `ShaderEffect`: AUTHORIZED 2026-07-15, FULLY CLOSED same day

New `D3D9ConstantTable.{hpp,cpp}` (`ParseConstantTableEXT()`): a real CTAB (constant table) binary
parser reading Microsoft's own `D3DXSHADER_CONSTANTTABLE`/`D3DXSHADER_CONSTANTINFO` structures
directly out of compiled D3D9 shader bytecode, no `ID3DXConstantTable`/D3DX linkage anywhere
(design decision 9). Needed because, unlike D3D11's own fixed-slot `D3D11EffectBackend` convention
(HLSL cbuffer offsets are caller-predictable), D3D9 constant *registers* are assigned by the
compiler and can vary — only a real post-compile name→register lookup is reliable.

New `D3D9_ConstantTable` CTest (`examples/d3d9_constanttable_test.cpp`, 14/14): compiles a known
3-constant test shader via the real `D3DCompile()`, then cross-checks the parser's output against
`D3DDisassemble()`'s own independent `"// Registers:"` text block (same regex
`extract_shader_registers.py`, `D9-72`, already uses) rather than trusting the binary parser
blindly. **Found and fixed a real bug this way**: the first attempt returned 0 constants — a debug
byte-dump against real compiler output found every offset field inside the CTAB structures is
relative to 4 bytes *past* the `'CTAB'` FourCC (where `Size` begins), not the FourCC itself.
Mutation-verified (reverted to the wrong offset base — parser reads the shader-version-token field
as an absurd constant count and crashes with `std::bad_alloc`; reverted, reconfirmed 14/14 green).
Added a defensive sanity bound so malformed/corrupted CTAB data returns empty rather than crashing.
Full `D3D9` CTest suite now 15/15.

**`D9-111` CLOSED**: new `D3D9EffectBackend.{hpp,cpp}` (`IEffectBackend`) — real runtime
`D3DCompile()` (`vs_2_0`/`ps_2_0` Reach, `vs_3_0`/`ps_3_0` HiDef), `SetUniform*` genuinely looks
`name` up per-stage via `D9-110`'s own real register tables (not D3D11EffectBackend's fixed-slot
convention — D3D9 registers are compiler-assigned and vary per shader). Build-isolated per design
decision 16: excluded from the main backend source glob, built as its own
`cna_backend_graphics_d3d9_effect` static library with `d3dcompiler` linked ONLY there — the stock
D3D9 pipeline stays dependency-free, a real divergence from D3D11/D3D12's own simpler "link it to
the whole backend" precedent. New `D3D9_EffectBackend` CTest (6/6), matching D3D11EffectBackend's
own `DX-58` test bar exactly (compile + bind + draw + uniform-driven pixel readback) on a real
device. **Real finding via mutation-testing**: the original single "far-away WorldViewProj leaves
background unpainted" check wasn't actually discriminating — an entirely-disabled vertex-constant
upload ALSO leaves the register at zero, which ALSO degenerates the triangle to nothing, for a
completely different (broken) reason. Fixed by splitting into a positive-case anchor (identity
re-upload must still paint red) immediately before the negative case; re-mutated and confirmed the
anchor now correctly fails first. Full D3D9 CTest suite now 16/16.

**`D9-112` CLOSED — `SpriteBatch::Begin(effect)` wiring, Phase D9-11 now FULLY CLOSED.** New
`D3D9SpriteBatchBackend::SetCustomEffect()` (flush-on-change, mirrors D3D11's identical pattern).
`FlushBatch()` branches on a valid custom `D3D9EffectBackend`: uploads viewport size to a
`"vpSize"` uniform (reusing `D9-111`'s own generic `SetUniformVec2()` directly — no dedicated
method needed, unlike D3D11's `SetViewportSizeEXT()`, since D3D9's real per-name lookup has no
fixed-slot limitation to work around), calls `Apply()` then `Bind()`, replacing the stock
shader/`MatrixTransform` block. Vertex declaration/texture/sampler binding stay unchanged between
paths — genuinely simpler than D3D11 (whose `InputLayout` is baked into specific shader bytecode)
since D3D9's declaration is a decoupled device state. New `D3D9GraphicsBackend::CreateEffectBackend()`
and the matching `CMakeLists.txt` circular-link fix (D3D9 joins the `CNA`-back-link `OR` chain,
closing the gap `D9-10`'s own row deferred here).

**Real, previously-invisible bug found and fixed**: moving `D3D9EffectBackend.cpp` out of the main
backend glob while `D3D9ConstantTable.cpp` stayed in it created a genuine link-order circular
dependency between the two new targets — every D3D9 test binary failed with `undefined reference
to ParseConstantTableEXT`. Root-caused: `D3D9ConstantTable.cpp` has no consumer outside
`D3D9EffectBackend.cpp`, so it moved into the isolated effect target too, eliminating the cycle.

New `D3D9_SpriteBatch_CustomEffect` CTest (4/4), through the real public `SpriteBatch`/`ShaderEffect`
API, mirroring D3D11's own `DX-71` test bar (a runtime-compiled RGB-inversion shader replaces the
stock pipeline for a batch) in real D3D9 SM2/SM3 HLSL syntax. All 4 passed on the first successful
build. Mutation-verified: forced the custom-effect branch unreachable, confirmed exactly the
color-inversion discriminator failed while position/restore-to-stock checks correctly stayed green
(the stock path still draws in the right place, just uninverted — expected, not a test gap);
reverted clean. Full D3D9 CTest suite now 17/17; EasyGL/`CnaTests` regression-checked (491/491+,
`*SpriteBatch*` filter 36/36) — every CMake change scoped inside `CNA_GRAPHICS_BACKEND STREQUAL
"D3D9"` guards.

### Phase D9-12 — the indistinguishability suite: ALL CLOSED (D9-120/D9-121/D9-122/D9-123)

| Task | Status |
|---|---|
| `D9-120` — promote `D9-A`'s corpus to a real, checked-in-reference-image CTest | ✅ |
| `D9-121` — a written, honest divergence report | ✅ |
| `D9-122` — `D3D9_Smoke`/`D3D9_Common` + the 4 reused state tests, mutation-verified | ✅ |
| `D9-123` — `CnaTests` under `CNA_GRAPHICS_BACKEND=D3D9` | ✅ (both the setenv compile blocker and the follow-up `gtest_discover_tests` CTest-registration blocker fixed and verified, see below) |

**Closed 2026-07-15 (`D9-120`/`D9-121`).** All 31 `D9-A5` scenes' real-XNA-4.0 renders regenerated
FRESH (not trusted from an earlier cached run) and confirmed byte-identical (SHA-256) to the
already-cached versions before checking them in at `tools/xna-oracle/reference/*.png` (132 KB
total). New `scripts/run-oracle-corpus-diff.sh` renders each scene via `cna_oracle_render.exe`
through the existing D3D9 Wine wrapper (**not** the XNA one) and diffs against the checked-in
reference at `tolerance=0`. New `D3D9_XNA_Diff` CTest — full D3D9 suite now 14/14, ~88s for the
complete 31-scene sweep, zero XNA-prefix dependency at test-run time. Mutation-verified: corrupted
one checked-in reference pixel, confirmed the script reports exactly that scene FAIL (with the
real delta) and exits 1, restored, reconfirmed 31/31 green.

New `docs/d3d9-divergence-report.md` — headline result **0/31 scenes diverge from real XNA 4.0 at
tolerance=0**, with an explicit table of what the corpus does NOT yet cover (so the empty-list
result isn't mistaken for total coverage) and an honest, current-status re-read of all six
project-wide CNA-vs-XNA divergences `plan_dx9.md`'s own section names: Divergence 3
(`GraphicsProfile` decorative) is now CLOSED on D3D9 (Phase D9-10); Divergence 4 (`SpriteBatch`
half-texel convention never modeled) is now MEASURED AND CONFIRMED CORRECT on D3D9 (`D9-91`);
Divergences 1/5/6 remain genuinely unmeasured or only partially measured; Divergence 2 is resolved
for D3D9's own dispatch correctness only. Also documents the 2 real backend bugs this whole
measurement effort found and fixed (`D9-93`'s `SpriteSortMode` Z-clipping; Phase D9-10's own
`GraphicsProfile`-never-reaches-the-device bug) as evidence the methodology finds real problems.

**Closed 2026-07-15 (`D9-122`).** Read every task's closure note that produced a `D3D9_Smoke`/
`D3D9_Common` check (`D9-23`, `D9-30`–`D9-64`, `D9-82`/`D9-83`, Phase D9-10) and classified each
check CONFIRMED (explicit mutation-test evidence on record) or GAP (never deliberately broken).
Ran 6 new mutation cycles against the highest-priority GAPs — each: break the implementation →
rebuild `cna_test_d3d9_smoke` → confirm EXACTLY the predicted check(s) go red and nothing else →
revert → rebuild → reconfirm `55/55`:

1. `ApplyBlendState()`'s `D3DRS_DESTBLEND` forced to `D3DBLEND_ONE` — first-ever mutation proof for
   `D3D9_BlendState_Opaque`/`D3D9_BlendState_AlphaBlend`.
2. `PerformResetRecovery()`'s `deviceLost_ = false;` disabled — Check M's own recovery assertion
   failed as predicted, and surfaced a genuine finding (not a bug): the very next line in Check M
   is a bare `dev.Clear(...)` with no try/catch, so a still-lost device throws an uncaught
   `DeviceLostException` and crashes the whole test binary (exit 3) before Checks N–Z ever run — a
   broken device-lost recovery doesn't fail one check, it takes the rest of the suite down with it.
3. `SetRenderTargets()`'s per-slot bind loop hardcoded to slot 0 — failed only Check V's "both
   targets get the exact color" assertion; the pre-existing "over-request throws" and "unbind
   restores back buffer" halves stayed correctly unaffected.
4. `D3D9RenderTargetBackend::BindAsRenderTarget()`'s `SetRenderTarget(0, ...)` disabled — failed
   both Check S (baseline, previously UNVERIFIED) and Check U (MSAA), Check T (cube) correctly
   unaffected, confirming 2D/cube code-path isolation.
5. `D3D9VertexBufferBackend::Upload()`'s `memcpy` swapped for a zeroing `memset` — failed Check N
   (baseline round-trip, previously UNVERIFIED), Check P (device-lost buffer recovery), and
   cascaded into Check Z's two `SetDepthTestEnabled` assertions (expected — Check Z's quads come
   from `SetData()`-uploaded buffers, a real transitive dependency, not a false positive).
6. `D3D9TextureCubeBackend::SetData()`'s row-copy `memcpy` swapped for zeroing — failed only Check
   R's cube-face round-trip assertion (previously UNVERIFIED).

All 6 reverted (`git diff --stat` confirmed zero-diff each time), full `D3D9_Smoke` reconfirmed
`55/55` after every cycle. **Explicitly NOT mutation-tested, real remaining gaps, not silently
assumed covered**: Check Y's `MAGFILTER`/`MIPFILTER`/`ADDRESSV` fields (deprioritized — same
combined boolean assertion as the already-CONFIRMED `ADDRESSU`); Check R's volume-texture half
(only the cube half was tested); `D3D9_Smoke` Checks B/C/D–I/J/L; `D3D9_Common`'s other 29 of 30
checks (only 1 `CullMode`-mapping check has explicit evidence, judged lower-priority — homogeneous
lookup-table checks, method already proven on one representative entry). Full detail in
`plan_dx9.md`'s own `D9-122` row.

**`D9-123` — FULLY CLOSED 2026-07-15, both the compile blocker and its own downstream follow-up.**
The POSIX `::setenv()`/`::unsetenv()` wall (`AudioEngineTests.cpp`/`WaveBankTests.cpp`/
`CueTests.cpp` and 10 other files) is resolved — all 62 call sites replaced with
`System::Environment::SetEnvironmentVariable` (`sharp-runtime`, already MinGW-proven). `CnaTests`
now compiles and links cleanly under `CNA_GRAPHICS_BACKEND=D3D9` for the first time ever.

That fix surfaced a second, previously-unreachable blocker: `gtest_discover_tests` tried to
execute the cross-compiled `CnaTests.exe` directly to enumerate test names, and a PE32+ Windows
binary can't run natively on Linux without a Wine `CMAKE_CROSSCOMPILING_EMULATOR` — invisible
before since the binary never compiled far enough to reach this step. Measured the naive fix's
real cost first (a single Wine spawn costs ~1.2s; the discovered suite has 4367 individual test
cases, so spawning each separately would cost ~87 minutes of pure process overhead) before picking
an approach. Fixed by setting `CROSSCOMPILING_EMULATOR` on the `CnaTests` target (the same
CMake-native mechanism `DX-80`'s own `cna_d3d11_ctest_command` macro already uses for D3D11/D3D12's
own CTests), selecting the correct per-backend Wine wrapper with its DXVK/vkd3d-proton
authenticity gate deliberately disabled (`CnaTests` spans non-Graphics namespaces that never open
a device, so that gate would misreport every one of those as a fake fallback). This also
automatically fixed `CnaInputTests`' own separate `add_test` registration a few lines below, with
no code change needed there. Deliberately did NOT redesign test granularity — the project's real
workflow (`ctest -L D3D9`) label-filters, and none of the 4367 discovered cases carry that label,
so they're registered but never actually invoked by the normal command; the 87-minute concern only
applies to a hypothetical unfiltered full run.

**Verified end-to-end, not just "no longer crashes"**: `ctest -L D3D9` — 14/14 pass, no more
test-file-generation crash; `ctest -N` shows 4383 total registered tests; explicitly ran 2
individual discovered cases plus `CnaInputTests` itself via `ctest -R` — all genuinely execute
through Wine and pass. **EasyGL regression-checked**: reconfigured + rebuilt, ran the same 2
spot-checks natively — pass at native speed, `CMAKE_CROSSCOMPILING` guard correctly no-ops for the
9 already-established non-cross backends. Not independently re-verified on D3D11/D3D12's own build
dirs (not configured in this worktree); full detail in `plan_dx9.md`'s `D9-123` row.

### Phase D9-13 — docs: `D9-130` CLOSED, `D9-140` still open (`needs_human`)

New `docs/d3d9-backend.md` — leads with what this backend is *for* (XNA pixel authenticity, not
feature parity), the fact that it runs Microsoft's own vendored Stock Effects HLSL bytecode, and
the 0/31-divergence oracle result, before any "known limitations" list. A full `D3D9` column was
added across all 7 tables in `docs/graphics-backend-feature-matrix.md` (2D SpriteBatch/SpriteFont,
Stock Effects, RenderTarget/MSAA/mip/depth, Texture2D/3D/Cube, GraphicsDevice state objects,
OcclusionQuery, Model) plus a new "Remaining genuine D3D9 limitations" section, matching the
existing Vulkan/Bgfx precedent. Every cell was grounded by actually reading
`tools/xna-oracle/scenes/*.scene` rather than recalled from memory (e.g. confirmed exactly which
`SpriteSortMode`/`AlphaTestEffect.AlphaFunction`/`WeightsPerVertex` values have a dedicated scene,
and that `EnvironmentMapEffect.specularEnabled` is structurally unreachable on this backend's
current dispatch, `D9-82e`) — cells that couldn't be grounded this way are honestly `⬜`/`🟨`, not
assumed `✅`. `README.md` gained a `D3D9` Project-Status bullet, a "Build (Windows
cross-compilation — D3D9 backend)" section mirroring D3D11/D3D12's, and a Tested-Compilers row.
`programs.md` §9 gained the D3D9-specific three-Wine-prefix subsection this document's own
`plan_dx9.md` line 103 had flagged as a gap (`programs.md` previously documented only the D3D11
prefix). `D9-140` (real Windows hardware) remains open, `needs_human`, unchanged.

### Does NOT work yet

`BasicEffect`'s `PreferPerPixelLighting` variants, `EnvironmentMapEffect`'s specular variants, and
`SkinnedEffect`'s `PreferPerPixelLighting` variants (all blocked on `D9-81`'s still-open
`GpuDrawParams` gaps), and any `BasicEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
`SkinnedEffect` combination with no matching CNA vertex layout (`D9-82b`/`D9-82d`'s own
enumerations) — all still throw a named not-yet-implemented naming their own follow-up task, by
design. `SpriteBatch` now has a real, oracle-verified `D3D9SpriteBatchBackend` (Phase D9-9,
`D9-90`–`D9-93` all CLOSED, see that phase's own section above) — no longer belongs in this list.
`BasicEffect`'s realistically-drawable 10 `ShaderIndex` values, all 8 of
`AlphaTestEffect`'s, 2 of `DualTextureEffect`'s 4, all 8 non-specular of `EnvironmentMapEffect`'s
16, all 12 non-pixel-lighting of `SkinnedEffect`'s 18, and the narrow
`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` path are real — every XNA Stock Effect has a
real dispatch path now. The mapping tables (`D9-20`–`23`) are now consumed by the render-state push path,
the buffer-creation path, the texture/render-target-creation paths, and the draw path itself.

---

## 3. Recent changes

Most recent first. Full detail lives in `plan_dx9.md` — this is a short index.

| Commit(s) | Summary |
|---|---|
| *(pending)* | **RESOLVED the documented render-target-as-texture D3D9 crash from `08aba091`/§4 — a real CNA bug, not a DXVK/environment limitation.** Root cause: every `D3D9EffectDraw.cpp` texture-binding call site did `static_cast<const D3D9TextureBackend*>(params.texture0)` unconditionally, which is undefined behavior whenever `params.texture0` actually points at a `D3D9RenderTargetBackend` (a real, legal runtime type for that `const ITextureBackend*` field, since `IRenderTargetBackend : ITextureBackend`) — exactly what happens when a `RenderTarget2D` is sampled as an ordinary effect texture. `D3D11GraphicsBackend.cpp` already solves this exact problem (`GetSrvForTextureEXT`, a two-concrete-type `dynamic_cast` resolver) — `D3D9` never had the equivalent. Fixed with new `ResolveD3D9TextureEXT`/`ResolveD3D9TextureCubeEXT` helpers (mirroring D3D11's own precedent) replacing all 6 unsafe `static_cast` sites in `D3D9EffectDraw.cpp`, plus the same fix in `D3D9SpriteBatch.cpp`'s own analogous (already `dynamic_cast`-safe, non-crashing but silently-wrong) gap. **Mutation-verified**: temporarily reintroduced the exact original `static_cast`, reproduced the documented symptom verbatim (`terminate called after throwing an instance of 'dxvk::DxvkError'`); reverted, reconfirmed the fix. New `D3D9_DrawEx` Check Q (18 checks): a `D3D9RenderTargetBackend` created/bound/`Clear()`ed/unbound then sampled directly as an ordinary `BasicEffect` texture — exact readback, no crash. Full `ctest -L D3D9`: 17/17 green, zero regressions. Deliberately did not re-attempt the reverted oracle-corpus scene (`D9-A5`/`D9-84`'s own territory) as part of this fix. See §4's own updated record and `plan_dx9.md`'s `D9-84` row. |
| *(pending)* | **`D9-A6` CLOSED — the oracle corpus run against a SECOND backend (EasyGL) for the first time ever.** Confirmed `tools/xna-oracle/CnaOracleRender.cpp` was already backend-agnostic before touching it (only two `printf` strings and one comment literally said `D3D9`) — added a small `OracleBackendName()` helper keyed off the `CNA_BACKEND_*` compile definitions, no other line changed. New, purely-additive `cna_easygl_test(cna_oracle_render_easygl ...)` CMake registration inside the existing EasyGL-tests section (not a CTest, same "comparison tool" precedent as `cna_oracle_render`); new non-Wine `scripts/run-oracle-corpus-diff-easygl.sh` twin driver script. Existing D3D9 registration/CTest/script untouched (`cmake --build cmake-build-d3d9 --target cna_oracle_render` re-verified still green after the change). **Result: 10/31 pixel-perfect (all `sprite_*` + `alphatest_never_quad`), 21/31 diverge in three evidenced patterns** — (1) 17 scenes diverge only in a thin silhouette-edge band (rasterization fill-rule/pixel-center convention gap vs. D3D9-over-DXVK, most likely), notably including 5 "lit" scenes that turn out structurally incapable of exposing the predicted `preferPerPixelLighting` gap (uniform normal/light by construction); (2) 2 scenes (`colored3d`, `colored_trianglestrip_quad`) diverge almost everywhere but by only 1-3/channel — ordinary Mesa/RADV-vs-DXVK float rounding noise, not a bug; (3) 2 real, previously-unmeasured `plan_graphics.md` candidates — `fog_gradient_quad` renders fully-fogged/black everywhere (including the near/unfogged edge) instead of the correct linear gradient (likely a negative-`FogEnd`-specific EasyGL bug), and `envmap_fresnel_quad` renders nearly the whole quad at the bright top-edge Fresnel value instead of Gouraud-interpolating to the dim bottom edge — a concrete confirmation of this plan's own predicted design-decision-8 gap, in the one scene actually shaped to detect it. All three patterns logged in `docs/d3d9-divergence-report.md`'s new "Cross-backend measurement (D9-A6)" section, **none investigated further or fixed**, per this row's own explicit rule. Vulkan/D3D11 remain unmeasured. |
| *(pending)* | **`D9-112` CLOSED — `SpriteBatch::Begin(effect)` wiring, Phase D9-11 FULLY CLOSED.** New `D3D9SpriteBatchBackend::SetCustomEffect()` (flush-on-change, mirrors D3D11's identical pattern). `FlushBatch()` branches on a valid custom `D3D9EffectBackend`: uploads viewport size via `D9-111`'s own generic `SetUniformVec2("vpSize", ...)` (no dedicated method needed, unlike D3D11's `SetViewportSizeEXT()`), calls `Apply()` then `Bind()`, replacing the stock shader/`MatrixTransform` block — vertex declaration/texture/sampler binding stay unchanged between paths (D3D9's declaration is a decoupled device state, simpler than D3D11's shader-baked `InputLayout`). New `D3D9GraphicsBackend::CreateEffectBackend()` + the matching `CMakeLists.txt` circular-link fix (D3D9 joins the `CNA`-back-link `OR` chain, closing the gap `D9-10` deferred here). **Real bug found and fixed**: moving `D3D9EffectBackend.cpp` out of the main glob while `D3D9ConstantTable.cpp` stayed in it created a genuine link-order cycle between the two new targets (`undefined reference to ParseConstantTableEXT` on every D3D9 test binary) — root-caused and fixed by moving `D3D9ConstantTable.cpp` into the isolated effect target too (it has no other consumer). New `D3D9_SpriteBatch_CustomEffect` CTest (4/4) through the real public `SpriteBatch`/`ShaderEffect` API, mirroring D3D11's own `DX-71` test bar in real D3D9 SM2/SM3 HLSL — all 4 passed on the first successful build. Mutation-verified (forced the custom-effect branch unreachable, confirmed exactly the color-inversion discriminator failed while position/restore-to-stock checks correctly stayed green; reverted clean). Full D3D9 suite now 17/17; EasyGL/`CnaTests` regression-checked. |
| `f69094dd` | **`D9-111` CLOSED — `D3D9EffectBackend`, real runtime `D3DCompile()` custom-ShaderEffect backend**. New `D3D9EffectBackend.{hpp,cpp}` (`IEffectBackend`): `CompileProgram()` compiles vertex+pixel source separately (`vs_2_0`/`ps_2_0` Reach, `vs_3_0`/`ps_3_0` HiDef), `SetUniform*` genuinely looks `name` up per-stage via `D9-110`'s own real register tables — not D3D11EffectBackend's own fixed-slot convention, since D3D9 registers are compiler-assigned and vary per shader. Build-isolated per design decision 16: `D3D9EffectBackend.cpp` excluded from the main backend source glob, built as its own `cna_backend_graphics_d3d9_effect` static library with `d3dcompiler` linked ONLY there — the stock D3D9 pipeline stays dependency-free, diverging from D3D11/D3D12's own simpler "link it to the whole backend" precedent. New `D3D9_EffectBackend` CTest (6/6), matching D3D11EffectBackend's own `DX-58` test bar (compile+bind+draw+uniform-driven pixel readback) on a real device — all 6 passed on the FIRST successful build. **Real finding via mutation-testing**: the original single "far-away WorldViewProj leaves background unpainted" check wasn't discriminating — a fully-disabled vertex-constant upload also leaves the register at zero, also degenerating the triangle to nothing, for a completely different reason. Fixed by splitting into a positive-case anchor (identity re-upload must still paint red) before the negative case; re-mutated and confirmed the anchor now correctly fails first. All mutations reverted (`diff`-confirmed byte-identical each time). Full D3D9 suite now 16/16; EasyGL/CNA build regression-checked. `D9-112` remains open. |
| `d01bbfb1` | **Phase D9-11 authorized 2026-07-15; `D9-110` (CTAB constant-table parser) CLOSED**. New `D3D9ConstantTable.{hpp,cpp}` — real `D3DXSHADER_CONSTANTTABLE`/`D3DXSHADER_CONSTANTINFO` binary parsing (no D3DX/`ID3DXConstantTable`, design decision 9), locating the CTAB comment token via the same DWORD-walking strategy `compare_against_fxb.py` (`D9-73`) already proved against 66 real shaders. New `D3D9_ConstantTable` CTest (14/14): compiles a known 3-constant shader via real `D3DCompile()`, cross-checks against `D3DDisassemble()`'s own independent `"// Registers:"` text (same regex as `extract_shader_registers.py`). **Found and fixed a real bug via this cross-check**: first attempt returned 0 constants — a raw byte-dump against real compiler output found every CTAB offset field is relative to 4 bytes past the `'CTAB'` FourCC (where `Size` begins), not the FourCC itself. Mutation-verified (reverted the fix, parser reads garbage and crashes with `std::bad_alloc` — an even more dramatic catch than a value mismatch); reverted clean, reconfirmed 14/14. Added a defensive bound so malformed CTAB data returns empty instead of crashing. Full D3D9 suite now 15/15; EasyGL/CNA build unaffected (file only compiles under D3D9). |
| `4ec9e781` | **`D9-123` FULLY CLOSED — the `gtest_discover_tests` cross-compile follow-up**. `CMakeLists.txt:7117`'s `gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST)` had no `MINGW`/`CMAKE_CROSSCOMPILING` guard and tried to directly execute the cross-compiled `CnaTests.exe` (a PE32+ binary) to enumerate test names — invisible before the setenv fix landed. Measured the naive per-test-Wine-spawn cost first (~1.2s/spawn × 4367 discovered cases ≈ 87 minutes) before picking an approach. Fixed by setting `CROSSCOMPILING_EMULATOR` on the `CnaTests` target (same CMake-native mechanism `DX-80`'s own `cna_d3d11_ctest_command` macro already uses), routing through the correct per-backend Wine wrapper with its DXVK/vkd3d-proton authenticity gate deliberately disabled inline (`env CNA_D3D9_SKIP_DXVK_GATE=1 <wrapper>`) — `CnaTests` spans non-Graphics namespaces that never open a device. Also automatically fixed `CnaInputTests`' own separate `add_test`, no extra change needed. Deliberately kept `gtest_discover_tests` as-is rather than redesigning granularity: confirmed the real workflow (`ctest -L D3D9`) label-filters and none of the 4367 discovered cases carry that label, so the 87-minute concern never applies to the actual documented command. **Verified end-to-end**: `ctest -L D3D9` 14/14 pass (no more test-file-generation crash); `ctest -N` shows 4383 total registered tests; 2 individual discovered cases plus `CnaInputTests` itself explicitly run via `ctest -R` and genuinely execute through Wine, not just register. EasyGL regression-checked (reconfigured + rebuilt, same 2 spot-checks pass natively, `CMAKE_CROSSCOMPILING` guard correctly no-ops). Not independently re-verified on D3D11/D3D12's own build dirs. `plan_dx9.md`'s `D9-123` row now ✅ in full. |
| `5e5dcc7c` | **`D9-123` setenv compile blocker IMPLEMENTED (project-owner go-ahead given) — `CnaTests` compiles under D3D9 for the first time ever**. All 62 `::setenv()`/`::unsetenv()` call sites (60 setenv + 2 unsetenv — a small correction from the proposal's "63+2" estimate) across 13 files replaced with `System::Environment::SetEnvironmentVariable`; `#include "System/Environment.hpp"` added where missing. `tools/audio/audio_no_hardware_harness.cpp` already had a working `#if _WIN32` `_putenv_s()` branch (not actually blocking) — simplified to the shared wrapper for consistency anyway. EasyGL regression-checked: 491/491 tests pass across the 11 affected suites, full-output-grepped for `FAILED`. D3D9 verified: compiles and links with zero errors and zero remaining setenv/unsetenv. Surfaced a second, distinct `gtest_discover_tests` blocker (see the entry above, fixed the same day). |
| `cf082a52` | **`D9-123` written proposal (superseded by implementation above)** — new `docs/cnatests-mingw-setenv-proposal.md`, grounded by actually grepping every call site rather than the earlier "~10 test files" estimate. Confirmed a zero-new-risk fix already exists: `sharp-runtime`'s `System::Environment::SetEnvironmentVariable` already branches `_putenv_s()` on `_WIN32`, already compiles/links in the existing D3D11/D3D12 MinGW builds, matches .NET's empty-value-unsets convention — a mechanical 1:1 replace, no new abstraction. |
| `5e3a82c0` | **`D9-130` CLOSED (Phase D9-13 docs) — new `docs/d3d9-backend.md`, a full `D3D9` column across all 7 tables in `docs/graphics-backend-feature-matrix.md`, and a `README.md`/`programs.md` build-doc update**. `docs/d3d9-backend.md` leads with XNA pixel-authenticity (not a feature checklist), the real-Microsoft-shader fact, and the 0/31-divergence oracle result, following `docs/d3d11-backend.md`'s own structure. Every feature-matrix cell was grounded by reading `tools/xna-oracle/scenes/*.scene` directly (confirmed exact `SpriteSortMode`/`AlphaFunction`/`WeightsPerVertex` coverage, and that `EnvironmentMapEffect.specularEnabled` is structurally unreachable, `D9-82e`) rather than recalled from memory; ungrounded cells marked honestly `⬜`/`🟨`. New matrix section "Remaining genuine D3D9 limitations", matching the Vulkan/Bgfx precedent sections. `README.md`: new `D3D9` Project-Status bullet, a "Build (Windows cross-compilation — D3D9 backend)" section, a Tested-Compilers row. `programs.md` §9: new D3D9-specific three-Wine-prefix subsection, closing the gap `plan_dx9.md`'s own line 103 flagged. |
| `2a0f1576` | **Phase D9-12 `D9-122` CLOSED — systematic mutation-verification of `D3D9_Smoke`/`D3D9_Common` + the 4 reused state tests**. Classified every check as CONFIRMED (explicit prior mutation evidence) or GAP, then ran 6 new mutation cycles against the highest-priority GAPs (`ApplyBlendState`'s `D3DRS_DESTBLEND`, `PerformResetRecovery`'s `deviceLost_` flag — which also surfaced that a broken recovery crashes the whole test binary via an uncaught `DeviceLostException`, not just failing one check — `SetRenderTargets`' per-slot MRT bind, `BindAsRenderTarget`, `D3D9VertexBufferBackend::Upload`, `D3D9TextureCubeBackend::SetData`), each confirmed to fail exactly the predicted check(s) and nothing else, then reverted clean. Full D3D9 CTest suite reconfirmed 14/14 independently (not just the closing agent's own self-report). Full detail in `plan_dx9.md`'s own `D9-122` row and §2's Phase D9-12 section above. |
| `65ba7ce8` | **Phase D9-12 `D9-120`/`D9-121` CLOSED — the D9-A oracle corpus is now a real, checked-in CTest, plus a written divergence report**. All 31 real-XNA-4.0 reference PNGs regenerated fresh and confirmed byte-identical to earlier cached renders before committing (`tools/xna-oracle/reference/*.png`, 132 KB). New `scripts/run-oracle-corpus-diff.sh` + `D3D9_XNA_Diff` CTest -- diffs every scene against its checked-in reference at `tolerance=0`, needs only the D3D9 Wine prefix (never the XNA one) to run. Mutation-verified (corrupted one reference pixel, confirmed exactly that scene failed with the real delta, restored). New `docs/d3d9-divergence-report.md`: headline **0/31 scenes diverge from real XNA 4.0**, with an explicit "not yet covered" table and an honest status re-read of all 6 project-wide CNA-vs-XNA divergences (3 now closed on D3D9, 4 now measured-correct on D3D9, 1/5/6 still open). Full D3D9 CTest suite now 14/14. |
| `389470fb` | **Phase D9-10 follow-up CLOSED — `TextureCube`/`Texture3D` profile size ceilings + `MaxRenderTargets` enforcement**. Reuses `D3D9ProfileCapabilities`' own already-written helpers (no new capability logic, just wiring): `TextureCube` throws past 512 (Reach)/4096 (HiDef); `Texture3D` throws UNCONDITIONALLY under Reach (volume textures unsupported entirely) and past 256 under HiDef; `GraphicsDevice::SetRenderTargets()` throws past 1 target under Reach (4 under HiDef) -- separate from `MAX_RENDERTARGET_BINDINGS` (XNA's general cap) and `D9-54`'s own hardware-cap enforcement. 8 new checks (`D3D9_GraphicsProfile` now 19/19), mutation-verified (disabled all 3 new profile functions at once, confirmed exactly the 6 tied checks failed, restored) -- also found and fixed a real bug in the CTest's OWN cleanup logic (only unbinding render targets on the throw path left them bound and crashed `Present()` when a mutation made the call NOT throw). Regression-checked again on EasyGL (150 relevant `CnaTests` cases, all green). Full D3D9 CTest suite 13/13 green. Only NPOT-wrap-on-`Reach` and hardware-instancing's HiDef-only gate remain open in Phase D9-10. |
| `9c3210df` | **Phase D9-10 CLOSED (`D9-100`–`D9-105`) — `GraphicsProfile.Reach`/`HiDef` made real on D3D9, plus a real cross-backend `GraphicsProfile`-propagation bug found and fixed**. New `D3D9ProfileCapabilities.{hpp,cpp}` (`D3DCAPS9` probed via `IDirect3D9::GetDeviceCaps`/`CheckDeviceFormat`/`CheckDeviceType`/`CheckDeviceMultiSampleType`, all pre-device-creation, backend-local under `#ifdef CNA_BACKEND_D3D9`). `IsProfileSupported()`/`QueryRenderTargetFormat()`/`QueryBackBufferFormat()` real; `Texture2D` throws `System::NotSupportedException` past its own profile's size ceiling (2048 Reach/4096 HiDef). Real bug found in SHARED code: `Game`'s `GraphicsDevice_` member is eagerly default-constructed (hardcoded Reach) before `GraphicsDeviceManager` exists, and `applyToExistingBackend()` never wrote a changed profile back onto the live device -- `graphics.GraphicsProfile = HiDef; graphics.ApplyChanges();` had NO path to the real device at all. Fixed with new `GraphicsDevice::SetGraphicsProfileEXT()`. EasyGL's own `CnaTests` (70 cases) regression-checked, all green. New `D3D9_GraphicsProfile` CTest, 10/10, mutation-verified. Full D3D9 CTest suite 13/13 green. |
| `47ca4a15` | **`D9-A5` grown to 31 scenes (`colored_linelist_quad`/`colored_linestrip_quad`) — completes ALL 4 real `PrimitiveType` values, both PIXEL-PERFECT (0/65536 differ) on the first attempt**. `LineList`: two SEPARATE horizontal segments at different Y rows, proving independent segments with nothing connecting them (confirmed on real XNA: RED/GREEN midpoints exact, the row between stays background). `LineStrip`: a 3-vertex "V" polyline, proving 2 CONNECTED segments share the middle vertex (confirmed on real XNA: 307 non-background pixels spanning the full expected extent, both leg midpoints exact RED). New `D3D9_Draw` Check E/F (now 6/6) — real bug found and fixed in the CTest's OWN color-packing, not CNA: `0x00FF00FFu` decodes (byte order R,G,B,A ascending, little-endian literal) to `R=255,G=0,B=255,A=0` — magenta at zero alpha, invisible — not green; fixed to `0xFF00FF00u`. Caught immediately via a full-frame debug scan showing the RED segment rendered exactly as predicted but no GREEN pixels anywhere. Mutation-verified after the fix (hardcoded both `primitiveCount`s to 1, confirmed exactly Check E/F went red, restored). All 31 corpus scenes re-verified pixel-perfect; full D3D9 CTest suite 12/12 green. |
| `426d8af7` | **`D9-A5` grown to 29 scenes (`colored_trianglestrip_quad`) — first scene to ever use `PrimitiveType.TriangleStrip`, PIXEL-PERFECT (0/65536 differ) on the first attempt**. Every earlier scene (and every existing `D3D9_Draw`/`D3D9_DrawEx` check) only ever used `TriangleList`, even though `GraphicsDevice::PrimitiveVerts()`/`ToD3D9Topology()` already handled all 4 `PrimitiveType` values unconditionally -- a real, previously-untested code path, not a new feature (no code changes needed). A 4-vertex colored quad in the canonical "Z" strip order (TL/TR/BL/BR), 4 distinct corner colors so a broken vertex-count<->primitiveCount conversion would show a missing quadrant or wrong Gouraud gradient. New `D3D9_Draw` Check D (now 4/4): an oversized strip quad sampled at the first triangle's own corner AND the second triangle's own corner (only covered if `primitiveCount` genuinely resolved to 2, not 1). Mutation-verified (hardcoded `primitiveCount=1`, confirmed exactly Check D went red, restored, reconfirmed green). All 29 corpus scenes re-verified pixel-perfect; full D3D9 CTest suite 12/12 green. |
| `b557c2bf` | **`D9-A5` grown to 28 scenes (`sprite_multitexture_quad`) — closes D9-90's own explicitly-named multi-texture-batching gap, PIXEL-PERFECT (0/65536 differ) on the first attempt**. 3 non-overlapping sprites, interleaved RED-texture/BLUE-texture/RED-texture (not RED-RED-BLUE, so the second red draw genuinely forces a SECOND rebind after the blue draw's own flush) -- new optional trailing `textureIndex` column on `spritedraw=` lines, reusing the scene format's existing `texture2*` keys rather than inventing new ones. Mutation-verified (disabled the texture-change flush trigger in `D3D9SpriteBatchBackend::Draw()`, confirmed the middle sprite's BLUE leaked into the third position -- `1600/65536` pixels wrong -- then restored and reconfirmed green). New `D3D9_SpriteBatch` Check I (now 10/10). All 28 corpus scenes re-verified pixel-perfect; full D3D9 CTest suite 12/12 green. |
| `df682701` | **Phase D9-9 `D9-93` CLOSED (3 of 5 `SpriteSortMode` values) — found and fixed a real D3D9 backend bug (`BuildMatrixTransformEXT`'s Z-row clipped any nonzero `layerDepth` sprite)**. 3 new `D9-A5` scenes (`sprite_sortmode_deferred_quad`/`sprite_sortmode_backtofront_quad`/`sprite_sortmode_fronttoback_quad`) using 2 overlapping `NonPremultiplied`-blended RED/GREEN sprites at different `layerDepth`s. First scene in the whole corpus to draw with `layerDepth != 0` — surfaced that `CreateOrthographicOffCenter(0,W,H,0,0, zFarPlane=1)` gives `Z'=-layerDepth`, outside D3D9's valid `[0,1]` clip-space Z range, silently clipping the second sprite away entirely regardless of sort mode (root-caused via the real XNA oracle producing correct output while CNA didn't). Fixed with `zFarPlane=-1` (identity Z-row, `Z'=layerDepth`) — only the Z row changes, D9-91's own X/Y half-pixel math is unaffected. All 3 new scenes pixel-perfect against real XNA 4.0 after the fix; 3 new checks added to `D3D9_SpriteBatch` (now 9/9), mutation-verified (reverted the fix, confirmed the sort-mode checks failed, restored, reconfirmed green). `SpriteSortMode.Immediate`/`.Texture` explicitly scoped out (not pixel-observable / needs multi-texture design, respectively) — see `plan_dx9.md` D9-93's own closure note. All 27 corpus scenes re-verified pixel-perfect; full D3D9 CTest suite 12/12 green. |
| `8de8e5b9` | **Phase D9-9 `D9-92` CLOSED — real `TextureAddressMode.Wrap`/`Mirror` for `SpriteBatch`, both oracle-verified with discriminating patterns**. `SetSamplerFilter()`/`SetSamplerAddressMode()` already plumbed through to the real `D9-63` `ApplySamplerState()` path -- the missing piece was purely test coverage, since `Begin()` with no args only exercises `LinearClamp`. New `spritesourcerect`/`spritesampler` scene keys + `Begin(sortMode, blendState, samplerState, null, null)` wiring. 2 new `D9-A5` scenes: `sprite_wrap_quad` (`PointWrap`, sourceRect double the texture width -- tiles RED/GREEN/RED/GREEN across 4 bands) and `sprite_mirror_quad` (manually-constructed Point/Mirror sampler, same geometry -- folds symmetrically to RED/GREEN/GREEN/RED instead, genuinely distinguishable from Wrap). Both patterns predicted before running either side, then confirmed pixel-for-pixel identical. 2 new checks added to `D3D9_SpriteBatch` (now 6/6). All 24 corpus scenes re-verified pixel-perfect; full D3D9 CTest suite 12/12 green. |
| `b889cab1` | **Phase D9-9 `D9-90`/`D9-91` CLOSED — real `D3D9SpriteBatchBackend`, half-pixel offset oracle- AND mutation-verified**. New `D3D9SpriteBatch.{hpp,cpp}`; reuses the existing stride-24 vertex layout and D3D11SpriteBatchBackend's own quad-geometry formula, real `SpriteEffect.fx` bytecode (already compiled/register-mapped by the general D9-71/72 sweep). `MatrixTransform` bakes SpriteBatch's own transform + a D3D9 half-pixel correction (`M41 += -0.5*M11` etc.) into one uniform. 3 new `D9-A5` scenes (`sprite_basic_quad`/`sprite_rotated_quad`/`sprite_flipped_quad`), all pixel-perfect on the first attempt. Real finding: a 1x1-texture scene and quadrant-center CTest sample points are BOTH structurally incapable of detecting the half-pixel offset (it shifts texture CONTENT sampling, not geometric edges) -- caught via mutation-testing, which showed the boundary check staying green with the offset removed while the multi-texel oracle scenes diverged by 4800/65536 pixels; fixed with a dedicated boundary-blend-color CTest check. New `D3D9_SpriteBatch` CTest, 4/4, mutation-verified. `D9-92` (sampler Wrap/Mirror) and `D9-93` (SpriteSortMode sweep) explicitly NOT closed -- honest gaps, not assumed. All 21 corpus scenes re-verified pixel-perfect; full D3D9 CTest suite 12/12 green. |
| `17a1a607` | **`D9-A5` grown to 19 scenes (`skinned_fourbone_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to exercise a REAL 4-bone skinning blend, completing all 3 real `WeightsPerVertex` values**. Extended vertex line format to an optional 16 columns (3rd/4th boneindex/boneweight pair); new `bone2translate`/`bone3translate` scene keys. Four pure-translation bones weighted 0.4/0.3/0.2/0.1 blend to an exact hand-derived `Translate(0.12,0.03,0)` -- a genuine two-axis shift proving all four weighted terms sum correctly. Confirmed at predicted shifted boundaries in both X and Y on both sides. All 19 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `08aba091` | **Documented a genuine, unresolved D3D9 backend crash (render-target-as-texture sampling), reverted all attempted code rather than land it half-working**. See §4's own "New blocker found 2026-07-15" for the full reproduction record and recommended next step (Vulkan validation layers). |
| `83e2edf2` | **`D9-A5` grown to 18 scenes (`alphatest_never_quad`/`alphatest_always_quad`) — also PIXEL-PERFECT (0/65536 differ each), COMPLETE ALL 8 REAL XNA `AlphaTestEffect.AlphaFunction` VALUES IN THE CORPUS**. `Never` sets both branch targets negative (`clip(-1)` unconditionally, discards everything regardless of alpha); `Always` sets both positive (`clip(1)` unconditionally, discards nothing). Both reuse `alphatest_quad.scene`'s exact texture; `Never` renders pure clear color everywhere (even the alpha=255 texels that would normally pass `Greater`), `Always` renders every texel including the alpha=1/64 ones that would normally fail. No code changes needed. `AlphaTestEffect`'s entire compare-function surface is now independently verified: `Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/`Always` on `PSAlphaTestLtGt`, `Equal`/`NotEqual` on `PSAlphaTestEqNe`. All 18 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `dab209af` | **`D9-A5` grown to 16 scenes (`alphatest_greaterequal_quad`/`alphatest_lessequal_quad`) — also PIXEL-PERFECT (0/65536 differ each), exercise `GreaterEqual`/`LessEqual`, sharing `PSAlphaTestLtGt` with `Greater`/`Less` but differing at the EXACT boundary value**. Confirmed against FNA's own `AlphaTestEffect.cs`: `GreaterEqual` sets `X=reference-threshold` (vs `Greater`'s `+threshold`); `LessEqual` sets `X=reference+threshold` (vs `Less`'s `-threshold`) -- a texel exactly at `ReferenceAlpha` passes under the `-Equal` variant, fails under the plain variant. Both reuse `alphatest_equal_quad.scene`'s own texture (which already has an exact-128 texel; `alphatest_quad.scene`'s texture never lands on 128). Predicted patterns confirmed exactly right, then pixel-for-pixel identical. No code changes needed. Completes all 4 alpha-value-dependent `PSAlphaTestLtGt` values. All 16 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `ac7f39cd` | **`D9-A5` grown to 14 scenes (`alphatest_notequal_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to exercise `AlphaFunction=NotEqual`, completing coverage of both real pixel shader buckets' compare-function directions**. Confirmed against FNA's own `AlphaTestEffect.cs` source: `NotEqual` uses the identical `abs(a-x)<y` comparison as `Equal`, only the pass/fail branch targets swap. Reuses `alphatest_equal_quad.scene`'s exact texture/threshold, flips every texel's pass/fail. No code changes needed (`NotEqual` already supported). All 14 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `5bf410fc` | **`D9-A5` grown to 13 scenes (`skinned_twobone_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to exercise a REAL, non-degenerate 2-bone skinning blend**. New `weightspervertex`/`bone1translate` scene keys; vertex line format extended to an optional 12 columns (2nd boneindex/boneweight pair), backward compatible. Real finding: `skinned_quad.scene`'s own comment claimed `WeightsPerVertex=1` but never actually set it -- real XNA defaults to 4, so that scene had actually been running the FourBones bucket, harmless only because its single weight pair leaves the rest 0. Fixed with explicit `weightspervertex=1`. New scene blends Bone0=Identity + Bone1=Translate(0.4,0,0) at 50/50, giving an exact hand-derived Translate(0.2,0,0) -- the whole quad shifts right by 0.2 NDC units, confirmed at the predicted shifted screen boundaries on both sides. All 13 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `1f0738fc` | **`D9-A5` grown to 12 scenes (`envmap_fresnel_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to genuinely exercise `EnvironmentMapEffect.FresnelFactor` with a real per-vertex gradient**. New `fresnelfactor` scene key wired on both sides. Real finding: `envmap_quad.scene`'s own comment wrongly claimed the "non-fresnel bucket" -- real XNA defaults `FresnelFactor=1`, and neither side had ever set it, so that scene had actually been running the fresnel-ENABLED bucket the whole time, undetected because its coplanar-with-eye geometry makes Fresnel enabled/disabled coincide (`viewAngle=0` everywhere -> `pow(1,anything)=1`). Fixed with explicit `fresnelfactor=0`. New scene uses deliberately different per-vertex normals (top vs bottom edge) to escape the origin-centered-quad symmetry trap (any single shared normal gives an identical fresnelFactor at all 4 corners); hand-derived center prediction `≈(129.3,64.6,32.3)` matched the observed `(129,65,32)` exactly on both sides. All 12 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `3263db79` | **`D9-A5` grown to 11 scenes (`alphatest_equal_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to exercise `AlphaFunction=Equal`, a STRUCTURALLY different pixel shader bucket (`PSAlphaTestEqNe`) from `Greater`/`Less`'s shared `PSAlphaTestLtGt`**. Confirmed against FNA's own `AlphaTestEffect.cs` source, including the exact tolerance (`threshold=0.5/255`). 4 texels straddle that boundary: `alpha=128` exact match PASSES, `alpha=127`/`129` (off by `1/255`) both FAIL, `alpha=1` FAILS -- pass/fail pattern predicted before running either side, then confirmed pixel-for-pixel. No code changes needed (`Equal` already supported). All 11 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `7f2bbc7a` | **`D9-A5` grown to 10 scenes (`alphatest_less_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to exercise a SECOND `AlphaTestEffect.AlphaFunction` value (`Less`)**. Reuses `alphatest_quad`'s own texture/threshold, only `AlphaFunction` changes -- flips which texels pass vs. discard, proving the compare function is genuinely honored (not just that `clip()` exists). No code changes needed (`Less` already supported). Real finding: a PNG-encoder quirk, not a rendering bug -- a first draft used `alpha=0` for a passing texel; the shader OUTPUT matched byte-for-byte on both sides, but real XNA's `SaveAsPng` zeroed RGB for that exact-`alpha=0` pixel while CNA preserved it (confirmed `alpha==0`-specific, not general premultiply, since `alpha=64` matched exactly). Fixed by using `alpha=1` instead of `0`. All 10 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `658b4c8c` | **`D9-A5` grown to 9 scenes (`fog_gradient_quad`) — also PIXEL-PERFECT (0/65536 differ), first scene to exercise `IEffectFog`**. Fog wiring added to all 5 effect branches on both sides. Required two false starts before a genuinely correct gradient rendered identically on both sides: (1) `z=0..1`/`FogStart=0`/`FogEnd=1` gave `fogFactor=saturate(-z)`, always clamped to 0 -- uniformly white on both sides, an exact but meaningless match; (2) flipping far vertices to `z=-1` for a "correct" negative view-space Z instead near-plane-clipped the whole quad away on both sides (`Projection` is also `Identity`, so clip-space z is the raw vertex z). Working fix: keep `z=0..1`, use `FogStart=0`/`FogEnd=-1` (negative) so `fogFactor=z` directly -- produced a genuine monotonic white-to-black gradient, confirmed pixel-for-pixel identical on both sides. All 9 scenes re-verified pixel-perfect; full D3D9 CTest suite 11/11 green. |
| `9b8a4e9a` | **`D9-A5` grown to 8 scenes (`multilight_textured_quad`) — also PIXEL-PERFECT (0/65536 differ)**. First scene to genuinely exercise `BasicEffect`'s multi-light SUMMATION formula (`D9-82b`'s own "2-light-sum" `ShaderIndex` bucket, structurally different from the "OneLight" bucket every earlier lit scene uses). Two active lights (diffuse 0.3+0.2, same direction) sum to the exact same dimming `lit_textured_quad`'s own single 0.5 light produces -- exact `(128,128,128,255)`, matching byte-for-byte, proving genuine summation not overwrite. A third light present but disabled (large nonzero diffuse) confirmed NOT to contribute. Extended `light1*`/`light2*` keys, applied uniformly to all 3 lit effects. All 8 scenes re-verified pixel-perfect. |
| `e0afe3a0` | **`D9-A5` grown to 7 scenes (`skinned_quad`) — also PIXEL-PERFECT (0/65536 differ). MILESTONE: all 5 XNA Stock Effects now represented in the corpus, all pixel-perfect.** Added `effect=SkinnedEffect` plus a fourth custom vertex shape (`PositionNormalTextureWeights`, stride 52, `VSInputNmTxWeights`, matches existing layout byte-for-byte). Single Identity bone at 100% weight (skinning is a no-op, matching `D9-82f`'s own CTest simplification) reduces expected math to `lit_textured_quad`'s own formula: exact `(128,128,128,255)` both sides. Same `LightingEnabled` explicit-interface-implementation carve-out found for `SkinnedEffect` too (confirmed against FNA source) -- same quirk as `EnvironmentMapEffect`, not a new bug. Also proactively added `[StructLayout(LayoutKind.Sequential)]` to both the new struct and (retroactively) `VertexPositionDualTexture` on the C# side -- C#'s default "auto" layout doesn't formally guarantee field order, which `DrawUserPrimitives<T>`'s raw-byte marshalling silently depends on. |
| `bf2f467c` | **`D9-A5` grown to 6 scenes (`envmap_quad`) — also PIXEL-PERFECT (0/65536 differ), 3rd non-BasicEffect Stock Effect**. Added `effect=EnvironmentMapEffect` plus `environmentmap*` keys, reusing the existing `PositionNormalTexture` shape. 1x1 base texture + 1x1 all-same-color `TextureCube` + dim light + `EnvironmentMapAmount=0.5`: real `lerp(texture*diffuseSum, environmentMap, environmentMapAmount)` produced exact `(164,114,89,255)` both sides. Real finding (not a bug): real XNA/FNA's `EnvironmentMapEffect` implements `IEffectLights.LightingEnabled` via explicit interface implementation, invisible on the concrete class (confirmed live: `emfx.LightingEnabled=...` is a genuine `CS1061`) -- lighting is always on, no game can disable it. CNA's own `setLightingEnabledProperty` already matches the exact same behavior (getter always true, setter throws given false); only the C++ vs C# visibility differs. Neither side calls it for this effect now. |
| `88dee0c2` | **`D9-A5` grown to 5 scenes (`dualtexture_quad`) — also PIXEL-PERFECT (0/65536 differ), 2nd non-BasicEffect Stock Effect, first scene needing a vertex shape neither side had a built-in type for**. Added `effect=DualTextureEffect` plus `texture2*`/`diffusecolor` keys and a new `vertexformat=PositionDualTexture` (stride 28, `VSInputTx2`) -- real XNA has no dual-UV vertex struct either, so both sides define their own custom `IVertexType`/`VertexDeclaration`. Two 1x1 solid-color textures + `DiffuseColor=(0.5,0.5,0.5)`: the doubling-blend formula's `*2*0.5` cancels out, expected result `(100,60,20,255)` hand-derived before running either side (matching `D9-82d`'s own proven check value), then confirmed pixel-for-pixel. Added as a third `std::unique_ptr` alongside `alphaFx`/`basicFx`, correctly avoiding a repeat of `alphatest_quad`'s own dangling-pointer bug -- no new bug this time. |
| `b0272385` | **`D9-A5` grown to 4 scenes (`alphatest_quad`) — also PIXEL-PERFECT (0/65536 differ), first non-BasicEffect Stock Effect in the corpus**. Added `effect=BasicEffect`/`AlphaTestEffect` plus `alphafunction`/`referencealpha` keys. A 2x2 texture straddling `ReferenceAlpha=128` exercises `clip()`'s real discard end to end. Real bug found and fixed live in `CnaOracleRender.cpp`: a dangling-pointer bug (not a backend bug) -- the newly-constructed Effect object was scoped inside an `if`/`else` block and destroyed before the shared `DrawUserPrimitives()` call read `GraphicsDevice::currentEffect_` (a raw pointer `Effect::Apply()` sets), causing `textured_quad`/`lit_textured_quad` to spuriously fail with garbage flags; `colored3d` passed only by allocation-timing luck. Fixed via `std::unique_ptr` at `Draw()`'s own top level; re-verified all 4 scenes pixel-perfect afterward. |
| `b4252bfa` | **`D9-A5` grown to 3 scenes (`lit_textured_quad`) — also PIXEL-PERFECT (0/65536 differ)**. Extended the shared scene format to a third vertex shape (`vertexformat=PositionNormalTexture`, `VSInputNmTx`'s stride-32 shape) plus `ambientcolor`/`light0enabled`/`light0diffuse`/`light0direction` keys wired to `BasicEffect.AmbientLightColor`/`DirectionalLight0` on both sides. Deliberately dimmed the light (`diffuse=0.5`, no ambient) rather than a bright one that would saturate to full intensity and prove nothing about whether lighting math is genuinely applied -- the dimmed version visibly halves the texture color and still matches real XNA exactly. First evidence this backend's lit+textured `BasicEffect` dispatch is genuinely indistinguishable from real XNA 4.0, not just hand-verified pixel math. |
| `fd8df277` | **`D9-A5` grown to 2 scenes (`textured_quad`) — also PIXEL-PERFECT (0/65536 differ)**. Extended the shared scene format to a second vertex shape (`vertexformat=PositionColor`/`PositionTexture`) and inline procedural texture data (`texturewidth`/`textureheight`/`texturefilter`/`texturepixel`, no content-pipeline asset needed) on both `Oracle.cs` and `CnaOracleRender.cpp`. Exact match confirmed including the UV=(0.5,0.5) point-filter texel-boundary pixel. Real bug found and fixed in `Oracle.cs` itself: `ParseBool` used a C# 6 expression-bodied member the real .NET-4.0-era `csc.exe` rejects outright (`CS1002`/`CS1519`) -- meaning `D9-A3`'s own original "pixel-perfect" claim had never actually been verified against the rewritten, scene-driven `Oracle.cs` (only the old hardcoded spike); fixed, recompiled, re-ran `colored3d` and reconfirmed 0/65536. |
| `848e56b2` | **`D9-A3`/`D9-A4` closed (XNA oracle diff harness) — first real oracle comparison is PIXEL-PERFECT (0/65536 differ)**. New shared `.scene` text format (`tools/xna-oracle/scenes/*.scene`), a rewritten scene-driven `tools/xna-oracle/Oracle.cs` (moved from `dx9-spike/`), a new `tools/xna-oracle/CnaOracleRender.cpp` (real public `Game`/`GraphicsDeviceManager`/`BasicEffect` API, `cna_oracle_render` CMake target), and `scripts/xna-diff.py` (needs Pillow, `--tolerance` defaults to 0, mutation-verified). Installed DXVK into the XNA oracle's own Wine prefix (`~/.wine-cna-xna40`) -- `D9-A4`'s own critical prerequisite -- confirmed via the adapter string flipping from WineD3D's spoofed string to the real GPU. `colored3d` scene (`D9-A2`'s own original triangle) matches real XNA 4.0 byte-for-byte across all 65536 pixels. `D9-A5`'s corpus now has its first scene, growing incrementally from here. |
| `6fd21fa3` | **`D9-64` closed (reuse backend-agnostic state CTests) — Phase D9-6 now FULLY CLOSED (all 5 rows)**. Reused D3D11's own 4-test subset (`easygl_blendstate_opaque_test.cpp`/`easygl_blendstate_alphablend_test.cpp`/`easygl_depthstencilstate_stencil_enable_test.cpp`/`easygl_rasterizerstate_cullmode_test.cpp`, verbatim) as new `D3D9_BlendState_Opaque`/`D3D9_BlendState_AlphaBlend`/`D3D9_DepthStencilState_StencilEnable`/`D3D9_RasterizerState_CullMode` CTests. Found and fixed 2 real, pre-existing backend bugs: (1) `SetDepthTestEnabled`/`SetDepthWriteEnabled` were silent-throw stubs since `D9-11`, never wired up -- same class of bug as D3D11's own 2026-07-14 fix (`191c28f1`), now direct `SetRenderState(D3DRS_ZENABLE/ZWRITEENABLE)` calls (`SetBlendEnabled` -> deliberate no-op, matching D3D11/D3D12); (2) `UpdatePresentationFormatEXT()` deferred a changed `DepthStencilFormat` until the next `Present()`, causing `Clear()` to fail `D3DERR_INVALIDCALL` on any test drawing depth/stencil content on the literal first frame -- fixed by applying eagerly inside `UpdatePresentationFormatEXT()` itself (within the interface's own documented allowance, no `IGraphicsBackend.hpp` change). New `D3D9_Smoke` Check Z (2 checks, ported from D3D11's own near/far depth-test proof) confirms fix 1 for real. Both mutation-verified. Full D3D9 CTest suite: 11/11 binaries green (`D3D9_Smoke` now 55/55). |
| `90f59e7c` | **`D9-83` closed (`DrawInstancedPrimitivesEx` via `SetStreamSourceFreq`) — Phase D9-8's dispatch+instancing work is now COMPLETE**, only `D9-84` remains. New `D3D9InstancedDraw.cpp`, a fresh NOXNA `vs_2_0`/`ps_2_0` shader (`shaders/cna/Instanced3D.hlsl`, real XNA has no per-instance-aware Stock Effect shader) compiled+disassembly-verified, new stride-64 2-stream vertex declaration. `SetStreamSourceFreq(0/1, INDEXEDDATA\|count / INSTANCEDATA\|1)` per MSDN, reset to 1 before returning. Real bug found and fixed during development was in the new CTest's own pixel-sample coordinates (sat exactly on the test triangle's diagonal hypotenuse), not the instancing logic -- every D3D9 API call returned `S_OK` throughout. New `D3D9_Instanced` CTest, 4/4 (two distinct instances in one draw call, null-instanceVb fallback, stream-frequency reset regression check). Mutation-verified (hardcoded the instance-count frequency to 1; exactly the 2nd-instance check went red). Full 7-CTest D3D9 suite passes. |
| `d945ec59` | **`D9-82f` closed (`SkinnedEffect` dispatch) — Phase D9-8's dispatch work is now COMPLETE for all 5 XNA Stock Effects**. This row's own "12 unblocked" estimate was exactly right, same as `D9-82e`'s. New `DrawSkinnedEffectEXT()` + `UploadBonesVS()`. `VSInputNmTxWeights` matches the existing stride-52 layout byte-for-byte. `preferPerPixelLighting` always `false` makes the pixel-lighting `ShaderIndex` bucket structurally unreachable. `Bones[72]` (216 registers, 3/bone) reuses the exact same "first 3 columns of the transposed matrix" packing `UploadMatrixConstantVS` already established for `World`/`WorldInverseTranspose`. `D3D9_DrawEx` extended to 17/17 (2 new real checks, Identity-bone skinning-as-no-op design so the expected math reuses the established lit-textured formulas while still exercising the full `Bones[72]` upload path). Mutation-verified (commented out the entire `UploadBonesVS()` call; both new checks went red -- a zero skinning matrix degenerates the triangle to a point -- everything else stayed green). Full 6-CTest D3D9 suite passes. |
| `da8504c6` | **`D9-82e` closed (`EnvironmentMapEffect` dispatch)** — this row's own "8 unblocked" estimate was exactly right this time. New `DrawEnvironmentMapEffectEXT()`; `specularEnabled` always `false` makes the 8 specular `ShaderIndex` values structurally unreachable (no separate throw branch needed). `VSInputNmTx` matches the existing stride-32 layout exactly -- no new vertex declaration needed (unlike `D9-82d`). Factored `oneLight` derivation out of `DrawBasicEffectEXT()` into a shared `ComputeOneLightEXT()`. `EmissiveColor` needs no reconstruction here (`FillGpuDrawParams()` already pre-folds it). `D3D9_DrawEx` extended to 15/15 (2 new real checks mirroring `BasicEffect`'s own Check C/D bucket-selection discipline, non-fresnel only for exactness). Mutation-verified (forced the shared `ComputeOneLightEXT()` to always `true`; BOTH `BasicEffect`'s AND `EnvironmentMapEffect`'s own 2-light checks went red simultaneously, confirming the shared helper is genuinely shared). Full 6-CTest D3D9 suite passes. |
| `d7fd2187` | **`D9-82d` closed (`DualTextureEffect` dispatch)** — this row's own original "4 ShaderIndex values, all unblocked" claim was wrong: only 2 of 4 are actually drawable. new `DrawDualTextureEffectEXT()`. Real finding: `VSInputTx2` needs a genuine 2-UV-set vertex (28 bytes) with no equivalent among the 5 shared layouts (D3D11's own reimplementation sidesteps this with a single shared UV set, not an option here since this backend must draw the real unmodified compiled shader) — resolved with a new, D3D9-only stride-28 vertex declaration (safe, backend-local, doesn't touch `GpuDrawParams`/other backends). `D3D9_Common` now 29/29. `D3D9_DrawEx` extended to 13/13 (1 new real check: the doubling-blend formula, exact `(100,60,20,255)`). Mutation-verified (skipped `DiffuseColor` upload, confirmed only the new check went red). Full 6-CTest D3D9 suite passes. |
| `55e1cd61` | **`D9-82c` closed (`AlphaTestEffect` dispatch)**: new `DrawAlphaTestEffectEXT()`, all 8 `ShaderIndex` values real, no vertex-layout gap this time (`AlphaTestEffect`'s two `VSInput` shapes map 1:1 onto the existing strides). `GpuDrawParams::alphaTest` uploads verbatim -- already exactly the real register layout, no reconstruction needed (unlike `BasicEffect`'s `EmissiveColor`). Factored `ComputeFogVectorEXT()` out into a helper shared with `D9-82b`. `D3D9_DrawEx` extended to 12/12 (3 new real checks: `Less` passes, `Less` fails/discarded, `Equal` passes on the vertex-color bucket). Mutation-verified (forced `isEqNe=false`, confirmed only the `Equal`-bucket check went red). Full 6-CTest D3D9 suite passes. |
| `5e502529` | **`D9-82b` closed (`DrawPrimitivesEx` entry point + `BasicEffect` dispatch)**: new `D3D9EffectDraw.cpp`; new "soft" `TryUpload*ShaderConstantEXT` helpers. Real, honest finding: only 10 of `BasicEffect`'s 32 `ShaderIndex` values are actually drawable (no CNA vertex layout for the rest) — narrower than this row's original 24-value estimate, documented not hidden. Corrected `D9-81`'s `oneLight` finding (the original "read `SkinnedEffect.cpp` directly" text wasn't actually reachable from `GpuDrawParams`-only input; the real fix is a provably-lossless derivation from existing `GpuDrawParams` fields). New `D3D9_DrawEx` CTest, 10/10, every expected pixel hand-computed from `BasicEffect.fx`/`Lighting.fxh`'s own formulas. Mutation-verified (forced `oneLight=true`, confirmed only the bucket-sensitive check went red). Full 6-CTest D3D9 suite passes. |
| `031e33a5` | **`D9-82` closed (first real 3D triangle) — split from its original scope into `D9-82`/`D9-82b`**: new `D3D9ConstantUpload` (name-keyed register lookup + `Set{Vertex,Pixel}ShaderConstantF`); real `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (stride-16, `BasicEffect` `ShaderIndex 3` only, chaining `D9-80`'s dispatch into `D9-74`'s shader cache); new stride-keyed vertex-declaration cache. Found and fixed a second real trap live (not the predicted `D3DCULL` one): `D9-22`'s `D3DDECLTYPE_D3DCOLOR` for `COLOR0` silently swapped R/B against XNA's own R,G,B,A `Color.PackedValue` layout (confirmed: fed red, read back blue, before the fix) — switched to `D3DDECLTYPE_UBYTE4N` (no reorder), confirmed exact red after. `D3D9_Common`'s stride-16/24 assertions updated to match. New `D3D9_Draw` CTest, 3/3 (paint non-indexed, paint indexed, real `WorldViewProj`-upload proof). Mutation-verified (corrupted `DiffuseColor`, confirmed only the mutated check went red). Full 5-CTest D3D9 suite passes. |
| `e5aa797b` | **`D9-80`/`D9-81` closed (XNA shader dispatch + audit)**: new `D3D9ShaderDispatch` — `Compute<Effect>ShaderIndex()`/`Get<Effect>{Vertex,Pixel}ShaderNameEXT()` for all 5 stock effects, transcribed from FNA's `.cs` sources + the vendored `.fx` files' own tables. `D9-81`'s audit independently re-verified against current source (forked agent): all 4 `GpuDrawParams` gaps still real, but `oneLight`/`isEqNe` turn out resolvable from CNA's own existing internal state with no `GpuDrawParams` change (only `PreferPerPixelLighting`/`specularEnabled` remain genuine cross-cutting blockers). New `D3D9_ShaderDispatch` CTest, 23 checks. Mutation-testing found a real gap in the test's own first draft (a prefix-only sweep missed a corrupted table entry); rewrote as an exact-match sweep against an independently-typed expected array, re-confirmed the mutation is now caught. Full 4-CTest D3D9 suite passes. |
| `678bc3be` | **`D9-74` closed (`D3D9ShaderCache`) — Phase D9-7 FULLY CLOSED** (`D9-73` honestly 🟨): installed DXVK into `~/.wine-cna-d3d9-spike` (now has both the real compiler and a live device); new `D3D9ShaderCache` + `Shaders::kAllShaders[]` manifest (regenerated, not hand-typed) + new `D3D9_ShaderCache` CTest (4 checks: all 66 create live, caching works, unknown names throw, stage-aware lookup). Mutation-verified (skipped one shader in `CreateAllEXT()`, confirmed the count-dependent checks went red). Full 3-CTest D3D9 suite passes. |
| `7ecd2d42` | **`D9-72` closed (transcribe register layout)**: real, empirical finding (`EnvironmentMapEffect.fx`'s `VSEnvMap` allocates `World` only 3 registers, not the naively-assumed 4, since that entry point never reads `pos_ws.w`) invalidated the plan's own original hand-derive-from-source approach. New `extract_shader_registers.py` compiles+disassembles all 66 shaders via a new `disasm_tool.cpp`, parsing the compiler's own `// Registers:` comment block for the real, per-entry-point ground truth. Output `D3D9ShaderRegisters.hpp` (627 lines), compiles clean, spot-checked against 3 independently-verified cases. |
| `dddeecbc` | **`D9-71` closed (compile all 66 entry points)**: new `compile_shaders_sm2.py` parses the entry-point list from the vendored `.fx` files' own `compile` statements (not hand-maintained), cross-builds the moved-in `fxc_tool.cpp` with MinGW-w64, compiles via a bare `wine` call against `~/.wine-cna-d3d9-spike`. Real run: 66/66 compiled, 0 failures, into a checked-in `d3d9_shaders.hpp` (381 KB) confirmed to compile clean as real C++ and to regenerate byte-identically on a second run. Bonus: re-ran `compare_against_fxb.py` against the real header's own bytecode — 61/66 exact matches, same 5 divergent `PixelLighting` variants the Phase D9-0 spike already found. `fxc_tool.cpp`/`compare_against_fxb.py` fully moved out of `dx9-spike/` into their real home. |
| `64de9d29` | **`D9-70` closed (vendor Stock Effects HLSL)**: all 10 files copied byte-for-byte from the FNA tree into `src/CNA/Internal/Backends/D3D9/shaders/xna/`, plus `LICENSE`, a provenance `README.md` (66 entry points, grep-verified), and a specific `THIRD_PARTY_NOTICES.md` entry. New `scripts/verify-d3d9-stock-effects-vendored.sh` mechanically diffs against the FNA tree. Mutation-verified (appended a line to the vendored `BasicEffect.fx`, confirmed the script reports `MISMATCH`/exit 1). First row of Phase D9-7. |
| `eb373571` | **`D9-63` closed (`ApplySamplerState`) — Phase D9-6 down to just `D9-64`**: plain `SetSamplerState()` calls (design decision 11), using the `D9-21` mapping tables; slot bound-checked against real `D3DCAPS9::MaxSimultaneousTextures`, not a hardcoded 16. `D3DSAMP_SRGBTEXTURE` genuinely out of scope (interface signature carries no sRGB parameter, same category as `D9-60`'s own `D3DRS_COLORWRITEENABLE` gap). New `D3D9_Smoke` Check Y (2 checks): values read back via `GetSamplerState()` (no draw needed) confirm an exact match; out-of-range slot silently no-ops. Mutation-verified (hardcoded `D3DSAMP_ADDRESSU` to ignore the requested value, confirmed exactly that assertion went red). `D3D9_Smoke` now 53/53. |
| `1206fc42` | **`D9-56` closed (NPOT capability) — Phase D9-5 FULLY CLOSED (all 7 rows)**: new NOXNA `RequiresPowerOfTwoTexturesEXT()`/`NonPowerOfTwoRequiresClampAddressingEXT()` surface the real `D3DCAPS9::TextureCaps` `POW2`/`NONPOW2CONDITIONAL` bits. This dev environment's DXVK device reports full, unconditional NPOT support, matching `D9-3`'s own original caps dump. New `D3D9_Smoke` Check X (2 checks): asserts the exact reported capability, then round-trips a genuinely non-power-of-two (5×3) texture for real. Enforcing the `Reach`-profile "no Wrap on NPOT" restriction itself is deferred to `D9-10`/`D9-82` (no draw/sampler path exists yet). Mutation-verified (hardcoded the POW2 helper to always return true, confirmed exactly that assertion went red). `D3D9_Smoke` now 51/51. |
| `f33d4fe9` | **`D9-55` closed (occlusion queries)**: new `D3D9OcclusionQueryBackend` (`IDirect3DQuery9`, `D3DQUERYTYPE_OCCLUSION`), gated on the official D3D9 support-probe idiom (`CreateQuery(type, nullptr)`). New `D3D9_Smoke` Check W (3 checks). Mutation-verified (forced `IsComplete()` to always return false, confirmed exactly that assertion went red). `D3D9_Smoke` now 49/49. `D9-56` (NPOT) is the only Phase D9-5 row left open. |
| `9c8ccfe9` | **`D9-54` closed (MRT)**: real `D3D9GraphicsBackend::SetRenderTargets(rts, count)` (`SetRenderTarget(i, surface)` per slot, unused slots disabled, over-request throws per design decision 13, deliberately not matching D3D11/D3D12's own silent-clamp precedent). Real, unplanned finding: an MRT bind isn't representable by the single-pointer `currentCustomRT_`/`currentCustomCubeRT_` tracking, so unbinding via `SetRenderTargets(nullptr, 0)` silently failed to restore the back buffer — fixed by making `RestoreBackBufferRenderTargetEXT()` unconditional in `SetRenderTarget2D()`'s/`SetRenderTargetCubeFace()`'s own `!rt` branches. New `D3D9_Smoke` Check V (3 checks). Mutation-verified (disabled the over-request guard, exactly that assertion went red). `D3D9_Smoke` now 46/46. `D9-55`–`56` (occlusion/NPOT) remain open. |
| `9b309cc5` | **`D9-53` closed**: new `D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend` (`D3DUSAGE_RENDERTARGET`, `D3DPOOL_DEFAULT`, registered with the `D9-40` device-lost registry; real MSAA via `CheckDeviceMultiSampleType`, resolved via `StretchRect` on unbind). Three real, unplanned findings fixed: `EnsureDeviceSize()`'s resize path never released `D3DPOOL_DEFAULT` resources before `Reset()` (only the device-lost path did); a cached depth-stencil-surface `ComPtr` is itself an app-held reference to a losable resource and must be released before every `Reset()` too (DXVK's own "still has alive losable resources" diagnostic caught this immediately); `IGraphicsBackend::SetRenderTargetCubeFace()`'s inherited default never actually unbinds a cube target for real, fixed with an explicit override + a second `currentCustomCubeRT_` field. New `D3D9_Smoke` Checks S/T/U (6 checks): 2D/cube/MSAA render targets, each create/bind/Clear/readback (via `GetRenderTargetData()`)/unbind-restores-back-buffer. Mutation-verified (dropped the MSAA resolve `StretchRect` call, exactly Check U's assertion went red). `D3D9_Smoke` now 43/43. `D9-54`–`56` (MRT/occlusion/NPOT) remain open. |
| `bfadcb0e` | **Phase D9-5 partially closed** (`D9-50`/`D9-51`/`D9-52`): new `D3D9TextureBackend`/`D3D9TextureCubeBackend`/`D3D9Texture3DBackend` (`D3DFMT_A8B8G8R8`, `D3DPOOL_MANAGED`). Found empirically that `D9-52`'s own premise only half-applies: `ITextureBackend` (2D) has no `GetData()` at all — `Texture2D::GetData()` is CPU-shadow-based, same architecture as D3D11 — while `ITextureCubeBackend`/`ITexture3DBackend` genuinely delegate `GetData()` to the backend, and those ARE real `LockRect`/`LockBox` reads, exactly as `D9-4`'s spike predicted (no staging/`SYSTEMMEM` fallback needed). Volume/cube-map creation gated on real `D3DCAPS9` (`MaxVolumeExtent`/`D3DPTEXTURECAPS_CUBEMAP`), not assumed. New `D3D9_Smoke` Checks Q/R (6 checks): exact-byte round-trips via direct locks on the `D3DPOOL_MANAGED` resources (no staging texture needed). Mutation-verified: corrupting the 2D upload's source-row offset turned exactly Check Q's first assertion red, nothing else; reverted, reconfirmed 36/36 green. `D9-53`–`56` (render targets/MRT/occlusion/NPOT) remain open. |
| `3e855b2d` | **Phase D9-4 fully closed** (`D9-40`/`D9-41`/`D9-42`): real `D3D9VertexBufferBackend`/`D3D9IndexBufferBackend` (16-bit and 32-bit, `CreateIndexBuffer32()` explicitly overridden), `Lock`/`Unlock` with `SetDataOptions` → `D3DLOCK_DISCARD`/`NOOVERWRITE`. Real finding: `D3DUSAGE_DYNAMIC` requires `D3DPOOL_DEFAULT` (forbidden with `POOL_MANAGED`), so these buffers do NOT survive `Reset()` automatically — new `ID3D9DefaultPoolResourceEXT` registry lets `D9-34`'s recovery path release them before `Reset()`, each recreating lazily on next use (real XNA/D3D9 behavior). Mutation-verified (`CreateIndexBuffer32()` temporarily broken to build a 16-bit buffer, caught immediately via a real uncaught exception, reverted). Also avoided the "pointer-inequality isn't sound recreation proof" false-negative this project's own D3D12 work already found once. `D3D9_Smoke` now 30/30. |
| `cbd75a0b` | **Phase D9-3 fully closed — `D9-34` (device-lost lifecycle)**: `Present()` detects real `D3DERR_DEVICELOST`, fires `DeviceLost`; while lost, polls `TestCooperativeLevel()` until `D3DERR_DEVICENOTRESET`, then fires `DeviceResetting`, calls a real `Reset()`, restores the viewport, fires `DeviceReset`. `Clear`/all `Clear*` combos/`ReadBackbuffer` now throw the real XNA `DeviceLostException` while lost. Exercised deterministically (DXVK rarely loses the device naturally) via the pre-existing `DebugSimulateContextLoss()`/`DebugRestoreContext()` test channel — new `D3D9_Smoke` Check M (8 checks): real event counts/order, a real `Reset()` during recovery, and the device genuinely working again afterward. Also fixed a separate pre-existing gap: `GraphicsDevice::getGraphicsDeviceStatusProperty()` was hardcoded to `Normal` always; now tracks the real backend-reported state. `D3D9_Smoke` now 24/24. Verified no regression on EasyGL/CnaTests. |
| `70e81079` | **`D9-32` closed (shader-model floor) + `D9-33`'s dedicated resize test (Check L)**: `GraphicsProfile::HiDef` now checked against the real `D3DCAPS9` at construction, throwing the real XNA `NoSuitableGraphicsDeviceException` if below `vs_3_0`/`ps_3_0` (only the positive path provable on this real, already-SM3-capable GPU); a new `D3D9_Smoke` Check L resizes 64×64→96×80 via the real `GraphicsDeviceManager` path and confirms the viewport, a post-resize pixel readback at both the origin and the new far edge, and `PresentationParameters` all reflect the new size. `D3D9_Smoke` now 17/17. |
| `50954798` | **`D9-30`/`D9-31` closed + `D9-33`'s resize mechanism + Phase D9-6's `D9-60`/`D9-61`/`D9-62` forced in early**: real `Direct3DCreate9`/`CreateDevice` using the game's actual requested back-buffer/depth-stencil format (the approved `GraphicsBackendCreateArgs` extension, finally consumed for real); all 6 `Clear*` combos + `Present` + `ReadBackbuffer` pixel-verified (`D3D9_Smoke` 12/12); a real `EnsureDeviceSize()` resize-via-`Reset()` mechanism (proven working, not theoretical — it's what makes the smoke test converge to the requested 64×64 size at all). Two real, unplanned findings fixed in place: DXVK genuinely rejects `SurfaceFormat::Color`'s own `D3DFMT_A8B8G8R8` as a *swap-chain* format (a real D3D9 display-format restriction, fixed with a back-buffer-specific substitution to `A8R8G8B8`); and `GraphicsDevice::Reset()` never forwarded updated presentation settings to an already-constructed backend, fixed with one more small additive `IGraphicsBackend` method (`UpdatePresentationFormatEXT`, same category as the already-approved extension). Separately, `GraphicsDevice`'s own constructor turned out to unconditionally push `BlendState`/`DepthStencilState`/`RasterizerState`/viewport defaults, forcing `D9-60`/`D9-61`/`D9-62` in immediately (real `D3DRS_*` `SetRenderState()` sequences) — no device could otherwise finish constructing. Also found 4 more silently-empty `IGraphicsBackend` virtuals `D9-11`'s own grep missed (multi-line `{}` defaults). Verified no regression on EasyGL (34 gtest+CTest checks, including 5 resize/reset-specific ones). |
| `bf26d7d1` | **Phase D9-2 fully closed** (`D9-20`–`D9-23`): new `D3D9FormatMapping`/`D3D9StateMapping`/`D3D9VertexDeclarations` + a 28-check `D3D9_Common` CTest, mutation-verified. Two non-obvious, easy-to-get-backwards findings, both verified against Microsoft's own published D3D9→DXGI legacy-format table rather than assumed: `SurfaceFormat::Color` → `D3DFMT_A8B8G8R8` (not the superficially-obvious `A8R8G8B8`), and `Rgba1010102` → `D3DFMT_A2B10G10R10` (not `A2R10G10B10`, which has no real DXGI equivalent at all). `TextureFilter` needed a new `{min,mag,mip}` triple struct, not a single enum, since D3D9 has no composed filter value. One row (`D9-21`) is 🟨: the mapping table is done, but its own "pixel-test `D3DCULL` against the oracle" obligation is honestly deferred to `D9-84` (no draw path exists yet to test it with). |
| `1a3ca71f` | **Phase D9-1 fully closed** (`D9-10`/`D9-11`/`D9-12`): D3D9 wired into `CMakeLists.txt` (6 of 7 `"D3D12"` sites, correcting a stale plan claim about the 7th — see `plan_dx9.md`'s `D9-10` row); new `D3D9GraphicsBackend` skeleton + shared `NotYetImplemented.hpp`; `GraphicsDevice.cpp` audited, zero changes needed. `CNA_GRAPHICS_BACKEND=D3D9` configures and builds clean; a runtime check confirms the skeleton's real bookkeeping methods work and its throwing methods actually throw. |
| `09121309` | **Phase D9-0 fully closed** (`D9-2`–`D9-5`): confirmed `d3d9`-alone link set (no `dxguid`); a real Wine+DXVK D3D9 device/swap-chain/`Clear`/`Present`/`GetRenderTargetData`/`LockRect` round-trip with an exact pixel match plus a full `D3DCAPS9` dump (`vs_3_0`/`ps_3_0`, `NumSimultaneousRTs=4`, 16384 max texture size, DXVK reports unconditional NPOT support — flagged as provisional/synthetic, not an authentic XNA-era driver's caps); confirmed `D3DPOOL_MANAGED` textures are genuinely `LockRect`-readable and survive `Reset()` with no re-upload (so `Texture2D::GetData()` can be a plain `LockRect` later, `D9-52`); and a new `scripts/run-wine-dxvk9.sh` (mirrors `run-wine-dxvk.sh`'s DXVK-marker gate under new `CNA_D3D9_*` env-var names), proven both ways — passes against the real `~/.wine-cna-d3d11` DXVK prefix, and correctly fails (exit 3) against a freshly-initialized, DXVK-less prefix that silently fell back to WineD3D. |
| `59a35d4c` | Recorded the project owner's two 2026-07-14 decisions in `plan_dx9.md`: implementation authorized through Phase D9-13, and the `IGraphicsBackend` boundary problem resolved via an approved additive extension. |
| `d1ae928f` | Added `plan_dx9.md` and the proven Phase D9-0 spike artifacts (`dx9-spike/`: shader compiler, `.fxb` bytecode oracle, real XNA 4.0 reference renderer) to the `feature/dx9` worktree. |
| many, see `plan_graphics.md` (2026-07-16) | **`plan_graphics.md` Phase 78 (HLSL→GLSL sample shader conversion, DEFERRED.md #11) fully closed — unrelated to the D3D work below, see this file's own top banner.** Task 945 decided (manual line-by-line porting). Task 947 went 0→**13/13**: `NetRumble` (`Clouds.fx` + the bloom trio), `PerPixelLighting`/`VertexLighting` (5 effect/technique combinations), `DistortionSample` (`Distort.fx` + `Distorters.fx`, 5 techniques), `NonPhotoRealistic` (`CartoonEffect.Fx` + `PostprocessEffect.Fx`, 8 techniques), `ShadowMapping`, `NormalMapping`, `BillboardSample`, `ShatterEffect`, `Particles3D`/`XmlParticles`, `ShipGame` (4 distinct shaders: `AnimSprite.fx`/`Blur.fx`/`NormalMapping.fx`/`Particle.fx`, incl. real GPU point sprites), `InstancedModel` (`InstancedModel.fx`, incl. real GPU hardware instancing). 4 new EasyGL-only backend capabilities landed along the way as their own tasks: **1079** (`ShaderEffect` into the 3D draw path), **1080** (custom vertex layouts for that path), **1081** (`TextureCube` sampling for custom shaders), **1082** (real GPU hardware instancing via `glVertexAttribDivisor`). `ctest -R "EasyGL_"` grew from ~190 to **231/233** across the whole campaign, same 2 pre-existing unrelated failures throughout, every task individually mutation-tested and committed separately. Full chronological detail (exact expected pixel values, discriminating-power mutation testing per shader) is in `plan_graphics.md`'s own Task 947/1079–1082 rows, not duplicated here. `plan_samples.md` updated per-sample (13 rows now say "No longer CNA-blocked"). **Not done**: the actual sample ports themselves in `../cna-samples` — out of `cna_graphics` scope. |

---

## 4. Current blocker / main problem

**No blocker.** Phases D9-0/D9-1/D9-2/D9-3/D9-4/D9-5/D9-6/D9-7 are all fully closed (D9-32/D9-34/
D9-60/D9-62/D9-73 honestly 🟨 — see their own plan rows for exactly what's deferred and why). Phase
D9-6's last open row, `D9-64` (reused backend-agnostic state CTests), closed 2026-07-15 and
surfaced two real, pre-existing D3D9 bugs along the way (`SetDepthTestEnabled`/
`SetDepthWriteEnabled` silent-throw stubs; `UpdatePresentationFormatEXT()`'s deferred-format-apply
timing) — both fixed and mutation-verified, see Phase D9-6's own section above.

**Phase D9-A: `D9-A3`/`D9-A4` closed 2026-07-15 — the XNA oracle diff harness is real and all
results landed so far are pixel-perfect** (`colored3d`, `textured_quad`, `lit_textured_quad`,
`alphatest_quad`, `alphatest_less_quad`, `alphatest_equal_quad`, `alphatest_notequal_quad`,
`alphatest_greaterequal_quad`, `alphatest_lessequal_quad`, `alphatest_never_quad`,
`alphatest_always_quad`, `dualtexture_quad`, `envmap_quad`, `envmap_fresnel_quad`, `skinned_quad`,
`skinned_twobone_quad`, `skinned_fourbone_quad`, `multilight_textured_quad`, `fog_gradient_quad`,
`sprite_basic_quad`, `sprite_rotated_quad`, `sprite_flipped_quad`, `sprite_wrap_quad`,
`sprite_mirror_quad`, `sprite_sortmode_deferred_quad`, `sprite_sortmode_backtofront_quad`,
`sprite_sortmode_fronttoback_quad`, `sprite_multitexture_quad`, `colored_trianglestrip_quad`,
`colored_linelist_quad`, `colored_linestrip_quad` — `0/65536` pixels differ from real XNA 4.0
each, see Phase D9-A's own section above). `D9-A5` (the scene corpus) has 31 entries and now
represents **all 5 XNA Stock Effects** (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect`, `SkinnedEffect`) including `BasicEffect`'s own multi-light summation
bucket, `IEffectFog` (shared by all 5 effects), **ALL 8** `AlphaTestEffect.AlphaFunction` values
covering both real pixel shader buckets (`Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/
`Always` on `PSAlphaTestLtGt`, `Equal`/`NotEqual` on `PSAlphaTestEqNe` — `AlphaTestEffect`
compare-function coverage is now COMPLETE), `EnvironmentMapEffect.FresnelFactor` with a genuine
per-vertex gradient, **ALL 3** `SkinnedEffect.WeightsPerVertex` values (`1`/`2`/`4` —
`SkinnedEffect` weighting coverage is now COMPLETE), `SpriteBatch`'s core draw path, sampler
address modes, 3 of 5 `SpriteSortMode` values, and multi-texture `FlushBatch()`-on-texture-change
batching (`D9-90`/`D9-91`/`D9-92`/`D9-93` all now CLOSED), and **ALL 4** `PrimitiveType` values
(`TriangleList`/`TriangleStrip`/`LineList`/`LineStrip` — `PrimitiveType` coverage is now
COMPLETE), every comparison pixel-perfect; `D9-84` (every draw path validated against the oracle)
can now genuinely continue, one scene at a time.

**Phase D9-9: `D9-90`–`D9-93` all closed 2026-07-15 — `D3D9SpriteBatchBackend` is real, the
half-pixel offset is both oracle- and mutation-verified, `Wrap`/`Mirror` addressing are both
oracle-verified with genuinely distinguishing patterns, and 3 of 5 `SpriteSortMode` values are
oracle-verified with a real backend bug found and fixed along the way.** See Phase D9-9's own
section above for the full record, including a real finding about why the FIRST mutation-test
attempt for `D9-91` (boundary check alone) would have been a false-positive "closed" claim, and
`D9-93`'s own Z-clipping bug (`BuildMatrixTransformEXT`'s `zFarPlane=1` silently clipped away any
sprite with `layerDepth > 0`, fixed with `zFarPlane=-1`). Phase D9-9 has no open rows left;
`SpriteSortMode.Immediate`/`.Texture` remain explicitly out of scope, not silently assumed.

**New blocker found 2026-07-15, NOT fixed, reverted to keep the tree clean — a real D3D9 backend
crash when a `D3DUSAGE_RENDERTARGET`-flagged `RenderTarget2D` texture exists in-process alongside
any subsequent draw call.** While attempting to add the corpus's first render-target oracle scene
(`rendertarget_texture_quad.scene` — create a `RenderTarget2D`, `SetRenderTarget`/`Clear`/unbind
it, then use it as a `BasicEffect` texture for an ordinary textured quad), `cna_oracle_render.exe`
crashes with `terminate called after throwing an instance of 'dxvk::DxvkError'` — an UNCAUGHT
exception (this file's own `main()` already has a `catch (const std::exception&)` around the
entire `Game::Run()` call; the crash bypasses it entirely, meaning the throw happens on a
different thread, most likely one of DXVK's own async shader-compiler threads: `DXVK_LOG_LEVEL=
trace` + `DXVK_LOG_PATH=...` showed the crash lands immediately after `debug: Compiling shader
FS_...`, with nothing more written to the log).

**Isolated via bisection, not guessed:**
- Reproduces regardless of render target SIZE (tried `1×1` and `4×4`).
- Reproduces regardless of whether the render target is ever bound, cleared, or unbound at all —
  a version that only *constructs* a `RenderTarget2D` (`new RenderTarget2D(dev, w, h)`) and never
  calls `SetRenderTarget`/`Clear` on it still crashes identically on the next ordinary draw call.
- Reproduces regardless of whether the render-to-texture happens in the same frame or a frame
  earlier (tried deferring the RT construction to frame N-1 and the texture-sampling draw to
  frame N, relying on the framework's automatic `Present()` between `Draw()` calls as a
  synchronization boundary — no change).
- Every one of this corpus's other 19 scenes (none of which ever construct a
  `D3DUSAGE_RENDERTARGET`-flagged texture) pass pixel-perfect, including plain `Texture2D`-based
  textured-quad scenes using the exact same `BasicEffect`/`PositionTexture`/unlit/untextured-
  vertex-color shader bucket this new scene also uses — so the crash is specific to the presence
  of a `RenderTarget2D`-backed (`D3DUSAGE_RENDERTARGET`) texture object, not to texturing or this
  particular effect/shader bucket in general.
- `D3D9RenderTargetBackend::Recreate()`/`GetTextureEXT()` (`src/CNA/Internal/Backends/D3D9/
  D3D9RenderTargets.cpp`) both look correct on inspection: `CreateTexture(..., D3DUSAGE_RENDER
  TARGET, D3DFMT_A8B8G8R8, D3DPOOL_DEFAULT, ...)` (a real, sample-able D3D9 texture per the D3D9
  API contract, not a `CreateRenderTarget()` surface-only resource), `GetTextureEXT()` returns the
  same `colorTexture_.Get()` this creates. Nothing wrong was found by code inspection alone —
  this needs either Vulkan validation layers or DXVK-internals-level debugging to actually
  diagnose, beyond what this task's own scope justifies.
- **Not the same gap `CnaOracleRender.cpp`'s own header comment already documents** (that comment
  is about `RenderTarget2D::GetData()`'s CPU readback path being unproven) — this new scene never
  calls `GetData()` at all; the crash is in ordinary GPU-side texture *sampling* of a render
  target from within a normal effect draw, a different and previously totally untested code path
  (D9-53's own `D3D9_Smoke` Check S/T/U cover create/bind/Clear/`GetRenderTargetData`-readback/
  unbind-restores-back-buffer, never "use the render target as a sampled shader texture").

**Reverted, not committed**: all code (`CnaOracleRender.cpp`/`Oracle.cs` `rendertargettexture=`/
`rendertargetwidth=`/`rendertargetheight=`/`rendertargetclearcolor=` scene-key wiring) and the
new scene file were fully reverted (`git checkout --`) rather than landed half-working, per this
project's own "no half-finished implementations" rule — the working tree is clean, all 19
committed scenes still pass, `D3D9` CTest suite still 11/11 green. **Recommended next step for
whoever picks this up**: reproduce with Vulkan validation layers enabled
(`VK_LAYER_KHRONOS_validation` via `VK_INSTANCE_LAYERS`, if available in this environment) to get
an actual Vulkan-level diagnostic message instead of an opaque `dxvk::DxvkError`; alternatively,
compare against a minimal known-working D3D9 render-target-to-texture sample (outside this
project) on the same DXVK/driver stack to establish whether this is a genuine CNA-side bug or an
environment/DXVK-version limitation. Do not re-attempt the render-target oracle scene until this
is root-caused — a scene that "passes" by accident (e.g. by catching and silently swallowing the
crash) would be worse than no scene at all.

**RESOLVED 2026-07-16 — root-caused as a real CNA-side bug, not a DXVK/environment limitation;
fixed, no Vulkan validation layers ultimately needed.** Root cause found by code inspection once a
minimal repro was isolated at the `D3D9_DrawEx` CTest level (not the oracle harness — kept that
work out of scope for this fix, see below): every `D3D9EffectDraw.cpp` texture-binding call site
(`DrawBasicEffectEXT`/`DrawAlphaTestEffectEXT`/`DrawDualTextureEffectEXT`/
`DrawEnvironmentMapEffectEXT`/`DrawSkinnedEffectEXT`) did
`static_cast<const D3D9TextureBackend*>(params.texture0)` unconditionally. `GpuDrawParams::
texture0`/`texture1`/`envMap` are declared `const ITextureBackend*`/`const ITextureCubeBackend*`,
and `D3D9RenderTargetBackend`/`D3D9RenderTargetCubeBackend` (`IRenderTargetBackend : ITextureBackend`)
are real, legal runtime types for that pointer whenever a game samples a `RenderTarget2D`/
`RenderTargetCube` as an ordinary effect texture — exactly what the reverted oracle scene did. The
`static_cast` silently reinterpreted a `D3D9RenderTargetBackend*` (an unrelated sibling class, not
a base/derived relationship) as a `D3D9TextureBackend*`: undefined behavior that read whichever
field sits at `D3D9TextureBackend::texture_`'s own offset in a `D3D9RenderTargetBackend`'s actual,
different layout, handed `SetTexture()` a garbage `IDirect3DTexture9*`, and crashed later when
DXVK's async shader-compiler thread actually tried to use it — matching the observed symptom and
timing exactly (`SPIR-V`/shader-compile-adjacent crash, uncaught, off the main thread).

This project's own `D3D11GraphicsBackend.cpp` already had to solve the identical problem
(`GetSrvForTextureEXT`, a `dynamic_cast`-based two-concrete-type resolver) — `D3D9` simply never
got the equivalent. Fixed with `ResolveD3D9TextureEXT`/`ResolveD3D9TextureCubeEXT` (new, anonymous-
namespace-local helpers in `D3D9EffectDraw.cpp`, mirroring `D3D11`'s own precedent and its own
documented "duplicated per-file rather than factored into a shared header" rationale), replacing
all 6 unsafe `static_cast` call sites. `D3D9SpriteBatch.cpp` had the same category of gap in its
own texture resolve (already `dynamic_cast`-based, so it silently dropped the texture instead of
crashing) — fixed the same way for consistency, not because it was the crash's own cause.

**Mutation-verified, not just "compiles and doesn't crash the one time it was run"**: temporarily
reintroduced the exact original `static_cast` at the `DrawBasicEffectEXT` site and reran the new
regression check below — reproduced the EXACT documented symptom verbatim (`terminate called
after throwing an instance of 'dxvk::DxvkError'`); reverted, reconfirmed the fix passes. New
`D3D9_DrawEx` Check Q (18 checks total now): a `D3D9RenderTargetBackend` created/bound/`Clear()`ed
to a known color/unbound, then used directly as `params.texture0` for an ordinary unlit+textured
`BasicEffect` draw — exact readback of the render target's real cleared content (not garbage), no
crash. Full `ctest -L D3D9`: 17/17 green, zero regressions.

**Deliberately did NOT re-attempt the reverted `rendertarget_texture_quad.scene` oracle-corpus
addition as part of this fix** — that's `D9-A5`/`D9-84`'s own territory (a separate concern from
root-causing and fixing the crash itself), and picking it up here would have been exactly the kind
of scope drift this session was already corrected for once. Whoever next grows the oracle corpus
can now safely re-add a render-target-as-texture scene; the underlying crash is gone.

**Phase D9-8: `D9-80`–`D9-83` ALL CLOSED — real, verified dispatch for all 5 XNA Stock Effects plus
hardware instancing on this backend.** The shader-dispatch tables/formulas are transcribed and
tested, the `GpuDrawParams` audit is independently re-verified (2 of its 4 gaps turned out resolvable
with no `GpuDrawParams` change; the other 2 remain genuine cross-cutting blockers, not this plan's
call), this backend has drawn its first real, pixel-verified 3D triangle
(`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`, `BasicEffect`-VertexColor-only scope), draws
real effect-aware geometry for `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/
`EnvironmentMapEffect`/`SkinnedEffect` (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` — 10 of
`BasicEffect`'s 32 `ShaderIndex` values, all 8 of `AlphaTestEffect`'s, 2 of `DualTextureEffect`'s
4, 8 of `EnvironmentMapEffect`'s 16, and 12 of `SkinnedEffect`'s 18 are actually drawable given
this project's vertex layouts and `D9-81`'s still-open gaps — all pixel-verified), and now draws
real hardware-instanced geometry (`DrawInstancedPrimitivesEx` via `SetStreamSourceFreq`, CNA's own
NOXNA instancing shader since real XNA has no per-instance-aware Stock Effect shader). `D9-64` (reuse the backend-agnostic state CTest sources) is also now closed, finding and fixing 2
real, pre-existing D3D9 bugs along the way (`SetDepthTestEnabled`/`SetDepthWriteEnabled` silent-
throw stubs; `UpdatePresentationFormatEXT()`'s deferred-format-apply timing) — Phase D9-6 is now
fully closed too. Next smallest task: keep growing `D9-A5`'s scene corpus and validating each
scene against the oracle (`D9-84`, the last row in Phase D9-8) — the harness itself (`D9-A3`/
`D9-A4`) is done and its first result (`colored3d`) is pixel-perfect.
`PreferPerPixelLighting` variants (`BasicEffect`/
`SkinnedEffect`) and `EnvironmentMapEffect`'s specular variants stay blocked on a project-owner-level
`GpuDrawParams` decision (`D9-81`'s still-open findings). The `D3DCULL` winding trap (`D9-21`) did NOT
need to be worked around for `D9-82`/`D9-82b`–`f`/`D9-83` (explicit `CullMode::None` resets
sidestepped it, matching `D3D11_Smoke`'s own precedent) — it's still open, and `D9-84` may yet hit it
for real once culling-sensitive scenes are drawn.

---

## 5. Known bugs and limitations

- `BasicEffect` via `DrawPrimitivesEx` only supports 12 of its 32 `ShaderIndex` values (was 10,
  updated 2026-07-16 — see below) — every combination whose `VSInput` shape has no matching CNA
  vertex layout (Position-only 12 bytes; Position+Normal 24 bytes, colliding with the existing
  Position+Color+TexCoord layout; Position+Normal+Color[+TexCoord] 28/36 bytes) throws a named
  error instead of drawing. See `plan_dx9.md` `D9-82b`'s own closure note / `D3D9EffectDraw.cpp`'s
  header comment for the exact enumeration.
- **RESOLVED 2026-07-16 (`plan_graphics.md` Phase 80, project-owner-authorized cross-backend
  fix): `GpuDrawParams` now carries real `preferPerPixelLighting`/`specularEnabled` fields, and
  this backend's dispatch reads them instead of hardcoding `false`.** `BasicEffect`'s
  pixel-lighting-textured bucket (`ShaderIndex` 28/29) and `SkinnedEffect`'s entire pixel-lighting
  bucket (12-17, all 3 `WeightsPerVertex` values) are now reachable and oracle-proven pixel-perfect
  against real XNA (4 of `D9-73`'s own 5 divergent shader variants — see that row); the untextured
  bucket (`BasicEffect` `ShaderIndex` 24/25, `VSBasicPixelLighting`) stays permanently blocked by
  the SAME missing-vertex-layout gap as the untextured vertex-lit bucket above, unrelated to this
  fix. `EnvironmentMapEffect`'s specular buckets (all 8, `ShaderIndex` 4-7/12-15) are also now
  reachable — a real bug (constants uploaded to the wrong shader stage, or not at all for
  `EnvironmentMapSpecular`) was found and fixed along the way, see `plan_dx9.md` `D9-73`/`D9-84`'s
  own rows for the full record.
- `DualTextureEffect` via `DrawPrimitivesEx` only supports 2 of its 4 `ShaderIndex` values — the
  vertex-color variant (`VSInputTx2Vc`, 32 bytes) collides with the existing
  Position+Normal+TexCoord layout and throws a named error instead of drawing (`plan_dx9.md`
  `D9-82d`'s own closure note). Unaffected by the above (no `PreferPerPixelLighting`/
  `specularEnabled` concept in this effect).
- `D3DCULL` winding (`CullClockwiseFace`/`CullCounterClockwiseFace` vs. `D3DCULL_CW`/`_CCW`) is
  mapped but not yet pixel-proven against the real XNA oracle (`plan_dx9.md` `D9-21`/`D9-84`).

See `plan_dx9.md`'s "CNA's divergences from XNA 4.0" for the six pre-existing, cross-cutting
CNA-vs-XNA fidelity gaps this plan will measure (not fix) once Phase D9-A's oracle is complete.

**Two standing, project-wide architecture-decision items, unrelated to D3D9** (not this plan's to
decide — see `plan_graphics.md` for full detail): `Texture3D`/`TextureCube` inherit
`GraphicsResource` directly instead of `Texture` (Task 863); `GraphicsDevice` stores state objects
by value instead of FNA's reference-type aliasing (Task 869). Both need a project-owner direction
before any backend acts on them — see §9.

---

## 6. Architecture notes

### Main modules (D3D9-relevant)

| Layer | Location | Notes |
|---|---|---|
| Backend contracts | `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` | Being extended additively (approved) for D3D9's needs — see `plan_dx9.md`. |
| **D3D9 backend** | `include/\|src/CNA/Internal/Backends/D3D9/` | Windows-only, MinGW-w64 cross-compiled, own format/state/vertex-declaration mapping (not `D3DCommon`). Device/present/buffers/textures/render-targets/render-state/stock-effect-shaders/colored draws, all 5 XNA Stock Effect draws (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`), and hardware instancing (`DrawInstancedPrimitivesEx`) are real; `D9-84` (oracle validation)/`SpriteBatch` still pending. |
| Vendored XNA stock effects | `src/CNA/Internal/Backends/D3D9/shaders/xna/` (destination) | Microsoft's `.fx`/`.fxh`, verbatim, MS-PL. |
| Spike artifacts (temporary) | `dx9-spike/` | Proven Phase D9-0 code, being moved into the real tree task by task. |

### Critical invariants (do not break these)

Same project-wide invariants as `plan_dx.md`'s `NEXT.md` used to list (Doxygen/SPDX/NOXNA/property
convention/stride-keyed vertex layout/etc.) — see `CLAUDE.md` and `CHECKLIST.md`, not repeated here.
D3D9-specific invariants (from `plan_dx9.md` design decisions): plain D3D9 not D3D9Ex;
`D3DPOOL_MANAGED` for user resources; Microsoft's `.fx`/`.fxh` sources are never edited; shader
targets stay `vs_2_0`/`ps_2_0` for stock effects (never "upgraded" to SM3); no D3DX linked, ever.

### FNA / XNA reference

Authoritative behavioral reference for this backend is **not** FNA (FNA has no D3D9 driver) — it is
XNA itself, in two forms: Microsoft's Stock Effects HLSL sources
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/`) for the shaders, and the
real XNA 4.0 runtime under Wine (`~/.wine-cna-xna40`, `tools/xna-oracle/`) for behavior.

---

## 7. Useful commands

```bash
# Wine prefixes (see dx9-spike/README.md for full detail)
~/.wine-cna-d3d9-spike   # real Microsoft d3dcompiler_47.dll -- shader compile work ONLY
~/.wine-cna-xna40        # real XNA 4.0 (win32, .NET 4.0, in-prefix csc.exe) -- the oracle
~/.wine-cna-d3d11        # D3D9 RUNTIME device tests use this one too (its own dxvk-setup install
                         # already wires d3d9.dll to DXVK) -- do not touch its D3D11/D3D12 CTest role

# Run a D3D9 .exe under Wine+DXVK, with the DXVK-marker gate (mirrors run-wine-dxvk.sh's DX-85 gate)
scripts/run-wine-dxvk9.sh path/to/some_d3d9_test.exe
# Override the prefix (defaults to ~/.wine-cna-d3d11): CNA_D3D9_WINEPREFIX=...
# Bypass the DXVK gate for a deliberate non-DXVK diagnostic: CNA_D3D9_ALLOW_WINED3D=1
# Skip the gate for a binary that never opens a device (e.g. a future D3D9_Common): CNA_D3D9_SKIP_DXVK_GATE=1

# Once D9-10 lands (CMake wiring), the configure command will mirror D3D11's:
cmake -S . -B cmake-build-d3d9 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
      -DCNA_GRAPHICS_BACKEND=D3D9 -DCNA_BUILD_TESTS=ON
```

---

## 8. Next smallest tasks

**Phases D9-0 through D9-13 are ALL fully closed** (`D9-32`/`D9-34`/`D9-60`/`D9-62`/`D9-73`
honestly 🟨 — see their own plan rows for exactly what's deferred and why; `D9-73` is now 4/5
closed, only the permanently-blocked untextured `VSBasicPixelLighting` variant remains, see below).
**Phase D9-A's diff harness (`D9-A1`–`D9-A4`) is fully closed**, now 36 scenes deep, all
pixel-perfect (5 new since the 31-scene count above: `lit_textured_quad_pixellighting`,
`skinned_pixellighting_quad`/`_twobone_quad`/`_fourbone_quad`, `envmap_specular_quad`).

**`D9-81`'s `PreferPerPixelLighting`/`specularEnabled` `GpuDrawParams` gap is now RESOLVED, 2026-07-16**
(project owner authorized the full cross-backend fix, `plan_graphics.md` Phase 80, D3D9 first since
it needs no new shader — Microsoft's own `.fx` sources already have both shader families). This
backend's dispatch now reads the real values; `plan_dx9.md`'s `D9-73`/`D9-84` rows have the full
record, including a real bug found and fixed along the way (lighting constants uploaded to the
wrong shader stage for the pixel-lighting bucket, and a missing `EnvironmentMapSpecular` upload
entirely). **The remaining 8 backends (EasyGL/Vulkan/Bgfx/WebGPU/D3D11/D3D12/Software each need a
genuinely new per-vertex-lit shader; `SdlRenderer`/`Headless` don't render 3D lighting at all) are
`plan_graphics.md`'s own scope, not this plan's** — see that file's Phase 80 for the per-backend
task breakdown, sequenced one at a time by explicit project-owner request. **Phase D9-12 is
now fully closed, including `D9-123`** — `D9-120`/`D9-121`/`D9-122`/`D9-123` all ✅ (see §2's own
Phase D9-12 section for the full `D9-122`/`D9-123` detail, including the `gtest_discover_tests`
cross-compile fix and its own end-to-end verification). **`D9-130` (Phase D9-13 docs) is closed
too** — `docs/d3d9-backend.md`, a full `D3D9` column across all 7 tables in
`docs/graphics-backend-feature-matrix.md`, a `D3D9` build section + Tested-Compilers row in
`README.md`, and the `programs.md` §9 Wine-prefix gap it flagged are all done.

**Phase D9-11 (custom `ShaderEffect`) is authorized AND fully closed (2026-07-15)** — `D9-110`/
`D9-111`/`D9-112` all ✅, see §2's own Phase D9-11 section for the complete detail.

**`D9-A6` (run the oracle corpus against CNA's other backends) is now CLOSED too (2026-07-16)** —
measured EasyGL first: 10/31 scenes pixel-perfect (all `sprite_*` + `alphatest_never_quad`), 21/31
diverge across three evidenced patterns (a rasterization-boundary gap spanning 17 scenes, GPU/driver
floating-point rounding noise on 2 scenes, and 2 real, previously-unmeasured `plan_graphics.md`
candidates — `fog_gradient_quad`'s negative-`FogEnd` handling and `envmap_fresnel_quad`'s Fresnel
interpolation, the latter a concrete confirmation of this plan's own predicted `preferPerPixelLighting`
gap). Logged in `docs/d3d9-divergence-report.md`'s new "Cross-backend measurement (D9-A6)" section,
none fixed, per this row's own explicit "log them and move on" rule. Vulkan/D3D11 remain unmeasured
by this pass — the same recipe (a `cna_*_test`-style CMake registration + a
`run-oracle-corpus-diff-<backend>.sh` twin script) is the natural next step for either.

**Only `D9-140` (real Windows hardware verification) remains in this entire plan** — `needs_human`,
out of scope for this dev environment entirely. Every unilaterally-startable task in `plan_dx9.md`
is now closed (verified just now: `D9-A6`, the last one, is done above; the only other non-`✅` rows
in the whole plan are `D9-A5`/`D9-84`, both deliberately-ongoing "growing with the plan" 🟨 rows with
their own documented remaining scope, not blocked-and-unstarted work).

**Readiness re-audit, 2026-07-16 — accurate current punch list** (replaces this section's own prior
paragraph, which had gone stale: it still said "`EnvironmentMapEffect` specular/
`PreferPerPixelLighting` are blocked on `D9-81`'s cross-cutting `GpuDrawParams` gaps," contradicting
this very section's own "RESOLVED 2026-07-16" paragraph above it — that gap is fixed, not still
blocking). Every non-`✅` row in `plan_dx9.md` was re-read individually; here is what each one
actually still needs, if anything:

- **`D9-21` (`D3DCULL`) — CLOSED 2026-07-16.** 3 new oracle scenes (`cullmode_none_quad`/
  `cullmode_ccwface_quad`/`cullmode_cwface_quad.scene`, reusing `colored3d.scene`'s own triangle,
  a confirmed negative-NDC-signed-area winding per `docs/xna_culling_compatibility_audit.md`'s
  real-hardware-verified table) proved `CullModeToD3D9()`'s mapping against real XNA 4.0,
  `0/65536` each, mutation-verified (swapping the `CW`/`CCW` mapping reproduces the exact
  predicted failure). Corpus is now 39 scenes.
- **`D9-62` (`RasterizerState.DepthBias`/`SlopeScaleDepthBias`) — attempted, NOT closed.** A
  discriminating scene was designed and multiple magnitudes tried (`1.0` through `±1e8`) against
  the real XNA 4.0 oracle — none produced any observable pixel change, while a bias-free baseline
  confirmed the underlying depth test itself works. Likely shares a root cause with this project's
  own separate, pre-existing `Vulkan_DepthBias` CTest failure (same DXVK stack) — a suspected
  environment/driver limitation, not a CNA-side forwarding bug (unchanged, not suspected). See
  `D9-21`'s own plan row for the full investigation and the recommended next lead (try a real,
  non-identity perspective `Projection` — every scene in this corpus is `Identity` today).
- **`SurfaceFormat` sweep** (`plan_graphics.md` Phase 81) — scoped, not decided: needs a project-
  owner call on which formats justify the effort, and `Texture2D`'s own API needs new construction/
  `SetData` paths for non-`Color` formats before the oracle can even describe one. Not started.
- **`D9-A6` extended to Vulkan/D3D11** — offered, deferred by the project owner (2026-07-16). The
  same recipe already proven for EasyGL (a `cna_*_test`-style CMake registration + a
  `run-oracle-corpus-diff-<backend>.sh` twin script) is the natural next step whenever picked up.
- **Permanently blocked, not further actionable without new, larger, out-of-scope work**:
  `D9-73`'s 5th `PixelLighting` variant (untextured `VSBasicPixelLighting` — needs a Position-only
  vertex layout that doesn't exist); `D9-84`'s own full closure (blocked by `D9-62`'s `DepthBias`
  gap above, the `SurfaceFormat` sweep, plus `SpriteSortMode.Immediate`/`.Texture`, already
  confirmed out of scope for good reasons, not silently dropped); NPOT-wrap-on-`Reach` and
  hardware-instancing's `HiDef`-only gate (both need real XNA reference behavior this project has
  no way to verify — FNA implements neither; do not guess).
- **`D9-140` (real Windows hardware)** — the only item in the entire plan that is `needs_human`,
  out of reach in this dev environment entirely.
- A render-target-as-texture oracle scene can now safely be re-attempted (the crash blocking it is
  fixed, §4) but isn't itself a named remaining task — it would be new `D9-A5` growth, not a gap.

See `plan_dx9.md`'s "Execution order" table for the full sequence beyond this.

**Other standing backlog, unrelated to D3D9** (full history: `plan_dx.md` for the now-fully-closed
D3D11/D3D12 work, this file's own top banner for Phase 78):
- **`plan_samples.md` standing queue** (formerly Phase 79, `plan_graphics.md` Tasks 957–1076,
  moved+renumbered `SAMPLE-1`–`SAMPLE-120` on 2026-07-16): a full re-audit of all 153
  `../cna-samples`-catalogued samples, one row per sample. **13 rows** (`SAMPLE-32`/`33`/`34`/`35`/
  `36`/`38`/`39`/`40`/`42`/`43`/`45`/`62`/`66`) now say "No longer CNA-blocked" thanks to Phase 78
  — their own CNA-side shader gap is closed, but they're still `⬜` in `plan_samples.md` because
  **the actual sample port itself** (`.cpp`/`.hpp`/`Content/` under `../cna-samples/samples/<Name>/`)
  hasn't been written yet — that's a different repo, out of `cna_graphics` scope, tracked in
  `../cna-samples`'s own plan file. The other ~88 `⬜` rows in `plan_samples.md` are unrelated to
  shaders (re-verification passes, other DEFERRED.md items) — pick any of those, or any of the 13
  above if the sibling repo's own plan calls for it. Do not touch `⛔` rows (structural/permanent,
  no CNA action possible).
- Task 952 (`RenderTargetCube` depth-gating bug on Bgfx) remains **DEFERRED**, not a next task —
  see §9.

---

## 9. Do not do yet

- **Do not fix any of the six CNA-vs-XNA divergences** (`plan_dx9.md`'s own section) from inside this
  branch — measure with the oracle, report, propose to the project owner for a `plan_graphics.md`
  task. Never "just add the flag while in there."
- **Do not start Phase D9-11 (custom `ShaderEffect`)** without asking first — explicitly flagged
  optional/ask-first in `plan_dx9.md`'s execution order.
- **Do not edit Microsoft's vendored `.fx`/`.fxh` files**, ever, for any reason (`D9-70`).
- **Do not "upgrade" stock effects to `vs_3_0`/`ps_3_0`** because the hardware supports it.
- **Do not widen an oracle tolerance to turn a red test green** (`D9-A4`) — that silently converts
  this from an authenticity project into a parity project.
- **Do not touch `GpuDrawParams`, `D3DCommon/`, `D3D11/`, or `D3D12/`** — still off-limits regardless
  of branch state (cross-cutting or another backend's active territory).
- **Do not touch `IGraphicsBackend.hpp` beyond the approved additive extension** (new
  `GraphicsBackendCreateArgs` fields + the one device-event channel) — nothing else, no drive-by
  refactors.
- **`plan_dx.md` is entirely closed for both D3D11 and D3D12, through Phase DX16** (2026-07-15) —
  nothing left to authorize or implement there on this Debian machine. Only `DX-27`/`DX-90`/`DX-91`
  (D3D11) and `DX-110`/`DX-114` (D3D12) remain, all `needs_human` — a real Windows machine with a
  real GPU, or a real device-removed trigger neither backend can induce under Wine. **Do not open a
  "Phase DX17" or similar speculatively** — if the project owner wants more D3D work, they'll say so
  (e.g. the same way Phase DX16 itself started from an explicit percentage-audit request). Don't
  invent new gaps to close just because the plan file is open-ended in principle.
- **When working Phase DX12, do not merge D3D11 and D3D12 into one shared device/backend class**
  "for less duplication" — `plan_dx.md` design decision 4 already scoped what's genuinely shared
  (`D3DCommon`); forcing the actual device/command/resource logic to share code across two
  structurally different APIs is exactly the kind of premature abstraction `CLAUDE.md` warns
  against.
- **Do not assume D3D12 swap-chain/`Present()` support works locally under Wine "since D3D11's
  DXVK path worked"** — `DX-100`'s real spike found the opposite for presentation specifically
  (`CreateSwapChainForHwnd` crashes/fails under vanilla Wine's `dxgi.dll` + vkd3d-proton, even
  though the device/queue/command-list path itself is genuinely solid). Build `DX-102` onward
  around off-screen/readback proof and re-verify swap-chain support explicitly before relying on
  it, rather than inheriting D3D11's own assumption by analogy.
- **Do not resume Task 952** (Bgfx `RenderTargetCube` depth-gating bug) without explicit
  instruction — explicitly marked **DEFERRED** by the project owner after 2 full investigation
  rounds found no root cause.
- **Do not attempt Task 863 or Task 869** (the two architecture-decision items in §5) without the
  project owner picking a direction first.
- **Phase 78's `cna_graphics`-side shader-conversion work (Tasks 945/946/947/1079–1082) is DONE**
  (2026-07-16, explicit project-owner direction) — do not re-port any of the 13+1 already-closed
  shaders, and do not re-litigate Task 945's own decision (manual porting). What's still off-limits
  without new instruction: writing the actual sample ports themselves in the sibling `../cna-samples`
  repo — that's a different repo with its own plan file (not opened this session), genuinely outside
  `cna_graphics` scope, not merely "not yet started."
- **Do not chase `cna_demo_xact`'s build failure** — missing example asset directory, not a CNA bug.
- **Do not attempt `EasyGL_MRT_TwoAttachments`** opportunistically — pre-existing, off-limits
  without a dedicated task.
- **Do not run more than one backend's `ctest`/`CnaTests` suite concurrently** — causes spurious
  `Subprocess aborted` failures from resource contention, not real bugs.
- **Do not bundle multiple task numbers into one commit** — one task per commit, staged by explicit
  filename (never `git add -A`/`.`).
- **Do not claim indistinguishability from Wine+DXVK results alone** — `D3DCAPS9` under DXVK is
  synthesized, not driver-reported, and device-lost rarely fires naturally under Wine. Real hardware
  verification is `D9-140`, `needs_human`.

---

## 10. Resume prompt

```
Read NEXT.md first (this file, feature/dx9 branch), then plan_dx9.md in full before touching any
code -- this is a much stricter plan than the other CNA backends (indistinguishability from real
XNA 4.0, verified against a real oracle, not just "renders plausibly").

Implementation is authorized through Phase D9-13. The IGraphicsBackend boundary problem is resolved
(additive GraphicsBackendCreateArgs extension + device-event channel, approved 2026-07-14). Phase
D9-11 (custom ShaderEffect) still needs an explicit ask before starting. Phase D9-14 needs real
Windows hardware, out of reach here.

Pick exactly one task from §8 "Next smallest tasks" (default to the first one unless told
otherwise; the D3D9 punch list is exhausted down to `needs_human` rows, so also consider the
"Other standing backlog, unrelated to D3D9" items at the end of that section). Inspect only the
files that task names -- do not go exploring unrelated modules, and do not refactor anything you
find along the way that isn't directly required for this task. See §9 for what stays off-limits
generally.

Make one small, verified improvement:
1. Investigate/reproduce first (run the exact command named in the task).
2. Implement the smallest correct thing per plan_dx9.md's design decisions -- do not improvise past
   what the plan already decided.
3. Where the task is a rendering/behavior claim, verify it against the real XNA 4.0 oracle
   (tools/xna-oracle/, ~/.wine-cna-xna40), not just "looks right" -- that is this plan's whole
   point.
4. Update plan_dx9.md's own task table (status + notes) with the real result.
5. Update this NEXT.md: Sec.2/Sec.3/Sec.8, following the same short-index style as the rest of the
   file -- do not let it grow into a duplicate of plan_dx9.md.
6. Commit (staged by explicit filename, one task per commit), following this repo's existing
   commit-message style (git log --oneline).

Do not start a second task in the same session unless the first is fully closed, tested, and
committed, and NEXT.md/plan_dx9.md are updated.
```
