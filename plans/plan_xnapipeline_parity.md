# plan_xnapipeline_parity.md — TRUE Microsoft XNA 4.0 Content Pipeline API + input parity

> **Scope of this plan.** [`plan_xnapipeline.md`](plan_xnapipeline.md) answered *"can CNA natively
> build useful, real XNB content?"* and is closed. This plan sits **above** that pipeline and asks
> the harder question: *can an XNA Game Studio 4.0 Content Pipeline developer move the complete
> public pipeline programming model and every built-in source-content workflow to native C++ CNA
> without meeting a missing API concept, importer, processor, source extension or XNB output
> path?* The answer is a number read off a matrix whose denominator was **measured from the
> genuine Microsoft assemblies**, never typed from memory.
>
> **Boundaries.** One canonical engine — `CNA::Content::Pipeline` in `modules/content/` plus the
> build-time-only `modules/content-pipeline/` — and one XNB writer — `CNA::Internal::Xnb`. Every
> public XNA type this plan adds is a façade, adapter, base-class compatibility layer or strongly
> typed view over that engine. **No second pipeline, no second writer**: `plan_xnapipeline.md` §0
> records what that mistake cost once, and `XnbArchitectureUniquenessTest` fails if a superseded
> path returns. CNB (`plan_cnb.md`) is never weakened for XNB; the runtime XNB reader
> (`plan_xnb.md`) is not reopened; shader *compilation* stays with `plan_fx.md`.
>
> **Task IDs.** `XNAPP-###`, three digits, allocated in phase ranges (§32). Never reuse an ID;
> never renumber; append new tasks at the tail of their phase or in a new phase.
>
> **Status (2026-09-05, session 1).** Phase 0 and Phase 1 are complete: the authoritative
> inventory exists, is deterministic, and is committed. Every parity percentage below is
> **0 % until `tools/xna-pipeline-oracle/parity_report.py` says otherwise**; this file quotes
> that report and never a hand-counted number. Nothing in this plan is verified against a genuine
> XNA runtime yet; §25 says how that will change, and it *can* change here, because — unlike the
> container `plan_xnapipeline.md` was written in — this machine has XNA Game Studio 4.0 (§3.2).

---

## 1. Scope definition

**In scope — the denominator.** The PUBLIC and PROTECTED surface of the seven Content Pipeline
assemblies shipped by XNA Game Studio 4.0 Refresh:

| Assembly | Public types | Role |
|---|---:|---|
| `Microsoft.Xna.Framework.Content.Pipeline` | 120 | core, contexts, graphics/audio intermediate object model, processors, compiler and intermediate serialization, MSBuild tasks |
| `Microsoft.Xna.Framework.Content.Pipeline.AudioImporters` | 3 | `WavImporter`, `Mp3Importer`, `WmaImporter` |
| `Microsoft.Xna.Framework.Content.Pipeline.EffectImporter` | 1 | `EffectImporter` |
| `Microsoft.Xna.Framework.Content.Pipeline.FBXImporter` | 1 | `FbxImporter` (mixed-mode; FBX SDK 2011.3.1 inside) |
| `Microsoft.Xna.Framework.Content.Pipeline.TextureImporter` | 1 | `TextureImporter` (mixed-mode) |
| `Microsoft.Xna.Framework.Content.Pipeline.VideoImporters` | 1 | `WmvImporter` |
| `Microsoft.Xna.Framework.Content.Pipeline.XImporter` | 1 | `XImporter` (mixed-mode; D3DX `.x` loader inside) |

plus, as an auxiliary set, the five `Microsoft.Xna.Framework.Content.ContentSerializer*Attribute`
types of the runtime assembly, because `IntermediateSerializer` and the automatic type writer are
specified in terms of them.

**Measured on 2026-09-05** (`tests/reference/xna40/content-pipeline-api.json`, `counts`):

| Quantity | Value |
|---|---:|
| public/protected types | **128** |
| public/protected members (constructors, methods, operators, properties, indexers, fields, constants, events; enum values counted under their enum, see §5) | **708** |
| enums / enum values | 8 / 27 |
| delegates | 1 (`ContentTypeSerializer.ChildCallback`) |
| built-in importers | **10** |
| declared source extensions (distinct) | **18** |
| built-in processors (public, concrete) | **12** |
| public configurable processor properties | **47** |
| public `ContentTypeWriter` roots | 1 (`ContentTypeWriter<T>`; every concrete writer is internal) |
| public `ContentTypeSerializer` roots | 1 (`ContentTypeSerializer<T>`; every concrete serializer is internal) |
| external types the public surface names | 66 (§16.3) |
| public MSBuild task types | 4 |

**Excluded from the denominator, by rule and by name** (both lists are in the JSON):
39 "public" types that no consumer can name because they are nested inside non-public declaring
types — 35 C++/CLI compiler shims for native headers in the three mixed-mode assemblies (`std`,
`<CrtImplementationDetails>`, `fbxsdk_2011_3_1_20100707`) and 4 nested inside `internal`
`ReflectionEmitUtils` / `UnsafeNativeMethods`. Each enum's `value__` storage field is CLR
plumbing, not API, and is not a member.

**Out of scope.** Private/internal Microsoft implementation classes (their *observable* behaviour
through public API and built-in components is in scope); XACT (`BuildXact` is inventoried as an
MSBuild host task, §18, and `plan_audio.md` owns XACT); Visual Studio project-system integration
beyond the `.contentproj` file itself.

---

## 2. Source and provenance rules

**Allowed, and actually used:**

1. Public metadata of the legally obtained XNA Game Studio 4.0 Refresh reference assemblies, read
   by `System.Reflection` — names, signatures, attributes, enum values, inheritance
   (`tools/xna-pipeline-oracle/PipelineApiOracle.cs`).
2. The official IntelliSense documentation `Microsoft.Xna.Framework.Content.Pipeline.xml`
   shipped in the same SDK — summaries, parameter text, declared exceptions
   (`merge_xml_docs.py`).
3. Black-box execution of Microsoft's own tools: constructing built-in processors and reading
   their public properties; running the `BuildContent` MSBuild task under Wine on synthetic
   fixtures and observing the `.xnb` it writes (§23); loading CNA-written `.xnb` in the genuine
   runtime (§25).
4. The public MSBuild schema in `Microsoft.Xna.GameStudio.ContentPipeline.targets` and the
   `.contentproj` files of the public XNA samples — item metadata names, property names, the
   platform/profile spellings (§18).
5. CNA's existing reader, writer and pipeline, and CNA's own independently authored code.
6. Published XNB format documentation and the committed black-box fixtures
   (`tests/assets/xnb/xna40/`, `tests/assets/xnb/monogame/`).

**Forbidden, and enforced:** decompiling any XNA method body; deriving an algorithm from IL or
native code; reading or copying MonoGame's or FNA's content-*building* source; copying another
clean-room implementation; committing a Microsoft DLL, EXE, font or SDK component; inventing an
API member because it "probably existed". `XNAPP-315` is the gate: no file under `tests/` or
`tools/` may contain a Microsoft binary, and the oracle's compiled output lives only under the
ignored `build/`.

The **FNA reference tree** (`/rv/data/library/github.com/FNA-XNA/FNA`) is the runtime reference
and has no content-*building* code, so it is neither needed nor consulted here. Where XNA and FNA
disagree, XNA wins (`CLAUDE.md`, decided 2026-09-04).

---

## 3. Authoritative XNA assembly/API inventory

### 3.1 Files

| File | Content | Produced by |
|---|---|---|
| `tests/reference/xna40/content-pipeline-api.json` | the inventory: assemblies (name, version, public key token, SHA-256, MVID, PE kind), every type with every member, attributes, enum values, importers, processors with instantiated defaults, external references, exclusion lists | `tools/xna-pipeline-oracle/run-oracle.sh` (mcs + Wine .NET 4.0) |
| `tests/reference/xna40/content-pipeline-api-docs.json` | the XML-doc join: per-member summary/params/returns/exceptions; both directions of documented-vs-public disagreement | `tools/xna-pipeline-oracle/merge_xml_docs.py` |
| `tests/reference/xna40/content-pipeline-parity-map.json` | CNA's answer per type and per member: status, C++ spelling, header, note | hand-maintained, gate-checked (§28) |
| `docs/xna-content-pipeline-parity-report.md` | generated coverage report and matrices | `tools/xna-pipeline-oracle/parity_report.py` |

