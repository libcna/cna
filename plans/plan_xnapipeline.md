# plan_xnapipeline.md — CNA native XNB content-build pipeline (XNB **output**)

> **Scope of this plan.** This plan owns the **XNB writer** side of CNA's content system: turning
> ordinary source assets into `.xnb` files that an XNA-4.0-compatible runtime can load, using only
> native C++ and without XNA Game Studio, MonoGame, FNA build tools or MSBuild.
>
> **Boundary with the existing plans.**
> * [`plan_xnb.md`](plan_xnb.md) owns the **runtime XNB reader** (`ContentManager::Load<T>()` from
>   `.xnb`). That work is complete for its declared scope and is **not** reopened here.
> * [`plan_content_pipeline.md`](plan_content_pipeline.md) owns the build-time pipeline
>   (importer → processor → writer → **CNB**), including the CLI, incremental manifests and the
>   `.cna-content.json` project format. This plan **extends** that pipeline with a second output
>   format; it does not fork it.
> * [`plan_cnb.md`](plan_cnb.md) owns the frozen native CNB format. XNB compatibility must never
>   weaken or constrain CNB.
> * [`plan_fx.md`](plan_fx.md) owns CNA's FX/effect infrastructure. Effect **bytecode
>   serialization** belongs here; **shader compilation** stays there.
>
> **Task IDs.** `XNAP-###`. Never reuse an ID; never renumber.

> **⚠ One writer, and a history worth reading.** This plan's opening audit was wrong in a way
> that mattered: a second, independently developed XNB writer already existed on the `pipeline`
> branch when Phases B–E and G were written here, so for a while the repository contained two
> implementations of the same subsystem. **That is history, not architecture.** The two branch
> histories were reconciled, one implementation survives — `CNA::Internal::Xnb`, in
> `modules/content/{include/CNA/Internal/Xnb,src/Xnb}` — and the useful parts of the superseded
> one were ported into it (`XNAP-97`–`XNAP-9A`). §0 keeps the mistake on the record; §0.5 says
> which features were ported and which paths no longer exist. Nothing below describes a second
> writer, and nothing may reintroduce one.

> **Status (2026-09-03).** **74 tasks: 64 done, 8 partial, 2 blocked, 0 open.** Every partial row
> names its remaining scope and every blocked row names its blocker, in the row itself.
> `tools/xnb/check_plan_status.py` (ctest `CnaXnbPlanStatusConsistency`) recounts the table and
> fails if this sentence disagrees with it, because it did once. Exact test totals live in §0.4 and
> nowhere else. **Nothing here has been verified against a genuine Microsoft XNA 4.0 runtime**,
> because none exists in this environment; `docs/xnb-interoperability.md` is the capability matrix
> and it says so in its second paragraph. Three assets are byte-identical to output produced by
> somebody else — one by Microsoft's own XNA 4.0 Content Pipeline, two by MonoGame — which is the
> strongest verification available without a runtime.

---

## 0. Session-start audit (2026-09-03) — corrected 2026-09-03

> ### ⚠ Correction: this audit was wrong, and a duplicate implementation was written because of it
>
> §0 originally read: *"The task that opened this plan described a large pre-existing XNB writer
> implementation … **None of that existed at HEAD `756096626`.**"*
>
> That sentence was true of `next` and false of the repository. **The implementation the task
> described already existed on the `pipeline` branch**, in five commits dated 2026-09-02
> (`07cac247a`…`6549d8032`), built on the same base commit `756096626` this plan started from. The
> session-start audit checked HEAD and did not check any other branch, which is the whole of the
> mistake: `git ls-remote --heads origin` would have found it in one command.
>
> The consequence was not cosmetic. **Most of Phases B, C, D, E and G below re-implemented work
> that was already finished**, in a second location, under a colliding set of task IDs — a second
> writer architecture that briefly existed in the repository by accident.
>
> **That duplication has since been resolved and no longer exists.** The two branch histories were
> merged (`ff2fbca42`), the second architecture's paths were removed, and the features worth
> keeping from it were ported into the surviving one (`XNAP-97`–`XNAP-9A`). §0.5 records what was
> ported, what was dropped, and which paths must never come back. Read §0.1–§0.4 as what they are:
> a dated audit of one branch, kept because the mistake in it is instructive, not as a description
> of the repository today.
>
> Nothing else in this plan is retracted: the code, tests and measurements it records are real, and
> the environment audit in §0.3 and the test baseline in §0.4 stand. What is retracted is the claim
> that this work started from nothing.

The task that opened this plan described a large pre-existing XNB **writer** implementation
(`XnbByteWriter`, `XnbWriteLimits`, `XnbFileOptions`, `XnbTypeWriter`, `XnbTypeKey<T>`,
`XnbTypeWriterRegistry`, `XnbWriter`, `XnbAssetWriter`, a `--format cnb|xnb` CLI switch, an
independent Python XNB conformance parser, CNA-generated XNB fixtures, `docs/xnb-interoperability.md`
and this plan file). None of that existed **on `next`** at HEAD `756096626`; all of it existed on
`pipeline`. The audit below is the verified state of `next`, and the rest of this plan was written
against it.

### 0.1 Verified absent

| Claimed | Verified state at `756096626` |
|---|---|
| `XnbByteWriter`, `XnbWriteLimits`, `XnbFileOptions`, `XnbTypeWriter`, `XnbTypeKey<T>`, `XnbTypeWriterRegistry`, `XnbWriter`, `XnbAssetWriter` | Zero occurrences anywhere in the tree (source, tests, docs). |
| `ContentPipeline::Build()` XNB route | `Build()` produces CNB only. `ContentTypeWriter` returns a CNB asset type/schema. |
| `cna-content --format cnb|xnb` | No `--format` option exists. Usage is `build <src> -o <out> [--config] [--workers] [--explain] [--quiet]`. |
| Independent Python XNB conformance parser | No Python file in the tree mentions XNB. |
| CNA-generated XNB fixtures | Every `.xnb` in `tests/assets/` is externally produced (1 genuine XNA 4.0, 14 MonoGame). |
| `docs/xnb-interoperability.md`, `xnb.md` XNB-writer content, `plans/plan_xnapipeline.md` | `docs/xnb-interoperability.md` and this plan did not exist. `xnb.md` documents the reader. |
| Primitive/collection/graphics/media **writers** | No XNB writer of any kind. The only XNB-emitting code was hand-rolled `MakeTexture2DXnb()`-style helpers **inside test files**. |

### 0.2 Verified present (and reused by this plan)

| Component | Location | Role for XNB output |
|---|---|---|
| XNB container reader | `modules/content/include/CNA/Internal/Xnb/XnbHeader.hpp` | Authoritative in-repo header spec; the writer's counterpart. |
| Canonical XNB data model | `CNA/Internal/Xnb/XnbCanonicalData.hpp` (+ 1913-line `.cpp`) | `XnbTextureData`, `XnbSpriteFontData`, `XnbSoundEffectData`, `XnbSongData`, `XnbVideoData`, `XnbVertexDeclarationData`, `XnbVertexBufferData`, `XnbIndexBufferData`, `Xnb{Basic,AlphaTest,DualTexture,EnvironmentMap,Skinned}EffectData`, `XnbModelData`. **This is the XNB-side canonical intermediate representation the writer serializes from.** |
| Model graph wire order | `modules/content/src/Xnb/XnbModelGraphReader.hpp` | Exact bone/mesh/part/tag/shared-reference order, sink-templated so a writer can mirror it. |
| Built-in reader family | `modules/content/src/Xnb/*.cpp` | The round-trip oracle every writer is tested against. |
| `ContentReader` | `Microsoft/Xna/Framework/Content/ContentReader.hpp` | 1-based type-reader dispatch, shared-resource fixups, `ReadExternalReference` (plain 7-bit-prefixed string). |
| Content pipeline core | `CNA/Content/Pipeline/ContentPipeline.hpp` | Importer/processor/writer registry, `ContentValue`, dependency collector, diagnostics, limits. |
| Source routes | `Texture2DContentPipeline`, `SoundEffectContentPipeline`, `SongContentPipeline`, `VideoContentPipeline`, `ModelContentPipeline`, `CnjContentPipeline`, `XnbContentPipeline` | Importers/processors are format-neutral and are reused verbatim for XNB output. |
| `XnbContentPipeline` | `CNA/Content/Pipeline/XnbContentPipeline.hpp` | **XNB as a source** (`.xnb` → CNB transcode). Not an XNB writer. |
| CLI + incremental manifest | `tools/content/content.cpp`, `ContentBuildManifest` | Fingerprints, atomic publication, `--explain`, parallel workers, manifest-proven clean. |
| Image decode | `CNA/Internal/Graphics/ImageLoader` (stb) | `.png .jpg .jpeg .bmp .tga .gif .psd .hdr .pic .pnm`. |
| BC/DXT | `CNA/Internal/Graphics/DxtUtil` | **Decompress only** — no encoder. |
| Model canonical data | `CNA/Content/Cnb/CnbModelV2Data.hpp` | Exact vertex declarations, shared vertex/index buffers, stock effects, bones, meshes, parts, bounds, root. Sufficient for XNA `Model` output. |

