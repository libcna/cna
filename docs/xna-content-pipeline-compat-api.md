# The XNA 4.0 Content Pipeline compatibility API in C++

This page describes the current C++ mapping of the XNA Game Studio 4.0 Content Pipeline
namespace family (`XNAPP-030`): carriers, registration, contexts and serialization.
The [generated parity map](xna-content-pipeline-parity-report.md) classifies the recorded
[API inventory](../tests/reference/xna40/content-pipeline-api.json); a classification is not
proof of every runtime behaviour or feature combination.

The native coordinator/CLI contract is [content-pipeline.md](content-pipeline.md).
These are build-time C++ components; native extension interfaces are experimental, and existing
content-format/version contracts remain separate. Current defects and qualification limits are
in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## 1. One engine and canonical writers

```text
Microsoft::Xna::Framework::Content::Pipeline::*          public XNA-shaped API (this document)
        │  adapters (CNA::Content::Pipeline::Xna*Component, CNAEXT)
        ▼
CNA::Content::Pipeline::{ContentPipelineRegistry, ContentPipeline, ContentValue, …}
        │
        ├── CNA::Internal::Xnb            (.xnb)
        └── CNA::Content::Cnb             (.cnb)
```

Every XNA type here is a façade, adapter, compatibility base class or strongly typed view over
`CNA::Content::Pipeline` (`modules/content/`) and the build-time module `modules/content-pipeline/`.
The XNA importer/processor bases adapt to canonical ContentImporter/ContentProcessor contracts.
Both use the coordinator's selection, fingerprints, logging and publication. CNB and XNB have
different canonical serializers, and nested synchronous processing has the boundaries in §6;
“one engine” does not mean every operation is a separately scheduled graph node.

