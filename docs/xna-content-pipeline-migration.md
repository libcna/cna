# Migrating an XNA 4.0 content project to CNA

This is the porting guide: what you already have, what it becomes, and what to type. The
neighbouring documents answer the other questions --
[`xna-content-pipeline-components.md`](xna-content-pipeline-components.md) is the generated
reference for every importer, processor and property;
[`xna-content-pipeline-compat-api.md`](xna-content-pipeline-compat-api.md) is the C# to C++
mapping rule by rule; [`content-pipeline.md`](content-pipeline.md) is the canonical engine
underneath; and [`xna-content-pipeline-parity-report.md`](xna-content-pipeline-parity-report.md) is
the parity matrix with a status for every public member.

Two things do not change and are worth saying first, because they decide how much of this guide you
need at all:

* **Your `.contentproj` is an input.** It is not converted, imported or migrated; it is built.
* **Your `.xnb` files are still `.xnb` files.** CNA writes the same container XNA wrote, and eleven
  asset families of CNA's output are verified by loading them in a genuine XNA 4.0 runtime
  (`tests/interop/xna40/README.md`). Nothing obliges you to rebuild content you already shipped.

---

## 1. Three ways in, in order of how little work they are

| You have | You run | Where it is documented |
|---|---|---|
| A `.contentproj` | `cna-content build MyGame.contentproj -o Content` | §2 |
| A directory of source assets | `cna-content build ContentSource -o Content` | §3 |
| Your own importers and processors in C# | a small C++ compiler binary that registers them | §7 |

All three drive the same coordinator, the same importers and the same writers. The difference is
only where the per-asset decisions come from.

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
Item paths are spelled the way MSBuild spells them, with a backslash, and resolve on any host.

A project build takes **XNA's leniency**, not the tool's own strictness: an unrecognised processor
parameter is dropped with a warning and the processor keeps its default, exactly as XNA does. §11
says what that means and how to get the strict behaviour back.

**What is refused, deliberately**:

* A project whose `<Reference>` or `<ProjectReference>` names a **pipeline assembly**. C++ has no
  assembly loading, so a custom importer cannot be discovered from a DLL path; compile it into your
  own compiler binary instead (§7). The refusal names the assembly rather than building without it.
* A project naming an importer or processor this build has not got. All of them are named **at
  once**, so a project needing three custom components says so in one run rather than three.

`CONTENT_PROJECT` and `FORMAT`/`XNB_*`/`XNA_COMPATIBLE` are alternatives in `cna_add_content`: a
project carries its own platform, profile and compression, and an option that would silently lose
to the file is refused rather than accepted.

---

## 3. Building a source directory instead

If the project file was only ever a list of files with default processors, you do not need it:

```bash
cna-content build ContentSource -o Content
```

Every extension is routed by the importer that declares it, exactly as XNA routes it, and the
importer's `DefaultProcessor` chooses the processor -- the table in
[`xna-content-pipeline-components.md`](xna-content-pipeline-components.md) §1 is that mapping. The
relative path becomes the content name, so `ContentSource/Textures/wall.png` is loaded as
`Textures/wall`.

Per-asset overrides that a directory cannot express live in an optional `.cna-content.json` beside
the sources -- logical name, importer, processor, writer and typed parameters -- documented in
[`content-pipeline.md`](content-pipeline.md), *Optional per-asset configuration*.

---

## 4. Which container to build: `.xnb` or `.cnb`

Both are produced by the same importers and processors; only the writer differs.

| | `.xnb` | `.cnb` |
|---|---|---|
| What it is | the XNA 4.0 container | CNA's own container |
| Choose it when | you want XNA-compatible output, a `.contentproj`, or content a genuine XNA runtime must load | you are building for CNA only |
| Selected by | `--format xnb` (a `.contentproj` implies it) | the default |
| Platform/profile/compression | `--xnb-platform`, `--xnb-profile`, `--xnb-compress` | not applicable: there is no platform identity in CNB |

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

Runtime loading is unchanged either way:

```cpp
auto wall = content.Load<Texture2D>("Textures/wall");
```

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

Every property, its type, its XNA default and its C++ accessor is in
[`xna-content-pipeline-components.md`](xna-content-pipeline-components.md) §3. The defaults are not
transcribed: they are the values Microsoft's own classes answered when the oracle read them, and a
ctest asserts a freshly constructed CNA processor against that measurement.

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

Members are listed in XNA's own order -- public properties first, then public fields, each in
declaration order, base class first. The `[ContentSerializer…]` attributes are fluent calls on the
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

XNA discovers these by scanning assemblies for attributes. CNA has no dynamic loading and no
attributes, so the two become one thing: **a small compiler executable of your own** that registers
its components and then runs the same coordinator `cna-content` runs. Nothing else about your
components changes -- they still derive from `ContentImporter<T>`, `ContentProcessor<TInput,
TOutput>` and `ContentTypeWriter<T>`.

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
    bindings.Add<std::int32_t>("Repeats", &QuestProcessor::getRepeatsProperty,
                               &QuestProcessor::setRepeatsProperty, "Repeats",
                               "How many times the quest may be taken.");
}
```

and a writer names the runtime reader in your own assembly, exactly as in C#:

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
        output.WriteObject<QuestStep>(value->opening);
    }
};
```