The inventory is **byte-deterministic** (two runs, identical bytes, `XNAPP-013`) and records each
assembly's SHA-256, so the denominator is tied to exact binaries: `Microsoft.Xna.Framework.Content.Pipeline.dll`
version 4.0.0.0, token `842cf8be1de50553`, and its six importer siblings, all from XNA Game
Studio 4.0 Refresh.

### 3.2 Environment audit (2026-09-05) — what `plan_xnapipeline.md` §0.3 could not see

| Capability | State here |
|---|---|
| XNA Game Studio 4.0 Refresh SDK (pipeline assemblies, MSBuild targets, XML docs) | **Present**, extracted installer under `/rv/tmp/samples/_tools/xna-game-studio-4-refresh/` |
| Microsoft XNA 4.0 runtime | **Present** in Wine prefix `~/.wine-cna-xna40` (GAC, `XnaNative.dll`, D3D9 via DXVK, `d3dx9_41`/`d3dx9_43`, `d3dcompiler_43`) |
| .NET Framework 4.0 `csc.exe`, `MSBuild.exe` under Wine; mono `mcs` natively | **Present** |
| Microsoft `BuildContent` task executing under Wine | **Proven** by the cna-samples campaign (108 sample content trees; Texture, FontDescription, Fbx, Effect, Wav, Xml and X importers all ran) |
| Genuine `fxc` | **Not found as a standalone binary**; `d3dx9_43.dll`/`d3dcompiler_43.dll` are present and are what XNA's `EffectProcessor` itself calls, so the effect route can be verified through `BuildContent` (§23) even without `fxc.exe` |
| Xbox 360 / Windows Phone hardware or runtime | **Absent** — the only genuinely external blocker in this plan (§30) |
| learn.microsoft.com XNA doc mirror (`/rv/tmp/xna_learn.microsoft.com/`) | present but **2851/2851 pages are redirect stubs** — useless; not an authority |

The two blocked rows of the previous plan (`XNAP-34`, `XNAP-A4`) recorded "no Wine, no .NET, no
XNA" from an environment that lacked `/rv`. **That finding does not hold on this machine**, and
this plan's Phase 20 executes what those rows could not.

### 3.3 Documentation cross-check

120 of 128 public types are documented in the XML; the 8 undocumented ones are exactly the 8
importer classes of the six importer assemblies (they ship without an XML file). 665 of 708
members carry documentation; the 43 undocumented members are the importers' constructors and
`Import` overrides, `Collection<T>` protected overrides (`InsertItem`, `SetItem`, …) on the
collection types, `OpaqueDataDictionary`'s protected overrides and `DefaultSerializerType`,
`ContentWriter.Dispose(bool)`, `ChildCallback`'s delegate plumbing, and the four tasks'
`ContentProjectGUID`. No documented type is missing from the metadata.

---

## 4. Public type parity matrix

Generated — see `docs/xna-content-pipeline-parity-report.md` §1 (`XNAPP-020`). The statuses:

| Status | Meaning |
|---|---|
| `EXACT_EQUIVALENT` | Same name, same members (under the CNA property convention), same observable behaviour. |
| `SEMANTIC_EQUIVALENT` | C++ cannot spell it the same way (attribute → descriptor, `IEnumerable<T>` → range, delegate → callable, reflection → descriptor/registry) but every developer-visible capability exists and is tested. The row says which substitution. |
| `HOST_SUBSTITUTION` | The Microsoft mechanism exists only to integrate with a Microsoft host (MSBuild, Visual Studio, CLR binary serialization). CNA provides the developer-visible capability another way and the row says how. |
| `EXTERNAL_BLOCKED` | Needs a component that is legally or physically unavailable here; the row names it exactly. |
| `MISSING` | Not there. **Zero allowed at completion.** |

Types by namespace (denominator): `…Pipeline` 32 · `…Audio` 5 · `…Graphics` 47 · `…Processors`
28 · `…Serialization.Compiler` 5 · `…Serialization.Intermediate` 7 · `…Tasks` 4.

## 5. Public member parity matrix

Generated — report §2. Denominator 708 members + 27 enum values. Two mechanical rules, stated in
the report rather than applied silently: (a) a delegate type's `.ctor(Object, IntPtr)`,
`BeginInvoke`, `EndInvoke` are CLR plumbing and count as one item, the `Invoke` signature, which
maps to a C++ callable; (b) `System.Runtime.Serialization` members on the two exception types
(`GetObjectData`, the `(SerializationInfo, StreamingContext)` constructor) are
`HOST_SUBSTITUTION` — .NET binary serialization has no C++ counterpart, and the exceptions'
developer-visible contract (message, identity, inner exception) is fully provided.

## 6. Attribute parity matrix

C++ has no CLR attributes; each attribute becomes a **descriptor** of the same name whose
properties keep the attribute's names, consumed by an explicit registration API instead of a
reflective scan. Every attribute row is therefore `SEMANTIC_EQUIVALENT` at best, and the report
says so.

| XNA attribute | Where it applies | C++ descriptor / mechanism |
|---|---|---|
| `ContentImporterAttribute` (`FileExtensions`, `DefaultProcessor`, `DisplayName`, `CacheImportedData`) | importer classes | `ContentImporterAttribute` descriptor passed to `PipelineComponentRegistry::RegisterImporter<T>()` (§17) |
| `ContentProcessorAttribute` (`DisplayName`) | processor classes | `ContentProcessorAttribute` descriptor, same registry |
| `ContentTypeWriterAttribute` | writer classes | descriptor at `ContentCompiler` writer registration (§12) |
| `ContentTypeSerializerAttribute` | intermediate serializers | descriptor at `IntermediateSerializer` registration (§13) |
| `ContentSerializerAttribute` (`ElementName`, `CollectionItemName`, `AllowNull`, `Optional`, `SharedResource`, `FlattenContent`) | members of user types | per-member descriptor in the C++ type-description system (§13) |
| `ContentSerializerIgnoreAttribute` | members | per-member descriptor flag |
| `ContentSerializerCollectionItemNameAttribute` | collection types | per-type descriptor |
| `ContentSerializerRuntimeTypeAttribute` | types | per-type descriptor (runtime type name for the XNB) |
| `ContentSerializerTypeVersionAttribute` | types | per-type descriptor (reader version) |

## 7. Importer inventory

Read from `ContentImporterAttribute` metadata (through the `Localized*` subclass, whose public
properties resolve the resource-backed display name), 2026-09-05:

| XNA importer | Assembly | Extensions | DisplayName | DefaultProcessor | CacheImportedData | Output type |
|---|---|---|---|---|---|---|
| `EffectImporter` | EffectImporter | `.fx` | Effect - XNA Framework | `EffectProcessor` | false | `EffectContent` |
| `FbxImporter` | FBXImporter | `.fbx` | Autodesk FBX - XNA Framework | `ModelProcessor` | **true** | `NodeContent` |
| `FontDescriptionImporter` | Pipeline | `.spritefont` | Sprite Font Description - XNA Framework | `FontDescriptionProcessor` | false | `FontDescription` |
| `Mp3Importer` | AudioImporters | `.mp3` | MP3 Audio File - XNA Framework | `SongProcessor` | false | `AudioContent` |
| `TextureImporter` | TextureImporter | `.bmp .dds .dib .hdr .jpg .pfm .png .ppm .tga` | Texture - XNA Framework | `SpriteTextureProcessor` | false | `TextureContent` |
| `WavImporter` | AudioImporters | `.wav` | WAV Audio File - XNA Framework | `SoundEffectProcessor` | false | `AudioContent` |
| `WmaImporter` | AudioImporters | `.wma` | WMA Audio File - XNA Framework | `SongProcessor` | false | `AudioContent` |
| `WmvImporter` | VideoImporters | `.wmv` | WMV Video File - XNA Framework | `VideoProcessor` | false | `VideoContent` |
| `XImporter` | XImporter | `.x` | X File - XNA Framework | `ModelProcessor` | **true** | `NodeContent` |
| `XmlImporter` | Pipeline | `.xml` | XML Content - XNA Framework | *(none)* | false | `Object` |

Two facts worth noticing before implementing: `TextureImporter`'s default processor is
**`SpriteTextureProcessor`** (mip generation off, `Color` output), not `TextureProcessor`; and
`XmlImporter` declares **no** default processor, so a `.xml` asset with no explicit processor is
passed through unprocessed by the host.

## 8. Importer extension / input-format inventory