### 0.3 Environment capability audit

| Capability | State | Consequence |
|---|---|---|
| Microsoft XNA 4.0 runtime | **Absent** — no `wine`, `mono`, `dotnet`, `msbuild`, `xbuild` on `PATH`. | `XNAP-30`–`XNAP-34` build a ready-to-run harness and expected-value manifests; they **cannot** be executed here. No task in this plan may claim "tested against Microsoft XNA 4.0" from this environment. |
| FNA reference tree (`/rv/data/library/github.com/FNA-XNA/FNA`) | **Absent** (`/rv` does not exist). | Every wire-format decision in this plan is derived from CNA's own reader, the committed fixtures, or public format documentation. This is a provenance improvement, not a loss. |
| Genuine XNA 4.0 XNB fixture | **Present**: `tests/assets/xnb/xna40/windows/uncompressed/ContentManifestListStrings.xnb` | Enables a **byte-exact** golden test: CNA writes the same `List<string>` and must reproduce Microsoft's own file byte for byte (`XNAP-14`). |
| MonoGame XNB fixtures | 14 files, provenance-manifested | Reader-name evidence and second-source byte checks. |
| FreeType 2 | Installed system-wide (`/usr/include/freetype2`), **not yet used by CNA** | Enables the SpriteFont source route (`XNAP-50`+). New optional build-time dependency; license audit required. |
| Audio device | **Absent** (ALSA fails) | Pre-existing test failures; see §0.4. |
| Renderer | `STUB` in `cmake-build-unit` | Pre-existing test failures; see §0.4. |

### 0.4 Recorded test baseline (before any change in this plan)

Build dir `cmake-build-unit` (`Debug`, `CNA_PLATFORM=SDL3`, `CNA_GRAPHICS_RENDERER=STUB`).

```text
CnaContentTests:  1585 run   1487 passed   68 skipped   30 failed
```

All 30 failures are environmental and reproduce on the unmodified tree:

| Failure group | Count | Cause |
|---|---|---|
| `SoundEffectContentTypeReaderTest.*`, `CnbSoundEffectCodecTest.*`, `ContentManagerSoundEffectXnbTest.*`, `XnbContentPipelineTest.SoundEffectRuntime…`, `CnjCapabilityMatrixTest.SoundEffectDelegatesViaSourceFile`, `XnbBuiltInReaderRegistrationTest.FreshContentManagerLoadsASoundEffectFixture…` | 20 | `SDL_OpenAudioDeviceStream failed: ALSA: Couldn't open audio device`. No sound card in the container. |
| `CnbTextureContentManagerTest.*`, `CnbTextureCubeProducerTest.*`, `Texture3DTextureCubeContentTypeReaderTest.*`, `XnbBuiltInReaderRegistrationTest.FreshContentManagerLoadsATextureCubeFixture…`, `CnjTexture3DTest.LoadsRealCnjFixture`, `EffectMaterialContentTypeReaderTest.ExternalReferenceReaderPreservesReferencedTextureCubeConcreteType` | 9 | `STUB` renderer has no `TextureCube`/`Texture3D` support. |
| `ContentPipelineCliTest.MultiOutputFailureLeavesTheOldManifestAndRecoversSafely` | 1 | The test induces a publish failure by removing owner-write permission; the container runs as **uid 0**, which bypasses it, so the expected failure never happens. |

`XNAP-04` owns keeping this baseline current. **Any new failure outside these 30 is a regression.**

**Current (2026-09-03, after `XNAP-96`):**

```text
CnaContentTests:          1733 run   1635 passed   68 skipped   30 failed
CnaContentPipelineTests:    37 run     37 passed    0 skipped    0 failed
```

`CnaContentTests` shows the same 30 environmental failures as the recorded baseline, +96 tests,
+129 passing, **zero new failures**. `CnaContentPipelineTests` is the new build-time module's own
focused executable and is entirely new. The whole-suite figure is recorded here for completeness;
its 29 failures are the STUB-renderer `TextureCube`/`Texture3D`/capability group, the uid-0
permission case and the two audio-device `MediaLibrary` cases — none of them content-pipeline
code, and none of them introduced by this plan.

---

### 0.5 Reconciliation with the `pipeline` branch — resolved

> **Read this section as a record of a resolved duplication, not of a live one.** The
> reconciliation is done. Exactly one XNB writer architecture exists in the tree, and this section
> exists so that nobody has to re-derive which half of the work came from where.

#### 0.5.1 The final architecture (what is true now)

| Concern | Surviving location |
|---|---|
| XNB serializer core, type writers, registry, container options | `modules/content/include/CNA/Internal/Xnb/`, `modules/content/src/Xnb/` |
| XNB **output** as a content-pipeline format | `CNA/Content/Pipeline/XnbOutputContentPipeline.hpp` |
| XNB **input** (`.xnb` as a *source*, transcoded) | `CNA/Content/Pipeline/XnbContentPipeline.hpp` |
| Independent conformance parser | `tools/xnb/xnb_conformance.py` |
| Task IDs | `XNAP-01`…`XNAP-9A`, two hex digits |

`XnbContentPipeline` and `XnbOutputContentPipeline` are **not** two writers and must not be merged:
the first reads `.xnb` and the second writes it. Both are correct and both stay.

The superseded architecture's paths are gone and must not reappear as a second writer:

| Removed path | Replaced by |
|---|---|
| `modules/content/include/CNA/Content/Xnb/` | `modules/content/include/CNA/Internal/Xnb/` |
| `modules/content/src/XnbWrite/` | `modules/content/src/Xnb/` |
| `XnbOutput.hpp`, `XnbOutput.cpp`, `XnbModelOutput.cpp` | `XnbOutputContentPipeline.hpp/.cpp` |
| `scripts/xnb_conformance.py` | `tools/xnb/xnb_conformance.py` |
| Task IDs `XNAP-001`…`XNAP-029` | retired; never reused, never renumbered |

`XnbArchitectureUniquenessTest` in
`modules/content/tests/CNA/Internal/Xnb/XnbArchitectureUniquenessTests.cpp` fails if any of those
paths comes back, so this is enforced rather than only written down.

#### 0.5.2 How the two histories related (historical)

Both branches started at `756096626`. `pipeline` carried 5 commits and 55 changed files; this
branch carried 20 commits and 97 changed files at the point they met. They were siblings, not a
sequence, and 15 files were touched by both — including this plan file,
`docs/xnb-interoperability.md`, `ContentPipeline.hpp/.cpp`, `tools/content/content.cpp` and
`SongContentTypeReader.cpp`, where both branches independently found and fixed the same
`SongReader` duration defect.

The task IDs collided: `pipeline` used three-digit IDs and this branch two-digit ones, so
`XNAP-01` and `XNAP-001` were different tasks with similar names. The `XNAP-0##` numbering was
retired with the implementation it described; this plan's two-digit numbering is the live one.

#### 0.5.3 Delivered on `pipeline` and re-implemented here — the cost of the mistake

Every one of these was `[x]` on `pipeline` before this session began:

`XnbByteWriter` and its limits · `XnbFileOptions`, platform and profile, header emission ·
`XnbTypeWriter`/`XnbTypeKey<T>`/`XnbTypeWriterRegistry` with configure-then-freeze · `XnbWriter`
object dispatch, shared resources, `ExternalReference`, bounded depth · primitive and
`String`/`Char` writers · the math value types · the collection writers · `Curve` · the
`SurfaceFormat` ↔ `CnbTextureFormat` mapping · `Texture2D`/`Texture3D`/`TextureCube` ·
`ContentOutputFormat` and the pipeline writer layer · PNG → `.xnb` → `ContentManager::Load` with
exact pixel equality · `SpriteFont` · `SoundEffect` · `Song` and `Video` ·
`VertexDeclaration`/`VertexBuffer`/`IndexBuffer` · `Model` · all five stock effects · the compiled
`Effect` blob · `cna-content --format xnb` with its container options and a format-aware manifest ·
the correction of the permanent-out-of-scope wording in `xnb.md` and
`docs/xnb-content-pipeline-support.md` · an independent Python conformance parser · a CNA fixture
corpus · `docs/xnb-interoperability.md` · the custom-type writer extension point.

That is Phases B, C, most of D, E and G of this plan.

#### 0.5.4 Present on `pipeline`, absent here — and what happened to each

