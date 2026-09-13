# The XNA 4.0 Content Pipeline compatibility API in C++

`plans/plan_xnapipeline_parity.md` `XNAPP-030`. This document is the design contract for the
`Microsoft::Xna::Framework::Content::Pipeline` namespace family: how every public XNA Game Studio
4.0 Content Pipeline type is spelled in C++, which substitutions C++ forces, and how the façade
reaches the one canonical engine underneath it. The generated parity matrix is
`docs/xna-content-pipeline-parity-report.md`; the denominator it measures against is
`tests/reference/xna40/content-pipeline-api.json`.

## 1. One engine, one writer — the façade is a view

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
There is no second registry, no second build graph and no second serializer. The XNA importer and
processor base classes are adapted into the canonical `ContentImporter`/`ContentProcessor`
contracts by two generic bridges, so a component written against the XNA shape is scheduled,
fingerprinted, logged and published exactly like a native one.

The headers live in `modules/content-pipeline/include/Microsoft/Xna/Framework/Content/Pipeline/`
(build-time module: a game that only *loads* content never links them). The five
`ContentSerializer*Attribute` descriptors live in `modules/content/include/Microsoft/Xna/Framework/Content/`
because game code puts them on runtime types.

## 2. Spelling rules

| C# | C++ | Why |
|---|---|---|
| `class Foo` (reference type) | `class Foo : public System::Object` with `CNAEXT GetTypeName()` returning the .NET full name | CNA's convention for XNA classes; makes `Foo` polymorphic and boxable |
| property `X { get; set; }` | `getXProperty()` / `setXProperty(v)` | CNA convention (`AGENTS.md`) |
| a reference-type value passed around | `std::shared_ptr<T>` (the **carrier**, `Carrier<T>`) | .NET reference semantics: processors mutate their input graphs in place |
| a value-type value (`int`, `Vector3`, `string`, enum) | `T` | |
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

Every value that crosses the pipeline needs its .NET-style full name: it selects processors and
writers in the canonical registry, and it is what an `.xnb` type-reader table spells. The trait
`ContentTypeName<T>::Name()` (`ContentTypeName.hpp`) supplies it:

* façade classes declare `static constexpr std::string_view XnaTypeName = "Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent";`
  and the primary template reads it;
* primitives, `std::string`, the math types, `System::TimeSpan` and the containers have
  specializations (`System.Int32`, `Microsoft.Xna.Framework.Vector3`,
  `System.Collections.Generic.List`1[[…]]`, `System.Nullable`1[[…]]`, …);
* `std::shared_ptr<T>` has the name of `T`;
* a game's own type either declares `XnaTypeName` or specializes the trait — this is the same
  obligation XNA places on a custom `ContentTypeWriter<T>` through `GetRuntimeType`.

`Box<T>(carrier)` and `Unbox<T>(object)` (`ContentObject.hpp`) move a typed carrier into and out
of a `ContentObject` under that name; a mismatched `Unbox` throws `System::InvalidCastException`,
which is what the `(T)value` cast in `OpaqueDataDictionary.GetValue<T>` throws in XNA.

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

The adapter derives the canonical identity from the XNA class name and the version string, the
source extensions from the descriptor, the imported/processed stable types from
`ContentTypeName<T>`, and constructs a fresh importer/processor per asset — which is what
`BuildContent` does. Processor properties reach the instance through its declared
`ProcessorParameter` bindings (§5), never through RTTI.

## 5. Processor parameters without reflection

XNA discovers a processor's configurable properties by reflection and sets them from
`ProcessorParameters_<Name>` item metadata. The C++ processor declares them once:

