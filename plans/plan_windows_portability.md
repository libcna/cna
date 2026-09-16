# CNA whole-framework Windows portability — the Unicode filesystem path model

This is the successor workstream to `plans/plan_win32_native_validation.md`, which is **complete
and closed**. That one asked whether CNA's Win32 *backend* works on real Windows and answered yes,
with 78 commits of evidence. This one asks a larger question that the first deliberately left
open: **is the rest of CNA correct on Windows**, starting with the defect it recorded and refused
to fix in passing — `WINNATIVE-F30`, non-ASCII filesystem paths.

The objective is not "replace 210 `.string()` calls". It is to give CNA a coherent, documented,
cross-platform filesystem path model, and to make the framework Unicode-correct on native Windows
without changing Linux behaviour and without quietly absorbing the graphics limitations that
belong to the virtual GPU.

---

## 0. Baseline

| | |
|---|---|
| Baseline branch | `win32-native-validation` (complete, not extended) |
| **Baseline SHA** | **`db7852c6eff5d49e37be0f2e90c167801e9bec16`** |
| This branch | `windows-portability`, branched from that SHA |
| `origin/next` at start | `4cf33c2b7968cb0d335e18faf2d9c0c32d730a0e` |
| sharp-runtime | `33da53f5ce971ee0cd6383c80987a7e4a2a0e9ff` (branch `next`) |
| Authorship | `Robert Vokac <robertvokac@robertvokac.com>`, author and committer, every commit |

### The Windows laboratory

| | |
|---|---|
| Guest | Windows 10 Home 22H2, build 10.0.19045, x64, VirtualBox |
| **Active ANSI code page** | **1252** (Western European) |
| OEM code page | 437 |
| System locale | `en-US` |
| `LongPathsEnabled` | **0** — legacy `MAX_PATH` policy is in force |
| Compiler | MSVC **19.44.35229** (VC tools 14.44.35207) |
| Windows SDK | **10.0.26100.0** |
| CMake | 3.31.6 |
| git (guest) | 2.47.1.windows.1 |
| Free space on `C:` at start | 50.1 GiB |

**Why ACP 1252 is the right machine for this.** It is not a Czech or Japanese machine, and that
makes it a *harder* test, not an easier one: `ž`, `ě`, `ř`, `ů` and every Cyrillic, CJK and
supplementary-plane character are outside it, so the conversion that silently succeeds on a
UTF-8 Linux host fails loudly here. It is also exactly the machine an English-locale user has
while storing content with Czech or Japanese names in it — the case F30 says is ordinary.

### Linux regression authority

Debian 13, kernel 6.12.107, GCC 14, glibc, ext4, UTF-8 locale. Same repository, same commit.

---

## 1. Inherited findings

Carried forward from `plan_win32_native_validation.md` and **not** re-litigated here.

| Id | What | Status entering this workstream |
|---|---|---|
| `F24` | D3D11 `DXGI_ERROR_UNSUPPORTED` after ~898 tests, never recovers; both controls (400 sequential cycles, 400 held) fail to reproduce it | **deferred**, environment-side, needs a physical Windows GPU. 1 480 of 1 557 failures. |
| `F29` | `Win32GraphicsServices.AContextEitherIsCreatedAndUsableOrFailsExplicitly` passes standalone, faults `0xC0000005` in the full run | **unresolved**, not claimed as validated |
| `F30` | non-ASCII filesystem paths broken on Windows | **the subject of this workstream** |
| `F31` | POSIX hardcoded in tests (`/bin/echo`, `/dev/null`), hiding an untested Windows `CreateProcess` path in `RunHostProcess` | open; in scope here (Phase 14/25) |
| `F26` | `CaseInsensitivePathTest` ×4 — a divergence, not a defect | recorded; Phase 31 says do not "fix" it by normalising case globally |
| `F22` | a Linux test ends the `CnaTests` process | pre-existing, out of scope |

### The numbers this workstream must move

```
Windows whole suite, baseline    8136 tests · 1557 failures · 0 errors · 195 skipped
  of which F24 D3D11 cascade                  1480
  of which also fail on Linux                   13
  remaining genuine Windows-only                44     <- the target
Linux, same commit               8939 tests ·   25 failures · 0 errors · 479 skipped
```