The headers live in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/`
(build-time module: a game that only *loads* content never links them). The five
`ContentSerializer*Attribute` descriptors live in `modules/content/include/Microsoft/Xna/Framework/Content/`
because game code puts them on runtime types.

## 2. Spelling rules

| C# | C++ | Why |
|---|---|---|
| `class Foo` (reference type) | `class Foo : public System::Object` with `CNAEXT GetTypeName()` returning the .NET full name | CNA's convention for XNA classes; makes `Foo` polymorphic and boxable |
| property `X { get; set; }` | `getXProperty()` / `setXProperty(v)` | CNA convention (`AGENTS.md`) |
| a reference content value | `std::shared_ptr<T>` through `Carrier<T>` for System::Object-derived T | shared identity permits in-place graph mutation; DateTime/TimeSpan/Decimal have value-carrier specializations |
| value types (int, Vector3, enum) | `T` | primitive API spellings use the established SharpRuntime aliases |
| C# string | `std::string` / SharpRuntime String by value | nullable-string semantics require an explicit optional/specialized carrier where needed |
| `object` as a pipeline payload | `CNA::Content::Pipeline::ContentValue`, spelled `ContentObject` in the façade | the canonical type-erased box; carries the stable .NET type name the XNB writer needs |
| `null` for a reference-typed property | empty `std::shared_ptr`; for `ContentIdentity`, an identity whose three strings are empty | `ContentIdentity` is a small value class here |
| `IEnumerable<T>` return | `std::vector<T>` (or `const std::vector<T>&`) | range-iterable; matches sharp-runtime's `Dictionary::getKeysProperty()` precedent |
| `IDictionary<string,T>`, `Collection<T>`, `ReadOnlyCollection<T>` bases | sharp-runtime `System::Collections::Generic::IDictionary`, `System::Collections::ObjectModel::Collection<T>` / `ReadOnlyCollection<T>` | interfaces preserved as abstract bases (`AGENTS.md`) |
| `Type` | `System::Type` (sharp-runtime, `typeid`-backed) for `InputType`/`OutputType`/`TargetType`; the .NET name comes from `ContentTypeName<T>` | |
| `string.Format(format, params object[])` | variadic template over `std::format` (`{0}` and `{}` both work) forwarding to a non-template virtual; a subclass overriding the virtual adds `using ContentBuildLogger::LogMessage;` to keep the template visible on its own type | C++ virtuals cannot be templates, and an override hides same-named base overloads |
| generic method `BuildAsset<TInput,TOutput>(…)` | member template forwarding to a non-template virtual `BuildAssetCore(…)` carrying the two type names | same reason; a test double overrides the `*Core` virtual |
| explicit interface implementation `object IContentImporter.Import(…)` | `IContentImporter::Import(...)` is **non-virtual** and returns `ContentObject`; `ContentImporter<T>::Import(...)` is the virtual typed one that hides it | the interface spelling is reachable only through the interface, exactly as in C# |
| `[ContentImporter(".png", DisplayName = …)]` | a `ContentImporterAttribute` **descriptor object** passed at registration | C++ has no CLR attributes; the descriptor keeps the attribute's name and properties |
| `[ContentProcessor]`, `[ContentTypeWriter]`, `[ContentTypeSerializer]`, `[ContentSerializer…]` | descriptor objects, same rule | |
| assembly scanning (`PipelineComponentScanner.Update(assemblyPaths)`) | enumeration of a `ContentPipelineRegistry`; the strings name registered **component catalogs** | CNA has no dynamic code loading (`docs/content-pipeline.md`, "Custom extensions") |
| delegate `ChildCallback` | `std::function` with the `Invoke` signature | |
| `Nullable<T>` | `std::optional<T>` | |
| `T[]` | `std::vector<T>` | |
| `ISerializable` constructor / `GetObjectData` on exceptions | not provided (`HOST_SUBSTITUTION`) | .NET binary serialization has no C++ counterpart |

## 3. Stable type names

Each XNA-shaped payload needs its stable .NET-style full name: it selects processors and
writers in the canonical registry, and it is what an `.xnb` type-reader table spells. The trait
`ContentTypeName<T>::Name()` (`ContentTypeName.hpp`) supplies it:

* façade classes declare `static constexpr std::string_view XnaTypeName = "Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent";`
  and the primary template reads it;
* primitives, `std::string`, the math types, `System::TimeSpan` and the containers have
  specializations (`System.Int32`, `Microsoft.Xna.Framework.Vector3`,
  `System.Collections.Generic.List`1[[…]]`, `System.Nullable`1[[…]]`, …);
* `std::shared_ptr<T>` has the name of `T`;
* a game's type declares XnaTypeName or specializes the trait. Its build-time payload name,
  runtime type name and runtime reader identity have separate jobs; configure its writer's
  GetRuntimeType/GetRuntimeReader as well. Merely naming a payload does not register a writer.

`Box<T>(carrier)` and `Unbox<T>(object)` (`ContentObject.hpp`) move a typed carrier into and out
of a ContentObject under that name. Unbox checks the stored C++ carrier type and translates an
empty/wrong carrier to System::InvalidCastException. It does not implement general CLR casts:
a stored shared_ptr<Derived> is not automatically unboxed as shared_ptr<Base>. Holds checks the
declared stable name, so keep that name and the actual carrier consistent.

## 4. Registering an XNA-shaped component

```cpp
using namespace Microsoft::Xna::Framework::Content::Pipeline;

class LevelImporter final : public ContentImporter<LevelContent>
{
public:
    std::shared_ptr<LevelContent> Import(const std::string& filename,
                                         ContentImporterContext& context) override;
};

ContentImporterAttribute levelImporterAttribute({".level"});
levelImporterAttribute.setDisplayNameProperty("Level - MyGame");
levelImporterAttribute.setDefaultProcessorProperty("LevelProcessor");

auto registry = std::make_shared<CNA::Content::Pipeline::ContentPipelineRegistry>();
CNA::Content::Pipeline::RegisterBuiltInContentPipeline(*registry);
CNA::Content::Pipeline::RegisterXnaImporter<LevelImporter>(*registry, "LevelImporter",
                                                           levelImporterAttribute, "1");
CNA::Content::Pipeline::RegisterXnaProcessor<LevelProcessor>(*registry, "LevelProcessor",
                                                             ContentProcessorAttribute{}, "1");
