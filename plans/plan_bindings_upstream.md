# plan_bindings_upstream.md — defects the language bindings measured in CNA

Ten language bindings live in `../\_bindings`: `cna-cs`, `cna-common-lisp`, `cna-go`,
`cna-java`, `cna-python`, `cna-ruby`, `cna-rust`, `cna-swift`, `cna-ts`, and their
templates. Each one binds this repository's C ABI, and each one has been writing down
what it found wrong in CNA rather than working around it silently.

This plan is the CNA-side backlog derived from those records. It exists because the
evidence was scattered across roughly fifty documents in ten repositories, and because
none of those repositories can fix anything here: they are consumers of a read-only
dependency.

**Collected 2026-09-04** from the binding repositories' own documents. Their measurements
were taken against CNA C ABI 0.7.0 through 0.21.0, at commits between
`a09196a6477f69a7a57c8364f990658d31531a5b` and `e5ae0820e`. This repository's `next` is
several hundred commits past most of them and the ABI is now 0.22.0, so **every row must
be re-measured before it is worked on**. Several rows the bindings still carry as open
were closed by later commits, and the bindings say so themselves where they have
re-measured.

## What this plan is not

It is not a list of places where CNA differs from Microsoft XNA. Several of the strongest
binding findings were exactly that, and they were recorded here as **decisions for the
project owner rather than defects**, because `CLAUDE.md` then made FNA the authoritative
behavioural reference and CNA matched FNA faithfully in each case.

**All five are now closed, in CNA's favour of XNA.** The owner made XNA the tie-break on
2026-09-04 and restated it on 2026-09-09 — *CNA should follow XNA faithfully, and FNA only
after it*. This section is kept because the reasoning is worth having, not because anything
here is open.

| divergence | what it was | closed by |
|---|---|---|
| Stock effect scalar getters and Bones reflection | XNA EffectParameter.GetValueBoolean/GetValueInt32/GetValueSingle reject non-scalar, non-array shapes; genuine SkinnedEffect Bones exposes72 coherent4x3 matrix views with raw fourth column zero, while the stored FNA reflection describes3x4 register storage. Numeric arrays traverse the matrix row-first; GetBoneTransforms restores affine M44. | CNA-DEMOS-ISSUE-034/035,2026-10-22: genuine41 checks under Wine/private GPU;27 new native tests, full graphics3067 with90 unchanged skips and selected aggregate1128 with21 unchanged skips. Compiled-on SDL_GPU/FNA3D/WebGPU each127PASS; ASan/UBSan27PASS. Original demo33PASS on four renderers;142 stock/C passes, six declared shadow-path skips, six exact visuals,132 unchanged external outcomes including eight separate082 failures. Standalone parameter extension behavior retained. See demos STOCK_PARAMETER_TRIAGE.md. |
| `VideoPlayer` guards and retention | genuine XNA refuses GetTexture before Play with InvalidOperationException (not FNA's NRE), refuses out-of-range Volume without mutation, retains Video after Stop/Dispose, and guards disposed state/position and configuration writes while leaving configuration getters readable | CNA-DEMOS-ISSUE-099, 2026-10-21: genuine41 controls; original guards9/lifetime6 reproduced. Owner approved C alignment. Native53focused/355media/full10885PASS+101sameSKIP, sanitizer53, external112 parity, seven metadata gates and final ten renderer/eight CTest checks pass; original assertions retained. See CNA_DEMOS_REPAIR_HANDOFF.md and demos VIDEO_PLAYER_GUARDS_TRIAGE.md. |
| `VideoPlayer::Play` deferred missing-file exception | FNA throws FileNotFoundException; genuine XNA's pre-decoder file guard returns E_INVALIDARG and its actual translator throws exact ArgumentException with absent ParamName | CNA-DEMOS-ISSUE-101, 2026-10-21:32 genuine guard/translator checks and native5FAIL/1PASS baseline becoming6PASS; included in the complete099 runtime/external/sanitizer checkpoint. Owner approved C INVALID_ARGUMENT. Raw Video/URI construction keeps FileNotFoundException. Native Windows codec/error timing remains unverified because Wine failed the valid WMV control. See demos VIDEO_C_CONTRACT_ALIGNMENT.md and VIDEO_CODEC_REFERENCE_LIMITATION.md. |
| `SoundEffectInstance::setPanProperty` refusal order | genuine XNA `set_Pan` IL RVA0x1ac10 checks disposal, clears an unsubmitted spatial latch, refuses a submitted spatial voice, then validates range and mutates Pan; FNA silently ignores a latched spatial voice and CNA mutated Pan before throwing | CNA-DEMOS-ISSUE-073, 2026-10-21: genuine33/native18 PASS; native full10905 preserves prior outcomes; integrated sanitizer27, external112 on SOFTWARE/OPENGLES3, registered32native/19demo PASS. The old Apply3D demo's mutation expectation was corrected from measured XNA, with all18 checks retained. |
| `DynamicSoundEffectInstance` disposed getters/conversion helpers | genuine XNA IL RVAs0x1b0d0/0x1b15c/0x1b280/0x1b364 checks disposal before arguments or queue access; FNA omits these guards | CNA-DEMOS-ISSUE-038, 2026-10-21: genuine24/native5 PASS; same full/sanitizer/external/CTest checkpoint as073. Both old helper tests explicitly adopt XNA exceptions; private queue cleanup and stress bounds remain asserted. |
| `GraphicsAdapter::getIsWideScreenProperty()` | compared the aspect ratio against FNA's `4.0f/3.0f` (`FNA/src/Graphics/GraphicsAdapter.cs:71-83`, comment included); `cna-ruby` read the pinned XNA IL and found `1.6f`. Every mode between them — 3:2, 14:9 — was widescreen to FNA and is not to XNA, and 16:10 sits exactly on XNA's strictly-greater limit | `fb62662c9`, 2026-09-04 (`BINDFIX-033`) |
| `Microphone::setBufferDurationProperty()` | read `getMillisecondsProperty()`, the sub-second component, exactly as `FNA/src/Audio/Microphone.cs:57-60` does. That made the documented `> 1000` branch unreachable and refused 1000 ms — the value the property itself *reports* on an unconfigured microphone, so `mic.BufferDuration = mic.BufferDuration` threw, while 1100, 1500 and 2500 ms were accepted. XNA's IL reads `get_TotalMilliseconds` | `fb62662c9`, 2026-09-04 (`BINDFIX-032`) |
| `SoundEffectInstance::Apply3D` | refused any listener count but one, as FNA does. XNA copies every listener into a native array and hands XACT the whole thing; there is no count restriction in `UnsafeApply3D` | `0345f2471`, 2026-08-26 (`CABI-6`) |
| `Microphone::All` / `Default` | carried a synthetic `"Default Device"` entry that FNA prepends (`SDL3_FNAPlatform.cs:1699,1707`) and XNA does not have, so `Default` was not a real device and `All` was one longer than the machine's device list | `30bd6cf60`, 2026-09-09 |
| `RendererDetail` identity | compared `RendererId` alone — `Equals`, `GetHashCode` and both operators, exactly as `FNA/src/Audio/RendererDetail.cs:47-75` does. XNA's `operator==` is `if (left._name == right._name) { return left._id == right._id; } return false;` and its `GetHashCode` XORs the hashes of both fields, substituting 0 for an empty one, so the friendly name is part of the identity. Two renderers with the same id and different names were equal to CNA and are not to XNA | `XNA-MISSING-017`, 2026-09-20 |
| `SignedInGamerCollection::operator[](PlayerIndex)` | returned the collection element at the enum's ordinal, as the FNA-era port did (`return collection[(int)index];`). XNA's IL (`get_Item(PlayerIndex)`) walks the collection and returns the gamer whose `PlayerIndex` matches, else null — so with only player two signed in, `SignedInGamers[PlayerIndex.One]` named player two in CNA and is null in XNA | `GS-004m`, 2026-09-29 |
| `NetworkSession::BeginCreate` private slots | accepted `privateGamerSlots == maxGamers`, the CNA/FNA-era bound. The XNA IL (`BeginCreate(NetworkSessionType,int,int,int,int,int,NetworkSessionProperties,...)`) throws `ArgumentOutOfRangeException` for `privateGamerSlots < 0 \|\| privateGamerSlots >= maxGamers`, so one slot is always public; the local XML's contradictory range text does not override the IL | `GS-007e2c3b`, 2026-09-28 |
| `NetworkSession` explicit local-gamer lists | sized the local-gamer limit to the list, as FNA's constructor does, so a session created for one gamer refused `AddLocalGamer`; a gamer listed twice joined twice; SystemLink `Join` always used a limit of 4 whatever the `Find` had asked. XNA's IL: `BeginCreate`/`BeginFind`/`BeginJoinInvited(IEnumerable<SignedInGamer>, ...)` run `GetLocalGamers` first (null entry or empty list `ArgumentException`, disposed gamer `ObjectDisposedException`), fold the list into a user mask and pass a limit of 4; `Join` keeps the search's limit | `GS-007p`, 2026-09-29 |
| `Game::Tick()` after `Exit()` | kept running the frame's remaining fixed-step catch-up Updates, and the variable step's one Update, after `Exit()` — from an earlier Update or from a quit handled by `PollEvents()` — exactly as `FNA/src/Game.cs:479` does. XNA's IL begins `Tick` with `if (ShouldExit) return;`, loops `while (num > 0 && !ShouldExit)` and guards the variable step with `if (!ShouldExit)`. Found by `cna-killer`, whose stop condition fired three times in one slow frame | `KILLER-1`, 2026-09-30 |
| `SoundEffect::GetSampleDuration` / `GetSampleSizeInBytes` (and the `DynamicSoundEffectInstance` and `Microphone` forms) | truncated the duration to whole milliseconds and computed the size as seconds × rate × channels × 2, as FNA does. XNA's `AudioFormat.DurationFromSize` rounds `FromMilliseconds` to the nearest millisecond (.NET Framework), and `SizeFromDuration` is `(int)(ms * (double)((float)rate / 1000f))` frames plus `frames % channels`, times `BlockAlign` — always whole frames, where FNA's size could be a torn sample; one second of mono at 44100 Hz is 88198 bytes in XNA | `KF-7`/`KF-8`, 2026-09-30 |
| Audio argument validation | followed FNA, which validates almost nothing: a `SoundEffect` or `DynamicSoundEffectInstance` of any sample rate or channel count, a buffer, offset or count that is not whole frames, a loop region outside the sound, `Volume`/`Pitch`/`MasterVolume` out of range or NaN (passed through or clamped), `SpeedOfSound` of zero, `DopplerScale` NaN, `IsLooped = true` on a dynamic instance (ignored), `CreateInstance` on a disposed `SoundEffect`. XNA's IL (`SoundEffect.FromBuffer`, the instance setters, `DynamicSoundEffectInstance`'s constructor, `SubmitBuffer` and `IsLooped`) refuses each with `ArgumentOutOfRangeException`, `ArgumentException`, `InvalidOperationException` or `ObjectDisposedException`, and turns a `DistanceScale` of zero into `float.Epsilon` | `KF-12`, 2026-09-30 |
| A game whose device cannot be created | the platform's failure left the `Game` constructor, and with no display FNA's `SDL_Init` throws a plain `Exception` (`SDL3_FNAPlatform.cs:197-202`). XNA's `GraphicsDeviceManager.CreateDevice` IL rethrows a `NoSuitableGraphicsDeviceException` and wraps every other exception from creating the device in one (`Direct3DCreateError`, "Unable to create the graphics device."); CNA creates the device in `Game`'s constructor and wraps there | `CBIND-144`, 2026-10-02 |

