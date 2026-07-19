# Audit: tools/fna-reference/FnaReference.csproj

## Metadata
- Source file: `tools/fna-reference/FnaReference.csproj` (77 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-fna-reference` shard
- File type: MSBuild project file (Task 471)
- XNA/FNA relevance: builds the `FnaReference` console app against a local `FNA.dll` build
- Main related tests: N/A

## Purpose
A deliberately old-style (`ToolsVersion="4.0"`, non-SDK) MSBuild project, matching FNA's own
`FNA.csproj` convention exactly, referencing a locally-built `FNA.dll` via an absolute `HintPath`.

## Executive Verdict
Correct, and each unusual choice is explicitly justified in the file's own top comment: the
old-style project format (this sandbox has `mono`/`xbuild` but no `dotnet` CLI/NuGet, so an
SDK-style project isn't viable), the absolute `HintPath` (simpler and less error-prone than
counting `..` levels back out of `tools/fna-reference/`, and matches this project's own documented
"Source Reference" convention of a fixed sandbox path for the FNA checkout), and `<Private>True</Private>`
(the README explains this copies `FNA.dll` locally, since mono's assembly resolver doesn't consult
the original `HintPath` at runtime — `Private=False` would fail with `FileNotFoundException`).

## Checklist Results
- `<LangVersion>4</LangVersion>` correctly pins to C# 4.0, matching FNA's own `FNA.csproj` exactly
  — consistent with `PackedVectorReference.cs`'s own comment explaining why it uses plain
  `float[]` arrays instead of value tuples (a later C# feature).
- The `<Compile Include>` list correctly enumerates all 4 real source files (`Program.cs`,
  `JsonWriter.cs`, `NonRenderingApiReference.cs`, `PackedVectorReference.cs`) plus
  `ViewportReference.cs` is present in the file list too — confirmed all 5 `.cs` files in this
  shard are included in the build.

## Detailed Findings
None.

## Cross-File Observations
None beyond what's already covered in the sibling files' own reports.

## Missing or Weak Tests
Not applicable — this is a build configuration file.

## Positive Findings
Every unusual/legacy choice in this file is explicitly and specifically justified in its own top
comment, rather than left as an unexplained oddity a future maintainer might "modernize" and
accidentally break.

## Final Assessment
No findings.
