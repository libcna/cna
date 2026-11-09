# Migrating an XNA 4.0 content project to CNA

This is the porting guide: what you already have, what it becomes, and what to type. The
neighbouring documents answer the other questions --
[`xna-content-pipeline-components.md`](xna-content-pipeline-components.md) is the generated
reference for every importer, processor and property;
[`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md) is the C# to C++
mapping rule by rule; [`content-pipeline.md`](content-pipeline.md) is the canonical engine
underneath; and [`xna-content-pipeline-parity-report.md`](xna-content-pipeline-parity-report.md) is
the parity matrix with a status for every public member.

Two starting points determine how much migration is needed:

* **Your `.contentproj` is an input.** CNA builds its supported project/item subset directly;
  this does not provide a general MSBuild engine or load a project's managed pipeline assemblies.
* **Existing `.xnb` files may be reused.** Check that CNA supports their readers, payloads and
  runtime renderer requirements; custom reader registration is still required. CNA also writes
  XNB. The dated genuine-XNA interoperability results cover specific fixtures and observable
  values, not every asset that uses the container; see
  [`xnb-interoperability.md`](xnb-interoperability.md) §11.

---

## 1. Three ways in, in order of how little work they are

| You have | You run | Where it is documented |
|---|---|---|
| A `.contentproj` | `cna-content build MyGame.contentproj -o Content` | §2 |
| A directory of source assets | `cna-content build ContentSource -o Content` | §3 |
| Your own importers and processors in C# | a small C++ compiler binary that registers them | §7 |

All three drive the same coordinator. Project component mappings and native directory conventions
can select different routes and defaults. Container, target platform and graphics profile can also
affect processing, not just the final writer.

---

## 2. Building the `.contentproj` you already have

**Before** -- Visual Studio builds the content project through MSBuild, and the game project
references it:

```xml
<ProjectReference Include="Content\MyGameContent.contentproj">
  <Name>MyGameContent</Name>
  <XnaReferenceType>Content</XnaReferenceType>
</ProjectReference>
```

**After** -- the same file, built by the tool:

```bash
cna-content build Content/MyGameContent.contentproj -o Content/bin
```

or, from CMake, as a target your game depends on:

```cmake
cna_add_content(
    TARGET MyGameContent
    CONTENT_PROJECT Content/MyGameContent.contentproj
    OUTPUT_DIR Content
)
add_dependencies(MyGame MyGameContent)
```

**What the project decides, and CNA honours**: `XnaPlatform`, `XnaProfile`,
`XnaCompressContent`, each item's `<Importer>`, `<Processor>`, `<Name>` and every
`<ProcessorParameters_X>`, and the `Content`/`None` items, which are *copied* rather than built,
honouring `CopyToOutputDirectory` and `Link` -- because that is what XNA's targets do with them.
Windows-style item separators are normalized on the current host. The parser evaluates its
supported property substitutions and quoted-string comparison conditions in document order; it
refuses conditions it cannot evaluate. Arbitrary MSBuild tasks, targets and expressions are not
implemented. Linked sources remain subject to the build's path containment rules.

A project build takes **XNA's leniency**, not the tool's own strictness: an unrecognised processor
parameter is dropped with a warning and the processor keeps its default, exactly as XNA does. §11
says what that means and how to get the strict behaviour back.

**What makes the build fail**:

* A project whose `<Reference>` or `<ProjectReference>` names a **pipeline assembly**. C++ has no
  assembly loading, so a custom importer cannot be discovered from a DLL path; compile it into your
  own compiler binary instead (§7). The diagnostic names the assembly; routable built-in assets
  can still be published before the task reports failure.
* A project naming an importer or processor this build has not got. All of them are named **at
  once**. Unroutable assets are omitted while routable assets are attempted. The invocation still
  returns failure; finding some `.xnb` files in the output directory does not mean the project built.

The CLI accepts explicit `--xnb-platform`, `--xnb-profile` and `--xnb-compress` overrides of the
project properties. `--fx-compiler`, `--xma-encoder` and `--font-directory` services also reach the
project build. `--build-configuration Release` overrides the project's selected configuration;
without that option, the project configuration is used (normally its `Debug` default).

The CMake helper has a narrower interface: `CONTENT_PROJECT` cannot be combined with `FORMAT`,
`XNB_*`, `CONFIG_FILE` or `XNA_COMPATIBLE`. Use the project's properties for those choices in this
form, or invoke the CLI explicitly when overrides are needed. Relative `OUTPUT_DIR` is under the
caller's CMake binary directory; CLI `-o` is relative to its working directory.

---

## 3. Building a source directory instead

If the project file was only ever a list of files with default processors, you do not need it:

```bash
cna-content build ContentSource -o Content
```

This uses CNA's native convention routes and defaults to CNB output. It does not reproduce every
XNA component default merely by recognizing the same extension. For a legacy source directory,
start with `--format xnb` and set the required component/parameters explicitly. Some routes,
including generic intermediate XML and legacy effects, have XNB writers only. The native route
table and its limits are in [`content-pipeline.md`](content-pipeline.md).

The relative path becomes the content name, so `ContentSource/Textures/wall.png` is loaded as
`Textures/wall`. The generated component reference describes XNA importer/processor declarations;
it is not the native directory convention table.

Per-asset overrides that a directory cannot express live in an optional `.cna-content.json` beside
the sources -- logical name, importer, processor, writer and typed parameters -- documented in
[`content-pipeline.md`](content-pipeline.md), *Optional per-asset configuration*.

---

## 4. Which container to build: `.xnb` or `.cnb`

Many routes share source import and processing code across the two containers, but output format
and target settings can affect processing. For example, sprite-font atlas encoding and XNB Reach
limits are format/profile decisions. Writer availability and supported material graphs also differ;
see [`cnb-format.md`](cnb-format.md) and the native guide.

| | `.xnb` | `.cnb` |
|---|---|---|
| What it is | the XNA 4.0 container | CNA's own container |
| Choose it when | you want XNA-compatible output, a `.contentproj`, or content a genuine XNA runtime must load | you are building for CNA only |
| Selected by | `--format xnb` (a `.contentproj` implies it) | the default |
| Platform/profile/compression | `--xnb-platform`, `--xnb-profile`, `--xnb-compress` | no XNB platform/profile header; CNA's own per-kind schemas and compression policy apply |

```bash
cna-content build ContentSource -o Content --format xnb \
    --xnb-platform windows --xnb-profile reach --xnb-compress lzx
```

```cmake
cna_add_content(
    TARGET MyGameContent
    SOURCE_DIR ContentSource
    OUTPUT_DIR Content
    FORMAT xnb
    XNB_PLATFORM windows
    XNB_PROFILE reach
    XNB_COMPRESS lzx
)
```

`--xnb-compress lzx` is the compression XNA itself produced and the only compressed form an XNA 4.0
runtime loads. `--xnb-platform xbox360` is refused unless you add
`--xnb-allow-unverified-xbox`, because this build has one piece of Xbox byte-order handling and
writing the `x` header byte over the other payloads would claim a compatibility it cannot deliver.
[`xnb-interoperability.md`](xnb-interoperability.md) is the full account of what the writer
guarantees and how strongly each claim is verified.

The logical runtime name can stay the same when the requested type and payload are supported:

```cpp
auto wall = content.Load<Texture2D>("Textures/wall");
```

Use separate output roots when comparing formats, and deploy the intended format's complete
outputs and referenced files. Building one format does not establish that the other can represent
the same asset or that a runtime can load it.

---

## 5. Processor parameters

**Before** -- item metadata in the project file:

```xml
<Compile Include="Textures\hero.png">
  <Name>hero</Name>
  <Importer>TextureImporter</Importer>
  <Processor>TextureProcessor</Processor>
  <ProcessorParameters_ColorKeyEnabled>False</ProcessorParameters_ColorKeyEnabled>
  <ProcessorParameters_GenerateMipmaps>True</ProcessorParameters_GenerateMipmaps>
</Compile>
```

**After** -- the same, if you are building the project. If you are building a directory, the same
parameters live in `.cna-content.json` with an explicit type per value:

```json
{
  "format": "CNA.ContentPipeline.Config",
  "version": 1,
  "assets": {
    "Textures/hero.png": {
      "processor": "TextureProcessor",
      "parameters": {
        "ColorKeyEnabled": { "type": "bool", "value": false },
        "GenerateMipmaps": { "type": "bool", "value": true }
      }
    }
  }
}
```

**After, in C++** -- the property on the processor object itself:

```cpp
TextureProcessor processor;
processor.setColorKeyEnabledProperty(false);
processor.setGenerateMipmapsProperty(true);
```

The JSON example selects the XNA `TextureProcessor` alias deliberately. A native component such as
`CNA.TextureProcessor` has its own parameter spelling and defaults; do not apply XNA property names
to an arbitrary native route.

The recorded properties, their types, XNA defaults and C++ accessors are in
[`xna-content-pipeline-components.md`](xna-content-pipeline-components.md) §3. The defaults are not
transcribed: they are the values Microsoft's own classes answered when the oracle read them, and a
test compares freshly constructed CNA processors against that measurement. Those 47 property
defaults do not establish parity of every processor operation.

---

## 6. Your game's own content types on the `.xml` route

XNA's `XmlImporter` reads an intermediate XML document naming any type, and
`IntermediateSerializer` fills it by reflection. C++ has no reflection, so the type says what its
members are -- once, in a static function.

**Before** (C#):

```csharp
public class Quest
{
    public string Name { get; set; }
    public List<Vector3> Waypoints = new List<Vector3>();
}
```

```xml
<XnaContent><Asset Type="QuestGame.Quest">
  <Name>Rescue</Name>
  <Waypoints><Item>1 2 3</Item></Waypoints>
</Asset></XnaContent>
```

**After** (C++), the same document, and a description instead of reflection:

The snippets below use these headers and aliases. They illustrate the registration contract;
the complete executable linked in §7 also supplies importer/processor bodies and argument handling.

```cpp
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "CNA/Content/Pipeline/XnaXmlSourceContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Canon = CNA::Content::Pipeline;
namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
namespace Compiler = Xna::Serialization::Compiler;
namespace Intermediate = Xna::Serialization::Intermediate;
using Microsoft::Xna::Framework::Vector3;
```

```cpp
class Quest final : public Xna::ContentItem
{
public:
    static constexpr std::string_view XnaTypeName = "QuestGame.Quest";

    std::string name;
    std::vector<Vector3> waypoints;

    [[nodiscard]] const std::string& GetTypeName() const override
    {
        static const std::string value{XnaTypeName};
        return value;
    }

    static void DescribeContent(Intermediate::ContentTypeDescriptor<Quest>& d)
    {
        d.Field("Name", &Quest::name);
        d.Field("Waypoints", &Quest::waypoints);
    }
};
```

List members explicitly in the measured XNA serialization order: base members first, then the
current type's public properties and fields. This example describes the original plain C# Quest
payload; it intentionally does not add `ContentItem` base members. For a source type that actually
serializes a base class, declare that base with `d.BaseType<Base>()`. The `[ContentSerializer…]`
attributes are fluent calls on the
member (`.Optional()`, `.ElementName("…")`, `.SharedResource()`, …); the type-level ones are
`d.RuntimeType("…")`, `d.TypeVersion(n)`, `d.CollectionItemName("…")`. The mapping rules, including
the one that bites -- a `std::string` has no null, so `Null="true"` reads as the empty string and
`std::optional<std::string>` is what XNA's nullable string is --  are in
[`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md) §8, and the document
format itself in [`xna-intermediate-xml-format.md`](xna-intermediate-xml-format.md).