The five Windows-only failures F30 was known to explain:

```
AudioTagParserTest.ReadsNonAsciiVorbisCommentTitleCorrectly
CnjContentPipelineTest.ResolvesUtf8AuthoredSidecarsThroughNativePaths
MediaLibraryTestFixture.InternationalResolvesTheNonAsciiEntry
PlaylistParserTest.ParsesInternationalM3U8WithNonAsciiEntry
Texture2DContentPipelineTest.ReadsANativeNonAsciiFilesystemPathWithoutNarrowing
```

How many of the 44 F30 actually explains is a measurement, not an assumption, and Phase 24 makes
it.

---

## 2. Tasks

| Id | Task | Status |
|---|---|---|
| `WINPORT-0001` | Branch from the validated baseline; record every version in §0 | **done** |
| `WINPORT-0002` | Establish this plan as the living evidence ledger | **done** |
| `WINPORT-0003` | Reproduce F30 deterministically *before* changing anything | **done** — see F30-A |
| `WINPORT-0004` | Repository-wide path-use audit, classified A–I | **done** — §4 |
| `WINPORT-0005` | Document CNA's path encoding contract (Phase 3, ten questions) | **done** — `docs/filesystem-path-model.md` |
| `WINPORT-0006` | Central conversion layer, with its own unit tests | **done** — `CNA/Internal/PathUtf8.hpp`, 22 tests |
| `WINPORT-0007` | Migrate category-A (OS/filesystem) uses to stay native | **done** — 9 modules |
| `WINPORT-0008` | Migrate category-C (log/display) uses to explicit UTF-8 | **done**, with 0007 |
| `WINPORT-0009` | Serialization audit — do not change on-disk formats to fix runtime paths | **done** — no format changed |
| `WINPORT-0010` | Third-party boundary contracts, recorded per library | **done** — §5 |
| `WINPORT-0011` | Unicode content-load regression suite | |
| `WINPORT-0012` | Build CNA from a Unicode source path with MSVC | |
| `WINPORT-0013` | Run a real CNA application from a Unicode path | |
| `WINPORT-0014` | SDL-free Win32 build from a Unicode path, re-proved by PE imports | |
| `WINPORT-0015` | Whole Windows suite rerun; before/after partition | |
| `WINPORT-0016` | Triage every remaining Windows-only failure by root cause | |
| `WINPORT-0017` | Linux regression after every batch | |
| `WINPORT-0018` | Automate the Unicode phase in the native Windows harness | |

---

## 3. Findings

### WINPORT-F30-A — F30 reproduced, and half the inherited prescription is wrong

`spikes/windows-unicode-path-spike/` — eleven path classes, a real file per class with a known
ASCII payload, fifteen questions per class, every PASS a byte-for-byte read of the payload rather
than a string the probe printed.

```
Linux   (GCC 14, glibc, UTF-8)              165 pass    0 fail
Windows (MSVC 19.44, ACP 1252, SDK 26100)   158 pass   51 fail
```

**F30 is confirmed, and its shape is now measured rather than inferred.**

What never fails anywhere: `create_directories(path)`, `ofstream(path)`, `ifstream(path)`,
`exists(path)`, `file_size(path)`, `directory_iterator`, `path::u8string()`, the
`path → u8string() → path(std::u8string)` round trip, and the wide Win32/CRT surface
(`GetFileAttributesW`, `CreateFileW`, `_wfopen`).

What fails on Windows, by input class:

| Operation | Fails for |
|---|---|
| `path::string()` | the 7 classes not representable in CP1252 |
| `path::generic_string()` | **the same 7** |
| `std::ifstream(const char*)` / `fopen` / `GetFileAttributesA`, fed from `string()` | the same 7 |
| **`fopen` given UTF-8 bytes** | **9 classes — the 7, plus `café` and precomposed `étude`** |

Two corrections to what was written down before this was measured:

**`generic_string()` does not help.** It narrows through the ANSI code page exactly as `string()`
does and fails on an identical input set. F25's `generic_*` change was right about separators and
has nothing to do with encoding.