The fourth was found by the sample campaign rather than by a binding, and is the only one
backed by a side-by-side capture of both runtimes on this machine rather than by reading
IL: SAMPLE-098's whole HUD is `Microphone.Name`, and the unchanged XNA executable drew
`PulseAudio Input is Stopped` where CNA drew `Default Device is Stopped`.

**A note for whoever reads this next.** On 2026-09-09 the first three were reported to the
owner as still open, on the strength of the prose that used to stand here, without being
re-measured against the tree. All three had been fixed weeks or days earlier. That is the
mistake this document's own header warns about — *"every row must be re-measured before it
is worked on"* — so the rows above now name the commit that closed each one, and the
behaviours are pinned by tests rather than only by comments:

- `GraphicsAdapterTest.IsWideScreenUsesXnasLimitOfOnePointSixExclusive` — the property
  reads whatever display the host has, so it could never pin the constant; the rule is now
  reachable through `GraphicsAdapter::IsWideScreenAspectRatioEXT` and asserted on both
  sides of 1.6, including 16:10 sitting exactly on it.
- `MicrophoneTest.BufferDurationUsesTotalMillisecondsAsXnaDoes` — `set(get())` at 1000 ms,
  and the three values FNA's reading wrongly accepted.
- `SoundEffectInstanceTest.Apply3DArrayOverload` and
  `Apply3DMultiListenerNearestListenerDominates` already pinned the third, and the second
  of those is written so that a "listeners[0] decides" implementation fails it.
- `MicrophoneTest.AllContainsOnlyRealDevicesWithNoInventedDefaultEntry` and
  `Sdl3AudioRecordingDeviceTests.ProviderEnumeratesOnlyRealDevicesSortedById` for the
  fourth.
- **Display modes carrying a hardcoded `SurfaceFormat::Color`**, which `cna-ruby` and
  `cna-swift` both recorded. `SurfaceFormat.Color // FIXME: Assumption!` is what FNA writes,
  four times over, in `SDL3_FNAPlatform.cs` and `SDL2_FNAPlatform.cs`.
- **`SpriteFont::MeasureString`** adding the last glyph's right side bearing unclamped.
  `cna-ts` (finding 27) and `cna-go` (Foundation 69) measured this independently, with
  tables, against XNA's IL, which keeps the bearing pending and adds `Math.Max(pendingZ, 0)`
  at each line break. CNA's `curLineWidth += cKern.Y + cKern.Z` is
  `FNA/src/Graphics/SpriteFont.cs:201`, character for character, in both of FNA's measure
  paths.

Each of these is a real observable divergence from XNA that a binding has measured and
pinned. Fixing them means deciding that XNA's IL outranks FNA where the two disagree,
which is a change to the project's governing rule and not a change to a function.

That rule has since been decided: `CLAUDE.md` now makes XNA the tie-break where a measured
Microsoft XNA 4.0 behaviour and FNA's implementation disagree, and requires each divergence
taken on those grounds to be recorded here. The rows above are still owner decisions, because
none of them has been re-measured against the runtime itself; **XNAPACK-001** below is the first
one that has.

---

## Measured XNA decisions from the CNA demos repair campaign

Recorded 2026-10-19 under the same XNA-over-FNA policy. The permanent issue
inventory is `../cna-demos/docs/CNA_ISSUES.md`; individual dispositions and exact
validation boundaries are in `CNA_DEMOS_REPAIR_MATRIX.json`. These rows record
reference decisions, not a claim that every CNA port defect also exists in FNA,
nor that every listed native repair has completed external regression validation.
All probes use the genuine installed Windows XNA 4.0 assembly, not a replacement.

Content dispatch087 (2026-10-20, candidate; full validation pending): genuine XNA4
reference53/value-and-nullable34 controls require one invocation boundary for typed, raw and
erased reads. A new disposable reference is recorded after successful type checking;
an existing reference must retain its identity and is not recorded again. A boxed
existing value cannot preserve identity and is rejected after reading. An empty
Nullable existing argument boxes to null and therefore creates a new value; it is
not an existing instance merely because the caller chose that overload. Explicit
readers bypass the serialized index only for value targets. CNA's existing
shared_ptr carriers preserve reference identity; its by-value reference wrappers
(String/vector/Curve/graphics/media wrappers) do not have a managed ReferenceEquals
identity. Retain their existing value-carrier behavior instead of inventing aliasing
or changing their public ownership model. The internal serialized-reference trait
is shared with collection wire dispatch and does not use Sharp Type.IsValueType's
documented stub. Manager disposal011 and by-value graphics ownership are separate
unresolved requirements, not claimed fixed by this dispatch repair.

Reader target lookup043 (2026-10-20, native validated; external regression pending): genuine
`../cna-demos/docs/repair-evidence/content/ReaderTargetLookupOracle.cs` passes5/5.
Initialize resolves the reader by its target type even when reader and target names
differ; implicit ReadRawObject uses that association without consuming a type index.
Genuine XNA returns its existing table reader. CNA's already documented fresh
unique_ptr reader ownership is retained; this repair does not import XNA's static
reader-instance cache or redesign CNA's registry API. Registration identity guards
publication against removal/replacement inside a factory callback. Native regression
coverage also preserves manual factory-key and same-name registrations.

Intermediate initialization089 (2026-10-20, candidate validation pending): genuine
IntermediateInitializationOracle21 and InitializationEdgesOracle15 establish one
long-lived callback owner, one Initialize per worker, self/mutual published lookup,
and failed-owner cache retention. CNA's explicit unique_ptr registry owns existing
instances and exposes stable returned references; it has no reflection constructor
catalog with which to create XNA's fresh singleton/worker instances after failure.
The candidate preserves that ownership extension: a failed initializer is not
retried or copied, operations retain its failure, and raw lookup remains available.
This is not claimed equivalent to XNA's fresh reflected singleton recovery. No new
factory/clone API is introduced. Document relocation paths and aliases remain
operation-local (nested/concurrent controls), even though callbacks share the owner.

Content runtime039/045 (2026-10-20, repair validation pending): genuine
`../cna-demos/docs/repair-evidence/content/ContentOwnershipOracle.cs` passes89/89.
XNA refuses RootDirectory assignment while loaded assets remain, including the
same value; FNA's unrestricted auto-property is not the intended contract here.
Failed Load and uncached ReadAsset do not freeze the root. XNA wraps IOException
at the XNB header and object-body boundaries while preserving a user reader's
InvalidOperationException. CNA keeps its low-level header parser's documented
EndOfStreamException and performs wrapping at the content-load boundary instead.
The common header guard also prevents an ASan-confirmed four-byte heap overread
in the erased external-reference path on a ten-byte compressed file. No broader
FNA lifecycle/ownership rewrite is implied;011/087 remain separately open.

| Findings | Measured contract and reference | CNA repair / evidence |
|---|---|---|
| 156, SDL_GPU stock sprite depth projection | Genuine Windows XNA SpriteBatch.SetRenderState initializes M33=1/M44=1 with no Z translation; identity layerDepth remains normalized depth. CPU QueueSprite incorrectly used0.5-0.5*Z/W, unlike its already-correct compiled sprite path. | Preserve Z/W and keep the existing depth-bias fixture coplanar at0.5 without changing its72 assertions. Original five pixel witnesses change from15PASS/5SDL_GPU FAIL to20PASS; all four original demo frames exactly match the correct SOFTWARE reference. Native11005 retains all11000 old outcomes plus fivePASS. External132 retains123 rows, fixes SDL_GPU depth, and exposes eight separately open082 callback-contract deltas; not a globally green checkpoint. Linux SW/GLES/GL33/SDL_GPU runtime only. See demos SPRITE_DEPTH_PROJECTION_TRIAGE.md. |
| 002/036/097/137/138, Curve | `Curve.Evaluate` Step fraction, post-loop final tangent, degenerate spans and NaN evaluation; `CurveKey.CompareTo` NaN ordering; exact key-position collection matching. Preserve the separate C++ value-ownership alias fix. | `d753d0997`; `../cna-demos/docs/repair-evidence/curve/` |
| 115, Matrix decomposition | `Matrix.Decompose` retains zero-scale/rank-deficient, reflected and shear behavior with XNA output semantics; do not substitute a convenient inverse/determinant rejection. | `705ea2324`; `../cna-demos/docs/repair-evidence/issue-115/` |
| 117, perspective factories | `Matrix.CreatePerspective*` validates public float Pi, exact argument exceptions/parameter names and order, retaining the measured IEEE edge cases. | `2a245b91f`; `../cna-demos/docs/repair-evidence/issue-117/` |
| 119, Quaternion inverse/division | `Quaternion.Inverse` and division retain wide intermediate range through the final component store; squared magnitude is not an interchangeable float-returning intermediate. | `df9234202`; `../cna-demos/docs/repair-evidence/issue-119/` |
| 120, value hashes | .NET Framework Single hashing canonicalizes both zero signs but retains nonzero NaN payload bits. `CurveKey.GetHashCode` adds the field hashes with 32-bit wrap rather than XOR. | `bf209dcbe`; `../cna-demos/docs/repair-evidence/issue-120/` |
| 121, vector arrays | `Vector2/3/4.Transform*` checks wide endpoints source-first, then accesses elements only for positive lengths; negative indices with actual access have the measured index exception. | `9786d4c93`; `../cna-demos/docs/repair-evidence/issue-121/` |
| 122/136, bounds | `Vector2/3/4.Clamp` caps then floors reversed bounds; vector Min/Max selects the second operand on ties/unordered comparisons. `MathHelper.Min/Max` propagates the first NaN payload. | `eef23e6f8`; `../cna-demos/docs/repair-evidence/bounds/` |
| 124/125, integer values | `Point/Rectangle.GetHashCode` uses unchecked sums. `Rectangle.Union` output may alias either input without changing its result. | `5a44d2c50`; `../cna-demos/docs/repair-evidence/integer-values/` |
| 139, transpose | `Matrix.Transpose(ref,out)` preserves all source fields before aliased output writes; `Plane.Transform` relies on this internally. | `0b03b7b21`; `../cna-demos/docs/repair-evidence/matrix-residual/` |
| 140, planes/shadows | `Plane.Normalize` IL keeps a strict near-unit shortcut and explicitly narrows Math.Sqrt to Single, but not its reciprocal. `Matrix.CreateShadow` normalizes its plane; Reflection uses the same public semantics. | `e4ddea6e0`; `../cna-demos/docs/repair-evidence/matrix-residual/`; external closure pending |
| 123, vector products | `Vector2/3/4.Dot` retains wide cancellation; `Vector3.Cross` Y is the original subtraction, not a negated reverse subtraction that changes zero signs. | `0e7be08e0`; `../cna-demos/docs/repair-evidence/issue-123/`; external closure pending |
| 118, magnitudes | `Vector2/3/4` and `Quaternion.Length/Normalize`, plus vector Distance, match genuine method-entry precision: wide squared sums, float square root for normalization, wide reciprocal. Caller-dependent JIT probes are retained separately, not presented as universal behavior. | `bd46b6ffe`; `../cna-demos/docs/repair-evidence/issue-118/`; external closure pending |

