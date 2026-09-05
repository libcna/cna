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