**Narrowing to UTF-8 and calling a narrow C API is, for some inputs, *worse* than today.** `café`
is representable in CP1252, so `fopen(p.string())` opens it right now. `fopen(p.u8string())` does
not — the CRT reads UTF-8's `0xC3 0xA9` as `Ã©`. So the mechanical `.string()` → `.u8string()`
migration that the F30 note could be read as recommending would *break* a class of paths that
currently works, while still failing the seven that already fail.

The model this workstream builds is therefore:

> **UTF-8 is CNA's text encoding for paths. It is not a way to call a narrow API.**
> Anything that opens, stats, creates, enumerates or deletes holds a `std::filesystem::path` all
> the way to the call. A narrow `std::string` path is display, log, manifest or identity text, and
> it is converted back into a `path` before it touches the filesystem again.

### WINPORT-M1 — the Linux regression authority for this branch

Measured on this branch with the conversion layer in place but **before** any module migration,
from the repository root under `Xvfb :99`, with the two suites the preceding workstream excluded
for cause (`Sdl3XErrorHandlerTest`, which ends the process — F22; `XnaDifferentialBuildTest`, whose
Wine prefix wedges):

```
8961 tests · 26 failures · 0 errors
```

The 22 extra tests relative to the inherited `8939` are this branch's own `PathUtf8Tests`. The
failure **set** is what the final run is compared against, not the count — two runs can agree on 26
and still have swapped one failure for another. The set is recorded in
`reports/windows-portability/linux_baseline_failures.txt` (gitignored) and is, in full:

`CnbTextureContentManagerTest` ×2, `CnbTextureCubeProducerTest`, `CnjCapabilityMatrixTest`,
`CnjEffectTest`, `CnjStockEffectTest`, `CnjTexture3DTest`, `ContentManagerSkinnedModelTest` ×3,
`GltfRendererIndexWidthPolicy`, `GraphicsDeviceCapabilityTest`, `MediaLibraryTestFixture` ×2,
`TerminalRestoration.SighupGivesTheTerminalBack`, `XnaAudioContent`, `XnaAudioProcessors`,
`XnaBuildDeterminism` ×3, `XnaContentProjectCommandLine`, `XnaSourceToOutput` ×3,
`XnbContainerFuzzTest`, `XnbContentPipelineTest`.

One of these — `TerminalRestoration.SighupGivesTheTerminalBack` — is not in the preceding
workstream's list of 25 and is a SIGHUP/terminal test with an obvious environmental dependence. It
is carried as baseline here and re-checked at the end rather than attributed to anything.

### WINPORT-F1 — the Windows suite was being run from the wrong directory *(harness, fixed)*

The first whole-suite rerun after the migration reported **613 failures, 588 of them apparently
Windows-only** — against an inherited baseline of 44. That number was wrong, and it was wrong for a
reason that has now caught three workstreams in a row.

`Invoke-GTest` ran `CnaTests.exe` with its working directory set to the **build tree**. A large part
of the suite names its fixtures relative to the repository root (`tests/assets/media/music/Artist
One/Album Alpha/01 - Sunrise.ogg`), and the build tree holds only the *generated* assets, not the
checked-in ones. So hundreds of tests failed for want of a file, and the resulting number was not
comparable with the Linux baseline, which has always been measured from the repository root.

Measured rather than argued, on the same binary in the same minute:

| Working directory | `SongTest` + `AudioTagParserTest` |
|---|---|
| `C:\cna\build\full-win32-d3d11-nosdl` | wholesale failures |
| `C:\src\cna` | **49 passed, 1 failed** |

This is the third finding in two workstreams where the **harness** produced the red rather than the
library — after the PowerShell argv/stdout mangling (`WINNATIVE-F7`) and the session-global cursor
state. The rule Phase 38 states for Unicode applies just as well to a working directory: be
suspicious of a large new failure count before being suspicious of the code.

`Invoke-GTest` now takes the working directory explicitly, and the `cnatests` step passes the
source root.

### Scale, measured on this branch

`.string()` on a `path`-shaped expression, production code only (`modules/**`, excluding
`tests/`): **210 sites** — content-pipeline 68, renderers 43, media 28, content 28, platform 22,
core 11, storage 5, runtime 3, gamer-services 2. Against **2** existing uses of `u8string`, both
inside the one helper that already does this correctly
(`modules/core/include/CNA/Internal/ContentPath.hpp`). A further 1 214 sites live in tests and
40 in `tools/` and `tests/`.