Additional measured geometry decisions (2026-10-20; repairs pending external closure):

| Findings | Measured contract and reference | Evidence |
|---|---|---|
| 024/126, text | BoundingBox, Ray and the four standard XNA vertex ToString methods use one literal outer brace pair; doubled braces in C# format strings are escapes, not output. Keep three CNA-only extended vertex formats unchanged. | `../cna-demos/docs/repair-evidence/geometry/GeometryValuesOracle.cs`, `GeometryTextConsumersOracle.cs` |
| 128, sphere construction | BoundingSphere constructor rejects radius < 0 with ArgumentException, but public Radius remains mutable. C marshalling is field copying, not constructor validation. Empty CreateFromPoints was already repaired by `cd211420e3`. | Same geometry oracle; native and C mutated-radius controls |
| 130, ray/box | BoundingBox.Intersects(Ray) IL starts the clipped interval at zero and uses the strict parallel threshold abs(direction) < 1e-6. Preserve all four public call forms, boundary starts and nonunit directions. | Same oracle: stationary grid, boundaries and threshold controls |
| 135, negative-zero text | Genuine frustum planes retain seven negative-zero fields for Identity, but .NET Framework Single text prints either zero sign as 0. Do not alter derived plane bits; canonicalize display operands only. | Same oracle: all24 plane bits and seven math text types; disproved positive-zero hypotheses retained |

Containment batch127/129/133 (2026-10-20, external closure pending): genuine
`BoundingBox.Contains(BoundingFrustum)` and sphere/frustum containment first use
actual convex separation, not just a plane-rejection classifier or constant-zero
placeholder. A sphere missing a frustum corner proves why a private GJK simplex
helper is necessary; its scope is the three existing volume Intersects paths,
without a public API/cache or changing the owner-reserved ray method. The fixture
`tests/reference/xna40/framework/frustum-separation-oracle.txt` records14328 genuine
shape pairs. Point containment checks all planes with XNA's1e-5 threshold; equal
frusta are geometrically contained. Sphere point surfaces are Disjoint, while
contained volume corners are inclusive; sphere containment uses signed radius
differences and intersection excludes external tangency. The inline frustum/sphere
plane arithmetic must not be replaced by a rounded public Dot intermediary:
genuine1944-case model comparison gives zero wide-model mismatches and18 rounded
mismatches. Genuine12800 radius-arithmetic checks additionally prove18 cases
requiring wide radius sum/difference comparisons. Native math961 passes/two
unchanged skips,registered160/160,actual production-TU ASan/UBSan/leak11/11 pass;
broad consumer and external validation remain pending. Genuine96 boundary-ray
consumer checks confirm zero hits after point containment repair; the reserved
ray implementation/exception remains unchanged. Source/oracles and retained failures:
`../cna-demos/docs/repair-evidence/geometry/`.

Additional129 precision gate: a genuine2048-case axis-surface probe disproves
point-overload parity in1012 cases. Value Contains keeps the inlined wide squared
distance; output Contains explicitly stores it as Single before comparing with
the wide radius product. A19200-check varied3D/scaled oracle validates these
boundaries and the underlying Vector3 LengthSquared/DistanceSquared wide
accumulation. A private arithmetic helper shares the calculation without widening
public Single results or changing118's squared overflow/underflow contract.
Native math963PASS/two skips; registered247/247; four-production-TU ASan/UBSan/leak
13/13; broad consumers1408PASS/four unchanged skips. Actual C ABI/external
containment closure still pending. Preserve the failed parity hypothesis/log.

Containment127/129/133 external closure completed2026-10-20: isolated full CTest
732accepted/zero failures(1233.07s), both719-ID SOFTWARE/OPENGLES3 sweeps with
exact46 cumulative repaired statuses and no unrelated deltas, six pixel-exact
visual references. Frozen executable and both actual libraries reverified.
The pending containment annotations above are superseded; permanent evidence:
`../cna-demos/docs/repair-evidence/geometry/CONTAINMENT_CHECKPOINT.md`.

Lifecycle141–144 repair decisions (2026-10-20, native-validated commits031693a9f
andb086814f9; external validation pending): genuine
GameComponent/DrawableGameComponent setters invoke virtual On* hooks; the base
hook raises the public event with this sender. Explicit component Dispose repeats
the managed override/event and removes membership before Disposed. Drawable
Dispose(true) always invokes UnloadContent before the base, even before Initialize;
false is silent. Collection Clear uses a live index loop including null slots,
whereas Game.Dispose uses an original snapshot. C++ owning overloads must retain
references through nested callback removal, exception unwinding, re-registration
and the complete virtual disposal call. No public layout/vtable is changed.
LaunchParameters retains empty values, skips only process argv0 (not the first
explicit-vector entry), keeps the first parsed duplicate and throws ArgumentException
for direct duplicate Add. Genuine oracles and failing pre-fix native/C witnesses:
`../cna-demos/docs/repair-evidence/lifecycle/`. No external lifecycle closure yet.

Storage151 repair decision (2026-10-20, native validation complete/external pending):
genuine StorageContainer marks itself disposed before Disposing. Its properties and
filesystem operations then throw ObjectDisposedException, including from inside
that callback; existing streams and backing files survive. Unlike GameComponent,
its explicit disposal/event remains one-shot. One internal guard protects the
canonical entry points before argument validation, with no public layout/API change.
The existing C StorageSmoke incorrectly expected a successful FileExists in that
callback. The corrected test requires INVALID_STATE with untouched output and
independently checks file preservation via a fresh live container. The shared
exception barrier already performs that mapping; no redundant C-side guard is added.
Genuine172 plus seven event checks, native18/18, actual StorageContainer.cpp
ASan/UBSan/leak4/4; exact prior consumers1408PASS/four unchanged skips. All239 runtime
tests remain visible, including three pre-existing new Title145 failures and three
unchanged capability skips. Actual external C ABI execution is still pending.
Evidence: `../cna-demos/docs/repair-evidence/storage/`; no FileShare152 closure.

Museum StorageDevice async repair decision (2026-10-22, focused validation complete):
XNA4 StorageDevice.BeginShowSelector validates player before size, accepts player255
for shared storage, and leaves directoryCount unchecked. End methods reject null or
mismatched result kinds with ArgumentNullException; a matching result is consumed
before owner/name/filesystem validation. A failed End is still one-shot; End inside
the synchronous callback is permitted. EndOpenContainer checks both originating
device identity and captured player. These differ from FNA's reusable fake-async
results and unchecked selector arguments. Preserve XNA's resource-derived ParamName
for the empty title and mismatched-owner errors. Evidence: genuine46-check public
oracle in cna-demos/docs/repair-evidence/storage/StorageDeviceAsyncOracle.cs and the
shipped XNA4 StorageDevice/StorageContainer IL. Native baseline:8/9 new tests fail,
all18 existing storage tests pass. Candidate27/27 native,9/9 ASan/UBSan/LSan and
46/46 genuine checks PASS. External36 comparisons preserve32PASS/four FileShare152
failures (before/after, SOFTWARE/GLES); selector checks increase6->12 on each renderer.
No C API contract change or full ISSUE010 closure. See demos storage/ASYNC_CONTRACT.md.

Title145 measured repair decision (2026-10-20, external validation pending):
genuine Windows XNA4 rejects initially empty input before cleaning, normalizes
dot/parent components before rejecting rooted/reserved/escaping names, and validates
a relative URI including Framework4's65520 UTF16-unit boundary. A generic lexical
normalizer does not preserve empty-component behavior. Actual GAC assembly IL,
not the available mismatching decompilation, translates only FileNotFoundException
and DirectoryNotFoundException to missing-file categories; other open failures
retain their inner exception under InvalidOperationException. Genuine30/30 checks
pin both ASCII/astral limits and existing/missing embedded-NUL prefixes.

The canonical FileStream must reject empty/NUL names before filesystem effects,
refuse directory handles and retain native stat errors; an existence precheck in
TitleContainer masked those distinctions. Android asset fallback is retained only
for genuine missing paths, not for invalid/denied paths. Its branch syntax-checks
under native headers; no Android runtime/cross-build claim. Case-insensitive lookup
is preserved. C title-read invalid arguments map to INVALID_ARGUMENT; missing or
unreadable entries retain the route's documented IO result, with no buffer write.
The old native absolute-path acceptance assertion contradicted genuine XNA and is
corrected to require rejection while preserving its existing-file control.