**The input-parity denominator is 18 extensions**, one importer each (no extension is claimed by
two built-in importers). Status per extension is generated (report §4) from
`tests/reference/xna40/content-pipeline-inputs.json` (`XNAPP-021`), which also records for each
extension the fixture, the importer test, the processor test, the source→XNB test, the
source→CNB test, the malformed-input tests and the target/profile tests §22 demands.

| Extension | XNA importer | CNA route today (2026-09-05, before this plan) |
|---|---|---|
| `.bmp` `.jpg` `.png` `.tga` `.hdr` | `TextureImporter` | `ImageImporter` (stb) — present, behaviour parity unmeasured |
| `.ppm` | `TextureImporter` | stb decodes binary PNM; `.ppm` extension not registered |
| `.dib` | `TextureImporter` | not registered (BMP variant) |
| `.dds` | `TextureImporter` | **absent** (only a DXT *decoder* exists) |
| `.pfm` | `TextureImporter` | **absent** |
| `.spritefont` | `FontDescriptionImporter` | present (FreeType) — schema parity unmeasured |
| `.fx` | `EffectImporter` | present (external compiler) |
| `.wav` | `WavImporter` | present (PCM 8/16/24/32/float) |
| `.mp3` | `Mp3Importer` | **absent** as a decoded route |
| `.wma` | `WmaImporter` | **absent** |
| `.wmv` | `WmvImporter` | **absent** as an XNA-semantics route |
| `.fbx` | `FbxImporter` | **absent** |
| `.x` | `XImporter` | **absent** |
| `.xml` | `XmlImporter` | **absent** |

`.gltf`/`.glb`/`.cnj`/`.xnb`/`.fxb` remain CNA extensions above the XNA set and are not in the
denominator.

## 9. Processor inventory

12 public concrete processors. Three of them (`MaterialProcessor`, `ModelTextureProcessor`,
`SpriteTextureProcessor`) carry **no** `ContentProcessorAttribute` — they are public, derivable
and named as defaults, but not "browsable" components.

| XNA processor | DisplayName | Input → Output | Properties |
|---|---|---|---|
| `EffectProcessor` | Effect - XNA Framework | `EffectContent` → `CompiledEffectContent` | 2 |
| `FontDescriptionProcessor` | Sprite Font Description - XNA Framework | `FontDescription` → `SpriteFontContent` | 0 |
| `FontTextureProcessor` | Sprite Font Texture - XNA Framework | `Texture2DContent` → `SpriteFontContent` | 3 |
| `MaterialProcessor` | *(none)* | `MaterialContent` → `MaterialContent` | 7 |
| `ModelProcessor` | Model - XNA Framework | `NodeContent` → `ModelContent` | 14 |
| `ModelTextureProcessor` | *(none)* | `TextureContent` → `TextureContent` | 6 |
| `PassThroughProcessor` | No Processing Required | `Object` → `Object` | 0 |
| `SongProcessor` | Song - XNA Framework | `AudioContent` → `SongContent` | 1 |
| `SoundEffectProcessor` | Sound Effect - XNA Framework | `AudioContent` → `SoundEffectContent` | 1 |
| `SpriteTextureProcessor` | *(none)* | `TextureContent` → `TextureContent` | 6 |
| `TextureProcessor` | Texture - XNA Framework | `TextureContent` → `TextureContent` | 6 |
| `VideoProcessor` | Video - XNA Framework | `VideoContent` → `VideoContent` | 1 |

## 10. Processor property / default inventory

Black-box: each processor constructed with its parameterless constructor, each public property
read back (`defaultValueSource` in the JSON). These are the values a `.contentproj` gets when it
says nothing.

| Processor | Property | Type | XNA default |
|---|---|---|---|
| `EffectProcessor` | `DebugMode` | `EffectProcessorDebugMode` | `Auto` |
| | `Defines` | `String` | `null` |
| `FontTextureProcessor` | `FirstCharacter` | `Char` | `' '` (U+0020) |
| | `PremultiplyAlpha` | `Boolean` | `true` |
| | `TextureFormat` | `TextureProcessorOutputFormat` | `Color` |
| `MaterialProcessor` | `ColorKeyColor` | `Color` | `{255,0,255,255}` |
| | `ColorKeyEnabled` | `Boolean` | `true` |
| | `DefaultEffect` | `MaterialProcessorDefaultEffect` | `BasicEffect` |
| | `GenerateMipmaps` | `Boolean` | `true` |
| | `PremultiplyTextureAlpha` | `Boolean` | `true` |
| | `ResizeTexturesToPowerOfTwo` | `Boolean` | `false` |
| | `TextureFormat` | `TextureProcessorOutputFormat` | `DxtCompressed` |
| `ModelProcessor` | `ColorKeyColor` | `Color` | `{255,0,255,255}` |
| | `ColorKeyEnabled` | `Boolean` | `true` |
| | `DefaultEffect` | `MaterialProcessorDefaultEffect` | `BasicEffect` |
| | `GenerateMipmaps` | `Boolean` | `true` |
| | `GenerateTangentFrames` | `Boolean` | `false` |
| | `PremultiplyTextureAlpha` | `Boolean` | `true` |
| | `PremultiplyVertexColors` | `Boolean` | `true` |
| | `ResizeTexturesToPowerOfTwo` | `Boolean` | `false` |
| | `RotationX` / `RotationY` / `RotationZ` | `Single` | `0` |
| | `Scale` | `Single` | `1` |
| | `SwapWindingOrder` | `Boolean` | `false` |
| | `TextureFormat` | `TextureProcessorOutputFormat` | `DxtCompressed` |
| `ModelTextureProcessor` | `ColorKeyColor` / `ColorKeyEnabled` / `GenerateMipmaps` / `PremultiplyAlpha` / `ResizeToPowerOfTwo` / `TextureFormat` | | `{255,0,255,255}` / `true` / `true` / `true` / `false` / `DxtCompressed` |
| `SongProcessor` | `Quality` | `ConversionQuality` | `Best` |
| `SoundEffectProcessor` | `Quality` | `ConversionQuality` | `Best` |
| `SpriteTextureProcessor` | `ColorKeyColor` / `ColorKeyEnabled` / `GenerateMipmaps` / `PremultiplyAlpha` / `ResizeToPowerOfTwo` / `TextureFormat` | | `{255,0,255,255}` / `true` / **`false`** / `true` / `false` / **`Color`** |
| `TextureProcessor` | `ColorKeyColor` / `ColorKeyEnabled` / `GenerateMipmaps` / `PremultiplyAlpha` / `ResizeToPowerOfTwo` / `TextureFormat` | | `{255,0,255,255}` / `true` / `false` / `true` / `false` / `Color` |
| `VideoProcessor` | `VideoSoundtrackType` | `Media.VideoSoundtrackType` | `Music` |

Two of these already contradict CNA's current `TextureProcessor` parameters: XNA's
`ColorKeyEnabled` defaults **true** with magenta, where CNA's `colorKey` is absent by default;
and CNA has no `SpriteTextureProcessor`/`ModelTextureProcessor` distinction at all. `XNAPP-131`
resolves this without breaking the CNB route.

