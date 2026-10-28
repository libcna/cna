# XNA/FNA behavior decisions

This is the permanent ledger for cases where measured Microsoft XNA 4.0 behavior differs from
FNA or from CNA's earlier FNA-derived behavior. It is not a defect list: current defects live only
in [`../handoff.md`](../handoff.md) §5.1.

FNA remains the normal readable implementation reference. When genuine XNA behavior has been
measured and disagrees with FNA, XNA wins. A new entry needs reproducible evidence, a regression
test, and the implementation disposition; source recollection or a stale consumer report is not
enough.

The first binding-led campaign was collected in 2026-09 from ten consumer repositories. CNA.NET is
now the maintained language binding; the Go, Java, Python, Ruby, Rust, Swift and TypeScript
bindings are archived in `cna-lab`. See [`binding.md`](binding.md) for the current binding model.

## Measured XNA-over-FNA decisions

| Area | Measured XNA contract | CNA evidence or disposition |
|---|---|---|
| Effect structure defaults | Every non-array structure member keeps its own declared default; a later array member does not overwrite its rows. | `tests/reference/xna40/effect-struct-defaults/`, `EffectStructDefaultsOracle.cs`, dependency audit A2v2 A-09, and the corresponding libcna/mojoshader fix. |
| Pixel centres on multisampled targets | XNA's integer pixel-centre correction remains active on multisampled destinations. | `XnaMultisamplePixelCenterTest`; libcna/FNA3D `74698f735a43`; CNA `baf453c9c`. The measurement used XNA 4.0 under Wine/DXVK, not native Direct3D 9. |
| Achievement collections | Mutation is refused with `NotSupportedException` before argument validation; a missing string key raises `KeyNotFoundException`. | CNA-DEMOS-ISSUE-077/078 and the gamer-services regression corpus. The C missing-key route keeps its documented `INVALID_ARGUMENT` mapping. |
| Effect scalar getters and SkinnedEffect bones | Scalar getters reject non-scalar shapes. Bones exposes 72 coherent 4x3 views, while restored transforms remain affine 4x4 matrices. | CNA-DEMOS-ISSUE-034/035 and the compiled-effect reflection tests. |
| `VideoPlayer` guards and retention | `GetTexture` before `Play` is an invalid operation; out-of-range volume is refused without mutation; `Video` remains readable after stop/dispose; disposed transport and writes are guarded. | CNA-DEMOS-ISSUE-099 and the media/native/C regression corpus. |
| Missing video file at `Play` | The exact public exception is `ArgumentException`, not FNA's `FileNotFoundException`; raw `Video` construction keeps its separate file exception. | CNA-DEMOS-ISSUE-101. C maps this path to `INVALID_ARGUMENT`. |
| `SoundEffectInstance::Pan` refusal order | Disposal and spatial-state checks precede range validation and mutation. | CNA-DEMOS-ISSUE-073 and `SoundEffectInstance` regressions. |
| Dynamic sound disposed members | XNA checks disposal before argument validation or queue access. | CNA-DEMOS-ISSUE-038 and the dynamic-sound regressions. |
| Widescreen threshold | `GraphicsAdapter.IsWideScreen` is strictly greater than 1.6; 16:10 is not widescreen. | `fb62662c9`, `GraphicsAdapterTest.IsWideScreenUsesXnasLimitOfOnePointSixExclusive`. |
| Microphone buffer duration | Validation uses total milliseconds; 1000 ms is accepted while values above the documented limit are refused. | `fb62662c9`, `MicrophoneTest.BufferDurationUsesTotalMillisecondsAsXnaDoes`. |
| `SoundEffectInstance::Apply3D` | The listener-array overload accepts multiple listeners. | `0345f2471`, `Apply3DArrayOverload`, and `Apply3DMultiListenerNearestListenerDominates`. |
| Microphone enumeration | No synthetic `Default Device` entry is prepended; `Default` names a real enumerated device. | `30bd6cf60` and the SDL3 recording-device regressions. |
| `RendererDetail` identity | Friendly name and renderer id both participate in equality and hashing. | XNA-MISSING-017 and `RendererDetailTests`. |
| Signed-in gamer lookup | `operator[](PlayerIndex)` finds the gamer whose `PlayerIndex` matches; it is not collection ordinal lookup. | GS-004m and gamer-services collection tests. |
| Network private slots | `privateGamerSlots` must be strictly less than `maxGamers`, leaving at least one public slot. | GS-007e2c3b and network-session tests. |
| Explicit local-gamer lists | XNA validates the list, folds a user mask, uses a four-local-gamer limit, and preserves the search limit on join. | GS-007p and the network-session regressions. |
| `Game::Tick` after exit | No further fixed or variable update runs after `ShouldExit` becomes true. | KILLER-1 and runtime lifecycle tests. |
| Audio size/duration conversion | Conversion rounds duration as .NET Framework does and always returns whole frames. | KF-7/KF-8 and audio conversion tests. |
| Audio argument validation | XNA rejects invalid rates, channels, frame alignment, loop regions, state and numeric ranges that FNA often accepts or clamps. | KF-12 and the audio argument-validation corpus. |
| Device-creation failure | Failures surface as `NoSuitableGraphicsDeviceException`, wrapping other creation exceptions where XNA does. | CBIND-144 and runtime device-creation tests. |
| `SpriteFont::MeasureString` | The trailing right-side bearing is held pending and clamped at a line end. | `fb62662c9` and the negative-right-bearing font regressions. |
| Packed float channels | Float-to-integer packing saturates and rounds ties to even; NaN becomes zero and infinities saturate. `Color.Lerp` and `Multiply` use XNA's fixed-point arithmetic. | XNAPACK-001, `tests/reference/xna40/framework/framework-packing-oracle.json`, `XnaFrameworkPackingTests`, and the ISSUE-132 oracle. |
| Packed-vector value contracts | All 17 packed-vector types implement the measured equality, hash and exact hexadecimal/text `ToString` contracts. | `b6f127b15` and `PackedVectorValueContractTests`. |
| Launch parameters | Direct duplicate `Add` throws `ArgumentException` and preserves the first value; parsing retains empty and colonless values. | `b086814f9`, native `LaunchParametersTests`, and strict-C `RuntimeGameSmoke.c`. |