Sharp prerequisites1c0fc9b5/551ced42/82b57541/3304ecd6/58c6cba1 now pass18144/18144
tests across41 executables,zero failures/skips. The FileStream Append/ReadWrite
long-path guard ordering differs between Framework4 and current.NET; both measured
72-check versions are recorded, and Sharp retains current.NET's existing validation
order. No title OpenStream call uses Append. No FileShare policy is inferred.
Native/C ABI pre-fix failures and the intermediate length-category regression are
retained; no assertion was relaxed. The actual two production TUs now pass13/13
ASan/UBSan/leak tests. Final rebuilt-native focus17/17 and all241 runtime tests
238PASS/three unchanged skips now pass; exact1412 prior consumers1408PASS/four
unchanged skips (168.769s), and both native hashes are reverified. Actual external
C ABI/demos remain pending; no issue closure is claimed by this native checkpoint.

The 118/123/140 external closures completed on 2026-10-20: full732 enabled CTests,
both719-ID renderer sweeps and six exact visuals pass. The pending annotations in
the recorded rows above are superseded by this checkpoint; permanent evidence is
`../cna-demos/docs/repair-evidence/issue-118/REGRESSION_CHECKPOINT.md`.

The versioned avatar asset recipe is not an XNA math API. Companions `32e470ed4`
and `785bf2d2a` preserve its authored binary32 arithmetic without reverting public
compatibility repairs or changing catalog golden digests. Complete raw field
comparisons and the original failed runs are retained with the 123/118 evidence.

---

## XNAPACK-001 — every float channel XNA packs is rounded, ties to even, and CNA truncated

**Fixed 2026-09-05** in `modules/math/src/Color.cpp`, the fourteen headers under
`modules/graphics/include/Microsoft/Xna/Framework/Graphics/PackedVector/` that turn a float
channel into an integer one (the three half-float types have no rounding rule of their own),
and the new shared helper `modules/core/include/CNA/Internal/PackedRounding.hpp`.

Measured on the XNA 4.0 runtime itself, not read from anyone's source: the driver is
`tools/xna-pipeline-oracle/framework/FrameworkPackingOracle.cs`, run by
`run-framework-oracle.sh` under Wine against the installed XNA Game Studio 4.0 assemblies, and
its 68 measurements are committed as
`tests/reference/xna40/framework/framework-packing-oracle.json`. CNA reproduces all of them in
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/PackedVector/XnaFrameworkPackingTests.cpp`,
which also fails if a measured case gains no reproduction.

The rule XNA follows, everywhere a float channel becomes an integer one:

| finding | source | repro |
|---|---|---|
| `Color(Vector4)` rounds; it does not truncate | `color/vector4_quarters` | `new Color(new Vector4(0.25f, 0.5f, 0.75f, 1))` is `{64, 128, 191, 255}`; CNA gave `{63, 127, 191, 255}` |
| the tie goes to the even neighbour, not away from zero | `color/vector4_tie_even`, `packed/Byte4/ties` | `126.5, 127.5, 128.5, 129.5` byte units pack as `126, 128, 128, 130`; `new Byte4(0.5f, 1.5f, 2.5f, 3.5f)` is `0x04020200` |
| the same rule holds for the normalized types, which CNA rounded away from zero (`std::lroundf`) | `packed/NormalizedByte4/ties`, `packed/NormalizedShort4/ties` | `0.5f/127, 1.5f/127, 2.5f/127, 3.5f/127` packs as `0, 2, 2, 4` |
| and for the colour layouts, which CNA rounded with `+ 0.5f` then truncated | `packed/Alpha8/ties`, `packed/Bgr565/ties`, `packed/Rg32/ties`, `packed/Rgba1010102/ties` | `new Alpha8(0.5f/255)` is `0x00`, not `0x01` |
| `Color.PackFromVector4` saturates and rounds exactly like the constructor | `color/packfromvector4_out_of_range` | `(2, -1, 0.5, 1)` packs as `{255, 0, 128, 255}`; CNA wrapped to `{254, 1, 127, 255}` |
| a NaN channel packs as 0, and the infinities saturate | `color/vector4_nan`, `packed/Byte4/nan_and_infinities` | `new Byte4(NaN, +inf, -inf, 1e30f)` is `0xFF00FF00` |
| `Color.Lerp` and `Color.Multiply` use fixed-point factors and shifted channel products, not rounded output channels | `color/lerp_half`, `color/multiply_odd_ties`, `color/lerp_amount_above_one`; expanded ISSUE-132 oracle below | `Lerp(black, white, 0.5f)` is `127`, not `128` |

**ISSUE-132 refinement (2026-10-20):** the original half-factor measurements did
not distinguish float truncation from XNA's actual fixed-point algorithm.
`../cna-demos/docs/repair-evidence/issue-132/ColorFixedPointOracle.cs` now passes
1,782 genuine XNA checks over nine colors and eighteen factors; unchanged CNA
fails162 of the matching native checks. Lerp first packs amount*65536 with
ties-to-even saturation (NaN ->0), then shifts signed channel deltas by16.
Multiply truncates the scaled factor after clamping to[0,16777215], multiplies
unsigned channels, shifts16 and saturates at255. This changes finite factors too,
for example(10,20,30,40)*1.1 ->(10,21,32,43), and preserves zero channels at+Inf.
NaN Lerp preserves the first color including its alpha. The original FNA-style
float implementation and the former all-zero NaN Lerp expectation are not the
XNA contract. Existing constructor/packing rules and API surface stay unchanged.
Native regression and strict C smoke retain the prior failure witnesses; the
shared packing helper handles Lerp rounding without nonfinite integer casts.

FNA truncates in the constructors (`R = (byte) MathHelper.Clamp(color.X * 255, Byte.MinValue,
Byte.MaxValue);`, `Color.cs`) and neither clamps nor rounds in `PackFromVector4`, which is what
CNA reproduced and what the Color packing clamp pinned. XNA wins, so those pins were rewritten against
the measurements rather than deleted.

Two consequences worth naming. The old `+ 0.5f then cast` and `std::clamp` paths let a NaN
channel reach an integer cast, which is undefined behaviour in C++ where C# merely leaves the
value unspecified; the shared helper closes that for every type at once. And the change is
visible in output, not only in edge cases: it is what made the content pipeline's
`VectorConverter` tables agree with XNA
(`plans/plan_xnapipeline_parity.md` XNAPP-090..092).

**Still open, measured in the same pass:** XNA's packed-vector structs all override `ToString()`
to print their packed value as hex (`packed/Byte4/tostring` is `04030201`,
`packed/Short2/tostring` is `00020001`). CNA implements `ToString()` on none of the seventeen
types. That is a missing member of the XNA surface rather than a packing difference, so it is
recorded here and left to the graphics module's own parity work; the two cases are listed as
unreproduced in the test above rather than silently skipped.

---

## BINDFIX-001 — `GraphicsAdapter` is enumerated before the video subsystem exists

**Reported by:** `cna-ruby`, in `docs/graphics-adapter-ordering-upstream-defect.md`,
measured 2026-09-01 against `cnanext` `e5ae0820e`, re-measured 2026-09-04 (Foundation 104
and 105) against artifact `c32bfbd307d695664f906ccf2834ec3f9ebc240fa388d544ac21ee3ebaeb731b`.
Corroborated by `cna-go` (`docs/foundation-68-graphics-adapter-evidence.md`) and
`cna-swift`.

**Status: fixed in `5726b1a828390e90e20d7973260069ea9697525a`.** Requalify against that SHA.

What was taken, and why it is two changes rather than one:

- `AdaptersChanged()` raises the video subsystem itself, so the enumeration no longer
  depends on the order someone else happens to call it in. A platform with no display
  server refuses the acquire, which is the no-display case and keeps the existing
  fallback.
- The reference is **kept** afterwards, not given straight back. A display id is only
  meaningful inside the video session that issued it, so an enumeration that raises
  video, reads the displays and drops it again caches ids that name nothing once the
  device's own acquire has restarted the session — measured: the name and the 22-mode
  list came out real while `CurrentDisplayMode` still answered 800x480.
- A `Game` installs its own platform (`modules/runtime/src/Game.cpp:197`), so a second
  `Game` in one process ends the session the cache was built from however long the pin
  is held. The adapter therefore **rebinds itself by display name, in place**, when its
  id stops naming anything. Fix shape C, and deliberately not B: rebuilding the cache is
  what `cna_graphics_adapters_refresh` already refuses to do, "because a live
  GraphicsDevice retains its adapter and replacing it would dangle" — the same
  constraint this row asked for, already enforced at the ABI.

Measured through the C ABI with nothing calling `SDL_Init` by hand, two sequential games
in one process:

| | before | after |
|---|---|---|
| `Description` | `Default Display` | `Dell Inc. 27"` |
| `DeviceName` | `\\.\DISPLAY1` | `\\.\DISPLAY1` (the synthetic XNA-convention name FNA also produces) |
| `CurrentDisplayMode` | 800x480 | 2048x1152 |
| `SupportedDisplayModes` | 1 | 22 |
| window vs adapter | contradict | agree, in the same frame |
| second `Game` | same fallback | identical to the first |

The probe is `build-probe/bindfix_adapter_ordering.c`.

**Still open on this row:** the HEADLESS/no-display artifact check from the regression
list below has not been run — this build carries OPENGLES3 only, and the available
control was a video driver that cannot start, which degrades to the fallback and exits
cleanly. The four separate audits at the end of this row are also untouched.

### The measured behaviour

`GraphicsAdapter::getAdaptersProperty()` fills a static `adapters_` cache on first use.
`GraphicsDevice`'s default constructor evaluates `GraphicsAdapter::getDefaultAdapterProperty()`
as a **delegating-constructor argument**, which C++ sequences before the delegated
constructor's body — and the body is where `createOrAttachWindow()` and
`setVideoSubsystemAcquired(true)` run.

`Sdl3Displays::GetDisplays()` is `SDL_GetDisplays`, which answers nothing before
`SDL_INIT_VIDEO`. So the first enumeration happens with no video subsystem, the no-display
fallback is what gets cached, and it is process-global and stale for the rest of the
process:

| field | cached fallback | truth on the measured host |
|---|---|---|
| adapter count | 1 | 1 |
| `Description` | `Default Display` | `screen` |
| `DeviceName` | `\\.\DISPLAY1` | the real X11 display name |
| `CurrentDisplayMode` | 800x480, `SurfaceFormat.Color` | 1280x800, aspect 1.6 |
| `SupportedDisplayModes` | that one mode | the display's own modes |
| refresh | `CNA_RESULT_NOT_SUPPORTED` | — |