The inherited note said 165; the difference is that it did not count `modules/renderers/`. The
audit does not stop at this number — the classification in §4 is what decides each site.

---

## 4. The audit — WINPORT-0004

Three passes over the production tree (`modules/**`, excluding `tests/`), classified A–I as Phase 2
requires rather than counted. The point of classifying is that the correct fix differs per category
and two of them must **not** be converted at all.

| Category | What it is | Sites | What was done |
|---|---|---|---|
| A | OS/filesystem operation | ~230 | carry `std::filesystem::path` to the call |
| B | UTF-8 public API boundary | ~140 | declare the encoding; convert at the edge |
| C | log/error/display text | ~40 | `PathToUtf8()` |
| D | serialization / on-disk format | ~25 | `PathToGenericUtf8()` — **no format changed** |
| E | comparison / map key / identity | ~50 | `PathToGenericUtf8()`, both sides together |
| F | subprocess argv | ~12 | `PathToUtf8()` — `RunHostProcess` already widens `CP_UTF8` |
| G | third-party boundary | ~20 | per-library contract, §5 |
| H | test-only | 1 214 | out of scope for this migration |
| I | provably ASCII / internally generated | ~50 | **left unchanged** |

### What the numbers do not say

The inherited note put the scope at "165 `.string()` sites". The real shape is different in three
ways, and each changed the work:

1. **It was an undercount of the search and an overcount of the problem.** 210 production
   `.string()` sites exist, not 165 — the difference is `modules/renderers/`, which the earlier
   count did not include. But ~100 of the content pipeline's conversions were *already correct*
   (`ContentPathToUtf8`/`ContentPathFromUtf8`), and ~50 more are category I and must not be touched
   at all: `/dev/input/eventN`, `/proc/self/comm`, `"."`, a hex hash, an extension literal.
2. **`.string()` was not the whole search.** The narrow `std::filesystem::path(std::string)`
   constructor and the narrow `std::ifstream`/`exists`/`create_directories` overloads are the same
   defect facing the other way, are invisible at a grep for `.string()`, and are **more** numerous.
   `StandardFileSystem::TryLoadFile` — the platform's primary asset read — contained no `.string()`
   at all.
3. **`generic_string()` had to be migrated too.** It narrows through the ANSI code page exactly as
   `string()` does (measured: it fails on an identical input set), so treating it as the safe
   spelling would have left the defect in every serialization path.

### The eight root causes, and where they went

| | Root cause | Resolution |
|---|---|---|
| RC-1 | `PathContainment`'s narrow family did up to four ANSI round trips per call | native core `ResolveContainedNativePathFromBase`; the narrow helpers became thin UTF-8 wrappers |
| RC-2 | `StandardFileSystem` is what Win32 delegates to, and was entirely ANSI | the interface now declares UTF-8; `GetBasePath()` was where a game lost its whole content root |
| RC-3 | the case-insensitive walker existed **three** times, each narrowing every entry it enumerated | one implementation; the other two delegate |
| RC-4 | `getenv("LOCALAPPDATA")` reads the ANSI environment | `GetEnvironmentVariableW` |
| RC-5 | a path narrowed and re-parsed to make a trivial edit | concat on the path (`+= ".tmp"`) |
| RC-6 | `media` carried narrow strings end to end | converted; uncovered the `songByPath_` key mismatch |
| RC-7 | `Win32SystemServices` undid its own correct conversions | native throughout; the triple conversion at `SetFileName` is gone |
| RC-8 | third-party sinks reached with an undifferentiated narrow string | §5 |

### Two defects found by the audit rather than inherited

**The `songByPath_` key mismatch.** `MediaLibraryIndex` produced the key with native separators
while `PlaylistParser`'s members arrive through `ResolveContainedPath`, which always answers
generic form. On Windows every playlist member lookup missed and playlists came out **empty**.
That is exactly the case `PathContainment.hpp`'s own comment claims to have fixed, and the claim
only held once the producer agreed. Byte-identical on POSIX, which is why no test saw it.

**`XnaModelSourceContentPipeline.cpp`** passed `ContentPathToUtf8(path.lexically_normal().generic_string())`
— narrowing to ANSI and then letting the helper's implicit conversion widen it back.

---

## 5. Third-party boundary contracts — WINPORT-0010

