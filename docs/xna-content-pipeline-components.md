# XNA 4.0 Content Pipeline component reference

> **Generated** by `tools/xna-pipeline-oracle/component_reference.py` from
> `tests/reference/xna40/content-pipeline-api.json` (read from the genuine XNA Game Studio 4.0
> Refresh assemblies), `tests/reference/xna40/content-pipeline-parity-map.json` (CNA's answer) and
> `tests/reference/xna40/content-pipeline-inputs.json` (the per-extension route). Do not edit by
> hand; edit those and regenerate. The ctest `XnaPipelineComponentReferenceIsCurrent` fails if this
> file is not what a regeneration writes.

This is the *what does it do* reference. Three companions answer the neighbouring questions:

* [`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md) -- how a C# pipeline
  concept is spelled in C++ (attributes, properties, reflection, contexts).
* [`xna-content-pipeline-migration.md`](xna-content-pipeline-migration.md) -- porting an existing
  XNA content project, with before/after examples.
* [`xna-content-pipeline-parity-report.md`](xna-content-pipeline-parity-report.md) -- the parity
  matrix: every public type and member, with its status.

**How to read the status column.** `EXACT_EQUIVALENT` means the same name, members and observable
behaviour. `SEMANTIC_EQUIVALENT` means C++ cannot spell it identically but every capability is
there, and the note says how. `HOST_SUBSTITUTION` means a Microsoft-host mechanism was replaced by
a CNA one. There is no `MISSING` row in this document, and the parity gate is what keeps it that
way.

## 1. Source extensions

Every file extension a built-in XNA importer declares, and what a source with that extension reaches in CNA. The importer is selected by extension exactly as XNA selects it; the processor named here is the one the importer's `DefaultProcessor` asks for, which a `.contentproj` item's `<Processor>` or a `.cna-content.json` asset's `processor` field overrides.

| Extension | XNA importer | Default processor | CNA importer | CNA processor | Fixture |
|---|---|---|---|---|---|
| `.bmp` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.bmp` |
| `.dds` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.dds` |
| `.dib` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.dib` |
| `.fbx` | `FbxImporter` | `ModelProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::FbxImporter` | `ModelProcessor` | `tests/assets/xna40/model/fbx_quad_textured.fbx` |
| `.fx` | `EffectImporter` | `EffectProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::EffectImporter` | `EffectProcessor` | `tests/assets/xna40/source/probe.fx` |
| `.hdr` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.hdr` |
| `.jpg` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.jpg` |
| `.mp3` | `Mp3Importer` | `SongProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::Mp3Importer` | `SongProcessor` | `tests/assets/xna40/media/mp3_mono_44100_128k.mp3` |
| `.pfm` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.pfm` |
| `.png` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.png` |
| `.ppm` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.ppm` |
| `.spritefont` | `FontDescriptionImporter` | `FontDescriptionProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::FontDescriptionImporter` | `FontDescriptionProcessor` | `tests/assets/xna40/source/probe.spritefont, tests/assets/xna40/source/buildable.spritefont` |
| `.tga` | `TextureImporter` | `SpriteTextureProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::TextureImporter` | `SpriteTextureProcessor` | `tests/assets/xna40/texture/probe.tga` |
| `.wav` | `WavImporter` | `SoundEffectProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::WavImporter` | `SoundEffectProcessor` | `tests/assets/xna40/media/tone_mono_44100.wav` |
| `.wma` | `WmaImporter` | `SongProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::WmaImporter` | `SongProcessor` | `tests/assets/xna40/media/wma_mono_44100.wma` |
| `.wmv` | `WmvImporter` | `VideoProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::WmvImporter` | `VideoProcessor` | `tests/assets/xna40/media/wmv_64x48_15fps_silent.wmv` |
| `.x` | `XImporter` | `ModelProcessor` | `Microsoft::Xna::Framework::Content::Pipeline::XImporter` | `ModelProcessor` | `tests/assets/xna40/model/quad_textured.x, tests/assets/xna40/model/surface.png` |
| `.xml` | `XmlImporter` | -- | `Microsoft::Xna::Framework::Content::Pipeline::XmlImporter` | `PassThroughProcessor(<the type the document declares>)` | `tests/assets/xna40/source/probe.xml, tests/assets/xna40/source/xml_int.xml, tests/assets/xna40/source/xml_vector3.xml, tests/assets/xna40/source/xml_curve.xml, tests/assets/xna40/source/xml_dictionary.xml, tests/assets/xna40/source/xml_rectangles.xml` |

18 extensions, all IMPLEMENTED and TESTED. The matrix that records the tests and fixtures behind each row is section 9 of the parity report.

## 2. Importers

`Cached` is XNA's `CacheImportedData`: whether the build is allowed to cache the imported object graph between builds. The two model importers declare it and the other eight do not.

| Importer | Display name | Extensions | Produces | Cached | Status |
|---|---|---|---|---|---|
| `EffectImporter` | Effect - XNA Framework | `.fx` | `EffectContent` | no | SEMANTIC_EQUIVALENT |
| `FbxImporter` | Autodesk FBX - XNA Framework | `.fbx` | `NodeContent` | yes | SEMANTIC_EQUIVALENT |
| `FontDescriptionImporter` | Sprite Font Description - XNA Framework | `.spritefont` | `FontDescription` | no | SEMANTIC_EQUIVALENT |
| `Mp3Importer` | MP3 Audio File - XNA Framework | `.mp3` | `AudioContent` | no | SEMANTIC_EQUIVALENT |
| `TextureImporter` | Texture - XNA Framework | `.bmp` `.dds` `.dib` `.hdr` `.jpg` `.pfm` `.png` `.ppm` `.tga` | `TextureContent` | no | SEMANTIC_EQUIVALENT |
| `WavImporter` | WAV Audio File - XNA Framework | `.wav` | `AudioContent` | no | SEMANTIC_EQUIVALENT |
| `WmaImporter` | WMA Audio File - XNA Framework | `.wma` | `AudioContent` | no | SEMANTIC_EQUIVALENT |
| `WmvImporter` | WMV Video File - XNA Framework | `.wmv` | `VideoContent` | no | SEMANTIC_EQUIVALENT |
| `XImporter` | X File - XNA Framework | `.x` | `NodeContent` | yes | SEMANTIC_EQUIVALENT |
| `XmlImporter` | XML Content - XNA Framework | `.xml` | `Object` | no | SEMANTIC_EQUIVALENT |

* **EffectImporter** -- the effect is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute().
* **FbxImporter** -- the node graph is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Measured against the genuine importer over a corpus written for this repository (tests/reference/xna40/model, cases fbx/*). One recorded divergence, deliberately in CNA's favour: XNA carries FBX SDK 2011.3.1 and refuses every document of version 7400 or above -- which is every FBX a current tool writes -- and CNA reads those too.
* **FontDescriptionImporter** -- the description is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute().
* **Mp3Importer** -- the audio is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Everything the genuine importer answers for the corpus is reproduced: the format is the decoder's own -- 16-bit PCM at 44100 whatever the source rate, with only the channel count surviving -- the duration is the whole decoded stream truncated to whole milliseconds, encoder delay and padding included, and both loop fields are 0.
* **TextureImporter** -- the texture is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute().
* **WavImporter** -- the audio is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute().
* **WmaImporter** -- the audio is answered by shared pointer. The genuine importer could not be measured here -- Wine carries no Windows Media Format runtime, so every WMA is refused before it is opened (docs/xna-content-pipeline-media.md section 6) -- so this reads the format itself and answers the shape the MP3 measurement settled for the same SongProcessor input.
* **WmvImporter** -- the video is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). The refusals are the measured ones, including the split that makes a missing file a FileNotFoundException here and an InvalidContentException through VideoContent's own constructor.
* **XImporter** -- the node graph is answered by shared pointer, which is the lifetime a .NET reference gives it; the descriptor XNA declares through an attribute is answered by a static Attribute(). Both encodings the format defines for uncompressed data are read, text and binary, and thirteen corpus files are compared graph for graph against the genuine importer (tests/reference/xna40/model). The two compressed encodings, tzip and bzip, are refused by name rather than mis-read.
* **XmlImporter** -- the imported object is a ContentObject rather than a System.Object reference, which is what a boxed value of any type is here; the descriptor XNA declares through an attribute is answered by a static Attribute().

## 3. Processors and their properties

A processor property is set from item metadata named `ProcessorParameters_<Name>` in a `.contentproj`, from an asset's `parameters` object in a `.cna-content.json`, or directly on the C++ object through the accessor in the last column. `Configurable` is whether XNA's own designer offered the property; a non-configurable one is still settable in code. `XNA default` is the value a freshly constructed processor answered when the oracle read it, and the same values are asserted against a freshly constructed CNA processor by `XnaProcessorDefaultsTests.cpp`.

### EffectProcessor

*Effect - XNA Framework* -- takes `EffectContent`, produces `CompiledEffectContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::EffectProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/EffectProcessor.hpp`.

XNA compiles in-process with D3DX; CNA drives the one effect compiler this repository has, the canonical CNA::Content::Pipeline::EffectCompilerService, and carries a CNAEXT constructor taking it. The observable contract is measured (effectprocessor/*): the byte code comes back as CompiledEffectContent, a refused source raises InvalidContentException beginning `Errors compiling <file>:` with the compiler's own diagnostics, and a null input is refused. Two host differences: the runtime composed that message with Environment.NewLine, where CNA writes a newline; and a compiler that is not installed is reported through the same message rather than being impossible, as it is in XNA.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `DebugMode` | `EffectProcessorDebugMode` | `EffectProcessorDebugMode.Auto` | yes | `EffectProcessor` | `getDebugModeProperty() / setDebugModeProperty()` |
| `Defines` | `String` | -- | yes | `EffectProcessor` | `getDefinesProperty() / setDefinesProperty()` |

### FontDescriptionProcessor

*Sprite Font Description - XNA Framework* -- takes `FontDescription`, produces `SpriteFontContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::FontDescriptionProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/FontProcessors.hpp`.

Rasterizes through the canonical CNA::Content::Pipeline::RasterizeFontDescription and resolves the font the way the canonical importer does, which is a filename match rather than a family-table lookup -- so a font installed under another filename is not found where XNA would find it. The refusals are XNA's own, measured (fontprocessor/description_missing_font, /description_no_characters, /description_null).

No properties.

### FontTextureProcessor

*Sprite Font Texture - XNA Framework* -- takes `Texture2DContent`, produces `SpriteFontContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::FontTextureProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/FontProcessors.hpp`.

Defaults and the character mapping are measured (processor/FontTextureProcessor, fontprocessor/texture_character_for_index, /texture_first_character_set). What Process produces cannot be compared beyond its boundary, because SpriteFontContent publishes nothing: the two measured outcomes -- a delimited strip is accepted, a texture with no glyphs is refused with XNA's message -- are reproduced, and the glyph packing is CNA's own.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `FirstCharacter` | `Char` | `'\u0020' (32)` | yes | `FontTextureProcessor` | `getFirstCharacterProperty() / setFirstCharacterProperty()` |
| `PremultiplyAlpha` | `Boolean` | `True` | yes | `FontTextureProcessor` | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | yes | `FontTextureProcessor` | `getTextureFormatProperty() / setTextureFormatProperty()` |

### MaterialProcessor

*--* -- takes `MaterialContent`, produces `MaterialContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::MaterialProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp`.

Measured against a build context that records what it is asked to build (materialprocessor/*): the material comes back as the same object with each texture reference replaced, every texture is built through `TextureProcessor` with the six parameters this processor's properties map onto, and an effect material's effect is built through `EffectProcessor` with no parameters at all. Its own defaults differ from the texture processor's: mipmaps on and DxtCompressed.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | yes | `MaterialProcessor` | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `ColorKeyEnabled` | `Boolean` | `True` | yes | `MaterialProcessor` | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `DefaultEffect` | `MaterialProcessorDefaultEffect` | `MaterialProcessorDefaultEffect.BasicEffect` | yes | `MaterialProcessor` | `getDefaultEffectProperty() / setDefaultEffectProperty()` |
| `GenerateMipmaps` | `Boolean` | `True` | yes | `MaterialProcessor` | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `PremultiplyTextureAlpha` | `Boolean` | `True` | yes | `MaterialProcessor` | `getPremultiplyTextureAlphaProperty() / setPremultiplyTextureAlphaProperty()` |
| `ResizeTexturesToPowerOfTwo` | `Boolean` | -- | yes | `MaterialProcessor` | `getResizeTexturesToPowerOfTwoProperty() / setResizeTexturesToPowerOfTwoProperty()` |
| `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | yes | `MaterialProcessor` | `getTextureFormatProperty() / setTextureFormatProperty()` |

### ModelProcessor

*Model - XNA Framework* -- takes `NodeContent`, produces `ModelContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp`.

the scene it is given is transformed in place and the model it answers holds shared pointers, both of which are what XNA does with .NET references.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | yes | `ModelProcessor` | `getColorKeyColorProperty() / setColorKeyColorProperty(Color)` |
| `ColorKeyEnabled` | `Boolean` | `True` | yes | `ModelProcessor` | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty(bool)` |
| `DefaultEffect` | `MaterialProcessorDefaultEffect` | `MaterialProcessorDefaultEffect.BasicEffect` | yes | `ModelProcessor` | `getDefaultEffectProperty() / setDefaultEffectProperty(MaterialProcessorDefaultEffect)` |
| `GenerateMipmaps` | `Boolean` | `True` | yes | `ModelProcessor` | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty(bool)` |
| `GenerateTangentFrames` | `Boolean` | -- | yes | `ModelProcessor` | `getGenerateTangentFramesProperty() / setGenerateTangentFramesProperty(bool)` |
| `PremultiplyTextureAlpha` | `Boolean` | `True` | yes | `ModelProcessor` | `getPremultiplyTextureAlphaProperty() / setPremultiplyTextureAlphaProperty(bool)` |
| `PremultiplyVertexColors` | `Boolean` | `True` | yes | `ModelProcessor` | `getPremultiplyVertexColorsProperty() / setPremultiplyVertexColorsProperty(bool)` |
| `ResizeTexturesToPowerOfTwo` | `Boolean` | -- | yes | `ModelProcessor` | `getResizeTexturesToPowerOfTwoProperty() / setResizeTexturesToPowerOfTwoProperty(bool)` |
| `RotationX` | `Single` | `0` | yes | `ModelProcessor` | `getRotationXProperty() / setRotationXProperty(Single)` |
| `RotationY` | `Single` | `0` | yes | `ModelProcessor` | `getRotationYProperty() / setRotationYProperty(Single)` |
| `RotationZ` | `Single` | `0` | yes | `ModelProcessor` | `getRotationZProperty() / setRotationZProperty(Single)` |
| `Scale` | `Single` | `1` | yes | `ModelProcessor` | `getScaleProperty() / setScaleProperty(Single)` |
| `SwapWindingOrder` | `Boolean` | -- | yes | `ModelProcessor` | `getSwapWindingOrderProperty() / setSwapWindingOrderProperty(bool)` |
| `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | yes | `ModelProcessor` | `getTextureFormatProperty() / setTextureFormatProperty(TextureProcessorOutputFormat)` |

### ModelTextureProcessor

*--* -- takes `TextureContent`, produces `TextureContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::ModelTextureProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp`.

Mipmapped and DXT-compressed by default, which is the only difference from the texture processor (measured, processor/ModelTextureProcessor).

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | no | `ModelTextureProcessor` | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `ColorKeyEnabled` | `Boolean` | `True` | no | `ModelTextureProcessor` | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `GenerateMipmaps` | `Boolean` | `True` | no | `ModelTextureProcessor` | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `PremultiplyAlpha` | `Boolean` | `True` | yes | `TextureProcessor` | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `ResizeToPowerOfTwo` | `Boolean` | -- | no | `ModelTextureProcessor` | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |
| `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.DxtCompressed` | no | `ModelTextureProcessor` | `getTextureFormatProperty() / setTextureFormatProperty()` |

### PassThroughProcessor

*No Processing Required* -- takes `Object`, produces `Object`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::PassThroughProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/PassThroughProcessor.hpp`.

An object-to-object processor, so its carrier is the pipeline's ContentObject box.

No properties.

### SongProcessor

*Song - XNA Framework* -- takes `AudioContent`, produces `SongContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::SongProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/AudioProcessors.hpp`.

the audio and the song are shared pointers, which is the lifetime a .NET reference gives them; the default Quality is the measured Best. No longer EXTERNAL_BLOCKED: Microsoft's own Windows Media encoder is unavailable, but the format is not, and a song is a Windows Media file the runtime streams rather than a payload the .xnb carries.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `Quality` | `ConversionQuality` | `ConversionQuality.Best` | yes | `SongProcessor` | `getQualityProperty() / setQualityProperty(ConversionQuality)` |

### SoundEffectProcessor

*Sound Effect - XNA Framework* -- takes `AudioContent`, produces `SoundEffectContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::SoundEffectProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/AudioProcessors.hpp`.

the audio and the answered content are shared pointers, which is the lifetime a .NET reference gives them.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `Quality` | `ConversionQuality` | `ConversionQuality.Best` | yes | `SoundEffectProcessor` | `getQualityProperty() / setQualityProperty(ConversionQuality)` |

### SpriteTextureProcessor

*--* -- takes `TextureContent`, produces `TextureContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::SpriteTextureProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp`.

The texture processor's defaults exactly (measured, processor/SpriteTextureProcessor).

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | no | `SpriteTextureProcessor` | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `ColorKeyEnabled` | `Boolean` | `True` | no | `SpriteTextureProcessor` | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `GenerateMipmaps` | `Boolean` | -- | no | `SpriteTextureProcessor` | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `PremultiplyAlpha` | `Boolean` | `True` | yes | `TextureProcessor` | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `ResizeToPowerOfTwo` | `Boolean` | -- | no | `SpriteTextureProcessor` | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |
| `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | no | `SpriteTextureProcessor` | `getTextureFormatProperty() / setTextureFormatProperty()` |

### TextureProcessor

*Texture - XNA Framework* -- takes `TextureContent`, produces `TextureContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::TextureProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/TextureProcessor.hpp`.

Every default and every step is measured (processor/TextureProcessor and textureprocessor/*): the colour key runs first, then the resize, then the premultiply, then the mipmaps, and the format last; NoChange keeps the bitmap type the texture arrived with, and DxtCompressed picks Dxt1 unless a pixel is partly transparent.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `ColorKeyColor` | `Color` | `Color:{R:255 G:0 B:255 A:255}` | yes | `TextureProcessor` | `getColorKeyColorProperty() / setColorKeyColorProperty()` |
| `ColorKeyEnabled` | `Boolean` | `True` | yes | `TextureProcessor` | `getColorKeyEnabledProperty() / setColorKeyEnabledProperty()` |
| `GenerateMipmaps` | `Boolean` | -- | yes | `TextureProcessor` | `getGenerateMipmapsProperty() / setGenerateMipmapsProperty()` |
| `PremultiplyAlpha` | `Boolean` | `True` | yes | `TextureProcessor` | `getPremultiplyAlphaProperty() / setPremultiplyAlphaProperty()` |
| `ResizeToPowerOfTwo` | `Boolean` | -- | yes | `TextureProcessor` | `getResizeToPowerOfTwoProperty() / setResizeToPowerOfTwoProperty()` |
| `TextureFormat` | `TextureProcessorOutputFormat` | `TextureProcessorOutputFormat.Color` | yes | `TextureProcessor` | `getTextureFormatProperty() / setTextureFormatProperty()` |

### VideoProcessor

*Video - XNA Framework* -- takes `VideoContent`, produces `VideoContent`. SEMANTIC_EQUIVALENT

`Microsoft::Xna::Framework::Content::Pipeline::Processors::VideoProcessor`, declared in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/Processors/VideoProcessor.hpp`.

the video is a shared pointer, which is the lifetime a .NET reference gives it. The default is the measured VideoSoundtrackType.Music, and Process answers its own input rather than a copy.

| Property | Type | XNA default | Configurable | Declared by | CNA |
|---|---|---|---|---|---|
| `VideoSoundtrackType` | `VideoSoundtrackType` | `VideoSoundtrackType.Music` | yes | `VideoProcessor` | `getVideoSoundtrackTypeProperty() / setVideoSoundtrackTypeProperty()` |

## 4. What this document covers

| Quantity | Count |
|---|---:|
| importers | 10 |
| extensions they declare | 18 |
| processors | 12 |
| processor properties | 47 |
| extensions with a CNA route | 18 |

The denominators are frozen (`inventory_freeze.py`), so a regeneration that found one importer fewer would fail rather than quietly renumber this table.