The binding's own control settles the cause: initialising SDL video before device
construction makes the **identical build** answer `screen`, 1280x800, aspect 1.6.
CNA's SDL3 display enumeration is correct. Only the moment it is first asked is wrong.

The clearest single symptom is a one-frame self-contradiction on a qualified
OPENGL33/X11 run with a real window (`cna_game_window_get_native_window_ext` answers
`CNA_NATIVE_WINDOW_SYSTEM_X11` with a non-null `Display*` and a real XID):

```
cna_game_window_copy_screen_device_name   -> "screen"
cna_graphics_adapter_copy_description     -> "Default Display"
```

Two sequential `Game`s in one process both read the fallback.

### What the fix must satisfy

1. On a display-capable platform and renderer, the adapter cache must not stay
   permanently populated from the pre-video fallback.
2. **Adapter object and reference stability is preserved.** Do not fix this by
   invalidating adapter objects a `GraphicsDevice` already holds or that are exposed
   through C API handles.
3. HEADLESS and genuinely displayless configurations keep their current, correct
   behaviour. Do not invent a physical display where there is none.
4. An ordinary consumer must not have to call `SDL_Init` or initialise video by hand.
5. Sequential `Game` creation must not retain a stale fallback adapter.
6. The existing C API adapter routes start answering truthfully after the core fix.
   Prefer fixing CNA's core over adding routes for one binding.

### Fix shapes, in preference order

- **A.** Acquire the video subsystem before the first evaluation of
  `getDefaultAdapterProperty()` that default `GraphicsDevice` construction performs.
  Note the trap: moving the acquisition into the delegated constructor's *body* is too
  late, because the adapter expression has already been evaluated by then.
- **B.** If A is architecturally awkward, let the cache record that it was populated from
  the no-video fallback, and refresh the **existing** adapter objects in place once
  display enumeration becomes valid.
- **C.** Otherwise make refresh safe while preserving object identity.

Do not simply replace the cached adapter objects: `GraphicsDevice` and C API consumers
may already hold them.

### Regression tests to add on the CNA side

- A real display is available and nothing initialises SDL externally.
- `DefaultAdapter` is the real adapter.
- `Description` and `DeviceName` are the real ones.
- `CurrentDisplayMode` is the real mode, not a hardcoded 800x480.
- `SupportedDisplayModes` come from the real display.
- The `GameWindow` screen-device identity and `GraphicsAdapter` no longer contradict
  each other.
- Create a `Game`, destroy it, create another one.
- A HEADLESS / no-display control.
- Retained adapter references and C API handles stay valid across the fix.

The acceptance criterion is **not** that the adapter routes answer `CNA_RESULT_SUCCESS`.
It is that on a real-display qualification artifact, the adapter describes the same real
display the window and device are actually using, with no cached fallback anywhere.

Commit the fix on its own and record its SHA here, so `cna-ruby` can requalify against a
named commit rather than against a description.

### What unblocks in `cna-ruby` when this lands

Missing types: `GraphicsAdapter`, `GraphicsDeviceInformation`,
`PreparingDeviceSettingsEventArgs`.
Missing `GraphicsDevice` members: the `GraphicsAdapter`-taking constructor, `Adapter`,
`DisplayMode`.
Missing `GraphicsDeviceManager` members: `FindBestDevice`, `CanResetDevice`,
`RankDevices`, `OnPreparingDeviceSettings`, `PreparingDeviceSettings`.
Five further Ruby overload-mapping diagnostics are derivative of those and are not
separate CNA problems.

`PreparingDeviceSettings` must eventually be able to expose **and mutate** truthful
device-selection information. An observer-only callback does not reproduce XNA's
semantics.

### The four separate audits — done, and none of them is a fix to take here

Asked after the enumeration was repaired, each against FNA, which `CLAUDE.md` makes the
authoritative behavioural reference. All four come back the same way.

- **`MonitorHandle`.** The question was whether CNA can now supply a truthful native
  monitor handle. It cannot without diverging twice. `cna-ruby` observed that CNA's own
  value underneath is the display id cast to `uintptr_t` rather than an `HMONITOR` — and
  that is exactly FNA: `SDL3_FNAPlatform.GetMonitorHandle` is
  `return new IntPtr(unchecked((int)displayIds[adapterIndex]));`. The route's
  `CNA_RESULT_NOT_SUPPORTED` is the second, separate decision: this ABI does not disclose
  native handles anywhere, which `display.h` states. Both halves are deliberate, and
  fabricating an `HMONITOR` is what the row asked not to do.
- **`Revision` and `SubSystemId`.** They answer a hardcoded zero and `display.h` says so.
  FNA's answer is worse: both properties `throw new NotImplementedException()`. Neither
  matches XNA, which reports real values; CNA's is the more useful of the two and is the
  only one that is honest at the point of use. Supplying real ones means per-adapter PCI
  data CNA does not read today.
- **Display modes hardcoded to `SurfaceFormat::Color`.** `CNA::Platform::DisplayMode`
  carries width, height and refresh rate and no format, so the graphics layer has nothing
  to map. That is FNA again, and FNA is not comfortable about it either: `SurfaceFormat.Color
  // FIXME: Assumption!` appears four times across `SDL3_FNAPlatform.cs` and
  `SDL2_FNAPlatform.cs`. A neutral format on the platform struct, mapped in the SDL backends
  and reported unknown by Headless, is the shape a fix would take — and it is a divergence
  from FNA, so it belongs to the owner.
- **Every `cna_graphics_adapter_*` route is device-scoped.** This one is a real contract
  gap rather than an FNA match: XNA's `Adapters` and `DefaultAdapter` are statics usable
  before any `GraphicsDevice` exists, because an adapter is what a device is chosen *from*.
  Closing it means routes that answer with no device handle, which is an ABI addition and a
  design decision, not a defect to repair. It survives the cache fix untouched.

---

## BINDFIX-008 — should `cna_game_launch_parameters_add` refuse a duplicate key?

`LaunchParameters::Add` calls `emplace`, so the first value for a key wins and a second add is a
silent no-op that still answers `CNA_RESULT_SUCCESS`. Three contracts existed for one operation:
the C header said "adds or replaces", the implementation kept the first value, and XNA's
`Dictionary<string, string>.Add` throws on a duplicate.

The header's claim was the demonstrably false one — it asserted the canonical add overwrites,
which it does not — so it now states the measured behaviour. What remains open is whether the
boundary should answer `CNA_RESULT_INVALID_STATE` for a key already present, which is what XNA's
throw becomes in a C ABI. That is a behaviour change to a published route, and `LaunchParameters`
also carries two related divergences `cna-ruby` measured and `cna-rust` restated:
`cna_game_launch_parameters_parse_ext` skips an argument shorter than three characters or without
a colon, where XNA keeps a colonless argument with an empty value; and
`cna_game_launch_parameters_get_key_size` enumerates by name rather than in container order.
Reported by `cna-rust` (`RUST-UPSTREAM-026`) and `cna-ruby`.

2026-10-20 repair campaign ISSUE-144 resolves the canonical behavior in commit
b086814f9, with genuine-XNA evidence and native tests. Duplicate Add throws
System::ArgumentException, whose existing C barrier maps to INVALID_ARGUMENT (not
the earlier proposed INVALID_STATE); original value is preserved. The canonical
parse now retains empty/colonless values. The deliberately sorted C enumeration
order is unchanged. External C regression and final closure are still pending.

---

## BINDFIX-018 — a decal pass has no support query

`cna_decal_pass_create` succeeds on a renderer that cannot run the pass, which is the
engine layer's usual contract, and the caller is meant to ask before using it. There is
nothing to ask: a decal pass is its own handle kind, so `cna_post_process_pass_is_supported`
refuses it with `CNA_RESULT_INVALID_HANDLE`, and no `cna_decal_pass_is_supported` exists.

The header used to name that route anyway, along with `cna_post_process_pass_destroy`,
which refuses the handle too and so leaked every pass a caller released by the book. The
header is fixed; the missing query is not, because adding a route is an ABI addition.
Either give the family its own `cna_decal_pass_is_supported`, or let the
`cna_post_process_pass_*` routes accept the kind — the second is what the header assumed
all along and would close `copy_name` at the same time.

Measured by `cna-java`'s `engine_layer_families.c`, which asks a bloom pass the same
questions and gets SUCCESS, so the difference is the handle kind rather than the renderer.
Reported as `JAVA-UPSTREAM-008`.

---

## BINDFIX-019 — a test camera's birth state is one its own setter refuses

`cna_camera_set_test_state_ext` refuses `CNA_CAMERA_STATE_CLOSED` and
`CNA_CAMERA_STATE_NOT_SUPPORTED`, and its implementation gives a reason worth keeping: a
device that is not open is never handed out, so nothing reports closed, and "not supported"
is what an absent device answers rather than a state one can be put in. Every state it
accepts reads back unchanged, which is a property worth having.

Measured against `next`, with `cna-java`'s own probe:

```
state at birth        SUCCESS NOT_SUPPORTED
set NOT_SUPPORTED     INVALID_ARGUMENT  reads back SUCCESS OPENING
```

So the refusal and the birth reading disagree, though not for the reason
`JAVA-UPSTREAM-021` gives. `CameraTestState::state` defaults to `Opening`, not to
`NOT_SUPPORTED`; the `NOT_SUPPORTED` at birth is the `Camera` object's own answer, from a
layer above the test state, before anything has driven it. Two layers, two answers, and a
setter whose domain matches one of them.

Left as measured. Widening the setter's domain for a test backend — where "closed" and "not
supported" are exactly the situations a test wants to reproduce — is defensible, and so is
keeping the guarantee that every accepted state reads back unchanged. That is a decision
about what the test surface is for.

---

## BINDFIX-028 — `JAVA-UPSTREAM-018` does not reproduce as reported

The row says creating a device resets three renderer-selection queries, "the count becomes
0 while `copy_available_ext` still answers 5". Asked directly, back to back, in a probe
that does nothing else:

```
get_available_count_ext  result=0 count=1
copy_available_ext       result=14 count=1
```

They agree, and both read the same `State().available`. The `0` in the binding's own probe
output comes from elsewhere in its sequence, after a `reset_for_tests`.

The row's second half does not reproduce either. Asked the same way:

```
get_current_type          result=0 type=3
copy_current_name         result=0 name="OPENGLES3"
```

