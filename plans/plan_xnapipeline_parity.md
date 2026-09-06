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
> **Status (2026-09-06, session 2).** The API denominator is closed: **128/128 types, 705/705
> members, 27/27 enum values, 10/10 importers, 12/12 processors, 47/47 processor properties, and
> all 18 declared source extensions routed** — `MISSING` 0 and `EXTERNAL_BLOCKED` 0, with
> `parity_report.py --gate` exiting zero. Every percentage in this file is read off
> `tools/xna-pipeline-oracle/parity_report.py` and never hand-counted, and two ctests now make
> that mechanical: the report must be byte for byte a regeneration, and the eighteen-extension
> matrix must still name every extension the genuine importer attributes declare. What remains is
> not API but behaviour: §31 says which legs of the input matrix are still unnamed, and §19's
> targets stand at 1 of 3.

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
`ContentTypeSerializerAttribute` (1), `ChildCallback`. **Was the largest gap; closed by Phase 5
(`XNAPP-070`–`073`) against the measured corpus.** Phase 5 built a C++ type-description system (descriptors, not reflection) that yields
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

The intermediate XML format has its own oracle, already run (`XNAPP-074`, 2026-09-05):
`tools/xna-pipeline-oracle/intermediate/IntermediateOracle.cs` drives the genuine
`IntermediateSerializer` over CNA-authored graphs and hand-written XML variants; the corpus is
`tests/reference/xna40/intermediate/` (254 cases: 71 graphs written, 86 variants accepted, 93
refused with their messages, 3 graphs the serializer itself refuses) and the measured
specification is `docs/xna-intermediate-xml-format.md`. CNA's serializer tests read the corpus
files directly, so a divergence from the genuine format is a failing test, not an opinion.

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
each phase close. The member denominator the report counts is 705: the inventory's 708 minus the
3 delegate-plumbing members the report lists separately (§5).

**Current (2026-09-06, session 2): types 128/128, members 705/705, enum values 27/27,
importers 10/10, processors 12/12, processor properties 47/47, source extensions 18/18 present
and routed, `MISSING` 0 and `EXTERNAL_BLOCKED` 0.** `parity_report.py --gate`, which fails on any
`MISSING`, on an unknown map entry and on a status without its required annotation, exits zero.

What that number does and does not say. It says every public type and member of the seven
assemblies has a named, tested C++ counterpart, that every extension the genuine importer
attributes declare is read by an importer of CNA's own, and that no row is hiding behind
`EXTERNAL_BLOCKED`. It does **not** say the input matrix is finished: the eighteen extensions
stand at `IMPLEMENTED`, not `IMPLEMENTED+TESTED`, because that bar additionally requires a
source-to-XNB and a source-to-CNB test *for that extension*, and those legs are named in the
matrix as the work they are. Targets verified stay at 1 of 3 (Windows), and the black-box-verified
families are now seven: the intermediate XML byte for byte including the genuine `XmlImporter`;
the graphics content object model against 553 measurements; the framework's float packing against
68; the audio content model against 35; the texture importer, its three processors, every
`TextureProcessor` property and every target/profile leg over a committed per-extension corpus;
the MP3 route's format and duration over twelve files; and both modelling importers, graph for
graph, over twenty documents.

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
| `XNAPP-006` | Record the test baseline of `CnaContentTests` and `CnaContentPipelineTests` in this environment before any change (§33). | [x] Recorded in §33.1; needed a one-line fix to a test that did not compile at HEAD (`CnbDocument::Parse` arity). |
| `XNAPP-007` | Create this plan; index it in `plans/README.md`. | [x] |
| `XNAPP-008` | Provenance gate: a test that fails if any Microsoft binary (`Microsoft.Xna.*.dll`, `XnaNative.dll`, `*.exe` from the SDK) or a Microsoft font appears under `tests/`, `tools/`, `modules/`, `docs/`. | [ ] |

### Phase 1 — authoritative XNA public API inventory

| ID | Task | State |
|---|---|---|
| `XNAPP-010` | `PipelineApiOracle.cs`: reflection-only, metadata + black-box processor defaults, no XNA compile-time reference, deterministic JSON. | [x] 128 types, 708 members. Attribute detection walks the base chain because the built-ins carry internal `Localized*Attribute` subclasses; `IsVisibleType` requires the whole declaring chain to be reachable, which removed 39 types nobody can name (listed in the JSON with the rule). |
| `XNAPP-011` | `run-oracle.sh`: mcs + Wine .NET 4.0; Microsoft binaries only under ignored `build/`. | [x] |
| `XNAPP-012` | `merge_xml_docs.py`: join with the official XML docs; report both directions of disagreement. | [x] 120/128 types documented; the 8 undocumented are the importer classes; 665/708 members documented. |
| `XNAPP-013` | Determinism: two oracle runs produce identical bytes. | [x] `cmp` identical. |
| `XNAPP-014` | `parity_report.py` + `content-pipeline-parity-map.json`: per-type and per-member statuses joined to the inventory, coverage computed mechanically, `--gate` mode, generated `docs/xna-content-pipeline-parity-report.md`. Every inventory type and member must appear in the map; the map may not name anything the inventory lacks. | [x] `tools/xna-pipeline-oracle/parity_report.py` joins the two and writes `docs/xna-content-pipeline-parity-report.md`; `--check` proves the committed report is exactly what a regeneration writes, `--gate` is the completion check that also fails on `MISSING`, and `--init` fills the map from the inventory. Section 6 of the report is generated from the input matrix (`XNAPP-021`). |
| `XNAPP-015` | ctest `CnaXnaPipelineParityInventoryConsistency`: the committed inventory's assembly SHA-256 set is the one this plan quotes; the map covers the inventory; this file's §31 numbers equal the report's. | [x] Two ctests rather than one, because they fail for different reasons and a reader should be told which: `XnaPipelineParityReportIsCurrent` (the report is byte for byte a regeneration, and no status lacks its required note or names a member the inventory does not have) and `XnaPipelineInputParityMatrixIsCurrent` (`XNAPP-021`). Wired in `cmake/XnaPipelineParityGates.cmake`; both skip cleanly without a Python 3 interpreter. Verified to fail: removing one extension from the matrix reports `.hdr is in the inventory and not in the matrix`. |
| `XNAPP-016` | Inventory freeze: after `XNAPP-014`/`015`, declare the denominator frozen at the SHA-256 set in the JSON. Later regeneration is a recorded event, not a silent change. | [ ] |

### Phase 2 — importer / extension / processor inventory

| ID | Task | State |
|---|---|---|
| `XNAPP-020` | Generate §4–§10 tables from the inventory into the report; this plan quotes them. | [ ] |
| `XNAPP-021` | `tests/reference/xna40/content-pipeline-inputs.json`: the 18-extension matrix with, per extension, importer, default processor, fixture path, six test names, per-target status, and a status word; a checker that verifies every named test exists in the tree and every extension is present. | [x] `tools/xna-pipeline-oracle/inputs_matrix.py` with `sync` and `check`. Each entry has two halves: an `xna` half regenerated from the inventory's `extensionIndex` on every `sync` and never edited, and a `cna` half that is CNA's answer. `check` fails if an extension is absent, if the matrix names one the assemblies do not, if a status is outside the vocabulary, if a status that needs a note has none, or if a test name or a fixture path it claims does not exist in the tree -- test existence is decided by finding `TEST(Suite, Name)` in the sources, so "the test exists" is mechanical. Writing it immediately found five extensions with no committed source at all and one (`.hdr`) with no measurement at all, both now closed by `XNAPP-167`. The report's own `source extensions IMPLEMENTED+TESTED` row read the status off the wrong half of each entry and could therefore only ever print `0/18`; fixed, and it now reads 9/18. |
| `XNAPP-022` | Black-box corpus plan: the synthetic fixture set per extension (§23), authored or generated by CNA, with provenance file; nothing third-party without a licence row. | [ ] |
| `XNAPP-023` | Audit CNA's current routes against §7/§9/§10 and record every default that differs (`ColorKeyEnabled`, `SpriteTextureProcessor` defaulting, `FontDescriptionStyle.BoldItalic`, `.ppm`/`.dib` registration, …) as decisions to take in their phases. | [ ] |

### Phase 3 — C++ public compatibility façade: pipeline core

Location: `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/` (build-time
module) with `src/Xna/`; `ContentSerializer*` descriptors in `modules/content/include/Microsoft/Xna/Framework/Content/`
(runtime module) because game types carry them. Namespace `Microsoft::Xna::Framework::Content::Pipeline`.

| ID | Task | State |
|---|---|---|
| `XNAPP-030` | Façade design note in `docs/xna-content-pipeline-compat-api.md`: property convention, attribute→descriptor, generics, `IEnumerable<T>`→ranges, delegates→callables, nullable→`std::optional`, how an XNA-style importer/processor is adapted into the canonical registry, and what a C++ user must write. | [x] Written; §7 says what counts as `EXACT_EQUIVALENT` (the carrier and property conventions are the baseline C++ spelling, not substitutions). |
| `XNAPP-031` | `ContentIdentity` (4 ctors, 3 properties), `ContentItem` (Identity, Name, OpaqueData). | [x] `ContentIdentity` is a value class whose all-empty state stands for null; `ContentItem` derives `System::Object` so it travels as a shared, mutable reference. |
| `XNAPP-032` | `OpaqueDataDictionary` (+ `NamedValueDictionary<T>` generic base, 16 members) and `GetValue<T>(key, default)`. | [x] `GetContentAsXml()` returns the measured document (`opaque_data_dictionary.getcontentasxml.txt`: compact, `utf-16` declaration, `<Data Key>` entries typed only when not strings); the default serializer type is `string` as measured. Insertion order is kept, because that is the order XNA's XML lists entries in; the C# indexer setter is `Set()` since `operator[]` cannot add a key without a value. |
| `XNAPP-033` | `ChildCollection<TParent,TChild>` (parent back-pointer semantics, `GetParent`/`SetParent` protected). | [x] Over sharp-runtime's `Collection<std::shared_ptr<TChild>>`; a child already owned is refused, `setItem` detaches the old child and attaches the new. **The `collection[i] = child` spelling bypasses the virtual hook in sharp-runtime's `Collection<T>`** (its `operator[]` returns an element reference); the .NET indexer setter is `setItem(i, child)`, and the design note says so. |
| `XNAPP-034` | `InvalidContentException` (6 ctors, `ContentIdentity`, `GetObjectData` as `HOST_SUBSTITUTION`), `PipelineException` (5 ctors incl. format overload). | [x] Both derive `System::Exception`; inner exceptions are `std::exception_ptr` as sharp-runtime spells them; the `params object[]` constructor is a variadic `std::format` template accepting both `{0}` and `{}`. |
| `XNAPP-035` | `ContentBuildLogger` (LoggerRootDirectory, LogMessage/LogImportantMessage/LogWarning, PushFile/PopFile, GetCurrentFilename) bridged to `CNA::Content::Pipeline::ContentBuildLogger`. | [x] Variadic `std::format` overloads forward to non-template virtuals; `XnaBridgeLogger` reports through the canonical context so XNA-shaped components land in the same build log. A subclass overriding the virtuals must `using` the base overloads to keep them visible on its own type -- documented. |
| `XNAPP-036` | `TargetPlatform` enum (3 values, exact numbering) mapped to `XnbTargetPlatform`. | [x] Mapped to the new format-neutral `CNA::Content::Pipeline::ContentTargetPlatform` (`XNAPP-040`); the XNB writer's own platform byte mapping is `XNAPP-062`'s. `TryParseTargetPlatform` accepts the MSBuild spellings `Xbox 360` / `Windows Phone`. |
| `XNAPP-037` | `ContentImporterAttribute`, `ContentProcessorAttribute` descriptors; `IContentImporter`, `IContentProcessor`. | [x] Descriptors derive `System::Attribute` and are passed at registration. The explicit interface implementations are non-virtual `Import`/`Process` returning `ContentObject`, reachable only through the interface -- the C++ shape of what C# does. |
| `XNAPP-038` | `ContentImporter<T>` and `ContentProcessor<TInput,TOutput>` compatibility bases with the exact `Import(filename, context)` / `Process(input, context)` shape, adapted into the canonical `ContentImporter`/`ContentProcessor` by a generic bridge (stable type identity derived from a descriptor, not RTTI). | [x] `CNA/Content/Pipeline/XnaPipelineBridge.hpp`: `XnaImporterComponent<T>`/`XnaProcessorComponent<T>` construct a fresh component per asset (as `BuildContent` does), take the identity from the XNA class name + version, the extensions from the descriptor and the stable types from `ContentTypeName<T>`. The canonical `ContentImporter` gained `DefaultProcessor()` and `Build()` honours it when the request names none -- XNA's `DefaultProcessor` semantics, unchanged for every built-in CNA importer (they return empty). |
| `XNAPP-039` | `ContentImporterContext` (IntermediateDirectory, Logger, OutputDirectory, AddDependency). | [x] Abstract, as in XNA; `XnaBridgeImporterContext` implements it over the canonical context (`AddDependency` records through `ResolveSourceDependency`, so containment still applies). |
| `XNAPP-040` | `ContentProcessorContext` (17 members): `AddDependency`, `AddOutputFile`, `BuildAsset`/`BuildAndLoadAsset`/`Convert` with nested-build semantics on the canonical build graph, `Parameters` (`OpaqueDataDictionary`), platform/profile/configuration/directories/filename. | [x] All 17. The canonical engine gained, additively, `ContentBuildEnvironment` (target platform, graphics profile, build configuration, output and intermediate directories) on `ContentBuildRequest` and both contexts, plus `SourceRoot()/SourcePath()/ExternalSourceRoots()/Dependencies()/Logger()/Pipeline()` on the processor context and `ContentPipeline::Registry()`. `Convert` runs a registered processor in-process through the pipeline pointer; `AddOutputFile` is a canonical deployment file; the three generic methods are templates over `*Core` virtuals (`SEMANTIC_EQUIVALENT`). Nested builds are `XNAPP-044`. |
| `XNAPP-041` | `ExternalReference<T>` (3 ctors, Filename; relative-path resolution against the referencing identity). | [x] A relative name resolves against the referencing identity's source directory; an empty name, or a relative one with no source to resolve against, is refused. |
| `XNAPP-042` | `ProcessorParameter` (7), `ProcessorParameterCollection`; `PipelineComponentScanner` (12) as the explicit registry's enumeration view. | [x] Processors declare parameters through `ProcessorParameterBindings<T>` (`DescribeParameters`) with typed text/object conversion for bool, integers, float/double, string, char, `Color`, `Vector2/3/4` and enums by spelling; the bridge validates and assigns them per asset. The scanner enumerates a registry by catalog name (`HOST_SUBSTITUTION` for assembly scanning) and reports unknown catalogs in `Errors`. |
| `XNAPP-043` | `VideoContent` (7 properties, `Dispose`). | [ ] Needs a build-time video probe (duration, bit rate, frame rate, size); done with Phase 14 (`XNAPP-210`), where the probe is decided. |
| `XNAPP-044` | Nested build semantics: circular dependency detection, nested output naming (`OutputFilename`), caching within one build, `ContentIdentity` propagation into diagnostics. | [x] Two additive engine changes: `ContentPipeline::ImportAndProcess()` (the in-process half of a build, recording into a caller-supplied collector so the outer node depends on the nested source **as a source file, never as a second primary**) and `ContentProcessorContext::AddNestedOutput()`, whose outputs `Build()` appends after the writer's own once names are distinct -- so a `BuildAsset` result is owned, fingerprinted, published and cleaned like every other artifact. The bridge derives the asset name from the source path relative to the content root without its extension, as XNA does, returns the output path below `OutputDirectory`, merges the nested node's dependencies, runtime references and deployment files into the outer node, and refuses one name built from two different sources or processings while returning the same reference for a repeat. A nested failure is rethrown as `InvalidContentException` carrying the referencing identity. Cycles cannot form: a nested build is a fresh `Build()` of another source, and a source that nests itself recurses into the canonical containment and depth ceilings rather than into a graph. Tested end to end through the real coordinator with a canonical writer. |
| `XNAPP-045` | Compile-parity tests for every Phase 3 type (construct/derive/static_assert), plus behaviour tests. | [~] `XnaPipelineCoreTests.cpp`: 19 tests over every type landed so far, including an XNA-shaped importer + processor registered into a canonical registry and driven through canonical contexts, `DefaultProcessor` through `Build()`, `Convert`, the scanner, and parameter conversion edges. `VideoContent` and the nested-build members follow their tasks. |