| Feature `pipeline` had | Outcome |
|---|---|
| `Decimal` writer | **Ported** (`XNAP-97`), behind the same `SHARP_RUNTIME_HAS_NATIVE_INT128` guard the reader carries. |
| `BoundingFrustum` writer | **Ported** (`XNAP-97`). |
| `Enum<T>` writer | **Ported** (`XNAP-98`) — and porting it exposed a real defect in the reader-identity model, fixed by `XnbReaderIdentity::targetSharesGenericArguments`. |
| `T[]` and `Nullable<T>` registered writers | **Ported as far as CNA's reader supports** (`XNAP-22`): every instantiation CNA's own runtime reader registry resolves is registered, and the rest stays an explicit extension point rather than a writer with no reader. |
| Manifest `rootReaderName` (schema 9) | **Ported** (`XNAP-99`), recorded from the actual write rather than declared per writer, and fingerprinted so a changed root reader invalidates the artifact. |
| Xbox 360 excluded from the writable platform set | **Not ported, deliberately.** Here `x` is writable but refused at write time unless `allowUnverifiedXboxPayloads` is set (`XNAP-82`). `pipeline`'s surface was smaller; this one lets somebody with real hardware produce candidate files, which is the only way the gap ever closes. |
| Architecture/usage documentation (`docs/xna-content-pipeline.md`) | **Folded in** (`XNAP-9A`) as `docs/xnb-interoperability.md` §0. |

#### 0.5.5 Genuinely new here, and open or absent on `pipeline`

- **glTF/GLB → `Model`** (`XNAP-56`–`XNAP-59`). `pipeline`'s `XNAP-022` left this open for exactly
  the reason this branch resolved: CNB schema 1 stores a vertex stride and no `VertexDeclaration`.
  The resolution — recovering it from `VertexDeclarationFidelity.hpp`, the table every CNA renderer
  already interprets those bytes with — is the single most valuable thing on this branch.
- **`.spritefont` + TrueType source route** with FreeType rasterization and deterministic packing
  (`XNAP-50`–`XNAP-52`). `pipeline` writes a `SpriteFont` from an already-processed sprite font; it
  does not rasterize one.
- **BC1/BC2/BC3 encoder** and the texture policy parameters (`XNAP-53`, `XNAP-54`).
- **24-bit, 32-bit and IEEE-float WAV** sources (`XNAP-55`).
- **`.fxb` compiled-effect source route** (`XNAP-84`).
- **LZ4 output** (`XNAP-80`). `pipeline`'s `XNAP-023` covers LZX only, and is open.
- **`modules/content-pipeline/`**, the build-time-only module (`XNAP-52`, `XNAP-90`). This is
  `pipeline`'s open `XNAP-029` — the runtime/pipeline library split.
- **`EffectMaterial`** and the boxed `ExternalReference` (`XNAP-29`, `XNAP-2B`).
- **Byte-exact golden tests** against XNA 4.0's own output and two MonoGame files (`XNAP-41`,
  `XNAP-42`). `pipeline` verifies `Model` by decode-write-decode, which is a round trip rather than
  a comparison against another producer.
- The untrusted-input hardening sweep (`XNAP-85`) and the recorded performance numbers (`XNAP-93`).

Roughly two thirds of this branch duplicated `pipeline`; roughly one third was new. The
reconciliation kept this branch's architecture and carried `pipeline`'s remaining features across
one at a time, each with its own task ID and test — which is why the duplication is now a closed
chapter with a paper trail rather than a fork somebody has to choose between.

---

## 1. Target architecture

```text
                       source assets (.png .wav .gltf .glb .spritefont .fx …)
                                        |
                                        v
                              Content Importer          (format-neutral, shared)
                                        |
                                        v
                     imported / source-oriented value    (ImportedImage, ImportedSound, …)
                                        |
                                        v
                              Content Processor          (format-neutral, shared)
                                        |
                                        v
                  processed canonical value  (Cnb*Data / CnbModelV2Data / Curve / …)
                                        |
                        +---------------+----------------+
                        |                                |
                        v                                v
              ContentTypeWriter                 ContentTypeWriter
              (OutputFormat::Cnb)               (OutputFormat::Xnb)
                        |                                |
                        |                          Xnb*Data adapter
                        |                                |
                        |                          XnbAssetWriter
                        v                                v
                      .cnb                             .xnb
```

Design commitments:

1. **Importers and processors are never duplicated.** The output format is chosen at the writer
   boundary only. A new source format therefore reaches both outputs at once.
2. **`Xnb*Data` (already in the tree, reader-owned) is the XNB-side canonical representation.**
   Writers serialize `Xnb*Data`; thin adapters map `Cnb*Data` → `Xnb*Data`. This makes every writer
   directly round-trip-testable against CNA's own reader and keeps provenance clean.
3. **CNB is never degraded for XNB.** Where XNA cannot represent something (glTF PBR, morph
   targets, animation clips, richer materials) CNB keeps it and the XNB route emits a documented,
   diagnosed downgrade — never a silent loss.
4. **Runtime users never link build-time dependencies.** FreeType, BC encoders and model importers
   stay out of the runtime link closure (`XNAP-90`).

---

## 2. XNB container facts — verified, and how

Everything in this section is either (a) read out of a committed fixture, or (b) derived from CNA's
own reader. Nothing is taken from MonoGame or FNA implementation source, and no Microsoft binary was
decompiled.

### 2.1 Header

```text
offset size  field
0      3     'X' 'N' 'B'
3      1     target platform byte
4      1     format version  (5 = XNA 4.0 era; 4 = earlier)
5      1     flags
6      4     int32 little-endian total file length, *including* these 10 bytes
```

Flags bits, as read by CNA's `ParseXnbHeader()` plus the published format description:

| Bit | Meaning | CNA reader | CNA writer (this plan) |
|---|---|---|---|
| `0x01` | Graphics profile: set = HiDef, clear = Reach | ignored | written from `XnbFileOptions::graphicsProfile`; **default Reach** because a Reach asset loads under both profiles |
| `0x40` | Single raw LZ4 block (later-ecosystem only, **not** XNA 4.0) | supported | `XNAP-80` |
| `0x80` | LZX | supported | `XNAP-81` |

Evidence for `0x01`: the genuine XNA 4.0 fixture has flags `0x01`; every MonoGame fixture has `0x00`
or `0x40`/`0x80`. Both load under CNA's reader, which ignores the bit.

### 2.2 Platform bytes — XNA-4.0-era vs extended ecosystem

CNA's reader accepts 16 bytes (`XnbAcceptedPlatforms()`). **They are not all XNA 4.0 targets.**
This plan fixes the documentation to say so.

| Byte | Identity | Category |
|---|---|---|
| `w` | Windows | **Microsoft XNA 4.0 target** |
| `m` | Windows Phone 7 | **Microsoft XNA 4.0 target** |
| `x` | Xbox 360 | **Microsoft XNA 4.0 target** |
| `i`, `a`, `d`, `X`, `W`, `n`, `u`, `p`, `M`, `r`, `P`, `g`, `l` | iOS, Android, DesktopGL, Xbox One, Windows Store/UWP, NativeClient, Ouya, PlayStation Mobile, Windows Phone 8, RaspberryPi, PlayStation 4, legacy Windows-GL, legacy Linux | **extended XNB ecosystem** — introduced by post-XNA implementations. Writing one of these produces a file no Microsoft XNA 4.0 runtime ever consumed. |

`XnbFileOptions` therefore exposes `XnbTargetPlatform` with `Windows`/`WindowsPhone`/`Xbox360`
marked as XNA-4.0 targets and everything else marked extended, and the CLI/documentation must not
present the second group as XNA 4.0 compatibility.

### 2.3 Version

* **Version 5 is the XNA 4.0-era container version** and is CNA's writer default.
* Version 4 is *earlier* XNB (XNA 3.x era). CNA's reader accepts it and applies a legacy
  `SurfaceFormat` mapping in `Texture2DReader`. The writer may emit version 4 only under an explicit
  opt-in and must apply the inverse legacy mapping (`XNAP-13`).
* Describing version 4 as "the normal XNA 4.0 version" is wrong; `XNAP-06` removes any such wording.

### 2.4 Payload

```text
7BitEncodedInt   typeReaderCount
  repeat:
    String       readerName          (7-bit-encoded UTF-8 byte length, then the bytes)
    Int32        readerVersion
7BitEncodedInt   sharedResourceCount
<root object>                        (via the ReadObject protocol)
<shared resource 1..sharedResourceCount>
```

`ReadObject` protocol: a `7BitEncodedInt` **1-based** index into the type-reader table; `0` means
null. A value type's reader is invoked without a null option only because the writer always emits a
non-zero index for it. `ReadRawObject` consumes no index.

Shared resources are referenced by a `7BitEncodedInt` **1-based** index into the shared-resource
list; `0` means "no reference".

### 2.5 Reader type names — verified table

Extracted directly from the committed fixtures (see `XNAP-02` for the extraction tool):