Both name OPENGLES3. Both derive from `getCurrentGraphicsRendererType()`, which is a
compile-time constant, so they cannot disagree — and the `UNKNOWN` in the binding's output
is again a later line in a sequence that has called `reset_for_tests` by then.

Two lessons, and the second is the more useful. A binding's probe output is evidence about
the binding's sequence, not about a route: both halves of this row were read off an
aggregate that had moved on. Asking one route, twice, in a program that does nothing else
settled it in a minute. `get_active_ext` answering `UNKNOWN` before any device exists is
separate and correct — it is documented to, and `GetActive()` throws until a renderer has
been created.

---

## BINDFIX-031 — the GPU instance culler culls nothing, narrowed

`cna-ts` (finding 20) measured the culler running, reporting success and keeping every
instance it is given: one ten thousand units off the side of a hundred-unit frustum
survives. `is_supported` answers true and the unsupported reason is empty, so nothing is
announced.

What this pass established, without reproducing it on a GPU:

- CNA has its own test for exactly this, `ComputeCullingTest.TheGpuCullerAgreesWithTheCpuOneBoxForBox`
  (`plans/plan_modern.md` MOD-1551), and it passes. So the shader's arithmetic and the
  outward-normal convention it mirrors are not the fault on their own — the test runs the
  same test expression, character for character.
- The test and the shipped route differ in exactly one thing: **where the frustum comes
  from**. The test uploads `culler.getFrustum()`, the frustum the CPU culler already holds.
  `GpuInstanceCuller::cull` builds its own, `BoundingFrustum(view * projection)`, from the
  two matrices the caller passed.

So the hypothesis to test first is the product, not the shader: whether the matrices reach
`cull` in the order and convention `BoundingFrustum` expects. A frustum built from a
transposed or mis-ordered view-projection yields six planes that reject nothing, which is
the measured symptom exactly, and it would leave the existing test green because that test
never goes through this path.

The next step is a probe that culls with a known camera and compares against
`FrustumCullerEXT` on the CPU, then prints the six planes both produce. That is a GPU
measurement rather than a reading, which is why it is written down here instead of guessed
at.

---

## BINDFIX-035 — a `.cnb` model carries its skeleton and publishes no skins

**Status: fixed by `fix(CNASTREET-SKINMETA)` (cna-street's GLTF-207, 2026-09-30).** A compiled
`.cnj` or `.cnb` model lists its one skin -- the `Tag`'s `SkinningData`, driving the meshes whose
vertices carry `BlendIndices` -- so `skinned_model.cnb` now answers 1. A skin's name still has no
place in the format and stays empty.

Found while adding the skinned content asset the owner asked for. `cna_tool_gltf_to_cnb`
compiles `tests/assets/gltf/skin-four-weighted.gltf` into a model whose container holds an
`MSKL` chunk of 792 bytes -- `cna_tool_cnb_info` lists it beside `MBON`, `MMSH` and the
rest -- so the skeleton survives the format.

The loaded model's skin collection does not. Measured through the C ABI with the same
probe against both formats:

| asset | `cna_model_get_skin_count_ext` | skeleton route |
|---|---|---|
| `tests/assets/gltf/skin-four-weighted.gltf` | 1 | publishes, twice |
| `tests/assets/cnb/skinned/skinned_model.cnb` | 0 | nothing to ask |

`ContentManager.cpp:4103` populates `setSkinsEXTProperty` on the glTF import path and
nowhere else, and no CNB chunk carries the `ModelSkinEXT` triple -- a skin's name, its
skeleton, and the meshes its palette drives. So for any model delivered as `.cnb`,
`Model.SkinsEXT` is empty, `cna_model_get_skin_count_ext` answers zero and every skin
route below it is unreachable. That is a format gap rather than a defect in a route: the
data the collection describes is partly there (`MSKL`) and partly not (which meshes each
skin poses).

The asset is checked in as the fixture, so whoever closes it has the failing case and the
working one side by side.

---

## BINDFIX-036 — fractional global Doppler scale changes stationary pitch

Found through the native and browser ports of XNA Audio3D sample SAMPLE-059. Its normal dog
and cat recordings sounded like a motorcycle because the sample sets
`SoundEffect.DopplerScale = 0.1f`: CNA computed the physical Doppler factor with only the
emitter scale and then multiplied the completed playback ratio by `0.1`, so even zero
listener/emitter velocities produced a `0.1` frequency ratio.

Microsoft XNA 4.0's `KernelSoundEffectInstance.Apply3D` IL establishes the required ordering.
Before calling `X3DAudioCalculate`, it replaces the emitter's `_DopplerScale` with
`_DopplerScale * KernelSoundEffect::dopplerScale`; afterward it applies only
`dspSettings.DopplerFactor` to the pitch ratio. FNA's current `SoundEffectInstance.UpdatePitch`
diverges by multiplying the completed factor by the global scale.

Fixed under `plans/plan_audio.md` **AUD-09-009** by passing the combined scale into
`ComputeDopplerFactor` exactly once. Two regressions cover both the stationary `0.1` case and
a moving-source `0.5 * 0.5` case whose XNA ratio is exactly `8/9`.

---

## Fixed in this pass

Three defects, each independently reported by two or more bindings, each one a place where
CNA contradicted its own documentation or its own siblings:

- **BINDFIX-002** — `cna_spot_shadow_map_destroy` did not check `activeBorrowCount`, so a
  spot shadow map destroyed itself with a lent handle still pointing at it where its three
  siblings refuse. The guard existed: it had been copy-pasted a second time into
  `cna_shadow_map_destroy`, where it was unreachable, still carrying the spot map's
  message. Moved to the function it was written for.
  Reported by `cna-ts` (finding 16) and `cna-java` (`JAVA-UPSTREAM-013`).
- **BINDFIX-003** — `CallWithExceptionBarrier` caught `std::out_of_range` and
  `std::invalid_argument` but not `std::logic_error` itself, so routes that throw a bare
  one answered `CNA_RESULT_INTERNAL` where their headers document
  `CNA_RESULT_INVALID_STATE`. A shim comment in the engine layer already asserted the arm
  existed. Reported by `cna-ts` (findings 14 and 25) and `cna-java`
  (`JAVA-UPSTREAM-006`, and `-012` for the compute-shader route).
- **BINDFIX-004** — the two `cna_cube_lut_*` shims caught `CNA::CNAException` while
  `CubeLut::parse` and `CubeLut::loadFromFile` throw `CNA::Graphics::EngineException`.
  The two are siblings, not parent and child, so the catch could never match and the
  barrier's `EngineException` arm answered `CNA_RESULT_NOT_SUPPORTED` for every failure.
  A typo in an artist's `.cube` file reached a game as "this renderer cannot do colour
  grading". Reported by `cna-ts` (finding 24) and `cna-java` (`JAVA-UPSTREAM-009`).
- **BINDFIX-005** — `cna_post_process_chain_add_owned_pass` consumed a pass handle without
  the `RemoveOwnedGraphicsResourceFor` its sibling destroy performs, so one transfer left
  `cna_game_destroy` refusing for the rest of the process. Reported by `cna-ts` (finding 1)
  and `cna-java` (`JAVA-UPSTREAM-011`).
- **BINDFIX-006** — `~MeshResource` moved an absent `detachedValue` over a content-loaded
  part's live borrow and `~PartResource` dereferenced the result. Destroying or tearing
  down any content-loaded `Model` faulted. Reported by `cna-rust` (`RUST-UPSTREAM-021`),
  `cna-java` (`JAVA-UPSTREAM-004`) and `cna-ruby`.
- **BINDFIX-007** — `ValidateMorphShape` restated a stride list that had gone stale against
  CNA's own canonical table, refusing both PBR layouts. Reported by `cna-rust`
  (`RUST-UPSTREAM-024`).
- **BINDFIX-009** — `cna_morph_target_data_ext_copy_tangent_deltas` bounded the target index
  against an array that is empty until the setter runs. Reported by `cna-java`
  (`JAVA-UPSTREAM-023`).
- **BINDFIX-010** — both `SpriteBatch` `Begin` routes refused the null state descriptors they
  document, and that FNA's own `Begin` substitutes. Reported by `cna-ruby`.
- **BINDFIX-011** — `cna_camera_destroy` freed the provider the process-wide platform override
  points at, without taking the pointer back out. Reported by `cna-rust`
  (`RUST-UPSTREAM-020`), `cna-ts` (finding 11) and `cna-java` (`JAVA-UPSTREAM-019`).
- **BINDFIX-012** — the four clustered constructors documented a game handle and resolve a
  graphics device. Header and parameter names corrected. Reported by `cna-python`
  (ENGINE-006), `cna-ts` (finding 10) and `cna-java` (`JAVA-UPSTREAM-005`).
- **BINDFIX-013** — `cna_pbr_effect_apply_material` wrote the C++ effect while
  `set_texture`/`get_texture` use the C API's retained-handle table, so a material lost every
  texture in both directions and could not clear a slot. Both routes now go through the table.
  Reported by `cna-python` (ENGINE-005), `cna-ts` (finding 19) and `cna-java`
  (`JAVA-UPSTREAM-010`).
- **BINDFIX-014** — the packed depth encoding used base 256 where an 8-bit UNORM channel
  stores `round(c * 255) / 255`, so the four channels delivered one channel's resolution.
  Reported by `cna-ts` (finding 13).
- **BINDFIX-015** — `cna_render_pipeline_get_skybox` echoes the handle that was set while
  `cna_skybox_get_environment` mints a counted one, and both carried the same borrow
  sentence: following it destroyed the caller's own skybox on one route and made the game
  undestroyable on the other. Both headers now say which they are. Reported by `cna-ts`
  (finding 15).
- **BINDFIX-016** — withdrawing a `Motion` or `Compass` test backend left `IsDataValid`
  true for a reading `getCurrentValueProperty()` would no longer hand over. Reported by
  `cna-ts` (finding 5).
- **BINDFIX-017** — `cna_area_light_brdf_table_get_texture` mints a handle counted against
  the game and said only that it borrows. Reported by `cna-rust` (`RUST-UPSTREAM-025`).
- **BINDFIX-020** — `SDL_INIT_CAMERA` appeared nowhere in the tree, so `SDL_GetCameras`
  always answered nothing while `IsSupported` answered true. Reported by `cna-ts` (34).
- **BINDFIX-021** — every GPU timer's first sample was `4294.967295` ms, the value a 32-bit
  query saturates at and what a driver returns for a disjoint result. Reported by
  `cna-python` (ENGINE-003).
- **BINDFIX-022** — `PacketWriter::Write(Color)` wrote four bytes and `PacketReader::ReadColor`
  read four floats, so a colour could not be read back out of a packet. Reported by
  `cna-python`.
- **BINDFIX-023** — the XACT demo's own Z and X keys wrote to a variable its generator had
  marked read-only, under a comment saying "settable". Reported by `cna-ts` (31).
- **BINDFIX-024** — three `_init` routes documented identity transforms and fill zero
  matrices; the code is right and `default(Matrix)` is why. Reported by `cna-ts` (23) and
  `cna-python` (ENGINE-008).
- **BINDFIX-025** — EasyGL's uniform setters wrote through `glUniform` without making their
  own program current, so a uniform set before an effect was applied went to whichever
  program was bound. Reported by `cna-java` (`JAVA-UPSTREAM-016`).
- **BINDFIX-026** — `cna_weighted_blended_transparency_begin` documented the behaviour
  MOD-1697 corrected, and the defensive code it asked for leaves the bracket open forever.
  Reported by `cna-ts` (21).
- **BINDFIX-027** — `importance_sample_ggx` needs a unit normal and said nothing about it.
  Reported by `cna-python` (ENGINE-007).
- **BINDFIX-029** — `cna_post_process_chain_is_gpu_timing_enabled`, the route the header
  sends a caller to, answered `CNA_FALSE` until the chain had applied once, because it
  asked timers that are made lazily. Reported by `cna-python` (ENGINE-004).
- **BINDFIX-030** — a glTF-imported skin's skeleton was refused as "not created through the
  C API" though `skin->Data` held it; it is now published as an aliasing borrow of the
  Model. Reported by `cna-rust` (`RUST-UPSTREAM-022`).

## Fixed in the 2026-09-30 binding retirement pass

The seven standalone bindings (Go, Java, Python, Ruby, Rust, Swift, TS) were requalified against
ABI 0.35.0 before being archived into `cna-lab`; every row below was reproduced against `next`
first.

- **BINDFIX-037** — `cna_graphics_device_supports_capability(TEXTURE_3D)` answered true on
  OPENGLES3 for a device in XNA's default Reach profile, where `Texture3D` refuses every volume
  (SOFTWARE-179), so `CApi_TextureVolumeSmoke` failed on every GL tree. The capability now folds in
  the profile exactly as `MultipleRenderTargets` already did (RLGL-040), and a profile change
  invalidates the cached feature snapshot. Pinned by `CApi_TextureVolumeSmoke` (Reach refuses,
  HiDef follows the renderer) and `Texture3DProfileTest.CapabilityFollowsTheDeviceGraphicsProfile`.
- **BINDFIX-038** — `JAVA-UPSTREAM-017`: a `CNA_GRAPHICS_RENDERER` naming no renderer, or one this
  build lacks, made `libcna_c_api.so` call `std::terminate` while it loaded, because the static
  initializer that publishes the compiled-in set also consulted the variable; a JVM, Python or Node
  host died before its first call. The variable is now read on first use, where the refusal is an
  ordinary `CNA_RESULT_INVALID_STATE`, and it keeps refusing instead of being consumed by the first
  query and quietly replaced by the default. Pinned by `CApi_RendererEnvironment_{unknown,absent}`
  and `GraphicsRendererSelectionTest.ARefusedEnvironmentVariableKeepsRefusingUntilARendererIsSet`.
- **BINDFIX-039** — `next` no longer built the WebAssembly C ABI (`cna_c_api_wasm`) that `cna-ts`
  runs in the browser. Three breaks landed since the last Emscripten C API build: an unqualified
  `apply(*top, ui)` in `GuideScreens.cpp` that ADL resolved to `std::apply` (`Screen` derives from
  `enable_shared_from_this`; libc++ hard-errors where libstdc++ quietly falls back), a missing
  `override` on `DisplayMode::ToString` that clang's `-Werror` in the C API target refuses, and
  `ENetDiscoveryService::Matching` defined only inside the native-only block. Proven by building
  `cna_c_api_wasm` (WEBGL2) and running `CApi_WasmModuleSmoke` and the browser probe (11/11).
- **BINDFIX-040** — `cna-ruby` A8: `cna_effect_get_parameters`, `_get_techniques` and
  `_get_current_technique` segfaulted on the effect a content-loaded `Model` publishes, on every
  renderer. The model mirror published it as an `EffectResource` without the adapter state all 24
  generic Effect routes read; it now gets the same state `CreateEffectHandle` builds, minus the
  game-child count: the loading content owns that effect, and counting it (the first version did)
  left the game undestroyable once a model had been loaded. Pinned by
  `CApi_ContentModelEffectSmoke` (loads `tests/assets/xnb/.../BlenderDefaultCube.xnb`, leaves the
  model to its content manager, then destroys the game).
- **BINDFIX-041** — `JAVA-UPSTREAM-014`: a process exiting with a live game whose creating thread
  had already ended -- every JVM, whose launcher runs `main` on a thread of its own -- aborted on
  the GL renderers. The static handle registry destroyed the game on the exiting thread, whose
  thread-local GL function table is empty, and meta-gl terminates there. At exit the registry now
  leaves objects created by another thread to the operating system. Pinned by
  `CApi_ExitWithForeignThreadGameSmoke` (a game and a dynamic vertex buffer made on a thread that
  ends first; the process must exit 0 -- it aborted with 134 on OPENGLES3 before).
- **BINDFIX-042** — `cna_texture2d_set_data`/`_get_data` sized every transfer as one element per
  texel. XNA's `GetAndValidateSizes` lets an element narrower than the texel view it (a Color
  texture read as bytes is four elements per texel), and the canonical overload accepted that
  count, so a 16-byte read of a 2x2 Color texture returned 4 bytes and a 16-byte write stored 4 and
  zeroed the other three texels -- both reporting success (reported by `cna-ruby`). The region is
  now measured in the caller's elements, and `out_required_elements` reports it even when the
  count is too small. Pinned by `CApi_TextureSmoke` (byte-view round trip; per-width counts).