Each verified against the library's own source rather than assumed. The full table is in
`docs/filesystem-path-model.md`; what this workstream changed:

| Library | Contract on Windows | Evidence | Action |
|---|---|---|---|
| stb_image / stb_image_write | **ANSI** — the `_wfopen` branch needs `STBI_WINDOWS_UTF8`, which CNA does not define | `third_party/stb/stb_image.h:1356-1382` | stopped passing a filename: read/write through a native path, decode/encode in memory |
| cgltf | **ANSI** — `cgltf_default_file_read` is a bare `fopen`, no wide branch | `third_party/cgltf/cgltf.h:1045` | CNA-owned `cgltf_options::file` callbacks |
| FreeType | **build-dependent** — the header documents `fopen` semantics, the CMake build ships wide ones | `freetype/freetype.h:2563-2568` | `FT_New_Memory_Face`, which is what FreeType's own docs recommend for this |
| SDL3 / SDL3_mixer | UTF-8 | `third_party/SDL/src/io/SDL_iostream.c:129` | pass `PathToUtf8()` |
| FFmpeg | UTF-8 (`ff_win32_open` widens with `utf8towchar`) | upstream; not vendored | pass `PathToUtf8()` at the call |
| zlib, Draco, dr_flac, dr_mp3, stb_vorbis, shaderc | no path crosses | memory APIs / diagnostic labels only | **nothing — deliberately** |

The last row matters as much as the others: five libraries were checked and found already correct,
and "fixing" shaderc's `filename` argument — a diagnostic label it never opens — would have been a
change with no defect behind it.

---

## 6. sharp-runtime — WINPORT-0011

One focused commit, `ef75cd18a47351e89e6c76d610c049ccb2e89c6e`, because CNA cannot deliver this
without it: `System::IO::FileStream` is the sink `TitleContainer::OpenStream` (XNA asset loading)
and `StorageContainer::CreateFile`/`OpenFile` (save games) both reach, and it opened through the
narrow `std::fstream` overload.

`FileStream` and `File` now convert once, at the edge, through an implementation-private
`Utf8Path.hpp` (under `src/`, not `include/` — it is not part of the `System.IO` surface).
`File::Exists` uses the non-throwing form, because a predicate must answer rather than propagate a
`filesystem_error`.

Six new tests. **IO suite: 1022/1022 pass.** The five `Xml.Linq::XLinqNamespaceTests` failures in
the full sharp-runtime run are **pre-existing and unrelated** — verified by stashing the change and
reproducing them.

---

## 7. Linux regression — WINPORT-0017

The rule the preceding workstream set, and this one keeps: compare the failure **set**, not the
count. Two runs can agree on a number and still have swapped one failure for a different one.

| | Tests | Failures |
|---|---|---|
| Baseline, conversion layer only (WINPORT-M1) | 8 961 | 26 |
| After the whole migration, commit `187ea8b6` | 8 977 | 27 |

The 16 extra tests are `UnicodePathResolutionTests`. The set difference is **one test newly
failing and none newly passing**:

```
+ DynamicSoundEffectInstanceTest.StressSubmitFloatBufferEXTAgainstRepeatedPlayCyclesNeverCorruptsLiveStream
```

**It is not a regression, and the test says so itself.** Its assertion is
`EXPECT_GT(callsThrown.load(), 0)` with the message *"expected at least some submissions to
genuinely race a live Play()'d stream in the other format and be rejected — zero here would mean
this test never actually exercised the guard it exists to verify"*. Zero is the test declaring its
own run inconclusive, not the product misbehaving. It happened because the native Windows build was
saturating the host's CPUs at the time, so the threads never interleaved.

Checked rather than assumed: 3/3 in isolation, and 57/57 twice for the whole
`DynamicSoundEffectInstanceTest` suite. The API it exercises, `SubmitFloatBufferEXT`, takes a
memory buffer and touches no path at all; the only audio changes on this branch are three
`ifstream` widenings in file-loading code this test never reaches.

Everything else is identical: all 26 baseline failures still fail, for the same reasons, and
nothing that passed before now fails.

## 8. Windows — WINPORT-0012