Enumerations (from metadata): `TargetPlatform {Windows=0, Xbox360=1, WindowsPhone=2}`;
`ConversionFormat {Pcm, Adpcm, WindowsMedia, Xma}`; `ConversionQuality {Low, Medium, Best}`;
`AudioFileType {Wav, Mp3, Wma}`; `TextureProcessorOutputFormat {NoChange, Color, DxtCompressed}`;
`MaterialProcessorDefaultEffect {BasicEffect, SkinnedEffect, EnvironmentMapEffect,
DualTextureEffect, AlphaTestEffect}`; `EffectProcessorDebugMode {Auto, Debug, Optimize}`;
`FontDescriptionStyle {Regular, Bold, Italic}` (three values — `BoldItalic` is not in XNA, and
CNA's current `FontDescriptionStyle` has it; `XNAPP-181`).

## 11. Intermediate content type inventory

The 47 `…Graphics` types, `VideoContent`, the 5 `…Audio` types, `ContentItem`, `ExternalReference<T>`,
`OpaqueDataDictionary`, `NamedValueDictionary<T>`, `ChildCollection<TParent,TChild>` and the
`…Processors` output types (`ModelContent` graph, `SpriteFontContent`, `SoundEffectContent`,
`SongContent`, `CompiledEffectContent`, `VertexBufferContent`, `VertexDeclarationContent`). Full
member lists are in the inventory; the report shows each as it is mapped. Phases 6–9.

## 12. Serialization.Compiler API inventory

`ContentCompiler` (1 public method, `GetTypeWriter`), `ContentTypeWriter` (9),
`ContentTypeWriter<T>` (3), `ContentTypeWriterAttribute` (1), `ContentWriter` (15, derives
`BinaryWriter`). Phase 4 maps these onto `CNA::Internal::Xnb::XnbWriter`/`XnbTypeWriter`: the
XNA `ContentWriter` becomes a façade over the canonical byte writer, `ContentTypeWriter<T>` a
compatibility base class whose `Write` is adapted into an `XnbTypeWriter` registration, and the
runtime reader/type names come from `XnbReaderIdentity`.

## 13. Serialization.Intermediate API inventory

`IntermediateSerializer` (3: `Serialize`, `Deserialize`, `GetTypeSerializer`), `IntermediateReader`
(14), `IntermediateWriter` (9), `ContentTypeSerializer` (10), `ContentTypeSerializer<T>` (10),
`ContentTypeSerializerAttribute` (1), `ChildCallback`. **This is the largest gap: nothing exists in
CNA.** Phase 5 builds a C++ type-description system (descriptors, not reflection) that yields
XNA-shaped XML (`<XnaContent><Asset Type="…">…</Asset></XnaContent>`), shared resources,
external references, polymorphism via `Type=` attributes, collections, dictionaries, nullable,
enums, and the `ContentSerializer*` attribute semantics — verified against XML that the genuine
`IntermediateSerializer` writes for the same graphs (§23).

## 14. Graphics pipeline object model inventory

`BitmapContent`, `PixelBitmapContent<T>`, `DxtBitmapContent` + `Dxt1/3/5BitmapContent`,
`MipmapChain`, `MipmapChainCollection`, `TextureContent`, `Texture2DContent`, `Texture3DContent`,
`TextureCubeContent`, `TextureReferenceDictionary`; `FontDescription`, `FontDescriptionStyle`;
`MaterialContent` + `Basic/AlphaTest/DualTexture/EnvironmentMap/Skinned/EffectMaterialContent`,
`EffectContent`; `NodeContent`, `NodeContentCollection`, `BoneContent`, `MeshContent`,
`GeometryContent`, `GeometryContentCollection`, `VertexContent`, `VertexChannel`,
`VertexChannel<T>`, `VertexChannelCollection`, `VertexChannelNames`, `PositionCollection`,
`IndirectPositionCollection`, `IndexCollection`, `BoneWeight`, `BoneWeightCollection`,
`MeshBuilder`, `MeshHelper`, `VectorConverter`; `AnimationContent`, `AnimationContentDictionary`,
`AnimationChannel`, `AnimationChannelDictionary`, `AnimationKeyframe`. Phases 6 and 8.

## 15. Audio/media pipeline inventory

`AudioContent` (11), `AudioFormat` (7), `AudioFileType`, `ConversionFormat`, `ConversionQuality`;
`VideoContent` (9); processors `SoundEffectProcessor`, `SongProcessor`, `VideoProcessor` and
their outputs. Phases 9, 13, 14.

## 16. Model / mesh / material / animation inventory

### 16.1 Importers
`FbxImporter` (`.fbx`, FBX SDK 2011.3.1 embedded — FBX 2011 binary and ASCII, older versions the
SDK reads), `XImporter` (`.x`, D3DX text and binary `.x`).

### 16.2 Processors and outputs
`ModelProcessor` (14 properties), `MaterialProcessor`, `ModelTextureProcessor`; `ModelContent`,
`ModelBoneContent(Collection)`, `ModelMeshContent(Collection)`, `ModelMeshPartContent(Collection)`,
`VertexBufferContent`, `VertexDeclarationContent`.

### 16.3 External types the public surface names (66)
Runtime types every façade signature needs, all already in CNA: `Vector2/3/4`, `Matrix`,
`Quaternion`, `Color`, `Rectangle`, `BoundingSphere`, `SurfaceFormat`, `VertexElement`,
`VertexElementFormat`, `VertexElementUsage`, `CompareFunction`, `GraphicsProfile`,
`VideoSoundtrackType`, `ContentSerializerAttribute`; BCL types map to sharp-runtime
(`TimeSpan`, `IDisposable`, `IEquatable<T>`, collections, `XmlReader`/`XmlWriter` → CNA's own
XML layer, `BinaryWriter`) and the three `Microsoft.Build.*` host types (§18).

## 17. Build context / extensibility inventory

`ContentImporterContext` (5), `ContentProcessorContext` (17: `AddDependency`, `AddOutputFile`,
`BuildAndLoadAsset<TInput,TOutput>` ×2, `BuildAsset<TInput,TOutput>` ×2, `Convert<TInput,TOutput>`,
`BuildConfiguration`, `IntermediateDirectory`, `Logger`, `OutputDirectory`, `OutputFilename`,
`Parameters`, `TargetPlatform`, `TargetProfile`), `ContentBuildLogger` (8), `IContentImporter`,
`IContentProcessor`, `ContentImporter<T>`, `ContentProcessor<TInput,TOutput>`,
`PipelineComponentScanner` (12), `ProcessorParameter` (7), `ProcessorParameterCollection`,
`ContentIdentity`, `ContentItem`, `InvalidContentException`, `PipelineException`,
`ExternalReference<T>`, `OpaqueDataDictionary`, `TargetPlatform`. Phase 3.

## 18. `.contentproj` / MSBuild workflow compatibility

The public schema, from `Microsoft.Xna.GameStudio.ContentPipeline.targets` and the sample
projects: `<Compile Include="…">` items with `<Name>`, `<Importer>`, `<Processor>`,
`<ProcessorParameters_<Property>>`, `<Link>`, `<CopyToOutputDirectory>`; `<Content>`/`<None>`
items copied not built; `<XnaPlatform>` (`Windows` | `Xbox 360` | `Windows Phone`, normalised to
`Windows`/`Xbox360`/`WindowsPhone`), `<XnaProfile>` (`Reach`/`HiDef`), `<XnaCompressContent>`,
`<ContentRootDirectory>`, `<XnaFrameworkVersion>`, `<Reference>`/`<ProjectReference>` to
pipeline assemblies. The four `Tasks` types (`BuildContent` 19 members, `BuildXact` 16,
`CleanContent` 9, `GetLastOutputs` 5) are `HOST_SUBSTITUTION`: their *inputs* become the
`.contentproj` compatibility parser's fields and `cna-content`'s options; their *outputs*
(intermediate files, output files, rebuilt files) become the manifest. Phase 16.

## 19. Target-platform matrix

`TargetPlatform {Windows, Xbox360, WindowsPhone}`. Per importer/processor/writer the report's
§6 table (generated from `content-pipeline-inputs.json` and the writer registry) records
`implemented` / `verified-xna` / `blocked` per target. Windows first (Phase 21); Windows Phone
(Phase 22) and Xbox 360 (Phase 23) are implemented from `BuildContent` output for those targets,
which the SDK can produce without hardware, and only their *device* verification is
`EXTERNAL_BLOCKED`.

## 20. GraphicsProfile matrix

`Reach` / `HiDef` restrictions enforced by processors and writers: texture size and
non-power-of-two limits, `SurfaceFormat` availability, vertex/index counts, effect profiles
(`vs_2_0`/`ps_2_0` vs `vs_3_0`/`ps_3_0`), `Texture3D`/`TextureCube` support. Measured by building
the same fixtures under both profiles through `BuildContent` (Phase 17/19).

## 21. Source-format behavioral matrix

Per extension: dimensions, channels, alpha, gamma/colour interpretation, source mips, compressed
data, cube/volume, format conversion, colour key, premultiply, resize, mip generation; audio
channel/bit-depth/sample-rate/loop/duration; video container/codec/dimensions/frame rate/duration;
model hierarchy/bones/transforms/channels/skin/animation/materials/coordinate system/winding.
Rows are generated from `content-pipeline-inputs.json` and the differential harness results (§23).

## 22. Custom importer / processor / writer support

A third-party CNA application must be able to define a custom intermediate C++ type, register an
importer for a custom extension, process it, write it to XNB with a custom runtime reader name,
declare dependencies, build nested assets, use shared resources and external references,
participate in incremental fingerprints and emit diagnostics — **without touching CNA
internals**. Phase 18 builds that as an external-style CMake project under
`tests/assets/content_pipeline_custom_cmake/` (the fixture directory that already exists for the
canonical engine) and runs it as a ctest.

## 23. Differential XNA oracle plan

`tools/xna-pipeline-oracle/differential/`: a C# driver compiled with `mcs`, executed under Wine,
that runs Microsoft's `BuildContent` task over a committed synthetic corpus for each
(importer, processor, parameters, platform, profile) case and records the `.xnb`, diagnostics and
dependency list; `cna-content` builds the same corpus; both outputs are parsed by the independent
Python XNB parser (`tools/xnb/xnb_conformance.py`, extended per type) into a normalized semantic
form and compared. Byte equality is asserted only where serialization is deterministic and
expected identical (it already is for `List<string>`, `Texture2D`, `SoundEffect`).

## 24. XNB semantic comparison strategy

Compare: root reader; type-reader table (names, versions, order); object graph; texture format,
dimensions, mip count and pixels; audio format block, loop region, duration; SpriteFont tables;
Model bone/mesh/part graph, declarations, buffers, effects; Effect bytecode container header and
parameter/technique reflection; shared-resource count and order; external references. Every
difference is a row in the report with one of three outcomes: CNA fixed, XNA divergence taken
deliberately (recorded here and in `plans/plan_bindings_upstream.md`), or open.

## 25. Runtime interoperability

`tests/interop/xna40/` (`XNAP-32`/`33`) exists and has never run. Phase 20 compiles it with the
in-prefix `csc.exe`, runs it under Wine against the committed CNA fixture corpus with a real
D3D9 device (DXVK, `Xvfb :131`), and records the result in `XNAP-34`'s row and here. Every
output family this plan adds (generic/custom data, `Texture2D`, `Texture3D`, `TextureCube`,
`SpriteFont`, `SoundEffect`, `Song`, `Video`, `Curve`, `Model`, `Effect`) gets a fixture and a
value assertion in that harness.

## 26. Dependency / license inventory

Every new third-party dependency gets a row here **before** it is used (project, version/commit,
licence, vendored or system, build-time or runtime, redistributable, optional or required, reason,
alternatives). Build-time libraries never enter a runtime game's link closure
(`CnaXnbDependencyBoundary` already enforces this for FreeType). Candidates to evaluate, not
decisions: a native FBX parser (`ufbx`, MIT) for §16; DDS is written in-house; PFM is trivial;
MP3 decoding (`minimp3`, CC0, or `dr_mp3`, public domain); WMA/WMV have no free decoder and are
the likeliest `EXTERNAL_BLOCKED` rows — the plan will first establish through `BuildContent`
what XNA's `SongProcessor`/`VideoProcessor` actually *emit* for them.

## 27. Fuzz / security strategy

All new source importers and the intermediate XML serializer treat input as untrusted: checked
arithmetic, explicit ceilings, recursion depth, cyclic graphs, path traversal, no XML external
entities, huge dimensions, malformed UTF-8, NaN/Inf where invalid, malformed vertex/index ranges,
excessive bone/animation counts, malformed FBX/X/DDS/WAV/MP3 headers, hostile shader includes.
Sanitizer runs and fuzz targets for every parser with structure (DDS, FBX, X, XML, MP3 framing).
Phase 21.

## 28. Cross-platform build strategy

Linux is the development host; Windows and macOS builds of the compiler must stay possible
(`HostProcess.cpp` already has both). Microsoft XNA is an optional reference oracle, never a
dependency of `cna-content`. A Linux host must produce Windows-target XNB whenever the source
format's dependencies are available.

## 29. Completion gates

| Gate | Tool | Fails when |
|---|---|---|
| API parity | `parity_report.py --gate` | any type or member is `MISSING`, or a status lacks its required note, or the map names a member the inventory does not have |
| importer parity | same | any of the 10 importers lacks a CNA class |
| processor parity | same | any of the 12 processors or 47 properties is unmapped or has a default that differs from XNA's without a recorded decision |
| input-extension parity | `content-pipeline-inputs.json` check | any of the 18 extensions lacks `IMPLEMENTED+TESTED` or `EXTERNAL_BLOCKED` with an exact reason, or a claimed test does not exist in the tree |
| intermediate serializer | `CnaContentPipelineTests` suite | any §13 feature fails |
| custom extension | ctest external project | fails to build/run |
| source → XNB / CNB integration | `CnaContentPipelineTests`, `CnaContentTests` | |
| independent conformance | `tools/xnb/xnb_conformance.py` | |
| differential oracle | `differential/compare.py` | a semantic difference without a recorded decision |
| runtime harness | `tests/interop/xna40` under Wine | any fixture fails to load or a value differs |
| sanitizers, fuzz, determinism, full suites, `git diff --check`, licence audits, provenance grep, single-writer test, dependency boundary | as in Phase 25 | |

## 30. External blockers

Only these qualify: Xbox 360 hardware/devkit runtime for *device* verification of `x` output;
Windows Phone 7 device/emulator for *device* verification of `m` output; a proprietary codec whose
decoder is legally unavailable (to be established, not assumed, for WMA/WMV). "Difficult", "old",
"needs a parser", "nobody uses it" are implementation tasks, not blockers.

## 31. Exact final denominator and coverage counts

Filled by `parity_report.py` into `docs/xna-content-pipeline-parity-report.md` and quoted here at
each phase close. **Current (2026-09-05): types 0/128, members 0/708 (+0/27 enum values),
importers 0/10, extensions 0/18, processors 0/12, properties 0/47, intermediate-serializer
features 0/§13, targets verified 0/3, black-box-verified families 0.** The previous plan's
routes exist and work, but no XNA-namespaced type exists yet, so by this plan's definition the
API counts start at zero; the input/processor counts start at zero because no row has yet passed
this plan's `IMPLEMENTED+TESTED` bar, which requires a fixture, an importer test, a processor
test, both output tests and a malformed-input test *for that extension*.

---

## 32. Task log

Legend: `[ ]` open · `[x]` complete · `[~]` partial (detail in the row) · `[!]` blocked (blocker
named in the row). ID ranges: Phase 0 `001–009`, 1 `010–019`, 2 `020–029`, 3 `030–059`,
4 `060–069`, 5 `070–089`, 6 `090–129`, 7 `130–149`, 8 `150–159`, 9 `160–164`, 10 `165–179`,
11 `180–189`, 12 `190–199`, 13 `200–219`, 14 `220–229`, 15 `230–239`, 16 `240–249`,
17 `250–259`, 18 `260–264`, 19 `265–279`, 20 `280–289`, 21 `290–299`, 22 `300–309`,
23 `310–319`, 24 `320–329`, 25 `330–339`.

### Phase 0 — repository and provenance audit

| ID | Task | State |
|---|---|---|
| `XNAPP-001` | Verify branch/HEAD/tree/upstream/identity; confirm `xnapipeline` is a feature branch (same commit as `next`, `e1d3aa5d5`, clean, no upstream, author Robert Vokac). | [x] |
| `XNAPP-002` | Read the governing documents and the five sibling plans; record the boundary in this file's preamble. | [x] |
| `XNAPP-003` | Audit the canonical engine and confirm one pipeline, one XNB writer, and the build-time/runtime module split (§1 boundaries; `RegisterBuiltInContentPipeline` in `tools/content/content.cpp` is the single registration point). | [x] |
| `XNAPP-004` | Environment audit for oracles (§3.2): SDK, runtime, Wine .NET 4.0, mcs, BuildContent precedent. Recorded in memory so a later session cannot regress to "unavailable". | [x] |
| `XNAPP-005` | Configure the stable build directory `cmake-build-debug/` (Ninja, Debug, HEADLESS, SDL3, ccache, `CNA_TEST_DISPLAY=:99`, `CNA_ENABLE_DRACO=OFF`, `CNA_SHARP_RUNTIME_ROOT` = the `next`-era `sharp-runtimenext` checkout because `develop`'s sharp-runtime lacks `StoragePaths::SetIsolatedStorageRootOverride`). | [x] |
| `XNAPP-006` | Record the test baseline of `CnaContentTests` and `CnaContentPipelineTests` in this environment before any change (§33). | [ ] |
| `XNAPP-007` | Create this plan; index it in `plans/README.md`. | [x] |
| `XNAPP-008` | Provenance gate: a test that fails if any Microsoft binary (`Microsoft.Xna.*.dll`, `XnaNative.dll`, `*.exe` from the SDK) or a Microsoft font appears under `tests/`, `tools/`, `modules/`, `docs/`. | [ ] |

### Phase 1 — authoritative XNA public API inventory

| ID | Task | State |
|---|---|---|
| `XNAPP-010` | `PipelineApiOracle.cs`: reflection-only, metadata + black-box processor defaults, no XNA compile-time reference, deterministic JSON. | [x] 128 types, 708 members. Attribute detection walks the base chain because the built-ins carry internal `Localized*Attribute` subclasses; `IsVisibleType` requires the whole declaring chain to be reachable, which removed 39 types nobody can name (listed in the JSON with the rule). |
| `XNAPP-011` | `run-oracle.sh`: mcs + Wine .NET 4.0; Microsoft binaries only under ignored `build/`. | [x] |
| `XNAPP-012` | `merge_xml_docs.py`: join with the official XML docs; report both directions of disagreement. | [x] 120/128 types documented; the 8 undocumented are the importer classes; 665/708 members documented. |
| `XNAPP-013` | Determinism: two oracle runs produce identical bytes. | [x] `cmp` identical. |
| `XNAPP-014` | `parity_report.py` + `content-pipeline-parity-map.json`: per-type and per-member statuses joined to the inventory, coverage computed mechanically, `--gate` mode, generated `docs/xna-content-pipeline-parity-report.md`. Every inventory type and member must appear in the map; the map may not name anything the inventory lacks. | [ ] |
| `XNAPP-015` | ctest `CnaXnaPipelineParityInventoryConsistency`: the committed inventory's assembly SHA-256 set is the one this plan quotes; the map covers the inventory; this file's §31 numbers equal the report's. | [ ] |
| `XNAPP-016` | Inventory freeze: after `XNAPP-014`/`015`, declare the denominator frozen at the SHA-256 set in the JSON. Later regeneration is a recorded event, not a silent change. | [ ] |

### Phase 2 — importer / extension / processor inventory

| ID | Task | State |
|---|---|---|
| `XNAPP-020` | Generate §4–§10 tables from the inventory into the report; this plan quotes them. | [ ] |
| `XNAPP-021` | `tests/reference/xna40/content-pipeline-inputs.json`: the 18-extension matrix with, per extension, importer, default processor, fixture path, six test names, per-target status, and a status word; a checker that verifies every named test exists in the tree and every extension is present. | [ ] |
| `XNAPP-022` | Black-box corpus plan: the synthetic fixture set per extension (§23), authored or generated by CNA, with provenance file; nothing third-party without a licence row. | [ ] |
| `XNAPP-023` | Audit CNA's current routes against §7/§9/§10 and record every default that differs (`ColorKeyEnabled`, `SpriteTextureProcessor` defaulting, `FontDescriptionStyle.BoldItalic`, `.ppm`/`.dib` registration, …) as decisions to take in their phases. | [ ] |

### Phase 3 — C++ public compatibility façade: pipeline core

Location: `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/` (build-time
module) with `src/Xna/`; `ContentSerializer*` descriptors in `modules/content/include/Microsoft/Xna/Framework/Content/`
(runtime module) because game types carry them. Namespace `Microsoft::Xna::Framework::Content::Pipeline`.

| ID | Task | State |
|---|---|---|
| `XNAPP-030` | Façade design note in `docs/xna-content-pipeline-compat-api.md`: property convention, attribute→descriptor, generics, `IEnumerable<T>`→ranges, delegates→callables, nullable→`std::optional`, how an XNA-style importer/processor is adapted into the canonical registry, and what a C++ user must write. | [ ] |
| `XNAPP-031` | `ContentIdentity` (4 ctors, 3 properties), `ContentItem` (Identity, Name, OpaqueData). | [ ] |
| `XNAPP-032` | `OpaqueDataDictionary` (+ `NamedValueDictionary<T>` generic base, 16 members) and `GetValue<T>(key, default)`. | [ ] |
| `XNAPP-033` | `ChildCollection<TParent,TChild>` (parent back-pointer semantics, `GetParent`/`SetParent` protected). | [ ] |
| `XNAPP-034` | `InvalidContentException` (6 ctors, `ContentIdentity`, `GetObjectData` as `HOST_SUBSTITUTION`), `PipelineException` (5 ctors incl. format overload). | [ ] |
| `XNAPP-035` | `ContentBuildLogger` (LoggerRootDirectory, LogMessage/LogImportantMessage/LogWarning, PushFile/PopFile, GetCurrentFilename) bridged to `CNA::Content::Pipeline::ContentBuildLogger`. | [ ] |
| `XNAPP-036` | `TargetPlatform` enum (3 values, exact numbering) mapped to `XnbTargetPlatform`. | [ ] |
| `XNAPP-037` | `ContentImporterAttribute`, `ContentProcessorAttribute` descriptors; `IContentImporter`, `IContentProcessor`. | [ ] |
| `XNAPP-038` | `ContentImporter<T>` and `ContentProcessor<TInput,TOutput>` compatibility bases with the exact `Import(filename, context)` / `Process(input, context)` shape, adapted into the canonical `ContentImporter`/`ContentProcessor` by a generic bridge (stable type identity derived from a descriptor, not RTTI). | [ ] |
| `XNAPP-039` | `ContentImporterContext` (IntermediateDirectory, Logger, OutputDirectory, AddDependency). | [ ] |
| `XNAPP-040` | `ContentProcessorContext` (17 members): `AddDependency`, `AddOutputFile`, `BuildAsset`/`BuildAndLoadAsset`/`Convert` with nested-build semantics on the canonical build graph, `Parameters` (`OpaqueDataDictionary`), platform/profile/configuration/directories/filename. | [ ] |
| `XNAPP-041` | `ExternalReference<T>` (3 ctors, Filename; relative-path resolution against the referencing identity). | [ ] |
| `XNAPP-042` | `ProcessorParameter` (7), `ProcessorParameterCollection`; `PipelineComponentScanner` (12) as the explicit registry's enumeration view. | [ ] |
| `XNAPP-043` | `VideoContent` (7 properties, `Dispose`). | [ ] |
| `XNAPP-044` | Nested build semantics: circular dependency detection, nested output naming (`OutputFilename`), caching within one build, `ContentIdentity` propagation into diagnostics. | [ ] |
| `XNAPP-045` | Compile-parity tests for every Phase 3 type (construct/derive/static_assert), plus behaviour tests. | [ ] |

### Phase 4 — Serialization.Compiler parity

| ID | Task | State |
|---|---|---|
| `XNAPP-060` | `ContentWriter` façade (15 members: `Write` overloads for `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `WriteObject<T>` ×2, `WriteRawObject<T>` ×2, `WriteSharedResource<T>`, `WriteExternalReference<T>`, `TargetPlatform`, `TargetProfile`, `Dispose(bool)`) over `XnbWriter`. | [ ] |
| `XNAPP-061` | `ContentTypeWriter` (9) and `ContentTypeWriter<T>` (3) compatibility bases: `GetRuntimeReader`, `GetRuntimeType`, `ShouldCompressContent`, `TypeVersion`, `CanDeserializeIntoExistingObject`, `Initialize`, `Write`; adapter to `XnbTypeWriter`. | [ ] |
| `XNAPP-062` | `ContentTypeWriterAttribute` descriptor + `ContentCompiler::GetTypeWriter(Type)` over the registry. | [ ] |
| `XNAPP-063` | A user-defined `ContentTypeWriter<T>` round-trips through the real `.xnb` route and loads in CNA's reader; compile-parity tests. | [ ] |

### Phase 5 — Serialization.Intermediate parity

| ID | Task | State |
|---|---|---|
| `XNAPP-070` | C++ type-description system: per-type descriptors (element name, runtime type, type version, collection item name, serializer), per-member descriptors (name, element name, optional, allow-null, shared-resource, flatten, ignore), reusable by custom content. | [ ] |
| `XNAPP-071` | `IntermediateSerializer::Serialize/Deserialize/GetTypeSerializer`; `IntermediateWriter` (9); `IntermediateReader` (14); `ContentTypeSerializer`/`<T>` bases; `ContentTypeSerializerAttribute`. | [ ] |
| `XNAPP-072` | Primitives, strings, enums, arrays, lists, dictionaries, nullable, nested types, inheritance/polymorphism (`Type=`), shared resources (`<Resources>`), external references, optional/ignored/flattened/allow-null members, collection item names, `XnaContent`/`Asset` envelope, namespace aliases (`xmlns`), error location/identity. | [ ] |
| `XNAPP-073` | Built-in serializers for every math/framework type XNA serializes inline (`Vector*`, `Matrix`, `Quaternion`, `Color`, `Rectangle`, `Point`, `BoundingBox`, `Curve`, `TimeSpan`, …). | [ ] |
| `XNAPP-074` | Verify against XML the genuine `IntermediateSerializer` writes for the same graphs (§23), and XML the genuine `XmlImporter` accepts; record every difference. | [ ] |
| `XNAPP-075` | Hardening: no DOCTYPE/external entities, depth/size ceilings, cyclic shared resources, malformed numbers; fuzz target. | [ ] |

### Phase 6 — graphics intermediate content API (47 types)

| ID | Task | State |
|---|---|---|
| `XNAPP-090` | `BitmapContent` (13), `PixelBitmapContent<T>` (12, all XNA-permitted `T`: `Color`, `Vector4`, `Single`, `Byte`, `Rgba64`, `Bgr565`, …), `Copy`/`TryCopyFrom`/`TryCopyTo`/`GetPixelData`/`SetPixelData`, `ReplaceColor`. | [ ] |
| `XNAPP-091` | `DxtBitmapContent` + `Dxt1/3/5` (using the existing BC encoder/decoder). | [ ] |
| `XNAPP-092` | `MipmapChain` (implicit conversion from `BitmapContent`), `MipmapChainCollection`, `TextureContent` (`Faces`, `ConvertBitmapType`, `GenerateMipmaps`, `Validate`), `Texture2DContent` (`Mipmaps`), `Texture3DContent`, `TextureCubeContent`, `TextureReferenceDictionary`. | [ ] |
| `XNAPP-093` | `FontDescription` (3 ctors, 7 properties incl. `Characters` collection), `FontDescriptionStyle` (3 values). | [ ] |
| `XNAPP-094` | `MaterialContent` (`Textures`, `GetTexture`/`SetTexture`, `GetReferenceTypeProperty`/`GetValueTypeProperty`/`SetProperty`) + the six stock material contents with their string constants and typed properties; `EffectContent`. | [ ] |
| `XNAPP-095` | `NodeContent` (Transform, AbsoluteTransform, Parent, Children, Animations), `NodeContentCollection`, `BoneContent`, `MeshContent` (Geometry, Positions), `GeometryContent` (Indices, Material, Vertices, Parent), `GeometryContentCollection`. | [ ] |
| `XNAPP-096` | `VertexContent` (Channels, PositionIndices, Positions, VertexCount, Add/Insert/Remove/ConvertToVertexBufferContent), `VertexChannel`/`<T>`, `VertexChannelCollection` (20), `VertexChannelNames` (13), `PositionCollection`, `IndirectPositionCollection`, `IndexCollection`, `BoneWeight`, `BoneWeightCollection` (`NormalizeWeights` ×2), `VectorConverter` (5). | [ ] |
| `XNAPP-097` | `AnimationContent`, `AnimationContentDictionary`, `AnimationChannel` (10), `AnimationChannelDictionary`, `AnimationKeyframe` (`IComparable`). | [ ] |
| `XNAPP-098` | Compile-parity and behaviour tests for every Phase 6 type. | [ ] |

### Phase 7 — processors namespace and property/default parity (28 types)

| ID | Task | State |
|---|---|---|
| `XNAPP-130` | `TextureProcessorOutputFormat`, `MaterialProcessorDefaultEffect`, `EffectProcessorDebugMode` enums (exact values). | [ ] |
| `XNAPP-131` | `TextureProcessor` (6 properties with XNA defaults), `SpriteTextureProcessor`, `ModelTextureProcessor` as public classes over the canonical texture processor; the XNA defaults (`ColorKeyEnabled=true`, magenta) apply on the XNA façade and through `.contentproj`, the `.cna-content.json` route keeps its documented defaults — recorded as the one deliberate divergence of that route. | [ ] |
| `XNAPP-132` | `FontDescriptionProcessor`, `FontTextureProcessor` (3 properties), `SpriteFontContent`. | [ ] |
| `XNAPP-133` | `MaterialProcessor` (7 properties, `BuildTexture`/`BuildEffect`/`Process` protected virtuals). | [ ] |
| `XNAPP-134` | `ModelProcessor` (14 properties; `ConvertMaterial`, `ProcessGeometryUsingMaterial`, `ProcessVertexChannel` protected virtuals; `Process`), `ModelContent` graph types (`ModelBoneContent(Collection)`, `ModelMeshContent(Collection)`, `ModelMeshPartContent(Collection)`, `VertexBufferContent`, `VertexDeclarationContent`) as the typed view of the canonical model IR. | [ ] |
| `XNAPP-135` | `EffectProcessor` (2 properties), `CompiledEffectContent` (`GetEffectCode`). | [ ] |
| `XNAPP-136` | `SoundEffectProcessor`/`SongProcessor` (`Quality`), `SoundEffectContent`, `SongContent`; `VideoProcessor` (`VideoSoundtrackType`). | [ ] |
| `XNAPP-137` | `PassThroughProcessor`. | [ ] |
| `XNAPP-138` | For every processor: tests with explicit values, omitted values (XNA defaults), and property interactions; defaults asserted against §10. | [ ] |

### Phase 8 — MeshBuilder / MeshHelper / model intermediate API

| ID | Task | State |
|---|---|---|
| `XNAPP-150` | `MeshBuilder` (13: `StartMesh`, `CreatePosition` ×2, `CreateVertexChannel<T>`, `SetVertexChannelData`, `SetMaterial`, `SetOpaqueData`, `AddTriangleVertex`, `FinishMesh`, `MergeDuplicatePositions`, `SwapWindingOrder`, `Name`). | [ ] |
| `XNAPP-151` | `MeshHelper` (10: `CalculateNormals` ×2, `CalculateTangentFrames` ×2, `FindSkeleton`, `FlattenSkeleton`, `MergeDuplicatePositions`, `MergeDuplicateVertices` ×2, `OptimizeForCache`, `SwapWindingOrder`, `TransformScene`). | [ ] |
| `XNAPP-152` | A custom processor builds a `NodeContent` graph programmatically, runs `ModelProcessor`, writes through the canonical XNB writer and loads in CNA. | [ ] |

### Phase 9 — audio / media intermediate API

| ID | Task | State |
|---|---|---|
| `XNAPP-160` | `AudioContent` (11: `FileName`, `FileType`, `Format`, `Data`, `Duration`, `LoopStart`, `LoopLength`, `ConvertFormat`, `Dispose`), `AudioFormat` (7), the three enums. | [ ] |
| `XNAPP-161` | `ConvertFormat(Pcm, quality, target)`: PCM re-encode at XNA's three qualities (measured by `BuildContent`), `Adpcm` (MS ADPCM encoder, in-house), `WindowsMedia`/`Xma` refused with `EXTERNAL_BLOCKED` reasons unless §26 finds a route. | [ ] |

### Phase 10 — texture input format parity (9 extensions)

| ID | Task | State |
|---|---|---|
| `XNAPP-165` | DDS reader: DX9 header + DX10 extension, uncompressed formats XNA accepts, DXT1/3/5, source mip chains, cube maps, volumes, pitch/linear-size validation; keeps compressed data compressed when `TextureFormat=NoChange`. | [ ] |
| `XNAPP-166` | PFM (portable float map) reader → `PixelBitmapContent<Vector4>`/`Single`; `.ppm` (P3/P6) and `.dib` routes; `.hdr`, `.tga`, `.bmp`, `.jpg`, `.png` behavioural audit against `BuildContent` output (channels, alpha, orientation). | [ ] |
| `XNAPP-167` | Per-extension fixtures, importer/processor/XNB/CNB/malformed/target tests; differential comparison per §21. | [ ] |

### Phase 11 — SpriteFont parity

| ID | Task | State |
|---|---|---|
| `XNAPP-180` | `.spritefont` schema audit vs `BuildContent`: every element, `CharacterRegions` multiplicity, `DefaultCharacter`, missing-glyph behaviour, `Style` values, `UseKerning`, `Spacing`, `Size` units. | [ ] |
| `XNAPP-181` | `FontDescriptionStyle` reduced to XNA's three values on the façade; CNA's `BoldItalic` kept as a CNAEXT extension on the native route, recorded. | [ ] |
| `XNAPP-182` | Atlas/glyph/kerning/line-spacing/baseline differential vs XNA for an OFL font registered in the Wine prefix. | [ ] |

### Phase 12 — EffectImporter / EffectProcessor parity

| ID | Task | State |
|---|---|---|
| `XNAPP-190` | `EffectImporter`/`EffectContent`/`EffectProcessor` façade over the existing external-compiler route; `Defines` and `DebugMode` semantics (`Auto` = optimise unless Debug configuration). | [ ] |
| `XNAPP-191` | Genuine-compiler verification: build `.fx` fixtures through `BuildContent` under Wine (which uses the prefix's `d3dx9`), compare the Effect container CNA's route produces with a real `fxc`-compatible backend where one exists; record exactly which backend produced what. Closes or re-scopes `XNAP-A4`. | [ ] |
| `XNAPP-192` | Reach vs HiDef, includes/include dependencies, macros, techniques/passes/state/parameter/sampler reflection compared. | [ ] |

### Phase 13 — audio source parity

| ID | Task | State |
|---|---|---|
| `XNAPP-200` | `WavImporter` façade; WAV variants XNA accepts (measured by `BuildContent`: PCM 8/16/24/32, float, ADPCM, WAVE_FORMAT_EXTENSIBLE, loop metadata). | [ ] |
| `XNAPP-201` | `Mp3Importer`: MP3 decoding (dependency per §26) → `AudioContent`; what `SongProcessor` emits for it under XNA, measured, reproduced. | [ ] |
| `XNAPP-202` | `WmaImporter`: measure XNA's output first; implement or record `EXTERNAL_BLOCKED` with the exact codec reason. | [ ] |

### Phase 14 — video source parity

| ID | Task | State |
|---|---|---|
| `XNAPP-210` | `WmvImporter`/`VideoProcessor`: measure what XNA emits (the `.xnb` is a header plus an external media file); implement the container semantics; codec availability per §26. | [ ] |

### Phase 15 — FBX importer

| ID | Task | State |
|---|---|---|
| `XNAPP-215` | Dependency audit and choice for a native FBX parser (§26). | [ ] |
| `XNAPP-216` | `FbxImporter`: hierarchy, transforms, meshes, channels, normals/tangents/UVs/colours, skin weights, animations, materials/textures, coordinate-system and unit conversion as `BuildContent` observably does them. | [ ] |
| `XNAPP-217` | FBX black-box corpus (CNA-authored/generated, plus permissively licensed) compared per §24. | [ ] |

### Phase 16 — DirectX `.x` importer

| ID | Task | State |
|---|---|---|
| `XNAPP-220` | `.x` text and binary (incl. compressed) parser: templates, `Frame`, `FrameTransformMatrix`, `Mesh`, `MeshNormals`, `MeshTextureCoords`, `MeshVertexColors`, `MeshMaterialList`, `Material`, `TextureFilename`, `SkinWeights`, `XSkinMeshHeader`, `AnimationSet`/`Animation`/`AnimationKey`, `VertexDuplicationIndices`, `DeclData`. | [ ] |
| `XNAPP-221` | `XImporter` façade (4 members); black-box corpus and comparison. | [ ] |

### Phase 17 — XML importer / intermediate integration

| ID | Task | State |
|---|---|---|
| `XNAPP-230` | `XmlImporter` (no default processor) over Phase 5; `.xml` → `Object` → `PassThroughProcessor` → automatic writer → XNB; XNA's own XML fixtures (the samples' `.xml` content) as the black-box corpus. | [ ] |

### Phase 18 — `.contentproj` compatibility

| ID | Task | State |
|---|---|---|
| `XNAPP-240` | `.contentproj` parser → canonical build graph (§18 schema); `cna-content build Foo.contentproj`; unknown metadata refused, not guessed. | [ ] |
| `XNAPP-241` | The four `Tasks` types as `HOST_SUBSTITUTION` rows with the exact mapping of each input/output property. | [ ] |
| `XNAPP-242` | Build the public XNA samples' `.contentproj` files (108 available locally) through the parser and record which items route, which need a custom processor, and which fail and why. | [ ] |

### Phase 19 — target platform and profile parity

| ID | Task | State |
|---|---|---|
| `XNAPP-250` | Windows: differential tests for every route (§23). | [ ] |
| `XNAPP-251` | Windows Phone: `BuildContent` fixtures for `WindowsPhone`/`Reach`; texture format, profile, media, effect and model differences measured and implemented. | [ ] |
| `XNAPP-252` | Xbox 360: `BuildContent` fixtures for `Xbox360`; big-endian payload rules per type, texture tiling/swizzle, `SoundEffect` format, effect bytecode, compression; `RequireVerifiedPlatformPayload()` lifted per type only as each is proven against those fixtures. Device verification stays `EXTERNAL_BLOCKED`. | [ ] |
| `XNAPP-253` | Reach/HiDef enforcement measured and implemented per §20. | [ ] |

### Phase 20 — custom importer/processor/writer external-style sample

| ID | Task | State |
|---|---|---|
| `XNAPP-260` | External CMake project with a custom intermediate type, importer for a custom extension, processor, `ContentTypeWriter<T>` with a custom runtime reader name, dependencies, nested `BuildAsset`, shared resources, fingerprints, diagnostics; run as a ctest. | [ ] |

### Phase 21 — black-box XNA differential harness

| ID | Task | State |
|---|---|---|
| `XNAPP-265` | `tools/xna-pipeline-oracle/differential/`: C# `BuildContent` driver (mcs + Wine), corpus manifest, per-case result JSON. | [ ] |
| `XNAPP-266` | Extend `tools/xnb/xnb_conformance.py` to normalize every root type this plan emits; `compare.py` with the §24 semantics; decision file for accepted differences. | [ ] |
| `XNAPP-267` | Error-parity corpus: malformed/unsupported/impossible-parameter/missing-dependency/cyclic/unsupported-target/bad-declaration/missing-glyph/shader-failure cases; XNA and CNA failure classes compared. | [ ] |

### Phase 22 — genuine XNA runtime verification

| ID | Task | State |
|---|---|---|
| `XNAPP-280` | Compile and run `tests/interop/xna40` under Wine (`csc.exe`, DXVK, `Xvfb :131`) against the committed corpus; record the result here and in `plan_xnapipeline.md` `XNAP-34`. | [ ] |
| `XNAPP-281` | Extend the harness and corpus to every output family (§25) with value assertions; every family marked `xna40` only after execution. | [ ] |

### Phase 23 — fuzzing / hardening

| ID | Task | State |
|---|---|---|
| `XNAPP-290` | Fuzz targets: DDS, PFM/PPM, X (text+binary), FBX, intermediate XML, MP3 framing, `.contentproj`; sanitizer runs; explicit ceilings documented. | [ ] |

### Phase 24 — performance / determinism

| ID | Task | State |
|---|---|---|
| `XNAPP-300` | Determinism: multi-worker, multi-process builds of the new routes byte-identical; tool identities in fingerprints. | [ ] |
| `XNAPP-301` | Representative large inputs per route with recorded measurements; no accidental quadratic behaviour. | [ ] |

### Phase 25 — gates, documentation, final qualification

| ID | Task | State |
|---|---|---|
| `XNAPP-310` | All §29 gates wired as ctests. | [ ] |
| `XNAPP-315` | Provenance gate (`XNAPP-008`) plus a MonoGame/FNA build-source grep. | [ ] |
| `XNAPP-320` | Documentation: compatibility API, C#→C++ mapping, every importer/extension/processor/property, IntermediateSerializer, custom extensions, `.contentproj`, targets, profiles, dependencies, CLI, CMake, XNB vs CNB, runtime verification; migration guide with before/after examples. | [ ] |
| `XNAPP-330` | Final audit checklist (mission §"Final audit", 26 items) executed and recorded; final report §33. | [ ] |

---

## 33. Baselines, measurements and handoff

### 33.1 Test baseline (this environment)

*To be recorded by `XNAPP-006` once `cmake-build-debug` finishes building.*

### 33.2 Handoff

Session 1 (2026-09-05): Phases 0–1 done except `XNAPP-006`/`008`/`014`–`016`. Next: `XNAPP-006`
(baseline), `XNAPP-014` (parity map + report generator — the coverage numbers depend on it), then
Phase 3 in ID order. The build directory is `cmake-build-debug/` (see `XNAPP-005` for the exact
configuration); the oracle regenerates with `tools/xna-pipeline-oracle/run-oracle.sh`.