The type must be registered before a document can name it, because an untyped read resolves a name
through the registry:

```cpp
(void)Intermediate::IntermediateSerializer::TypeSerializerFor<Quest>();
```

---

## 7. Custom importers, processors and writers

XNA discovers these by scanning assemblies for attributes. CNA's pipeline registers C++ components
in code: **a compiler executable of your own** registers them and runs the coordinator used by
`cna-content`. Port component bodies using the API mapping guide; their base classes remain
`ContentImporter<T>`, `ContentProcessor<TInput,TOutput>` and `ContentTypeWriter<T>`.

**Before** (C#), the attribute is the registration:

```csharp
[ContentImporter(".quest", DefaultProcessor = "QuestProcessor",
                 DisplayName = "Quest - QuestGame")]
public class QuestImporter : ContentImporter<Quest>
{
    public override Quest Import(string filename, ContentImporterContext context) { … }
}
```

**After** (C++), the attribute becomes a descriptor object passed at registration:

```cpp
class QuestImporter final : public Xna::ContentImporter<Quest>
{
public:
    [[nodiscard]] std::shared_ptr<Quest> Import(const std::string& filename,
                                                Xna::ContentImporterContext& context) override;
};

Xna::ContentImporterAttribute attribute(".quest");
attribute.setDefaultProcessorProperty("QuestProcessor");
attribute.setDisplayNameProperty("Quest - QuestGame");
Canon::RegisterXnaImporter<QuestImporter>(*registry, "QuestImporter", attribute, "1",
                                          "QuestGame.Pipeline");
```

A processor's configurable properties are declared once instead of being found by reflection:

```cpp
static void DescribeParameters(Xna::ProcessorParameterBindings<QuestProcessor>& bindings)
{
    bindings.Add<SharpRuntime::intcs>("Repeats", &QuestProcessor::getRepeatsProperty,
                               &QuestProcessor::setRepeatsProperty, "Repeats",
                               "How many times the quest may be taken.");
}
```

This assumes `QuestProcessor` defines the matching getter/setter and processing body. A writer for
the Quest in §6 names the original game's reader and writes the payload that reader must consume:

```cpp
class QuestWriter final : public Compiler::ContentTypeWriter<Quest>
{
public:
    [[nodiscard]] std::string GetRuntimeReader(Xna::TargetPlatform) const override
    {
        return "QuestGame.QuestReader, QuestGame";
    }

protected:
    void Write(Compiler::ContentWriter& output, const std::shared_ptr<Quest>& value) override
    {
        output.Write(value->name);
        output.Write(static_cast<SharpRuntime::intcs>(value->waypoints.size()));
        for (const Vector3& point : value->waypoints) { output.Write(point); }
    }
};
```

Inside the executable's entry point, `arguments` is a `std::vector<std::filesystem::path>` and
`attribute` is the descriptor above. The registry factory wires both the custom source route and
the XML route to the same writer compiler:

```cpp
return Canon::RunContentCompiler(arguments, [attribute](const Canon::ContentCompilerOptions& options)
{
    auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
    Canon::RegisterBuiltInContentPipeline(*registry, options);
    Canon::RegisterXnaImporter<QuestImporter>(*registry, "QuestImporter", attribute);
    Canon::RegisterXnaProcessor<QuestProcessor>(*registry, "QuestProcessor", {});

    auto compiler = std::make_shared<Compiler::ContentCompiler>();
    compiler->AddTypeWriter<QuestWriter>();
    (void)Intermediate::IntermediateSerializer::TypeSerializerFor<Quest>();
    Canon::RegisterXnaXmlSourceContentPipeline(*registry, compiler, options.xnbContainer);
    return registry;
});
```

`RegisterXnaXmlSourceContentPipeline` also registers XNB output. For a custom route without XML,
call `RegisterXnaXnbOutput` instead. Finish registration before constructing a `ContentPipeline`,
which freezes its registry. These writers supply XNB output, so select `FORMAT xnb` below.

and CMake points the content target at it:

```cmake
add_executable(my_content_compiler my_content_compiler.cpp)
target_link_libraries(my_content_compiler PRIVATE CNA::ContentCompiler)

cna_add_content(
    TARGET MyGameContent
    SOURCE_DIR ContentSource
    OUTPUT_DIR Content
    CONTENT_EXECUTABLE "$<TARGET_FILE:my_content_compiler>"
    FORMAT xnb
)
add_dependencies(MyGameContent my_content_compiler)
```

[`modules/content-pipeline/examples/xna-custom-pipeline.cpp`](../modules/content-pipeline/examples/xna-custom-pipeline.cpp)
is a complete, larger example with its own Quest/QuestStep/QuestBook payloads: a `.quest` extension,
a sidecar dependency, a nested
build, a shared resource written through two references, declared parameters, and the same game's
types reached through the built-in `.xml` route. Target `cna_xna_custom_pipeline_example` and the
`XnaCustomPipelineAcceptanceTest` suite check that integration when examples/tests are enabled;
the guide's smaller Quest payload is not interchangeable with that example's runtime payload.

**The runtime half is not optional.** A writer that names `QuestGame.QuestReader` needs a reader
registered under that name, or the `.xnb` will not load:

```cpp
Microsoft::Xna::Framework::Content::ContentTypeReaderManager::AddTypeCreator("QuestGame.QuestReader",
                                         [] { return std::make_unique<QuestReader>(); });
```

Include `Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp` and supply a `QuestReader`
that reads the writer's string, count and Vector3 values in that order. This is CNA's runtime
registration; a genuine XNA runtime requires the matching managed reader in its existing game
assembly. Matching reader names alone does not establish matching payloads.

---

## 8. Dependencies, nested builds and external references

These keep their XNA shape. Inside an importer or processor:

| XNA | CNA |
|---|---|
| `context.AddDependency(path)` | the same -- a source whose change rebuilds this asset |
| `context.AddOutputFile(path)` | registers a generated deployment file; output-relative subdirectories are preserved subject to containment checks |
| `context.Convert<TIn,TOut>(value, "Processor")` | the same -- runs a processor in-process |
| `context.BuildAndLoadAsset<TIn,TOut>(ref, "Processor")` | synchronously imports/processes another source and returns build-time content, not a runtime asset |
| `context.BuildAsset<TIn,TOut>(ref, "Processor")` | returns an `ExternalReference<TOutput>`; a nested build publishes an additional output, while a matching planned sibling can supply only its output reference |
| `context.Logger.LogMessage("{0}", x)` | `context.getLoggerProperty().LogMessage("{0}", x)` |

Nested compiled outputs belong to the containing node and participate in its manifest and cleanup.
Automatic nested names use suffixes such as `_0`, `_1`; source dependencies, runtime references and
deployment files have separate roles. The planned-sibling shortcut does not build or wait for the
sibling, or add a scheduling edge; its failure need not invalidate the referencing node. Explicit
graph-cycle checks do not provide a general recursion guard for synchronous context calls.
See the API guide §6 for the exact boundaries.

---

## 9. Driving the MSBuild task directly

If your build system already models XNA's task, the task itself exists:

```cpp
namespace Tasks = Microsoft::Xna::Framework::Content::Pipeline::Tasks;
Tasks::BuildContent task;
task.setRootDirectoryProperty("Content");
task.setOutputDirectoryProperty("Content/bin");
task.setIntermediateDirectoryProperty("Content/obj");
task.setTargetPlatformProperty("Windows");
task.setTargetProfileProperty("Reach");
task.setSourceAssetsProperty(items);
const bool built = task.Execute();
```

The properties, the `bool Execute()` answer and the item-valued outputs are XNA's. What is replaced
is the hosting: there is no MSBuild engine, so nothing sets these for you and nothing calls
`Execute()` -- which is what `cna-content build <project>.contentproj` does on your behalf.

Include `Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp`; `items` must be a
`std::vector<Tasks::TaskItem>` carrying source names and the intended Importer/Processor metadata.
Link the compiler library. A custom host supplies `setRegistryFactoryEXT` when it needs its own
components or compiler services. Check `Execute()` and its diagnostics even when output items
exist: the task can report partial-build failure.

---

## 10. Verifying the port

Use complementary checks:

1. **Check the invocation and expected outputs.** Require successful exit status, inspect warnings,
   and compare the expected logical assets and deployment files with what was produced. A cache
   hit can skip processing, a directory build can omit unconfigured assets, and a failed project
   build can still publish other assets. Build success alone does not prove the game loaded them.
2. **Inspect supported XNB payloads independently.**
   `python3 tools/xnb/xnb_conformance.py <file>.xnb` parses the supported reader/payload forms
   without CNA code. `--expect <manifest.json>` also compares declared expected values. It is
   not a universal parser for game-defined readers, and structural validity does not establish
   runtime compatibility. A custom Quest payload needs its own reader and value assertions.
3. **Load in the intended runtime and assert values.** Test CNA's runtime route for the game.
   If genuine-XNA compatibility is required, `tests/interop/xna40/` provides a harness for a finite
   corpus; see its README and the dated interoperability report for prerequisites and coverage.
   The recorded LZX trailer defect demonstrates why agreement between CNA and the Python parser
   was insufficient. New fixtures/custom readers require matching harness support.

Run window/GPU checks through [`tools/platform/run_gpu_tests_private.sh`](../tools/platform/run_gpu_tests_private.sh)
on this repository's Linux host, never on the owner's live desktop.

---

## 11. What behaves differently, and why

* **Strictness.** By default `cna-content` refuses a processor parameter naming a property that
  does not exist, a value it cannot convert, and a `.spritefont` character region the font has no
  glyphs for. XNA warns and carries on for all three. `--xna-compatible` reproduces XNA's
  warning/default handling for these cases; a `.contentproj` build selects it automatically.
  Omitting the flag does not make a project build strict. To request strict handling, express the
  intended items/components in a native directory/JSON build and omit `--xna-compatible`; select
  XNB separately if needed. See the native guide for other validation and discovery rules.
* **No assembly scanning.** `PipelineComponentScanner` enumerates a registry rather than loading
  DLLs. Pipeline assembly references make the project build fail after routable work (§2, §7).
* **No designer.** The `[DisplayName]`/`[Description]`/`[DefaultValue]` attributes survive as
  descriptor data that `PipelineComponentScanner` reports, but nothing draws a property grid.
* **XMA.** CNA has no in-tree conforming encoder. Without a configured external encoder, asking
  for it refuses with `XMA ENCODER EXTERNALLY UNAVAILABLE`. `--xma-encoder` connects an external
  process; captured fixture bytes are not a general encoder. See
  [`xma-encoder-backend.md`](xma-encoder-backend.md) for fingerprint, loop and native-Xbox limits.
* **WMA and WMV.** These use optional FFmpeg-backed build-time routes. Availability depends on the
  configured FFmpeg support, separate from the runtime video option. Recorded media-oracle results
  include host failures and metadata-only observations; current CNA encoding policy is not a
  successful Microsoft encoder measurement. See
  [`xna-content-pipeline-media.md`](xna-content-pipeline-media.md).
* **FBX.** The recorded Microsoft importer uses FBX SDK 2011.3.1 and rejects the measured newer
  forms. CNA also accepts supported 7400+ documents; this deliberate extension does not promise
  every FBX SDK feature or every exporter variant. See the component and model documentation.

The parity report records mapped member status and intentional differences. Its absence of
`MISSING` rows is an inventory gate, not proof that all behaviour matches or that every defect is
known. Current defects and qualification gaps live in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md); consult
the subsystem guides and dated evidence for the scope of each claim.

## Verification of this guide

Reconciled on 2026-11-01 against the project parser, CLI/task forwarding, XML/XNB registration
bridge, nested-build implementation and recorded interoperability evidence. The existing multi-
renderer build verified `cna_xna_custom_pipeline_example` and `CnaContentPipelineTests`; 39 targeted
project/task/custom-compiler tests passed on the private runner. The Quest/parameter/writer/factory
and task fragments also passed a C++ syntax check with supplied example component bodies. No new
genuine-XNA runtime, Windows media or Xbox qualification run was performed for this edit.