### Phase 4 — Serialization.Compiler parity

| ID | Task | State |
|---|---|---|
| `XNAPP-060` | `ContentWriter` façade (15 members: `Write` overloads for `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `WriteObject<T>` ×2, `WriteRawObject<T>` ×2, `WriteSharedResource<T>`, `WriteExternalReference<T>`, `TargetPlatform`, `TargetProfile`, `Dispose(bool)`) over `XnbWriter`. | [x] Derives sharp-runtime's `BinaryWriter` as XNA's derives .NET's, with every primitive overload overridden to write through the one canonical `XnbWriter` (the placeholder base stream stays empty). `WriteObject` dispatches a reference on its **dynamic** type, as .NET does; `WriteSharedResource` writes one resource however many references name it; `WriteExternalReference` spells the path relative to the asset's output directory without the extension. Two additive canonical hooks: `XnbWriter::WriteObject/WriteRawObject/AddSharedResource` taking an explicit type writer (XNA's writer-worker overloads), and **a fix to `XnbWriter::WriteExternalReference`**, whose containment check started counting at the content root while `ContentReader::ReadExternalReference` resolves relative to the asset's own directory -- so an asset in a subdirectory could never reference a sibling directory. It now starts at the asset's directory depth. |
| `XNAPP-061` | `ContentTypeWriter` (9) and `ContentTypeWriter<T>` (3) compatibility bases: `GetRuntimeReader`, `GetRuntimeType`, `ShouldCompressContent`, `TypeVersion`, `CanDeserializeIntoExistingObject`, `Initialize`, `Write`; adapter to `XnbTypeWriter`. | [x] The non-generic base is `ContentTypeWriterBase` (a class and a class template cannot share a name; CNA's runtime spells `ContentTypeReaderBase` the same way -- `SEMANTIC_EQUIVALENT`, recorded). `GetRuntimeReader` is written verbatim into the type-reader table, `TypeVersion` beside it; the compiler's adapter turns a user writer into a canonical `XnbTypeWriterBase` per target platform, so `GetRuntimeReader(platform)` can differ per platform as XNA allows. |
| `XNAPP-062` | `ContentTypeWriterAttribute` descriptor + `ContentCompiler::GetTypeWriter(Type)` over the registry. | [x] `ContentCompiler` is constructible here (XNA's is host-created), owns one frozen canonical `XnbTypeWriterRegistry` per platform -- the built-in writers plus every `AddTypeWriter<TWriter>()` -- and answers `GetTypeWriter` for built-ins (37 types, including the registered `List<>`/`Dictionary<,>` instantiations) and user writers alike. `Compile<T>()`/`CompileObject()` are the C++ form of XNA's internal `Compile`; a root writer's `ShouldCompressContent` veto is honoured. `ContentValue` gained `CppType()`/`RawData()` so the compiler can dispatch on a box without knowing `T`. |
| `XNAPP-063` | A user-defined `ContentTypeWriter<T>` round-trips through the real `.xnb` route and loads in CNA's reader; compile-parity tests. | [x] `XnaSerializationCompilerTests.cpp`: three user writers (one for a derived type, dispatched on the dynamic type of a base-typed reference), shared resources referenced twice, an external reference, raw and dispatched objects, the 7-bit int, a platform-dependent reader name and the HiDef bit -- all read back by CNA's reader with matching custom `ContentTypeReader`s, `TypeVersion` enforced. `RegisterXnaXnbOutput()` registers a canonical `.xnb` writer per type the compiler knows (`XnbOutputAssetId::XnaObject`, told apart by type name), and a canonical `ContentTypeWriter::Write(input, name, environment)` overload -- defaulting to the old one -- carries the output directory to it, so an XNA-shaped route builds to `.xnb` through the real coordinator. A reference whose dynamic type has no writer is refused rather than written with a base writer: XNA gives such a type the reflective writer, which is `XNAPP-070`. |

### Phase 5 — Serialization.Intermediate parity

| ID | Task | State |
|---|---|---|
| `XNAPP-070` | C++ type-description system: per-type descriptors (element name, runtime type, type version, collection item name, serializer), per-member descriptors (name, element name, optional, allow-null, shared-resource, flatten, ignore), reusable by custom content. | [x] `ContentTypeDescription.hpp`: `ContentTypeDescriptor<T>` (`Field`/`Property`/`ReadOnlyProperty`/`BaseType`, `RuntimeType`/`TypeVersion`/`CollectionItemName`), fluent `ContentMemberDescriptor` for the `[ContentSerializer]` settings and `Ignore()`, `DescribedTypeSerializer<T>` as the reflective serializer's stand-in, `CNA_XNA_CONTENT_ENUM` for enum names; the five `ContentSerializer*Attribute` classes in `modules/content`. docs/xna-content-pipeline-compat-api.md §8. |
| `XNAPP-071` | `IntermediateSerializer::Serialize/Deserialize/GetTypeSerializer`; `IntermediateWriter` (9); `IntermediateReader` (14); `ContentTypeSerializer`/`<T>` bases; `ContentTypeSerializerAttribute`. | [x] All 47 inventory members, over sharp-runtime's `System::Xml` (component `Xml` now in CNA's closure); the base is `ContentTypeSerializerBase` (class/template name clash, as `ContentTypeWriterBase`); `object` is `ContentObject`, delegates are `std::function`. sharp-runtime gained a .NET-faithful `XmlWriter` text form, `XmlReader` navigation/namespace/line-info members and whitespace-only content (commits c8fadba4, 2499b756 on `next`), because the corpus is compared byte for byte. |
| `XNAPP-072` | Primitives, strings, enums, arrays, lists, dictionaries, nullable, nested types, inheritance/polymorphism (`Type=`), shared resources (`<Resources>`), external references, optional/ignored/flattened/allow-null members, collection item names, `XnaContent`/`Asset` envelope, namespace aliases (`xmlns`), error location/identity. | [x] Every feature the corpus exercises (docs/xna-intermediate-xml-format.md §1–§10), including the measured rules that were not in the plan's list: properties before fields, packed versus element-per-item collections, the mixed-content indentation rule, mandatory `Resources`/`ExternalReferences` once a reference is pending, the root written twice in a cycle, `Type` only where the dynamic type differs. Errors reproduce XNA's message texts with `Line L, position 0.` (tinyxml2 has no column). Divergences: §12 of the format document. `OpaqueDataDictionary::GetContentAsXml()` now exists (`XNAPP-032` closes). |
| `XNAPP-073` | Built-in serializers for every math/framework type XNA serializes inline (`Vector*`, `Matrix`, `Quaternion`, `Color`, `Rectangle`, `Point`, `BoundingBox`, `Curve`, `TimeSpan`, …). | [x] bool, the 8 integer types, float/double with .NET Framework 4.0's `R` format, char, string, decimal, TimeSpan (ISO 8601 duration), DateTime, Vector2/3/4, Quaternion, Matrix, Plane, Rectangle, Point, Color (`AARRGGBB`), BoundingBox, BoundingSphere, Ray, Curve (with `CurveLoopType`/`CurveContinuity` names), plus List/Dictionary/Nullable/reference-wrapper/ExternalReference/enum/object and `NamedValueDictionary` serializers. |
| `XNAPP-074` | Verify against XML the genuine `IntermediateSerializer` writes for the same graphs (§23), and XML the genuine `XmlImporter` accepts; record every difference. | [x] Oracle and corpus done first, so the implementation follows a measured specification: `tools/xna-pipeline-oracle/intermediate/` (driver + `run-intermediate-oracle.sh`), `tests/reference/xna40/intermediate/` (254 cases, `manifest.json`), `docs/xna-intermediate-xml-format.md`. Measured facts that shape `XNAPP-070`–`073`: properties serialize before fields; reading is positional and strict; packed collections are exactly the single-token types (`bool`, integers, `float`, `double`, `Vector*`, `Quaternion`, `Matrix`, `Rectangle`, `Point`, `Plane`, `Color`) while `char`, `decimal`, `TimeSpan`, `DateTime`, enums and nullables are element-per-item; `Color` is one `AARRGGBB` hex token; `TimeSpan` is an ISO 8601 duration; a null shared reference leaves the member unassigned; the root is written twice when a shared-resource cycle reaches it; self-closing `<Resources />` is refused while `<Resources></Resources>` is accepted; three XNA crashes (`NullReferenceException` on `Null="true"` for a value type, `RankException`, U+0000) become `InvalidContentException` in CNA (recorded divergence). The CNA-vs-corpus run is `XnaIntermediateSerializerTests.cpp`: 72 graphs serialize byte for byte and round-trip unchanged, 86 accepted variants normalize to XNA's text, 93 refused variants fail with XNA's message (7 recorded divergences, format document §12), `GetContentAsXml()` matches. The `XmlImporter` acceptance leg closed with `XNAPP-230`, over nine `importer_*` corpus cases driven through the genuine importer. |
| `XNAPP-075` | Hardening: no DOCTYPE/external entities, depth/size ceilings, cyclic shared resources, malformed numbers; fuzz target. | [x] DOCTYPE, entity references and malformed numbers are corpus-tested refusals; tinyxml2 stops element nesting at 500 levels and the reader/writer keep a 1024-level guard (a cycle through an unshared member is refused on writing; shared cycles are corpus-tested). `tools/content/xna_intermediate_fuzzer.cpp` (`cna_xna_intermediate_fuzzer`, replay/mutate, libFuzzer entry with `-DCNA_XNA_INTERMEDIATE_FUZZER_ENTRY_POINT=ON`) deserializes as five root shapes and re-serializes what it accepts; 343 corpus inputs replayed and 3000 seeded mutations ran clean; `XnaIntermediateSerializerHardeningTests.cpp` runs a 400-mutation seeded pass on every build. No size ceiling: tinyxml2 parses the whole document into memory, as .NET's XmlReader over a string does. |

### Phase 6 — graphics intermediate content API (47 types)

| ID | Task | State |
|---|---|---|
| `XNAPP-090` | `BitmapContent` (13), `PixelBitmapContent<T>` (12, all XNA-permitted `T`: `Color`, `Vector4`, `Single`, `Byte`, `Rgba64`, `Bgr565`, …), `Copy`/`TryCopyFrom`/`TryCopyTo`/`GetPixelData`/`SetPixelData`, `ReplaceColor`. | [x] `BitmapContent.hpp`/`.cpp` and `PixelBitmapContent.hpp`, with `detail/PixelTraits.hpp` giving each of the 22 permitted pixel types its size, name, surface/vertex format and `Vector4` conversion (a concept refuses the rest at compile time, where XNA asks `VectorConverter` at run time). `Copy` runs XNA's measured protocol in order: null checks, `ValidateCopyArguments` (its two `ArgumentOutOfRangeException` parameter names included), the zero-size no-op, the same-instance snapshot, the destination's `TryCopyFrom`, the source's `TryCopyTo`, then the `Vector4` intermediate; resizing enlarges bilinearly and reduces by box filter, within 8 channel units of D3DX on the corpus. `GetRow` answers a `std::span` aliasing the bitmap, because XNA's `T[]` is the bitmap's own row (measured, `color/get_row_is_live`), while `GetPixelData` is a snapshot. A CNAEXT bitmap-type registry stands in for XNA's reflection over the assembly. `PixelBitmapContent<T>`'s parameterless constructor is the one member not implemented (map note). |
| `XNAPP-091` | `DxtBitmapContent` + `Dxt1/3/5` (using the existing BC encoder/decoder). | [x] `DxtBitmapContent.hpp`/`.cpp`: block storage sized `ceil(w/4) * ceil(h/4) * blockSize` as measured, `SetPixelData` accepting any length as XNA does, and `Decode`/`Encode` over the canonical `DxtUtil` decoder and `EncodeBlockCompressedImage` encoder -- no second codec. XNA's blocks come out of D3DX, so the corpus is compared by decoding XNA's blocks with CNA's decoder (within 3 channel units), not byte for byte. |
| `XNAPP-092` | `MipmapChain` (implicit conversion from `BitmapContent`), `MipmapChainCollection`, `TextureContent` (`Faces`, `ConvertBitmapType`, `GenerateMipmaps`, `Validate`), `Texture2DContent` (`Mipmaps`), `Texture3DContent`, `TextureCubeContent`, `TextureReferenceDictionary`. | [x] `MipmapChain`/`MipmapChainCollection`/`TextureContent`/`TextureReferenceDictionary` (+ `VectorConverter`, `XNAPP-096`'s row keeps the rest of that task). The face collection carries XNA's fixed-size flag -- 1 face for `Texture2DContent`, 6 for `TextureCubeContent`, resizable only for `Texture3DContent` -- and refuses a resize with XNA's own `NotSupportedException` text. `GenerateMipmaps` halves with a floor of 1 and leaves an existing chain alone unless told to overwrite; the 3D form halves the depth too. Every `Validate` refusal text is the measured one, including the Reach size and format limits and the cubemap squareness check. |
| `XNAPP-093` | `FontDescription` (3 ctors, 7 properties incl. `Characters` collection), `FontDescriptionStyle` (3 values). | [x] `FontDescription.hpp`/`.cpp`, measured first (corpus cases `font/*`, 34 of them). The measurements settle what the API alone does not: all three constructors leave `UseKerning` **false**, they assign through the property setters (so their refusals name the parameter `value`), an empty name and a size that is not greater than zero are refused with their exact texts while a NaN size and an undefined style are accepted, and `Characters` behaves as a sorted set. The `.spritefont` document is this type in intermediate XML, so `DescribeContent` writes the measured order -- `FontName`, `Size`, `Spacing`, `UseKerning`, `Style`, `DefaultCharacter`, `CharacterRegions` -- with only the middle two and `DefaultCharacter` optional; `CharacterRegions` merges the character set into inclusive ranges on writing and expands them on reading, refusing a reversed region with XNA's message. One CNAEXT addition: a public parameterless constructor, because XNA's serializer reaches its private one by reflection and C++ has none. |
| `XNAPP-094` | `MaterialContent` (`Textures`, `GetTexture`/`SetTexture`, `GetReferenceTypeProperty`/`GetValueTypeProperty`/`SetProperty`) + the six stock material contents with their string constants and typed properties; `EffectContent`. | [x] `MaterialContent.hpp`/`.cpp`, `StockMaterials.hpp`/`.cpp`, `EffectContent.hpp`/`.cpp`, measured first (corpus cases `material/*`, `effectcontent/*`, `compiledeffect/*`). A material has no state: every property is a view over `OpaqueData` or `Textures`, so setting one to null removes its entry and reading one whose stored value has another type answers null rather than refusing -- both measured, neither guessable from the signature. Three findings reach beyond the materials and are recorded in the format document: `ContentItem` serializes `Name` and `OpaqueData` (not `Identity`) before a derived type's own members; an optional member holding an empty string is omitted like an empty collection; and an external reference's `TargetType` is spelled with a namespace alias only when the document already declares one, never by declaring a new one. `TextureReferenceDictionary` writes its entries as `<Texture Key="…">`, so `NamedValueDictionarySerializer` gained the collection item name it had hard-coded. `CompiledEffectContent` (`XNAPP-135`'s type) landed here too, because `EffectMaterialContent` references it. |
| `XNAPP-095` | `NodeContent` (Transform, AbsoluteTransform, Parent, Children, Animations), `NodeContentCollection`, `BoneContent`, `MeshContent` (Geometry, Positions), `GeometryContent` (Indices, Material, Vertices, Parent), `GeometryContentCollection`. | [x] `NodeContent.hpp`/`.cpp`, measured first (corpus cases `node/*`, `mesh/*`, `geometry/*`). A node starts with the **identity** transform, not the zero matrix a default .NET `Matrix` is -- visible only in what the serializer writes, which is where the measurement caught it. `AbsoluteTransform` composes up the chain; a node refuses a second parent with the runtime's own `InvalidOperationException` text, which `ChildCollection` now gives everywhere (its Phase 3 pin was updated to the measured wording); removing or clearing detaches. The intermediate form is pinned in both directions: `Name`, `Transform`, then `Children` (item `Child`, with a `Type` attribute for a derived node) and `Animations`, each omitted while empty; a mesh adds `Positions` and `Geometry` (item `Batch`); a batch writes its material as an **optional shared resource**, its indices, then its vertices. Reading an absent optional shared resource no longer refuses -- the reader had no such case before. |
| `XNAPP-096` | `VertexContent` (Channels, PositionIndices, Positions, VertexCount, Add/Insert/Remove/ConvertToVertexBufferContent), `VertexChannel`/`<T>`, `VertexChannelCollection` (20), `VertexChannelNames` (13), `PositionCollection`, `IndirectPositionCollection`, `IndexCollection`, `BoneWeight`, `BoneWeightCollection` (`NormalizeWeights` ×2), `VectorConverter` (5). | [x] All of it. `VertexContent::CreateVertexBuffer` closed with `XNAPP-134`: the layout is the position first and then every channel a vertex element can carry, in channel order (measured, `modelprocessor/triangle`). First half (measured, `vertexnames/*`, `boneweight/*`, `indexcollection/*`, `positioncollection/*`): the blend weights channel is spelled `Weights` rather than by its `VertexElementUsage` name, a name with no trailing digits decodes to usage index 0, a bone weight is a value type whose weight must lie in [0, 1] (NaN passes), and `NormalizeWeights` sorts largest first, keeps the largest ones, then scales them to sum to one -- the parameterless overload refusing an empty collection with the normalization message rather than the `maxWeights` range check. Second half (measured, `vertexcontent/*`): every channel follows its vertices, gaining a default entry where one is inserted and losing it where one is removed; a channel added with the wrong entry count, a duplicate name, an unknown name and a wrong-typed `Get<T>` each give the runtime's own message; `ConvertChannelContent<T>` replaces the channel in place through the `Vector4` conversion. XNA reflects a `VertexChannel<T>` into being from the `ElementType` attribute; CNA registers a factory per element type instead, and the base is `VertexChannelBase` because a class and a class template cannot share a name. `VectorConverter` landed with `XNAPP-092`. |
| `XNAPP-097` | `AnimationContent`, `AnimationContentDictionary`, `AnimationChannel` (10), `AnimationChannelDictionary`, `AnimationKeyframe` (`IComparable`). | [x] `AnimationContent.hpp`/`.cpp`, measured first (corpus cases `animation/*`, 21 of them). A channel keeps its keyframes ordered by time, answers the index it inserted at, and places a keyframe added at an occupied time **after** the one already there; membership is by reference, so a keyframe equal in time and transform to one in the channel is neither contained nor found. Both directions of the format are pinned: an animation writes `Duration` and `Channels` (neither optional -- an empty animation still writes `<Channels />`), each channel is `<Channel Key="…">` holding `<Keyframe>` elements, and each keyframe writes `Time` then `Transform` **despite both carrying `[ContentSerializerIgnore]`** in XNA, because the channel writes them itself. Reading puts every keyframe back through `Add`, so a document listing them out of order still yields an ordered channel. `NamedValueDictionary` gained the null-value refusal every named-value dictionary gives (`ArgumentNullException`, parameter `value`), which the measurements caught. |
| `XNAPP-098` | Compile-parity and behaviour tests for every Phase 6 type. | [~] `XnaGraphicsBitmapTests.cpp`: 11 tests over the texture side (`XNAPP-090`–`092`), `XnaFontDescriptionTests.cpp`: 10 over `XNAPP-093` `XnaMaterialContentTests.cpp`: 15 over `XNAPP-094` `XnaAnimationContentTests.cpp`: 13 over `XNAPP-097` `XnaVertexCollectionTests.cpp`: 11 over the first half of `XNAPP-096` and `XnaNodeContentTests.cpp`: 15 over `XNAPP-095` and the second half of `XNAPP-096`, each comparing against `tests/reference/xna40/graphics/graphics-content-oracle.json` -- layouts, pixel data, conversions and the 22-type converter table exactly; resampled pixels and DXT blocks within the tolerances the row above names. The oracle driver is `tools/xna-pipeline-oracle/graphics/`. `XNAPP-096`'s `CreateVertexBuffer` is covered by `XNAPP-134`'s model tests in `XnaProcessorTests.cpp`. |
| `XNAPP-099` | The framework packing rule Phase 6 depends on: measure how XNA turns a float channel into an integer one, and make CNA agree. | [x] `tools/xna-pipeline-oracle/framework/` (driver + `run-framework-oracle.sh`), 68 measurements committed as `tests/reference/xna40/framework/framework-packing-oracle.json`. XNA saturates and rounds every float channel to the nearest integer **with ties to even**, packs a NaN channel as 0, and truncates only in `Color.Lerp`/`Color.Multiply`. CNA truncated in the `Color` float constructors, rounded ties away from zero in the normalized packed types, used `+ 0.5f` in the colour layouts, and let `Color.PackFromVector4` wrap instead of saturate (all of it faithful to FNA, none of it XNA). Fixed through one shared helper, `CNA::Internal::ClampAndRound` (`modules/core/include/CNA/Internal/PackedRounding.hpp`), in `Color` and the fourteen packing headers; `XnaFrameworkPackingTests.cpp` reproduces all 68 cases and fails if a measured case gains no reproduction. Recorded as `XNAPACK-001` in `plans/plan_bindings_upstream.md`; the `REMED-CORE-004` pins that asserted the FNA behaviour were rewritten against the measurements. Measured gap left open: XNA's packed-vector structs override `ToString()` (packed value as hex) and CNA's seventeen do not. | 

### Phase 7 — processors namespace and property/default parity (28 types)

| ID | Task | State |
|---|---|---|
| `XNAPP-130` | `TextureProcessorOutputFormat`, `MaterialProcessorDefaultEffect`, `EffectProcessorDebugMode` enums (exact values). | [x] `Processors/ProcessorEnums.hpp`, values from the inventory metadata and pinned by a test; each is registered with the intermediate serializer under its .NET name. |
| `XNAPP-131` | `TextureProcessor` (6 properties with XNA defaults), `SpriteTextureProcessor`, `ModelTextureProcessor` as public classes over the canonical texture processor; the XNA defaults (`ColorKeyEnabled=true`, magenta) apply on the XNA façade and through `.contentproj`, the `.cna-content.json` route keeps its documented defaults — recorded as the one deliberate divergence of that route. | [x] `Processors/TextureProcessor.hpp`/`.cpp`, measured first. The defaults are read from the runtime's own objects (`processor/TextureProcessor`, `/SpriteTextureProcessor`, `/ModelTextureProcessor`), and `Process` is measured against a build context the driver supplies, because XNA's own build context is internal. The measured order is colour key, resize to the next power of two, premultiply, mipmaps, format -- and three things a reading would have missed: `NoChange` keeps the bitmap type the texture arrived with (a `Bgr565` texture stays `Bgr565`), `DxtCompressed` picks **Dxt1** unless a pixel is partly transparent (a colour-keyed cutout still fits Dxt1), and a null input is refused with `ArgumentNullException`. The canonical `CNA::Content::Pipeline::TextureProcessor` keeps its own parameter defaults for the `.cna-content.json` route; this façade is the XNA object-model processor and shares the canonical BC encoder through `DxtBitmapContent`. `BitmapContent` now registers every built-in bitmap type eagerly, because converting to a type no instance of which exists yet -- exactly what a compressing processor does -- found an empty registry. |
| `XNAPP-132` | `FontDescriptionProcessor`, `FontTextureProcessor` (3 properties), `SpriteFontContent`. | [x] `Processors/FontProcessors.hpp`/`.cpp` and `Processors/SpriteFontContent.hpp`, measured first (`fontprocessor/*`). `SpriteFontContent` publishes **no member of its own** in XNA -- the driver enumerated them and found none -- so CNA reaches its canonical sprite-font data through a CNAEXT accessor. The description processor rasterizes through the canonical `RasterizeFontDescription` and resolves the font the way the canonical importer does, reproducing XNA's refusals for a missing family and for a description with no characters. `FontTextureProcessor`'s defaults and its `GetCharacterForIndex` (first character plus the index) are measured; what its `Process` produces is not comparable beyond its boundary, because the output type publishes nothing, so the two measured outcomes are reproduced and the glyph packing is recorded as CNA's own. `CNA::Content::Pipeline::FindSystemFontFile` is now exported so the façade resolves a font through the engine's own rule rather than a second one. |
| `XNAPP-133` | `MaterialProcessor` (7 properties, `BuildTexture`/`BuildEffect`/`Process` protected virtuals). | [x] `Processors/MaterialProcessor.hpp`/`.cpp`, measured against a build context that records what it is asked to build (`materialprocessor/*`). The material comes back as the **same object** with each texture reference replaced by the built one; every texture goes through `TextureProcessor` with the six parameters this processor's properties map onto (`PremultiplyTextureAlpha` becomes `PremultiplyAlpha`, `ResizeTexturesToPowerOfTwo` becomes `ResizeToPowerOfTwo`); an effect material's effect goes through `EffectProcessor` with **no parameters at all**, and the result lands in `CompiledEffect`. Its own defaults differ from the texture processor's: mipmaps on, `DxtCompressed`. |
| `XNAPP-134` | `ModelProcessor` (14 properties; `ConvertMaterial`, `ProcessGeometryUsingMaterial`, `ProcessVertexChannel` protected virtuals; `Process`), `ModelContent` graph types (`ModelBoneContent(Collection)`, `ModelMeshContent(Collection)`, `ModelMeshPartContent(Collection)`, `VertexBufferContent`, `VertexDeclarationContent`) as the typed view of the canonical model IR. | [x] All of it, measured over 14 `modelprocessor/*` cases. What the measurements settled, none of which a reading of the API would have given: the processor's `Scale` and `Rotation*` are **baked into the scene in place** rather than left on the root bone -- the geometry is transformed, every node's transform is re-expressed in the new frame, and the caller's own `MeshContent` comes back transformed; the three rotations compose as **Z then X then Y**; a normal is transformed and then made unit again; every node becomes a bone in depth-first order and each geometry batch becomes one mesh part; the batches sharing a material are handed to `ProcessGeometryUsingMaterial` together, which is why that method takes a material and a sequence rather than one batch; a geometry with no material of its own is given a `BasicMaterialContent` before the conversion, and the conversion goes through `MaterialProcessor` carrying this processor's own seven texture parameters; `PremultiplyVertexColors` scales each colour channel by its own alpha **in whole bytes with the remainder dropped** (129 at alpha 3 answers 1, where rounding would answer 2); `GenerateTangentFrames` appends `Tangent0` and `Binormal0` after the existing channels and refuses a batch with no `TextureCoordinate0` by that name; `SwapWindingOrder` reverses each triangle's last two indices; and `VertexBufferContent.SizeOf` answers the sizes .NET *marshals* a value to -- `Boolean` 4 and `Char` 1 -- refusing an unsupported type with `NotSupportedException` and the null type with `ArgumentNullException`. Two comparisons are numeric rather than textual and say so in the test: the three-rotation case and the tangent frame carry the extended-precision intermediates the x86 .NET Framework evaluated the measurement in (3 units in the last place on one position component; a 4.4e-08 X where the cross product answers zero). |
| `XNAPP-135` | `EffectProcessor` (2 properties), `CompiledEffectContent` (`GetEffectCode`). | [x] `CompiledEffectContent` landed with `XNAPP-094` (both members, measured); `EffectProcessor` is `Processors/EffectProcessor.hpp`/`.cpp` over the canonical `EffectCompilerService`, which is the one effect compiler this repository has -- XNA compiles in-process with D3DX. Measured (`effectprocessor/*`, which compile real HLSL under Wine): a successful compile answers the byte code, a refused one raises `InvalidContentException` beginning `Errors compiling <file>:` followed by the compiler's diagnostics, a missing define fails with the compiler's own `X3004`, and a null input is refused. Two host differences recorded in the map: the runtime composed that message with `Environment.NewLine`; and a compiler that is not installed is reported through the same message, which cannot happen in XNA. |
| `XNAPP-136` | `SoundEffectProcessor`/`SongProcessor` (`Quality`), `SoundEffectContent`, `SongContent`; `VideoProcessor` (`VideoSoundtrackType`). | [~] The two audio processors and both content types are done, over 8 more `audio-content-oracle.json` cases. Measured: both processors default to `Quality=Best`; `SoundEffectProcessor.Process` **converts its input in place** -- at `Best` the source is left exactly as it is, and at `Medium` and `Low` it becomes ADPCM at that quality, answering the same numbers the plain `convert/adpcm_*` cases do; both processors refuse a null input with `ArgumentNullException("input")`; and `SoundEffectContent` and `SongContent` declare **no public member at all**, so CNA's carry the converted audio behind CNAEXT accessors. `SongProcessor.Process` is `EXTERNAL_BLOCKED`: a song is Windows Media audio, whose encoder exists only on the platform that owns it and whose behaviour could not even be measured -- XNA's own never returns under the oracle's Wine prefix. `VideoProcessor` waits on `XNAPP-043`'s `VideoContent`, which is its input and its output; its default is measured and recorded here for that task: `VideoSoundtrackType=Music`, with the enum `Music=0 Dialog=1 MusicAndDialog=2`. |
| `XNAPP-137` | `PassThroughProcessor`. | [x] `Processors/PassThroughProcessor.hpp`/`.cpp`: an object-to-object processor that answers its input, with the pipeline's `ContentObject` as the `object` carrier. |
| `XNAPP-138` | For every processor: tests with explicit values, omitted values (XNA defaults), and property interactions; defaults asserted against §10. | [~] `XnaProcessorTests.cpp`: 19 tests over `XNAPP-130`, `131`, `132`, `133`, `135` and `137`, each against the measured corpus rather than against §10's table -- the defaults are now read from the runtime, and §10 agrees with them. The remaining processors follow their own rows. |

### Phase 8 — MeshBuilder / MeshHelper / model intermediate API

| ID | Task | State |
|---|---|---|
| `XNAPP-150` | `MeshBuilder` (13: `StartMesh`, `CreatePosition` ×2, `CreateVertexChannel<T>`, `SetVertexChannelData`, `SetMaterial`, `SetOpaqueData`, `AddTriangleVertex`, `FinishMesh`, `MergeDuplicatePositions`, `MergePositionTolerance`, `SwapWindingOrder`, `Name`). | [x] All thirteen, over 10 `meshbuilder/*` measurements, tested beside `MeshHelper` in `XnaMeshHelperTests.cpp` (8 tests). What the measurements settled: the builder always merges the vertices that name the same position and carry the same channel data, while `MergeDuplicatePositions` decides only whether the *positions* merge, and both happen at `FinishMesh` rather than at `CreatePosition`, which keeps answering fresh indices; a channel value set once is carried into every corner that follows; `FinishMesh` gives the mesh normals when it has no normal channel and keeps the ones it has; finishing twice answers the same mesh; and a mesh with no triangles is answered with its positions and no geometry at all. `CreateVertexChannel` after the first corner, a channel data value of the wrong type, a channel index out of range and a corner count that is not a multiple of three are each refused with the runtime's own message; a null mesh name, a position index naming no position, a null material and null opaque data are all **accepted**. |
| `XNAPP-151` | `MeshHelper` (10: `CalculateNormals`, `CalculateTangentFrames`, `FindSkeleton`, `FlattenSkeleton`, `MergeDuplicatePositions`, `MergeDuplicateVertices` ×2, `OptimizeForCache`, `SwapWindingOrder`, `TransformScene`). | [x] All ten, over 14 `meshhelper/*` measurements, with `XnaMeshHelperTests.cpp` (11 tests). The inventory lists one `CalculateNormals` and one `CalculateTangentFrames`, not the two each this row first guessed. What the measurements settled: a face normal is the **clockwise** one, so a triangle wound counter-clockwise in the XY plane answers -Z; a vertex normal is averaged over the faces meeting at its **position**, so the two vertices of a texture seam come out with the same normal; overwriting a normal channel removes and re-adds it, which moves it to the end of the channel list; `OptimizeForCache` takes the triangles in **reverse** and renumbers the vertices in the order the reversed list reaches them, which a scrambled grid confirms by coming out as the exact reverse of the order it went in; `SwapWindingOrder` reverses the whole triangle rather than its last two corners; `MergeDuplicateVertices` keys on the position index and every channel entry; `MergeDuplicatePositions` merges within the tolerance and accepts a negative one; `FlattenSkeleton` is depth-first with the parent first; and `FindSkeleton` answers the same skeleton from any node of the scene. Every null argument is refused by its own parameter name. `ModelProcessor` (`XNAPP-134`) now routes its scene transform, tangent frames, winding swap and cache ordering through this type rather than carrying its own copies -- which fixed two defects the model rows could not see: its swap reversed only the last two indices, and it never ran the cache ordering, so a two-triangle mesh came out in the wrong order (measured, `modelprocessor/swap_winding_detail` and `quad_ordering`). |
| `XNAPP-152` | A custom processor builds a `NodeContent` graph programmatically, runs `ModelProcessor`, writes through the canonical XNB writer and loads in CNA. | [x] `XnaProcessorTests.cpp`, `XnaModelPipeline.AProcessorsOwnSceneBecomesAnXnbCnaReadsBack`: a game's own `ContentProcessor` builds a quad with `MeshBuilder`, gives it a `BasicMaterialContent`, runs `ModelProcessor`, and the compiler writes the result to an `.xnb` that CNA's own reader decodes back -- both bones, the mesh, the part, and all three shared resources with their bytes. The route added for it is `CNA::Content::Pipeline::ToCanonicalModel`, which turns the XNA-shaped graph into `CNA::Internal::Xnb::XnbModelData`: one shared resource per distinct vertex buffer, index buffer and material, sixteen-bit indices unless one does not fit, and each of the five stock materials as its own canonical effect resource. `ContentCompiler` registers the model writer as a built-in, so `Compile<ModelContent>` needs no user writer; like XNA's own `ModelWriter` it is internal, and no new type reaches the XNA namespace. The file shows the cache ordering the processor runs: the index buffer reads `0,1,2,0,3,1`, the second triangle first. |

### Phase 9 — audio / media intermediate API

| ID | Task | State |
|---|---|---|
| `XNAPP-160` | `AudioContent` (11: `FileName`, `FileType`, `Format`, `Data`, `Duration`, `LoopStart`, `LoopLength`, `ConvertFormat`, `Dispose`), `AudioFormat` (7), the three enums. | [x] Complete: `ConvertFormat` landed with `XNAPP-161`, which is closed, so nothing in this row is outstanding. A fourth oracle now measures the audio side (`tools/xna-pipeline-oracle/audio/`, 21 cases in `tests/reference/xna40/audio/audio-content-oracle.json`), and `XnaAudioContentTests.cpp` reproduces the ones this row covers with the driver's own WAV files written byte for byte. What the measurements settled: `Duration` is **whole milliseconds with the remainder dropped** -- 13228 bytes at 132300 bytes a second answers 99 ms, not 100; `LoopStart` is 0 and `LoopLength` the whole sound when the file names no loop; `NativeWaveFormat` is an eighteen-byte `WAVEFORMATEX` **including its `cbSize`** even for PCM; every unreadable source -- missing, wrong type, empty name, or not a WAV -- is refused with one message that names the file (`Failed to open file X. Ensure the file is a valid audio file and is not DRM protected.`); and after `Dispose` the samples throw while `Duration`, `Format` and `FileName` keep answering, with a second `Dispose` accepted. Reading is the canonical `ImportWavAsImportedSound`, so no second WAV reader exists. Only WAVE sources are read here: a named MP3 or WMA is refused with XNA's own unreadable-file message rather than decoded, which is recorded in the map. The enum values are all three complete, which closes the enum denominator at 27/27. |
| `XNAPP-161` | `ConvertFormat(Pcm, quality, target)`: PCM re-encode at XNA's three qualities (measured by `BuildContent`), `Adpcm` (MS ADPCM encoder, in-house), `WindowsMedia`/`Xma` refused with `EXTERNAL_BLOCKED` reasons unless §26 finds a route. | [x] `HOST_SUBSTITUTION`, recorded in the map with what is exact and what is not. **Pcm at Best is XNA's own answer byte for byte** -- the samples are untouched and an eight-bit source stays eight-bit -- and the two resampled qualities match XNA's rate, depth, channel count, byte rate, data length, loop and duration: **Low is half the source rate and Medium three quarters** (44100 answers 22050 and 33075), the frame count truncates, and the loop comes back one frame short of the data. The sample *values* are this host's resampler; XNA's lives inside its native helper. **Adpcm** is an in-house MS-ADPCM encoder in the canonical audio module (`CNA::Internal::Audio::EncodeMsAdpcm`), writing XNA's own block geometry -- format 2, four bits, 70 bytes per channel per 128-frame block, and the 32-byte extension with the seven standard coefficient pairs -- at the source rate, where XNA's own encoder also resamples to a rate of its choosing (43519 Hz for a 44100 source), which no in-house encoder reproduces. It chooses the predictor **and the starting delta** per block by trying seven coefficients against five scalings of the block's own average prediction error; a fixed starting delta rings at every block boundary (measured: peak error 10062 of 20000 before, 449 after). The round trip is verified through CNA's own MS-ADPCM decoder. `WindowsMedia` and `Xma` are refused by name -- neither encoder exists outside the platform that owns it, and neither could even be measured: XNA's Windows Media encoder never returns under the oracle's Wine prefix. |

### Phase 10 — texture input format parity (9 extensions)

| ID | Task | State |
|---|---|---|
| `XNAPP-165` | DDS reader: DX9 header + DX10 extension, uncompressed formats XNA accepts, DXT1/3/5, source mip chains, cube maps, volumes, pitch/linear-size validation; keeps compressed data compressed when `TextureFormat=NoChange`. | [x] Measured over eight DDS shapes (`textureimporter/dds_variants`) and implemented as `CNA::Internal::Graphics::ReadDdsSurfaces`, beside the cube decoder that already existed -- that one answers RGBA, and a pipeline needs the bytes the author stored. What the measurements settled: **a compressed source stays compressed**, answering `Dxt1BitmapContent`, `Dxt3BitmapContent` or `Dxt5BitmapContent` rather than decoded pixels; a source mip chain is kept whole, down to its 1x1 level; a cube map answers a **`TextureCubeContent` of six faces** and a volume a **`Texture3DContent` of one face per slice**; an uncompressed surface is converted to `Color` through whatever masks the header names. **The row's premise about the DX10 extension is wrong**: XNA's own reader refuses one outright, with the same corrupt-file message a garbage file gets, so CNA refuses it too rather than reading something the runtime would not. |
| `XNAPP-166` | PFM (portable float map) reader → `PixelBitmapContent<Vector4>`/`Single`; `.ppm` (P3/P6) and `.dib` routes; `.hdr`, `.tga`, `.bmp`, `.jpg`, `.png` behavioural audit against `BuildContent` output (channels, alpha, orientation). | [x] `TextureImporter` implemented and audited against the genuine one over the same four pixels (`textureimporter/*`). Measured: every source answers a `Texture2DContent` of **one face and one level**, its pixels **RGBA in that order**, top row first -- a bottom-up BMP, a top-left TGA and a bottom-up PFM all come out the same way up; a `.ppm` has no alpha and answers 255; a **portable float map answers `PixelBitmapContent<Vector4>`** with an alpha of one; and the **file's own bytes decide how it is read, not its extension** -- a PNG named `.xyz` is read as a PNG. The importer records no dependency and stamps an identity naming `TextureImporter`. A missing file is a `FileNotFoundException` reading `Can not read the texture "…". The file could not be found.`, and a corrupt one an `InvalidContentException` whose sentence CNA reproduces without XNA's trailing `D3DXERR_INVALIDDATA`, which is its own reader's code. A new PFM decoder lives in the graphics module beside the DDS one; `.dib` is read by putting back the file header a bitmap lost; everything else goes through the one shared image decoder. DDS came with `XNAPP-165`, in the same importer. |
| `XNAPP-167` | Per-extension fixtures, importer/processor/XNB/CNB/malformed/target tests; differential comparison per §21. | [~] The corpus is committed (`tests/assets/xna40/texture/`, written by `tools/xna-pipeline-oracle/texture/make_texture_fixtures.py`) and **both sides now read the same bytes**, which they did not before: each side used to synthesize its own file and the comparison silently assumed the two encoders agreed. They do not. Two things that assumption had been hiding: **`.hdr` had never been measured at all**, and XNA answers a `PixelBitmapContent<Vector4>` for a Radiance picture with the RGBE `(mantissa + 0.5) / 256 * 2^(e - 128)` convention -- CNA was answering `Color`, so a new `RadianceHdrDecoder` and an importer route landed; and **XNA's corrupt-file message carries a D3DX code chosen by whether the source had any bytes at all** (`D3DERR_INVALIDCALL` for empty, `D3DXERR_INVALIDDATA` otherwise), which CNA had been dropping. `XnaTextureExtensionTests.cpp` compares nine extensions, the three texture processors, all seventeen `TextureProcessor` property cases and five target/profile legs against `textureext/*`, `textureprop/*` and `textureprofile/*`. Two recorded divergences: a JPEG's pixels are compared within an IDCT's tolerance because two conformant decoders differ, and `ResizeToPowerOfTwo` within 48 of 255 because XNA resizes through D3DX's dithered triangle filter. One deliberate divergence: D3DX cannot read a flat (non-run-length) Radiance scanline and answers the second pixel first with infinities for the rest; CNA reads it. The source-to-XNB and source-to-CNB legs are `XnaSourceToOutputTests.cpp`, and all nine texture extensions are now `IMPLEMENTED+TESTED`. Writing them found what a per-format importer differential cannot: **four of the nine had no canonical route at all**. `.dds`, `.dib`, `.pfm` and `.ppm` were accepted by the XNA façade's `TextureImporter` and refused by the coordinator, so they imported perfectly and reached no container -- `Built: 0`, exit status zero, no diagnostic. `ImageImporter` now lists them and decodes them through the same readers the façade uses (`ReadDdsSurfaces`, `DecodePfm`), and a headerless DIB is re-headed inside `ImageLoader::LoadFromMemory` rather than in the pipeline, so the runtime and a content build answer the same pixels for the same bytes; the façade's private copy of that code, which wrote a temporary `.bmp` named after a heap address, is gone. All seven byte-comparable formats agree texel for texel. |

### Phase 11 — SpriteFont parity

| ID | Task | State |
|---|---|---|
| `XNAPP-180` | `.spritefont` schema audit vs `BuildContent`: every element, `CharacterRegions` multiplicity, `DefaultCharacter`, missing-glyph behaviour, `Style` values, `UseKerning`, `Spacing`, `Size` units. | [x] The schema is measured through the genuine `FontDescriptionImporter` (17 probes in `fontimporter/*`), and `FontDescriptionImporter` itself is implemented over the same serializer. A `.spritefont` is an intermediate XML document, read **positionally and strictly**: the order is `FontName`, `Size`, `[Spacing]`, `[UseKerning]`, `Style`, `[DefaultCharacter]`, `CharacterRegions`, and swapping `FontName` with `Size` is refused by name. **`Style` and `CharacterRegions` are required** -- a document carrying only a name and a size is refused with `XML element "Style" not found.`, which is what XNA's own template hides by always writing them. Omitting `Spacing` and `UseKerning` answers 0 and **false**, the state a default-constructed description has rather than the constructor's `true`. `Style` parses as flags, so `Bold, Italic` is accepted, and an unknown value carries the enum refusal by name. Multiple, overlapping and reversed `CharacterRegion` elements are all accepted; a region missing its `End` is not. The importer records no dependency and stamps an identity carrying the file and the tool name. CNA needed no change: the description `XNAPP-093` measured already had this exact shape. |
| `XNAPP-181` | `FontDescriptionStyle` reduced to XNA's three values on the façade; CNA's `BoldItalic` kept as a CNAEXT extension on the native route, recorded. | [x] Done with `XNAPP-093`, confirmed here. The façade enum has exactly XNA's three values (`Regular`, `Bold`, `Italic`) with the `|` operator a `[Flags]` enum gives, and the map records it `EXACT_EQUIVALENT`; the canonical `CNA::Content::Pipeline::FontDescriptionStyle` keeps its own `BoldItalic`, which `FontProcessors.cpp` maps a combined façade value onto. The `.spritefont` measurement confirms the flags form is what XNA reads: `Bold, Italic` is accepted and carries both bits. |
| `XNAPP-182` | Atlas/glyph/kerning/line-spacing/baseline differential vs XNA for an OFL font registered in the Wine prefix. | [ ] |

### Phase 12 — EffectImporter / EffectProcessor parity

| ID | Task | State |
|---|---|---|
| `XNAPP-190` | `EffectImporter`/`EffectContent`/`EffectProcessor` façade over the existing external-compiler route; `Defines` and `DebugMode` semantics (`Auto` = optimise unless Debug configuration). | [x] `EffectContent` landed with `XNAPP-094` and `EffectProcessor` with `XNAPP-135`; this row adds `EffectImporter` and settles `DebugMode` by measurement rather than by the documentation's word. The importer reads the file's text **verbatim** -- an empty file and a binary one are both accepted, and an `#include` is left for the compiler, with **no dependency recorded** -- and stamps an identity carrying the file and the tool name `EffectImporter`, which is the first importer measured that stamps one at all. A missing file carries XNA's own doubled message, naming the path twice. `DebugMode.Auto` is measured against both explicit modes as controls (`effectprocessor/debugmode`): the same shader answers 796 bytes under `Auto` with a `Debug` configuration and 696 under `Auto` with any other, matching `Debug` and `Optimize` exactly, and an explicit mode wins over the configuration. CNA already decided it that way; the rule is now measured, and a test holds the four combinations against the compiler request. |
| `XNAPP-191` | Genuine-compiler verification: build `.fx` fixtures through `BuildContent` under Wine (which uses the prefix's `d3dx9`), compare the Effect container CNA's route produces with a real `fxc`-compatible backend where one exists; record exactly which backend produced what. Closes or re-scopes `XNAP-A4`. | [ ] |
| `XNAPP-192` | Reach vs HiDef, includes/include dependencies, macros, techniques/passes/state/parameter/sampler reflection compared. | [ ] |

### Phase 13 — audio source parity

| ID | Task | State |
|---|---|---|
| `XNAPP-200` | `WavImporter` façade; WAV variants XNA accepts (measured by `BuildContent`: PCM 8/16/24/32, float, ADPCM, WAVE_FORMAT_EXTENSIBLE, loop metadata). | [x] Eleven variants measured through the genuine importer and all of them reproduced. XNA **keeps the file's own encoding rather than decoding it**: PCM 8, 16, 24 and 32, IEEE float, MS-ADPCM, IMA ADPCM and `WAVE_FORMAT_EXTENSIBLE` all come through with the fmt fields the file wrote -- the extensible tag even answers **-2**, the sixteen-bit `0xFFFE` read as a signed int -- and `NativeWaveFormat` is always the eighteen base bytes with `cbSize` zero, XNA rebuilding it rather than copying the source extension. `LoopLength` is the data length divided by the block alignment, which is frames for PCM and **blocks** for ADPCM; a `smpl` chunk's loop is honoured; an empty `data` chunk is refused. The importer adds **no dependency**, leaves `Identity` and `Name` null, and its missing-file message keeps XNA's **unformatted `"{0}"` placeholder**, which is reproduced verbatim. Reading it needed a new verbatim reader in the canonical audio module (`CNA::Internal::Audio::ReadWavFormatAndData`), because the existing one decodes to PCM16 and a pipeline must report what the author's file holds; `AudioContent` now reads through it. |
| `XNAPP-201` | `Mp3Importer`: MP3 decoding (dependency per §26) → `AudioContent`; what `SongProcessor` emits for it under XNA, measured, reproduced. | [x] Measured first, and the measurement changed the implementation three times. The genuine importer's `Format` is **not the source's**: it is the PCM the decoder will produce -- 16-bit at **44100 Hz for every source from 8000 to 48000 and across all three MPEG versions**, with only the channel count surviving, where `WavImporter` reports the source's own rate. `Duration` is the whole decoded stream truncated to whole milliseconds, and **XNA does not honour an MP3's gapless-playback information**: encoder delay and padding are counted, so a half-second tone is 548 ms and a Xing/LAME-tagged one 574 ms rather than the 500 ms of content (FFmpeg trims by default; `AV_CODEC_FLAG2_SKIP_MANUAL` is what leaves them in, and two of twelve corpus files caught it). `LoopStart` and `LoopLength` are **both 0**, where a WAV that names no loop answers 0 and its whole length. And the importers are **format-specific, not extension-specific**: a WAV renamed `.mp3` is refused, which the first implementation accepted because a demuxer reads whatever it recognizes. Decoding is `CNA::Content::Pipeline::BuildTimeMedia`, behind `CNA_ENABLE_MEDIA_PIPELINE` and linked only by `cna_content_compiler`. |
| `XNAPP-202` | `WmaImporter`: measure XNA's output first; implement or record `EXTERNAL_BLOCKED` with the exact codec reason. | [x] Implemented, **not** `EXTERNAL_BLOCKED`. The measurement attempt is recorded rather than skipped: the genuine importer refuses every WMA in the corpus before opening it, because Wine 10.0 carries no Windows Media Format runtime; `winetricks l3codecx directshow mf` does not change it and `winetricks wmp9` leaves `XnaMediaHelper_1.dll` unloadable and was reverted (`docs/xna-content-pipeline-media.md` §6). So the successful path is held to the format itself and to the shape the MP3 measurement settled for the same `SongProcessor` input, and the map and the input matrix say exactly that instead of claiming a measurement. The refusals it *can* be compared against -- a missing file's unformatted `"{0}"`, and a source whose bytes are not Windows Media audio -- are reproduced. |

### Phase 14 — video source parity

| ID | Task | State |
|---|---|---|
| `XNAPP-210` | `WmvImporter`/`VideoProcessor`: measure what XNA emits (the `.xnb` is a header plus an external media file); implement the container semantics; codec availability per §26. | [~] `VideoContent`, `WmvImporter` and `VideoProcessor` are implemented and their measured behaviour reproduced. What the measurements settled: the **constructor is eager**, so the same missing file is a `FileNotFoundException` through the importer and an `InvalidContentException` through `VideoContent` itself, and a **null filename is not refused as null** -- it reaches the message as an empty name; every unreadable source carries the one sentence `Video file X is invalid. Please make sure that the video is not DRM protected and is a valid single-pass CBR encoded video file.`; `VideoProcessor` defaults to `VideoSoundtrackType.Music`, refuses a null input with `ArgumentNullException("input")`, and answers **its own input** rather than a copy. The successful decode could not be measured -- Wine 10.0 aborts on `mfplat.dll.MFCreateVideoMediaType` and faults inside the helper for a 64x48 source (`docs/xna-content-pipeline-media.md` §6) -- so the shape read from the container is held to the format. **Remaining: the source-to-XNB leg**, which needs the writer to name the external media file. |

### Phase 15 — FBX importer

| ID | Task | State |
|---|---|---|
| `XNAPP-215` | Dependency audit and choice for a native FBX parser (§26). | [x] **No third-party parser was taken.** The audit's outcome: an FBX document is a node tree in two encodings, and both are readable in about six hundred lines (`CNA::Content::Pipeline::ReadFbxFile`), where a library would have brought its own scene normalization -- triangulation, node merging, unit conversion -- to fight with XNA's, which is the failure mode §26 warns about. The one external thing the binary encoding needs is **zlib**, for the deflated arrays a current exporter writes; it is optional (`find_package(ZLIB QUIET)`), and a compressed array without it is refused by name rather than mis-read. Assimp is used to *write* one fixture, never to read one. |

### The canonical model route (`XNAPP-021`, Phases 15 and 16)

Both readers, XNA's `ModelProcessor`, `MaterialProcessor` and `TextureProcessor`, and the
`ModelContent` -> canonical-model bridge were all implemented and measured before anything could
reach them: `XnaComponentNames` mapped `XImporter` and `FbxImporter` onto `CNA.XImporter` and
`CNA.FbxImporter`, and **no registry contained either name**, so a `.contentproj` naming a model
built nothing and reported nothing wrong. `RegisterXnaModelSourceContentPipeline` is the wiring:
the two importers, `CNA.XnaModelProcessor` (XNA's processor, with its output turned into the one
processed-model value both writers already take -- schema 1 when every semantic fits it exactly,
schema 2 otherwise, the same choice the `.xnb` import route makes), and, under XNA's own names
because XNA's own components reach them by name, `MaterialProcessor` and a `TextureProcessor` for
the nested build a material starts. Every one of the eighteen well-formed model fixtures builds to
both containers; every one of the nine malformed ones is refused.

Four defects had to be fixed for that to be true, and all four were reachable before this route
existed:

- **A nested build's outputs were compiled and then dropped.** `ConvertCore` created a context for
  the converted processor and discarded it, and `MaterialProcessor` -- the only way XNA reaches a
  model's textures -- is reached only through `Convert`. The model referred to a texture no build
  ever wrote. `XNAPP-044` claimed this worked; it worked for `BuildAsset` called directly from a
  top-level processor and for nothing else.
- **A nested build's writer schemas never joined the node's own.** The manifest describes every
  output it publishes by looking the schema up in the node's writer's list, and a nested output was
  written by a different writer, so publishing one failed the manifest. Carrying the schemas up
  fixes the manifest *and* the incremental check, which could not otherwise notice a change to the
  codec that wrote a nested output.
- **A nested copy and a listed item fought over one asset name.** A project that lists the texture
  its model names -- which is what a real XNA project does -- had two nodes claiming
  `Textures/surface`. The item keeps it and the model's nested copy is dropped, because the item is
  what the project asked for with the processor it chose. Only nested copies yield; a writer's own
  additional output colliding is still a conflict, which is what `ContentPipelineCliTest`
  `AdditionalOutputCannotClaimAnotherBuildNodesIdentity` measures.
- **`ToProcessorParameters` refused a `Color` and refused an enum.** XNA's processors forward both
  to one another, so every model that reached a material failed halfway through processing with a
  message telling the user to spell the value as a string. They are now spelled as strings on the
  way through, which is what every parameter binding parses and what a `.contentproj` writes;
  nothing about how the XNA processors box their forwarded values changed, so the measured
  forwarding is still what the oracle recorded.

`ContentProcessor::SelectedByNameOnly()` is the one new contract: a processor registered under a
name another component reaches it by, which must not join default resolution. Without it the
XNA-named `TextureProcessor` competed with `CNA.TextureProcessor` for every `.png` in the tree.

### Target-platform parity for the source families (`XNAPP-021`, Phase 13)

The texture route's five legs were the only ones measured; seven extensions carried a blank
`target`. All seven are now measured or recorded, and the measurement is not uniform:

- **`ModelProcessor` and `FontDescriptionProcessor` answer identically on all five legs**
  (`modelprofile/*`, `fontprofile/*`). The target is a no-op for these two -- measured, not
  assumed. The font legs use a family neither side has, because XNA resolves a family through the
  host and this prefix's font list is not the one CNA sees; a refusal is still an answer, and it is
  the same answer on every leg.
- **`SoundEffectProcessor` is the one that diverges, and completely.** On Xbox 360 XNA converts the
  audio to XMA -- format tag `0x6601`, a different duration, different loop points, a quarter of the
  data, and a big-endian `XMA2WAVEFORMATEX` where the other targets carry a `WAVEFORMATEX` -- while
  Windows and Windows Phone keep the source's PCM (`soundeffectprofile/*`). CNA reproduces the two
  PCM targets exactly and answers PCM on Xbox as well. **This is not a "no decoder in this build"
  case**: XMA is Microsoft's own codec, it has no public specification, and there is no licensable
  third-party encoder for it, so this is a real capability gap rather than unwritten code. The test
  asserts the divergence rather than hiding it, and names what would have to change for the leg to
  become an equality. `.wav`'s `xbox360` row therefore reads `UNSUPPORTED`, with the reason.
- **`SongProcessor` and `VideoProcessor` cannot be measured here at all.** Every
  `videoprocessor/target_*` case is `SEHException` because constructing a `VideoContent` needs Media
  Foundation and Wine aborts on `MFCreateVideoMediaType`; XNA's Windows Media encoder, which
  `SongProcessor` needs, never returns under the same prefix. Both legs therefore hold CNA's own
  half only -- the same answer for every target -- and say in the test that they are the weaker
  claim, with the recorded XNA refusals asserted so the leg turns into a differential the moment
  the environment can answer.

### The `.fx` route against the compiler XNA used (`XNAPP-021`, `XNAPP-191`/`192`)

Microsoft's legacy `fxc` at `fx_2_0` from the June 2010 DirectX SDK is on this machine and runs
under Wine, so the route could be measured rather than reasoned about. Two things came out of it.

**A real defect.** A path handed to a compiler *through a launcher* was spelled the host's way, so
`fxc.exe` read `/tmp/cna-fx-0-.../effect.fxb` as an option -- a leading `/` is how a Windows command
line begins one -- and answered `Unknown or invalid option`. The whole `.fx` route was broken on
every non-Windows build machine, which is every machine this project builds on, and no test caught
it because every other `.fx` test drives a scripted compiler. Paths are now spelled the way the
launcher's program reads them, asked of the launcher itself (`winepath -w`, once per compile for
every path at once) rather than guessed; a launcher whose translation has not been measured gets
the paths unchanged.

**A measurement nothing here had.** `EffectCompilerService.cpp` already said XNA "wraps it in a
`0xBCF00BCF` header carrying the offset of the inner token" -- what nobody had measured is the
header's contents. It is sixteen bytes: magic `0xBCF00BCF`, `0x10` (the offset of the inner token),
then two zero dwords, and it is byte-identical in front of two unrelated effects
(`effectprocessor/compile_simple_digest`, `effectprocessor/compile_second_digest`, with XNA's own
bytes published beside them). CNA emits it now, so its container header is XNA's.

**What still differs, exactly.** The blob behind the header. XNA compiles from a memory buffer
through `d3dx9`; CNA runs the `fxc` command line, which embeds source information the memory path
does not. For the measured effect XNA's inner blob is 460 bytes and `fxc`'s is 476, first differing
at offset 50 of the blob. It is not the source path: compiling from a file named `shader.fx` with a
relative name and a working directory gives the same 476. Closing it means calling
`D3DXCreateEffectCompiler` on a buffer, which needs a small native helper linked against `d3dx9`
and run under Wine -- `d3dx9_43.dll` is present, so this is work rather than a blocker.

| `XNAPP-216` | `FbxImporter`: hierarchy, transforms, meshes, channels, normals/tangents/UVs/colours, skin weights, animations, materials/textures, coordinate-system and unit conversion as `BuildContent` observably does them. | [x] Measured first, and almost every rule differs from the `.x` route's for no reason a reader would predict. **FBX is already right-handed, so nothing is converted** -- positions and normals pass through as written where `.x` negates Z -- and yet **the winding IS reversed**, a polygon `0, 1, 2` answering indices `2, 1, 0`. **A texture coordinate's V is flipped** (`0.2` answers `0.8`), where `.x`'s is not. The channel order is normals, texture coordinates, then colours; `.x`'s is normals, colours, then texture coordinates. A vertex colour is **not** quantized through eight bits; `.x`'s is. A material reaches a batch **only through a `LayerElementMaterial`** -- a `Connect` alone leaves the batch material-less -- and it keeps the name the file gave it, where a `.x` material's name is dropped; and `Opacity` and `Shininess` are **not read at all**, every material answering the SDK's own alpha of 1 and specular power of 20. The three refusals are told apart by content and not by size: a file with no FBX header is the loader's initialization failure at 31 bytes and at 1024 alike, a DirectX `.x` file is `Could not detect file format`, and a document that parses no further is `encountered when importing the scene`. |
| `XNAPP-218` | Read the FBX versions XNA cannot: its FBX SDK 2011.3.1 refuses every document of version 7400 and above. | [x] **A deliberate divergence, recorded rather than assumed away.** Every current exporter writes 7400 or 7500, and the genuine importer refuses all of them (measured, `fbx/fbx_binary_modern.fbx`); matching a bundled SDK's age would serve nobody. CNA's reader takes the text encoding (which is what a 6.1 document is, and what XNA reads) and the binary record stream alike, with zlib for the deflated arrays and a named refusal without it. |
| `XNAPP-217` | FBX black-box corpus (CNA-authored/generated, plus permissively licensed) compared per §24. | [x] Six documents in `tests/assets/xna40/model`, written by `make_fbx_fixtures.py` in **FBX 6.1 ASCII** for a measured reason: XNA carries FBX SDK 2011.3.1, and an FBX written by a current tool is version 7400 or 7500, which that SDK refuses. Nothing is third-party: the content is authored here and the container is written here. `XnaFbxImporter` compares each graph for graph against `model-import-oracle.json`, with two documented normalizations -- a triangle is compared as a cycle, because the SDK picks its own starting corner when it triangulates a quad, and every number to a tolerance, because both sides' matrices come out of float trigonometry. |

### Phase 16 — DirectX `.x` importer

| ID | Task | State |
|---|---|---|
| `XNAPP-220` | `.x` text and binary (incl. compressed) parser: templates, `Frame`, `FrameTransformMatrix`, `Mesh`, `MeshNormals`, `MeshTextureCoords`, `MeshVertexColors`, `MeshMaterialList`, `Material`, `TextureFilename`, `SkinWeights`, `XSkinMeshHeader`, `AnimationSet`/`Animation`/`AnimationKey`, `VertexDuplicationIndices`, `DeclData`. | [x] `CNA::Content::Pipeline::ReadDirectXFile`, reading both uncompressed encodings -- the text one and the binary token stream -- into one object tree. `template` blocks are skipped rather than interpreted, because every template this pipeline needs is one of the standard ones whose layout is fixed. **The two compressed encodings, `tzip` and `bzip`, are refused by name** rather than silently mis-read; they are MSZIP, and nothing in the corpus or the samples uses them. `VertexDuplicationIndices` and `DeclData` are read as data and ignored, as the genuine importer's answer shows it ignores them. |
| `XNAPP-221` | `XImporter` façade (4 members); black-box corpus and comparison. | [x] Fourteen authored documents compared graph for graph against the genuine importer (`model-import-oracle.json`, cases `x/*`). Ten rules came out of the measurement, none of them derivable from the format: **Z is negated** on positions, on normals and on a frame's matrix as the basis change `S M S` -- which needed a fixture written to catch it, because every earlier one had `z = 0` everywhere; a file whose single top-level object is a `Frame` answers **that frame as the root**; each material answers **its own `GeometryContent`** sharing the mesh's positions, with the material's `OpaqueData` written diffuse, specular, emissive, alpha, power, an order that is observable; **a mesh child is ordered before a frame child**, whatever order the file wrote them in; **`SkinWeights` are read only where an `XSkinMeshHeader` declares a skeleton**, and a zero weight is dropped; **vertex colours round-trip through eight bits**, which is why 0.5 comes back as 0.501961; separate rotation, scale and position key lists merge into one matrix track **by interpolation** at the union of their times, not by holding; a tick is **1/4800 of a second** unless `AnimTicksPerSecond` says otherwise, and `Duration` is the last key **truncated to whole milliseconds** while the keys keep full precision; and every animation in a set lands on the **skeleton's root bone** where there is a skeleton and on its own target where there is not. Each refusal carries the D3DX code its kind of failure gets, `E_FAIL` included, for a face naming a vertex the mesh does not have. |

### Phase 17 — XML importer / intermediate integration

| ID | Task | State |
|---|---|---|
| `XNAPP-230` | `XmlImporter` (no default processor) over Phase 5; `.xml` → `Object` → `PassThroughProcessor` → automatic writer → XNB; XNA's own XML fixtures (the samples' `.xml` content) as the black-box corpus. | [x] `XmlImporter` over the Phase 5 serializer, measured through nine new `importer_*` cases in the intermediate corpus (the oracle now drives the genuine importer, not only the serializer). What they settled: the importer builds **whatever the document's `Asset Type` names** -- a content item, a primitive, a list -- and **adds no dependency** to its context and leaves an imported content item's `Identity` **null**; a file that is not there is refused with the runtime's own `FileNotFoundException` rather than a content one; a malformed document, a missing `Asset`, an unknown type and an empty file each carry the serializer's own message. The two the XML parser itself rejects keep XNA's sentence and carry this parser's reason, a divergence recorded in `docs/xna-intermediate-xml-format.md` §12. Reaching an untyped `List[string]` needed one thing CNA did not have: XNA finds a `List<T>` by reflection, so the built-in registration now instantiates the lists of every type the serializer already knows, which is what gives an untyped read the same reach. No default processor, as XNA declares none. |

### Phase 18 — `.contentproj` compatibility

| ID | Task | State |
|---|---|---|
| `XNAPP-240` | `.contentproj` parser → canonical build graph (§18 schema); `cna-content build Foo.contentproj`; unknown metadata refused, not guessed. | [~] `Tasks::ContentProject` reads the whole §18 schema -- `Compile` with `Name`, `Importer`, `Processor`, `ProcessorParameters_X` and `Link`; `Content`/`None` with `CopyToOutputDirectory`; `XnaPlatform` (with `Xbox 360` and `Windows Phone` normalized), `XnaProfile`, `XnaCompressContent`, `ContentRootDirectory`, `XnaFrameworkVersion`, `ProjectGuid`, and the references -- and fills a `BuildContent`, which runs the canonical coordinator. No second build engine and no second project format; `.cna-content.json` is untouched. **`Condition` is decided rather than guessed**: every condition the 170 sample projects use is one comparison of two quoted, property-expanded strings, evaluated in document order so a default-value guard means what it says, and anything else -- a function call, a numeric comparison -- is refused by name rather than silently adding or dropping assets. `UnroutableEXT()` names every asset whose importer or processor is not built in, all at once. **Remaining: the `cna-content build Foo.contentproj` command line**, and copying the `Content`/`None` items. |
| `XNAPP-241` | The four `Tasks` types as `HOST_SUBSTITUTION` rows with the exact mapping of each input/output property. | [x] `BuildContent`, `BuildXact`, `CleanContent` and `GetLastOutputs`, as tasks a caller can drive. Every input property, every output property, the `bool Execute()` contract and XNA's own `CancelEventNameFormat` are reproduced; what is replaced is MSBuild's engine, and the caller takes its role. An `ITaskItem[]` is a `std::vector<TaskItem>`, `TaskItem` being that interface's whole developer-visible contract -- an `ItemSpec` and a case-insensitive metadata table. `BuildContent` builds nothing itself: it translates the item model (`Name`, `Importer`, `Processor`, `Link`, `ProcessorParameters`, in both spellings a `.contentproj` writes) into the canonical coordinator's own build configuration and runs `RunContentCompiler`, which keeps one engine rather than two and forced the piece `XNAPP-240` needs anyway -- a shared table mapping XNA component names to canonical ones, carrying the parameters that make one canonical texture processor behave as `TextureProcessor`, `SpriteTextureProcessor` or `ModelTextureProcessor`. Two refusals matter more than the successes: a project naming a `PipelineAssemblies` item is **refused rather than silently built without its own importers**, and `BuildXact` **validates every `.xap` before** reporting that Microsoft's `XactBld3.exe` is absent. |
| `XNAPP-242` | Build the public XNA samples' `.contentproj` files (108 available locally) through the parser and record which items route, which need a custom processor, and which fail and why. | [~] **170 projects on this machine, and all 170 read**, carrying 8795 source assets; 115 of them route entirely with no custom component. The sweep is a test (`XnaContentProject.ThePublicSamplesProjectsAreReadAndTheirRoutesReported`) rather than a one-off, so a schema this reader stops handling fails the build. Two things it found that a written fixture would not have: **126 of the 170 use a `'$(Configuration)|$(Platform)' == 'Debug|x86'` property group**, which the first reader refused outright, and the `.fbx` and `.x` component names were still mapped to "not implemented in this build" after both importers had landed. What the remaining 55 need is a custom processor of the sample's own -- `SkinnedModelProcessor`, `SkyProcessor`, `SpriteSheetProcessor` and seven more -- which is `XNAPP-280`'s external-extensibility route and not a defect here. **Remaining: actually building one end to end**, which needs the model importers registered as canonical components. |

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
| `XNAPP-260` | External CMake project with a custom intermediate type, importer for a custom extension, processor, `ContentTypeWriter<T>` with a custom runtime reader name, dependencies, nested `BuildAsset`, shared resources, fingerprints, diagnostics; run as a ctest. | [x] `modules/content-pipeline/examples/xna-custom-pipeline.cpp` is a game's own pipeline -- its own types, its own `.quest` extension, an importer and processor derived from XNA's bases, a `ContentTypeWriter<T>` naming readers in `QuestGame`, a sidecar dependency, a nested `BuildAsset`, a shared hub step written once for two references, declared processor parameters and its own diagnostics. It links into its own executable, includes only the two headers a consumer is meant to include, and nothing in CNA knows it exists; `XnaCustomPipelineAcceptanceTests.cpp` runs it the way a user would and reads the container back. Acceptance found two gaps, both now closed. **A factory could not see the container options**: a caller registering its own XNB writers had to bind them to defaults, so a `--xnb-platform windowsphone` build would have sent the game's own types to a Windows container while everything else went where it was asked -- `ContentCompilerOptions` now carries them, deliberately excluded from the equality that decides whether a pre-built registry can be used. **`LogImportantMessage` was indistinguishable from `LogMessage`**: both mapped to `Info`, which the tool suppresses, so the one XNA documents as reaching the user even at low verbosity reached nobody. There is now a `ContentLogLevel::Important` between `Info` and `Warning`, printed as `message` rather than dressed up as a warning it is not. |

### Phase 21 — black-box XNA differential harness

| ID | Task | State |
|---|---|---|
| `XNAPP-265` | `tools/xna-pipeline-oracle/differential/`: C# `BuildContent` driver (mcs + Wine), corpus manifest, per-case result JSON. | [x] `DifferentialOracle.cs` runs Microsoft's own `BuildContent` -- the task an XNA project's MSBuild invokes -- over the 38-case committed `corpus.json`, one case per (source, importer, processor, parameters, platform, profile), and records per case whether it built, the compiled bytes, the diagnostics and the files the task says it wrote, read from the task's own output properties. `XnaDifferentialBuildTests.cpp` builds the same corpus through CNA's own `BuildContent` façade -- the same ItemSpec, the same metadata, the same platform and profile, so both sides are asked the same question -- and compares outcomes. **All 38 now agree.** Getting there found four defects, every one of them invisible to the per-component differentials: XNA refuses to DXT-compress a texture whose dimensions are not multiples of four and CNA silently padded one (measured from both sides -- 2x2 and 3x2 refused, 4x4 built); `TaskItem` folded metadata names to lower case, so every `ProcessorParameters_Scale` arrived as `scale` and no processor recognised it; a mapping's default parameter beat the project's own explicit value, so a `.contentproj` asking for `DxtCompressed` got `Color`; and `ScopedEnvironment` restored a previously-unset variable as present-but-empty, which every later test in the process then saw. The corpus's own model fixture had to change too: `surface.png` was 2x2, which makes `quad_textured.x` a model XNA itself cannot build. |
| `XNAPP-266` | Extend `tools/xnb/xnb_conformance.py` to normalize every root type this plan emits; `compare.py` with the §24 semantics; decision file for accepted differences. | [ ] |
| `XNAPP-267` | Error-parity corpus: malformed/unsupported/impossible-parameter/missing-dependency/cyclic/unsupported-target/bad-declaration/missing-glyph/shader-failure cases; XNA and CNA failure classes compared. | [ ] |

### Phase 22 — genuine XNA runtime verification

| ID | Task | State |
|---|---|---|
| `XNAPP-280` | Compile and run `tests/interop/xna40` under Wine (`csc.exe`, DXVK, `Xvfb :131`) against the committed corpus; record the result here and in `plan_xnapipeline.md` `XNAP-34`. | [x] **Done, and every fixture passed.** Six of six CNA-written `.xnb` files loaded through the genuine Microsoft XNA 4.0 `ContentManager` and every value their expectation manifests declare matched: `texture2d_color_mips`, `soundeffect_pcm16_mono_22050`, `spritefont_two_glyphs`, `curve_two_keys`, `list_of_strings` and `model_triangle_basiceffect`. The runtime reported `4.0.0.0` and the device was a real one -- `AMD Radeon 780M (RADV PHOENIX)`, `Reach` -- so the texture, font and model roots were exercised against a GPU and not a stub. The recipe is `tests/interop/xna40/run-interop-harness.sh`: `mcs` against the SDK reference assemblies, `wine` in `~/.wine-cna-xna40` (GAC, `XnaNative.dll`, Direct3D 9 through DXVK), `DISPLAY=:99` on Xvfb; it rebuilds from a clean directory each run and was verified that way. **One harness defect this found**: `Game.RunOneFrame()` returns on this host with `GraphicsDeviceManager.GraphicsDevice` still null, so `Texture2D`, `SpriteFont` and `Model` all failed with `GraphicsDevice component not found` -- a message about the host, not about the `.xnb`. Calling the documented `((IGraphicsDeviceManager)manager).CreateDevice()` makes a real device; the harness now does that first and falls back to `RunOneFrame()`, and prints which case it was. The README's "no usable graphics device" limitation was avoidable, not inherent. What this does **not** show: only these six roots, and only the values XNA 4.0's public API exposes -- `XNAPP-281` is the rest. |
| `XNAPP-281` | Extend the harness and corpus to every output family (§25) with value assertions; every family marked `xna40` only after execution. | [~] Both corpora now pass six of six against the genuine runtime, and the compressed one only after a **real defect this harness found and nothing else could have**. Every LZX-compressed fixture was refused with `Error decompressing content data.` while CNA's own decoder and the independent Python parser accepted all six -- neither reads ahead, so neither could see what was missing. A hand-built LZX *uncompressed* block was refused too, which ruled out the Huffman coder; a Microsoft-loadable file from another writer turned out to end with **five zero bytes after its final block**; and handing the real runtime the same asset with 0 through 6 trailing bytes settled it -- four failed, five loaded. Two of the five are the next chunk's size field, consumed before the reader notices the stream ended, and three are slack for a bit buffer that fills a 16-bit word at a time. The encoder emits the trailer now and `LzxEncoderTest.EveryCompressedPayloadEndsWithTheTrailerXnaRequires` holds it there. Every fixture in that corpus is one LZX frame, so the multi-frame case was checked end to end as well: a 256x256 texture built from a `.png` by `cna-content build ... --format xnb --xnb-compress lzx`, 262 321 decompressed bytes over **nine frames**, loads as a `Texture2D` in the same runtime -- source through CNA's importer, processor, writer and encoder into Microsoft's `ContentManager`. Remaining: the output families beyond these six. |

### Phase 23 — fuzzing / hardening

| ID | Task | State |
|---|---|---|
| `XNAPP-290` | Fuzz targets: DDS, PFM/PPM, X (text+binary), FBX, intermediate XML, MP3 framing, `.contentproj`; sanitizer runs; explicit ceilings documented. | [~] **The two readers written in session 2 are cleared.** `tools/content/xna_model_fuzzer.cpp` has the same two shapes as the intermediate-XML harness beside it (standalone replay and mutation; a libFuzzer entry point on request), and `XnaModelReaderHardeningTests` runs a deterministic mutation pass on every build over the committed corpus, in three shapes because they break different things -- a flip corrupts a value in place, a truncation ends the document mid-structure, and a zero run turns a length or a count into something a reader might trust. The run they were actually cleared under is a standalone **ASAN + UBSAN** build of the two translation units (both depend on nothing but the standard library and zlib, so a sanitizer configuration of the whole project would be several hundred megabytes to prove the same thing about the same two files) over **1,159,999 mutated documents** -- 42,910 `.x` and 61,904 FBX parsed, 1,055,186 refused, no memory error, no undefined behaviour, no leak, and no exception escaping past each reader's own type. Every tree a mutated document produced is walked rather than merely held, so a reader answering a view of freed or out-of-range memory would be caught. Ceilings are checked on the declaration: a `.x` nested past its depth limit and an FBX array claiming four billion doubles are both refused without an allocation being attempted. **Remaining: DDS, PFM/PPM, MP3 framing and `.contentproj`**, and a campaign under a real driver rather than a fixed-seed mutation. |

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

Build dir `cmake-build-debug/` (`Debug`, Ninja, `CNA_PLATFORM=SDL3`, `CNA_GRAPHICS_RENDERER=HEADLESS`,
`CNA_AUDIO_PLATFORM=SDL3`, FreeType 2.13.3, FFmpeg, Zstandard, Draco off), both binaries run from
the repository root with `env -u WAYLAND_DISPLAY DISPLAY=:99 SDL_VIDEODRIVER=x11`, 2026-09-05, at
`92fb589f9` (after the one-line `XNAPP-006` test-compile fix, before any façade code):

```text
CnaContentTests:          1794 run   1779 passed   19 skipped   6 failed
CnaContentPipelineTests:   112 run    112 passed    0 skipped   0 failed
```

All 6 failures are environmental and reproduce on the unmodified tree:

| Failure group | Count | Cause |
|---|---:|---|
| `CnbTextureContentManagerTest.ATexture3DCnb…`, `…ATextureCubeCnb…`, `CnbTextureCubeProducerTest.…ThroughContentManager`, `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`, `CnjTexture3DTest.LoadsRealCnjFixture` | 5 | `HEADLESS` has no `TextureCube`/`Texture3D` storage, so `ContentManager::Load` of those roots refuses |
| `ContentManagerVideoXnbTest.TheObjectReferencedFormLoadsToTheSameValuesAsTheInlineOne` | 1 | Video-fixture load through this configuration's FFmpeg backend; unrelated to content building |

The 19 skips are the suite's own prerequisites (a >2 GiB file, an optional real-world glTF asset,
renderer-gated readers). **Any new failure outside these 6 is a regression.** The audio-device
failures `plan_xnapipeline.md` §0.4 recorded do not occur here: this machine has a sound device.

### 33.2 Handoff

Session 1 (2026-09-05): Phases 0–1 done except `XNAPP-008`/`015`/`016`; Phase 3 done except
`XNAPP-043` (`VideoContent`, with Phase 14), with `032`/`045` partial as their rows say. The parity map
is edited through `tools/xna-pipeline-oracle/parity_map_edit.py` with a decision document, never
by hand, and the report regenerates with `parity_report.py`. Phases 4 and 5 done (only the
`XmlImporter` leg of `074` remains, as `XNAPP-230`). Phase 6 is done (`XNAPP-090`–`097`, `099`; `098` partial).
Phase 7 is done except the `VideoProcessor` half of `XNAPP-136`, which waits on `XNAPP-043`'s
`VideoContent`; `138` stays partial for the same reason. **Phase 8 is done** (`XNAPP-150`–`152`):
`MeshBuilder`, all ten `MeshHelper` operations, and a game's own processor building a scene that
reaches an `.xnb` CNA reads back. **Phase 9 is done** (`XNAPP-160`, `161`), with a fourth oracle at
`tools/xna-pipeline-oracle/audio/`. **Five of the ten importers are done**: `XmlImporter` (`XNAPP-230`), `WavImporter` (`XNAPP-200`),
`EffectImporter` (`XNAPP-190`), `FontDescriptionImporter` (`XNAPP-180`, with `181`) and
`TextureImporter` (`XNAPP-166`, whose DDS half is `XNAPP-165`). The five left need something this
build does not have: an MP3 decoder (`XNAPP-201`), a WMA one (`XNAPP-202`), a WMV one (Phase 14),
and the two modelling formats (Phases 15 and 16). The next phases need their measurements before their code -- extend
`tools/xna-pipeline-oracle/graphics/GraphicsContentOracle.cs` the way the texture side was
measured. The intermediate serializer is
verified byte for byte against `tests/reference/xna40/intermediate/`; extend the oracle
(`tools/xna-pipeline-oracle/intermediate/run-intermediate-oracle.sh`) before asserting anything
about the format that the corpus does not show. sharp-runtime (`next`, sibling checkout
`sharp-runtimenext`) carries the XML fixes this phase needed; another session works in that
checkout concurrently, so stage only your own hunks there. The nine texture extensions are `IMPLEMENTED+TESTED`. `.x` and `.fbx` are now registered
canonically (`modules/content-pipeline/src/XnaModelSourceContentPipeline.cpp`) and want only the
target leg; `.xml` is the one source extension still with no canonical importer at all. `.wmv` resolves to
`CNA.VideoImporter` (it was missing from that importer's extension list, the same defect the four
texture formats had) and the canonical route now reads the frame metadata from the file:
`cna_content` has no decoder and must not grow one, so `VideoImporter` takes a
`VideoMetadataProbe` from whoever registers it and `cna_content_pipeline` supplies one over
`BuildTimeMedia::ProbeVideo`. A parameter still overrides what the probe read, a build with no
decoder behaves exactly as before, and a file the decoder cannot open is refused with a sentence
that says so rather than one naming a missing parameter.
Seventeen of the eighteen extensions are `IMPLEMENTED+TESTED`. The one that is not is `.xml`, and
it is not a coding gap: 1466 of the sample corpus's `.xml` assets declare **game-defined** types
(`RolePlayingGameData.Armor`, `MovipaLibrary.LayoutInfo`, `Particle3DSample.ParticleSettings`), which
is what the extension is *for* -- a game's own content types, built through a game's own pipeline
assembly. Reaching those is `XNAPP-260`, not a built-in route. What a built-in route could cover is
the handful the framework itself defines (`Graphics:NodeContent`, `Graphics:MeshContent`,
`System.String[]`, `System.Collections.Generic.List[...]`), and it would be XNB-only, because CNB has
no schema for an arbitrary XNA object. Next: that decision,
then `XNAPP-182` (the font atlas differential, which needs a font registered in the Wine prefix),
`XNAPP-191`/`192` (the effect compiler comparison), and `XNAPP-201`/`202` and Phase 14, each of
which waits on a decoder this build does not have -- measure XNA's answer first and record
`EXTERNAL_BLOCKED` with the exact reason where none can be reached. The owner asked for
continuous commits and pushes (2026-09-05), and on the same day asked that unnecessary disk writes
be avoided: build the one target a change needs (`ninja -C cmake-build-debug -j3
CnaContentPipelineTests`) rather than the whole configuration, and never reconfigure or clean.
The build directory is `cmake-build-debug/`, reused rather than reconfigured; the oracles
regenerate with the `run-*-oracle.sh` script beside each driver. **The owner's build rules
changed on 2026-09-06**: there is exactly one physical ccache, `~/.cache/ccache` (the
`/rv/cnaccache` symlink points at it and is deliberately not used as the path), with
`CCACHE_BASEDIR=/rv`, and this tree's compiler launcher now carries both -- it had been
setting a basedir but no cache directory, so every build was landing in the split
`~/.cache/ccache` at a 75 % miss rate. Build parallelism is not capped; memory is the
constraint. Nothing is built under `/tmp` or the session scratchpad, and no build directory
outside the closed list was created.