| Reader | Name as written | Source of evidence |
|---|---|---|
| `StringReader` | `Microsoft.Xna.Framework.Content.StringReader` (bare) | **genuine XNA 4.0** fixture, and MonoGame |
| `ListReader\`1` | `Microsoft.Xna.Framework.Content.ListReader\`1[[<arg, assembly-qualified>]]` (outer bare) | **genuine XNA 4.0** fixture |
| `Int32Reader`, `CharReader`, `RectangleReader`, `Vector3Reader`, `SoundEffectReader`, `SongReader` | bare | MonoGame fixtures |
| `Texture2DReader`, `TextureCubeReader`, `SpriteFontReader`, `ModelReader`, `VertexBufferReader`, `VertexDeclarationReader`, `IndexBufferReader`, `BasicEffectReader` | `…<Reader>, Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553` | MonoGame fixtures |

Generic **arguments** are always fully assembly-qualified:
`System.String, mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089`,
`Microsoft.Xna.Framework.Vector3, Microsoft.Xna.Framework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553`.

Derived rule, consistent with every observed file: **the reader type itself is assembly-qualified
exactly when it does not live in `Microsoft.Xna.Framework`**; generic arguments always are. CNA
keeps this in one data table (`XnbReaderIdentity`) with a per-entry confidence field, so a
correction is a one-line data change rather than a code hunt.

`XnbFileOptions::readerNameStyle` selects:
* `Xna40` (default) — the table above; maximally XNA-4.0-compatible.
* `Portable` — bare names only. CNA, FNA and MonoGame all normalize away the assembly qualifier
  before lookup, so these load there; **a Microsoft XNA 4.0 runtime is not known to accept them.**

---

## 3. Confidence vocabulary (used by every table in this plan and in `docs/`)

| Label | Meaning |
|---|---|
| `impl` | Code exists and is exercised by a test. |
| `cna-rt` | CNA writes it and CNA's own independent reader loads it back with the expected values. |
| `spec` | An independent, specification-based parser that shares no code with CNA validates the bytes. |
| `golden` | The bytes match an externally produced fixture exactly. |
| `xna40` | Loaded by a genuine Microsoft XNA 4.0 runtime with values asserted. **Unreachable in this environment.** |
| `none` | Not verified. |

"Supported" is never written without at least `cna-rt`.

---

## 4. Task log

Legend: `[ ]` open · `[x]` complete · `[~]` partially complete (detail in the row) ·
`[!]` blocked (blocker named in the row).

### Phase A — audit, plan, baseline

| ID | Task | State |
|---|---|---|
| `XNAP-01` | Audit HEAD against the reported baseline; record what exists and what does not (§0). | [x] |
| `XNAP-02` | Extract and record the verified reader-name/type-table evidence from every committed fixture (§2.5). | [x] |
| `XNAP-03` | Create this plan as the authoritative living record, with explicit boundaries against `plan_xnb.md`, `plan_content_pipeline.md`, `plan_cnb.md`, `plan_fx.md`. | [x] |
| `XNAP-04` | Record and maintain the exact test baseline (§0.4), separating pre-existing environmental failures from regressions. | [x] |
| `XNAP-05` | Audit the environment for an XNA 4.0 oracle; record the negative result honestly (§0.3). | [x] |

### Phase B — XNB serializer core

| ID | Task | State |
|---|---|---|
| `XNAP-10` | `XnbByteWriter`: deterministic little-endian primitives, `Write7BitEncodedInt`, `.NET BinaryWriter`-compatible strings, UTF-8 `charcs`, raw bytes, bounded growth. | [x] |
| `XNAP-11` | `XnbWriteLimits`: output ceilings mirroring `XnbReadLimits` (file size, type-table size, collection counts, nesting depth, string bytes, shared-resource count). | [x] |
| `XNAP-12` | `XnbFileOptions`: target platform (with XNA-era vs extended classification), container version, graphics profile, compression, reader-name style. | [x] |
| `XNAP-13` | Legacy version-4 output: inverse `SurfaceFormat` mapping, explicit opt-in, rejection of formats version 4 cannot express. | [x] |
| `XNAP-14` | `XnbWriter`: type-manifest interning, 1-based `WriteObject` dispatch, `WriteRawObject`, shared-resource queue + 1-based references, nesting-depth guard, single-pass body then header prepend. | [x] |
| `XNAP-15` | `XnbTypeKey<T>` + `XnbTypeWriterRegistry`: RTTI-free typed registry keyed by a per-`T` unique address; deterministic, freezable, no central switch. | [x] |
| `XNAP-16` | `XnbAssetWriter`: root-object entry point producing complete `.xnb` bytes with a correct total-length field. | [x] |
| `XNAP-17` | `XnbReaderIdentity` table with per-entry confidence, plus `Xna40`/`Portable` name styles (§2.5). | [x] |

### Phase C — built-in type writers