```

This is a registration sketch; LevelContent/LevelProcessor and their stable type names must be
defined by the application. The generic adapter requires default-constructible components.
It derives the canonical identity from the XNA class name and version string, the
source extensions from the descriptor, the imported/processed stable types from
`ContentTypeName<T>`, and constructs a fresh importer/processor per asset — which is what
`BuildContent` does. Processor properties reach the instance through its declared
ProcessorParameter bindings (§5). Registry freeze prevents later registration, but does not
sandbox component code or isolate mutable process-global services such as the XMA encoder
attachment. Built-in font importers snapshot their own search directories (CNA-FIX-L06).

Importer/processor registration alone does not supply an output writer. For XNB, add the
application's ContentTypeWriter to a Serialization::Compiler::ContentCompiler, then call
CNA::Content::Pipeline::RegisterXnaXnbOutput before constructing ContentPipeline/freezing the
registry, and select ContentOutputFormat::Xnb in the build request. Generic XML additionally uses
RegisterXnaXmlSourceContentPipeline. A CLI registry factory receives the invocation's container/
service options so custom writers can bind the same options as built-ins.

## 5. Processor parameters without reflection

XNA discovers a processor's configurable properties by reflection and sets them from
`ProcessorParameters_<Name>` item metadata. The C++ processor declares them once:

```cpp
class LevelProcessor final : public ContentProcessor<LevelContent, CompiledLevel>
{
public:
    [[nodiscard]] SharpRuntime::Single getScaleProperty() const;
    void setScaleProperty(SharpRuntime::Single value);