## Further measured oracle families

The 2026-10 demos repair campaign also measured and pinned these families. Detailed raw probes live
in the sibling `cna-demos/docs/repair-evidence/` tree; the named CNA tests and commits are the local
regression authority.

| Family | Decisions retained here | Evidence |
|---|---|---|
| Content dispatch and readers | Reference identity is preserved only by reference-shaped carriers; reader lookup uses target type; initialization has one owner and stable published instances. CNA retains its explicit `unique_ptr` registry ownership where XNA reflection cannot be reproduced directly. | Content dispatch 087, reader lookup 043, intermediate initialization 089. |
| Curves and matrices | Curve evaluation, matrix decomposition, perspective validation, quaternion inverse/division and transpose preserve XNA's exact precision, aliasing and exception boundaries. | Commits `d753d0997`, `705ea2324`, `2a245b91f`, `df9234202`, `0b03b7b21`. |
| Numeric values | .NET Framework float hashing, vector transforms, reversed-bound clamp/min/max, integer hashes and union aliasing follow measured XNA behavior. | Commits `bf209dcbe`, `9786d4c93`, `eef23e6f8`, `5a44d2c50`. |
| Geometry and containment | Text formatting, sphere construction, ray/box thresholds, frustum negative-zero text and convex containment use the genuine XNA oracle boundaries. | `tests/reference/xna40/framework/frustum-separation-oracle.txt` and cna-demos geometry evidence. |
| Lifecycle | Component hooks/events, disposal order, collection mutation during callbacks and launch-parameter behavior follow genuine XNA ordering. | Commits `031693a9f` and `b086814f9`; lifecycle regressions. |
| Storage and title I/O | Disposed-container guards, async result consumption, path validation, exception translation and Android fallback boundaries follow measured XNA behavior. | Storage 151, StorageDevice async oracle, Title 145, and the storage/title regressions. |

## Deliberate retained boundaries

These are current decisions, not open defects:

- Display modes report `SurfaceFormat::Color`. CNA's platform display value has no format and FNA
  makes the same explicit assumption. Change it only with genuine XNA evidence and a platform-level
  representation design.
- Native monitor handles are not exposed through the C ABI. The platform value is not necessarily
  an operating-system monitor handle, and the C boundary deliberately does not disclose native
  backend pointers.
- `GraphicsAdapter::Revision` and `SubSystemId` return documented zero values where FNA throws;
  CNA has no portable PCI identity source.
- `cna_camera_set_test_state_ext` accepts only states a handed-out test camera can really occupy.
  `CLOSED` and `NOT_SUPPORTED` remain refusal states, preserving read-after-write for every accepted
  state (BINDFIX-019).
- Launch-parameter key enumeration in C is deliberately sorted even though the native dictionary's
  enumeration order is a separate implementation detail.

## Rejected or retired findings

- BINDFIX-028 did not reproduce: the adapter renderer identity and shader dialect follow the active
  renderer; the old aggregate consumer probe had reset global test state before reading them.
- BINDFIX-018 added the missing decal support query in `e18854238`; the complete graphics engine
  layer was later retired by `5572f3ca1`.
- BINDFIX-031's GPU instance-culler hypothesis was never measured through its failing path before
  that engine layer was retired. Comparable future functionality requires a new measurement.
- BINDFIX-052 is retained as an Emscripten dependency correction: SDL created the shared
  `/tmp/filedrop` directory defensively but removed it unguarded for every window, so destroying a
  second window raised `ENOENT`. CNA carries
  `cmake/patches/sdl-86296ac8-0001-emscripten-filedrop-rmdir.patch`, pinned by the twelfth
  `CApi_WasmBrowserProbe` check.
- Historical binding defect queues and their point-in-time test totals remain in Git history. A
  fixed finding is not a current defect merely because an archived consumer still reports it.

## Adding a decision

1. Measure genuine XNA behavior or inspect the exact shipped assembly IL; record the boundary and
   any platform limitation.
2. Compare FNA and current CNA, including validation and exception ordering rather than only the
   successful result.
3. Add a regression that fails on the old behavior.
4. Record the decision here with its evidence and implementation disposition.
5. Put a real current defect in `handoff.md` §5.1; do not turn this ledger into a second backlog.
