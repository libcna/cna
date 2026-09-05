# XNA 4.0 Content Pipeline parity report

> **Generated** by `tools/xna-pipeline-oracle/parity_report.py` from
> `tests/reference/xna40/content-pipeline-api.json` (the denominator, read from the genuine
> XNA Game Studio 4.0 Refresh assemblies) and `tests/reference/xna40/content-pipeline-parity-map.json`
> (CNA's answer). Do not edit by hand; edit the map and regenerate. Task log and decisions:
> `plans/plan_xnapipeline_parity.md`.

## 1. Coverage summary

| Quantity | Implemented (EXACT + SEMANTIC + HOST_SUBSTITUTION) | EXTERNAL_BLOCKED | MISSING |
|---|---|---:|---:|
| public/protected types | 0/128 (0.0%) | 0 | 128 |
| public/protected members | 3/705 (0.4%) | 0 | 702 |
| enum values | 0/27 (0.0%) | 0 | 27 |
| built-in importers | 0/10 (0.0%) | 0 | 10 |
| built-in processors | 0/12 (0.0%) | 0 | 12 |
| processor properties | 0/47 (0.0%) | 0 | 47 |

Status vocabulary: EXACT_EQUIVALENT, SEMANTIC_EQUIVALENT (spelling differs, capability identical; note says how),
HOST_SUBSTITUTION (Microsoft-host mechanism replaced; note says how), EXTERNAL_BLOCKED (note names the
unavailable component), MISSING. Type status by value: EXACT_EQUIVALENT 0, SEMANTIC_EQUIVALENT 0, HOST_SUBSTITUTION 0, EXTERNAL_BLOCKED 0, MISSING 128.
Member status by value: EXACT_EQUIVALENT 0, SEMANTIC_EQUIVALENT 0, HOST_SUBSTITUTION 3, EXTERNAL_BLOCKED 0, MISSING 702.

Rules applied mechanically: 3 delegate plumbing members are listed in section 7 and not counted; 3 exception
serialization members are HOST_SUBSTITUTION by rule (System.Runtime.Serialization has no C++ counterpart).

## 2. Types by namespace

| Namespace | Types | Implemented | Blocked | Missing |
|---|---:|---:|---:|---:|
| `Microsoft.Xna.Framework.Content.Pipeline.Audio` | 5 | 0 | 0 | 5 |
| `Microsoft.Xna.Framework.Content.Pipeline` | 32 | 0 | 0 | 32 |
| `Microsoft.Xna.Framework.Content.Pipeline.Graphics` | 47 | 0 | 0 | 47 |
| `Microsoft.Xna.Framework.Content.Pipeline.Processors` | 28 | 0 | 0 | 28 |
| `Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler` | 5 | 0 | 0 | 5 |
| `Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate` | 7 | 0 | 0 | 7 |
| `Microsoft.Xna.Framework.Content.Pipeline.Tasks` | 4 | 0 | 0 | 4 |

## 3. Type matrix

| Namespace | XNA type | Kind | Status | CNA type | Note |
|---|---|---|---|---|---|
| `….Audio` | `AudioContent` | class | MISSING |  |  |
| `….Audio` | `AudioFileType` | enum | MISSING |  |  |
| `….Audio` | `AudioFormat` | class | MISSING |  |  |
| `….Audio` | `ConversionFormat` | enum | MISSING |  |  |
| `….Audio` | `ConversionQuality` | enum | MISSING |  |  |
| `…` | `ChildCollection<TParent, TChild>` | class | MISSING |  |  |
| `…` | `ContentBuildLogger` | class | MISSING |  |  |
| `…` | `ContentIdentity` | class | MISSING |  |  |
| `…` | `ContentImporterAttribute` | class | MISSING |  |  |
| `…` | `ContentImporterContext` | class | MISSING |  |  |
| `…` | `ContentImporter<T>` | class | MISSING |  |  |
| `…` | `ContentItem` | class | MISSING |  |  |
| `…` | `ContentProcessorAttribute` | class | MISSING |  |  |
| `…` | `ContentProcessorContext` | class | MISSING |  |  |
| `…` | `ContentProcessor<TInput, TOutput>` | class | MISSING |  |  |
| `…` | `EffectImporter` | class | MISSING |  |  |
| `…` | `ExternalReference<T>` | class | MISSING |  |  |
| `…` | `FbxImporter` | class | MISSING |  |  |
| `…` | `FontDescriptionImporter` | class | MISSING |  |  |
| `….Graphics` | `AlphaTestMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `AnimationChannel` | class | MISSING |  |  |
| `….Graphics` | `AnimationChannelDictionary` | class | MISSING |  |  |
| `….Graphics` | `AnimationContent` | class | MISSING |  |  |
| `….Graphics` | `AnimationContentDictionary` | class | MISSING |  |  |
| `….Graphics` | `AnimationKeyframe` | class | MISSING |  |  |
| `….Graphics` | `BasicMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `BitmapContent` | class | MISSING |  |  |
| `….Graphics` | `BoneContent` | class | MISSING |  |  |
| `….Graphics` | `BoneWeight` | struct | MISSING |  |  |
| `….Graphics` | `BoneWeightCollection` | class | MISSING |  |  |
| `….Graphics` | `DualTextureMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `Dxt1BitmapContent` | class | MISSING |  |  |
| `….Graphics` | `Dxt3BitmapContent` | class | MISSING |  |  |
| `….Graphics` | `Dxt5BitmapContent` | class | MISSING |  |  |
| `….Graphics` | `DxtBitmapContent` | class | MISSING |  |  |
| `….Graphics` | `EffectContent` | class | MISSING |  |  |
| `….Graphics` | `EffectMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `EnvironmentMapMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `FontDescription` | class | MISSING |  |  |
| `….Graphics` | `FontDescriptionStyle` | enum | MISSING |  |  |
| `….Graphics` | `GeometryContent` | class | MISSING |  |  |
| `….Graphics` | `GeometryContentCollection` | class | MISSING |  |  |
| `….Graphics` | `IndexCollection` | class | MISSING |  |  |
| `….Graphics` | `IndirectPositionCollection` | class | MISSING |  |  |
| `….Graphics` | `MaterialContent` | class | MISSING |  |  |
| `….Graphics` | `MeshBuilder` | class | MISSING |  |  |
| `….Graphics` | `MeshContent` | class | MISSING |  |  |
| `….Graphics` | `MeshHelper` | class | MISSING |  |  |
| `….Graphics` | `MipmapChain` | class | MISSING |  |  |
| `….Graphics` | `MipmapChainCollection` | class | MISSING |  |  |
| `….Graphics` | `NodeContent` | class | MISSING |  |  |
| `….Graphics` | `NodeContentCollection` | class | MISSING |  |  |
| `….Graphics` | `PixelBitmapContent<T>` | class | MISSING |  |  |
| `….Graphics` | `PositionCollection` | class | MISSING |  |  |
| `….Graphics` | `SkinnedMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `Texture2DContent` | class | MISSING |  |  |
| `….Graphics` | `Texture3DContent` | class | MISSING |  |  |
| `….Graphics` | `TextureContent` | class | MISSING |  |  |
| `….Graphics` | `TextureCubeContent` | class | MISSING |  |  |
| `….Graphics` | `TextureReferenceDictionary` | class | MISSING |  |  |
| `….Graphics` | `VectorConverter` | class | MISSING |  |  |
| `….Graphics` | `VertexChannel` | class | MISSING |  |  |
| `….Graphics` | `VertexChannelCollection` | class | MISSING |  |  |
| `….Graphics` | `VertexChannelNames` | class | MISSING |  |  |
| `….Graphics` | `VertexChannel<T>` | class | MISSING |  |  |
| `….Graphics` | `VertexContent` | class | MISSING |  |  |
| `…` | `IContentImporter` | interface | MISSING |  |  |
| `…` | `IContentProcessor` | interface | MISSING |  |  |
| `…` | `InvalidContentException` | class | MISSING |  |  |
| `…` | `Mp3Importer` | class | MISSING |  |  |
| `…` | `NamedValueDictionary<T>` | class | MISSING |  |  |
| `…` | `OpaqueDataDictionary` | class | MISSING |  |  |
| `…` | `PipelineComponentScanner` | class | MISSING |  |  |
| `…` | `PipelineException` | class | MISSING |  |  |
| `…` | `ProcessorParameter` | class | MISSING |  |  |
| `…` | `ProcessorParameterCollection` | class | MISSING |  |  |
| `….Processors` | `CompiledEffectContent` | class | MISSING |  |  |
| `….Processors` | `EffectProcessor` | class | MISSING |  |  |
| `….Processors` | `EffectProcessorDebugMode` | enum | MISSING |  |  |
| `….Processors` | `FontDescriptionProcessor` | class | MISSING |  |  |
| `….Processors` | `FontTextureProcessor` | class | MISSING |  |  |
| `….Processors` | `MaterialProcessor` | class | MISSING |  |  |
| `….Processors` | `MaterialProcessorDefaultEffect` | enum | MISSING |  |  |
| `….Processors` | `ModelBoneContent` | class | MISSING |  |  |
| `….Processors` | `ModelBoneContentCollection` | class | MISSING |  |  |
| `….Processors` | `ModelContent` | class | MISSING |  |  |
| `….Processors` | `ModelMeshContent` | class | MISSING |  |  |
| `….Processors` | `ModelMeshContentCollection` | class | MISSING |  |  |
| `….Processors` | `ModelMeshPartContent` | class | MISSING |  |  |
| `….Processors` | `ModelMeshPartContentCollection` | class | MISSING |  |  |
| `….Processors` | `ModelProcessor` | class | MISSING |  |  |
| `….Processors` | `ModelTextureProcessor` | class | MISSING |  |  |
| `….Processors` | `PassThroughProcessor` | class | MISSING |  |  |
| `….Processors` | `SongContent` | class | MISSING |  |  |
| `….Processors` | `SongProcessor` | class | MISSING |  |  |
| `….Processors` | `SoundEffectContent` | class | MISSING |  |  |
| `….Processors` | `SoundEffectProcessor` | class | MISSING |  |  |
| `….Processors` | `SpriteFontContent` | class | MISSING |  |  |
| `….Processors` | `SpriteTextureProcessor` | class | MISSING |  |  |
| `….Processors` | `TextureProcessor` | class | MISSING |  |  |
| `….Processors` | `TextureProcessorOutputFormat` | enum | MISSING |  |  |
| `….Processors` | `VertexBufferContent` | class | MISSING |  |  |
| `….Processors` | `VertexDeclarationContent` | class | MISSING |  |  |
| `….Processors` | `VideoProcessor` | class | MISSING |  |  |
| `….Serialization.Compiler` | `ContentCompiler` | class | MISSING |  |  |
| `….Serialization.Compiler` | `ContentTypeWriter` | class | MISSING |  |  |
| `….Serialization.Compiler` | `ContentTypeWriterAttribute` | class | MISSING |  |  |
| `….Serialization.Compiler` | `ContentTypeWriter<T>` | class | MISSING |  |  |
| `….Serialization.Compiler` | `ContentWriter` | class | MISSING |  |  |
| `….Serialization.Intermediate` | `ContentTypeSerializer` | class | MISSING |  |  |
| `….Serialization.Intermediate` | `ContentTypeSerializer+ChildCallback` | delegate | MISSING |  |  |
| `….Serialization.Intermediate` | `ContentTypeSerializerAttribute` | class | MISSING |  |  |
| `….Serialization.Intermediate` | `ContentTypeSerializer<T>` | class | MISSING |  |  |
| `….Serialization.Intermediate` | `IntermediateReader` | class | MISSING |  |  |
| `….Serialization.Intermediate` | `IntermediateSerializer` | class | MISSING |  |  |
| `….Serialization.Intermediate` | `IntermediateWriter` | class | MISSING |  |  |
| `…` | `TargetPlatform` | enum | MISSING |  |  |
| `….Tasks` | `BuildContent` | class | MISSING |  |  |
| `….Tasks` | `BuildXact` | class | MISSING |  |  |
| `….Tasks` | `CleanContent` | class | MISSING |  |  |
| `….Tasks` | `GetLastOutputs` | class | MISSING |  |  |
| `…` | `TextureImporter` | class | MISSING |  |  |
| `…` | `VideoContent` | class | MISSING |  |  |
| `…` | `WavImporter` | class | MISSING |  |  |
| `…` | `WmaImporter` | class | MISSING |  |  |
| `…` | `WmvImporter` | class | MISSING |  |  |
| `…` | `XImporter` | class | MISSING |  |  |
| `…` | `XmlImporter` | class | MISSING |  |  |

## 4. Importers

| XNA importer | Extensions | Default processor | Status | CNA type | Note |
|---|---|---|---|---|---|
| `EffectImporter` | .fx | `EffectProcessor` | MISSING |  |  |
| `FbxImporter` | .fbx | `ModelProcessor` | MISSING |  |  |
| `FontDescriptionImporter` | .spritefont | `FontDescriptionProcessor` | MISSING |  |  |
| `Mp3Importer` | .mp3 | `SongProcessor` | MISSING |  |  |
| `TextureImporter` | .bmp, .dds, .dib, .hdr, .jpg, .pfm, .png, .ppm, .tga | `SpriteTextureProcessor` | MISSING |  |  |
| `WavImporter` | .wav | `SoundEffectProcessor` | MISSING |  |  |
| `WmaImporter` | .wma | `SongProcessor` | MISSING |  |  |
| `WmvImporter` | .wmv | `VideoProcessor` | MISSING |  |  |
| `XImporter` | .x | `ModelProcessor` | MISSING |  |  |
| `XmlImporter` | .xml | `-` | MISSING |  |  |

## 5. Processors and properties

| XNA processor | Input | Output | Properties | Status | CNA type |
|---|---|---|---:|---|---|
| `EffectProcessor` | `EffectContent` | `CompiledEffectContent` | 2 | MISSING |  |
| `FontDescriptionProcessor` | `FontDescription` | `SpriteFontContent` | 0 | MISSING |  |
| `FontTextureProcessor` | `Texture2DContent` | `SpriteFontContent` | 3 | MISSING |  |
| `MaterialProcessor` | `MaterialContent` | `MaterialContent` | 7 | MISSING |  |
| `ModelProcessor` | `NodeContent` | `ModelContent` | 14 | MISSING |  |
| `ModelTextureProcessor` | `TextureContent` | `TextureContent` | 6 | MISSING |  |
| `PassThroughProcessor` | `Object` | `Object` | 0 | MISSING |  |
| `SongProcessor` | `AudioContent` | `SongContent` | 1 | MISSING |  |
| `SoundEffectProcessor` | `AudioContent` | `SoundEffectContent` | 1 | MISSING |  |
| `SpriteTextureProcessor` | `TextureContent` | `TextureContent` | 6 | MISSING |  |
| `TextureProcessor` | `TextureContent` | `TextureContent` | 6 | MISSING |  |
| `VideoProcessor` | `VideoContent` | `VideoContent` | 1 | MISSING |  |

| Processor | Property | Type | XNA default (black-box) | Status | CNA |
|---|---|---|---|---|---|
| `EffectProcessor` | `DebugMode` | `EffectProcessorDebugMode` | `EffectProcessorDebugMode.Auto` | MISSING |  |
| `EffectProcessor` | `Defines` | `String` | `None` | MISSING |  |
| `FontTextureProcessor` | `FirstCharacter` | `Char` | `'\u0020' (32)` | MISSING |  |
| `FontTextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | MISSING |  |
| `FontTextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | MISSING |  |
| `MaterialProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | MISSING |  |
| `MaterialProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | MISSING |  |
| `MaterialProcessor` | `DefaultEffect` | `MaterialProcessorDefaultEffect` | `MaterialProcessorDefaultEffect.BasicEffect` | MISSING |  |
| `MaterialProcessor` | `GenerateMipmaps` | `Boolean` | `True` | MISSING |  |
| `MaterialProcessor` | `PremultiplyTextureAlpha` | `Boolean` | `True` | MISSING |  |
| `MaterialProcessor` | `ResizeTexturesToPowerOfTwo` | `Boolean` | `False` | MISSING |  |
| `MaterialProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | MISSING |  |
| `ModelProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | MISSING |  |
| `ModelProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | MISSING |  |
| `ModelProcessor` | `DefaultEffect` | `MaterialProcessorDefaultEffect` | `MaterialProcessorDefaultEffect.BasicEffect` | MISSING |  |
| `ModelProcessor` | `GenerateMipmaps` | `Boolean` | `True` | MISSING |  |
| `ModelProcessor` | `GenerateTangentFrames` | `Boolean` | `False` | MISSING |  |
| `ModelProcessor` | `PremultiplyTextureAlpha` | `Boolean` | `True` | MISSING |  |
| `ModelProcessor` | `PremultiplyVertexColors` | `Boolean` | `True` | MISSING |  |
| `ModelProcessor` | `ResizeTexturesToPowerOfTwo` | `Boolean` | `False` | MISSING |  |
| `ModelProcessor` | `RotationX` | `Single` | `0` | MISSING |  |
| `ModelProcessor` | `RotationY` | `Single` | `0` | MISSING |  |
| `ModelProcessor` | `RotationZ` | `Single` | `0` | MISSING |  |
| `ModelProcessor` | `Scale` | `Single` | `1` | MISSING |  |
| `ModelProcessor` | `SwapWindingOrder` | `Boolean` | `False` | MISSING |  |
| `ModelProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | MISSING |  |
| `ModelTextureProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | MISSING |  |
| `ModelTextureProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | MISSING |  |
| `ModelTextureProcessor` | `GenerateMipmaps` | `Boolean` | `True` | MISSING |  |
| `ModelTextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | MISSING |  |
| `ModelTextureProcessor` | `ResizeToPowerOfTwo` | `Boolean` | `False` | MISSING |  |
| `ModelTextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | MISSING |  |
| `SongProcessor` | `Quality` | `ConversionQuality` | `ConversionQuality.Best` | MISSING |  |
| `SoundEffectProcessor` | `Quality` | `ConversionQuality` | `ConversionQuality.Best` | MISSING |  |
| `SpriteTextureProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | MISSING |  |
| `SpriteTextureProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | MISSING |  |
| `SpriteTextureProcessor` | `GenerateMipmaps` | `Boolean` | `False` | MISSING |  |
| `SpriteTextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | MISSING |  |
| `SpriteTextureProcessor` | `ResizeToPowerOfTwo` | `Boolean` | `False` | MISSING |  |
| `SpriteTextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | MISSING |  |
| `TextureProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | MISSING |  |
| `TextureProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | MISSING |  |
| `TextureProcessor` | `GenerateMipmaps` | `Boolean` | `False` | MISSING |  |
| `TextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | MISSING |  |
| `TextureProcessor` | `ResizeToPowerOfTwo` | `Boolean` | `False` | MISSING |  |
| `TextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | MISSING |  |
| `VideoProcessor` | `VideoSoundtrackType` | `VideoSoundtrackType` | `VideoSoundtrackType.Music` | MISSING |  |

## 7. Members

| XNA type | Kind | Signature | Status | CNA | Note |
|---|---|---|---|---|---|
| `AudioContent` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioFileType)` | MISSING |  |  |
| `AudioContent` | property | `Data` | MISSING |  |  |
| `AudioContent` | property | `Duration` | MISSING |  |  |
| `AudioContent` | property | `FileName` | MISSING |  |  |
| `AudioContent` | property | `FileType` | MISSING |  |  |
| `AudioContent` | property | `Format` | MISSING |  |  |
| `AudioContent` | property | `LoopLength` | MISSING |  |  |
| `AudioContent` | property | `LoopStart` | MISSING |  |  |
| `AudioContent` | method | `ConvertFormat(Microsoft.Xna.Framework.Content.Pipeline.Audio.ConversionFormat, Microsoft.Xna.Framework.Content.Pipeline.Audio.ConversionQuality, System.String)` | MISSING |  |  |
| `AudioContent` | method | `Dispose()` | MISSING |  |  |
| `AudioContent` | method | `Finalize()` | MISSING |  |  |
| `AudioFileType` | enum value | `Wav = 0` | MISSING |  |  |
| `AudioFileType` | enum value | `Mp3 = 1` | MISSING |  |  |
| `AudioFileType` | enum value | `Wma = 2` | MISSING |  |  |
| `AudioFormat` | property | `AverageBytesPerSecond` | MISSING |  |  |
| `AudioFormat` | property | `BitsPerSample` | MISSING |  |  |
| `AudioFormat` | property | `BlockAlign` | MISSING |  |  |
| `AudioFormat` | property | `ChannelCount` | MISSING |  |  |
| `AudioFormat` | property | `Format` | MISSING |  |  |
| `AudioFormat` | property | `NativeWaveFormat` | MISSING |  |  |
| `AudioFormat` | property | `SampleRate` | MISSING |  |  |
| `ConversionFormat` | enum value | `Pcm = 0` | MISSING |  |  |
| `ConversionFormat` | enum value | `Adpcm = 1` | MISSING |  |  |
| `ConversionFormat` | enum value | `WindowsMedia = 2` | MISSING |  |  |
| `ConversionFormat` | enum value | `Xma = 3` | MISSING |  |  |
| `ConversionQuality` | enum value | `Low = 0` | MISSING |  |  |
| `ConversionQuality` | enum value | `Medium = 1` | MISSING |  |  |
| `ConversionQuality` | enum value | `Best = 2` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | constructor | `.ctor(TParent)` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | method | `ClearItems()` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | method | `GetParent(TChild)` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | method | `InsertItem(System.Int32, TChild)` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | method | `RemoveItem(System.Int32)` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | method | `SetItem(System.Int32, TChild)` | MISSING |  |  |
| `ChildCollection<TParent, TChild>` | method | `SetParent(TChild, TParent)` | MISSING |  |  |
| `ContentBuildLogger` | constructor | `.ctor()` | MISSING |  |  |
| `ContentBuildLogger` | property | `LoggerRootDirectory` | MISSING |  |  |
| `ContentBuildLogger` | method | `GetCurrentFilename(Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | MISSING |  |  |
| `ContentBuildLogger` | method | `LogImportantMessage(System.String, System.Object[])` | MISSING |  |  |
| `ContentBuildLogger` | method | `LogMessage(System.String, System.Object[])` | MISSING |  |  |
| `ContentBuildLogger` | method | `LogWarning(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity, System.String, System.Object[])` | MISSING |  |  |
| `ContentBuildLogger` | method | `PopFile()` | MISSING |  |  |
| `ContentBuildLogger` | method | `PushFile(System.String)` | MISSING |  |  |
| `ContentIdentity` | constructor | `.ctor()` | MISSING |  |  |
| `ContentIdentity` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `ContentIdentity` | constructor | `.ctor(System.String, System.String)` | MISSING |  |  |
| `ContentIdentity` | constructor | `.ctor(System.String, System.String, System.String)` | MISSING |  |  |
| `ContentIdentity` | property | `FragmentIdentifier` | MISSING |  |  |
| `ContentIdentity` | property | `SourceFilename` | MISSING |  |  |
| `ContentIdentity` | property | `SourceTool` | MISSING |  |  |
| `ContentImporterAttribute` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `ContentImporterAttribute` | constructor | `.ctor(System.String[])` | MISSING |  |  |
| `ContentImporterAttribute` | property | `CacheImportedData` | MISSING |  |  |
| `ContentImporterAttribute` | property | `DefaultProcessor` | MISSING |  |  |
| `ContentImporterAttribute` | property | `DisplayName` | MISSING |  |  |
| `ContentImporterAttribute` | property | `FileExtensions` | MISSING |  |  |
| `ContentImporterContext` | constructor | `.ctor()` | MISSING |  |  |
| `ContentImporterContext` | property | `IntermediateDirectory` | MISSING |  |  |
| `ContentImporterContext` | property | `Logger` | MISSING |  |  |
| `ContentImporterContext` | property | `OutputDirectory` | MISSING |  |  |
| `ContentImporterContext` | method | `AddDependency(System.String)` | MISSING |  |  |
| `ContentImporter<T>` | constructor | `.ctor()` | MISSING |  |  |
| `ContentImporter<T>` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `ContentItem` | constructor | `.ctor()` | MISSING |  |  |
| `ContentItem` | property | `Identity` | MISSING |  |  |
| `ContentItem` | property | `Name` | MISSING |  |  |
| `ContentItem` | property | `OpaqueData` | MISSING |  |  |
| `ContentProcessorAttribute` | constructor | `.ctor()` | MISSING |  |  |
| `ContentProcessorAttribute` | property | `DisplayName` | MISSING |  |  |
| `ContentProcessorContext` | constructor | `.ctor()` | MISSING |  |  |
| `ContentProcessorContext` | property | `BuildConfiguration` | MISSING |  |  |
| `ContentProcessorContext` | property | `IntermediateDirectory` | MISSING |  |  |
| `ContentProcessorContext` | property | `Logger` | MISSING |  |  |
| `ContentProcessorContext` | property | `OutputDirectory` | MISSING |  |  |
| `ContentProcessorContext` | property | `OutputFilename` | MISSING |  |  |
| `ContentProcessorContext` | property | `Parameters` | MISSING |  |  |
| `ContentProcessorContext` | property | `TargetPlatform` | MISSING |  |  |
| `ContentProcessorContext` | property | `TargetProfile` | MISSING |  |  |
| `ContentProcessorContext` | method | `AddDependency(System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `AddOutputFile(System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `BuildAndLoadAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `BuildAndLoadAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String, Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary, System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `BuildAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `BuildAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String, Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary, System.String, System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `Convert<TInput, TOutput>(TInput, System.String)` | MISSING |  |  |
| `ContentProcessorContext` | method | `Convert<TInput, TOutput>(TInput, System.String, Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary)` | MISSING |  |  |
| `ContentProcessor<TInput, TOutput>` | constructor | `.ctor()` | MISSING |  |  |
| `ContentProcessor<TInput, TOutput>` | method | `Process(TInput, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `EffectImporter` | constructor | `.ctor()` | MISSING |  |  |
| `EffectImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `ExternalReference<T>` | constructor | `.ctor()` | MISSING |  |  |
| `ExternalReference<T>` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `ExternalReference<T>` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | MISSING |  |  |
| `ExternalReference<T>` | property | `Filename` | MISSING |  |  |
| `FbxImporter` | constructor | `.ctor()` | MISSING |  |  |
| `FbxImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `FontDescriptionImporter` | constructor | `.ctor()` | MISSING |  |  |
| `FontDescriptionImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `AlphaTestMaterialContent` | constant | `AlphaFunctionKey` | MISSING |  |  |
| `AlphaTestMaterialContent` | constant | `AlphaKey` | MISSING |  |  |
| `AlphaTestMaterialContent` | constant | `DiffuseColorKey` | MISSING |  |  |
| `AlphaTestMaterialContent` | constant | `ReferenceAlphaKey` | MISSING |  |  |
| `AlphaTestMaterialContent` | constant | `TextureKey` | MISSING |  |  |
| `AlphaTestMaterialContent` | constant | `VertexColorEnabledKey` | MISSING |  |  |
| `AlphaTestMaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `AlphaTestMaterialContent` | property | `Alpha` | MISSING |  |  |
| `AlphaTestMaterialContent` | property | `AlphaFunction` | MISSING |  |  |
| `AlphaTestMaterialContent` | property | `DiffuseColor` | MISSING |  |  |
| `AlphaTestMaterialContent` | property | `ReferenceAlpha` | MISSING |  |  |
| `AlphaTestMaterialContent` | property | `Texture` | MISSING |  |  |
| `AlphaTestMaterialContent` | property | `VertexColorEnabled` | MISSING |  |  |
| `AnimationChannel` | constructor | `.ctor()` | MISSING |  |  |
| `AnimationChannel` | property | `Count` | MISSING |  |  |
| `AnimationChannel` | indexer | `Item[System.Int32]` | MISSING |  |  |
| `AnimationChannel` | method | `Add(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | MISSING |  |  |
| `AnimationChannel` | method | `Clear()` | MISSING |  |  |
| `AnimationChannel` | method | `Contains(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | MISSING |  |  |
| `AnimationChannel` | method | `GetEnumerator()` | MISSING |  |  |
| `AnimationChannel` | method | `IndexOf(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | MISSING |  |  |
| `AnimationChannel` | method | `Remove(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | MISSING |  |  |
| `AnimationChannel` | method | `RemoveAt(System.Int32)` | MISSING |  |  |
| `AnimationChannelDictionary` | constructor | `.ctor()` | MISSING |  |  |
| `AnimationContent` | constructor | `.ctor()` | MISSING |  |  |
| `AnimationContent` | property | `Channels` | MISSING |  |  |
| `AnimationContent` | property | `Duration` | MISSING |  |  |
| `AnimationContentDictionary` | constructor | `.ctor()` | MISSING |  |  |
| `AnimationKeyframe` | constructor | `.ctor(System.TimeSpan, Microsoft.Xna.Framework.Matrix)` | MISSING |  |  |
| `AnimationKeyframe` | property | `Time` | MISSING |  |  |
| `AnimationKeyframe` | property | `Transform` | MISSING |  |  |
| `AnimationKeyframe` | method | `CompareTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | MISSING |  |  |
| `BasicMaterialContent` | constant | `AlphaKey` | MISSING |  |  |
| `BasicMaterialContent` | constant | `DiffuseColorKey` | MISSING |  |  |
| `BasicMaterialContent` | constant | `EmissiveColorKey` | MISSING |  |  |
| `BasicMaterialContent` | constant | `SpecularColorKey` | MISSING |  |  |
| `BasicMaterialContent` | constant | `SpecularPowerKey` | MISSING |  |  |
| `BasicMaterialContent` | constant | `TextureKey` | MISSING |  |  |
| `BasicMaterialContent` | constant | `VertexColorEnabledKey` | MISSING |  |  |
| `BasicMaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `BasicMaterialContent` | property | `Alpha` | MISSING |  |  |
| `BasicMaterialContent` | property | `DiffuseColor` | MISSING |  |  |
| `BasicMaterialContent` | property | `EmissiveColor` | MISSING |  |  |
| `BasicMaterialContent` | property | `SpecularColor` | MISSING |  |  |
| `BasicMaterialContent` | property | `SpecularPower` | MISSING |  |  |
| `BasicMaterialContent` | property | `Texture` | MISSING |  |  |
| `BasicMaterialContent` | property | `VertexColorEnabled` | MISSING |  |  |
| `BitmapContent` | constructor | `.ctor()` | MISSING |  |  |
| `BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | MISSING |  |  |
| `BitmapContent` | property | `Height` | MISSING |  |  |
| `BitmapContent` | property | `Width` | MISSING |  |  |
| `BitmapContent` | method | `Copy(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | MISSING |  |  |
| `BitmapContent` | method | `Copy(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `BitmapContent` | method | `GetPixelData()` | MISSING |  |  |
| `BitmapContent` | method | `SetPixelData(System.Byte[])` | MISSING |  |  |
| `BitmapContent` | method | `ToString()` | MISSING |  |  |
| `BitmapContent` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `BitmapContent` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | MISSING |  |  |
| `BitmapContent` | method | `ValidateCopyArguments(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `BoneContent` | constructor | `.ctor()` | MISSING |  |  |
| `BoneWeight` | constructor | `.ctor(System.String, System.Single)` | MISSING |  |  |
| `BoneWeight` | property | `BoneName` | MISSING |  |  |
| `BoneWeight` | property | `Weight` | MISSING |  |  |
| `BoneWeightCollection` | constructor | `.ctor()` | MISSING |  |  |
| `BoneWeightCollection` | method | `NormalizeWeights()` | MISSING |  |  |
| `BoneWeightCollection` | method | `NormalizeWeights(System.Int32)` | MISSING |  |  |
| `DualTextureMaterialContent` | constant | `AlphaKey` | MISSING |  |  |
| `DualTextureMaterialContent` | constant | `DiffuseColorKey` | MISSING |  |  |
| `DualTextureMaterialContent` | constant | `Texture2Key` | MISSING |  |  |
| `DualTextureMaterialContent` | constant | `TextureKey` | MISSING |  |  |
| `DualTextureMaterialContent` | constant | `VertexColorEnabledKey` | MISSING |  |  |
| `DualTextureMaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `DualTextureMaterialContent` | property | `Alpha` | MISSING |  |  |
| `DualTextureMaterialContent` | property | `DiffuseColor` | MISSING |  |  |
| `DualTextureMaterialContent` | property | `Texture` | MISSING |  |  |
| `DualTextureMaterialContent` | property | `Texture2` | MISSING |  |  |
| `DualTextureMaterialContent` | property | `VertexColorEnabled` | MISSING |  |  |
| `Dxt1BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | MISSING |  |  |
| `Dxt1BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | MISSING |  |  |
| `Dxt3BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | MISSING |  |  |
| `Dxt3BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | MISSING |  |  |
| `Dxt5BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | MISSING |  |  |
| `Dxt5BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | MISSING |  |  |
| `DxtBitmapContent` | constructor | `.ctor(System.Int32)` | MISSING |  |  |
| `DxtBitmapContent` | constructor | `.ctor(System.Int32, System.Int32, System.Int32)` | MISSING |  |  |
| `DxtBitmapContent` | method | `GetPixelData()` | MISSING |  |  |
| `DxtBitmapContent` | method | `SetPixelData(System.Byte[])` | MISSING |  |  |
| `DxtBitmapContent` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `DxtBitmapContent` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `EffectContent` | constructor | `.ctor()` | MISSING |  |  |
| `EffectContent` | property | `EffectCode` | MISSING |  |  |
| `EffectMaterialContent` | constant | `CompiledEffectKey` | MISSING |  |  |
| `EffectMaterialContent` | constant | `EffectKey` | MISSING |  |  |
| `EffectMaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `EffectMaterialContent` | property | `CompiledEffect` | MISSING |  |  |
| `EffectMaterialContent` | property | `Effect` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `AlphaKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `DiffuseColorKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `EmissiveColorKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `EnvironmentMapAmountKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `EnvironmentMapKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `EnvironmentMapSpecularKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `FresnelFactorKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constant | `TextureKey` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `Alpha` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `DiffuseColor` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `EmissiveColor` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `EnvironmentMap` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `EnvironmentMapAmount` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `EnvironmentMapSpecular` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `FresnelFactor` | MISSING |  |  |
| `EnvironmentMapMaterialContent` | property | `Texture` | MISSING |  |  |
| `FontDescription` | constructor | `.ctor(System.String, System.Single, System.Single)` | MISSING |  |  |
| `FontDescription` | constructor | `.ctor(System.String, System.Single, System.Single, Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescriptionStyle)` | MISSING |  |  |
| `FontDescription` | constructor | `.ctor(System.String, System.Single, System.Single, Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescriptionStyle, System.Boolean)` | MISSING |  |  |
| `FontDescription` | property | `Characters` | MISSING |  |  |
| `FontDescription` | property | `DefaultCharacter` | MISSING |  |  |
| `FontDescription` | property | `FontName` | MISSING |  |  |
| `FontDescription` | property | `Size` | MISSING |  |  |
| `FontDescription` | property | `Spacing` | MISSING |  |  |
| `FontDescription` | property | `Style` | MISSING |  |  |
| `FontDescription` | property | `UseKerning` | MISSING |  |  |
| `FontDescriptionStyle` | enum value | `Regular = 0` | MISSING |  |  |
| `FontDescriptionStyle` | enum value | `Bold = 1` | MISSING |  |  |
| `FontDescriptionStyle` | enum value | `Italic = 2` | MISSING |  |  |
| `GeometryContent` | constructor | `.ctor()` | MISSING |  |  |
| `GeometryContent` | property | `Indices` | MISSING |  |  |
| `GeometryContent` | property | `Material` | MISSING |  |  |
| `GeometryContent` | property | `Parent` | MISSING |  |  |
| `GeometryContent` | property | `Vertices` | MISSING |  |  |
| `GeometryContentCollection` | method | `GetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent)` | MISSING |  |  |
| `GeometryContentCollection` | method | `SetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | MISSING |  |  |
| `IndexCollection` | constructor | `.ctor()` | MISSING |  |  |
| `IndexCollection` | method | `AddRange(System.Collections.Generic.IEnumerable<System.Int32>)` | MISSING |  |  |
| `IndirectPositionCollection` | property | `Count` | MISSING |  |  |
| `IndirectPositionCollection` | indexer | `Item[System.Int32]` | MISSING |  |  |
| `IndirectPositionCollection` | method | `Contains(Microsoft.Xna.Framework.Vector3)` | MISSING |  |  |
| `IndirectPositionCollection` | method | `CopyTo(Microsoft.Xna.Framework.Vector3[], System.Int32)` | MISSING |  |  |
| `IndirectPositionCollection` | method | `GetEnumerator()` | MISSING |  |  |
| `IndirectPositionCollection` | method | `IndexOf(Microsoft.Xna.Framework.Vector3)` | MISSING |  |  |
| `MaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `MaterialContent` | property | `Textures` | MISSING |  |  |
| `MaterialContent` | method | `GetReferenceTypeProperty<T>(System.String)` | MISSING |  |  |
| `MaterialContent` | method | `GetTexture(System.String)` | MISSING |  |  |
| `MaterialContent` | method | `GetValueTypeProperty<T>(System.String)` | MISSING |  |  |
| `MaterialContent` | method | `SetProperty<T>(System.String, T)` | MISSING |  |  |
| `MaterialContent` | method | `SetTexture(System.String, Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent>)` | MISSING |  |  |
| `MeshBuilder` | property | `MergeDuplicatePositions` | MISSING |  |  |
| `MeshBuilder` | property | `MergePositionTolerance` | MISSING |  |  |
| `MeshBuilder` | property | `Name` | MISSING |  |  |
| `MeshBuilder` | property | `SwapWindingOrder` | MISSING |  |  |
| `MeshBuilder` | method | `AddTriangleVertex(System.Int32)` | MISSING |  |  |
| `MeshBuilder` | method | `CreatePosition(Microsoft.Xna.Framework.Vector3)` | MISSING |  |  |
| `MeshBuilder` | method | `CreatePosition(System.Single, System.Single, System.Single)` | MISSING |  |  |
| `MeshBuilder` | method | `CreateVertexChannel<T>(System.String)` | MISSING |  |  |
| `MeshBuilder` | method | `FinishMesh()` | MISSING |  |  |
| `MeshBuilder` | method | `SetMaterial(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent)` | MISSING |  |  |
| `MeshBuilder` | method | `SetOpaqueData(Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary)` | MISSING |  |  |
| `MeshBuilder` | method | `SetVertexChannelData(System.Int32, System.Object)` | MISSING |  |  |
| `MeshBuilder` | method | `StartMesh(System.String)` | MISSING |  |  |
| `MeshContent` | constructor | `.ctor()` | MISSING |  |  |
| `MeshContent` | property | `Geometry` | MISSING |  |  |
| `MeshContent` | property | `Positions` | MISSING |  |  |
| `MeshHelper` | method | `CalculateNormals(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent, System.Boolean)` | MISSING |  |  |
| `MeshHelper` | method | `CalculateTangentFrames(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent, System.String, System.String, System.String)` | MISSING |  |  |
| `MeshHelper` | method | `FindSkeleton(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | MISSING |  |  |
| `MeshHelper` | method | `FlattenSkeleton(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BoneContent)` | MISSING |  |  |
| `MeshHelper` | method | `MergeDuplicatePositions(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent, System.Single)` | MISSING |  |  |
| `MeshHelper` | method | `MergeDuplicateVertices(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent)` | MISSING |  |  |
| `MeshHelper` | method | `MergeDuplicateVertices(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | MISSING |  |  |
| `MeshHelper` | method | `OptimizeForCache(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | MISSING |  |  |
| `MeshHelper` | method | `SwapWindingOrder(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | MISSING |  |  |
| `MeshHelper` | method | `TransformScene(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Matrix)` | MISSING |  |  |
| `MipmapChain` | constructor | `.ctor()` | MISSING |  |  |
| `MipmapChain` | constructor | `.ctor(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | MISSING |  |  |
| `MipmapChain` | method | `InsertItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | MISSING |  |  |
| `MipmapChain` | method | `SetItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | MISSING |  |  |
| `MipmapChain` | operator | `op_Implicit(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | MISSING |  |  |
| `MipmapChainCollection` | method | `ClearItems()` | MISSING |  |  |
| `MipmapChainCollection` | method | `InsertItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain)` | MISSING |  |  |
| `MipmapChainCollection` | method | `RemoveItem(System.Int32)` | MISSING |  |  |
| `MipmapChainCollection` | method | `SetItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain)` | MISSING |  |  |
| `NodeContent` | constructor | `.ctor()` | MISSING |  |  |
| `NodeContent` | property | `AbsoluteTransform` | MISSING |  |  |
| `NodeContent` | property | `Animations` | MISSING |  |  |
| `NodeContent` | property | `Children` | MISSING |  |  |
| `NodeContent` | property | `Parent` | MISSING |  |  |
| `NodeContent` | property | `Transform` | MISSING |  |  |
| `NodeContentCollection` | method | `GetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | MISSING |  |  |
| `NodeContentCollection` | method | `SetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | MISSING |  |  |
| `PixelBitmapContent<T>` | constructor | `.ctor()` | MISSING |  |  |
| `PixelBitmapContent<T>` | constructor | `.ctor(System.Int32, System.Int32)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `GetPixel(System.Int32, System.Int32)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `GetPixelData()` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `GetRow(System.Int32)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `ReplaceColor(T, T)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `SetPixel(System.Int32, System.Int32, T)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `SetPixelData(System.Byte[])` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `ToString()` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | MISSING |  |  |
| `PixelBitmapContent<T>` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | MISSING |  |  |
| `PositionCollection` | constructor | `.ctor()` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `AlphaKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `DiffuseColorKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `EmissiveColorKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `SpecularColorKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `SpecularPowerKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `TextureKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constant | `WeightsPerVertexKey` | MISSING |  |  |
| `SkinnedMaterialContent` | constructor | `.ctor()` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `Alpha` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `DiffuseColor` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `EmissiveColor` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `SpecularColor` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `SpecularPower` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `Texture` | MISSING |  |  |
| `SkinnedMaterialContent` | property | `WeightsPerVertex` | MISSING |  |  |
| `Texture2DContent` | constructor | `.ctor()` | MISSING |  |  |
| `Texture2DContent` | property | `Mipmaps` | MISSING |  |  |
| `Texture2DContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | MISSING |  |  |
| `Texture3DContent` | constructor | `.ctor()` | MISSING |  |  |
| `Texture3DContent` | method | `GenerateMipmaps(System.Boolean)` | MISSING |  |  |
| `Texture3DContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | MISSING |  |  |
| `TextureContent` | constructor | `.ctor(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChainCollection)` | MISSING |  |  |
| `TextureContent` | property | `Faces` | MISSING |  |  |
| `TextureContent` | method | `ConvertBitmapType(System.Type)` | MISSING |  |  |
| `TextureContent` | method | `GenerateMipmaps(System.Boolean)` | MISSING |  |  |
| `TextureContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | MISSING |  |  |
| `TextureCubeContent` | constructor | `.ctor()` | MISSING |  |  |
| `TextureCubeContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | MISSING |  |  |
| `TextureReferenceDictionary` | constructor | `.ctor()` | MISSING |  |  |
| `VectorConverter` | method | `GetConverter<TInput, TOutput>()` | MISSING |  |  |
| `VectorConverter` | method | `TryGetSurfaceFormat(System.Type, out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | MISSING |  |  |
| `VectorConverter` | method | `TryGetVectorType(Microsoft.Xna.Framework.Graphics.SurfaceFormat, out System.Type)` | MISSING |  |  |
| `VectorConverter` | method | `TryGetVectorType(Microsoft.Xna.Framework.Graphics.VertexElementFormat, out System.Type)` | MISSING |  |  |
| `VectorConverter` | method | `TryGetVertexElementFormat(System.Type, out Microsoft.Xna.Framework.Graphics.VertexElementFormat)` | MISSING |  |  |
| `VertexChannel` | property | `Count` | MISSING |  |  |
| `VertexChannel` | property | `ElementType` | MISSING |  |  |
| `VertexChannel` | property | `Name` | MISSING |  |  |
| `VertexChannel` | indexer | `Item[System.Int32]` | MISSING |  |  |
| `VertexChannel` | method | `Contains(System.Object)` | MISSING |  |  |
| `VertexChannel` | method | `CopyTo(System.Array, System.Int32)` | MISSING |  |  |
| `VertexChannel` | method | `GetEnumerator()` | MISSING |  |  |
| `VertexChannel` | method | `IndexOf(System.Object)` | MISSING |  |  |
| `VertexChannel` | method | `ReadConvertedContent<TargetType>()` | MISSING |  |  |
| `VertexChannelCollection` | property | `Count` | MISSING |  |  |
| `VertexChannelCollection` | indexer | `Item[System.Int32]` | MISSING |  |  |
| `VertexChannelCollection` | indexer | `Item[System.String]` | MISSING |  |  |
| `VertexChannelCollection` | method | `Add(System.String, System.Type, System.Collections.IEnumerable)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Add<ElementType>(System.String, System.Collections.Generic.IEnumerable<ElementType>)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Clear()` | MISSING |  |  |
| `VertexChannelCollection` | method | `Contains(Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Contains(System.String)` | MISSING |  |  |
| `VertexChannelCollection` | method | `ConvertChannelContent<TargetType>(System.Int32)` | MISSING |  |  |
| `VertexChannelCollection` | method | `ConvertChannelContent<TargetType>(System.String)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Get<T>(System.Int32)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Get<T>(System.String)` | MISSING |  |  |
| `VertexChannelCollection` | method | `GetEnumerator()` | MISSING |  |  |
| `VertexChannelCollection` | method | `IndexOf(Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel)` | MISSING |  |  |
| `VertexChannelCollection` | method | `IndexOf(System.String)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Insert(System.Int32, System.String, System.Type, System.Collections.IEnumerable)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Insert<ElementType>(System.Int32, System.String, System.Collections.Generic.IEnumerable<ElementType>)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Remove(Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel)` | MISSING |  |  |
| `VertexChannelCollection` | method | `Remove(System.String)` | MISSING |  |  |
| `VertexChannelCollection` | method | `RemoveAt(System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `Binormal(System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `Color(System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `DecodeBaseName(System.String)` | MISSING |  |  |
| `VertexChannelNames` | method | `DecodeUsageIndex(System.String)` | MISSING |  |  |
| `VertexChannelNames` | method | `EncodeName(Microsoft.Xna.Framework.Graphics.VertexElementUsage, System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `EncodeName(System.String, System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `Normal()` | MISSING |  |  |
| `VertexChannelNames` | method | `Normal(System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `Tangent(System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `TextureCoordinate(System.Int32)` | MISSING |  |  |
| `VertexChannelNames` | method | `TryDecodeUsage(System.String, out Microsoft.Xna.Framework.Graphics.VertexElementUsage)` | MISSING |  |  |
| `VertexChannelNames` | method | `Weights()` | MISSING |  |  |
| `VertexChannelNames` | method | `Weights(System.Int32)` | MISSING |  |  |
| `VertexChannel<T>` | property | `ElementType` | MISSING |  |  |
| `VertexChannel<T>` | indexer | `Item[System.Int32]` | MISSING |  |  |
| `VertexChannel<T>` | method | `Contains(T)` | MISSING |  |  |
| `VertexChannel<T>` | method | `CopyTo(T[], System.Int32)` | MISSING |  |  |
| `VertexChannel<T>` | method | `GetEnumerator()` | MISSING |  |  |
| `VertexChannel<T>` | method | `IndexOf(T)` | MISSING |  |  |
| `VertexChannel<T>` | method | `ReadConvertedContent<TargetType>()` | MISSING |  |  |
| `VertexContent` | property | `Channels` | MISSING |  |  |
| `VertexContent` | property | `PositionIndices` | MISSING |  |  |
| `VertexContent` | property | `Positions` | MISSING |  |  |
| `VertexContent` | property | `VertexCount` | MISSING |  |  |
| `VertexContent` | method | `Add(System.Int32)` | MISSING |  |  |
| `VertexContent` | method | `AddRange(System.Collections.Generic.IEnumerable<System.Int32>)` | MISSING |  |  |
| `VertexContent` | method | `CreateVertexBuffer()` | MISSING |  |  |
| `VertexContent` | method | `Insert(System.Int32, System.Int32)` | MISSING |  |  |
| `VertexContent` | method | `InsertRange(System.Int32, System.Collections.Generic.IEnumerable<System.Int32>)` | MISSING |  |  |
| `VertexContent` | method | `RemoveAt(System.Int32)` | MISSING |  |  |
| `VertexContent` | method | `RemoveRange(System.Int32, System.Int32)` | MISSING |  |  |
| `IContentImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `IContentProcessor` | property | `InputType` | MISSING |  |  |
| `IContentProcessor` | property | `OutputType` | MISSING |  |  |
| `IContentProcessor` | method | `Process(System.Object, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `InvalidContentException` | constructor | `.ctor()` | MISSING |  |  |
| `InvalidContentException` | constructor | `.ctor(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)` | HOST_SUBSTITUTION |  | .NET binary serialization of exceptions has no C++ counterpart; the exception's developer-visible contract (message, ContentIdentity, inner exception) is provided in full. |
| `InvalidContentException` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `InvalidContentException` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | MISSING |  |  |
| `InvalidContentException` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity, System.Exception)` | MISSING |  |  |
| `InvalidContentException` | constructor | `.ctor(System.String, System.Exception)` | MISSING |  |  |
| `InvalidContentException` | property | `ContentIdentity` | MISSING |  |  |
| `InvalidContentException` | method | `GetObjectData(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)` | HOST_SUBSTITUTION |  | .NET binary serialization of exceptions has no C++ counterpart; the exception's developer-visible contract (message, ContentIdentity, inner exception) is provided in full. |
| `Mp3Importer` | constructor | `.ctor()` | MISSING |  |  |
| `Mp3Importer` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `NamedValueDictionary<T>` | constructor | `.ctor()` | MISSING |  |  |
| `NamedValueDictionary<T>` | property | `Count` | MISSING |  |  |
| `NamedValueDictionary<T>` | property | `DefaultSerializerType` | MISSING |  |  |
| `NamedValueDictionary<T>` | property | `Keys` | MISSING |  |  |
| `NamedValueDictionary<T>` | property | `Values` | MISSING |  |  |
| `NamedValueDictionary<T>` | indexer | `Item[System.String]` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `Add(System.String, T)` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `AddItem(System.String, T)` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `Clear()` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `ClearItems()` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `ContainsKey(System.String)` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `GetEnumerator()` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `Remove(System.String)` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `RemoveItem(System.String)` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `SetItem(System.String, T)` | MISSING |  |  |
| `NamedValueDictionary<T>` | method | `TryGetValue(System.String, out T)` | MISSING |  |  |
| `OpaqueDataDictionary` | constructor | `.ctor()` | MISSING |  |  |
| `OpaqueDataDictionary` | property | `DefaultSerializerType` | MISSING |  |  |
| `OpaqueDataDictionary` | method | `AddItem(System.String, System.Object)` | MISSING |  |  |
| `OpaqueDataDictionary` | method | `ClearItems()` | MISSING |  |  |
| `OpaqueDataDictionary` | method | `GetContentAsXml()` | MISSING |  |  |
| `OpaqueDataDictionary` | method | `GetValue<T>(System.String, T)` | MISSING |  |  |
| `OpaqueDataDictionary` | method | `RemoveItem(System.String)` | MISSING |  |  |
| `OpaqueDataDictionary` | method | `SetItem(System.String, System.Object)` | MISSING |  |  |
| `PipelineComponentScanner` | constructor | `.ctor()` | MISSING |  |  |
| `PipelineComponentScanner` | property | `Errors` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ImporterAttributes` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ImporterNames` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ImporterOutputTypes` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ProcessorAttributes` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ProcessorInputTypes` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ProcessorNames` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ProcessorOutputTypes` | MISSING |  |  |
| `PipelineComponentScanner` | property | `ProcessorParameters` | MISSING |  |  |
| `PipelineComponentScanner` | method | `Update(System.Collections.Generic.IEnumerable<System.String>)` | MISSING |  |  |
| `PipelineComponentScanner` | method | `Update(System.Collections.Generic.IEnumerable<System.String>, System.Collections.Generic.IEnumerable<System.String>)` | MISSING |  |  |
| `PipelineException` | constructor | `.ctor()` | MISSING |  |  |
| `PipelineException` | constructor | `.ctor(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)` | HOST_SUBSTITUTION |  | .NET binary serialization of exceptions has no C++ counterpart; the exception's developer-visible contract (message, ContentIdentity, inner exception) is provided in full. |
| `PipelineException` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `PipelineException` | constructor | `.ctor(System.String, System.Exception)` | MISSING |  |  |
| `PipelineException` | constructor | `.ctor(System.String, System.Object[])` | MISSING |  |  |
| `ProcessorParameter` | property | `DefaultValue` | MISSING |  |  |
| `ProcessorParameter` | property | `Description` | MISSING |  |  |
| `ProcessorParameter` | property | `DisplayName` | MISSING |  |  |
| `ProcessorParameter` | property | `IsEnum` | MISSING |  |  |
| `ProcessorParameter` | property | `PossibleEnumValues` | MISSING |  |  |
| `ProcessorParameter` | property | `PropertyName` | MISSING |  |  |
| `ProcessorParameter` | property | `PropertyType` | MISSING |  |  |
| `CompiledEffectContent` | constructor | `.ctor(System.Byte[])` | MISSING |  |  |
| `CompiledEffectContent` | method | `GetEffectCode()` | MISSING |  |  |
| `EffectProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `EffectProcessor` | property | `DebugMode` | MISSING |  |  |
| `EffectProcessor` | property | `Defines` | MISSING |  |  |
| `EffectProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.EffectContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `EffectProcessorDebugMode` | enum value | `Auto = 0` | MISSING |  |  |
| `EffectProcessorDebugMode` | enum value | `Debug = 1` | MISSING |  |  |
| `EffectProcessorDebugMode` | enum value | `Optimize = 2` | MISSING |  |  |
| `FontDescriptionProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `FontDescriptionProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescription, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `FontTextureProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `FontTextureProcessor` | property | `FirstCharacter` | MISSING |  |  |
| `FontTextureProcessor` | property | `PremultiplyAlpha` | MISSING |  |  |
| `FontTextureProcessor` | property | `TextureFormat` | MISSING |  |  |
| `FontTextureProcessor` | method | `GetCharacterForIndex(System.Int32)` | MISSING |  |  |
| `FontTextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `MaterialProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `MaterialProcessor` | property | `ColorKeyColor` | MISSING |  |  |
| `MaterialProcessor` | property | `ColorKeyEnabled` | MISSING |  |  |
| `MaterialProcessor` | property | `DefaultEffect` | MISSING |  |  |
| `MaterialProcessor` | property | `GenerateMipmaps` | MISSING |  |  |
| `MaterialProcessor` | property | `PremultiplyTextureAlpha` | MISSING |  |  |
| `MaterialProcessor` | property | `ResizeTexturesToPowerOfTwo` | MISSING |  |  |
| `MaterialProcessor` | property | `TextureFormat` | MISSING |  |  |
| `MaterialProcessor` | method | `BuildEffect(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<Microsoft.Xna.Framework.Content.Pipeline.Graphics.EffectContent>, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `MaterialProcessor` | method | `BuildTexture(System.String, Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent>, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `MaterialProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `MaterialProcessorDefaultEffect` | enum value | `BasicEffect = 0` | MISSING |  |  |
| `MaterialProcessorDefaultEffect` | enum value | `SkinnedEffect = 1` | MISSING |  |  |
| `MaterialProcessorDefaultEffect` | enum value | `EnvironmentMapEffect = 2` | MISSING |  |  |
| `MaterialProcessorDefaultEffect` | enum value | `DualTextureEffect = 3` | MISSING |  |  |
| `MaterialProcessorDefaultEffect` | enum value | `AlphaTestEffect = 4` | MISSING |  |  |
| `ModelBoneContent` | property | `Children` | MISSING |  |  |
| `ModelBoneContent` | property | `Index` | MISSING |  |  |
| `ModelBoneContent` | property | `Name` | MISSING |  |  |
| `ModelBoneContent` | property | `Parent` | MISSING |  |  |
| `ModelBoneContent` | property | `Transform` | MISSING |  |  |
| `ModelContent` | property | `Bones` | MISSING |  |  |
| `ModelContent` | property | `Meshes` | MISSING |  |  |
| `ModelContent` | property | `Root` | MISSING |  |  |
| `ModelContent` | property | `Tag` | MISSING |  |  |
| `ModelMeshContent` | property | `BoundingSphere` | MISSING |  |  |
| `ModelMeshContent` | property | `MeshParts` | MISSING |  |  |
| `ModelMeshContent` | property | `Name` | MISSING |  |  |
| `ModelMeshContent` | property | `ParentBone` | MISSING |  |  |
| `ModelMeshContent` | property | `SourceMesh` | MISSING |  |  |
| `ModelMeshContent` | property | `Tag` | MISSING |  |  |
| `ModelMeshPartContent` | property | `IndexBuffer` | MISSING |  |  |
| `ModelMeshPartContent` | property | `Material` | MISSING |  |  |
| `ModelMeshPartContent` | property | `NumVertices` | MISSING |  |  |
| `ModelMeshPartContent` | property | `PrimitiveCount` | MISSING |  |  |
| `ModelMeshPartContent` | property | `StartIndex` | MISSING |  |  |
| `ModelMeshPartContent` | property | `Tag` | MISSING |  |  |
| `ModelMeshPartContent` | property | `VertexBuffer` | MISSING |  |  |
| `ModelMeshPartContent` | property | `VertexOffset` | MISSING |  |  |
| `ModelProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `ModelProcessor` | property | `ColorKeyColor` | MISSING |  |  |
| `ModelProcessor` | property | `ColorKeyEnabled` | MISSING |  |  |
| `ModelProcessor` | property | `DefaultEffect` | MISSING |  |  |
| `ModelProcessor` | property | `GenerateMipmaps` | MISSING |  |  |
| `ModelProcessor` | property | `GenerateTangentFrames` | MISSING |  |  |
| `ModelProcessor` | property | `PremultiplyTextureAlpha` | MISSING |  |  |
| `ModelProcessor` | property | `PremultiplyVertexColors` | MISSING |  |  |
| `ModelProcessor` | property | `ResizeTexturesToPowerOfTwo` | MISSING |  |  |
| `ModelProcessor` | property | `RotationX` | MISSING |  |  |
| `ModelProcessor` | property | `RotationY` | MISSING |  |  |
| `ModelProcessor` | property | `RotationZ` | MISSING |  |  |
| `ModelProcessor` | property | `Scale` | MISSING |  |  |
| `ModelProcessor` | property | `SwapWindingOrder` | MISSING |  |  |
| `ModelProcessor` | property | `TextureFormat` | MISSING |  |  |
| `ModelProcessor` | method | `ConvertMaterial(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `ModelProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `ModelProcessor` | method | `ProcessGeometryUsingMaterial(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent, System.Collections.Generic.IEnumerable<Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent>, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `ModelProcessor` | method | `ProcessVertexChannel(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent, System.Int32, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `ModelTextureProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `ModelTextureProcessor` | property | `ColorKeyColor` | MISSING |  |  |
| `ModelTextureProcessor` | property | `ColorKeyEnabled` | MISSING |  |  |
| `ModelTextureProcessor` | property | `GenerateMipmaps` | MISSING |  |  |
| `ModelTextureProcessor` | property | `ResizeToPowerOfTwo` | MISSING |  |  |
| `ModelTextureProcessor` | property | `TextureFormat` | MISSING |  |  |
| `ModelTextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `PassThroughProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `PassThroughProcessor` | method | `Process(System.Object, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `SongProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `SongProcessor` | property | `Quality` | MISSING |  |  |
| `SongProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `SoundEffectProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `SoundEffectProcessor` | property | `Quality` | MISSING |  |  |
| `SoundEffectProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `SpriteTextureProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `SpriteTextureProcessor` | property | `ColorKeyColor` | MISSING |  |  |
| `SpriteTextureProcessor` | property | `ColorKeyEnabled` | MISSING |  |  |
| `SpriteTextureProcessor` | property | `GenerateMipmaps` | MISSING |  |  |
| `SpriteTextureProcessor` | property | `ResizeToPowerOfTwo` | MISSING |  |  |
| `SpriteTextureProcessor` | property | `TextureFormat` | MISSING |  |  |
| `SpriteTextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `TextureProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `TextureProcessor` | property | `ColorKeyColor` | MISSING |  |  |
| `TextureProcessor` | property | `ColorKeyEnabled` | MISSING |  |  |
| `TextureProcessor` | property | `GenerateMipmaps` | MISSING |  |  |
| `TextureProcessor` | property | `PremultiplyAlpha` | MISSING |  |  |
| `TextureProcessor` | property | `ResizeToPowerOfTwo` | MISSING |  |  |
| `TextureProcessor` | property | `TextureFormat` | MISSING |  |  |
| `TextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `TextureProcessorOutputFormat` | enum value | `NoChange = 0` | MISSING |  |  |
| `TextureProcessorOutputFormat` | enum value | `Color = 1` | MISSING |  |  |
| `TextureProcessorOutputFormat` | enum value | `DxtCompressed = 2` | MISSING |  |  |
| `VertexBufferContent` | constructor | `.ctor()` | MISSING |  |  |
| `VertexBufferContent` | constructor | `.ctor(System.Int32)` | MISSING |  |  |
| `VertexBufferContent` | property | `VertexData` | MISSING |  |  |
| `VertexBufferContent` | property | `VertexDeclaration` | MISSING |  |  |
| `VertexBufferContent` | method | `SizeOf(System.Type)` | MISSING |  |  |
| `VertexBufferContent` | method | `Write(System.Int32, System.Int32, System.Type, System.Collections.IEnumerable)` | MISSING |  |  |
| `VertexBufferContent` | method | `Write<T>(System.Int32, System.Int32, System.Collections.Generic.IEnumerable<T>)` | MISSING |  |  |
| `VertexDeclarationContent` | constructor | `.ctor()` | MISSING |  |  |
| `VertexDeclarationContent` | property | `VertexElements` | MISSING |  |  |
| `VertexDeclarationContent` | property | `VertexStride` | MISSING |  |  |
| `VideoProcessor` | constructor | `.ctor()` | MISSING |  |  |
| `VideoProcessor` | property | `VideoSoundtrackType` | MISSING |  |  |
| `VideoProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.VideoContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | MISSING |  |  |
| `ContentCompiler` | method | `GetTypeWriter(System.Type)` | MISSING |  |  |
| `ContentTypeWriter` | constructor | `.ctor(System.Type)` | MISSING |  |  |
| `ContentTypeWriter` | property | `CanDeserializeIntoExistingObject` | MISSING |  |  |
| `ContentTypeWriter` | property | `TargetType` | MISSING |  |  |
| `ContentTypeWriter` | property | `TypeVersion` | MISSING |  |  |
| `ContentTypeWriter` | method | `GetRuntimeReader(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform)` | MISSING |  |  |
| `ContentTypeWriter` | method | `GetRuntimeType(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform)` | MISSING |  |  |
| `ContentTypeWriter` | method | `Initialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentCompiler)` | MISSING |  |  |
| `ContentTypeWriter` | method | `ShouldCompressContent(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform, System.Object)` | MISSING |  |  |
| `ContentTypeWriter` | method | `Write(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter, System.Object)` | MISSING |  |  |
| `ContentTypeWriterAttribute` | constructor | `.ctor()` | MISSING |  |  |
| `ContentTypeWriter<T>` | constructor | `.ctor()` | MISSING |  |  |
| `ContentTypeWriter<T>` | method | `Write(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter, System.Object)` | MISSING |  |  |
| `ContentTypeWriter<T>` | method | `Write(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter, T)` | MISSING |  |  |
| `ContentWriter` | property | `TargetPlatform` | MISSING |  |  |
| `ContentWriter` | property | `TargetProfile` | MISSING |  |  |
| `ContentWriter` | method | `Dispose(System.Boolean)` | MISSING |  |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Color)` | MISSING |  |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Matrix)` | MISSING |  |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Quaternion)` | MISSING |  |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Vector2)` | MISSING |  |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Vector3)` | MISSING |  |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Vector4)` | MISSING |  |  |
| `ContentWriter` | method | `WriteExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` | MISSING |  |  |
| `ContentWriter` | method | `WriteObject<T>(T)` | MISSING |  |  |
| `ContentWriter` | method | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentTypeWriter)` | MISSING |  |  |
| `ContentWriter` | method | `WriteRawObject<T>(T)` | MISSING |  |  |
| `ContentWriter` | method | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentTypeWriter)` | MISSING |  |  |
| `ContentWriter` | method | `WriteSharedResource<T>(T)` | MISSING |  |  |
| `ContentTypeSerializer` | constructor | `.ctor(System.Type)` | MISSING |  |  |
| `ContentTypeSerializer` | constructor | `.ctor(System.Type, System.String)` | MISSING |  |  |
| `ContentTypeSerializer` | property | `CanDeserializeIntoExistingObject` | MISSING |  |  |
| `ContentTypeSerializer` | property | `TargetType` | MISSING |  |  |
| `ContentTypeSerializer` | property | `XmlTypeName` | MISSING |  |  |
| `ContentTypeSerializer` | method | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, System.Object)` | MISSING |  |  |
| `ContentTypeSerializer` | method | `Initialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer)` | MISSING |  |  |
| `ContentTypeSerializer` | method | `ObjectIsEmpty(System.Object)` | MISSING |  |  |
| `ContentTypeSerializer` | method | `ScanChildren(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer+ChildCallback, System.Object)` | MISSING |  |  |
| `ContentTypeSerializer` | method | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, System.Object, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `ContentTypeSerializer+ChildCallback` | method | `Invoke(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, System.Object)` | MISSING |  |  |
| `ContentTypeSerializerAttribute` | constructor | `.ctor()` | MISSING |  |  |
| `ContentTypeSerializer<T>` | constructor | `.ctor()` | MISSING |  |  |
| `ContentTypeSerializer<T>` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, System.Object)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `ObjectIsEmpty(System.Object)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `ObjectIsEmpty(T)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `ScanChildren(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer+ChildCallback, System.Object)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `ScanChildren(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer+ChildCallback, T)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, System.Object, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `ContentTypeSerializer<T>` | method | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `IntermediateReader` | property | `Serializer` | MISSING |  |  |
| `IntermediateReader` | property | `Xml` | MISSING |  |  |
| `IntermediateReader` | method | `MoveToElement(System.String)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, T)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, T)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadSharedResource<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, System.Action<T>)` | MISSING |  |  |
| `IntermediateReader` | method | `ReadTypeName()` | MISSING |  |  |
| `IntermediateSerializer` | method | `Deserialize<T>(System.Xml.XmlReader, System.String)` | MISSING |  |  |
| `IntermediateSerializer` | method | `GetTypeSerializer(System.Type)` | MISSING |  |  |
| `IntermediateSerializer` | method | `Serialize<T>(System.Xml.XmlWriter, T, System.String)` | MISSING |  |  |
| `IntermediateWriter` | property | `Serializer` | MISSING |  |  |
| `IntermediateWriter` | property | `Xml` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteSharedResource<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | MISSING |  |  |
| `IntermediateWriter` | method | `WriteTypeName(System.Type)` | MISSING |  |  |
| `TargetPlatform` | enum value | `Windows = 0` | MISSING |  |  |
| `TargetPlatform` | enum value | `Xbox360 = 1` | MISSING |  |  |
| `TargetPlatform` | enum value | `WindowsPhone = 2` | MISSING |  |  |
| `BuildContent` | constant | `CancelEventNameFormat` | MISSING |  |  |
| `BuildContent` | constructor | `.ctor()` | MISSING |  |  |
| `BuildContent` | property | `BuildConfiguration` | MISSING |  |  |
| `BuildContent` | property | `CompressContent` | MISSING |  |  |
| `BuildContent` | property | `ContentProjectGUID` | MISSING |  |  |
| `BuildContent` | property | `IntermediateDirectory` | MISSING |  |  |
| `BuildContent` | property | `IntermediateFiles` | MISSING |  |  |
| `BuildContent` | property | `LoggerRootDirectory` | MISSING |  |  |
| `BuildContent` | property | `OutputContentFiles` | MISSING |  |  |
| `BuildContent` | property | `OutputDirectory` | MISSING |  |  |
| `BuildContent` | property | `PipelineAssemblies` | MISSING |  |  |
| `BuildContent` | property | `PipelineAssemblyDependencies` | MISSING |  |  |
| `BuildContent` | property | `RebuildAll` | MISSING |  |  |
| `BuildContent` | property | `RebuiltContentFiles` | MISSING |  |  |
| `BuildContent` | property | `RootDirectory` | MISSING |  |  |
| `BuildContent` | property | `SourceAssets` | MISSING |  |  |
| `BuildContent` | property | `TargetPlatform` | MISSING |  |  |
| `BuildContent` | property | `TargetProfile` | MISSING |  |  |
| `BuildContent` | method | `Execute()` | MISSING |  |  |
| `BuildXact` | constructor | `.ctor()` | MISSING |  |  |
| `BuildXact` | property | `BuildConfiguration` | MISSING |  |  |
| `BuildXact` | property | `ContentProjectGUID` | MISSING |  |  |
| `BuildXact` | property | `IntermediateDirectory` | MISSING |  |  |
| `BuildXact` | property | `IntermediateFiles` | MISSING |  |  |
| `BuildXact` | property | `LoggerRootDirectory` | MISSING |  |  |
| `BuildXact` | property | `OutputDirectory` | MISSING |  |  |
| `BuildXact` | property | `OutputXactFiles` | MISSING |  |  |
| `BuildXact` | property | `RebuildAll` | MISSING |  |  |
| `BuildXact` | property | `RebuiltXactFiles` | MISSING |  |  |
| `BuildXact` | property | `RootDirectory` | MISSING |  |  |
| `BuildXact` | property | `TargetPlatform` | MISSING |  |  |
| `BuildXact` | property | `TargetProfile` | MISSING |  |  |
| `BuildXact` | property | `XactProjects` | MISSING |  |  |
| `BuildXact` | property | `XnaFrameworkVersion` | MISSING |  |  |
| `BuildXact` | method | `Execute()` | MISSING |  |  |
| `CleanContent` | constructor | `.ctor()` | MISSING |  |  |
| `CleanContent` | property | `BuildConfiguration` | MISSING |  |  |
| `CleanContent` | property | `ContentProjectGUID` | MISSING |  |  |
| `CleanContent` | property | `IntermediateDirectory` | MISSING |  |  |
| `CleanContent` | property | `OutputDirectory` | MISSING |  |  |
| `CleanContent` | property | `RootDirectory` | MISSING |  |  |
| `CleanContent` | property | `TargetPlatform` | MISSING |  |  |
| `CleanContent` | property | `TargetProfile` | MISSING |  |  |
| `CleanContent` | method | `Execute()` | MISSING |  |  |
| `GetLastOutputs` | constructor | `.ctor()` | MISSING |  |  |
| `GetLastOutputs` | property | `ContentProjectGUID` | MISSING |  |  |
| `GetLastOutputs` | property | `IntermediateDirectory` | MISSING |  |  |
| `GetLastOutputs` | property | `OutputContentFiles` | MISSING |  |  |
| `GetLastOutputs` | method | `Execute()` | MISSING |  |  |
| `TextureImporter` | constructor | `.ctor()` | MISSING |  |  |
| `TextureImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `VideoContent` | constructor | `.ctor(System.String)` | MISSING |  |  |
| `VideoContent` | property | `BitsPerSecond` | MISSING |  |  |
| `VideoContent` | property | `Duration` | MISSING |  |  |
| `VideoContent` | property | `Filename` | MISSING |  |  |
| `VideoContent` | property | `FramesPerSecond` | MISSING |  |  |
| `VideoContent` | property | `Height` | MISSING |  |  |
| `VideoContent` | property | `VideoSoundtrackType` | MISSING |  |  |
| `VideoContent` | property | `Width` | MISSING |  |  |
| `VideoContent` | method | `Dispose()` | MISSING |  |  |
| `WavImporter` | constructor | `.ctor()` | MISSING |  |  |
| `WavImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `WmaImporter` | constructor | `.ctor()` | MISSING |  |  |
| `WmaImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `WmvImporter` | constructor | `.ctor()` | MISSING |  |  |
| `WmvImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `XImporter` | constructor | `.ctor()` | MISSING |  |  |
| `XImporter` | method | `Dispose()` | MISSING |  |  |
| `XImporter` | method | `Dispose(System.Boolean)` | MISSING |  |  |
| `XImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `XmlImporter` | constructor | `.ctor()` | MISSING |  |  |
| `XmlImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |

### 7.1 CLR plumbing not counted

| XNA type | Signature | Why |
|---|---|---|
| `ContentTypeSerializer+ChildCallback` | `.ctor(System.Object, System.IntPtr)` | delegate plumbing; the type maps to one C++ callable whose shape is Invoke |
| `ContentTypeSerializer+ChildCallback` | `BeginInvoke(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, System.Object, System.AsyncCallback, System.Object)` | delegate plumbing; the type maps to one C++ callable whose shape is Invoke |
| `ContentTypeSerializer+ChildCallback` | `EndInvoke(System.IAsyncResult)` | delegate plumbing; the type maps to one C++ callable whose shape is Invoke |