    static void DescribeParameters(ProcessorParameterBindings<LevelProcessor>& bindings)
    {
        bindings.Add("Scale", &LevelProcessor::getScaleProperty, &LevelProcessor::setScaleProperty,
                     "Scale", "Uniform scale applied to every vertex.");
    }
    …
};
```

The bindings give the bridge typed setters (bool, integer, float, string, enum by name, `Color`,
`Vector*`), give `PipelineComponentScanner` its `ProcessorParameterCollection`
(`PropertyName`, `PropertyType`, `DefaultValue`, `IsEnum`, `PossibleEnumValues`, display name,
description). The project/bridge mapping translates supported item parameters using those bindings.
Native .cna-content.json parameters follow the selected component's contract, which may use
different names from the XNA properties. Strict versus XNA-compatible warning/default handling is
in the native guide. The recorded defaults cover 47 properties, not every processor behaviour.

## 6. Contexts and nested builds

`ContentImporterContext` and `ContentProcessorContext` stay abstract, as in XNA, so a unit test
can subclass them. The bridge supplies concrete implementations over the canonical call-scoped
contexts: `AddDependency` records a source dependency in the canonical collector; `AddOutputFile`
registers a deployment file; `Logger` wraps the canonical logger in a `ContentBuildLogger`;
`TargetPlatform`/`TargetProfile`/`BuildConfiguration`/`IntermediateDirectory`/`OutputDirectory`/
`OutputFilename` come from the build request; `Parameters` is the `OpaqueDataDictionary` view of
the canonical typed parameters.

Convert runs a registered processor in-process. BuildAndLoadAsset imports/processes another
source and returns build-time content, not a runtime Model/Texture2D. It merges source dependencies;
it does not necessarily create an explicit content-build scheduling edge. A TextureContent-specific
bridge preserves the XNA importer/processor object form.

When BuildAsset performs a nested build, the compiled result joins the containing node's additional
outputs, source dependencies, runtime references and deployment files. Automatic names use the
derived source name plus the smallest available numeric suffix (_0, _1, ...); explicit names and
collisions are checked, and repeated equivalent requests can reuse a nested result. Convert shares
nested-name state with its caller.

There is also a sibling shortcut: an unnamed request matching an already planned primary item's
source/processing returns that item's output reference. It adds a runtime reference without
building it, waiting for it or adding an explicit build dependency. A sibling failure therefore
need not invalidate the referencing model node; this is a recorded XNA behaviour.

The CLI's graph-cycle checks apply to explicit content-build edges. They do not establish a general
recursion guard for synchronous Convert/BuildAsset/BuildAndLoadAsset calls. A context without an
active pipeline refuses nested work. At the nested exception boundary, existing InvalidContent/
Pipeline exceptions pass through; other standard exceptions are wrapped with the requested
source's ContentIdentity and inner exception, not universally the outer asset's identity.

## 7. What counts as EXACT_EQUIVALENT, and what "equivalent" does not mean

Three spellings are the baseline C++ form of the XNA API in this repository and do **not** make
a member `SEMANTIC_EQUIVALENT`: the property convention (`getXProperty`/`setXProperty`), the
carrier rule (a reference type as `std::shared_ptr<T>`), and inner exceptions as
`std::exception_ptr`. A member is `SEMANTIC_EQUIVALENT` when its *shape* had to change — an
attribute became a descriptor, a generic virtual became a template over a `*Core` virtual, an
`IEnumerable<T>` became a `std::vector<T>`, an explicit interface implementation became a
non-virtual interface method, a `params object[]` became a variadic `std::format` template —
and the parity map's note says which.

Two idioms differ from C# and are worth knowing:

* the C# indexer setter on a `Collection<T>`-based type is `setItem(index, value)`; an assignment
  through `operator[]` writes the slot directly and bypasses the virtual `SetItem` hook, so a
  `ChildCollection` would not re-parent;
* the C# indexer setter on a `NamedValueDictionary<T>` is `Set(key, value)`, because
  operator[] requires an existing key and direct reference assignment also bypasses SetItem.


* Byte-for-byte identical `.xnb` output is asserted only where XNA's serialization is
  deterministic and CNA is already golden-tested against it; elsewhere the differential harness
  compares semantics (`tools/xna-pipeline-oracle/differential/compare.py`).
* The map is a maintained classification. Metadata/freshness gates verify records and named
  source witnesses; they cannot independently prove that every mapped behaviour has a passing
  regression. Use actual test results and bounded differential observations as execution evidence.

## 8. The intermediate serializer without reflection

XNA's `IntermediateSerializer` reads a type's public properties and fields by reflection and
finds ContentTypeSerializer classes by scanning assemblies. CNA uses
two CNAEXT mechanisms beside the XNA surface, both in
`Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate`:

* **A content description.** A type declares
  `static void DescribeContent(ContentTypeDescriptor<T>& d)` (or specializes
  `ContentTypeDescription<T>`) and lists its members in XNA's order -- public properties first,
  then public fields, each in declaration order, base class first
  (`docs/xna-intermediate-xml-format.md` §3): `d.Field("Name", &T::Name)`,
  `d.Property("X", &T::getXProperty, &T::setXProperty)`, `d.ReadOnlyProperty("Items", &T::getItems)`
  for a get-only collection XNA fills in place, `d.BaseType<Base>()`. The `[ContentSerializer]`
  settings are fluent calls on the returned member descriptor (`.Optional()`, `.AllowNull(false)`,
  `.ElementName("…")`, `.FlattenContent()`, `.SharedResource()`, `.CollectionItemName("…")`,
  `.Ignore()` for `[ContentSerializerIgnore]`); the type-level attributes are
  `d.RuntimeType("…")`, `d.TypeVersion(n)` and `d.CollectionItemName("…")`. The five
  `ContentSerializer*Attribute` classes exist as XNA spells them (in `modules/content`) and are
  what a `ContentTypeSerializer<T>` receives as its `format` argument.
  `DescribedTypeSerializer<T>` is the serializer every described type gets -- CNA's counterpart of
  XNA's reflective serializer. The automatic described XNB writer already consumes this metadata;
  registering/choosing the compiler and output route remains a separate step.
* **A registry.** `IntermediateSerializer::AddTypeSerializer<TSerializer>()` registers a
  user-defined `ContentTypeSerializer<T>` (the `[ContentTypeSerializer]` attribute becomes its
  descriptor argument); `TypeSerializerFor<T>()` returns a type's serializer, creating and
  registering the described, enum, vector, OrderedDictionary, optional and shared_ptr
  and `ExternalReference<T>` serializers on first use. Polymorphic writing dispatches on the
  dynamic type of a `System::Object`-derived reference, so a derived type must have been
  registered (used once, or `TypeSerializerFor<Derived>()`) before an instance of it is written
  through a base-typed member; reading a `Type="…"` attribute resolves names through the same
  registry.

Field/Property bindings preserve their supplied order rather than rediscover CLR declaration
order. BaseType prepends its base bindings ahead of the derived bindings. A get-only collection
getter must return a mutable collection reference so deserialization can fill it in place,
rather than return a copy or const-only view.

Type mapping follows the carrier rule of §2 and adds: `std::vector<T>` is `List<T>` (and reads
T[]), System::Collections::Generic::OrderedDictionary<K,V> is Dictionary<K,V> written in
insertion/enumeration order. std::map is not the built-in dictionary carrier. std::optional<T> is
`Nullable<T>` (and a nullable `string`), `std::shared_ptr<U>` of a non-`Object` `U` gives a value
type reference semantics (identity for shared resources), `ContentObject` is `object`, an enum
needs `CNA_XNA_CONTENT_ENUM(E, "Namespace.E", flags, {E::A, "A"}, …)`. A `std::string` member has
no null, so its ordinary serializer maps Null=true to empty; use optional<string> for custom
members requiring the distinction. Some built-ins, including ContentItem.Name, retain additional
null state through dedicated CNAEXT hooks.

The C# spelling `ContentTypeSerializer` (base) and `ContentTypeSerializer<T>` follows the
`ContentTypeWriterBase` precedent: the base is `ContentTypeSerializerBase`. Everything else in
the namespace keeps XNA's names; the `protected internal` members are `protected` with
`Invoke*` entry points for the reader and writer.

## 9. Bitmaps and textures without reflection

`Microsoft::Xna::Framework::Content::Pipeline::Graphics` gives the texture object model the same
treatment: XNA's shape, with the two places that depend on the CLR replaced by something a C++
program can carry.

* **The pixel type is a compile-time trait, not a run-time lookup.** XNA lets
  `PixelBitmapContent<T>` accept any `T` `VectorConverter` knows, and fails at run time otherwise.
  CNA states the same 22 types in `Graphics/detail/PixelTraits.hpp` -- size, .NET name, surface
  and vertex format, and the conversion to and from `Vector4` -- and a concept refuses the rest
  at compile time. VectorConverter reads the same trait metadata, avoiding a duplicate type table;
  conversion correctness still depends on its numeric witnesses.
* **A bitmap-type registry stands in for reflection.** `ConvertBitmapType(Type)` and the copy
  path need to build a bitmap of a type named at run time, which XNA does by reflecting over the
  assembly. RegisterBitmapType<TBitmap>(dotNetName) records a factory keyed by System::Type,
  with the .NET spelling as metadata. CreateBitmap(Type,width,height) looks up that type key.
  PixelBitmapContent construction registers its concrete type; a game's factory must be registered
  before runtime construction by type. The lazy built-in list includes every supported pixel type,
  including PixelBitmapContent<Vector3>, and the three DXT bitmap classes. Fresh-process
  `XnaGraphicsBitmapColdStartDeathTest` cases verify the Vector3 factory, type metadata and
  conversion from a Color bitmap before any Vector3 bitmap has been constructed.

Two behaviours are worth knowing because they are easy to get wrong and were measured rather than
assumed (`tests/reference/xna40/graphics/graphics-content-oracle.json`):

* `BitmapContent::Copy` is a protocol, not a memcpy, and its order is observable: arguments are
  validated first (`ArgumentOutOfRangeException` naming `sourceRegion` or `destinationRegion`),
  a zero-size region is a no-op, a copy from a bitmap to itself goes through a snapshot, then the
  destination is offered the copy (`TryCopyFrom`), then the source (`TryCopyTo`), and only if both
  decline does the `Vector4` intermediate run. A custom bitmap type participates by overriding
  those two.
* `GetRow` hands out the bitmap's own row -- writing through it changes the bitmap -- so CNA
  answers a `std::span<T>` rather than a copy. `GetPixelData` is the opposite: a snapshot, whose
  bytes are yours to modify.

Region validation compares edges that wrap past Int32 exactly as XNA's `Rectangle.Right`/`Bottom`
do, so a region such as X=Int32.MaxValue, Width=1 passes, as it does in genuine XNA. The copy then
fails the way XNA was measured to (CNA-FIX-D05, owner's choice 2026-11-02,
`tests/reference/xna40/bitmap-region-overflow`): a same-format copy throws
`ArgumentOutOfRangeException("y")` for a row and `ArgumentException` for columns outside a bitmap,
a format conversion `ArgumentOutOfRangeException("y")` or `IndexOutOfRangeException`, a resize
the same for its destination; an empty source region in a resize throws
`ArgumentOutOfRangeException("height")`. Where XNA's resize reads unvalidated source memory and
returns non-source pixels, CNA reads transparent black. None of these paths overflows or touches
memory outside a bitmap (`XnaGraphicsBitmap.OverflowingRegionsFailAsXnaMeasured`).

Resampling is where CNA is deliberately not byte-exact: XNA resizes through D3DX's undocumented
kernel, CNA enlarges bilinearly and reduces with a box filter, and the corpus tests allow the
per-case channel allowances (3, 8 or 12 in the current resampling test). These are finite
witness thresholds, not a universal error bound or a claim of identical filters. DXT blocks are compared by decoding XNA's blocks with CNA's decoder for the
same reason.

## 10. Materials, and where a content item's own members go

`MaterialContent` has no fields: every property of a stock material is a view over two
dictionaries, `OpaqueData` for values and references and `Textures` for texture references. That is
why setting one to null removes its entry instead of storing a null, and why reading one whose
stored value has another type answers null rather than refusing -- both measured, both reproduced.
In C++ the null is an empty `std::optional` for a value property and a null `std::shared_ptr` for a
reference, and the five accessors XNA declares `protected` stay protected, so a game's own material
reaches them the same way.

Two things about serialization follow from the same measurements and apply to every content type,
not only materials:

* **`Name` and `OpaqueData` are serialized members of `ContentItem` itself**, written before a
  derived type's own and omitted while empty; `Identity` is not serialized at all. A described type
  gets them by calling `d.BaseType<ContentItem>()`, which every content type here does.
* **An optional member whose value is an empty string is omitted**, exactly as an optional empty
  collection is. That is how a content item with no name writes no `<Name>` element.

`EffectContent::EffectCode` is the one place where CNA prefers `std::optional<std::string>` to
`std::string`: an effect with no source serializes as `<EffectCode Null="true" />`, and a
`std::string` cannot tell that from an empty one.

---

## 11. The model processor, and what it does to the scene you give it

ModelProcessor mutates its input graph; other processors, including MaterialProcessor, can also
mutate their inputs. Its Scale and three Rotation properties form an adjustment baked through
MeshHelper::TransformScene when that matrix is non-identity. Positions move, Vector3 channels named
Normal/Tangent/Binormal are transformed/normalized, and node transforms change frame. Identity
adjustment deliberately skips that normalization, retaining authored normal bits. The `MeshContent` the caller still holds is the transformed one. That
is XNA's own behaviour, measured; a caller that needs its scene afterwards must copy it first.

Three more measured rules a reading of the API would not give:

* The three rotations compose as **Z, then X, then Y**, and the uniform scale follows.
* `PremultiplyVertexColors` scales each colour channel by its own alpha **in whole bytes, dropping
  the remainder**. A channel value of 129 at alpha 3 answers 1, not the 2 rounding would give.
* `GenerateTangentFrames` appends `Tangent0` and `Binormal0` after the channels already there, and
  refuses a batch with no `TextureCoordinate0` by that name.

The virtual `ProcessGeometryUsingMaterial` takes **a material and the batches that share it**,
not one batch: the processor groups a mesh's geometry by material, converts the material once
through `MaterialProcessor`, and gives every batch in the group the converted result. A batch with
no material of its own is given a `BasicMaterialContent` before that conversion.

`VertexBufferContent::SizeOf` answers the size .NET *marshals* a value to rather than the size C++
gives it: `Boolean` is 4 bytes and `Char` is 1. A type no vertex element can carry is refused with
`NotSupportedException`, and the null type with `ArgumentNullException`.

---

## 12. How a processed model reaches an `.xnb`

There is one model writer, and it is the canonical one. `ContentCompiler` knows a built-in writer
for `ModelContent` that turns the graph into `CNA::Internal::Xnb::XnbModelData` through
`CNA::Content::Pipeline::ToCanonicalModel` and hands it to the same writer every other model in the
engine goes through. Nothing new appears in the XNA namespace for it: XNA's own `ModelWriter` is
internal to its pipeline assembly, and so is this one.

What the conversion decides, because the XNB format asks and the content model does not say:

* One shared resource per **distinct** vertex buffer, index buffer and material, so two parts that
  share a buffer share its resource.
* Indices are sixteen bits unless one does not fit, and then all of them are thirty-two.
* The five stock materials map to their canonical effect resources. XNB also supports
  EffectMaterialContent with a compiled-effect reference and supported scalar/vector/matrix/
  quaternion/texture parameters, serialized in sorted key order. Unknown material or parameter
  kinds are refused. Native CNB Model conversion remains narrower; XNB effect-material support
  does not create a CNB Effect schema.

A game that wants its own model format writes its own `ContentTypeWriter`; a game that wants XNA's
writes a `ModelContent` and compiles it.

## 13. Verification scope

The 2026-11-01 audit rebuilt the existing CnaContentPipelineTests target and ran privately on
OPENGLES3 with dummy audio: 112 selected carrier/context/bridge/bitmap/material/compiler cases
passed, followed by 75 selected intermediate/model cases, including parameterized suites selected
with a leading wildcard. These checks exercise the named contracts; they are not fresh execution
of every recorded Microsoft XNA measurement or a new sanitizer/native-Windows campaign.