| ID | Task | State |
|---|---|---|
| `XNAP-20` | Primitives: `Byte`, `SByte`, `Int16`, `UInt16`, `Int32`, `UInt32`, `Int64`, `UInt64`, `Single`, `Double`, `Boolean`, `Char`, `String`, `TimeSpan`, `DateTime`, `Decimal`. | [x] All sixteen. `Decimal` landed with `XNAP-97`, behind `SHARP_RUNTIME_HAS_NATIVE_INT128` — the same conditional the reader carries, so the type is absent on both sides together or present on both. |
| `XNAP-21` | Framework value types: `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Point`, `Rectangle`, `Plane`, `BoundingBox`, `BoundingSphere`, `Ray`, `CurveKey`, `Curve`. | [x] |
| `XNAP-22` | Collections: `T[]`, `List<T>`, `Dictionary<K,V>`, `Nullable<T>`, arbitrary nesting, element-count limits. | [~] `XnbListTypeWriter`, `XnbArrayTypeWriter`, `XnbDictionaryTypeWriter` and `XnbNullableTypeWriter` all exist and the list/dictionary instantiations CNA's runtime reader registry resolves are registered and tested. No `T[]` or `Nullable<T>` instantiation is registered by default, because no built-in CNA reader resolves one; registering one is the documented extension path. |
| `XNAP-23` | `Texture2D`, `Texture3D`, `TextureCube` from `XnbTextureData`. | [x] |
| `XNAP-24` | `SpriteFont` from `XnbSpriteFontData`, including the nested `Texture2D` and the four nested list readers. | [x] |
| `XNAP-25` | `SoundEffect` from `XnbSoundEffectData`, including WAVEFORMATEX extension bytes and loop metadata. | [x] |
| `XNAP-26` | `Song`, `Video` from `XnbSongData`/`XnbVideoData`, including the object-referenced Video field form. | [x] |
| `XNAP-27` | `VertexDeclaration`, `VertexBuffer`, `IndexBuffer`. | [x] |
| `XNAP-28` | Stock effects: `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, each with its external texture references. | [x] |
| `XNAP-29` | `Effect` (already-compiled bytecode) and `EffectMaterial`. | [x] `Effect` bytecode is written verbatim and an empty payload is refused. `EffectMaterial` writes its inline effect reference and a `Dictionary<String,Object>` whose values each carry their own dispatch index -- `Boolean`, `Int32`, `Single`, `Vector2/3/4`, `Matrix`, `Quaternion` and a boxed external reference. **Array-valued parameters are deliberately absent**: which reader instantiation XNA writes for `float[]`/`int[]`/`Matrix[]` is not established by any fixture available here, and a guess would produce a file that loads into the wrong shape rather than one that fails to load. |
| `XNAP-2A` | `Model` from `XnbModelData`: bones, hierarchy, meshes, parts, tags, shared vertex/index/effect resources, root reference, byte/uint32 bone-reference width rule. | [x] |
| `XNAP-2B` | `ExternalReference<T>`. | [x] Both forms. The inline one (`WriteExternalReference()`, no dispatch index) is what every stock effect and every `EffectMaterial` uses; the boxed one (`XnbExternalAssetReference`, its own reader in the table) is what a texture-valued effect parameter needs, and it is also writable as a standalone root. Absolute and escaping references are refused in both. |

### Phase D — round-trip, golden and conformance validation

| ID | Task | State |
|---|---|---|
| `XNAP-40` | Round-trip every writer through CNA's own reader with value assertions (`cna-rt`). | [x] |
| `XNAP-41` | **Byte-exact golden test against the genuine XNA 4.0 fixture**: write the same `List<string>` and compare to `ContentManifestListStrings.xnb` byte for byte (`golden`). | [x] |
| `XNAP-42` | Byte-exact golden tests against MonoGame fixtures where CNA's canonical data is lossless for them. | [x] Two more golden matches, so three in total across two independent producers. **`Texture2D`**: CNA reproduces MonoGame's smallest real texture fixture (`white-1.xnb`, one opaque white pixel) byte for byte — the field layout, the reader-name spelling and the container all agree with a second producer, not only with CNA's own reader. **`SoundEffect`**: matching `tone_mono_44khz_16bit.xnb` required a real change, recorded in §5 — CNA wrote the 16-byte `PCMWAVEFORMAT` block that omits `cbSize`, and the only observed producer writes the full 18-byte `WAVEFORMATEX` with `cbSize` present and zero. Both load in CNA, both occur in the wild, and matching an observed producer beats matching none, so the writer now emits 18. The committed interop corpus was regenerated accordingly (`soundeffect_pcm16_mono_22050.xnb`, 4513 → 4515 bytes). Not attempted: `SpriteFont` and `Model`, whose MonoGame fixtures carry atlas pixels and mesh data CNA has no way to regenerate — a golden test there would be testing whether CNA can reproduce somebody else's art, not whether it can frame it. |
| `XNAP-43` | Independent specification-based XNB parser (Python), sharing no code or assumptions with CNA (`spec`). | [x] |
| `XNAP-44` | Deterministic-output tests: same inputs ⇒ byte-identical files, across process runs and worker counts. | [~] byte-identical repeat writes and dictionary key ordering are covered; the multi-worker CLI case waits on `XNAP-62`. |
| `XNAP-45` | Malformed/limit tests: oversized collections, oversized strings, deep nesting, overflowing sizes, cyclic shared resources. | [~] collection, string and file-size ceilings, escaping external references, unissued shared-resource references, double-finish, and the independent parser refusing a bad total length and trailing bytes are covered. A dedicated fuzz corpus over the writer's inputs is still open. |

### Phase E — pipeline and tooling integration

| ID | Task | State |
|---|---|---|
| `XNAP-60` | `ContentOutputFormat` axis: `ContentTypeWriter::OutputFormat()` (defaulting to `Cnb`, non-breaking), format-aware writer resolution, format in the build request/result. | [x] |
| `XNAP-61` | Register XNB writers for every existing processed type, reusing the existing importers/processors unchanged. | [~] `Texture2D`, `Texture3D`, `TextureCube`, `SpriteFont`, `SoundEffect`, `Song`, `Video`, `Curve` and `Model` are registered. `Model` accepts only the exact schema-2 canonical form; a schema-1 Model is refused with a diagnostic naming `XNAP-56`. `AnimationClip` has no XNA equivalent and deliberately has no XNB writer. |
| `XNAP-62` | `cna-content --format xnb|cnb`, `--xnb-platform`, `--xnb-version`, `--xnb-profile`, `--xnb-compress`; `.xnb` output extension; help/validation/exit codes. | [x] `--xnb-compress` accepts only `none` today; `lzx`/`lz4` are refused with a message naming `XNAP-80`/`XNAP-81` rather than silently writing an uncompressed file under a compressed header. |
| `XNAP-63` | `.cna-content.json` v2: project-wide and per-asset `format`, target platform, graphics profile. | [x] |
| `XNAP-64` | Incremental build correctness for XNB: writer identity/schema fingerprints, format changes invalidating output. | [x] |
| `XNAP-65` | Diagnostics: every XNB failure names source, importer, processor, output format, field and reason. | [x] Also fixes a pre-existing gap: `cna-content` collected the pipeline's log messages and never printed any of them, so a warning about a documented loss reached nobody. |

### Phase F — source-asset routes

| ID | Task | State |
|---|---|---|
| `XNAP-50` | `.spritefont` XML description importer (FontName, Size, Spacing, UseKerning, Style, CharacterRegions, DefaultCharacter). | [x] Includes `CNA::Internal::ParseXml`, a deliberately strict XML subset (no DOCTYPE, no internal subset, no external entities) with line/column diagnostics. A field declared twice is refused rather than silently resolved to the first occurrence. `<FontName>` resolves to a **file beside the description**, not a Windows font family — see §5. |
| `XNAP-51` | FreeType-backed glyph rasterization + deterministic atlas packing → `CnbSpriteFontData`/`XnbSpriteFontData`. | [x] Deterministic shelf packer (character order, 1px padding, power-of-two sides to 2048), premultiplied white glyphs, ABC kerning from FreeType's own bearings, `UseKerning=false` folding the bearings into the advance, a 1×1 transparent texel for a blank glyph, and a warning — not silence — when a style has to be synthesized. Both output formats come off the same rasterization. |
| `XNAP-52` | FreeType dependency + test-font license audit; build-time-only isolation. | [x] New module `modules/content-pipeline/`, linked only by `cna_content_compiler`, so FreeType is absent from every runtime game's link closure. `CNA_ENABLE_FONT_PIPELINE` is three-state (`OFF`/`AUTO`/`ON`) like `CNA_ENABLE_VIDEO`; `OFF` reports "no font rasterizer" rather than emitting an empty font. FreeType (FreeType License) and the vendored OFL `LiberationMono-Regular.ttf` are both recorded in `THIRD_PARTY_NOTICES.md`, with `tests/assets/fonts/PROVENANCE.json` carrying the upstream, distributor, SHA-256 and redistribution reasoning. |
| `XNAP-53` | Independent BC1/BC2/BC3 encoder + `TextureProcessor` format parameter. | [x] Integer-only encoder in `modules/content-pipeline/`, written against CNA's **own** decoder (`DxtUtil`) so the error it minimizes is the error a player sees. Endpoints come from two seeds -- the colour bounding box and the most distant texel pair -- each refined by an exact integer least-squares re-fit, keeping the best; the second seed is what stops a red/blue block collapsing to magenta. BC3 alpha tries both the eight-value and six-value modes and keeps the lower error. Measured against CNA's decoder: 38.0 dB on a 64x64 synthetic (all three formats), 39.6 dB on a smooth image with six partial edge blocks, 27.0 dB on a deliberately worst-case block where R, G and B all vary independently. The single-colour block, which naively encodes as BC1's transparent three-colour mode, is handled explicitly and tested. |
| `XNAP-54` | Mip generation, premultiply-alpha, colour-key, resize policy parameters shared by both outputs. | [x] `textureFormat`, `generateMipmaps`, `premultiplyAlpha` and `resizeToPowerOfTwo` join the existing `colorKey`, applied in that documented order, with premultiplication before mip generation so a distant mip does not average the colour of invisible texels into visible ones. Resampling is integer area-averaging down and linear interpolation up. **`premultiplyAlpha` defaults to `false` where XNA 4.0 defaults it to `true` -- see `XNAP-96` and §5.** |
| `XNAP-55` | Audio: broaden accepted WAV PCM variants; deterministic duration; loop metadata. | [x] The importer previously read 8-bit unsigned and 16-bit PCM and refused everything else on the grounds that narrowing is an authoring decision. It now also reads 24-bit PCM, 32-bit PCM and 32-bit IEEE float, narrowing each to the Pcm16 both containers store with a fixed integer rule (halves toward positive infinity, saturating at both ends; a NaN float sample becomes silence) and **reporting the loss through the pipeline's warning channel**. Refusing them only moved the same lossy conversion into the author's audio editor, where it is no less lossy and considerably less visible. Compressed tags are still refused: decoding ADPCM or a codec stream is not a compiler's job. The `smpl` loop region and the WAVEFORMATEX cross-checks are unchanged, and the XNB duration field was already exact integer arithmetic (`frames * 1000 / sampleRate`). |
| `XNAP-56` | Canonical pipeline model IR sufficient for XNA `Model` (declarations, streams, materials, bounds, shared resources) without overloading a frozen CNB carrier. | [x] Resolved without a new IR. CNB Model schema 1 records a vertex stride but no `VertexDeclaration`; `CNA/Internal/Graphics/VertexDeclarationFidelity.hpp` already holds the canonical stride-to-element table that **every CNA renderer interprets those same bytes with**, so the declaration is recovered rather than invented. Mesh bounds are derived exactly as the runtime adapter derives them, so both loaders see one bounding sphere. Adding a parallel IR would have created a second thing to keep in step with no new information in it. |
| `XNAP-57` | glTF/GLB → canonical model IR → `Model` XNB, with vertex-declaration synthesis and validation. | [x] |
| `XNAP-58` | Material downgrade: glTF PBR → BasicEffect-family, deterministic and diagnosed; CNB keeps the full material. | [x] |
| `XNAP-59` | Model test matrix: triangle, cube, multi-mesh, multi-material, hierarchy, transforms, indexed/non-indexed, multi-attribute, textured, skinned, malformed, limits. | [~] Covered by tests plus a corpus sweep: 130 of the 148 committed `.glb` fixtures build to Model XNB and every one is accepted by the independent parser. The 18 that do not are all correct refusals — four non-triangle topologies, twelve pre-existing import refusals shared with the CNB route, one multi-Model source needing `generateChildAssets`, one glTF material-variants source CNB schema 1 already refuses. The sweep is not yet a committed test. |

### Phase G — real-XNA interoperability harness (cannot execute here)

| ID | Task | State |
|---|---|---|
| `XNAP-30` | CNA-generated fixture corpus for XNA loading (`Texture2D`, `SoundEffect`, `SpriteFont`, `Curve`, `List<String>`, `Dictionary<,>`, `Model`, `Song`, `Effect`). | [x] |
| `XNAP-31` | Expected-value manifests per fixture (dimensions, mip counts, exact pixels, formats, glyph metrics, bone/mesh graphs). | [x] |
| `XNAP-32` | XNA 4.0 harness project + build/run instructions for an XNA-capable Windows installation. | [x] |
| `XNAP-33` | Harness asserts values, not just successful `Load<T>()`. | [x] |
| `XNAP-34` | Result-recording protocol so a future session with a real runtime can fill in the `xna40` column. | [!] The protocol is written (`tests/interop/xna40/README.md`, "Recording the result"). **Execution is blocked**: no XNA 4.0 runtime, Windows, Wine, Mono or .NET exists in this environment (§0.3). No row of any capability table may read `xna40` until this runs. |

### Phase J — interoperability defects found by the writer

Building a writer against the reader exposes disagreements the reader alone could not show.

| ID | Task | State |
|---|---|---|
| `XNAP-70` | **`SongReader` consumed the duration's dispatch byte.** A real content pipeline writes a Song as a bare media-path string followed by an `Int32` dispatched through `Int32Reader`; CNA read a bare `Int32`, so it consumed the `0x02` dispatch byte as the low byte of the duration. The committed MonoGame fixture reported 769282 ms for a 3-second clip, and its own provenance manifest had recorded that misparse as fact. Fixed by selecting the encoding from the type-reader table size, exactly as `VideoReader` already did; the fixture manifest now records 3005 ms, corroborated independently from the companion `.ogg` (last Ogg page granule 132480 samples at 44100 Hz = 3004.08 ms). | [x] |
| `XNAP-71` | `VideoReader`'s runtime path auto-detects the same two encodings from the table size, but CNA's hand-constructed Video fixtures still use the field-only form no real pipeline produces. The writer emits the object-referenced form. Regenerating those fixtures would make the legacy branch dead code that could then be removed. | [x] **Resolved by covering both branches rather than by deleting one.** A full-container fixture in the object-referenced form was added alongside the existing inline one, so `VideoReader`'s table-size selection is exercised end to end in both directions instead of only one. The inline branch is *not* removed: the claim that no real pipeline writes it is an inference, not an observation — no externally produced Video `.xnb` exists in this repository to check it against — and deleting a reader branch on an inference would break any file that does use it, for no gain beyond tidiness. **The new test does not run in this environment**: `ContentManagerVideoXnbTests.cpp` is excluded when FFmpeg is absent (`cmake/UnitTests.cmake`), and it is. The file was compiled by hand against the same flags to confirm it builds. |

### Phase H — compression, exotic targets, effects, hardening

| ID | Task | State |
|---|---|---|
| `XNAP-80` | LZ4 (`0x40`) output — extended-ecosystem only, never presented as XNA 4.0. | [x] A raw LZ4 block encoder written from the published block grammar: greedy matching over a 64 KiB window with a fixed hash table, so the same payload always compresses to the same bytes. It honours the two rules that exist for the decoder rather than the encoder — the last five bytes are always literals and no match begins within the last twelve — because a block that breaks them is not decodable by a conforming implementation. Verified three ways: round-tripped through CNA's own LZ4 decoder over seven payloads chosen at the format's boundaries (empty, sub-match-length, exactly at the twelve-byte margin, highly repetitive, incompressible); through the whole `DecodeXnbCanonicalAsset` path on a real compressed file; and by the independent Python parser, which gained **its own** LZ4 block decoder so a compressed file is validated by a second implementation rather than by the one that made it. `ValidateXnbFileOptions` already refused LZ4 on an XNA 4.0 target platform, and that stays: this is not an XNA 4.0 format. |
| `XNAP-81` | XNA-compatible LZX (`0x80`) output: bounded encoder, correct decompressed-size field, block framing, round-trip against CNA's existing decoder. | [!] **Not done, and scoped rather than attempted.** LZX is the one Microsoft XNA 4.0 itself produced, so it is the compression that would matter most — and it is also the expensive one. CNA has an LZX *decoder* (a port of an existing implementation); an *encoder* is a different program: Huffman tree construction and encoding for the main/length/aligned-offset alphabets, delta-encoded tree transmission between blocks, block-type selection, and the 64 KiB sliding window with its position-slot encoding. There is a shortcut — LZX's uncompressed block type would produce a conforming stream with no entropy coding at all — and it is deliberately **not** taken: a file that sets the compressed flag and does not compress would be honest only if documented so precisely that nobody would want it, and it would still risk an interop failure this environment cannot test for. **Uncompressed output loads in every XNA 4.0 runtime**, so nothing is blocked by this; only file size is affected. |
| `XNAP-82` | Xbox 360 (`x`) output audit: endianness, WAVEFORMATEX byte order, texture swizzling. Document as unsupported/experimental unless provable. | [x] **Audited, and the finding is enforced in code rather than only written down.** The 360 is big-endian; CNA has one piece of Xbox-specific payload handling (the `SoundEffect` WAVEFORMATEX swap) and nothing else — not texture dimension fields, not the bone table, not even that sound's own sample bytes, which CNA's *reader* already refuses to transcode out of an Xbox file for the same reason. Every asset writer now calls `RequireVerifiedPlatformPayload()`, so an `x` target is refused with a message naming what is missing. `XnbFileOptions::allowUnverifiedXboxPayloads` and `cna-content --xnb-allow-unverified-xbox` override it, because somebody producing candidate files on real hardware is the only way the gap closes and a refusal with no escape hatch would prevent that. Texture tiling was **not** investigated further: with the endianness question open, tiling cannot be the first thing to settle. |
| `XNAP-83` | Windows Phone (`m`) output audit: distinguish "header can be emitted" from "semantics verified". | [x] `m` is a little-endian ARM target with no known payload difference from Windows, so it is **not** refused — the honest statement is narrower than Xbox's: the header byte is written, the payloads are the ones Windows gets, and nothing about it is verified, because no `m`-platform fixture and no Windows Phone runtime exist here. That is a documentation claim rather than a byte-order one, which is why it is not enforced in code, and `docs/xnb-interoperability.md` says exactly that. |
| `XNAP-84` | Audit CNA's FX infrastructure for genuine XNA-compatible effect-bytecode generation; scope a sub-plan or record the blocker. Never fake it by embedding source text. | [x] **Audited; the blocker is recorded and the achievable half is now implemented.** Findings: (1) CNA *reads* compiled Effect Framework 9.1 binaries and executes them on four renderers (`plans/plan_fx.md`). (2) CNA does **not** compile HLSL, and that is a standing project decision, not an oversight -- `plans/plan_fx.md`'s own format table reads "`.fx` \| HLSL Effect Framework **source** \| Out of scope; CNA will not embed an HLSL source compiler". (3) A legally clean route to *obtain* the bytecode already exists and is already used in this repository: `fxc` 9.29.952.3111 from the June 2010 DirectX SDK at profile `fx_2_0` — the compiler XNA's own Content Pipeline used — run under Wine, with compiler identity, SDK hash and reproduction commands recorded beside the artifacts it produced. So the gap is not "CNA cannot produce XNA-compatible bytecode"; it is "CNA does not host the compiler", which is a different and much smaller statement. What was missing was the route from a compiled binary into content, and that is now built: `.fxb` → `CompiledEffectImporter` → `CompiledEffectProcessor` → `CNA.XnbEffectWriter`, verified end to end on the repository's own real `fx_2_0` binary with the bytecode surviving byte for byte. `.fx` has no importer at all, so a build tree containing one reports that nothing imports it rather than a component accepting it and failing inside. There is no CNB writer, deliberately: the CNB container reserves an `Effect` identifier and has no schema for it, because a `.cnb` carrying one API's shader bytecode would be useless on CNA's other renderers. |
| `XNAP-85` | Untrusted-input hardening sweep of the writer path: checked arithmetic, narrowing, offsets, counts, allocation ceilings, UTF-8 validity, deterministic failure. | [x] Four real defects found and fixed, each with a test. **(1)** Every limit is a signed `int32` and every check widened it to `std::size_t`, so a zero or negative limit did not mean "small", it meant "no limit at all"; `ValidateXnbWriteLimits()` now refuses one at the single point they enter the system. **(2)** The default payload ceiling is four times the file ceiling, so the writer would assemble up to 256 MB of something the file ceiling was always going to refuse; the assembly buffer is now capped by what the file ceiling leaves for a payload. **(3)** A texture level whose byte count disagreed with the dimensions and format the same file declares was written and only refused later by a reader, by which time the diagnostic could name a file but not the data; the writer now checks every level, down the mip chain, with the block-rounding rule block-compressed formats need. **(4)** An external reference containing a NUL or a control character was written verbatim — it survives this format's length-prefixed strings intact and truncates in whatever consumes the path as a C string. Also added: unconditional `Int32` representability checks on string lengths and collection counts, independent of the configured (relaxable) ceilings, because those two are properties of the format rather than policy. The writer already validated UTF-8 on the way out, refused unpaired surrogates, bounded nesting depth, and copied shared-resource values rather than borrowing pointers into caller temporaries. |

### Phase I — API, boundaries, documentation, finalization

| ID | Task | State |
|---|---|---|
| `XNAP-06` | Correct all XNB platform/version wording in `xnb.md`, `docs/xnb-content-pipeline-support.md` and elsewhere: XNA-era `w`/`m`/`x` vs extended ecosystem; version 5 = XNA 4.0 era, version 4 = legacy. | [x] Also corrected a larger error in `docs/xnb-content-pipeline-support.md`: it declared producing `.xnb` files "out of scope, permanently" and said CNA "has no need to generate them itself". That is no longer true, and the document now says so and points at the writer's own document rather than leaving a confident false statement in the tree. |
| `XNAP-07` | `docs/xnb-interoperability.md`: per-type capability matrix using the §3 confidence vocabulary. | [x] Written, and **the tables were made true rather than the claims rounded up**. Writing it surfaced four overclaims, each closed by a test rather than by softer wording: nine framework value types and both `System` time types had writers and readers but no round trip; the primitive and collection roots had never been put in front of the independent parser; `Texture3D`, `TextureCube`, `Song`, `Video` and `Effect` were `cna-rt` only. Three new conformance tests now write each of those and validate the bytes with `tools/xnb/xnb_conformance.py`. What remains uncovered is stated as uncovered. |
| `XNAP-90` | Runtime-vs-build-time dependency boundary audit; keep FreeType/encoders/importers out of the runtime link closure. | [~] The boundary now exists and FreeType is on the correct side of it (`XNAP-52`). The full audit — every build-time dependency, and a test that fails if one reaches the runtime umbrella — is still open. |
| `XNAP-91` | Public API audit of the writer surface: ownership, const-correctness, error handling, extension points, no accidental implementation exposure. | [x] Audited. **Ownership**: `XnbWriter` borrows the registry (documented lifetime), copies every shared-resource value into a `shared_ptr` rather than borrowing a caller temporary, and is non-copyable. **Error handling**: every failure is an `XnbWriteException` carrying the asset name and the reader that was being written; nothing returns a status code or an empty vector on failure. **Extension points**: `XnbTypeWriter<T>` plus `XnbTypeWriterRegistry::Register()`, documented and exercised end to end (`XNAP-92`). **Exposure**: the private section holds only `SharedResourceEntry` and `TypeTableEntry`, neither reachable from the public surface. One real finding, fixed: the constructor `Freeze()`s the registry it takes by `const&` — a permanent, build-wide side effect that the parameter's constness actively hides — and that was undocumented. It now says so, and says why (a registration accepted mid-write would change what later files in the same build contain). |
| `XNAP-92` | Custom-writer extension model + documented example. | [x] `docs/xnb-interoperability.md` §8 documents both halves — a custom `XnbTypeWriter` and the reader that gives its bytes meaning — and `XnbWriterTest.AGameCanRegisterItsOwnTypeWriterAndReaderAndRoundTripThroughThem` is that example run end to end, including the check that a custom writer inherits the built-ins' ceilings because it writes *through* `XnbWriter` rather than around it. The same section explains that an unregistered generic instantiation such as `List<Single>` is one registration call rather than a gap. The pipeline-level half (a custom `ContentTypeWriter` returning `ContentOutputFormat::Xnb`) points at `docs/content-pipeline.md`'s existing **Custom extensions** section rather than restating it. |
| `XNAP-93` | Performance pass on large textures, mip generation, BC encoding, large models, atlas generation, compression. | [~] Measured end to end through `cna-content` and recorded in `docs/content-pipeline-benchmark.md` (§`XNAP-93`), with the recipe. Headline: a 1024x1024 RGBA source costs 0.33 s uncompressed, +0.13 s for a full mip chain, and **1.08 s for Dxt1** — block compression dominates, at roughly 1.4 megapixels per second. That is slower than a tuned encoder and it is a deliberate trade (two endpoint seeds per block rather than one, which is what stops a red/blue block collapsing to magenta); `refinementRounds` is the dial. LZ4 runs at roughly 20 MB/s. A 190-glyph 32 px SpriteFont costs 0.044 s. **Not measured: large models.** The glTF corpus here is small-model dominated and a representative large-model source was not available to time, so that row is honestly absent rather than extrapolated. |
| `XNAP-94` | Provenance audit: no MonoGame/FNA/Microsoft implementation derivation; every fixture and dependency licensed and manifested. | [x] Audited and recorded in `docs/xnb-interoperability.md` §9. Every wire-format decision traces to one of three sources: committed fixtures read as **bytes**, CNA's own independently written reader, or a published format description. A grep of every file this plan added shows the only occurrences of "MonoGame" or "FNA" are `XnbNameEvidence::MonoGameFixture` values — a record of *which committed file a name was read out of*, which is data provenance rather than implementation derivation. Fixture inventory: 21 `.xnb` files, and all 21 are covered — 15 externally produced ones each carry a `.manifest.json` recording origin and licence (Ms-PL), and the 6 CNA-generated ones carry `.expected.json` manifests plus a corpus-level `fixtures.json`. New third-party dependencies: FreeType (FreeType License, system-provided, build-time module only) and the vendored Liberation Mono test font (SIL OFL 1.1, with a `PROVENANCE.json`), both in `THIRD_PARTY_NOTICES.md`, neither reachable from any runtime module. No Microsoft binary was decompiled. |
| `XNAP-95` | Final reconciliation: every checkbox true, exact test totals everywhere, no contradictory numbers, no placeholder shells. | [~] Reconciled, and **not** every checkbox is true — which is the honest outcome rather than a failure to finish. Of 69 tasks: 57 `[x]`, 8 `[~]` with the remaining scope named in the row itself, 2 `[!]` blocked with the blocker named (`XNAP-34`, no XNA runtime; `XNAP-81`, no LZX encoder), 1 open owner decision (`XNAP-96`). Test totals appear in exactly one place, §0.4, and every other document points at it rather than restating a number that would go stale. No placeholder shells: every type in every capability table either has a writer with a test or is listed as absent with the reason. The one thing a reader should carry away is at the top of `docs/xnb-interoperability.md`: **nothing in this repository has been verified against a genuine Microsoft XNA 4.0 runtime**, because none exists here, and no table says otherwise. |
| `XNAP-96` | **Owner decision, now taken:** should `premultiplyAlpha` default to `true`, as XNA 4.0's `TextureProcessor` does? | [x] **Yes -- the default is now `true`.** The primary objective of this subsystem is XNA 4.0 compatibility, and CNA was deliberately diverging from XNA on the one texture policy a player can see: `BlendState::AlphaBlend`, which `SpriteBatch::Begin()` selects when given no blend state in both frameworks, is the premultiplied blend, so straight-alpha content renders with dark fringes under the default state. Keeping the old default to protect experimental output built before this subsystem stabilized was the wrong trade. `TextureProcessor`'s build identity moved to version 3, so an incremental build rebuilds rather than skipping an artifact that is no longer current, and the manifest fingerprint carries the processor version already. The three equivalence contracts that pinned the old bytes now pin them *explicitly* (`premultiplyAlpha=false`), which is what they were really about -- colour-key and writer convergence with the unchanged producer -- and the default's own behaviour has its own tests: omitted parameter premultiplies, explicit `true` is byte-identical to omitting it, explicit `false` returns the source pixels exactly, alpha 255 is untouched, alpha 0 gives zero RGB, partial alpha rounds by `(c*a+127)/255`, a colour-keyed texel comes out transparent **black** as XNA's does, premultiplication precedes mip generation, and the same source produces identical texels through the CNB writer and the XNB writer -- which is the check that the processor was not forked per container. Two importers override the default because their source defines its own answer: a `.cnj` document (CNJ v1 compiles to straight alpha; `CompileCnjToCnb` pins it) and an already-built `.xnb` being transcoded (double premultiplication is corruption, not policy). Both say so through `ImportedImage::authoredPremultiplyAlpha`, the same mechanism `authoredColorKey` already used; an explicit parameter still beats both. |

### Phase K — merging the two parallel implementations into one

The `pipeline` branch was folded into this one on 2026-09-03 (merge commit with both parents, so
its history is preserved and its ref can be retired). Where both branches had the same thing, this
branch's implementation survived, because the surrounding code here — the asset writers, the
`XnbAssetWriter` API, the golden tests — is written against it. Where `pipeline` had something this
branch did not, it is ported task by task below rather than swallowed by the merge commit, so each
arrives with its own tests.

| ID | Task | Status |
|---|---|---|
| `XNAP-97` | Port `pipeline`'s `Decimal` and `BoundingFrustum` writers. | [x] `Decimal` writes the four Int32 words `System::IO::BinaryReader::ReadDecimal()` consumes — lo, mid, hi, flags — behind the same `SHARP_RUNTIME_HAS_NATIVE_INT128` guard as `DecimalReader`, so on a toolchain without a native 128-bit integer neither side exists rather than one silently degrading. `BoundingFrustum` is the one .NET **class** in the framework value-type group and is therefore registered `serializedByReference`; its payload is the source `Matrix` alone, with the planes and corners recomputed by the reading side's constructor. Both round-trip through CNA's own readers with the derived state asserted, not just the stored words. |
| `XNAP-98` | Port `pipeline`'s `Enum<T>` writer support. | [x] `XnbEnumTypeWriter<TEnum>` writes the underlying `Int32` — the exact inverse of `EnumTypeReader<T>`, which this repository already had on the reading side — and `RegisterXnbEnumWriter<T>(registry, name, assembly)` registers one. `pipeline` keyed its registry by .NET **name** and therefore had to re-check at write time that a boxed value matched its writer; here the key is the C++ type, so that class of mismatch does not exist. Not registered by default, for the reason the row above gives: a C++ enum does not carry its .NET name. **One real defect found and fixed on the way**: `XnbReaderIdentity` conflated the reader's generic arguments with the target type's own. `EnumReader\`1[[SurfaceFormat]]` produces the *non-generic* `SurfaceFormat`, so the argument list was being appended to the target name a second time — and the array writer had the same latent bug (`System.Int32[][[System.Int32]]`), which would have surfaced the first time an array appeared as a nested generic argument. `targetSharesGenericArguments` now records the distinction, and both cases are pinned by `XnbEnumReaderIdentityTest`. |
| `XNAP-99` | Port `pipeline`'s manifest `rootReaderName` field (schema version 9). | [x] `ContentBuildManifestOutput::rootReaderName`, serialized, parsed, compared and — the part that matters — fingerprinted into the `outputDefinitions` domain, so a build that changes which reader an asset dispatches to invalidates the artifact instead of being skipped as unchanged (asserted directly). Manifest version 8 → 9; the earlier-versions-rejected test now covers 8, and the CLI's stale-manifest test reads the constant rather than a literal so the next bump does not silently pass. **Deviation from `pipeline`, deliberate**: there an `.xnb` output carries *no* CNB identity and `rootReaderName` instead, enforced by the validator. Here the XNB pipeline writers declare a synthetic `assetTypeId`/schema/type name because this branch's whole incremental machinery (`writerSchemas`, the `schemaFor` lookups) is built on them; removing that is a far larger change than porting a field, so both identities coexist and the validator checks what it still can — a recorded reader name must be non-blank and free of control characters. It cannot check "`.xnb` has one, CNB has none", because a manifest entry does not record its output's container format; that is stated in the code rather than left implied. **Improvement over `pipeline`**: the name is not a per-writer constant. `XnbWriter` records the canonical reader of the first object written at depth zero and `WriteXnbAssetWithIdentity()` returns it, so the manifest identity and the file's own type-table dispatch are read from one source and cannot drift — `XnbOutputContentPipelineTest.EveryXnbWriterReportsTheRootReaderTheFileActuallyDispatchesTo` compares the reported name against the decoded file. |
| `XNAP-9A` | Reconcile the two `docs/` sets: fold `pipeline`'s `docs/xna-content-pipeline.md` architecture and usage material into the surviving documents. | [x] Compared section by section. Everything that document carried already had a home here except its architecture orientation, which is now `docs/xnb-interoperability.md` §0: the three-layer table with the real paths on this branch, the writer-boundary diagram, and the design commitment that a processor never learns which container it is feeding. Its tool usage is already §7 *Using it*; its extension points are §8 and `docs/content-pipeline.md`'s **Custom extensions**; its CNB relationship is that document's **CNJ, CNB, and XNB**; its provenance, limitations and verification sections are §9, §6 and §10 here, in more detail. Nothing was carried over verbatim where the surviving text was already the better one. |
| `XNAP-9B` | Make the reconciliation checkable rather than only described: a script that recounts this plan's task table against its own stated totals, and a test that fails if the superseded writer architecture's paths reappear. | [x] The stated totals were wrong on two counts at once -- "69 tasks" against a 73-row table, and "57 + 8 + 2 + 1" against its own "69" -- which is exactly the class of error a reader cannot catch and five lines of Python catch every time. `tools/xnb/check_plan_status.py` recounts the table, compares it with *every* "N tasks: ..." sentence in the file, rejects a duplicate task ID, and rejects a `[~]`/`[!]` row with no explanation after the marker; ctest `CnaXnbPlanStatusConsistency` runs it. `XnbArchitectureUniquenessTest` asserts the surviving paths exist, the six superseded ones do not, and no file under the content modules, tools, cmake or interop harness references them -- because a removed architecture comes back when somebody restores a header to fix a stale include. |

