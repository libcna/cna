# XNA 4.0 Content Pipeline parity report

> **Generated** by `tools/xna-pipeline-oracle/parity_report.py` from
> `tests/reference/xna40/content-pipeline-api.json` (the denominator, read from the genuine
> XNA Game Studio 4.0 Refresh assemblies) and `tests/reference/xna40/content-pipeline-parity-map.json`
> (CNA's answer). Do not edit by hand; edit the map and regenerate. Task log and decisions:
> `plans/plan_xnapipeline_parity.md`.

## 1. Coverage summary

| Quantity | Implemented (EXACT + SEMANTIC + HOST_SUBSTITUTION) | EXTERNAL_BLOCKED | MISSING |
|---|---|---:|---:|
| public/protected types | 127/128 (99.2%) | 0 | 1 |
| public/protected members | 703/705 (99.7%) | 0 | 2 |
| enum values | 27/27 (100.0%) | 0 | 0 |
| built-in importers | 9/10 (90.0%) | 0 | 1 |
| built-in processors | 12/12 (100.0%) | 0 | 0 |
| processor properties | 47/47 (100.0%) | 0 | 0 |
| source extensions IMPLEMENTED+TESTED | 0/18 (0.0%) | 0 | 18 |

Status vocabulary: EXACT_EQUIVALENT, SEMANTIC_EQUIVALENT (spelling differs, capability identical; note says how),
HOST_SUBSTITUTION (Microsoft-host mechanism replaced; note says how), EXTERNAL_BLOCKED (note names the
unavailable component), MISSING. Type status by value: EXACT_EQUIVALENT 44, SEMANTIC_EQUIVALENT 78, HOST_SUBSTITUTION 5, EXTERNAL_BLOCKED 0, MISSING 1.
Member status by value: EXACT_EQUIVALENT 456, SEMANTIC_EQUIVALENT 236, HOST_SUBSTITUTION 11, EXTERNAL_BLOCKED 0, MISSING 2.

Rules applied mechanically: 3 delegate plumbing members are listed in section 7 and not counted; 3 exception
serialization members are HOST_SUBSTITUTION by rule (System.Runtime.Serialization has no C++ counterpart).

## 2. Types by namespace

| Namespace | Types | Implemented | Blocked | Missing |
|---|---:|---:|---:|---:|
| `Microsoft.Xna.Framework.Content.Pipeline.Audio` | 5 | 5 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline` | 32 | 31 | 0 | 1 |
| `Microsoft.Xna.Framework.Content.Pipeline.Graphics` | 47 | 47 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline.Processors` | 28 | 28 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline.Serialization.Compiler` | 5 | 5 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate` | 7 | 7 | 0 | 0 |
| `Microsoft.Xna.Framework.Content.Pipeline.Tasks` | 4 | 4 | 0 | 0 |

## 3. Type matrix

| Namespace | XNA type | Kind | Status | CNA type | Note |
|---|---|---|---|---|---|
| `….Audio` | `AudioContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Audio::AudioContent` | the samples and the format are answered by const reference and shared pointer rather than as a ReadOnlyCollection and a reference. Every source XNA reads is read: a WAVE in its own encoding, and an MP3 or a WMA decoded to the 16-bit PCM at 44100 the genuine MP3 importer reports it will produce. |
| `….Audio` | `AudioFileType` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Audio::AudioFileType` |  |
| `….Audio` | `AudioFormat` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Audio::AudioFormat` | the native wave format is a std::vector<bytecs> rather than a ReadOnlyCollection<Byte>, which is what a read-only view of bytes is here. |
| `….Audio` | `ConversionFormat` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Audio::ConversionFormat` |  |
| `….Audio` | `ConversionQuality` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Audio::ConversionQuality` |  |
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
| `…` | `EffectImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::EffectImporter` | the effect is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `…` | `ExternalReference<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ExternalReference<T>` |  |
| `…` | `FbxImporter` | class | MISSING |  |  |
| `…` | `FontDescriptionImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::FontDescriptionImporter` | the description is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `….Graphics` | `AlphaTestMaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::AlphaTestMaterialContent` | Adds no state: every property is a view over the base's OpaqueData or Textures, verified entry by entry against tests/reference/xna40/graphics (case material/alphatest_properties or material/basic_properties). |
| `….Graphics` | `AnimationChannel` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::AnimationChannel` | Ordered by time, with Add answering the insertion index and a keyframe placed after one that already holds its time; membership is by reference. All measured (animation/channel_*). |
| `….Graphics` | `AnimationChannelDictionary` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::AnimationChannelDictionary` |  |
| `….Graphics` | `AnimationContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::AnimationContent` |  |
| `….Graphics` | `AnimationContentDictionary` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::AnimationContentDictionary` |  |
| `….Graphics` | `AnimationKeyframe` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::AnimationKeyframe` | Carries one CNAEXT member the inventory does not list: a public parameterless constructor, which the intermediate serializer needs where XNA's reflection reaches a private one. Both properties carry [ContentSerializerIgnore] in XNA and are written all the same, because the channel writes them itself (measured, animation/serialize_content). |
| `….Graphics` | `BasicMaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::BasicMaterialContent` | Adds no state: every property is a view over the base's OpaqueData or Textures, verified entry by entry against tests/reference/xna40/graphics (case material/basic_properties or material/basic_properties). |
| `….Graphics` | `BitmapContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::BitmapContent` | All 13 members present. The copy protocol runs in XNA's order -- argument validation, zero-size no-op, same-instance snapshot, destination TryCopyFrom, source TryCopyTo, then the Vector4 intermediate -- verified against tests/reference/xna40/graphics. |
| `….Graphics` | `BoneContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::BoneContent` |  |
| `….Graphics` | `BoneWeight` | struct | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::BoneWeight` | A value type here as in XNA, with the measured guards: the name may not be empty and the weight must lie in [0, 1], NaN included because neither comparison is true of it (boneweight/weight_range). Both properties are read-only and neither is serialized, so a weight is an empty <Item /> element. |
| `….Graphics` | `BoneWeightCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::BoneWeightCollection` | Carries one CNAEXT member the inventory does not list, an equality operator: a vertex channel needs its element type to be equality-comparable, and XNA's reference type compares by identity, which comparing the weights themselves agrees with wherever identity applies. |
| `….Graphics` | `DualTextureMaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::DualTextureMaterialContent` | Adds no state: every property is a view over the base's OpaqueData or Textures, verified entry by entry against tests/reference/xna40/graphics (case material/dualtexture_properties or material/basic_properties). |
| `….Graphics` | `Dxt1BitmapContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Dxt1BitmapContent` |  |
| `….Graphics` | `Dxt3BitmapContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Dxt3BitmapContent` |  |
| `….Graphics` | `Dxt5BitmapContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Dxt5BitmapContent` |  |
| `….Graphics` | `DxtBitmapContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::DxtBitmapContent` | Block storage sized as XNA sizes it (ceil(w/4) * ceil(h/4) * blockSize). Decode/Encode are CNAEXT hooks over CNA's existing BC codec; XNA reaches D3DX for the same step. |
| `….Graphics` | `EffectContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::EffectContent` | The effect source is a nullable C# string and the difference is observable, so it is a std::optional<std::string> here rather than a std::string; everything else is the direct translation. |
| `….Graphics` | `EffectMaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::EffectMaterialContent` | Adds no state: every property is a view over the base's OpaqueData or Textures, verified entry by entry against tests/reference/xna40/graphics (case material/effect_properties or material/basic_properties). |
| `….Graphics` | `EnvironmentMapMaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::EnvironmentMapMaterialContent` | Adds no state: every property is a view over the base's OpaqueData or Textures, verified entry by entry against tests/reference/xna40/graphics (case material/environmentmap_properties or material/basic_properties). |
| `….Graphics` | `FontDescription` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescription` | Every member and every refusal text is pinned to tests/reference/xna40/graphics (cases font/*), including the measured surprises: all three constructors leave UseKerning false, a NaN size and an undefined style are accepted while a size that is not greater than zero is refused, and the constructors refuse through the property setters, so their messages name the parameter `value`. One member exists here that the inventory does not list: a public parameterless constructor, marked CNAEXT. XNA has one too and keeps it private, because its serializer reaches a private constructor by reflection; C++ has none, so the serializer needs a constructor it can call. |
| `….Graphics` | `FontDescriptionStyle` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::FontDescriptionStyle` |  |
| `….Graphics` | `GeometryContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::GeometryContent` | The material is a shared resource and optional: a batch without one writes nothing and reads back without one (measured, mesh/serialize and mesh/deserialize). |
| `….Graphics` | `GeometryContentCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::GeometryContentCollection` | Carries one CNAEXT constructor the inventory does not list: a parentless one, for the same reason NodeContentCollection does. |
| `….Graphics` | `IndexCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::IndexCollection` | AddRange takes a std::vector rather than an IEnumerable, so the ArgumentNullException XNA gives for a null sequence has no counterpart (measured, indexcollection/addrange_null). |
| `….Graphics` | `IndirectPositionCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::IndirectPositionCollection` | A read-only view: the parent mesh's positions in the order the position indices name them (measured, vertexcontent/indirect_positions). |
| `….Graphics` | `MaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MaterialContent` | The five accessors XNA declares protected stay protected, and a C++ null is an empty std::optional or a null shared_ptr. Every behaviour is pinned to tests/reference/xna40/graphics (cases material/*): a property set to null removes its entry, a property whose stored value has another type reads as null rather than refusing, and an empty key is refused the way the runtime refuses a null one. |
| `….Graphics` | `MeshBuilder` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MeshBuilder` | a builder and the mesh it answers are shared pointers, which is the lifetime a .NET reference gives them. An empty name stands for XNA's null one, which the runtime accepts. |
| `….Graphics` | `MeshContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MeshContent` |  |
| `….Graphics` | `MeshHelper` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MeshHelper` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. A channel name left empty stands for XNA's null name. |
| `….Graphics` | `MipmapChain` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MipmapChain` | A Collection<BitmapContent> of shared_ptr elements; the null check XNA's InsertItem/SetItem perform is kept (ArgumentNullException, parameter name item). |
| `….Graphics` | `MipmapChainCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::MipmapChainCollection` | Carries XNA's fixed-size flag: a Texture2DContent has one face and a TextureCubeContent six, and resizing either refuses with XNA's NotSupportedException text. |
| `….Graphics` | `NodeContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::NodeContent` | The transform starts as the identity, not the zero matrix a default .NET Matrix is -- visible in what the serializer writes for an untouched node (measured, node/serialize). AbsoluteTransform composes up the chain, and a node refuses a second parent with the runtime's own InvalidOperationException, which CNA's ChildCollection now gives everywhere. |
| `….Graphics` | `NodeContentCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::NodeContentCollection` | Carries one CNAEXT constructor the inventory does not list: a parentless one, which the intermediate serializer's CreateInstance is compiled against even where reading fills the node's own collection in place. |
| `….Graphics` | `PixelBitmapContent<T>` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::PixelBitmapContent<T>` | T is constrained by a concept to the 22 pixel types XNA permits (detail/PixelTraits.hpp), where XNA discovers them through VectorConverter at run time. |
| `….Graphics` | `PositionCollection` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::PositionCollection` |  |
| `….Graphics` | `SkinnedMaterialContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::SkinnedMaterialContent` | Adds no state: every property is a view over the base's OpaqueData or Textures, verified entry by entry against tests/reference/xna40/graphics (case material/skinned_properties or material/basic_properties). |
| `….Graphics` | `Texture2DContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Texture2DContent` | The Mipmaps property is the single fixed face, so the setter takes the owning shared_ptr that replaces it rather than assigning a value; everything else is the direct translation. |
| `….Graphics` | `Texture3DContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::Texture3DContent` | The one texture whose face collection is resizable, and whose mipmap generation halves the depth as well. |
| `….Graphics` | `TextureContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::TextureContent` | Validate's refusal texts, the mipmap halving rule and ConvertBitmapType are pinned to tests/reference/xna40/graphics. |
| `….Graphics` | `TextureCubeContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::TextureCubeContent` |  |
| `….Graphics` | `TextureReferenceDictionary` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::TextureReferenceDictionary` | A NamedValueDictionary<ExternalReference<TextureContent>>, as XNA's is. |
| `….Graphics` | `VectorConverter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VectorConverter` | The 22-entry surface/vertex format table is pinned to tests/reference/xna40/graphics case by case. |
| `….Graphics` | `VertexChannel` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VertexChannelBase` | XNA's non-generic VertexChannel and its VertexChannel<T> cannot share a name in C++, so the base is VertexChannelBase -- the ContentTypeSerializerBase precedent. The object-typed members take a ContentObject, the pipeline's box. |
| `….Graphics` | `VertexChannelCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VertexChannelCollection` | Every refusal is the measured one: a duplicate name, an entry count that is not the vertex count, an unknown name (KeyNotFoundException) and a channel asked for as the wrong type (InvalidOperationException naming both types). The type-erased routes build the channel through a factory each element type registers with, where XNA reflects a VertexChannel<T> into being. |
| `….Graphics` | `VertexChannelNames` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VertexChannelNames` | Every name and refusal is pinned to tests/reference/xna40/graphics (cases vertexnames/*), including the two the API does not show: the blend weights channel is spelled `Weights`, not by its VertexElementUsage name, and a name with no trailing digits decodes to usage index 0. |
| `….Graphics` | `VertexChannel<T>` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VertexChannel<T>` | XNA's non-generic VertexChannel and its VertexChannel<T> cannot share a name in C++, so the base is VertexChannelBase -- the ContentTypeSerializerBase precedent. |
| `….Graphics` | `VertexContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Graphics::VertexContent` | Every channel follows the vertices: adding one gives each channel a default entry at that position and removing one takes it away (measured, vertexcontent/add_with_channel, insert_with_channel, remove_with_channel). CreateVertexBuffer is the one member with no equivalent yet -- it answers a VertexBufferContent, which XNAPP-134 owns. |
| `…` | `IContentImporter` | interface | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::IContentImporter` | The explicit interface implementation object IContentImporter.Import(...) is a non-virtual Import returning ContentObject, reachable only through the interface; the typed ContentImporter<T>::Import hides it exactly as C# does. |
| `…` | `IContentProcessor` | interface | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::IContentProcessor` | The explicit interface implementation object IContentProcessor.Process(object, ...) is a non-virtual Process over ContentObject, reachable only through the interface. |
| `…` | `InvalidContentException` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException` |  |
| `…` | `Mp3Importer` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Mp3Importer` | the audio is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Everything the genuine importer answers for the corpus is reproduced: the format is the decoder's own -- 16-bit PCM at 44100 whatever the source rate, with only the channel count surviving -- the duration is the whole decoded stream truncated to whole milliseconds, encoder delay and padding included, and both loop fields are 0. |
| `…` | `NamedValueDictionary<T>` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::NamedValueDictionary<T>` |  |
| `…` | `OpaqueDataDictionary` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::OpaqueDataDictionary` |  |
| `…` | `PipelineComponentScanner` | class | HOST_SUBSTITUTION | `Microsoft::Xna::Framework::Content::Pipeline::PipelineComponentScanner` | Assembly scanning has no C++ counterpart; the scanner enumerates the XNA-shaped components registered in a ContentPipelineRegistry, grouped by catalog name. |
| `…` | `PipelineException` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::PipelineException` |  |
| `…` | `ProcessorParameter` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ProcessorParameter` |  |
| `…` | `ProcessorParameterCollection` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::ProcessorParameterCollection` |  |
| `….Processors` | `CompiledEffectContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::CompiledEffectContent` | Landed with XNAPP-094 rather than XNAPP-135, because EffectMaterialContent references it. It carries one CNAEXT member the inventory does not list: a public parameterless constructor, which the intermediate serializer needs where XNA's reflection reaches a private one. |
| `….Processors` | `EffectProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessor` | XNA compiles in-process with D3DX; CNA drives the one effect compiler this repository has, the canonical CNA::Content::Pipeline::EffectCompilerService, and carries a CNAEXT constructor taking it. The observable contract is measured (effectprocessor/*): the byte code comes back as CompiledEffectContent, a refused source raises InvalidContentException beginning `Errors compiling <file>:` with the compiler's own diagnostics, and a null input is refused. Two host differences: the runtime composed that message with Environment.NewLine, where CNA writes a newline; and a compiler that is not installed is reported through the same message rather than being impossible, as it is in XNA. |
| `….Processors` | `EffectProcessorDebugMode` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessorDebugMode` |  |
| `….Processors` | `FontDescriptionProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::FontDescriptionProcessor` | Rasterizes through the canonical CNA::Content::Pipeline::RasterizeFontDescription and resolves the font the way the canonical importer does, which is a filename match rather than a family-table lookup -- so a font installed under another filename is not found where XNA would find it. The refusals are XNA's own, measured (fontprocessor/description_missing_font, /description_no_characters, /description_null). |
| `….Processors` | `FontTextureProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::FontTextureProcessor` | Defaults and the character mapping are measured (processor/FontTextureProcessor, fontprocessor/texture_character_for_index, /texture_first_character_set). What Process produces cannot be compared beyond its boundary, because SpriteFontContent publishes nothing: the two measured outcomes -- a delimited strip is accepted, a texture with no glyphs is refused with XNA's message -- are reproduced, and the glyph packing is CNA's own. |
| `….Processors` | `MaterialProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessor` | Measured against a build context that records what it is asked to build (materialprocessor/*): the material comes back as the same object with each texture reference replaced, every texture is built through `TextureProcessor` with the six parameters this processor's properties map onto, and an effect material's effect is built through `EffectProcessor` with no parameters at all. Its own defaults differ from the texture processor's: mipmaps on and DxtCompressed. |
| `….Processors` | `MaterialProcessorDefaultEffect` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessorDefaultEffect` |  |
| `….Processors` | `ModelBoneContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelBoneContent` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. The constructor and AddChild are CNAEXT, because XNA builds these only inside its own model processor and exposes no public constructor. |
| `….Processors` | `ModelBoneContentCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelBoneContentCollection` | a std::vector of shared pointers rather than a sealed ReadOnlyCollection: the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `….Processors` | `ModelContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelContent` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. The constructor is CNAEXT: XNA builds a model only inside its own processor. |
| `….Processors` | `ModelMeshContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelMeshContent` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. The constructor is CNAEXT for the same reason ModelContent's is. |
| `….Processors` | `ModelMeshContentCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelMeshContentCollection` | a std::vector of shared pointers rather than a sealed ReadOnlyCollection: the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `….Processors` | `ModelMeshPartContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelMeshPartContent` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. The constructor is CNAEXT for the same reason ModelContent's is. |
| `….Processors` | `ModelMeshPartContentCollection` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelMeshPartContentCollection` | a std::vector of shared pointers rather than a sealed ReadOnlyCollection: the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `….Processors` | `ModelProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelProcessor` | the scene it is given is transformed in place and the model it answers holds shared pointers, both of which are what XNA does with .NET references. |
| `….Processors` | `ModelTextureProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelTextureProcessor` | Mipmapped and DXT-compressed by default, which is the only difference from the texture processor (measured, processor/ModelTextureProcessor). |
| `….Processors` | `PassThroughProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::PassThroughProcessor` | An object-to-object processor, so its carrier is the pipeline's ContentObject box. |
| `….Processors` | `SongContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SongContent` | XNA declares no public member on this type; CNA's carries the converted audio behind CNAEXT accessors so its writer can reach it. |
| `….Processors` | `SongProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SongProcessor` | the audio and the song are shared pointers, which is the lifetime a .NET reference gives them; the default Quality is the measured Best. No longer EXTERNAL_BLOCKED: Microsoft's own Windows Media encoder is unavailable, but the format is not, and a song is a Windows Media file the runtime streams rather than a payload the .xnb carries. |
| `….Processors` | `SoundEffectContent` | class | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SoundEffectContent` | XNA declares no public member on this type; CNA's carries the converted audio behind CNAEXT accessors so its writer can reach it. |
| `….Processors` | `SoundEffectProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SoundEffectProcessor` | the audio and the answered content are shared pointers, which is the lifetime a .NET reference gives them. |
| `….Processors` | `SpriteFontContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SpriteFontContent` | XNA publishes no member of its own on this type (measured, fontprocessor/spritefont_content_members lists none), so what it holds is reachable here only through a CNAEXT accessor -- and what it holds is the canonical sprite-font data this repository already writes. |
| `….Processors` | `SpriteTextureProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SpriteTextureProcessor` | The texture processor's defaults exactly (measured, processor/SpriteTextureProcessor). |
| `….Processors` | `TextureProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessor` | Every default and every step is measured (processor/TextureProcessor and textureprocessor/*): the colour key runs first, then the resize, then the premultiply, then the mipmaps, and the format last; NoChange keeps the bitmap type the texture arrived with, and DxtCompressed picks Dxt1 unless a pixel is partly transparent. |
| `….Processors` | `TextureProcessorOutputFormat` | enum | EXACT_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessorOutputFormat` |  |
| `….Processors` | `VertexBufferContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::VertexBufferContent` | the vertex data is a std::vector<bytecs> rather than a Byte[], and the untyped Write takes a vector of boxed values rather than an IEnumerable. |
| `….Processors` | `VertexDeclarationContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::VertexDeclarationContent` | the nullable stride is a std::optional, which is what a Nullable<Int32> is here. |
| `….Processors` | `VideoProcessor` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::VideoProcessor` | the video is a shared pointer, which is the lifetime a .NET reference gives it. The default is the measured VideoSoundtrackType.Music, and Process answers its own input rather than a copy. |
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
| `….Tasks` | `BuildContent` | class | HOST_SUBSTITUTION | `Microsoft::Xna::Framework::Content::Pipeline::Tasks::BuildContent` | MSBuild's own hosting has no C++ counterpart: nothing discovers this task, sets its properties or calls Execute(). Everything a task author can observe is here -- every input property, every output property and the bool Execute() contract -- and the caller takes the engine's role. An item-valued property is a std::vector<TaskItem>, TaskItem being ITaskItem's developer-visible contract: an ItemSpec and a case-insensitive metadata table. The build itself is the one canonical coordinator (RunContentCompiler), so this is a translation from MSBuild's item model to that coordinator's, not a second build engine; the XNA-to-canonical component-name mapping it uses is shared with the .contentproj reader. |
| `….Tasks` | `BuildXact` | class | HOST_SUBSTITUTION | `Microsoft::Xna::Framework::Content::Pipeline::Tasks::BuildXact` | MSBuild's own hosting has no C++ counterpart: nothing discovers this task, sets its properties or calls Execute(). Everything a task author can observe is here -- every input property, every output property and the bool Execute() contract -- and the caller takes the engine's role. An item-valued property is a std::vector<TaskItem>, TaskItem being ITaskItem's developer-visible contract: an ItemSpec and a case-insensitive metadata table. Carries one CNAEXT pair the inventory does not list, SetXactCompilerEXT()/HasXactCompilerEXT(), because XNA finds XactBld3.exe through its own installation and there is none to find here. |
| `….Tasks` | `CleanContent` | class | HOST_SUBSTITUTION | `Microsoft::Xna::Framework::Content::Pipeline::Tasks::CleanContent` | MSBuild's own hosting has no C++ counterpart: nothing discovers this task, sets its properties or calls Execute(). Everything a task author can observe is here -- every input property, every output property and the bool Execute() contract -- and the caller takes the engine's role. An item-valued property is a std::vector<TaskItem>, TaskItem being ITaskItem's developer-visible contract: an ItemSpec and a case-insensitive metadata table. |
| `….Tasks` | `GetLastOutputs` | class | HOST_SUBSTITUTION | `Microsoft::Xna::Framework::Content::Pipeline::Tasks::GetLastOutputs` | MSBuild's own hosting has no C++ counterpart: nothing discovers this task, sets its properties or calls Execute(). Everything a task author can observe is here -- every input property, every output property and the bool Execute() contract -- and the caller takes the engine's role. An item-valued property is a std::vector<TaskItem>, TaskItem being ITaskItem's developer-visible contract: an ItemSpec and a case-insensitive metadata table. Carries one CNAEXT member the inventory does not list, SetOutputDirectoryEXT(): XNA keeps its incremental state under the intermediate directory and CNA's manifest lives beside the compiled output, so the caller can name that directly. |
| `…` | `TextureImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | the texture is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `…` | `VideoContent` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::VideoContent` | The constructor is eager, as the genuine one is: it probes inside the constructor, so a file that is not there, is not a video, or is named by an empty string is refused there with the one sentence XNA gives, and a null name reaches the message as an empty name rather than being refused as null. Carries one CNAEXT member the inventory does not list, HasSoundtrackEXT(), because a processor deciding what a soundtrack type means for a silent source has no other way to ask. The VideoSoundtrackType setter XNA declares internal stays public and is marked CNAEXT, because C++ has no assembly boundary. |
| `…` | `WavImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::WavImporter` | the audio is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `…` | `WmaImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::WmaImporter` | the audio is answered by shared pointer. The genuine importer could not be measured here -- Wine carries no Windows Media Format runtime, so every WMA is refused before it is opened (docs/xna-content-pipeline-media.md section 6) -- so this reads the format itself and answers the shape the MP3 measurement settled for the same SongProcessor input. |
| `…` | `WmvImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::WmvImporter` | the video is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). The refusals are the measured ones, including the split that makes a missing file a FileNotFoundException here and an InvalidContentException through VideoContent's own constructor. |
| `…` | `XImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::XImporter` | the node graph is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Both encodings the format defines for uncompressed data are read, text and binary, and thirteen corpus files are compared graph for graph against the genuine importer (tests/reference/xna40/model). The two compressed encodings, tzip and bzip, are refused by name rather than mis-read. |
| `…` | `XmlImporter` | class | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::XmlImporter` | the imported object is a ContentObject rather than a System.Object reference, which is what a boxed value of any type is here; the descriptor XNA declares through an attribute is answered by a static Attribute(). |

## 4. Importers

| XNA importer | Extensions | Default processor | Status | CNA type | Note |
|---|---|---|---|---|---|
| `EffectImporter` | .fx | `EffectProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::EffectImporter` | the effect is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `FbxImporter` | .fbx | `ModelProcessor` | MISSING |  |  |
| `FontDescriptionImporter` | .spritefont | `FontDescriptionProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::FontDescriptionImporter` | the description is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `Mp3Importer` | .mp3 | `SongProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Mp3Importer` | the audio is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Everything the genuine importer answers for the corpus is reproduced: the format is the decoder's own -- 16-bit PCM at 44100 whatever the source rate, with only the channel count surviving -- the duration is the whole decoded stream truncated to whole milliseconds, encoder delay and padding included, and both loop fields are 0. |
| `TextureImporter` | .bmp, .dds, .dib, .hdr, .jpg, .pfm, .png, .ppm, .tga | `SpriteTextureProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | the texture is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `WavImporter` | .wav | `SoundEffectProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::WavImporter` | the audio is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). |
| `WmaImporter` | .wma | `SongProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::WmaImporter` | the audio is answered by shared pointer. The genuine importer could not be measured here -- Wine carries no Windows Media Format runtime, so every WMA is refused before it is opened (docs/xna-content-pipeline-media.md section 6) -- so this reads the format itself and answers the shape the MP3 measurement settled for the same SongProcessor input. |
| `WmvImporter` | .wmv | `VideoProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::WmvImporter` | the video is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). The refusals are the measured ones, including the split that makes a missing file a FileNotFoundException here and an InvalidContentException through VideoContent's own constructor. |
| `XImporter` | .x | `ModelProcessor` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::XImporter` | the node graph is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Both encodings the format defines for uncompressed data are read, text and binary, and thirteen corpus files are compared graph for graph against the genuine importer (tests/reference/xna40/model). The two compressed encodings, tzip and bzip, are refused by name rather than mis-read. |
| `XmlImporter` | .xml | `-` | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::XmlImporter` | the imported object is a ContentObject rather than a System.Object reference, which is what a boxed value of any type is here; the descriptor XNA declares through an attribute is answered by a static Attribute(). |

## 5. Processors and properties

| XNA processor | Input | Output | Properties | Status | CNA type |
|---|---|---|---:|---|---|
| `EffectProcessor` | `EffectContent` | `CompiledEffectContent` | 2 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessor` |
| `FontDescriptionProcessor` | `FontDescription` | `SpriteFontContent` | 0 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::FontDescriptionProcessor` |
| `FontTextureProcessor` | `Texture2DContent` | `SpriteFontContent` | 3 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::FontTextureProcessor` |
| `MaterialProcessor` | `MaterialContent` | `MaterialContent` | 7 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessor` |
| `ModelProcessor` | `NodeContent` | `ModelContent` | 14 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelProcessor` |
| `ModelTextureProcessor` | `TextureContent` | `TextureContent` | 6 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelTextureProcessor` |
| `PassThroughProcessor` | `Object` | `Object` | 0 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::PassThroughProcessor` |
| `SongProcessor` | `AudioContent` | `SongContent` | 1 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SongProcessor` |
| `SoundEffectProcessor` | `AudioContent` | `SoundEffectContent` | 1 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SoundEffectProcessor` |
| `SpriteTextureProcessor` | `TextureContent` | `TextureContent` | 6 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::SpriteTextureProcessor` |
| `TextureProcessor` | `TextureContent` | `TextureContent` | 6 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessor` |
| `VideoProcessor` | `VideoContent` | `VideoContent` | 1 | SEMANTIC_EQUIVALENT | `Microsoft::Xna::Framework::Content::Pipeline::Processors::VideoProcessor` |

| Processor | Property | Type | XNA default (black-box) | Status | CNA |
|---|---|---|---|---|---|
| `EffectProcessor` | `DebugMode` | `EffectProcessorDebugMode` | `EffectProcessorDebugMode.Auto` | EXACT_EQUIVALENT | `getDebugModeProperty() / setDebugModeProperty()` |
| `EffectProcessor` | `Defines` | `String` | `None` | SEMANTIC_EQUIVALENT | `getDefinesProperty() / setDefinesProperty()` |
| `FontTextureProcessor` | `FirstCharacter` | `Char` | `'\u0020' (32)` | EXACT_EQUIVALENT | `getFirstCharacterProperty() / setFirstCharacterProperty()` |
| `FontTextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `FontTextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |
| `MaterialProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `MaterialProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `MaterialProcessor` | `DefaultEffect` | `MaterialProcessorDefaultEffect` | `MaterialProcessorDefaultEffect.BasicEffect` | EXACT_EQUIVALENT | `getDefaultEffectProperty() / setDefaultEffectProperty()` |
| `MaterialProcessor` | `GenerateMipmaps` | `Boolean` | `True` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `MaterialProcessor` | `PremultiplyTextureAlpha` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyTextureAlphaProperty() / setPremultiplyTextureAlphaProperty()` |
| `MaterialProcessor` | `ResizeTexturesToPowerOfTwo` | `Boolean` | `False` | EXACT_EQUIVALENT | `getResizeTexturesToPowerOfTwoProperty() / setResizeTexturesToPowerOfTwoProperty()` |
| `MaterialProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |
| `ModelProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty(Color)` |
| `ModelProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty(bool)` |
| `ModelProcessor` | `DefaultEffect` | `MaterialProcessorDefaultEffect` | `MaterialProcessorDefaultEffect.BasicEffect` | EXACT_EQUIVALENT | `getDefaultEffectProperty() / setDefaultEffectProperty(MaterialProcessorDefaultEffect)` |
| `ModelProcessor` | `GenerateMipmaps` | `Boolean` | `True` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty(bool)` |
| `ModelProcessor` | `GenerateTangentFrames` | `Boolean` | `False` | EXACT_EQUIVALENT | `getGenerateTangentFramesProperty() / setGenerateTangentFramesProperty(bool)` |
| `ModelProcessor` | `PremultiplyTextureAlpha` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyTextureAlphaProperty() / setPremultiplyTextureAlphaProperty(bool)` |
| `ModelProcessor` | `PremultiplyVertexColors` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyVertexColorsProperty() / setPremultiplyVertexColorsProperty(bool)` |
| `ModelProcessor` | `ResizeTexturesToPowerOfTwo` | `Boolean` | `False` | EXACT_EQUIVALENT | `getResizeTexturesToPowerOfTwoProperty() / setResizeTexturesToPowerOfTwoProperty(bool)` |
| `ModelProcessor` | `RotationX` | `Single` | `0` | EXACT_EQUIVALENT | `getRotationXProperty() / setRotationXProperty(Single)` |
| `ModelProcessor` | `RotationY` | `Single` | `0` | EXACT_EQUIVALENT | `getRotationYProperty() / setRotationYProperty(Single)` |
| `ModelProcessor` | `RotationZ` | `Single` | `0` | EXACT_EQUIVALENT | `getRotationZProperty() / setRotationZProperty(Single)` |
| `ModelProcessor` | `Scale` | `Single` | `1` | EXACT_EQUIVALENT | `getScaleProperty() / setScaleProperty(Single)` |
| `ModelProcessor` | `SwapWindingOrder` | `Boolean` | `False` | EXACT_EQUIVALENT | `getSwapWindingOrderProperty() / setSwapWindingOrderProperty(bool)` |
| `ModelProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty(TextureProcessorOutputFormat)` |
| `ModelTextureProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `ModelTextureProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `ModelTextureProcessor` | `GenerateMipmaps` | `Boolean` | `True` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `ModelTextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `ModelTextureProcessor` | `ResizeToPowerOfTwo` | `Boolean` | `False` | EXACT_EQUIVALENT | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |
| `ModelTextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |
| `SongProcessor` | `Quality` | `ConversionQuality` | `ConversionQuality.Best` | EXACT_EQUIVALENT | `getQualityProperty() / setQualityProperty(ConversionQuality)` |
| `SoundEffectProcessor` | `Quality` | `ConversionQuality` | `ConversionQuality.Best` | EXACT_EQUIVALENT | `getQualityProperty() / setQualityProperty(ConversionQuality)` |
| `SpriteTextureProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `SpriteTextureProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `SpriteTextureProcessor` | `GenerateMipmaps` | `Boolean` | `False` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `SpriteTextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `SpriteTextureProcessor` | `ResizeToPowerOfTwo` | `Boolean` | `False` | EXACT_EQUIVALENT | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |
| `SpriteTextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |
| `TextureProcessor` | `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `TextureProcessor` | `ColorKeyEnabled` | `Boolean` | `True` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `TextureProcessor` | `GenerateMipmaps` | `Boolean` | `False` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `TextureProcessor` | `PremultiplyAlpha` | `Boolean` | `True` | EXACT_EQUIVALENT | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `TextureProcessor` | `ResizeToPowerOfTwo` | `Boolean` | `False` | EXACT_EQUIVALENT | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |
| `TextureProcessor` | `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |
| `VideoProcessor` | `VideoSoundtrackType` | `VideoSoundtrackType` | `VideoSoundtrackType.Music` | EXACT_EQUIVALENT | `getVideoSoundtrackTypeProperty() / setVideoSoundtrackTypeProperty()` |

## 6. Source extensions

| Extension | XNA importer | CNA importer | Processor | Windows | Phone | Xbox | Tests | Status | Note |
|---|---|---|---|---|---|---|---|---|---|
| `.bmp` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.dds` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.dib` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.fbx` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.fx` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.hdr` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.jpg` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.mp3` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.pfm` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.png` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.ppm` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.spritefont` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.tga` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.wav` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.wma` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.wmv` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.x` | `` |  | `` |  |  |  | 0/0 | MISSING |  |
| `.xml` | `` |  | `` |  |  |  | 0/0 | MISSING |  |

## 7. Members

| XNA type | Kind | Signature | Status | CNA | Note |
|---|---|---|---|---|---|
| `AudioContent` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioFileType)` | SEMANTIC_EQUIVALENT | `AudioContent(const std::string&, AudioFileType)` | reads a WAVE in the encoding the file names, and an MP3 or a WMA decoded through the build-time media decoder to 16-bit PCM at 44100 with the source's channel count, which is what the genuine MP3 importer reports. Content-driven, as XNA's is: a source whose bytes are not of the named format is refused whatever its extension says. |
| `AudioContent` | property | `Data` | SEMANTIC_EQUIVALENT | `getDataProperty()` | a const std::vector<bytecs>& rather than a ReadOnlyCollection<byte>, which is what a read-only view of bytes is here; it throws after Dispose as XNA's does. |
| `AudioContent` | property | `Duration` | EXACT_EQUIVALENT | `getDurationProperty()` |  |
| `AudioContent` | property | `FileName` | EXACT_EQUIVALENT | `getFileNameProperty()` |  |
| `AudioContent` | property | `FileType` | EXACT_EQUIVALENT | `getFileTypeProperty()` |  |
| `AudioContent` | property | `Format` | SEMANTIC_EQUIVALENT | `getFormatProperty()` | a shared pointer, which is the lifetime a .NET reference gives it. |
| `AudioContent` | property | `LoopLength` | EXACT_EQUIVALENT | `getLoopLengthProperty()` |  |
| `AudioContent` | property | `LoopStart` | EXACT_EQUIVALENT | `getLoopStartProperty()` |  |
| `AudioContent` | method | `ConvertFormat(Microsoft.Xna.Framework.Content.Pipeline.Audio.ConversionFormat, Microsoft.Xna.Framework.Content.Pipeline.Audio.ConversionQuality, System.String)` | HOST_SUBSTITUTION | `ConvertFormat(ConversionFormat, ConversionQuality, const std::string&)` | Pcm at Best is XNA's own answer byte for byte, and the two resampled qualities match its rate, depth, channel count, byte rate, data length, loop and duration; the sample values are this host's resampler, where XNA's lives inside its native helper. Adpcm is an in-house MS-ADPCM encoder writing XNA's own block geometry. WindowsMedia writes a real Windows Media file at the target name with this host's encoder rather than Microsoft's, and refuses with XNA's own sentence when there is nowhere to write it. Xma is the one format still refused by name: its encoder ships only with the Xbox 360 tools and could not be measured. |
| `AudioContent` | method | `Dispose()` | SEMANTIC_EQUIVALENT | `Dispose()` | after it the samples throw while Duration, Format and FileName keep answering, and a second call is accepted, which is what the genuine one does. |
| `AudioContent` | method | `Finalize()` | EXACT_EQUIVALENT | `~AudioContent()` | a destructor stands for the finalizer: the samples are released when the object goes, without a garbage collector to wait for. |
| `AudioFileType` | enum value | `Wav = 0` | EXACT_EQUIVALENT | `AudioFileType::Wav` |  |
| `AudioFileType` | enum value | `Mp3 = 1` | EXACT_EQUIVALENT | `AudioFileType::Mp3` |  |
| `AudioFileType` | enum value | `Wma = 2` | EXACT_EQUIVALENT | `AudioFileType::Wma` |  |
| `AudioFormat` | property | `AverageBytesPerSecond` | EXACT_EQUIVALENT | `getAverageBytesPerSecondProperty()` |  |
| `AudioFormat` | property | `BitsPerSample` | EXACT_EQUIVALENT | `getBitsPerSampleProperty()` |  |
| `AudioFormat` | property | `BlockAlign` | EXACT_EQUIVALENT | `getBlockAlignProperty()` |  |
| `AudioFormat` | property | `ChannelCount` | EXACT_EQUIVALENT | `getChannelCountProperty()` |  |
| `AudioFormat` | property | `Format` | EXACT_EQUIVALENT | `getFormatProperty()` |  |
| `AudioFormat` | property | `NativeWaveFormat` | SEMANTIC_EQUIVALENT | `getNativeWaveFormatProperty()` | a const std::vector<bytecs>& stands for the ReadOnlyCollection<Byte>; the bytes are the same eighteen XNA answers. |
| `AudioFormat` | property | `SampleRate` | EXACT_EQUIVALENT | `getSampleRateProperty()` |  |
| `ConversionFormat` | enum value | `Pcm = 0` | EXACT_EQUIVALENT | `ConversionFormat::Pcm` |  |
| `ConversionFormat` | enum value | `Adpcm = 1` | EXACT_EQUIVALENT | `ConversionFormat::Adpcm` |  |
| `ConversionFormat` | enum value | `WindowsMedia = 2` | EXACT_EQUIVALENT | `ConversionFormat::WindowsMedia` |  |
| `ConversionFormat` | enum value | `Xma = 3` | EXACT_EQUIVALENT | `ConversionFormat::Xma` |  |
| `ConversionQuality` | enum value | `Low = 0` | EXACT_EQUIVALENT | `ConversionQuality::Low` |  |
| `ConversionQuality` | enum value | `Medium = 1` | EXACT_EQUIVALENT | `ConversionQuality::Medium` |  |
| `ConversionQuality` | enum value | `Best = 2` | EXACT_EQUIVALENT | `ConversionQuality::Best` |  |
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
| `EffectImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `EffectImporter()` |  |
| `EffectImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<EffectContent>` | the effect is answered by shared pointer; the text is read verbatim, the identity carries the file and the tool name EffectImporter, no dependency is recorded even for an include, and a missing file carries XNA's own doubled message. |
| `ExternalReference<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ExternalReference()` |  |
| `ExternalReference<T>` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `ExternalReference(System.String)` |  |
| `ExternalReference<T>` | constructor | `.ctor(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` | EXACT_EQUIVALENT | `ExternalReference(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentIdentity)` |  |
| `ExternalReference<T>` | property | `Filename` | EXACT_EQUIVALENT | `getFilenameProperty() / setFilenameProperty()` |  |
| `FbxImporter` | constructor | `.ctor()` | MISSING |  |  |
| `FbxImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | MISSING |  |  |
| `FontDescriptionImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `FontDescriptionImporter()` |  |
| `FontDescriptionImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<FontDescription>` | the description is answered by shared pointer; the document is read by the intermediate serializer under its own rules, and the identity carries the file and the tool name FontDescriptionImporter. |
| `AlphaTestMaterialContent` | constant | `AlphaFunctionKey` | SEMANTIC_EQUIVALENT | `AlphaTestMaterialContent::AlphaFunctionKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `AlphaTestMaterialContent` | constant | `AlphaKey` | SEMANTIC_EQUIVALENT | `AlphaTestMaterialContent::AlphaKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `AlphaTestMaterialContent` | constant | `DiffuseColorKey` | SEMANTIC_EQUIVALENT | `AlphaTestMaterialContent::DiffuseColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `AlphaTestMaterialContent` | constant | `ReferenceAlphaKey` | SEMANTIC_EQUIVALENT | `AlphaTestMaterialContent::ReferenceAlphaKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `AlphaTestMaterialContent` | constant | `TextureKey` | SEMANTIC_EQUIVALENT | `AlphaTestMaterialContent::TextureKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `AlphaTestMaterialContent` | constant | `VertexColorEnabledKey` | SEMANTIC_EQUIVALENT | `AlphaTestMaterialContent::VertexColorEnabledKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `AlphaTestMaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `AlphaTestMaterialContent()` |  |
| `AlphaTestMaterialContent` | property | `Alpha` | EXACT_EQUIVALENT | `getAlphaProperty() / setAlphaProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `AlphaTestMaterialContent` | property | `AlphaFunction` | EXACT_EQUIVALENT | `getAlphaFunctionProperty() / setAlphaFunctionProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `AlphaTestMaterialContent` | property | `DiffuseColor` | EXACT_EQUIVALENT | `getDiffuseColorProperty() / setDiffuseColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `AlphaTestMaterialContent` | property | `ReferenceAlpha` | EXACT_EQUIVALENT | `getReferenceAlphaProperty() / setReferenceAlphaProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `AlphaTestMaterialContent` | property | `Texture` | SEMANTIC_EQUIVALENT | `getTextureProperty() / setTextureProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `AlphaTestMaterialContent` | property | `VertexColorEnabled` | EXACT_EQUIVALENT | `getVertexColorEnabledProperty() / setVertexColorEnabledProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `AnimationChannel` | constructor | `.ctor()` | EXACT_EQUIVALENT | `AnimationChannel()` |  |
| `AnimationChannel` | property | `Count` | EXACT_EQUIVALENT | `getCountProperty()` |  |
| `AnimationChannel` | indexer | `Item[System.Int32]` | SEMANTIC_EQUIVALENT | `operator[](intcs)` | keyframes are owned and compared by reference, so they travel as std::shared_ptr. |
| `AnimationChannel` | method | `Add(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | SEMANTIC_EQUIVALENT | `Add(const std::shared_ptr<AnimationKeyframe>&)` | keyframes are owned and compared by reference, so they travel as std::shared_ptr. |
| `AnimationChannel` | method | `Clear()` | EXACT_EQUIVALENT | `Clear()` |  |
| `AnimationChannel` | method | `Contains(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | SEMANTIC_EQUIVALENT | `Contains(const std::shared_ptr<AnimationKeyframe>&)` | keyframes are owned and compared by reference, so they travel as std::shared_ptr. |
| `AnimationChannel` | method | `GetEnumerator()` | SEMANTIC_EQUIVALENT | `begin() / end()` | C++ traverses a collection with iterators, so the enumerator is the pair a range-based for loop uses. |
| `AnimationChannel` | method | `IndexOf(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | SEMANTIC_EQUIVALENT | `IndexOf(const std::shared_ptr<AnimationKeyframe>&)` | keyframes are owned and compared by reference, so they travel as std::shared_ptr. |
| `AnimationChannel` | method | `Remove(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | SEMANTIC_EQUIVALENT | `Remove(const std::shared_ptr<AnimationKeyframe>&)` | keyframes are owned and compared by reference, so they travel as std::shared_ptr. |
| `AnimationChannel` | method | `RemoveAt(System.Int32)` | EXACT_EQUIVALENT | `RemoveAt(intcs)` |  |
| `AnimationChannelDictionary` | constructor | `.ctor()` | EXACT_EQUIVALENT | `AnimationChannelDictionary()` |  |
| `AnimationContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `AnimationContent()` |  |
| `AnimationContent` | property | `Channels` | EXACT_EQUIVALENT | `getChannelsProperty()` |  |
| `AnimationContent` | property | `Duration` | EXACT_EQUIVALENT | `getDurationProperty() / setDurationProperty()` |  |
| `AnimationContentDictionary` | constructor | `.ctor()` | EXACT_EQUIVALENT | `AnimationContentDictionary()` |  |
| `AnimationKeyframe` | constructor | `.ctor(System.TimeSpan, Microsoft.Xna.Framework.Matrix)` | EXACT_EQUIVALENT | `AnimationKeyframe(System::TimeSpan, Matrix)` |  |
| `AnimationKeyframe` | property | `Time` | EXACT_EQUIVALENT | `getTimeProperty()` |  |
| `AnimationKeyframe` | property | `Transform` | EXACT_EQUIVALENT | `getTransformProperty() / setTransformProperty()` |  |
| `AnimationKeyframe` | method | `CompareTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.AnimationKeyframe)` | SEMANTIC_EQUIVALENT | `CompareTo(const AnimationKeyframe&)` | IComparable<T>.CompareTo takes a reference here rather than a nullable one; XNA's throws NullReferenceException for null (measured, animation/keyframe_compare_null), which a reference parameter makes unreachable. |
| `BasicMaterialContent` | constant | `AlphaKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::AlphaKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constant | `DiffuseColorKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::DiffuseColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constant | `EmissiveColorKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::EmissiveColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constant | `SpecularColorKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::SpecularColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constant | `SpecularPowerKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::SpecularPowerKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constant | `TextureKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::TextureKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constant | `VertexColorEnabledKey` | SEMANTIC_EQUIVALENT | `BasicMaterialContent::VertexColorEnabledKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `BasicMaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `BasicMaterialContent()` |  |
| `BasicMaterialContent` | property | `Alpha` | EXACT_EQUIVALENT | `getAlphaProperty() / setAlphaProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `BasicMaterialContent` | property | `DiffuseColor` | EXACT_EQUIVALENT | `getDiffuseColorProperty() / setDiffuseColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `BasicMaterialContent` | property | `EmissiveColor` | EXACT_EQUIVALENT | `getEmissiveColorProperty() / setEmissiveColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `BasicMaterialContent` | property | `SpecularColor` | EXACT_EQUIVALENT | `getSpecularColorProperty() / setSpecularColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `BasicMaterialContent` | property | `SpecularPower` | EXACT_EQUIVALENT | `getSpecularPowerProperty() / setSpecularPowerProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `BasicMaterialContent` | property | `Texture` | SEMANTIC_EQUIVALENT | `getTextureProperty() / setTextureProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `BasicMaterialContent` | property | `VertexColorEnabled` | EXACT_EQUIVALENT | `getVertexColorEnabledProperty() / setVertexColorEnabledProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
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
| `BoneContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `BoneContent()` |  |
| `BoneWeight` | constructor | `.ctor(System.String, System.Single)` | SEMANTIC_EQUIVALENT | `BoneWeight(std::string, Single)` | C++ has no null std::string, so an empty name carries the refusal XNA gives for a null one. |
| `BoneWeight` | property | `BoneName` | EXACT_EQUIVALENT | `getBoneNameProperty()` |  |
| `BoneWeight` | property | `Weight` | EXACT_EQUIVALENT | `getWeightProperty()` |  |
| `BoneWeightCollection` | constructor | `.ctor()` | EXACT_EQUIVALENT | `BoneWeightCollection()` |  |
| `BoneWeightCollection` | method | `NormalizeWeights()` | EXACT_EQUIVALENT | `NormalizeWeights()` | Not the maxWeights overload with Count: an empty collection refuses with the normalization message rather than the range check (measured, boneweight/collection_normalize_empty). |
| `BoneWeightCollection` | method | `NormalizeWeights(System.Int32)` | EXACT_EQUIVALENT | `NormalizeWeights(intcs)` |  |
| `DualTextureMaterialContent` | constant | `AlphaKey` | SEMANTIC_EQUIVALENT | `DualTextureMaterialContent::AlphaKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `DualTextureMaterialContent` | constant | `DiffuseColorKey` | SEMANTIC_EQUIVALENT | `DualTextureMaterialContent::DiffuseColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `DualTextureMaterialContent` | constant | `Texture2Key` | SEMANTIC_EQUIVALENT | `DualTextureMaterialContent::Texture2Key` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `DualTextureMaterialContent` | constant | `TextureKey` | SEMANTIC_EQUIVALENT | `DualTextureMaterialContent::TextureKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `DualTextureMaterialContent` | constant | `VertexColorEnabledKey` | SEMANTIC_EQUIVALENT | `DualTextureMaterialContent::VertexColorEnabledKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `DualTextureMaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `DualTextureMaterialContent()` |  |
| `DualTextureMaterialContent` | property | `Alpha` | EXACT_EQUIVALENT | `getAlphaProperty() / setAlphaProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `DualTextureMaterialContent` | property | `DiffuseColor` | EXACT_EQUIVALENT | `getDiffuseColorProperty() / setDiffuseColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `DualTextureMaterialContent` | property | `Texture` | SEMANTIC_EQUIVALENT | `getTextureProperty() / setTextureProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `DualTextureMaterialContent` | property | `Texture2` | SEMANTIC_EQUIVALENT | `getTexture2Property() / setTexture2Property()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `DualTextureMaterialContent` | property | `VertexColorEnabled` | EXACT_EQUIVALENT | `getVertexColorEnabledProperty() / setVertexColorEnabledProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
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
| `EffectContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `EffectContent()` |  |
| `EffectContent` | property | `EffectCode` | SEMANTIC_EQUIVALENT | `getEffectCodeProperty() / setEffectCodeProperty()` | the nullable C# string is a std::optional<std::string>, because the difference is observable: an effect with no source serializes as <EffectCode Null="true" /> (measured, effectcontent/serialize_null_code), which a plain std::string could not express. |
| `EffectMaterialContent` | constant | `CompiledEffectKey` | SEMANTIC_EQUIVALENT | `EffectMaterialContent::CompiledEffectKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EffectMaterialContent` | constant | `EffectKey` | SEMANTIC_EQUIVALENT | `EffectMaterialContent::EffectKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EffectMaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `EffectMaterialContent()` |  |
| `EffectMaterialContent` | property | `CompiledEffect` | SEMANTIC_EQUIVALENT | `getCompiledEffectProperty() / setCompiledEffectProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `EffectMaterialContent` | property | `Effect` | SEMANTIC_EQUIVALENT | `getEffectProperty() / setEffectProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | constant | `AlphaKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::AlphaKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `DiffuseColorKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::DiffuseColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `EmissiveColorKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::EmissiveColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `EnvironmentMapAmountKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::EnvironmentMapAmountKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `EnvironmentMapKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::EnvironmentMapKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `EnvironmentMapSpecularKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::EnvironmentMapSpecularKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `FresnelFactorKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::FresnelFactorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constant | `TextureKey` | SEMANTIC_EQUIVALENT | `EnvironmentMapMaterialContent::TextureKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `EnvironmentMapMaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `EnvironmentMapMaterialContent()` |  |
| `EnvironmentMapMaterialContent` | property | `Alpha` | EXACT_EQUIVALENT | `getAlphaProperty() / setAlphaProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `DiffuseColor` | EXACT_EQUIVALENT | `getDiffuseColorProperty() / setDiffuseColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `EmissiveColor` | EXACT_EQUIVALENT | `getEmissiveColorProperty() / setEmissiveColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `EnvironmentMap` | SEMANTIC_EQUIVALENT | `getEnvironmentMapProperty() / setEnvironmentMapProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `EnvironmentMapAmount` | EXACT_EQUIVALENT | `getEnvironmentMapAmountProperty() / setEnvironmentMapAmountProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `EnvironmentMapSpecular` | EXACT_EQUIVALENT | `getEnvironmentMapSpecularProperty() / setEnvironmentMapSpecularProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `FresnelFactor` | EXACT_EQUIVALENT | `getFresnelFactorProperty() / setFresnelFactorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `EnvironmentMapMaterialContent` | property | `Texture` | SEMANTIC_EQUIVALENT | `getTextureProperty() / setTextureProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
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
| `GeometryContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `GeometryContent()` |  |
| `GeometryContent` | property | `Indices` | EXACT_EQUIVALENT | `getIndicesProperty()` |  |
| `GeometryContent` | property | `Material` | SEMANTIC_EQUIVALENT | `getMaterialProperty() / setMaterialProperty()` | the material is an owned shared_ptr, and dispatch on its dynamic type is what puts a BasicMaterialContent in the document. |
| `GeometryContent` | property | `Parent` | SEMANTIC_EQUIVALENT | `getParentProperty() -> MeshContent*` | the parent back-reference is a raw pointer, valid while the batch is in that mesh's collection. |
| `GeometryContent` | property | `Vertices` | EXACT_EQUIVALENT | `getVerticesProperty()` |  |
| `GeometryContentCollection` | method | `GetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent)` | SEMANTIC_EQUIVALENT | `GetParent(const std::shared_ptr<GeometryContent>&) -> MeshContent*` | children are owned and shared, so they travel as std::shared_ptr; the parent back-reference is a raw pointer valid while the child is in the collection, the carrier rule ChildCollection already sets. |
| `GeometryContentCollection` | method | `SetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | SEMANTIC_EQUIVALENT | `SetParent(const std::shared_ptr<GeometryContent>&, MeshContent*)` | children are owned and shared, so they travel as std::shared_ptr; the parent back-reference is a raw pointer valid while the child is in the collection, the carrier rule ChildCollection already sets. |
| `IndexCollection` | constructor | `.ctor()` | EXACT_EQUIVALENT | `IndexCollection()` |  |
| `IndexCollection` | method | `AddRange(System.Collections.Generic.IEnumerable<System.Int32>)` | SEMANTIC_EQUIVALENT | `AddRange(const std::vector<intcs>&)` | IEnumerable<int> is a std::vector<intcs>, which cannot be null. |
| `IndirectPositionCollection` | property | `Count` | EXACT_EQUIVALENT | `getCountProperty()` |  |
| `IndirectPositionCollection` | indexer | `Item[System.Int32]` | EXACT_EQUIVALENT | `operator[](intcs)` |  |
| `IndirectPositionCollection` | method | `Contains(Microsoft.Xna.Framework.Vector3)` | EXACT_EQUIVALENT | `Contains(const Vector3&)` |  |
| `IndirectPositionCollection` | method | `CopyTo(Microsoft.Xna.Framework.Vector3[], System.Int32)` | SEMANTIC_EQUIVALENT | `CopyTo(std::vector<Vector3>&, intcs)` | Vector3[] is a std::vector<Vector3>. |
| `IndirectPositionCollection` | method | `GetEnumerator()` | SEMANTIC_EQUIVALENT | `operator[](intcs) over getCountProperty()` | the view is traversed by index, as it computes each position on demand. |
| `IndirectPositionCollection` | method | `IndexOf(Microsoft.Xna.Framework.Vector3)` | EXACT_EQUIVALENT | `IndexOf(const Vector3&)` |  |
| `MaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `MaterialContent()` |  |
| `MaterialContent` | property | `Textures` | EXACT_EQUIVALENT | `getTexturesProperty()` |  |
| `MaterialContent` | method | `GetReferenceTypeProperty<T>(System.String)` | SEMANTIC_EQUIVALENT | `GetReferenceTypeProperty<T>(const std::string&) -> std::shared_ptr<T>` | a reference travels as a shared_ptr, and a missing or mistyped entry answers null as XNA's does. |
| `MaterialContent` | method | `GetTexture(System.String)` | SEMANTIC_EQUIVALENT | `GetTexture(const std::string&) -> std::shared_ptr<ExternalReference<TextureContent>>` | the external reference is an owned shared_ptr; null when the slot is empty. |
| `MaterialContent` | method | `GetValueTypeProperty<T>(System.String)` | EXACT_EQUIVALENT | `GetValueTypeProperty<T>(const std::string&) -> std::optional<T>` | Nullable<T> is std::optional<T>. |
| `MaterialContent` | method | `SetProperty<T>(System.String, T)` | SEMANTIC_EQUIVALENT | `SetProperty<T>(const std::string&, const T&)` | T is the value's carrier -- std::optional for a value type, std::shared_ptr for a reference -- and an empty one removes the entry, which is what passing null does in XNA. |
| `MaterialContent` | method | `SetTexture(System.String, Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent>)` | SEMANTIC_EQUIVALENT | `SetTexture(const std::string&, const std::shared_ptr<ExternalReference<TextureContent>>&)` | a null pointer removes the slot, as null does in XNA. |
| `MeshBuilder` | property | `MergeDuplicatePositions` | EXACT_EQUIVALENT | `getMergeDuplicatePositionsProperty() / setMergeDuplicatePositionsProperty(bool)` |  |
| `MeshBuilder` | property | `MergePositionTolerance` | EXACT_EQUIVALENT | `getMergePositionToleranceProperty() / setMergePositionToleranceProperty(Single)` |  |
| `MeshBuilder` | property | `Name` | EXACT_EQUIVALENT | `getNameProperty() / setNameProperty(std::string)` |  |
| `MeshBuilder` | property | `SwapWindingOrder` | EXACT_EQUIVALENT | `getSwapWindingOrderProperty() / setSwapWindingOrderProperty(bool)` |  |
| `MeshBuilder` | method | `AddTriangleVertex(System.Int32)` | EXACT_EQUIVALENT | `AddTriangleVertex(intcs)` |  |
| `MeshBuilder` | method | `CreatePosition(Microsoft.Xna.Framework.Vector3)` | EXACT_EQUIVALENT | `CreatePosition(const Vector3&)` |  |
| `MeshBuilder` | method | `CreatePosition(System.Single, System.Single, System.Single)` | EXACT_EQUIVALENT | `CreatePosition(Single, Single, Single)` |  |
| `MeshBuilder` | method | `CreateVertexChannel<T>(System.String)` | EXACT_EQUIVALENT | `CreateVertexChannel<T>(const std::string&)` |  |
| `MeshBuilder` | method | `FinishMesh()` | SEMANTIC_EQUIVALENT | `FinishMesh() -> std::shared_ptr<MeshContent>` | a builder and the mesh it answers are shared pointers, which is the lifetime a .NET reference gives them. Finishing twice answers the same mesh, as XNA's does. |
| `MeshBuilder` | method | `SetMaterial(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent)` | SEMANTIC_EQUIVALENT | `SetMaterial(std::shared_ptr<MaterialContent>)` | a builder and the mesh it answers are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshBuilder` | method | `SetOpaqueData(Microsoft.Xna.Framework.Content.Pipeline.OpaqueDataDictionary)` | SEMANTIC_EQUIVALENT | `SetOpaqueData(const OpaqueDataDictionary*)` | the dictionary is borrowed rather than retained, and its entries are copied into the batch; a null pointer stands for XNA's null, which the runtime accepts. |
| `MeshBuilder` | method | `SetVertexChannelData(System.Int32, System.Object)` | SEMANTIC_EQUIVALENT | `SetVertexChannelData(intcs, const ContentObject&)` | the boxed value is a ContentObject, which is what System.Object is here. |
| `MeshBuilder` | method | `StartMesh(System.String)` | SEMANTIC_EQUIVALENT | `StartMesh(const std::string&) -> std::shared_ptr<MeshBuilder>` | a builder and the mesh it answers are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `MeshContent()` |  |
| `MeshContent` | property | `Geometry` | EXACT_EQUIVALENT | `getGeometryProperty()` |  |
| `MeshContent` | property | `Positions` | EXACT_EQUIVALENT | `getPositionsProperty()` |  |
| `MeshHelper` | method | `CalculateNormals(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent, System.Boolean)` | SEMANTIC_EQUIVALENT | `CalculateNormals(const std::shared_ptr<MeshContent>&, bool)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `CalculateTangentFrames(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent, System.String, System.String, System.String)` | SEMANTIC_EQUIVALENT | `CalculateTangentFrames(const std::shared_ptr<MeshContent>&, const std::string&, const std::string&, const std::string&)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. An empty channel name asks for that half of the frame not to be written, as XNA's null does. |
| `MeshHelper` | method | `FindSkeleton(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | SEMANTIC_EQUIVALENT | `FindSkeleton(const std::shared_ptr<NodeContent>&) -> std::shared_ptr<BoneContent>` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `FlattenSkeleton(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BoneContent)` | SEMANTIC_EQUIVALENT | `FlattenSkeleton(const std::shared_ptr<BoneContent>&) -> std::vector<std::shared_ptr<BoneContent>>` | the list is a std::vector of shared pointers, which is what an IList<BoneContent> is here. |
| `MeshHelper` | method | `MergeDuplicatePositions(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent, System.Single)` | SEMANTIC_EQUIVALENT | `MergeDuplicatePositions(const std::shared_ptr<MeshContent>&, Single)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `MergeDuplicateVertices(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent)` | SEMANTIC_EQUIVALENT | `MergeDuplicateVertices(const std::shared_ptr<GeometryContent>&)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `MergeDuplicateVertices(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | SEMANTIC_EQUIVALENT | `MergeDuplicateVertices(const std::shared_ptr<MeshContent>&)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `OptimizeForCache(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | SEMANTIC_EQUIVALENT | `OptimizeForCache(const std::shared_ptr<MeshContent>&)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `SwapWindingOrder(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshContent)` | SEMANTIC_EQUIVALENT | `SwapWindingOrder(const std::shared_ptr<MeshContent>&)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MeshHelper` | method | `TransformScene(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Matrix)` | SEMANTIC_EQUIVALENT | `TransformScene(const std::shared_ptr<NodeContent>&, const Matrix&)` | the mesh, batch and node are shared pointers, which is the lifetime a .NET reference gives them. |
| `MipmapChain` | constructor | `.ctor()` | EXACT_EQUIVALENT | `MipmapChain()` |  |
| `MipmapChain` | constructor | `.ctor(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `MipmapChain(std::shared_ptr<BitmapContent>)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `MipmapChain` | method | `InsertItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `InsertItem(intcs, const std::shared_ptr<BitmapContent>&)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `MipmapChain` | method | `SetItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `SetItem(intcs, const std::shared_ptr<BitmapContent>&)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `MipmapChain` | operator | `op_Implicit(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent)` | SEMANTIC_EQUIVALENT | `MipmapChain(std::shared_ptr<BitmapContent>)` | C#'s implicit conversion operator is C++'s converting constructor, which is what makes chain = bitmap compile in both. |
| `MipmapChainCollection` | method | `ClearItems()` | EXACT_EQUIVALENT | `ClearItems()` |  |
| `MipmapChainCollection` | method | `InsertItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain)` | SEMANTIC_EQUIVALENT | `InsertItem(intcs, const std::shared_ptr<MipmapChain>&)` | elements are shared_ptr carriers, as everywhere a chain is owned and shared. |
| `MipmapChainCollection` | method | `RemoveItem(System.Int32)` | EXACT_EQUIVALENT | `RemoveItem(intcs)` |  |
| `MipmapChainCollection` | method | `SetItem(System.Int32, Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain)` | SEMANTIC_EQUIVALENT | `SetItem(intcs, const std::shared_ptr<MipmapChain>&)` | elements are shared_ptr carriers, as everywhere a chain is owned and shared. |
| `NodeContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `NodeContent()` |  |
| `NodeContent` | property | `AbsoluteTransform` | EXACT_EQUIVALENT | `getAbsoluteTransformProperty()` |  |
| `NodeContent` | property | `Animations` | EXACT_EQUIVALENT | `getAnimationsProperty()` |  |
| `NodeContent` | property | `Children` | EXACT_EQUIVALENT | `getChildrenProperty()` |  |
| `NodeContent` | property | `Parent` | SEMANTIC_EQUIVALENT | `getParentProperty() -> NodeContent*` | the parent back-reference is a raw pointer, valid while the node is in that parent's collection. |
| `NodeContent` | property | `Transform` | EXACT_EQUIVALENT | `getTransformProperty() / setTransformProperty()` |  |
| `NodeContentCollection` | method | `GetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | SEMANTIC_EQUIVALENT | `GetParent(const std::shared_ptr<NodeContent>&) -> NodeContent*` | children are owned and shared, so they travel as std::shared_ptr; the parent back-reference is a raw pointer valid while the child is in the collection, the carrier rule ChildCollection already sets. |
| `NodeContentCollection` | method | `SetParent(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent)` | SEMANTIC_EQUIVALENT | `SetParent(const std::shared_ptr<NodeContent>&, NodeContent*)` | children are owned and shared, so they travel as std::shared_ptr; the parent back-reference is a raw pointer valid while the child is in the collection, the carrier rule ChildCollection already sets. |
| `PixelBitmapContent<T>` | constructor | `.ctor()` | EXACT_EQUIVALENT | `PixelBitmapContent()` |  |
| `PixelBitmapContent<T>` | constructor | `.ctor(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `PixelBitmapContent(intcs, intcs)` |  |
| `PixelBitmapContent<T>` | method | `GetPixel(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `GetPixel(intcs, intcs)` |  |
| `PixelBitmapContent<T>` | method | `GetPixelData()` | EXACT_EQUIVALENT | `GetPixelData()` |  |
| `PixelBitmapContent<T>` | method | `GetRow(System.Int32)` | EXACT_EQUIVALENT | `GetRow(intcs) -> std::span<T> / std::span<const T>` | XNA returns the bitmap's own row array, so writing through it changes the bitmap (measured, color/get_row_is_live). A std::span keeps that aliasing where a std::vector copy would silently lose it. |
| `PixelBitmapContent<T>` | method | `ReplaceColor(T, T)` | EXACT_EQUIVALENT | `ReplaceColor(const T&, const T&)` |  |
| `PixelBitmapContent<T>` | method | `SetPixel(System.Int32, System.Int32, T)` | EXACT_EQUIVALENT | `SetPixel(intcs, intcs, const T&)` |  |
| `PixelBitmapContent<T>` | method | `SetPixelData(System.Byte[])` | EXACT_EQUIVALENT | `SetPixelData(const std::vector<bytecs>&)` |  |
| `PixelBitmapContent<T>` | method | `ToString()` | EXACT_EQUIVALENT | `ToString()` | Inherited from BitmapContent, which composes the display name a virtual TypeDisplayName() supplies; C++ has no run-time generic type name to format as XNA's override does. The text matches (PixelBitmapContent<Color>, 3x2). |
| `PixelBitmapContent<T>` | method | `TryCopyFrom(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | EXACT_EQUIVALENT | `TryCopyFrom(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `PixelBitmapContent<T>` | method | `TryCopyTo(Microsoft.Xna.Framework.Content.Pipeline.Graphics.BitmapContent, Microsoft.Xna.Framework.Rectangle, Microsoft.Xna.Framework.Rectangle)` | EXACT_EQUIVALENT | `TryCopyTo(const std::shared_ptr<BitmapContent>&, Rectangle, Rectangle)` | reference parameters are shared_ptr carriers: a BitmapContent is polymorphic and owned, so CNA passes std::shared_ptr<BitmapContent> where XNA passes the reference itself. |
| `PixelBitmapContent<T>` | method | `TryGetFormat(out Microsoft.Xna.Framework.Graphics.SurfaceFormat)` | EXACT_EQUIVALENT | `TryGetFormat(SurfaceFormat&)` |  |
| `PositionCollection` | constructor | `.ctor()` | EXACT_EQUIVALENT | `PositionCollection()` |  |
| `SkinnedMaterialContent` | constant | `AlphaKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::AlphaKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constant | `DiffuseColorKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::DiffuseColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constant | `EmissiveColorKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::EmissiveColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constant | `SpecularColorKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::SpecularColorKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constant | `SpecularPowerKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::SpecularPowerKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constant | `TextureKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::TextureKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constant | `WeightsPerVertexKey` | SEMANTIC_EQUIVALENT | `SkinnedMaterialContent::WeightsPerVertexKey` | a C# `const string` is a `static constexpr std::string_view`, with the same value and the same compile-time constancy. |
| `SkinnedMaterialContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `SkinnedMaterialContent()` |  |
| `SkinnedMaterialContent` | property | `Alpha` | EXACT_EQUIVALENT | `getAlphaProperty() / setAlphaProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `SkinnedMaterialContent` | property | `DiffuseColor` | EXACT_EQUIVALENT | `getDiffuseColorProperty() / setDiffuseColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `SkinnedMaterialContent` | property | `EmissiveColor` | EXACT_EQUIVALENT | `getEmissiveColorProperty() / setEmissiveColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `SkinnedMaterialContent` | property | `SpecularColor` | EXACT_EQUIVALENT | `getSpecularColorProperty() / setSpecularColorProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `SkinnedMaterialContent` | property | `SpecularPower` | EXACT_EQUIVALENT | `getSpecularPowerProperty() / setSpecularPowerProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
| `SkinnedMaterialContent` | property | `Texture` | SEMANTIC_EQUIVALENT | `getTextureProperty() / setTextureProperty()` | the external reference is an owned shared_ptr; a null one removes the entry, as measured. |
| `SkinnedMaterialContent` | property | `WeightsPerVertex` | EXACT_EQUIVALENT | `getWeightsPerVertexProperty() / setWeightsPerVertexProperty()` | Nullable<T> is std::optional<T>; an empty optional is the null XNA stores nothing for: setting the property to it removes the entry, as measured. |
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
| `VertexChannel` | property | `Count` | EXACT_EQUIVALENT | `getCountProperty()` |  |
| `VertexChannel` | property | `ElementType` | EXACT_EQUIVALENT | `getElementTypeProperty()` |  |
| `VertexChannel` | property | `Name` | EXACT_EQUIVALENT | `getNameProperty()` |  |
| `VertexChannel` | indexer | `Item[System.Int32]` | SEMANTIC_EQUIVALENT | `operator[](intcs) -> ContentObject` | the object indexer answers the pipeline's box. |
| `VertexChannel` | method | `Contains(System.Object)` | SEMANTIC_EQUIVALENT | `Contains(const ContentObject&)` | object is ContentObject. |
| `VertexChannel` | method | `CopyTo(System.Array, System.Int32)` | SEMANTIC_EQUIVALENT | `CopyTo(std::vector<ContentObject>&, intcs)` | System.Array is a std::vector of boxes. |
| `VertexChannel` | method | `GetEnumerator()` | SEMANTIC_EQUIVALENT | `operator[](intcs) over getCountProperty()` | the non-generic channel is traversed by index; VertexChannel<T> exposes its entries directly. |
| `VertexChannel` | method | `IndexOf(System.Object)` | SEMANTIC_EQUIVALENT | `IndexOf(const ContentObject&)` | object is ContentObject. |
| `VertexChannel` | method | `ReadConvertedContent<TargetType>()` | SEMANTIC_EQUIVALENT | `ReadConvertedContent<TargetType>() -> std::vector<TargetType>` | the conversion runs through Vector4, which is what the pipeline's VectorConverter does; IEnumerable<T> is a std::vector. |
| `VertexChannelCollection` | property | `Count` | EXACT_EQUIVALENT | `getCountProperty()` |  |
| `VertexChannelCollection` | indexer | `Item[System.Int32]` | SEMANTIC_EQUIVALENT | `operator[](intcs)` | channels are owned and shared, so they travel as std::shared_ptr. |
| `VertexChannelCollection` | indexer | `Item[System.String]` | SEMANTIC_EQUIVALENT | `operator[](const std::string&)` | channels are owned and shared, so they travel as std::shared_ptr. |
| `VertexChannelCollection` | method | `Add(System.String, System.Type, System.Collections.IEnumerable)` | SEMANTIC_EQUIVALENT | `Add(const std::string&, System::Type, const std::vector<ContentObject>&)` | the untyped sequence is a std::vector of boxes, and the channel comes from the element-type factory. |
| `VertexChannelCollection` | method | `Add<ElementType>(System.String, System.Collections.Generic.IEnumerable<ElementType>)` | SEMANTIC_EQUIVALENT | `Add<ElementType>(const std::string&, std::vector<ElementType>)` | IEnumerable<T> is a std::vector<T>, which cannot be null; an empty one is XNA's null (measured, vertexcontent/channel_add_null_data). |
| `VertexChannelCollection` | method | `Clear()` | EXACT_EQUIVALENT | `Clear()` |  |
| `VertexChannelCollection` | method | `Contains(Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel)` | SEMANTIC_EQUIVALENT | `Contains(const std::shared_ptr<VertexChannelBase>&)` | channels travel as std::shared_ptr. |
| `VertexChannelCollection` | method | `Contains(System.String)` | EXACT_EQUIVALENT | `Contains(const std::string&)` |  |
| `VertexChannelCollection` | method | `ConvertChannelContent<TargetType>(System.Int32)` | EXACT_EQUIVALENT | `ConvertChannelContent<TargetType>(intcs)` |  |
| `VertexChannelCollection` | method | `ConvertChannelContent<TargetType>(System.String)` | EXACT_EQUIVALENT | `ConvertChannelContent<TargetType>(const std::string&)` |  |
| `VertexChannelCollection` | method | `Get<T>(System.Int32)` | SEMANTIC_EQUIVALENT | `Get<T>(intcs)` | answers a std::shared_ptr<VertexChannel<T>>. |
| `VertexChannelCollection` | method | `Get<T>(System.String)` | SEMANTIC_EQUIVALENT | `Get<T>(const std::string&)` | answers a std::shared_ptr<VertexChannel<T>>. |
| `VertexChannelCollection` | method | `GetEnumerator()` | SEMANTIC_EQUIVALENT | `begin() / end()` | C++ traverses a collection with iterators. |
| `VertexChannelCollection` | method | `IndexOf(Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel)` | SEMANTIC_EQUIVALENT | `IndexOf(const std::shared_ptr<VertexChannelBase>&)` | channels travel as std::shared_ptr. |
| `VertexChannelCollection` | method | `IndexOf(System.String)` | EXACT_EQUIVALENT | `IndexOf(const std::string&)` |  |
| `VertexChannelCollection` | method | `Insert(System.Int32, System.String, System.Type, System.Collections.IEnumerable)` | SEMANTIC_EQUIVALENT | `Insert(intcs, const std::string&, System::Type, const std::vector<ContentObject>&)` | the untyped sequence is a std::vector of boxes, and the channel comes from the element-type factory. |
| `VertexChannelCollection` | method | `Insert<ElementType>(System.Int32, System.String, System.Collections.Generic.IEnumerable<ElementType>)` | SEMANTIC_EQUIVALENT | `Insert<ElementType>(intcs, const std::string&, std::vector<ElementType>)` | IEnumerable<T> is a std::vector<T>. |
| `VertexChannelCollection` | method | `Remove(Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannel)` | SEMANTIC_EQUIVALENT | `Remove(const std::shared_ptr<VertexChannelBase>&)` | channels travel as std::shared_ptr. |
| `VertexChannelCollection` | method | `Remove(System.String)` | EXACT_EQUIVALENT | `Remove(const std::string&)` |  |
| `VertexChannelCollection` | method | `RemoveAt(System.Int32)` | EXACT_EQUIVALENT | `RemoveAt(intcs)` |  |
| `VertexChannelNames` | method | `Binormal(System.Int32)` | EXACT_EQUIVALENT | `Binormal(intcs)` |  |
| `VertexChannelNames` | method | `Color(System.Int32)` | EXACT_EQUIVALENT | `Color(intcs)` |  |
| `VertexChannelNames` | method | `DecodeBaseName(System.String)` | SEMANTIC_EQUIVALENT | `DecodeBaseName(const std::string&)` | C++ has no null std::string, so an empty name carries the refusal XNA gives for a null one. |
| `VertexChannelNames` | method | `DecodeUsageIndex(System.String)` | SEMANTIC_EQUIVALENT | `DecodeUsageIndex(const std::string&)` | C++ has no null std::string, so an empty name carries the refusal XNA gives for a null one. |
| `VertexChannelNames` | method | `EncodeName(Microsoft.Xna.Framework.Graphics.VertexElementUsage, System.Int32)` | EXACT_EQUIVALENT | `EncodeName(VertexElementUsage, intcs)` |  |
| `VertexChannelNames` | method | `EncodeName(System.String, System.Int32)` | SEMANTIC_EQUIVALENT | `EncodeName(const std::string&, intcs)` | C++ has no null std::string, so an empty name carries the refusal XNA gives for a null one. |
| `VertexChannelNames` | method | `Normal()` | EXACT_EQUIVALENT | `Normal()` |  |
| `VertexChannelNames` | method | `Normal(System.Int32)` | EXACT_EQUIVALENT | `Normal(intcs)` |  |
| `VertexChannelNames` | method | `Tangent(System.Int32)` | EXACT_EQUIVALENT | `Tangent(intcs)` |  |
| `VertexChannelNames` | method | `TextureCoordinate(System.Int32)` | EXACT_EQUIVALENT | `TextureCoordinate(intcs)` |  |
| `VertexChannelNames` | method | `TryDecodeUsage(System.String, out Microsoft.Xna.Framework.Graphics.VertexElementUsage)` | SEMANTIC_EQUIVALENT | `TryDecodeUsage(const std::string&, VertexElementUsage&)` | C++ has no null std::string, so an empty name carries the refusal XNA gives for a null one. The out parameter is a reference, left at Position when the name names no usage, as measured. |
| `VertexChannelNames` | method | `Weights()` | EXACT_EQUIVALENT | `Weights()` |  |
| `VertexChannelNames` | method | `Weights(System.Int32)` | EXACT_EQUIVALENT | `Weights(intcs)` |  |
| `VertexChannel<T>` | property | `ElementType` | EXACT_EQUIVALENT | `getElementTypeProperty()` |  |
| `VertexChannel<T>` | indexer | `Item[System.Int32]` | SEMANTIC_EQUIVALENT | `At(intcs) / SetAt(intcs, const T&)` | C++ cannot overload an indexer by return type against the base's boxed one, so the typed access is named. |
| `VertexChannel<T>` | method | `Contains(T)` | EXACT_EQUIVALENT | `Contains(const T&)` |  |
| `VertexChannel<T>` | method | `CopyTo(T[], System.Int32)` | SEMANTIC_EQUIVALENT | `CopyTo(std::vector<T>&, intcs)` | T[] is a std::vector<T>. |
| `VertexChannel<T>` | method | `GetEnumerator()` | SEMANTIC_EQUIVALENT | `Items()` | the entries themselves, which a range-based for loop traverses. |
| `VertexChannel<T>` | method | `IndexOf(T)` | EXACT_EQUIVALENT | `IndexOf(const T&)` |  |
| `VertexChannel<T>` | method | `ReadConvertedContent<TargetType>()` | SEMANTIC_EQUIVALENT | `ReadConvertedContent<TargetType>()` | inherited from the base; IEnumerable<T> is a std::vector. |
| `VertexContent` | property | `Channels` | EXACT_EQUIVALENT | `getChannelsProperty()` |  |
| `VertexContent` | property | `PositionIndices` | EXACT_EQUIVALENT | `getPositionIndicesProperty()` |  |
| `VertexContent` | property | `Positions` | EXACT_EQUIVALENT | `getPositionsProperty()` |  |
| `VertexContent` | property | `VertexCount` | EXACT_EQUIVALENT | `getVertexCountProperty()` |  |
| `VertexContent` | method | `Add(System.Int32)` | EXACT_EQUIVALENT | `Add(intcs)` |  |
| `VertexContent` | method | `AddRange(System.Collections.Generic.IEnumerable<System.Int32>)` | SEMANTIC_EQUIVALENT | `AddRange(const std::vector<intcs>&)` | IEnumerable<int> is a std::vector<intcs>, which cannot be null. |
| `VertexContent` | method | `CreateVertexBuffer()` | SEMANTIC_EQUIVALENT | `CreateVertexBuffer() -> std::shared_ptr<VertexBufferContent>` | the buffer is answered by shared pointer, which is the lifetime a .NET reference gives it; its layout is position first and then every channel a vertex element can carry, in channel order (measured, modelprocessor/triangle). |
| `VertexContent` | method | `Insert(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `Insert(intcs, intcs)` |  |
| `VertexContent` | method | `InsertRange(System.Int32, System.Collections.Generic.IEnumerable<System.Int32>)` | SEMANTIC_EQUIVALENT | `InsertRange(intcs, const std::vector<intcs>&)` | IEnumerable<int> is a std::vector<intcs>, which cannot be null. |
| `VertexContent` | method | `RemoveAt(System.Int32)` | EXACT_EQUIVALENT | `RemoveAt(intcs)` |  |
| `VertexContent` | method | `RemoveRange(System.Int32, System.Int32)` | EXACT_EQUIVALENT | `RemoveRange(intcs, intcs)` |  |
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
| `Mp3Importer` | constructor | `.ctor()` | EXACT_EQUIVALENT | `Mp3Importer()` |  |
| `Mp3Importer` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<AudioContent>` | the audio is answered by shared pointer. A missing file carries XNA's own unformatted "{0}", and a source whose bytes are not MPEG audio is refused even when its extension says otherwise, as XNA's is. |
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
| `CompiledEffectContent` | constructor | `.ctor(System.Byte[])` | SEMANTIC_EQUIVALENT | `CompiledEffectContent(std::vector<bytecs>)` | byte[] is std::vector<bytecs>, which cannot be null, so the ArgumentNullException XNA gives for a null array has no counterpart (measured: compiledeffect/null). |
| `CompiledEffectContent` | method | `GetEffectCode()` | EXACT_EQUIVALENT | `GetEffectCode()` |  |
| `EffectProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `EffectProcessor()` |  |
| `EffectProcessor` | property | `DebugMode` | EXACT_EQUIVALENT | `getDebugModeProperty() / setDebugModeProperty()` |  |
| `EffectProcessor` | property | `Defines` | SEMANTIC_EQUIVALENT | `getDefinesProperty() / setDefinesProperty()` | the nullable C# string is a std::string here; empty is the null XNA starts with, and neither compiles a definition. |
| `EffectProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.EffectContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<EffectContent>&, ContentProcessorContext&)` | content travels as an owned shared_ptr; the compile itself runs through the canonical compiler service rather than in-process D3DX. |
| `EffectProcessorDebugMode` | enum value | `Auto = 0` | EXACT_EQUIVALENT | `EffectProcessorDebugMode::Auto` |  |
| `EffectProcessorDebugMode` | enum value | `Debug = 1` | EXACT_EQUIVALENT | `EffectProcessorDebugMode::Debug` |  |
| `EffectProcessorDebugMode` | enum value | `Optimize = 2` | EXACT_EQUIVALENT | `EffectProcessorDebugMode::Optimize` |  |
| `FontDescriptionProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `FontDescriptionProcessor()` |  |
| `FontDescriptionProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.FontDescription, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<FontDescription>&, ContentProcessorContext&)` | content travels as an owned shared_ptr; the rasterization is the canonical one rather than XNA's own, so the atlas layout differs while the refusals do not. |
| `FontTextureProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `FontTextureProcessor()` |  |
| `FontTextureProcessor` | property | `FirstCharacter` | EXACT_EQUIVALENT | `getFirstCharacterProperty() / setFirstCharacterProperty()` |  |
| `FontTextureProcessor` | property | `PremultiplyAlpha` | EXACT_EQUIVALENT | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |  |
| `FontTextureProcessor` | property | `TextureFormat` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |  |
| `FontTextureProcessor` | method | `GetCharacterForIndex(System.Int32)` | EXACT_EQUIVALENT | `GetCharacterForIndex(intcs)` | protected in both; FirstCharacter plus the index, measured. |
| `FontTextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<Texture2DContent>&, ContentProcessorContext&)` | content travels as an owned shared_ptr; the glyph runs are found against the texture's own border colour and packed CNA's way, which XNA's output does not expose for comparison. |
| `MaterialProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `MaterialProcessor()` |  |
| `MaterialProcessor` | property | `ColorKeyColor` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` |  |
| `MaterialProcessor` | property | `ColorKeyEnabled` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |  |
| `MaterialProcessor` | property | `DefaultEffect` | EXACT_EQUIVALENT | `getDefaultEffectProperty() / setDefaultEffectProperty()` |  |
| `MaterialProcessor` | property | `GenerateMipmaps` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |  |
| `MaterialProcessor` | property | `PremultiplyTextureAlpha` | EXACT_EQUIVALENT | `getPremultiplyTextureAlphaProperty() / setPremultiplyTextureAlphaProperty()` |  |
| `MaterialProcessor` | property | `ResizeTexturesToPowerOfTwo` | EXACT_EQUIVALENT | `getResizeTexturesToPowerOfTwoProperty() / setResizeTexturesToPowerOfTwoProperty()` |  |
| `MaterialProcessor` | property | `TextureFormat` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |  |
| `MaterialProcessor` | method | `BuildEffect(Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<Microsoft.Xna.Framework.Content.Pipeline.Graphics.EffectContent>, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `BuildEffect(const std::shared_ptr<ExternalReference<EffectContent>>&, ContentProcessorContext&)` | external references are owned shared_ptr carriers; protected in both. |
| `MaterialProcessor` | method | `BuildTexture(System.String, Microsoft.Xna.Framework.Content.Pipeline.ExternalReference<Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent>, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `BuildTexture(const std::string&, const std::shared_ptr<ExternalReference<TextureContent>>&, ContentProcessorContext&)` | external references are owned shared_ptr carriers; protected in both. |
| `MaterialProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<MaterialContent>&, ContentProcessorContext&)` | the material is an owned shared_ptr, and the processed material is the same object, as XNA's is. |
| `MaterialProcessorDefaultEffect` | enum value | `BasicEffect = 0` | EXACT_EQUIVALENT | `MaterialProcessorDefaultEffect::BasicEffect` |  |
| `MaterialProcessorDefaultEffect` | enum value | `SkinnedEffect = 1` | EXACT_EQUIVALENT | `MaterialProcessorDefaultEffect::SkinnedEffect` |  |
| `MaterialProcessorDefaultEffect` | enum value | `EnvironmentMapEffect = 2` | EXACT_EQUIVALENT | `MaterialProcessorDefaultEffect::EnvironmentMapEffect` |  |
| `MaterialProcessorDefaultEffect` | enum value | `DualTextureEffect = 3` | EXACT_EQUIVALENT | `MaterialProcessorDefaultEffect::DualTextureEffect` |  |
| `MaterialProcessorDefaultEffect` | enum value | `AlphaTestEffect = 4` | EXACT_EQUIVALENT | `MaterialProcessorDefaultEffect::AlphaTestEffect` |  |
| `ModelBoneContent` | property | `Children` | SEMANTIC_EQUIVALENT | `getChildrenProperty()` | the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `ModelBoneContent` | property | `Index` | EXACT_EQUIVALENT | `getIndexProperty()` |  |
| `ModelBoneContent` | property | `Name` | EXACT_EQUIVALENT | `getNameProperty()` |  |
| `ModelBoneContent` | property | `Parent` | SEMANTIC_EQUIVALENT | `getParentProperty()` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelBoneContent` | property | `Transform` | EXACT_EQUIVALENT | `getTransformProperty() / setTransformProperty(Matrix)` |  |
| `ModelContent` | property | `Bones` | SEMANTIC_EQUIVALENT | `getBonesProperty()` | the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `ModelContent` | property | `Meshes` | SEMANTIC_EQUIVALENT | `getMeshesProperty()` | the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `ModelContent` | property | `Root` | SEMANTIC_EQUIVALENT | `getRootProperty()` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelContent` | property | `Tag` | EXACT_EQUIVALENT | `getTagProperty() / setTagProperty(ContentObject)` |  |
| `ModelMeshContent` | property | `BoundingSphere` | EXACT_EQUIVALENT | `getBoundingSphereProperty()` |  |
| `ModelMeshContent` | property | `MeshParts` | SEMANTIC_EQUIVALENT | `getMeshPartsProperty()` | the collection is a std::vector of shared pointers, which is what a read-only collection of reference types is here. |
| `ModelMeshContent` | property | `Name` | EXACT_EQUIVALENT | `getNameProperty()` |  |
| `ModelMeshContent` | property | `ParentBone` | SEMANTIC_EQUIVALENT | `getParentBoneProperty()` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelMeshContent` | property | `SourceMesh` | SEMANTIC_EQUIVALENT | `getSourceMeshProperty()` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelMeshContent` | property | `Tag` | EXACT_EQUIVALENT | `getTagProperty() / setTagProperty(ContentObject)` |  |
| `ModelMeshPartContent` | property | `IndexBuffer` | SEMANTIC_EQUIVALENT | `getIndexBufferProperty()` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelMeshPartContent` | property | `Material` | SEMANTIC_EQUIVALENT | `getMaterialProperty() / setMaterialProperty(std::shared_ptr<MaterialContent>)` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelMeshPartContent` | property | `NumVertices` | EXACT_EQUIVALENT | `getNumVerticesProperty()` |  |
| `ModelMeshPartContent` | property | `PrimitiveCount` | EXACT_EQUIVALENT | `getPrimitiveCountProperty()` |  |
| `ModelMeshPartContent` | property | `StartIndex` | EXACT_EQUIVALENT | `getStartIndexProperty()` |  |
| `ModelMeshPartContent` | property | `Tag` | EXACT_EQUIVALENT | `getTagProperty() / setTagProperty(ContentObject)` |  |
| `ModelMeshPartContent` | property | `VertexBuffer` | SEMANTIC_EQUIVALENT | `getVertexBufferProperty()` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelMeshPartContent` | property | `VertexOffset` | EXACT_EQUIVALENT | `getVertexOffsetProperty()` |  |
| `ModelProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ModelProcessor()` |  |
| `ModelProcessor` | property | `ColorKeyColor` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty(Color)` |  |
| `ModelProcessor` | property | `ColorKeyEnabled` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty(bool)` |  |
| `ModelProcessor` | property | `DefaultEffect` | EXACT_EQUIVALENT | `getDefaultEffectProperty() / setDefaultEffectProperty(MaterialProcessorDefaultEffect)` |  |
| `ModelProcessor` | property | `GenerateMipmaps` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty(bool)` |  |
| `ModelProcessor` | property | `GenerateTangentFrames` | EXACT_EQUIVALENT | `getGenerateTangentFramesProperty() / setGenerateTangentFramesProperty(bool)` |  |
| `ModelProcessor` | property | `PremultiplyTextureAlpha` | EXACT_EQUIVALENT | `getPremultiplyTextureAlphaProperty() / setPremultiplyTextureAlphaProperty(bool)` |  |
| `ModelProcessor` | property | `PremultiplyVertexColors` | EXACT_EQUIVALENT | `getPremultiplyVertexColorsProperty() / setPremultiplyVertexColorsProperty(bool)` |  |
| `ModelProcessor` | property | `ResizeTexturesToPowerOfTwo` | EXACT_EQUIVALENT | `getResizeTexturesToPowerOfTwoProperty() / setResizeTexturesToPowerOfTwoProperty(bool)` |  |
| `ModelProcessor` | property | `RotationX` | EXACT_EQUIVALENT | `getRotationXProperty() / setRotationXProperty(Single)` |  |
| `ModelProcessor` | property | `RotationY` | EXACT_EQUIVALENT | `getRotationYProperty() / setRotationYProperty(Single)` |  |
| `ModelProcessor` | property | `RotationZ` | EXACT_EQUIVALENT | `getRotationZProperty() / setRotationZProperty(Single)` |  |
| `ModelProcessor` | property | `Scale` | EXACT_EQUIVALENT | `getScaleProperty() / setScaleProperty(Single)` |  |
| `ModelProcessor` | property | `SwapWindingOrder` | EXACT_EQUIVALENT | `getSwapWindingOrderProperty() / setSwapWindingOrderProperty(bool)` |  |
| `ModelProcessor` | property | `TextureFormat` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty(TextureProcessorOutputFormat)` |  |
| `ModelProcessor` | method | `ConvertMaterial(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `ConvertMaterial(const std::shared_ptr<MaterialContent>&, ContentProcessorContext&)` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.NodeContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<NodeContent>&, ContentProcessorContext&)` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelProcessor` | method | `ProcessGeometryUsingMaterial(Microsoft.Xna.Framework.Content.Pipeline.Graphics.MaterialContent, System.Collections.Generic.IEnumerable<Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent>, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `ProcessGeometryUsingMaterial(const std::shared_ptr<MaterialContent>&, const std::vector<std::shared_ptr<GeometryContent>>&, ContentProcessorContext&)` | the sequence of batches is a std::vector of shared pointers rather than an IEnumerable. |
| `ModelProcessor` | method | `ProcessVertexChannel(Microsoft.Xna.Framework.Content.Pipeline.Graphics.GeometryContent, System.Int32, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `ProcessVertexChannel(const std::shared_ptr<GeometryContent>&, intcs, ContentProcessorContext&)` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `ModelTextureProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `ModelTextureProcessor()` |  |
| `ModelTextureProcessor` | property | `ColorKeyColor` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `ModelTextureProcessor` | property | `ColorKeyEnabled` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `ModelTextureProcessor` | property | `GenerateMipmaps` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `ModelTextureProcessor` | property | `ResizeToPowerOfTwo` | EXACT_EQUIVALENT | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `ModelTextureProcessor` | property | `TextureFormat` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `ModelTextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<TextureContent>&, ContentProcessorContext&)` | inherited from TextureProcessor, which is what XNA's override calls. |
| `PassThroughProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `PassThroughProcessor()` |  |
| `PassThroughProcessor` | method | `Process(System.Object, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const ContentObject&, ContentProcessorContext&)` | object is ContentObject. |
| `SongProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `SongProcessor()` |  |
| `SongProcessor` | property | `Quality` | EXACT_EQUIVALENT | `getQualityProperty() / setQualityProperty(ConversionQuality)` |  |
| `SongProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | HOST_SUBSTITUTION | `Process(const std::shared_ptr<AudioContent>&, ContentProcessorContext&) -> std::shared_ptr<SongContent>` | Writes a real Windows Media audio file beside the output asset, named after it, and answers a SongContent carrying that name and the source's duration; the file is added to the context as an output. The encoder is this host's rather than Microsoft's, whose behaviour could not be measured at all -- XNA's Windows Media path never returns under the oracle's Wine prefix -- so the bytes are not Microsoft's and are not claimed to be. What is held is that the file a runtime reads back is Windows Media audio of the source's own rate and channel count, that its length survives a lossy round trip, and that each lower quality writes a smaller file. Refusals carry XNA's own "Could not convert audio file X to WindowsMedia format." |
| `SoundEffectProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `SoundEffectProcessor()` |  |
| `SoundEffectProcessor` | property | `Quality` | EXACT_EQUIVALENT | `getQualityProperty() / setQualityProperty(ConversionQuality)` |  |
| `SoundEffectProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Audio.AudioContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<AudioContent>&, ContentProcessorContext&)` | the audio and the answered content are shared pointers, which is the lifetime a .NET reference gives them. The best quality leaves the source alone and the two below it compress it to ADPCM in place, which is what XNA does; the ADPCM sample values are this host's encoder (XNAPP-161). |
| `SpriteTextureProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `SpriteTextureProcessor()` |  |
| `SpriteTextureProcessor` | property | `ColorKeyColor` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `SpriteTextureProcessor` | property | `ColorKeyEnabled` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `SpriteTextureProcessor` | property | `GenerateMipmaps` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `SpriteTextureProcessor` | property | `ResizeToPowerOfTwo` | EXACT_EQUIVALENT | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `SpriteTextureProcessor` | property | `TextureFormat` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` | inherited; XNA redeclares it only to hide it from the designer. |
| `SpriteTextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<TextureContent>&, ContentProcessorContext&)` | inherited from TextureProcessor, which is what XNA's override calls. |
| `TextureProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `TextureProcessor()` |  |
| `TextureProcessor` | property | `ColorKeyColor` | EXACT_EQUIVALENT | `getColorKeyColorProperty() / setColorKeyColorProperty()` |  |
| `TextureProcessor` | property | `ColorKeyEnabled` | EXACT_EQUIVALENT | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |  |
| `TextureProcessor` | property | `GenerateMipmaps` | EXACT_EQUIVALENT | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |  |
| `TextureProcessor` | property | `PremultiplyAlpha` | EXACT_EQUIVALENT | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |  |
| `TextureProcessor` | property | `ResizeToPowerOfTwo` | EXACT_EQUIVALENT | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |  |
| `TextureProcessor` | property | `TextureFormat` | EXACT_EQUIVALENT | `getTextureFormatProperty() / setTextureFormatProperty()` |  |
| `TextureProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<TextureContent>&, ContentProcessorContext&)` | the texture is an owned shared_ptr, and the processed texture is the same object, as XNA's is. |
| `TextureProcessorOutputFormat` | enum value | `NoChange = 0` | EXACT_EQUIVALENT | `TextureProcessorOutputFormat::NoChange` |  |
| `TextureProcessorOutputFormat` | enum value | `Color = 1` | EXACT_EQUIVALENT | `TextureProcessorOutputFormat::Color` |  |
| `TextureProcessorOutputFormat` | enum value | `DxtCompressed = 2` | EXACT_EQUIVALENT | `TextureProcessorOutputFormat::DxtCompressed` |  |
| `VertexBufferContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `VertexBufferContent()` |  |
| `VertexBufferContent` | constructor | `.ctor(System.Int32)` | EXACT_EQUIVALENT | `VertexBufferContent(intcs)` |  |
| `VertexBufferContent` | property | `VertexData` | SEMANTIC_EQUIVALENT | `getVertexDataProperty()` | a std::vector<bytecs> stands for the Byte[]. |
| `VertexBufferContent` | property | `VertexDeclaration` | SEMANTIC_EQUIVALENT | `getVertexDeclarationProperty() / setVertexDeclarationProperty(std::shared_ptr<VertexDeclarationContent>)` | a child is held by shared pointer and a parent referenced by shared pointer, which is the lifetime a .NET reference gives them. |
| `VertexBufferContent` | method | `SizeOf(System.Type)` | EXACT_EQUIVALENT | `SizeOf(System::Type)` |  |
| `VertexBufferContent` | method | `Write(System.Int32, System.Int32, System.Type, System.Collections.IEnumerable)` | SEMANTIC_EQUIVALENT | `Write(intcs, intcs, System::Type, const std::vector<ContentObject>&)` | the untyped sequence is a vector of boxed values, which is what an IEnumerable of boxed values is here. |
| `VertexBufferContent` | method | `Write<T>(System.Int32, System.Int32, System.Collections.Generic.IEnumerable<T>)` | SEMANTIC_EQUIVALENT | `Write<T>(intcs, intcs, const std::vector<T>&)` | the sequence is a std::vector rather than an IEnumerable<T>. |
| `VertexDeclarationContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `VertexDeclarationContent()` |  |
| `VertexDeclarationContent` | property | `VertexElements` | EXACT_EQUIVALENT | `getVertexElementsProperty()` |  |
| `VertexDeclarationContent` | property | `VertexStride` | SEMANTIC_EQUIVALENT | `getVertexStrideProperty() / setVertexStrideProperty(std::optional<intcs>)` | a std::optional stands for Nullable<Int32>; an empty one is what a fresh declaration answers, as XNA's null does. |
| `VideoProcessor` | constructor | `.ctor()` | EXACT_EQUIVALENT | `VideoProcessor()` |  |
| `VideoProcessor` | property | `VideoSoundtrackType` | EXACT_EQUIVALENT | `getVideoSoundtrackTypeProperty() / setVideoSoundtrackTypeProperty()` |  |
| `VideoProcessor` | method | `Process(Microsoft.Xna.Framework.Content.Pipeline.VideoContent, Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorContext)` | SEMANTIC_EQUIVALENT | `Process(const std::shared_ptr<VideoContent>&, ContentProcessorContext&) -> std::shared_ptr<VideoContent>` | the video is a shared pointer, and the same object comes back with its soundtrack type set. A null input is refused with ArgumentNullException naming input, as XNA's is; the source is added both as a dependency and as an output file, because the built asset streams from it. |
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
| `BuildContent` | constant | `CancelEventNameFormat` | SEMANTIC_EQUIVALENT | `BuildContent::CancelEventNameFormat` | XNA's own value verbatim, a string.Format template whose one argument is the content project's GUID. There is no Windows named event here and nothing waits on one; the constant is the observable part. |
| `BuildContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `BuildContent()` |  |
| `BuildContent` | property | `BuildConfiguration` | EXACT_EQUIVALENT | `getBuildConfigurationProperty() / setBuildConfigurationProperty()` |  |
| `BuildContent` | property | `CompressContent` | EXACT_EQUIVALENT | `getCompressContentProperty() / setCompressContentProperty()` |  |
| `BuildContent` | property | `ContentProjectGUID` | EXACT_EQUIVALENT | `getContentProjectGUIDProperty() / setContentProjectGUIDProperty()` |  |
| `BuildContent` | property | `IntermediateDirectory` | EXACT_EQUIVALENT | `getIntermediateDirectoryProperty() / setIntermediateDirectoryProperty()` |  |
| `BuildContent` | property | `IntermediateFiles` | SEMANTIC_EQUIVALENT | `getIntermediateFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is, and filled by Execute(). |
| `BuildContent` | property | `LoggerRootDirectory` | EXACT_EQUIVALENT | `getLoggerRootDirectoryProperty() / setLoggerRootDirectoryProperty()` |  |
| `BuildContent` | property | `OutputContentFiles` | SEMANTIC_EQUIVALENT | `getOutputContentFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is, and filled by Execute(). |
| `BuildContent` | property | `OutputDirectory` | EXACT_EQUIVALENT | `getOutputDirectoryProperty() / setOutputDirectoryProperty()` |  |
| `BuildContent` | property | `PipelineAssemblies` | SEMANTIC_EQUIVALENT | `getPipelineAssembliesProperty()/setPipelineAssembliesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]. |
| `BuildContent` | property | `PipelineAssemblyDependencies` | SEMANTIC_EQUIVALENT | `getPipelineAssemblyDependenciesProperty()/setPipelineAssemblyDependenciesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]. |
| `BuildContent` | property | `RebuildAll` | EXACT_EQUIVALENT | `getRebuildAllProperty() / setRebuildAllProperty()` |  |
| `BuildContent` | property | `RebuiltContentFiles` | SEMANTIC_EQUIVALENT | `getRebuiltContentFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is, and filled by Execute(). |
| `BuildContent` | property | `RootDirectory` | EXACT_EQUIVALENT | `getRootDirectoryProperty() / setRootDirectoryProperty()` |  |
| `BuildContent` | property | `SourceAssets` | SEMANTIC_EQUIVALENT | `getSourceAssetsProperty()/setSourceAssetsProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]. |
| `BuildContent` | property | `TargetPlatform` | EXACT_EQUIVALENT | `getTargetPlatformProperty() / setTargetPlatformProperty()` |  |
| `BuildContent` | property | `TargetProfile` | EXACT_EQUIVALENT | `getTargetProfileProperty() / setTargetProfileProperty()` |  |
| `BuildContent` | method | `Execute()` | HOST_SUBSTITUTION | `Execute()` | Builds every source asset through the canonical coordinator into .xnb, honouring the Name, Importer, Processor, Link and ProcessorParameters metadata a .contentproj writes, and fills OutputContentFiles, RebuiltContentFiles and IntermediateFiles from the build manifest. RebuildAll drops the incremental state. A non-empty PipelineAssemblies is refused rather than ignored, because C++ has no assembly to load and a project naming one expects its own importers to run. |
| `BuildXact` | constructor | `.ctor()` | EXACT_EQUIVALENT | `BuildXact()` |  |
| `BuildXact` | property | `BuildConfiguration` | EXACT_EQUIVALENT | `getBuildConfigurationProperty() / setBuildConfigurationProperty()` |  |
| `BuildXact` | property | `ContentProjectGUID` | EXACT_EQUIVALENT | `getContentProjectGUIDProperty() / setContentProjectGUIDProperty()` |  |
| `BuildXact` | property | `IntermediateDirectory` | EXACT_EQUIVALENT | `getIntermediateDirectoryProperty() / setIntermediateDirectoryProperty()` |  |
| `BuildXact` | property | `IntermediateFiles` | SEMANTIC_EQUIVALENT | `getIntermediateFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is, and filled by Execute(). |
| `BuildXact` | property | `LoggerRootDirectory` | EXACT_EQUIVALENT | `getLoggerRootDirectoryProperty() / setLoggerRootDirectoryProperty()` |  |
| `BuildXact` | property | `OutputDirectory` | EXACT_EQUIVALENT | `getOutputDirectoryProperty() / setOutputDirectoryProperty()` |  |
| `BuildXact` | property | `OutputXactFiles` | SEMANTIC_EQUIVALENT | `getOutputXactFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is, and filled by Execute(). |
| `BuildXact` | property | `RebuildAll` | EXACT_EQUIVALENT | `getRebuildAllProperty() / setRebuildAllProperty()` |  |
| `BuildXact` | property | `RebuiltXactFiles` | SEMANTIC_EQUIVALENT | `getRebuiltXactFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is, and filled by Execute(). |
| `BuildXact` | property | `RootDirectory` | EXACT_EQUIVALENT | `getRootDirectoryProperty() / setRootDirectoryProperty()` |  |
| `BuildXact` | property | `TargetPlatform` | EXACT_EQUIVALENT | `getTargetPlatformProperty() / setTargetPlatformProperty()` |  |
| `BuildXact` | property | `TargetProfile` | EXACT_EQUIVALENT | `getTargetProfileProperty() / setTargetProfileProperty()` |  |
| `BuildXact` | property | `XactProjects` | SEMANTIC_EQUIVALENT | `getXactProjectsProperty()/setXactProjectsProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]. |
| `BuildXact` | property | `XnaFrameworkVersion` | EXACT_EQUIVALENT | `getXnaFrameworkVersionProperty() / setXnaFrameworkVersionProperty()` |  |
| `BuildXact` | method | `Execute()` | HOST_SUBSTITUTION | `Execute()` | Validates every named project -- that it exists, opens, and opens with an XACT signature -- and then invokes the XACT compiler named by SetXactCompilerEXT() or CNA_XACTBLD, through CNA_XACTBLD_LAUNCHER where one is set, collecting the .xgs, .xwb and .xsb it produced. Validation runs whether or not a compiler is present, so a bad project is told what is wrong with it rather than being told the tool is missing. Compiling an .xap itself needs Microsoft's own XactBld3.exe, which ships with the XNA Game Studio tools and is not something CNA can reimplement or redistribute; where none is found the task answers false with a message naming it. |
| `CleanContent` | constructor | `.ctor()` | EXACT_EQUIVALENT | `CleanContent()` |  |
| `CleanContent` | property | `BuildConfiguration` | EXACT_EQUIVALENT | `getBuildConfigurationProperty() / setBuildConfigurationProperty()` |  |
| `CleanContent` | property | `ContentProjectGUID` | EXACT_EQUIVALENT | `getContentProjectGUIDProperty() / setContentProjectGUIDProperty()` |  |
| `CleanContent` | property | `IntermediateDirectory` | EXACT_EQUIVALENT | `getIntermediateDirectoryProperty() / setIntermediateDirectoryProperty()` |  |
| `CleanContent` | property | `OutputDirectory` | EXACT_EQUIVALENT | `getOutputDirectoryProperty() / setOutputDirectoryProperty()` |  |
| `CleanContent` | property | `RootDirectory` | EXACT_EQUIVALENT | `getRootDirectoryProperty() / setRootDirectoryProperty()` |  |
| `CleanContent` | property | `TargetPlatform` | EXACT_EQUIVALENT | `getTargetPlatformProperty() / setTargetPlatformProperty()` |  |
| `CleanContent` | property | `TargetProfile` | EXACT_EQUIVALENT | `getTargetProfileProperty() / setTargetProfileProperty()` |  |
| `CleanContent` | method | `Execute()` | HOST_SUBSTITUTION | `Execute()` | Removes only the files a valid output manifest proves the pipeline owns, which is the canonical cleaner's own rule; a directory with no manifest is left alone rather than emptied, and one that is not there at all is a success. |
| `GetLastOutputs` | constructor | `.ctor()` | EXACT_EQUIVALENT | `GetLastOutputs()` |  |
| `GetLastOutputs` | property | `ContentProjectGUID` | EXACT_EQUIVALENT | `getContentProjectGUIDProperty() / setContentProjectGUIDProperty()` |  |
| `GetLastOutputs` | property | `IntermediateDirectory` | EXACT_EQUIVALENT | `getIntermediateDirectoryProperty() / setIntermediateDirectoryProperty()` |  |
| `GetLastOutputs` | property | `OutputContentFiles` | SEMANTIC_EQUIVALENT | `getOutputContentFilesProperty()` | a std::vector<TaskItem> rather than an ITaskItem[]; read-only, as XNA's is. |
| `GetLastOutputs` | method | `Execute()` | HOST_SUBSTITUTION | `Execute()` | Reads the previous build's manifest and nothing else, so it never rebuilds. No previous build is an empty answer and not a failure, which is what a project asking what is there needs. |
| `TextureImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `TextureImporter()` |  |
| `TextureImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<TextureContent>` | the texture is answered by shared pointer; every source XNA reads is read the same way and answers the same content -- a DDS included, whose compressed blocks stay compressed, whose cube becomes a TextureCubeContent and whose volume a Texture3DContent, and whose DX10 extension is refused as XNA refuses it. |
| `VideoContent` | constructor | `.ctor(System.String)` | EXACT_EQUIVALENT | `VideoContent(const std::string&)` |  |
| `VideoContent` | property | `BitsPerSecond` | EXACT_EQUIVALENT | `getBitsPerSecondProperty()` |  |
| `VideoContent` | property | `Duration` | SEMANTIC_EQUIVALENT | `getDurationProperty()` | a System::TimeSpan, truncated to whole milliseconds as every other duration in this pipeline is. |
| `VideoContent` | property | `Filename` | EXACT_EQUIVALENT | `getFilenameProperty()` |  |
| `VideoContent` | property | `FramesPerSecond` | EXACT_EQUIVALENT | `getFramesPerSecondProperty()` |  |
| `VideoContent` | property | `Height` | EXACT_EQUIVALENT | `getHeightProperty()` |  |
| `VideoContent` | property | `VideoSoundtrackType` | SEMANTIC_EQUIVALENT | `getVideoSoundtrackTypeProperty()/setVideoSoundtrackTypeProperty(...)` | the setter is internal in XNA and public and CNAEXT-marked here; VideoProcessor is still the only thing that calls it. |
| `VideoContent` | property | `Width` | EXACT_EQUIVALENT | `getWidthProperty()` |  |
| `VideoContent` | method | `Dispose()` | SEMANTIC_EQUIVALENT | `Dispose()` | nothing is held open past the constructor's probe, so every property keeps answering afterwards and a second call is accepted -- which is what the genuine one does with its own handle. |
| `WavImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `WavImporter()` |  |
| `WavImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<AudioContent>` | the audio is answered by shared pointer; every WAV variant XNA accepts is accepted with the same fields, including the unformatted "{0}" its missing-file message carries. |
| `WmaImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `WmaImporter()` |  |
| `WmaImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<AudioContent>` | the audio is answered by shared pointer, and a source whose bytes are not Windows Media audio is refused whatever its extension says. The successful path is not measured against the genuine importer, which this environment cannot run. |
| `WmvImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `WmvImporter()` |  |
| `WmvImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<VideoContent>` | the video is answered by shared pointer; the missing-file message carries XNA's own unformatted "{0}". |
| `XImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `XImporter()` |  |
| `XImporter` | method | `Dispose()` | SEMANTIC_EQUIVALENT | `Dispose()` | nothing native is held open past Import, which reads the file whole and closes it; the pattern is here because XNA declares it, and a second call is accepted. |
| `XImporter` | method | `Dispose(System.Boolean)` | EXACT_EQUIVALENT | `Dispose(bool)` |  |
| `XImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> std::shared_ptr<NodeContent>` | the graph is answered by shared pointer. Every measured behaviour is reproduced: Z negated on positions, normals and a frame's matrix as the basis change S M S; a single top-level Frame answering as the root; one GeometryContent per material sharing the mesh's positions; skin weights read only where an XSkinMeshHeader declares a skeleton, with zero weights dropped; a mesh child ordered before a frame child; vertex colours quantized through eight bits; separate rotation, scale and position key lists merged by interpolation at the union of their times; a tick worth 1/4800 of a second unless AnimTicksPerSecond says otherwise; a Duration that is the last key truncated to whole milliseconds while the keys keep full precision; and every animation in a set landing on the skeleton's root bone where there is one and on its own target where there is not. Refusals carry the D3DX code the genuine reader picks -- BADFILE, BADFILETYPE, BADFILEVERSION, PARSEERROR or E_FAIL. |
| `XmlImporter` | constructor | `.ctor()` | EXACT_EQUIVALENT | `XmlImporter()` |  |
| `XmlImporter` | method | `Import(System.String, Microsoft.Xna.Framework.Content.Pipeline.ContentImporterContext)` | SEMANTIC_EQUIVALENT | `Import(const std::string&, ContentImporterContext&) -> ContentObject` | the object is boxed as a ContentObject; a missing file is refused with FileNotFoundException and a malformed document with XNA's own deserialization sentence, whose parser-specific tail differs and is recorded in docs/xna-intermediate-xml-format.md. |

### 7.1 CLR plumbing not counted

| XNA type | Signature | Why |
|---|---|---|
| `ContentTypeSerializer+ChildCallback` | `.ctor(System.Object, System.IntPtr)` | delegate plumbing; the type maps to one C++ callable whose shape is Invoke |
| `ContentTypeSerializer+ChildCallback` | `BeginInvoke(Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializer, System.Object, System.AsyncCallback, System.Object)` | delegate plumbing; the type maps to one C++ callable whose shape is Invoke |
| `ContentTypeSerializer+ChildCallback` | `EndInvoke(System.IAsyncResult)` | delegate plumbing; the type maps to one C++ callable whose shape is Invoke |
