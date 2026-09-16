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
| `WINPORT-0004` | Repository-wide path-use audit, classified A–I | in progress |
| `WINPORT-0005` | Document CNA's path encoding contract (Phase 3, ten questions) | |
| `WINPORT-0006` | Central conversion layer, with its own unit tests | |
| `WINPORT-0007` | Migrate category-A (OS/filesystem) uses to stay native | |
| `WINPORT-0008` | Migrate category-C (log/display) uses to explicit UTF-8 | |
| `WINPORT-0009` | Serialization audit — do not change on-disk formats to fix runtime paths | |
| `WINPORT-0010` | Third-party boundary contracts, recorded per library | |
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

### Scale, measured on this branch

`.string()` on a `path`-shaped expression, production code only (`modules/**`, excluding
`tests/`): **210 sites** — content-pipeline 68, renderers 43, media 28, content 28, platform 22,
core 11, storage 5, runtime 3, gamer-services 2. Against **2** existing uses of `u8string`, both
inside the one helper that already does this correctly
(`modules/core/include/CNA/Internal/ContentPath.hpp`). A further 1 214 sites live in tests and
40 in `tools/` and `tests/`.

The inherited note said 165; the difference is that it did not count `modules/renderers/`. The
audit does not stop at this number — the classification in §4 is what decides each site.