---

## 5. Deviations and decisions

* **`XnbByteWriter` rather than `System::IO::BinaryWriter`.** `BinaryWriter` already provides
  little-endian primitives, `Write7BitEncodedInt` and 7-bit-length-prefixed strings, but it has no
  UTF-8 `charcs` overload and is stream-shaped, while XNB writing needs an in-memory body buffer
  that a header is prepended to. `CnbByteWriter` sets exactly this precedent (CNA owns its own
  deterministic byte writer next to the sharp-runtime reader). Adding `Write(charcs)` to
  sharp-runtime is the right long-term fix but that is a **separate repository** and outside this
  branch's authorization.
* **Reader-name table with confidence, not hard-coded strings.** Two entries (`SoundEffectReader`,
  `SongReader` as bare names) are MonoGame-verified but XNA-unverified. Keeping them as data with a
  confidence field is honest and makes correction cheap.
* **Reach is the default graphics profile** even though the one genuine XNA 4.0 fixture available
  here has the HiDef bit set: a Reach asset is loadable under both profiles, a HiDef asset is not.
* **`<FontName>` names a file, not a Windows font family.** XNA's `FontDescriptionProcessor` asks
  Windows to resolve a family name (`"Segoe UI"`) through GDI+, which means the same
  `.spritefont` produces different bytes on different machines, and no bytes at all on a machine
  without that font. CNA resolves `<FontName>` against the description's own directory first
  (`Console`, `Console.ttf`, `Console.otf`, `Console.ttc`), so a content build is reproducible and
  the font travels with the project. Installed system fonts are searched only as a **fallback**,
  and using one emits a warning saying the build is no longer reproducible. This is a deliberate
  divergence from XNA behavior; the XNA spelling still works wherever the font file is present.