**The whole framework builds on native Windows with MSVC after the migration**, at commit
`187ea8b6` against sharp-runtime `ef75cd18`, in the SDL-free `WIN32` + `DIRECTX11` configuration —
two distinct warning codes (`C4005`, `C4834`), no errors. That is the first thing the migration had
to not break, since the preceding workstream's F9–F16 showed how much of CNA had never been
compiled by this toolchain at all.

## 9. The Windows measurement — WINPORT-0015

Whole suite, native Windows, MSVC `RelWithDebInfo`, `WIN32` + `DIRECTX11`, SDL off, **run from the
repository root** (see WINPORT-F1 for why that sentence is load-bearing):

```
                              baseline        after
tests                             8136         8174
failures                          1537         1585
  F24 D3D11 cascade               1480         1536      deferred, environment-side
  also failing on Linux             13           12      not Windows-portability defects
  genuinely Windows-only            44           37
```

The failure total went **up** while the thing being measured went **down**, and both facts are real:
F24's cascade is nondeterministic in how far it spreads, and it swamps the number it shares. That is
the whole reason the partition exists rather than a headline count.

### F30 is fixed, measured rather than assumed

All five Windows-only failures the preceding workstream attributed to F30 now **pass on native
Windows**:

```
AudioTagParserTest.ReadsNonAsciiVorbisCommentTitleCorrectly                      pass
CnjContentPipelineTest.ResolvesUtf8AuthoredSidecarsThroughNativePaths            pass
MediaLibraryTestFixture.InternationalResolvesTheNonAsciiEntry                    pass
PlaylistParserTest.ParsesInternationalM3U8WithNonAsciiEntry                      pass
Texture2DContentPipelineTest.ReadsANativeNonAsciiFilesystemPathWithoutNarrowing  pass
```

So F30 explains **5 of the 44**, not the larger share it might have been given credit for. The rest
of the 44 were always something else, and §10 says what.

The new coverage passes on Windows too: `PathUtf8Test` 10/10, `IsWellFormedUtf8Test` 10/10,
`ContentPathCompatibilityTest` 2/2, `PathContainmentTest` 30/30.

## 10. Triage of the 37 — WINPORT-0016

One row per **root cause**, not per manifestation.

| Root cause | Tests | Status |
|---|---|---|
| **F31 — POSIX hardcoded in tests.** `HostProcessTest` asks for `/bin/echo`; `CnbGltfDirectToolTest` and `LargeModelScalingTest` redirect to `/dev/null`; `XmaEncoderService`'s stub encoder is not a PE, so `CreateProcess` refuses it with `ERROR_BAD_EXE_FORMAT`. | 18 | inherited, **not** a product defect — and the finding to carry is still that `RunHostProcess` has a Windows `CreateProcess` path with no test behind it |
| **F26 — case resolution on a case-insensitive filesystem.** | 1 | inherited divergence, deliberately not "fixed" |
| Separator assertions in `CaseInsensitivePathTest` that predate the contract | 3 | **fixed** — they asserted `path::string()` where the function documents generic UTF-8 |
| A defect in this branch's own new test (a narrow `operator/` on UTF-8 bytes) | 1 | **fixed** |
| `XnaDifferentialBuildTest` | 1 | **not comparable** — it is excluded from the Linux run (its Wine prefix wedges), so it has no baseline to be "Windows-only" against |
| Windows text mode: `XnaContentImporter` reads `"typed\r"` | 1 | the `WINNATIVE-F27` class, not a path defect |
| UNC: `SongTest.FromUriTreatsARemoteAuthorityAsUncRatherThanSilentlyDroppingIt` | 1 | Phase 21; genuinely Windows-only, analysed below |
| Not yet analysed: `SpriteFontFamilyResolutionTest` ×2, `XnaPipelineBridge` ×2, `ContentPipelineCoreTest`, `XnaBuildContent`, `XnaErrorParityTest`, `XnaContentProjectCommandLine`, `XnbWriterInputFuzzTest`, `CnaInputClipboardTest`, `StandardFileSystemTests` | 11 | open |

**18 of the 37 are F31**, which is a test-portability problem the preceding workstream already
recorded and declined to paper over. Fixing it properly needs a program that echoes its `argv`
without a shell re-parsing it — routing through `cmd.exe` would test cmd's quoting rather than
`RunHostProcess`'s — and that is its own piece of work, not a side effect of this one.