```cpp
class LevelProcessor final : public ContentProcessor<LevelContent, CompiledLevel>
{
public:
    [[nodiscard]] float getScaleProperty() const;
    void setScaleProperty(float value);

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
description), and give the `.contentproj` reader (`XNAPP-240`) the names it must accept. Built-in
processors declare exactly the properties the inventory lists, with the defaults it measured.

## 6. Contexts and nested builds

`ContentImporterContext` and `ContentProcessorContext` stay abstract, as in XNA, so a unit test
can subclass them. The bridge supplies concrete implementations over the canonical call-scoped
contexts: `AddDependency` records a source dependency in the canonical collector; `AddOutputFile`
registers a deployment file; `Logger` wraps the canonical logger in a `ContentBuildLogger`;
`TargetPlatform`/`TargetProfile`/`BuildConfiguration`/`IntermediateDirectory`/`OutputDirectory`/
`OutputFilename` come from the build request; `Parameters` is the `OpaqueDataDictionary` view of
the canonical typed parameters.

`Convert<TInput,TOutput>` runs a registered processor in-process. `BuildAndLoadAsset` runs importer
and processor for another source in-process and records the dependency edge.
`BuildAsset` additionally writes the nested asset as an **additional output** of the current node
under the derived or given asset name, returns an `ExternalReference<TOutput>` to it, and records
a runtime reference — the canonical multi-output path the glTF route already uses, so nested
outputs are owned, fingerprinted and cleaned like every other artifact. Cycles are refused by the
canonical graph, and every nested failure carries the outer `ContentIdentity`.

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
  `operator[]` cannot add a key without a value.


* Byte-for-byte identical `.xnb` output is asserted only where XNA's serialization is
  deterministic and CNA is already golden-tested against it; elsewhere the differential harness
  compares semantics (`plans/plan_xnapipeline_parity.md` §24).
* A type is not "implemented" because its header compiles. The parity map records
  `EXACT_EQUIVALENT`/`SEMANTIC_EQUIVALENT` only once the behaviour is tested; until then the row is
  `MISSING`, however much code exists.

## 8. The intermediate serializer without reflection

XNA's `IntermediateSerializer` reads a type's public properties and fields by reflection and
finds `ContentTypeSerializer` classes by scanning assemblies. C++ has neither, so Phase 5 adds
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
  XNA's reflective serializer -- and the same description will drive the automatic XNB writer.
* **A registry.** `IntermediateSerializer::AddTypeSerializer<TSerializer>()` registers a
  user-defined `ContentTypeSerializer<T>` (the `[ContentTypeSerializer]` attribute becomes its
  descriptor argument); `TypeSerializerFor<T>()` returns a type's serializer, creating and
  registering the described, enum, `std::vector`, `std::map`, `std::optional`, `std::shared_ptr`
  and `ExternalReference<T>` serializers on first use. Polymorphic writing dispatches on the
  dynamic type of a `System::Object`-derived reference, so a derived type must have been
  registered (used once, or `TypeSerializerFor<Derived>()`) before an instance of it is written
  through a base-typed member; reading a `Type="…"` attribute resolves names through the same
  registry.

Type mapping follows the carrier rule of §2 and adds: `std::vector<T>` is `List<T>` (and reads
`T[]`), `std::map<K,V>` is `Dictionary<K,V>` written in key order, `std::optional<T>` is
`Nullable<T>` (and a nullable `string`), `std::shared_ptr<U>` of a non-`Object` `U` gives a value
type reference semantics (identity for shared resources), `ContentObject` is `object`, an enum
needs `CNA_XNA_CONTENT_ENUM(E, "Namespace.E", flags, {E::A, "A"}, …)`. A `std::string` member has
no null, so `Null="true"` reads as the empty string -- use `std::optional<std::string>` where XNA
code relies on null.

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
  at compile time. `VectorConverter`'s tables are that same trait read back, so the two cannot
  drift.
* **A bitmap-type registry stands in for reflection.** `ConvertBitmapType(Type)` and the copy
  path need to build a bitmap of a type named at run time, which XNA does by reflecting over the
  assembly. `BitmapContent::RegisterBitmapType<TBitmap>("Microsoft.Xna.Framework.…")` records a
  factory under the .NET name, and `CreateBitmap(Type, width, height)` uses it. Every stock
  bitmap registers itself; a game's own bitmap type registers once, beside where it is defined.

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

Resampling is where CNA is deliberately not byte-exact: XNA resizes through D3DX's undocumented
kernel, CNA enlarges bilinearly and reduces with a box filter, and the corpus tests allow the
measured difference (at most 8 channel units on the cases they cover) rather than pretending the
filters are the same. DXT blocks are compared by decoding XNA's blocks with CNA's decoder for the
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

`ModelProcessor` is the one processor that **changes its input**. Its `Scale` and three `Rotation`
properties are not carried on the model's root bone: they are baked into the scene before anything
else happens. Every mesh's positions are transformed, every direction channel is transformed and
made unit again, and every node's transform is re-expressed in the new frame, so the scene comes
out transformed exactly once. The `MeshContent` the caller still holds is the transformed one. That
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
* Each of the five stock materials becomes its own canonical effect resource, carrying the
  properties it actually set; a material of any other type is refused by name.

A game that wants its own model format writes its own `ContentTypeWriter`; a game that wants XNA's
writes a `ModelContent` and compiles it.