- **BINDFIX-043** — `cna-ruby` A3 / `cna-swift` SW-05: `cna_texture2d_create_from_encoded_memory`
  with `zoom` refused a target wider than tall over a square or tall image ("crop rectangle lies
  outside the source image"). The ported FNA3D rule picks the scale axis from the source's
  orientation; where its crop overflows, FNA3D's blit clips it and CNA threw. Zooming now scales by
  the larger ratio -- identical wherever the old crop fitted. The non-zoom (fit) path is unchanged;
  whether XNA's native decoder can exceed the requested height there (SW-05's second half) has no
  IL to settle it. A crop narrower than one source pixel (2x2 zoomed to 8x2) now keeps one pixel
  instead of truncating to an empty rectangle (reported by `cna-swift` after the first fix).
  Pinned by `ImageLoaderTests.ZoomFillsWideAndTallTargetsByCroppingTheOverflow`.
- **BINDFIX-044** — `System::InvalidCastException` (a compiled effect parameter read or written in a
  shape it does not have, FX-089/FX-105) fell through the exception barrier to
  `CNA_RESULT_INTERNAL`, which `docs/c-api/ERRORS.md` reserves for native failures with no public
  category; it is the caller's argument and now maps to `CNA_RESULT_INVALID_ARGUMENT` (reported by
  `cna-ruby`). Pinned by `CApi_BoundaryDetail`.
- **BINDFIX-045** — `RUST-UPSTREAM-030`: a technique added through
  `cna_effect_technique_collection_add_named`/`_add_default` to an effect's own collection was built
  with no owning `Effect`, so `cna_effect_set_current_technique` refused it as foreign
  (SOFTWARE-257's owner check) and a reflection-built effect could never select its technique. The
  add routes now pass the collection's owner. Pinned by `CApi_EffectSmoke`.
- **BINDFIX-046** — `RUST-UPSTREAM-031`: ending a process while the shared avatar loader thread was
  still assembling models segfaulted (28 of 30 runs) or hung for good. The first finished job built
  function-local lookup maps after the `Loader`, so `exit()` destroyed them before `~Loader` joined
  the worker still reading them. The two tables are now constant arrays that are never constructed
  or destroyed. Pinned by `CApi_AvatarExitWhileLoadingSmoke` (fails 16 of 20 runs before).
- **BINDFIX-047** — `cna-python` online finding 3 / `cna-rust` RUST-BEHAVIOR-010: the `cna_gamer_*`
  base routes refused a network-gamer handle ("does not name a gamer this call can use"), so a
  remote gamer's gamertag and display name were unreachable, though `NetworkGamer` is a `Gamer`.
  `BorrowGamerBase` now also resolves network gamers; `cna_gamer_destroy` is unchanged. Pinned by
  `CApi_NetSmoke`.
- **BINDFIX-048** — `cna-python`: `cnb.h` said `CNA_CNB_SOUND_EFFECT_SCHEMA_VERSION` is `1` and that
  the WAV importer refuses 24/32-bit and float, while the library has written schema 2 (8-bit PCM)
  since XNASWEEP-197 and narrows those widths to 16-bit. Constant and text corrected within 0.35.0;
  the baseline change is recorded in `ABI_VERSIONING.md`, approved by the owner (2026-09-30).
  Pinned by `AbiHeaderC.c` and `CApiAbiBaseline`.
- **BINDFIX-050** — `RUST-UPSTREAM-023`: six threads creating and destroying standalone devices at
  once aborted 28 of 40 runs on OPENGLES3 (heap corruption, occasionally a stall). Five unguarded
  process-wide paths, found one backtrace at a time: the preset state objects' shared identity
  list (`GraphicsResource`), window and GL-context teardown and the camera subsystem start outside
  the SDL lock, the adapter cache and SDL's display queries, and overlapping device construction.
  Each now has a lock (order: device lifecycle, adapter cache, SDL). 240 of 240 runs pass. Pinned
  by `CApi_ConcurrentDeviceCreationSmoke` (fails 10 of 10 runs against the previous library).
- **BINDFIX-049** — `net.h` still described `cna_packet_reader_read_color` as reading four floats and
  the pair as deliberately asymmetric; BINDFIX-022 had made them inverses. Documentation only
  (reported by `cna-python`).
- **BINDFIX-051** — `cna-ts` finding 2: the audio module wrote its diagnostics straight to stderr
  (28 sites, one of them a format line on every successful mixer open), and `ShaderEffect` its
  compile error, past `CNA::Logger` -- so a consumer that had replaced the destination with
  `cna_logger_set_sink_ext` still got them on stderr. They are now log lines in the `AUDIO` and
  `RENDER` categories: the mixer's negotiated-format announcement at INFO (as the renderer
  banner), load confirmations at DEBUG, advisories at WARN, failures at ERROR. Pinned by
  `SoundEffectTest.RawBufferDiagnosticReachesAnInstalledLoggerSinkAndNotStderr`.