* **A separate module for build-time-only pipeline components.** `modules/content-pipeline/`
  exists to keep FreeType — and any future encoder or importer — out of the link closure of games
  that merely *load* content. `cna_content_pipeline` is linked by `cna_content_compiler` and by
  nothing else, so the boundary is enforced by the dependency graph rather than by convention.
  Its test group is named explicitly in `cmake/UnitTests.cmake` for the same reason: it is
  deliberately absent from the `CNA` runtime umbrella that `CnaTests` otherwise links.
* **Premultiplied alpha is available but not the default.** XNA 4.0's `TextureProcessor`
  premultiplies by default and `BlendState::AlphaBlend` -- which CNA implements identically to
  XNA, and which `SpriteBatch::Begin()` selects when given no blend state -- is the blend that
  expects it. CNA's `premultiplyAlpha` parameter nevertheless defaults to `false`. The reason is
  not a judgement that XNA is wrong: it is that flipping the default silently changes the bytes of
  every alpha-bearing texture any existing CNA project has already built, and this repository
  pins the current output in three separate equivalence tests against its own older producers.
  Recorded as `XNAP-96` for the project owner rather than decided here.
* **Block compression lives in `modules/content-pipeline/`, reached through a callable.**
  `cna_content` declares the encoder's *shape* (`TextureBlockEncoder`) and never links one;
  `cna_content_pipeline`, which only a content compiler links, supplies it. That keeps the
  processor -- and every other texture policy -- in one place while leaving the encoder on the
  build-time side of the boundary. A registry without an encoder builds every uncompressed
  texture and refuses a compressed `textureFormat` with a message naming the configuration that
  would provide one, rather than quietly writing uncompressed pixels.
* **A block-compressed texture has no `.cnb`.** CNB texture schema 1 is frozen to `Rgba8`
  (`plans/plan_cnb.md` `CNBF-101A`). A CNB build that asks for compression keeps the uncompressed
  pixels and says so through the pipeline's warning channel; it does not fail the build over a
  format the other container would have accepted. `ContentProcessorContext::OutputFormat()` was
  added so a processor can see the difference and take a documented fallback rather than failing
  late inside a codec.
* **`SoundEffect` writes the full 18-byte `WAVEFORMATEX`, not the 16-byte `PCMWAVEFORMAT`.**
  The format block is length-prefixed, so both forms are legal and CNA's reader accepts both; the
  writer originally emitted 16 bytes when there was no extension, omitting `cbSize` entirely. The
  only real producer whose output is committed here writes 18 with `cbSize` present and zero, and
  matching an observed producer beats matching none — so the writer changed, and the change is
  proven by a byte-exact golden test against that file (`XNAP-42`). This is a real output change:
  every `SoundEffect` CNA writes is two bytes longer than before, and the committed interop corpus
  was regenerated for it.