The `main` that ties them together is five statements:

```cpp
return Canon::RunContentCompiler(arguments, [](const Canon::ContentCompilerOptions& options)
{
    auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
    Canon::RegisterBuiltInContentPipeline(*registry, options);
    Canon::RegisterXnaImporter<QuestImporter>(*registry, "QuestImporter", attribute);
    Canon::RegisterXnaProcessor<QuestProcessor>(*registry, "QuestProcessor", {});

    auto compiler = std::make_shared<Compiler::ContentCompiler>();
    compiler->AddTypeWriter<QuestWriter>();
    Canon::RegisterXnaXnbOutput(*registry, compiler, options.xnbContainer);
    return registry;
});
```

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
is all of this as one compiling, tested file: a `.quest` extension, a sidecar dependency, a nested
build, a shared resource written through two references, declared parameters, and the same game's
types reached through the built-in `.xml` route. It is built and run by the acceptance test, so it
cannot rot.

**The runtime half is not optional.** A writer that names `QuestGame.QuestReader` needs a reader
registered under that name, or the `.xnb` will not load:

```cpp
ContentTypeReaderManager::AddTypeCreator("QuestGame.QuestReader",
                                         [] { return std::make_unique<QuestReader>(); });
```

---

## 8. Dependencies, nested builds and external references

These keep their XNA shape. Inside an importer or processor:

| XNA | CNA |
|---|---|
| `context.AddDependency(path)` | the same -- a source whose change rebuilds this asset |
| `context.AddOutputFile(path)` | the same -- a file deployed beside the compiled asset |
| `context.Convert<TIn,TOut>(value, "Processor")` | the same -- runs a processor in-process |
| `context.BuildAndLoadAsset<TIn,TOut>(ref, "Processor")` | the same -- builds another source and returns the object |
| `context.BuildAsset<TIn,TOut>(ref, "Processor")` | the same -- builds it as an additional output and returns an `ExternalReference<TOutput>` |
| `context.Logger.LogMessage("{0}", x)` | `context.getLoggerProperty().LogMessage("{0}", x)` |

A nested build is owned, fingerprinted and cleaned like any other artifact, and a cycle is refused
by the graph rather than recursing.

---

## 9. Driving the MSBuild task directly

If your build system already models XNA's task, the task itself exists:

```cpp
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

---

## 10. Verifying the port

Three checks, in increasing strength:

1. **It builds.** `cna-content` refuses rather than guesses, so a clean run already means every
   asset reached a registered importer, processor and writer.
2. **The bytes mean what they should.** `tools/xnb/xnb_conformance.py <file>.xnb` parses an `.xnb`
   from the format specification with no CNA code in the loop, and prints the reader table, the
   object graph, the texture format and the audio header.
3. **A genuine XNA 4.0 runtime loads it.** `tests/interop/xna40/` is a C# harness that loads
   fixtures through a real `ContentManager`; `tests/interop/xna40/README.md` says what it needs.
   This is what found three defects that every CNA-only test agreed with.

---

## 11. What behaves differently, and why

* **Strictness.** By default `cna-content` refuses a processor parameter naming a property that
  does not exist, a value it cannot convert, and a `.spritefont` character region the font has no
  glyphs for. XNA warns and carries on for all three. `--xna-compatible` reproduces XNA's
  behaviour; a `.contentproj` build selects it for itself. Everything else stays a refusal in both
  modes, because an impossible request is impossible whoever asked.
* **No assembly scanning.** `PipelineComponentScanner` enumerates a registry rather than loading
  DLLs, and a project referencing a pipeline assembly is refused (§2, §7).
* **No designer.** The `[DisplayName]`/`[Description]`/`[DefaultValue]` attributes survive as
  descriptor data that `PipelineComponentScanner` reports, but nothing draws a property grid.
* **XMA.** The Xbox 360's audio codec has no public specification sufficient to implement a
  conforming encoder, no licensable encoder and none in FFmpeg. CNA ships none, and asking for one
  refuses with `XMA ENCODER EXTERNALLY UNAVAILABLE`. `--xma-encoder` attaches your own; see
  [`xma-encoder-backend.md`](xma-encoder-backend.md).
* **WMA and WMV.** These import through this build's own media decoder rather than the Windows
  Media runtime; [`xna-content-pipeline-media.md`](xna-content-pipeline-media.md) records what was
  measurable and what was not.
* **FBX.** XNA carries FBX SDK 2011.3.1 and refuses every document of version 7400 or above, which
  is every FBX a current tool writes. CNA reads those too -- a divergence taken deliberately in the
  porter's favour, recorded in the parity map rather than left implicit.

Every other divergence is a row in
[`xna-content-pipeline-parity-report.md`](xna-content-pipeline-parity-report.md) with the note that
explains it. There is no `MISSING` row, and a ctest keeps it that way.
