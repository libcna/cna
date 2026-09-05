# XNA 4.0 Content Pipeline parity report

> **Generated** by `tools/xna-pipeline-oracle/parity_report.py` from
> `tests/reference/xna40/content-pipeline-api.json` (the denominator, read from the genuine
> XNA Game Studio 4.0 Refresh assemblies) and `tests/reference/xna40/content-pipeline-parity-map.json`
> (CNA's answer). Do not edit by hand; edit the map and regenerate. Task log and decisions:
> `plans/plan_xnapipeline_parity.md`.

## 1. Coverage summary

| Quantity | Implemented (EXACT + SEMANTIC + HOST_SUBSTITUTION) | EXTERNAL_BLOCKED | MISSING |
|---|---|---:|---:|
| public/protected types | 49/128 (38.3%) | 0 | 79 |
| public/protected members | 275/705 (39.0%) | 0 | 430 |
| enum values | 6/27 (22.2%) | 0 | 21 |
| built-in importers | 0/10 (0.0%) | 0 | 10 |
| built-in processors | 0/12 (0.0%) | 0 | 12 |
| processor properties | 0/47 (0.0%) | 0 | 47 |

Status vocabulary: EXACT_EQUIVALENT, SEMANTIC_EQUIVALENT (spelling differs, capability identical; note says how),
HOST_SUBSTITUTION (Microsoft-host mechanism replaced; note says how), EXTERNAL_BLOCKED (note names the
unavailable component), MISSING. Type status by value: EXACT_EQUIVALENT 30, SEMANTIC_EQUIVALENT 18, HOST_SUBSTITUTION 1, EXTERNAL_BLOCKED 0, MISSING 79.
Member status by value: EXACT_EQUIVALENT 205, SEMANTIC_EQUIVALENT 65, HOST_SUBSTITUTION 5, EXTERNAL_BLOCKED 0, MISSING 430.

Rules applied mechanically: 3 delegate plumbing members are listed in section 7 and not counted; 3 exception
serialization members are HOST_SUBSTITUTION by rule (System.Runtime.Serialization has no C++ counterpart).

## 2. Types by namespace

| Namespace | Types | Implemented | Blocked | Missing |
|---|---:|---:|---:|---:|
| `Microsoft.Xna.Framework.Content.Pipeline.Audio` | 5 | 0 | 0 | 5 |
| `Microsoft.Xna.Framework.Content.Pipeline` | 32 | 21 | 0 | 11 |
| `Microsoft.Xna.Framework.Content.Pipeline.Graphics` | 47 | 16 | 0 | 31 |
| `Microsoft.Xna.Framework.Content.Pipeline.Processors` | 28 | 0 | 0 | 28 |
| `Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler` | 5 | 5 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate` | 7 | 7 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline.Tasks` | 4 | 0 | 0 | 4 |

## 3. Type matrix

| Namespace | XNA type | Kind | Status | CNA type | Note |
|---|---|---|---|---|---|
| `….Audio` | `AudioContent` | class | MISSING |  |  |
| `….Audio` | `AudioFileType` | enum | MISSING |  |  |
| `….Audio` | `AudioFormat` | class | MISSING |  |  |
| `….Audio` | `ConversionFormat` | enum | MISSING |  |  |
| `….Audio` | `ConversionQuality` | enum | MISSING |  |  |
| `…` | `ChildCollection<TParent, TChild>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ChildCollection<TParent, TChild>` |  |
| `…` | `ContentBuildLogger` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentBuildLogger` |  |
| `…` | `ContentIdentity` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentIdentity` |  |
| `…` | `ContentImporterAttribute` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentImporterAttribute` | C++ has no CLR attributes: the same-named descriptor object is passed to RegisterXnaImporter<T>() instead of being attached to the class. |
| `…` | `ContentImporterContext` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentImporterContext` |  |
| `…` | `ContentImporter<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentImporter<T>` |  |
| `…` | `ContentItem` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentItem` |  |
| `…` | `ContentProcessorAttribute` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentProcessorAttribute` | C++ has no CLR attributes: the same-named descriptor object is passed to RegisterXnaProcessor<T>(). |
| `…` | `ContentProcessorContext` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentProcessorContext` |  |
| `…` | `ContentProcessor<TInput, TOutput>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ContentProcessor<TInput, TOutput>` |  |
| `…` | `EffectImporter` | class | MISSING |  |  |
| `…` | `ExternalReference<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<T>` |  |
| `…` | `FbxImporter` | class | MISSING |  |  |
| `…` | `FontDescriptionImporter` | class | MISSING |  |  |
| `….Graphics` | `AlphaTestMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `AnimationChannel` | class | MISSING |  |  |
| `….Graphics` | `AnimationChannelDictionary` | class | MISSING |  |  |
| `….Graphics` | `AnimationContent` | class | MISSING |  |  |
| `….Graphics` | `AnimationContentDictionary` | class | MISSING |  |  |
| `….Graphics` | `AnimationKeyframe` | class | MISSING |  |  |
| `….Graphics` | `BasicMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `BitmapContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::BitmapContent` | All 13 members present. The copy protocol runs in XNA's order -- argument validation, zero-size no-op, same-instance snapshot, destination TryCopyFrom, source TryCopyTo, then the Vector4 intermediate -- verified against tests/reference/xna40/graphics. |
| `….Graphics` | `BoneContent` | class | MISSING |  |  |
| `….Graphics` | `BoneWeight` | struct | MISSING |  |  |
| `….Graphics` | `BoneWeightCollection` | class | MISSING |  |  |
| `….Graphics` | `DualTextureMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `Dxt1BitmapContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Dxt1BitmapContent` |  |
| `….Graphics` | `Dxt3BitmapContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Dxt3BitmapContent` |  |
| `….Graphics` | `Dxt5BitmapContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Dxt5BitmapContent` |  |
| `….Graphics` | `DxtBitmapContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::DxtBitmapContent` | Block storage sized as XNA sizes it (ceil(w/4) * ceil(h/4) * blockSize). Decode/Encode are CNAEXT hooks over CNA's existing BC codec; XNA reaches D3DX for the same step. |
| `….Graphics` | `EffectContent` | class | MISSING |  |  |
| `….Graphics` | `EffectMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `EnvironmentMapMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `FontDescription` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescription` | Every member and every refusal text is pinned to tests/reference/xna40/graphics (cases font/*), including the measured surprises: all three constructors leave UseKerning false, a NaN size and an undefined style are accepted while a size that is not greater than zero is refused, and the constructors refuse through the property setters, so their messages name the parameter `value`. One member exists here that the inventory does not list: a public parameterless constructor, marked CNAEXT. XNA has one too and keeps it private, because its serializer reaches a private constructor by reflection; C++ has none, so the serializer needs a constructor it can call. |
| `….Graphics` | `FontDescriptionStyle` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescriptionStyle` |  |
| `….Graphics` | `GeometryContent` | class | MISSING |  |  |
| `….Graphics` | `GeometryContentCollection` | class | MISSING |  |  |
| `….Graphics` | `IndexCollection` | class | MISSING |  |  |
| `….Graphics` | `IndirectPositionCollection` | class | MISSING |  |  |
| `….Graphics` | `MaterialContent` | class | MISSING |  |  |
| `….Graphics` | `MeshBuilder` | class | MISSING |  |  |
| `….Graphics` | `MeshContent` | class | MISSING |  |  |
| `….Graphics` | `MeshHelper` | class | MISSING |  |  |
| `….Graphics` | `MipmapChain` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MipmapChain` | A Collection<BitmapContent> of shared_ptr elements; the null check XNA's InsertItem/SetItem perform is kept (ArgumentNullException, parameter name item). |
| `….Graphics` | `MipmapChainCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MipmapChainCollection` | Carries XNA's fixed-size flag: a Texture2DContent has one face and a TextureCubeContent six, and resizing either refuses with XNA's NotSupportedException text. |
| `….Graphics` | `NodeContent` | class | MISSING |  |  |
| `….Graphics` | `NodeContentCollection` | class | MISSING |  |  |
| `….Graphics` | `PixelBitmapContent<T>` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::PixelBitmapContent<T>` | T is constrained by a concept to the 22 pixel types XNA permits (detail/PixelTraits.hpp), where XNA discovers them through VectorConverter at run time. |
| `….Graphics` | `PositionCollection` | class | MISSING |  |  |
| `….Graphics` | `SkinnedMaterialContent` | class | MISSING |  |  |
| `….Graphics` | `Texture2DContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Texture2DContent` | The Mipmaps property is the single fixed face, so the setter takes the owning shared_ptr that replaces it rather than assigning a value; everything else is the direct translation. |
| `….Graphics` | `Texture3DContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Texture3DContent` | The one texture whose face collection is resizable, and whose mipmap generation halves the depth as well. |
| `….Graphics` | `TextureContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::TextureContent` | Validate's refusal texts, the mipmap halving rule and ConvertBitmapType are pinned to tests/reference/xna40/graphics. |
| `….Graphics` | `TextureCubeContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::TextureCubeContent` |  |
| `….Graphics` | `TextureReferenceDictionary` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::TextureReferenceDictionary` | A NamedValueDictionary<ExternalReference<TextureContent>>, as XNA's is. |
| `….Graphics` | `VectorConverter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VectorConverter` | The 22-entry surface/vertex format table is pinned to tests/reference/xna40/graphics case by case. |
| `….Graphics` | `VertexChannel` | class | MISSING |  |  |
| `….Graphics` | `VertexChannelCollection` | class | MISSING |  |  |
| `….Graphics` | `VertexChannelNames` | class | MISSING |  |  |
| `….Graphics` | `VertexChannel<T>` | class | MISSING |  |  |
| `….Graphics` | `VertexContent` | class | MISSING |  |  |
| `…` | `IContentImporter` | interface | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::IContentImporter` | The explicit interface implementation object IContentImporter.Import(...) is a non-virtual Import returning ContentObject, reachable only through the interface; the typed ContentImporter<T>::Import hides it exactly as C# does. |
| `…` | `IContentProcessor` | interface | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::IContentProcessor` | The explicit interface implementation object IContentProcessor.Process(object, ...) is a non-virtual Process over ContentObject, reachable only through the interface. |
| `…` | `InvalidContentException` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException` |  |
| `…` | `Mp3Importer` | class | MISSING |  |  |
| `…` | `NamedValueDictionary<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::NamedValueDictionary<T>` |  |
| `…` | `OpaqueDataDictionary` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary` |  |
| `…` | `PipelineComponentScanner` | class | HOST_SUBSTITUTION | `Microsoft::Xna::Framework::Content::Pipeline::PipelineComponentScanner` | Assembly scanning has no C++ counterpart; the scanner enumerates the XNA-shaped components registered in a ContentPipelineRegistry, grouped by catalog name. |
| `…` | `PipelineException` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::PipelineException` |  |
| `…` | `ProcessorParameter` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ProcessorParameter` |  |
| `…` | `ProcessorParameterCollection` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ProcessorParameterCollection` |  |
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
| `….Serialization.Compiler` | `ContentCompiler` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentCompiler` |  |
| `….Serialization.Compiler` | `ContentTypeWriter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentTypeWriterBase` | C++ cannot give a class and a class template the same name, so the non-generic base is ContentTypeWriterBase -- the spelling CNA's runtime already uses for ContentTypeReaderBase beside ContentTypeReader<T>. |
| `….Serialization.Compiler` | `ContentTypeWriterAttribute` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentTypeWriterAttribute` | C++ has no CLR attributes: the same-named descriptor is passed to ContentCompiler::AddTypeWriter<TWriter>(). |
| `….Serialization.Compiler` | `ContentTypeWriter<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentTypeWriter<T>` |  |
| `….Serialization.Compiler` | `ContentWriter` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler::ContentWriter` |  |
| `….Serialization.Intermediate` | `ContentTypeSerializer` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentTypeSerializerBase` | C++ cannot give a class and a class template the same name, so the non-generic base is ContentTypeSerializerBase, as ContentTypeWriterBase stands beside ContentTypeWriter<T>. The protected internal members are protected and reached through Invoke* entry points. |
| `….Serialization.Intermediate` | `ContentTypeSerializer+ChildCallback` | delegate | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentTypeSerializerBase::ChildCallback` | a delegate is a std::function<void(ContentTypeSerializerBase&, const ContentObject&)>; Invoke is the call operator |
| `….Serialization.Intermediate` | `ContentTypeSerializerAttribute` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentTypeSerializerAttribute` | an attribute becomes a descriptor object handed to IntermediateSerializer::AddTypeSerializer<TSerializer>(), since C++ has no attributes or reflection |
| `….Serialization.Intermediate` | `ContentTypeSerializer<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentTypeSerializer<T>` |  |
| `….Serialization.Intermediate` | `IntermediateReader` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::IntermediateReader` |  |
| `….Serialization.Intermediate` | `IntermediateSerializer` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::IntermediateSerializer` | XNA discovers ContentTypeSerializer classes and member layouts by reflection; CNA registers serializers (AddTypeSerializer) and describes types (ContentTypeDescriptor), which are CNAEXT additions rather than changes to the XNA surface. |
| `….Serialization.Intermediate` | `IntermediateWriter` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::IntermediateWriter` |  |
| `…` | `TargetPlatform` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::TargetPlatform` |  |
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
| `ChildCollection<TParent, TChild>` | constructor | `.ctor(TParent)` | EXACT_EQUIVALENT | `ChildCollection(TParent)` |  |
| `ChildCollection<TParent, TChild>` | method | `ClearItems()` | EXACT_EQUIVALENT | `ClearItems()` |  |
| `ChildCollection<TParent, TChild>` | method | `GetParent(TChild)` | SEMANTIC_EQUIVALENT | `GetParent(const std::shared_ptr<TChild>&) -> TParent*` | children are shared pointers and the parent back reference a raw pointer valid while the child is in the collection -- the lifetime a .NET reference gives it. |
| `ChildCollection<TParent, TChild>` | method | `InsertItem(System.Int32, TChild)` | EXACT_EQUIVALENT | `InsertItem(System.Int32, TChild)` |  |
| `ChildCollection<TParent, TChild>` | method | `RemoveItem(System.Int32)` | EXACT_EQUIVALENT | `RemoveItem(System.Int32)` |  |
| `ChildCollection<TParent, TChild>` | method | `SetItem(System.Int32, TChild)` | EXACT_EQUIVALENT | `SetItem(System.Int32, TChild)` |  |
| `ChildCollection<TParent, TChild>` | method | `SetParent(TChild, TParent)` | SEMANTIC_EQUIVALENT | `SetParent(const std::shared_ptr<TChild>&, TParent*)` | same carrier rule as GetParent. |
| `ContentBuildLogger` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentBuildLogger()` |  |
| `ContentBuildLogger` | property | `LoggerRootDirectory` | EXACT_EQUIVALENT | `getLoggerRootDirectoryProperty() / setLoggerRootDirectoryProperty()` |  |
| `ContentBuildLogger` | method | `GetCurrentFilename(Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | EXACT_EQUIVALENT | `GetCurrentFilename(Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` |  |
| `ContentBuildLogger` | method | `LogImportantMessage(System.String, System.Object[])` | SEMANTIC_EQUIVALENT | `LogImportantMessage(std::string_view, Args&&...)` | params object[] becomes a variadic std::format template forwarding to the non-template virtual LogImportantMessage(const std::string&). |
| `ContentBuildLogger` | method | `LogMessage(System.String, System.Object[])` | SEMANTIC_EQUIVALENT | `LogMessage(std::string_view, Args&&...)` | params object[] becomes a variadic std::format template forwarding to the non-template virtual LogMessage(const std::string&). |
| `ContentBuildLogger` | method | `LogWarning(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity, System.String, System.Object[])` | SEMANTIC_EQUIVALENT | `LogWarning(const std::string&, const ContentIdentity&, std::string_view, Args&&...)` | params object[] becomes a variadic std::format template forwarding to the non-template virtual LogWarning(helpLink, identity, const std::string&). |
| `ContentBuildLogger` | method | `PopFile()` | EXACT_EQUIVALENT | `PopFile()` |  |
| `ContentBuildLogger` | method | `PushFile(System.String)` | EXACT_EQUIVALENT | `PushFile(System.String)` |  |
| `ContentIdentity` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentIdentity()` |  |
| `ContentIdentity` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `ContentIdentity(System.String)` |  |
| `ContentIdentity` | constructor | `.ctor(System.String, System.String)` | EXACT_EQUIVALENT | `ContentIdentity(System.String, System.String)` |  |
| `ContentIdentity` | constructor | `.ctor(System.String, System.String, System.String)` | EXACT_EQUIVALENT | `ContentIdentity(System.String, System.String, System.String)` |  |
| `ContentIdentity` | property | `FragmentIdentifier` | EXACT_EQUIVALENT | `getFragmentIdentifierProperty() / setFragmentIdentifierProperty()` |  |
| `ContentIdentity` | property | `SourceFilename` | EXACT_EQUIVALENT | `getSourceFilenameProperty() / setSourceFilenameProperty()` |  |
| `ContentIdentity` | property | `SourceTool` | EXACT_EQUIVALENT | `getSourceToolProperty() / setSourceToolProperty()` |  |
| `ContentImporterAttribute` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `ContentImporterAttribute(System.String)` |  |
| `ContentImporterAttribute` | constructor | `.ctor(System.String[])` | EXACT_EQUIVALENT | `ContentImporterAttribute(System.String[])` |  |
| `ContentImporterAttribute` | property | `CacheImportedData` | EXACT_EQUIVALENT | `getCacheImportedDataProperty() / setCacheImportedDataProperty()` |  |
| `ContentImporterAttribute` | property | `DefaultProcessor` | EXACT_EQUIVALENT | `getDefaultProcessorProperty() / setDefaultProcessorProperty()` |  |
| `ContentImporterAttribute` | property | `DisplayName` | EXACT_EQUIVALENT | `getDisplayNameProperty() / setDisplayNameProperty()` |  |
| `ContentImporterAttribute` | property | `FileExtensions` | SEMANTIC_EQUIVALENT | `getFileExtensionsProperty()` | IEnumerable<string> is returned as const std::vector<std::string>&. |
| `ContentImporterContext` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentImporterContext()` |  |
| `ContentImporterContext` | property | `IntermediateDirectory` | EXACT_EQUIVALENT | `getIntermediateDirectoryProperty()` |  |
| `ContentImporterContext` | property | `Logger` | EXACT_EQUIVALENT | `getLoggerProperty()` |  |
| `ContentImporterContext` | property | `OutputDirectory` | EXACT_EQUIVALENT | `getOutputDirectoryProperty()` |  |
| `ContentImporterContext` | method | `AddDependency(System.String)` | EXACT_EQUIVALENT | `AddDependency(System.String)` |  |
| `ContentImporter<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentImporter()` |  |
| `ContentImporter<T>` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | EXACT_EQUIVALENT | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` |  |
| `ContentItem` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentItem()` |  |
| `ContentItem` | property | `Identity` | EXACT_EQUIVALENT | `getIdentityProperty() / setIdentityProperty()` |  |
| `ContentItem` | property | `Name` | EXACT_EQUIVALENT | `getNameProperty() / setNameProperty()` |  |
| `ContentItem` | property | `OpaqueData` | EXACT_EQUIVALENT | `getOpaqueDataProperty()` |  |
| `ContentProcessorAttribute` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentProcessorAttribute()` |  |
| `ContentProcessorAttribute` | property | `DisplayName` | EXACT_EQUIVALENT | `getDisplayNameProperty() / setDisplayNameProperty()` |  |
| `ContentProcessorContext` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentProcessorContext()` |  |
| `ContentProcessorContext` | property | `BuildConfiguration` | EXACT_EQUIVALENT | `getBuildConfigurationProperty()` |  |
| `ContentProcessorContext` | property | `IntermediateDirectory` | EXACT_EQUIVALENT | `getIntermediateDirectoryProperty()` |  |
| `ContentProcessorContext` | property | `Logger` | EXACT_EQUIVALENT | `getLoggerProperty()` |  |
| `ContentProcessorContext` | property | `OutputDirectory` | EXACT_EQUIVALENT | `getOutputDirectoryProperty()` |  |
| `ContentProcessorContext` | property | `OutputFilename` | EXACT_EQUIVALENT | `getOutputFilenameProperty()` |  |
| `ContentProcessorContext` | property | `Parameters` | EXACT_EQUIVALENT | `getParametersProperty()` |  |
| `ContentProcessorContext` | property | `TargetPlatform` | EXACT_EQUIVALENT | `getTargetPlatformProperty()` |  |
| `ContentProcessorContext` | property | `TargetProfile` | EXACT_EQUIVALENT | `getTargetProfileProperty()` |  |
| `ContentProcessorContext` | method | `AddDependency(System.String)` | EXACT_EQUIVALENT | `AddDependency(System.String)` |  |
| `ContentProcessorContext` | method | `AddOutputFile(System.String)` | EXACT_EQUIVALENT | `AddOutputFile(System.String)` |  |
| `ContentProcessorContext` | method | `BuildAndLoadAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String)` | SEMANTIC_EQUIVALENT | `BuildAndLoadAsset<TInput, TOutput>(const ExternalReference<TInput>&, const std::string&)` | the two-argument overload is the four-argument template with defaulted empty parameters and importer name. |
| `ContentProcessorContext` | method | `BuildAndLoadAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String, Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary, System.String)` | SEMANTIC_EQUIVALENT | `BuildAndLoadAsset<TInput, TOutput>(const ExternalReference<TInput>&, const std::string&, const OpaqueDataDictionary&, const std::string&)` | a generic method cannot be virtual in C++: the member template forwards to the non-template virtual BuildAndLoadAssetCore carrying the type names; the nested import/process runs on the canonical pipeline and its dependencies merge into the outer node. |
| `ContentProcessorContext` | method | `BuildAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String)` | SEMANTIC_EQUIVALENT | `BuildAsset<TInput, TOutput>(const ExternalReference<TInput>&, const std::string&)` | the two-argument overload is the five-argument template with defaulted empty parameters, importer name and asset name. |
| `ContentProcessorContext` | method | `BuildAsset<TInput, TOutput>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<TInput>, System.String, Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary, System.String, System.String)` | SEMANTIC_EQUIVALENT | `BuildAsset<TInput, TOutput>(const ExternalReference<TInput>&, const std::string&, const OpaqueDataDictionary&, const std::string&, const std::string&)` | a generic method cannot be virtual in C++: the member template forwards to the non-template virtual BuildAssetCore; the nested build runs through the canonical pipeline and its compiled output becomes an additional output of the current node, owned and fingerprinted like any other artifact. |
| `ContentProcessorContext` | method | `Convert<TInput, TOutput>(TInput, System.String)` | SEMANTIC_EQUIVALENT | `Convert<TInput, TOutput>(const Carrier<TInput>&, const std::string&)` | the two-argument overload is the three-argument template with a defaulted empty OpaqueDataDictionary. |
| `ContentProcessorContext` | method | `Convert<TInput, TOutput>(TInput, System.String, Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary)` | SEMANTIC_EQUIVALENT | `Convert<TInput, TOutput>(const Carrier<TInput>&, const std::string&, const OpaqueDataDictionary&)` | a generic method cannot be virtual in C++: the member template forwards to the non-template virtual ConvertCore carrying the type names. |
| `ContentProcessor<TInput, TOutput>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentProcessor()` |  |
| `ContentProcessor<TInput, TOutput>` | method | `Process(TInput, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | EXACT_EQUIVALENT | `Process(TInput, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` |  |
| `EffectImporter` | constructor | `.ctor()` | MISSING |  |  |
| `EffectImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `ExternalReference<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ExternalReference()` |  |
| `ExternalReference<T>` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `ExternalReference(System.String)` |  |
| `ExternalReference<T>` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | EXACT_EQUIVALENT | `ExternalReference(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` |  |
| `ExternalReference<T>` | property | `Filename` | EXACT_EQUIVALENT | `getFilenameProperty() / setFilenameProperty()` |  |
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
| `BitmapContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `BitmapContent()` |  |
| `BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `BitmapContent(intcs, intcs)` |  |
| `BitmapContent` | property | `Height` | EXACT_EQUIVALENT | `getHeightProperty() / setHeightProperty()` |  |
| `BitmapContent` | property | `Width` | EXACT_EQUIVALENT | `getWidthProperty() / setWidthProperty()` |  |
| `BitmapContent` | method | `Copy(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `Copy(const std::shared_ptr<BitmapContent>&, const std::shared_ptr<BitmapContent>&)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `BitmapContent` | method | `Copy(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `Copy(const std::shared_ptr<BitmapContent>&, Rectangle, const std::shared_ptr<BitmapContent>&, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `BitmapContent` | method | `GetPixelData()` | EXACT_EQUIVALENT | `GetPixelData()` | byte[] is std::vector<bytecs>; measured to be a snapshot, not the bitmap's storage (color/get_pixel_data_is_snapshot). |
| `BitmapContent` | method | `SetPixelData(System.Byte[])` | EXACT_EQUIVALENT | `SetPixelData(const std::vector<bytecs>&)` |  |
| `BitmapContent` | method | `ToString()` | EXACT_EQUIVALENT | `ToString()` |  |
| `BitmapContent` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `TryCopyFrom(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. Protected in both. |
| `BitmapContent` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `TryCopyTo(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. Protected in both. |
| `BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetFormat(SurfaceFormat&)` |  |
| `BitmapContent` | method | `ValidateCopyArguments(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `ValidateCopyArguments(const std::shared_ptr<BitmapContent>&, Rectangle, const std::shared_ptr<BitmapContent>&, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
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
| `Dxt1BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `Dxt1BitmapContent(intcs, intcs)` |  |
| `Dxt1BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetFormat(SurfaceFormat&)` | SurfaceFormat::Dxt1 |
| `Dxt3BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `Dxt3BitmapContent(intcs, intcs)` |  |
| `Dxt3BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetFormat(SurfaceFormat&)` | SurfaceFormat::Dxt3 |
| `Dxt5BitmapContent` | constructor | `.ctor(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `Dxt5BitmapContent(intcs, intcs)` |  |
| `Dxt5BitmapContent` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetFormat(SurfaceFormat&)` | SurfaceFormat::Dxt5 |
| `DxtBitmapContent` | constructor | `.ctor(System.Int32)` | EXACT_EQUIVALENT | `DxtBitmapContent(intcs)` |  |
| `DxtBitmapContent` | constructor | `.ctor(System.Int32, System.Int32, System.Int32)` | EXACT_EQUIVALENT | `DxtBitmapContent(intcs, intcs, intcs)` |  |
| `DxtBitmapContent` | method | `GetPixelData()` | EXACT_EQUIVALENT | `GetPixelData()` |  |
| `DxtBitmapContent` | method | `SetPixelData(System.Byte[])` | EXACT_EQUIVALENT | `SetPixelData(const std::vector<bytecs>&)` |  |
| `DxtBitmapContent` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `TryCopyFrom(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `DxtBitmapContent` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `TryCopyTo(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
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
| `FontDescription` | constructor | `.ctor(System.String, System.Single, System.Single)` | EXACT_EQUIVALENT | `FontDescription(std::string, Single, Single)` |  |
| `FontDescription` | constructor | `.ctor(System.String, System.Single, System.Single, Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescriptionStyle)` | EXACT_EQUIVALENT | `FontDescription(std::string, Single, Single, FontDescriptionStyle)` |  |
| `FontDescription` | constructor | `.ctor(System.String, System.Single, System.Single, Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescriptionStyle, System.Boolean)` | EXACT_EQUIVALENT | `FontDescription(std::string, Single, Single, FontDescriptionStyle, bool)` |  |
| `FontDescription` | property | `Characters` | SEMANTIC_EQUIVALENT | `getCharactersProperty()` | ICollection<char> is a std::set<charcs>&, which is what XNA's private CharacterCollection behaves as: measured, a character added twice appears once and the collection reads back in ascending order. |
| `FontDescription` | property | `DefaultCharacter` | EXACT_EQUIVALENT | `getDefaultCharacterProperty() / setDefaultCharacterProperty()` | Nullable<char> is std::optional<charcs>. |
| `FontDescription` | property | `FontName` | EXACT_EQUIVALENT | `getFontNameProperty() / setFontNameProperty()` | C++ has no null std::string, so the empty name carries both of the refusals XNA gives for null and for empty. |
| `FontDescription` | property | `Size` | EXACT_EQUIVALENT | `getSizeProperty() / setSizeProperty()` |  |
| `FontDescription` | property | `Spacing` | EXACT_EQUIVALENT | `getSpacingProperty() / setSpacingProperty()` |  |
| `FontDescription` | property | `Style` | EXACT_EQUIVALENT | `getStyleProperty() / setStyleProperty()` |  |
| `FontDescription` | property | `UseKerning` | EXACT_EQUIVALENT | `getUseKerningProperty() / setUseKerningProperty()` |  |
| `FontDescriptionStyle` | enum value | `Regular = 0` | EXACT_EQUIVALENT | `FontDescriptionStyle::Regular` |  |
| `FontDescriptionStyle` | enum value | `Bold = 1` | EXACT_EQUIVALENT | `FontDescriptionStyle::Bold` |  |
| `FontDescriptionStyle` | enum value | `Italic = 2` | EXACT_EQUIVALENT | `FontDescriptionStyle::Italic` |  |
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
| `MipmapChain` | constructor | `.ctor()` | EXACT_EQUIVALENT | `MipmapChain()` |  |
| `MipmapChain` | constructor | `.ctor(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `MipmapChain(std::shared_ptr<BitmapContent>)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `MipmapChain` | method | `InsertItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `InsertItem(intcs, const std::shared_ptr<BitmapContent>&)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `MipmapChain` | method | `SetItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `SetItem(intcs, const std::shared_ptr<BitmapContent>&)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `MipmapChain` | operator | `op_Implicit(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `MipmapChain(std::shared_ptr<BitmapContent>)` | C#'s implicit conversion operator is C++'s converting constructor, which is what makes chain = bitmap compile in both. |
| `MipmapChainCollection` | method | `ClearItems()` | EXACT_EQUIVALENT | `ClearItems()` |  |
| `MipmapChainCollection` | method | `InsertItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain)` | SEMANTIC_EQUIVALENT | `InsertItem(intcs, const std::shared_ptr<MipmapChain>&)` | elements are shared_ptr carriers, as everywhere a chain is owned and shared. |
| `MipmapChainCollection` | method | `RemoveItem(System.Int32)` | EXACT_EQUIVALENT | `RemoveItem(intcs)` |  |
| `MipmapChainCollection` | method | `SetItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain)` | SEMANTIC_EQUIVALENT | `SetItem(intcs, const std::shared_ptr<MipmapChain>&)` | elements are shared_ptr carriers, as everywhere a chain is owned and shared. |
| `NodeContent` | constructor | `.ctor()` | MISSING |  |  |
| `NodeContent` | property | `AbsoluteTransform` | MISSING |  |  |
| `NodeContent` | property | `Animations` | MISSING |  |  |
| `NodeContent` | property | `Children` | MISSING |  |  |
| `NodeContent` | property | `Parent` | MISSING |  |  |
| `NodeContent` | property | `Transform` | MISSING |  |  |
| `NodeContentCollection` | method | `GetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | MISSING |  |  |
| `NodeContentCollection` | method | `SetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | MISSING |  |  |
| `PixelBitmapContent<T>` | constructor | `.ctor()` | MISSING |  | Not implemented. XNA's parameterless constructor leaves a 0x0 bitmap for reflection to fill; CNA's derived types and its type registry construct through the sized constructor, which refuses a zero dimension as XNA's does. |
| `PixelBitmapContent<T>` | constructor | `.ctor(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `PixelBitmapContent(intcs, intcs)` |  |
| `PixelBitmapContent<T>` | method | `GetPixel(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `GetPixel(intcs, intcs)` |  |
| `PixelBitmapContent<T>` | method | `GetPixelData()` | EXACT_EQUIVALENT | `GetPixelData()` |  |
| `PixelBitmapContent<T>` | method | `GetRow(System.Int32)` | SEMANTIC_EQUIVALENT | `GetRow(intcs) -> std::span<T> / std::span<const T>` | XNA returns the bitmap's own row array, so writing through it changes the bitmap (measured, color/get_row_is_live). A std::span keeps that aliasing where a std::vector copy would silently lose it. |
| `PixelBitmapContent<T>` | method | `ReplaceColor(T, T)` | EXACT_EQUIVALENT | `ReplaceColor(const T&, const T&)` |  |
| `PixelBitmapContent<T>` | method | `SetPixel(System.Int32, System.Int32, T)` | EXACT_EQUIVALENT | `SetPixel(intcs, intcs, const T&)` |  |
| `PixelBitmapContent<T>` | method | `SetPixelData(System.Byte[])` | EXACT_EQUIVALENT | `SetPixelData(const std::vector<bytecs>&)` |  |
| `PixelBitmapContent<T>` | method | `ToString()` | SEMANTIC_EQUIVALENT | `ToString()` | Inherited from BitmapContent, which composes the display name a virtual TypeDisplayName() supplies; C++ has no run-time generic type name to format as XNA's override does. The text matches (PixelBitmapContent<Color>, 3x2). |
| `PixelBitmapContent<T>` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `TryCopyFrom(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `PixelBitmapContent<T>` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | SEMANTIC_EQUIVALENT | `TryCopyTo(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `PixelBitmapContent<T>` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetFormat(SurfaceFormat&)` |  |
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
| `Texture2DContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `Texture2DContent()` |  |
| `Texture2DContent` | property | `Mipmaps` | SEMANTIC_EQUIVALENT | `getMipmapsProperty() / setMipmapsProperty(std::shared_ptr<MipmapChain>)` | the getter answers a reference to the single face, the setter takes the owning shared_ptr that replaces it. |
| `Texture2DContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | EXACT_EQUIVALENT | `Validate(std::optional<GraphicsProfile>)` |  |
| `Texture3DContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `Texture3DContent()` |  |
| `Texture3DContent` | method | `GenerateMipmaps(System.Boolean)` | EXACT_EQUIVALENT | `GenerateMipmaps(bool)` |  |
| `Texture3DContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | EXACT_EQUIVALENT | `Validate(std::optional<GraphicsProfile>)` |  |
| `TextureContent` | constructor | `.ctor(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChainCollection)` | SEMANTIC_EQUIVALENT | `TextureContent(std::shared_ptr<MipmapChainCollection>)` | the face collection is an owned shared_ptr; protected in both. |
| `TextureContent` | property | `Faces` | EXACT_EQUIVALENT | `getFacesProperty()` |  |
| `TextureContent` | method | `ConvertBitmapType(System.Type)` | EXACT_EQUIVALENT | `ConvertBitmapType(System::Type)` | the target type is looked up in CNA's own bitmap-type registry, which RegisterBitmapType<T>() fills, where XNA reflects over the assembly. |
| `TextureContent` | method | `GenerateMipmaps(System.Boolean)` | EXACT_EQUIVALENT | `GenerateMipmaps(bool)` |  |
| `TextureContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | EXACT_EQUIVALENT | `Validate(std::optional<GraphicsProfile>)` |  |
| `TextureCubeContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `TextureCubeContent()` |  |
| `TextureCubeContent` | method | `Validate(System.Nullable<Microsoft.Xna.Framework.Graphics.GraphicsProfile>)` | EXACT_EQUIVALENT | `Validate(std::optional<GraphicsProfile>)` |  |
| `TextureReferenceDictionary` | constructor | `.ctor()` | EXACT_EQUIVALENT | `TextureReferenceDictionary()` |  |
| `VectorConverter` | method | `GetConverter<TInput, TOutput>()` | SEMANTIC_EQUIVALENT | `GetConverter<TInput, TOutput>() -> std::function<TOutput(TInput)>` | Converter<TInput,TOutput> is a delegate; std::function is its C++ carrier. XNA answers null for an unsupported pair at run time, where the C++ template refuses at compile time through a concept. |
| `VectorConverter` | method | `TryGetSurfaceFormat(System.Type, out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetSurfaceFormat(System::Type, SurfaceFormat&)` |  |
| `VectorConverter` | method | `TryGetVectorType(Microsoft.Xna.Framework.Graphics.SurfaceFormat, out System.Type)` | EXACT_EQUIVALENT | `TryGetVectorType(SurfaceFormat, System::Type&)` |  |
| `VectorConverter` | method | `TryGetVectorType(Microsoft.Xna.Framework.Graphics.VertexElementFormat, out System.Type)` | EXACT_EQUIVALENT | `TryGetVectorType(VertexElementFormat, System::Type&)` |  |
| `VectorConverter` | method | `TryGetVertexElementFormat(System.Type, out Microsoft.Xna.Framework.Graphics.VertexElementFormat)` | EXACT_EQUIVALENT | `TryGetVertexElementFormat(System::Type, VertexElementFormat&)` |  |
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
| `IContentImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> ContentObject` | object is ContentObject (the canonical type-erased box); non-virtual, dispatching to the protected virtual ImportObject. |
| `IContentProcessor` | property | `InputType` | EXACT_EQUIVALENT | `getInputTypeProperty()` |  |
| `IContentProcessor` | property | `OutputType` | EXACT_EQUIVALENT | `getOutputTypeProperty()` |  |
| `IContentProcessor` | method | `Process(System.Object, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const ContentObject&, ContentProcessorContext&) -> ContentObject` | object is ContentObject; non-virtual, dispatching to the protected virtual ProcessObject. |
| `InvalidContentException` | constructor | `.ctor()` | EXACT_EQUIVALENT | `InvalidContentException()` |  |
| `InvalidContentException` | constructor | `.ctor(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)` | HOST_SUBSTITUTION |  | .NET binary serialization of exceptions has no C++ counterpart; the exception's developer-visible contract (message, ContentIdentity, inner exception) is provided in full. |
| `InvalidContentException` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `InvalidContentException(System.String)` |  |
| `InvalidContentException` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | EXACT_EQUIVALENT | `InvalidContentException(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` |  |
| `InvalidContentException` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity, System.Exception)` | EXACT_EQUIVALENT | `InvalidContentException(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity, System.Exception)` |  |
| `InvalidContentException` | constructor | `.ctor(System.String, System.Exception)` | EXACT_EQUIVALENT | `InvalidContentException(System.String, System.Exception)` |  |
| `InvalidContentException` | property | `ContentIdentity` | EXACT_EQUIVALENT | `getContentIdentityProperty() / setContentIdentityProperty()` |  |
| `InvalidContentException` | method | `GetObjectData(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)` | HOST_SUBSTITUTION |  | .NET binary serialization of exceptions has no C++ counterpart; the exception's developer-visible contract (message, ContentIdentity, inner exception) is provided in full. |
| `Mp3Importer` | constructor | `.ctor()` | MISSING |  |  |
| `Mp3Importer` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `NamedValueDictionary<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `NamedValueDictionary()` |  |
| `NamedValueDictionary<T>` | property | `Count` | EXACT_EQUIVALENT | `getCountProperty()` |  |
| `NamedValueDictionary<T>` | property | `DefaultSerializerType` | SEMANTIC_EQUIVALENT | `getDefaultSerializerTypeProperty()` | protected internal getter becomes a protected virtual returning System::Type. |
| `NamedValueDictionary<T>` | property | `Keys` | SEMANTIC_EQUIVALENT | `getKeysProperty()` | ICollection<string> is returned as std::vector<std::string> (sharp-runtime Dictionary precedent). |
| `NamedValueDictionary<T>` | property | `Values` | SEMANTIC_EQUIVALENT | `getValuesProperty()` | ICollection<T> is returned as std::vector<T>. |
| `NamedValueDictionary<T>` | indexer | `Item[System.String]` | SEMANTIC_EQUIVALENT | `operator[] / Set()` | the C# indexer getter is operator[]; its add-or-replace setter is Set(key, value) because operator[] cannot add a key without a value. |
| `NamedValueDictionary<T>` | method | `Add(System.String, T)` | EXACT_EQUIVALENT | `Add(System.String, T)` |  |
| `NamedValueDictionary<T>` | method | `AddItem(System.String, T)` | EXACT_EQUIVALENT | `AddItem(System.String, T)` |  |
| `NamedValueDictionary<T>` | method | `Clear()` | EXACT_EQUIVALENT | `Clear()` |  |
| `NamedValueDictionary<T>` | method | `ClearItems()` | EXACT_EQUIVALENT | `ClearItems()` |  |
| `NamedValueDictionary<T>` | method | `ContainsKey(System.String)` | EXACT_EQUIVALENT | `ContainsKey(System.String)` |  |
| `NamedValueDictionary<T>` | method | `GetEnumerator()` | SEMANTIC_EQUIVALENT | `GetEnumerator() / begin(), end()` | IEnumerator<KeyValuePair<string,T>> is a heap-allocated sharp-runtime enumerator the caller owns; range-for over std::pair is the idiomatic form. |
| `NamedValueDictionary<T>` | method | `Remove(System.String)` | EXACT_EQUIVALENT | `Remove(System.String)` |  |
| `NamedValueDictionary<T>` | method | `RemoveItem(System.String)` | EXACT_EQUIVALENT | `RemoveItem(System.String)` |  |
| `NamedValueDictionary<T>` | method | `SetItem(System.String, T)` | EXACT_EQUIVALENT | `SetItem(System.String, T)` |  |
| `NamedValueDictionary<T>` | method | `TryGetValue(System.String, out T)` | EXACT_EQUIVALENT | `TryGetValue(System.String, out T)` |  |
| `OpaqueDataDictionary` | constructor | `.ctor()` | EXACT_EQUIVALENT | `OpaqueDataDictionary()` |  |
| `OpaqueDataDictionary` | property | `DefaultSerializerType` | SEMANTIC_EQUIVALENT | `getDefaultSerializerTypeProperty()` | protected internal getter becomes a protected virtual returning System::Type::From<System::Object>(). |
| `OpaqueDataDictionary` | method | `AddItem(System.String, System.Object)` | EXACT_EQUIVALENT | `AddItem(System.String, System.Object)` |  |
| `OpaqueDataDictionary` | method | `ClearItems()` | EXACT_EQUIVALENT | `ClearItems()` |  |
| `OpaqueDataDictionary` | method | `GetContentAsXml()` | EXACT_EQUIVALENT | `GetContentAsXml()` | the measured document: compact, utf-16 declaration, <Data Key> entries typed only when not strings |
| `OpaqueDataDictionary` | method | `GetValue<T>(System.String, T)` | EXACT_EQUIVALENT | `GetValue<T>(System.String, T)` |  |
| `OpaqueDataDictionary` | method | `RemoveItem(System.String)` | EXACT_EQUIVALENT | `RemoveItem(System.String)` |  |
| `OpaqueDataDictionary` | method | `SetItem(System.String, System.Object)` | EXACT_EQUIVALENT | `SetItem(System.String, System.Object)` |  |
| `PipelineComponentScanner` | constructor | `.ctor()` | EXACT_EQUIVALENT | `PipelineComponentScanner()` |  |
| `PipelineComponentScanner` | property | `Errors` | SEMANTIC_EQUIVALENT | `getErrorsProperty()` | IList<string> is returned as const std::vector<std::string>&. |
| `PipelineComponentScanner` | property | `ImporterAttributes` | SEMANTIC_EQUIVALENT | `getImporterAttributesProperty()` | IDictionary<string, X> is returned as const std::map<std::string, X>&. |
| `PipelineComponentScanner` | property | `ImporterNames` | SEMANTIC_EQUIVALENT | `getImporterNamesProperty()` | IEnumerable<string> is returned as std::vector<std::string>. |
| `PipelineComponentScanner` | property | `ImporterOutputTypes` | SEMANTIC_EQUIVALENT | `getImporterOutputTypesProperty()` | IDictionary<string, X> is returned as const std::map<std::string, X>&. |
| `PipelineComponentScanner` | property | `ProcessorAttributes` | SEMANTIC_EQUIVALENT | `getProcessorAttributesProperty()` | IDictionary<string, X> is returned as const std::map<std::string, X>&. |
| `PipelineComponentScanner` | property | `ProcessorInputTypes` | SEMANTIC_EQUIVALENT | `getProcessorInputTypesProperty()` | IDictionary<string, X> is returned as const std::map<std::string, X>&. |
| `PipelineComponentScanner` | property | `ProcessorNames` | SEMANTIC_EQUIVALENT | `getProcessorNamesProperty()` | IEnumerable<string> is returned as std::vector<std::string>. |
| `PipelineComponentScanner` | property | `ProcessorOutputTypes` | SEMANTIC_EQUIVALENT | `getProcessorOutputTypesProperty()` | IDictionary<string, X> is returned as const std::map<std::string, X>&. |
| `PipelineComponentScanner` | property | `ProcessorParameters` | SEMANTIC_EQUIVALENT | `getProcessorParametersProperty()` | IDictionary<string, X> is returned as const std::map<std::string, X>&. |
| `PipelineComponentScanner` | method | `Update(System.Collections.Generic.IEnumerable<System.String>)` | HOST_SUBSTITUTION | `Update(const std::vector<std::string>&)` | XNA loads pipeline assemblies from the given paths; CNA loads no code dynamically, so the names select registered component catalogs and an unknown name is reported in Errors exactly as an unloadable assembly is. |
| `PipelineComponentScanner` | method | `Update(System.Collections.Generic.IEnumerable<System.String>, System.Collections.Generic.IEnumerable<System.String>)` | HOST_SUBSTITUTION | `Update(const std::vector<std::string>&, const std::vector<std::string>&)` | XNA loads pipeline assemblies from the given paths; CNA loads no code dynamically, so the names select registered component catalogs and an unknown name is reported in Errors exactly as an unloadable assembly is. |
| `PipelineException` | constructor | `.ctor()` | EXACT_EQUIVALENT | `PipelineException()` |  |
| `PipelineException` | constructor | `.ctor(System.Runtime.Serialization.SerializationInfo, System.Runtime.Serialization.StreamingContext)` | HOST_SUBSTITUTION |  | .NET binary serialization of exceptions has no C++ counterpart; the exception's developer-visible contract (message, ContentIdentity, inner exception) is provided in full. |
| `PipelineException` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `PipelineException(System.String)` |  |
| `PipelineException` | constructor | `.ctor(System.String, System.Exception)` | EXACT_EQUIVALENT | `PipelineException(System.String, System.Exception)` |  |
| `PipelineException` | constructor | `.ctor(System.String, System.Object[])` | SEMANTIC_EQUIVALENT | `PipelineException(std::string_view, Args&&...)` | params object[] becomes a variadic std::format constructor. |
| `ProcessorParameter` | property | `DefaultValue` | EXACT_EQUIVALENT | `getDefaultValueProperty()` |  |
| `ProcessorParameter` | property | `Description` | EXACT_EQUIVALENT | `getDescriptionProperty()` |  |
| `ProcessorParameter` | property | `DisplayName` | EXACT_EQUIVALENT | `getDisplayNameProperty()` |  |
| `ProcessorParameter` | property | `IsEnum` | EXACT_EQUIVALENT | `getIsEnumProperty()` |  |
| `ProcessorParameter` | property | `PossibleEnumValues` | EXACT_EQUIVALENT | `getPossibleEnumValuesProperty()` |  |
| `ProcessorParameter` | property | `PropertyName` | EXACT_EQUIVALENT | `getPropertyNameProperty()` |  |
| `ProcessorParameter` | property | `PropertyType` | EXACT_EQUIVALENT | `getPropertyTypeProperty()` |  |
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
| `ContentCompiler` | method | `GetTypeWriter(System.Type)` | EXACT_EQUIVALENT | `GetTypeWriter(System.Type)` |  |
| `ContentTypeWriter` | constructor | `.ctor(System.Type)` | EXACT_EQUIVALENT | `ContentTypeWriter(System.Type)` |  |
| `ContentTypeWriter` | property | `CanDeserializeIntoExistingObject` | EXACT_EQUIVALENT | `getCanDeserializeIntoExistingObjectProperty()` |  |
| `ContentTypeWriter` | property | `TargetType` | EXACT_EQUIVALENT | `getTargetTypeProperty()` |  |
| `ContentTypeWriter` | property | `TypeVersion` | EXACT_EQUIVALENT | `getTypeVersionProperty()` |  |
| `ContentTypeWriter` | method | `GetRuntimeReader(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform)` | EXACT_EQUIVALENT | `GetRuntimeReader(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform)` |  |
| `ContentTypeWriter` | method | `GetRuntimeType(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform)` | EXACT_EQUIVALENT | `GetRuntimeType(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform)` |  |
| `ContentTypeWriter` | method | `Initialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentCompiler)` | EXACT_EQUIVALENT | `Initialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentCompiler)` |  |
| `ContentTypeWriter` | method | `ShouldCompressContent(Microsoft.Xna.Framework.Content.Pipeline.TargetPlatform, System.Object)` | SEMANTIC_EQUIVALENT | `ShouldCompressContent(TargetPlatform, const ContentObject&)` | object is ContentObject, the canonical type-erased box. |
| `ContentTypeWriter` | method | `Write(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter, System.Object)` | SEMANTIC_EQUIVALENT | `Write(ContentWriter&, const ContentObject&)` | object is ContentObject; protected internal becomes protected plus the CNAEXT InvokeWrite the compiler drives it through. |
| `ContentTypeWriterAttribute` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentTypeWriterAttribute()` |  |
| `ContentTypeWriter<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentTypeWriter()` |  |
| `ContentTypeWriter<T>` | method | `Write(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter, System.Object)` | SEMANTIC_EQUIVALENT | `Write(ContentWriter&, const ContentObject&)` | object is ContentObject; unboxes and forwards to the typed Write. |
| `ContentTypeWriter<T>` | method | `Write(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentWriter, T)` | EXACT_EQUIVALENT | `Write(ContentWriter&, const Carrier<T>&)` |  |
| `ContentWriter` | property | `TargetPlatform` | EXACT_EQUIVALENT | `getTargetPlatformProperty()` |  |
| `ContentWriter` | property | `TargetProfile` | EXACT_EQUIVALENT | `getTargetProfileProperty()` |  |
| `ContentWriter` | method | `Dispose(System.Boolean)` | EXACT_EQUIVALENT | `Dispose(System.Boolean)` |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Color)` | EXACT_EQUIVALENT | `Write(Microsoft.Xna.Framework.Color)` |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Matrix)` | EXACT_EQUIVALENT | `Write(Microsoft.Xna.Framework.Matrix)` |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Quaternion)` | EXACT_EQUIVALENT | `Write(Microsoft.Xna.Framework.Quaternion)` |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Vector2)` | EXACT_EQUIVALENT | `Write(Microsoft.Xna.Framework.Vector2)` |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Vector3)` | EXACT_EQUIVALENT | `Write(Microsoft.Xna.Framework.Vector3)` |  |
| `ContentWriter` | method | `Write(Microsoft.Xna.Framework.Vector4)` | EXACT_EQUIVALENT | `Write(Microsoft.Xna.Framework.Vector4)` |  |
| `ContentWriter` | method | `WriteExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` | EXACT_EQUIVALENT | `WriteExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` |  |
| `ContentWriter` | method | `WriteObject<T>(T)` | EXACT_EQUIVALENT | `WriteObject<T>(T)` |  |
| `ContentWriter` | method | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentTypeWriter)` | EXACT_EQUIVALENT | `WriteObject<T>(const Carrier<T>&, ContentTypeWriterBase&)` |  |
| `ContentWriter` | method | `WriteRawObject<T>(T)` | EXACT_EQUIVALENT | `WriteRawObject<T>(T)` |  |
| `ContentWriter` | method | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler.ContentTypeWriter)` | EXACT_EQUIVALENT | `WriteRawObject<T>(const Carrier<T>&, ContentTypeWriterBase&)` |  |
| `ContentWriter` | method | `WriteSharedResource<T>(T)` | EXACT_EQUIVALENT | `WriteSharedResource<T>(T)` |  |
| `ContentTypeSerializer` | constructor | `.ctor(System.Type)` | EXACT_EQUIVALENT | `ContentTypeSerializerBase(System::Type)` |  |
| `ContentTypeSerializer` | constructor | `.ctor(System.Type, System.String)` | EXACT_EQUIVALENT | `ContentTypeSerializerBase(System::Type, std::string)` |  |
| `ContentTypeSerializer` | property | `CanDeserializeIntoExistingObject` | EXACT_EQUIVALENT | `getCanDeserializeIntoExistingObjectProperty()` |  |
| `ContentTypeSerializer` | property | `TargetType` | EXACT_EQUIVALENT | `getTargetTypeProperty()` |  |
| `ContentTypeSerializer` | property | `XmlTypeName` | EXACT_EQUIVALENT | `getXmlTypeNameProperty()` | empty string where XNA answers null |
| `ContentTypeSerializer` | method | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, System.Object)` | SEMANTIC_EQUIVALENT | `Deserialize(IntermediateReader&, const ContentSerializerAttribute&, const ContentObject&)` | object is ContentObject |
| `ContentTypeSerializer` | method | `Initialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer)` | EXACT_EQUIVALENT | `Initialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer)` |  |
| `ContentTypeSerializer` | method | `ObjectIsEmpty(System.Object)` | SEMANTIC_EQUIVALENT | `ObjectIsEmpty(const ContentObject&)` | object is ContentObject |
| `ContentTypeSerializer` | method | `ScanChildren(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer+ChildCallback, System.Object)` | SEMANTIC_EQUIVALENT | `ScanChildren(IntermediateSerializer&, const ChildCallback&, const ContentObject&)` | the delegate is a std::function; object is ContentObject |
| `ContentTypeSerializer` | method | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, System.Object, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | SEMANTIC_EQUIVALENT | `Serialize(IntermediateWriter&, const ContentObject&, const ContentSerializerAttribute&)` | object is ContentObject |
| `ContentTypeSerializer+ChildCallback` | method | `Invoke(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, System.Object)` | SEMANTIC_EQUIVALENT | `operator()(ContentTypeSerializerBase&, const ContentObject&)` | std::function call operator |
| `ContentTypeSerializerAttribute` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentTypeSerializerAttribute()` |  |
| `ContentTypeSerializer<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ContentTypeSerializer()` |  |
| `ContentTypeSerializer<T>` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `ContentTypeSerializer(System.String)` |  |
| `ContentTypeSerializer<T>` | method | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, System.Object)` | SEMANTIC_EQUIVALENT | `Deserialize(IntermediateReader&, const ContentSerializerAttribute&, const ContentObject&)` | object is ContentObject |
| `ContentTypeSerializer<T>` | method | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` | EXACT_EQUIVALENT | `Deserialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateReader, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` |  |
| `ContentTypeSerializer<T>` | method | `ObjectIsEmpty(System.Object)` | SEMANTIC_EQUIVALENT | `ObjectIsEmpty(const ContentObject&)` | object is ContentObject |
| `ContentTypeSerializer<T>` | method | `ObjectIsEmpty(T)` | EXACT_EQUIVALENT | `ObjectIsEmpty(T)` |  |
| `ContentTypeSerializer<T>` | method | `ScanChildren(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer+ChildCallback, System.Object)` | SEMANTIC_EQUIVALENT | `ScanChildren(IntermediateSerializer&, const ChildCallback&, const ContentObject&)` | the delegate is a std::function; object is ContentObject |
| `ContentTypeSerializer<T>` | method | `ScanChildren(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateSerializer, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer+ChildCallback, T)` | SEMANTIC_EQUIVALENT | `ScanChildren(IntermediateSerializer&, const ChildCallback&, const Carrier<T>&)` | the delegate is a std::function |
| `ContentTypeSerializer<T>` | method | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, System.Object, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | SEMANTIC_EQUIVALENT | `Serialize(IntermediateWriter&, const ContentObject&, const ContentSerializerAttribute&)` | object is ContentObject |
| `ContentTypeSerializer<T>` | method | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | EXACT_EQUIVALENT | `Serialize(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.IntermediateWriter, T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` |  |
| `IntermediateReader` | property | `Serializer` | EXACT_EQUIVALENT | `getSerializerProperty()` |  |
| `IntermediateReader` | property | `Xml` | EXACT_EQUIVALENT | `getXmlProperty()` |  |
| `IntermediateReader` | method | `MoveToElement(System.String)` | EXACT_EQUIVALENT | `MoveToElement(System.String)` |  |
| `IntermediateReader` | method | `ReadExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` | EXACT_EQUIVALENT | `ReadExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | EXACT_EQUIVALENT | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | EXACT_EQUIVALENT | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, T)` | EXACT_EQUIVALENT | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, T)` |  |
| `IntermediateReader` | method | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` | EXACT_EQUIVALENT | `ReadObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | EXACT_EQUIVALENT | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | EXACT_EQUIVALENT | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, T)` | EXACT_EQUIVALENT | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, T)` |  |
| `IntermediateReader` | method | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` | EXACT_EQUIVALENT | `ReadRawObject<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, T)` |  |
| `IntermediateReader` | method | `ReadSharedResource<T>(Microsoft.Xna.Framework.Content.ContentSerializerAttribute, System.Action<T>)` | SEMANTIC_EQUIVALENT | `ReadSharedResource<T>(const ContentSerializerAttribute&, std::function<void(Carrier<T>)>)` | Action<T> is a std::function |
| `IntermediateReader` | method | `ReadTypeName()` | EXACT_EQUIVALENT | `ReadTypeName()` |  |
| `IntermediateSerializer` | method | `Deserialize<T>(System.Xml.XmlReader, System.String)` | EXACT_EQUIVALENT | `Deserialize<T>(System.Xml.XmlReader, System.String)` |  |
| `IntermediateSerializer` | method | `GetTypeSerializer(System.Type)` | EXACT_EQUIVALENT | `GetTypeSerializer(System.Type)` |  |
| `IntermediateSerializer` | method | `Serialize<T>(System.Xml.XmlWriter, T, System.String)` | EXACT_EQUIVALENT | `Serialize<T>(System.Xml.XmlWriter, T, System.String)` |  |
| `IntermediateWriter` | property | `Serializer` | EXACT_EQUIVALENT | `getSerializerProperty()` |  |
| `IntermediateWriter` | property | `Xml` | EXACT_EQUIVALENT | `getXmlProperty()` |  |
| `IntermediateWriter` | method | `WriteExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` | EXACT_EQUIVALENT | `WriteExternalReference<T>(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<T>)` |  |
| `IntermediateWriter` | method | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | EXACT_EQUIVALENT | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` |  |
| `IntermediateWriter` | method | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | EXACT_EQUIVALENT | `WriteObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` |  |
| `IntermediateWriter` | method | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | EXACT_EQUIVALENT | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` |  |
| `IntermediateWriter` | method | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` | EXACT_EQUIVALENT | `WriteRawObject<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute, Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer)` |  |
| `IntermediateWriter` | method | `WriteSharedResource<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` | EXACT_EQUIVALENT | `WriteSharedResource<T>(T, Microsoft.Xna.Framework.Content.ContentSerializerAttribute)` |  |
| `IntermediateWriter` | method | `WriteTypeName(System.Type)` | EXACT_EQUIVALENT | `WriteTypeName(System.Type)` |  |
| `TargetPlatform` | enum value | `Windows = 0` | EXACT_EQUIVALENT | `TargetPlatform::Windows` |  |
| `TargetPlatform` | enum value | `Xbox360 = 1` | EXACT_EQUIVALENT | `TargetPlatform::Xbox360` |  |
| `TargetPlatform` | enum value | `WindowsPhone = 2` | EXACT_EQUIVALENT | `TargetPlatform::WindowsPhone` |  |
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