- **BINDFIX-052** — `cna-ts` finding 32: on WebAssembly a caller-created `GraphicsDevice` made and
  destroyed while a Game was alive left the Game undestroyable -- `cna_game_destroy` threw
  Emscripten's `ErrnoError` (ENOENT) instead of returning. Each device is an SDL window on the same
  page; SDL's Emscripten backend creates the shared `/tmp/filedrop` drop directory inside a `try`
  but removed it unguarded, so the second window torn down threw out of `SDL_DestroyWindow`. CNA
  carries an Emscripten-only SDL patch guarding the removal as setup already does
  (`cmake/patches/sdl-cbe3fbe9-0003-emscripten-filedrop-rmdir.patch`; SDL `main` still has the
  bug). Pinned by the twelfth check of `modules/c-api/wasm/browser_probe.html`
  (`CApi_WasmBrowserProbe`), which fails 11/12 against the previous module.
- **BINDFIX-053** — `cna-ts` finding 30: on WEBGL2 a `BasicEffect` draw into two bound render
  targets painted neither and left `INVALID_OPERATION` pending. WebGL 2 refuses a draw that leaves
  an enabled draw buffer without a fragment output ("Active draw buffers with missing fragment
  shader outputs"); GLES 3.0 only leaves such a buffer undefined, which is why native OPENGLES3
  (Mesa) drew correctly all along. Every stock EasyGL program writes one output, so EasyGL now
  enables, for the length of each draw, only the draw buffers the program writes -- the stock
  programs' one, a compiled effect's `oCn` outputs from its pixel-shader reflection, a custom
  `ShaderEffect`'s whole set -- and restores the full set afterwards, so `Clear` still reaches every
  target. The untouched target stays unchanged, as in XNA. Pinned by the thirteenth check of
  `modules/c-api/wasm/browser_probe.html`, which fails 12/13 against the unfixed renderer.

---

## The rest of the backlog

Roughly 160 further open findings are recorded in the binding repositories. They are not
transcribed here, because each one's evidence — the probe source, the measured table, the
artifact SHA — lives with the binding that took it, and a copy here would go stale. What
follows is the index: where to look, and which rows carry a reproducer.

### Process-fatal

| what | where reported | reproducer |
|---|---|---|
| Content-loaded `Model` destroy faults; `~MeshResource` moves an absent `detachedValue` over a good pointer and `~PartResource` then calls `setTagProperty` on it with no null check | `cna-rust` `RUST-UPSTREAM-021`, `cna-java` `JAVA-UPSTREAM-004`, `cna-ruby` | `cna-rust/tools/reproducers/ext015g_load_model_destroy.c` (+ hand-built control), `cna-java/tools/native-abi/probes/content_manager_model_teardown.c` |
| `cna_camera_destroy` leaves a dangling process-wide platform override; the next camera route dereferences freed memory | `cna-rust` `RUST-UPSTREAM-020`, `cna-ts` (11), `cna-java` `JAVA-UPSTREAM-019` | `cna-java/tools/native-abi/probes/camera_test_backend.c` |
| Concurrent `cna_graphics_device_create` corrupts the heap; `Sdl3GlContext::CreateContext`'s mutex does not cover the whole construction | `cna-rust` `RUST-UPSTREAM-023` | `cna-rust/tools/reproducers/ext015h_concurrent_device_create.c` (13 aborts / 70) |
| A process exiting with a live vertex buffer aborts on the EasyGL renderers when the creating thread has ended — which every JVM satisfies | `cna-java` `JAVA-UPSTREAM-014` | `cna-java/tools/native-abi/probes/exit_with_live_graph.c` |
| An unavailable `CNA_GRAPHICS_RENDERER` aborts the process during library load, before any consumer frame exists | `cna-java` `JAVA-UPSTREAM-017` | `cna-java/tools/native-abi/probes/renderer_selection.c` |
| `cna_shader_effect_create` aborts on EasyGL/OPENGLES3 when handed desktop GLSL instead of the dialect the renderer declares | `cna-cs` | — |
| A second `Game` create/destroy cycle segfaults on OPENGL33 | `cna-cs` | two-line reproducer named in `docs/native-behavior-blockers.md` |
| A refused `cna_game_destroy` is not side-effect-free: the process then segfaults on the way out | `cna-ts` (18) | — |
| Shutdown on `SIGTERM` calls a pure virtual during native teardown | `cna-cs` | `scripts/Reproduce-SigtermShutdown.sh` |
| A virtual call into a `GraphicsDevice` whose derived destructor has run (UBSan + ASan agree) | `cna-cs` | reproduced on this repository's own `build-ubsan` and `build-asan` |

### Owned-handle accounting

`cna_post_process_chain_add_owned_pass` releases the handle without the matching
`RemoveOwnedGraphicsResourceFor`, so the game's owned-child count never falls and
`cna_game_destroy` refuses for the rest of the process — reported by `cna-ts` (1) and
`cna-java` (`JAVA-UPSTREAM-011`), with a precise fix and a reproducer
(`chain_owned_pass.c`). Several engine getters mint a fresh counted handle per read while
their headers say the handle is borrowed and must not be destroyed:
`cna_post_process_effect_pass_get_effect` is the one every binding names
(`cna-python` ENGINE-002, `cna-ts` 17, `cna-java`), and `cna_skybox_get_environment`
versus `cna_render_pipeline_get_skybox` document the same contract while only one of them
mints (`cna-ts` 15).

### Documentation that contradicts the code

The four clustered-lighting constructors name their first parameter `game` and resolve it
with `GetBorrowedGraphicsDevice`, so a real game handle is refused (`cna-python`
ENGINE-006, `cna-ts` 10, `cna-java` `JAVA-UPSTREAM-005`). `cna_compute_shader_create` is
documented to succeed for source that does not compile, and fails instead — which makes
`is_valid` and `copy_compile_error` unreachable for the case they exist for
(`cna-python` ENGINE-001, `cna-ts` 8, `cna-java` `JAVA-UPSTREAM-012`). Three `_init`
routes document identity transforms and write zero matrices (`cna-python` ENGINE-008,
`cna-ts` 23). `cna_sprite_batch_begin_with_effect` and `_with_states` document "or null
for AlphaBlend" and refuse null, one parameter at a time (`cna-ruby`). The weighted-blended
bracket's header describes behaviour the code deliberately changed (`cna-ts` 21).

### Two sources of truth

`cna_pbr_effect_apply_material` sets textures through the C++ object while
`set_texture`/`get_texture` use the C API's retained-handle table, so a material applied
through the ABI carries every scalar and no texture, `extract_material` never reports a
texture at all, and a read-modify-write silently unbinds every map. Reported by
`cna-python` (ENGINE-005), `cna-ts` (19) and `cna-java` (`JAVA-UPSTREAM-010`), all three
independently.

### Silent wrong answers

An upload into a render target reports success and is dropped (`cna-ruby`). A `Color`
written into a packet cannot be read back out of it — the writer writes four bytes and the
reader consumes sixteen (`cna-python`). A packet larger than the caller's buffer is cut
and reported as a success, and the `PacketReader` overload delivers every byte while
reporting none (`cna-rust` `RUST-UPSTREAM-028`). The GPU instance culler runs, reports
success and culls nothing (`cna-ts` 20). A `ShaderEffect`'s first draw through
`SpriteBatch` produces nothing (`cna-ts` 22). A uniform set before its shader effect is
applied is written to whichever program is current (`cna-java` `JAVA-UPSTREAM-016`). The
packed depth encoding divides by 256 where an 8-bit UNORM target quantises by 255, losing
every bit the packing bought (`cna-ts` 13). `cna_texture2d_create_from_encoded_memory`'s
cover-and-crop path refuses targets wider than tall while accepting taller than wide
(`cna-ruby`, `cna-swift`).

### Capability queries that do not predict behaviour

`cna-java` counts five instances by itself (`JAVA-UPSTREAM-005`, `-007`, `-015`, `-021`,
`-022`); `cna-cs` adds render-target readback and cube-face storage having no capability
identity at all, and `cna-ts` adds WEBGL2 advertising `MultipleRenderTargets` while a draw
into two bound targets reaches neither. `cna_graphics_device_get_shader_dialect_ext`
answers `UNKNOWN` on every renderer but WebGPU, while GLSL ES sources compile and draw.

### Arithmetic that disagrees with XNA

`SpriteFont::MeasureString` adds the last glyph's right side bearing unclamped, where
XNA's IL adds `Math.Max(pendingZ, 0)` — measured independently by `cna-ts` (27) and
`cna-go` (Foundation 69), both with tables. `GetSampleDuration` truncates the fractional
millisecond before `TimeSpan::FromMilliseconds` ever sees it, and `GetSampleSizeInBytes`
drops XNA's frame alignment and binary32 division (`cna-rust` `RUST-UPSTREAM-027`,
`cna-ruby`). The two stencil masks default to `Int32.MaxValue` where XNA's IL writes `-1`
(`cna-ruby`, `cna-common-lisp`, `cna-cs`). `cna_game_launch_parameters_add` documents
"adds or replaces", calls `emplace`, and so does neither (`cna-rust`
`RUST-UPSTREAM-026`, `cna-ruby`).

### Build and environment

`_bindings/fixcna-analysis.md` (2026-08-26, against `6319f30c5`) is the earlier
cross-binding analysis and still holds two prerequisites: the C-API test suite had six
red tests, and several rows in it were already stale when it was written. `cna-java`
`JAVA-UPSTREAM-003` reports five of this repository's own `CApi_*` smoke tests failing in
the HEADLESS/NULL-audio configuration. `cna-common-lisp` records that `next` at
`822d3b960` does not build against published `sharp-runtime:next`, because
`StorageDevice.cpp` calls `SetIsolatedStorageRootOverride`, which exists only on the
`next` branch of that repository — the same breakage this session hit, and the reason
`CMakeLists.txt`'s default of `../sharp-runtime` is worth revisiting.
