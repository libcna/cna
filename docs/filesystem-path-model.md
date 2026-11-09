# CNA's filesystem path model

What a path is inside CNA, what a `std::string` holding a path means, and where the conversion
between them happens. This exists because the answer used to be different in different modules, and
on Windows that is not a style question — it decides whether a file opens.

The original Windows encoding measurements are in
[windows-unicode-path-spike](../spikes/windows-unicode-path-spike/README.md) (MSVC 19.44, ACP1252).
WINPORT-* applied that model; later title/content/storage contracts have their own source and
regression tests. The 2026-11-01 audit checks current helpers/call boundaries without rerunning
every historical Windows measurement.

---

## The three rules

**1. `std::filesystem::path` is the representation for anything that touches the filesystem, and it
stays native all the way to the call.** On Windows a `path` holds UTF-16; on POSIX it holds the raw
bytes the kernel stores. Nothing is narrowed in order to open, stat, create, enumerate, rename or
delete.

```cpp
std::ifstream in(path, std::ios::binary);            // yes
std::ifstream in(path.string(), std::ios::binary);   // no -- can narrow through a code page
```

**2. A narrow `std::string` holding a path means UTF-8.** That is the encoding of a path in a public
API, a log line, an exception message, a content manifest, a serialized external reference, and a
map or cache key. It is converted back into a `std::filesystem::path` before it touches the
filesystem again.

**3. `path::string()` and `path::generic_string()` are never how CNA obtains path text.** On Windows
both convert through the process ANSI code page and throw when a character has no mapping there. Use
`CNA::Internal::PathToUtf8()` or `PathToGenericUtf8()` from
`modules/core/include/CNA/Internal/PathUtf8.hpp`.

---

## The conversion layer

| Function | Produces | Use it for |
|---|---|---|
| `PathToUtf8(path)` | UTF-8, separators as the path holds them | logs, exception text, names shown to a player |
| `PathToGenericUtf8(path)` | UTF-8, `/` separators | identities, map and cache keys, manifests, anything another platform reads |
| `PathFromUtf8(text)` | native `std::filesystem::path` | every return trip to the filesystem |
| `TryPathFromUtf8(text)` | native path or empty optional | conversion-failure-aware counterpart of PathFromUtf8; not an existence check |
| `IsWellFormedUtf8(text)` | `bool` | Unicode validity, separately from filename/path policy |

`ContentPathToUtf8()` / `ContentPathFromUtf8()` in `CNA/Internal/ContentPath.hpp` are the content
pipeline's existing spellings of `PathToGenericUtf8()` / `PathFromUtf8()` and forward to them
unchanged. Use the helpers rather than deriving an encoding policy from a call-site count.

### Why not just `u8string()` everywhere

Because the failure being fixed is not "the wrong function is spelled at 200 sites", and a
mechanical substitution makes some inputs worse. Measured on a CP1252 machine:

- `fopen(p.string())` **opens** `café`. CP1252 has `é`, so narrowing succeeds.
- `fopen(p.u8string())` **does not**. UTF-8 spells `é` as `C3 A9`, and the CRT reads those two bytes
  as `Ã©`.

Narrowing to UTF-8 is how CNA obtains *text*. It is not a way to call a narrow API. Anything that
opens a file either takes a `std::filesystem::path` or gets one built from UTF-8 first.

`generic_string()` is not a fix either: it narrows through the ANSI code page exactly as `string()`
does, and fails on an identical set of inputs. Separator style and text encoding are unrelated.

---

## Text, validation and logical identifiers

Public narrow path text uses UTF-8, retaining fixed XNA-style string signatures such as
ContentManager.RootDirectory and the audio/content APIs. ASCII names already satisfy it.
This supports well-formed Unicode names; it is not a claim that every arbitrary native UTF-16
sequence or POSIX byte name is portable Unicode.

PathFromUtf8 constructs a path from char8_t code units, and the output helpers use u8string/
generic_u8string. Exact conversion internals and failure categories depend on the standard library.
The recorded MSVC test uses native UTF-16 and UTF-8 conversions without an ANSI round trip.

On POSIX, conversion deliberately preserves native filename bytes even when they are not valid
UTF-8. On Windows, malformed UTF-8 can fail conversion (filesystem_error in the measured MSVC
configuration). Call IsWellFormedUtf8 first when an API requires the same Unicode validity decision
on both hosts. That function accepts U+0000 as valid Unicode; it does not validate a legal filename,
reject rooted paths, check existence or grant access. TryPathFromUtf8 catches std::exception from
conversion and returns an empty optional; it adds none of those filename/authorization checks.

Logs and serialized references use the text helpers. Content manifests, external references and
cache identities use generic-form UTF-8 with '/' separators. This is a portable representation for
suitable relative names, not proof that arbitrary absolute roots, drive names, case collisions,
reserved names or files themselves transfer between systems.

Conversion does not resolve dot components, canonicalize, change case or normalize Unicode.
Generic form uses the host path parser's separators: Windows recognizes backslash as a separator;
POSIX can preserve it as a literal character. An API that accepts Windows-style asset spelling
therefore normalizes it explicitly, separately from UTF-8 conversion.

Validate authored logical text before any conversion that rewrites it. A manifest identifier such
as @alias/path has one authored separator, '/', on every host. Checking for forbidden backslash
only after generic conversion would miss it on Windows. The external-dependency correction
WINCLOSE-0006 records that boundary; it is distinct from TitleContainer's intentional acceptance
of Windows-style separators.

---

## Third-party boundaries

Each library has its own contract, verified against its source rather than assumed.

| Library | What it expects on Windows | How CNA meets it |
|---|---|---|
| SDL3, SDL3_mixer | UTF-8 (`WIN_UTF8ToStringW` → `CreateFileW`) | pass `PathToUtf8()` |
| FFmpeg (libavformat) | Its UTF-8 filename API | pass UTF-8 text; CNA's current Windows build has no enabled FFmpeg decoder integration, so this is not Windows playback qualification |
| stb_image, stb_image_write | **ANSI** — the `_wfopen` branch needs `STBI_WINDOWS_UTF8`, which CNA does not define | do not pass a filename; use the memory entry points |
| cgltf | **ANSI** — `cgltf_default_file_read` is a bare `fopen` | install CNA-owned `cgltf_file_options` callbacks that open a native `path` |
| FreeType | Filename behavior depends on its build | read a native file into memory and use FT_New_Memory_Face |
| zlib, Draco, dr_flac, dr_mp3, stb_vorbis | no path crosses — memory APIs only | nothing to do; keep it that way |
| shaderc | the `filename` argument is a diagnostic label, never opened | nothing to do |
| C runtime environment (`std::getenv`) | **ANSI** — the narrow environment is in the process code page | read a path-valued variable through `System::Environment::GetEnvironmentVariable`, which reads the wide environment and answers UTF-8 |
| googletest (test binaries only) | **ANSI** — stream capture and death tests create files with `GetTempFileNameA` in `GetTempPathA` | nothing CNA can change: a `TEMP` with a character outside the ANSI code page aborts the test binary itself (KNOWN_ISSUES.md (Limitations)) |

---

## Windows path semantics that are not POSIX semantics

Measured on MSVC 19.44; these are `std::filesystem`'s answers, and CNA uses them deliberately rather
than making Windows imitate POSIX.

| Input | `is_absolute()` | `root_name()` | `root_directory()` |
|---|---|---|---|
| `/usr/share/cna` | **false** | `` | `\` |
| `C:\CNA test\a.png` | true | `C:` | `\` |
| `C:a.png` (drive-relative) | **false** | `C:` | `` |
| `\\server\share\a.png` (UNC) | true | `\\server` | `\` |
| `\rooted-no-drive` | **false** | `` | `\` |

The first and third rows are the traps. A POSIX-absolute path is *not* absolute on Windows, and a
drive-relative path has a root name without being absolute — so `is_absolute()` alone is not a test
for "rooted", and code that needs "rooted" must ask `has_root_directory()` or inspect both.

Containment is a path-component question. PathContainment.hpp normalizes both sides, optionally
weakly canonicalizes existing components (default on), and checks lexically_relative. It rejects
an empty result, the root itself ('.'), or a first component exactly '..'. A directory called
..config is not that component. Canonicalization errors refuse the request. The returned path
retains the lexical spelling rather than the canonical spelling.

Relative-input helpers normalize backslashes and reject slash-rooted/drive-prefixed input before
joining. File-relative helpers distinguish the join directory from the authorization root, allowing
legitimate in-root sibling references. Native root parsing still follows each platform's rules;
this does not make all Windows/POSIX paths equivalent. The check and later open are separate
operations, not an atomic filesystem sandbox against concurrent symlink/directory replacement.

Case sensitivity is not part of this model. Windows is usually case-insensitive and case-preserving,
POSIX usually is not, and the UTF-8 conversion layer does not normalize case. See
`CNA/Internal/CaseInsensitivePath.hpp` for the one place case is deliberately resolved -- the XNA
asset paths of `TitleContainer`, `ContentManager` and the XACT banks, which accept Windows separators
and fall back to a unique case-insensitive match so titles open on case-sensitive hosts. It
promises a spelling that opens the file, not the casing stored on disk, so on a case-insensitive
filesystem the requested spelling comes back unchanged (graphics shared cleanup GSC-0007, formerly
WINNATIVE-F26).

Long paths are separate from encoding. The recorded MSVC/Windows configuration with
LongPathsEnabled=0 hit legacy MAX_PATH behavior; this is not a claim about every machine's registry,
application manifest or standard library. Unicode correctness alone does not qualify long paths.

### TitleContainer's XNA path contract

`TitleContainer::OpenStream` accepts title-relative names, not arbitrary absolute
filesystem paths. It first rejects empty input with `ArgumentNullException`, then
applies XNA's dot/parent-component cleaning before validating rooted paths,
remaining escaping parents and the reserved characters `:*?"<>|`. Invalid cleaned
names produce `ArgumentException`. Keep this ordering: a removed component may
contain a reserved character, and XNA's empty-component rules are not equivalent
to a blanket `lexically_normal()`.

The relative URI check rejects lengths of 65520 UTF-16 code units or more,
not UTF-8 bytes. This is a title compatibility boundary, not a general filesystem
or modern Sharp Runtime URI limit. Ordinary OS path limits can fail earlier.

Missing files/parents produce `FileNotFoundException`. Other open failures, such
as an existing directory, an embedded NUL or an overlong native path, produce
`InvalidOperationException` retaining the underlying cause. Android packaged-asset
fallback is attempted only for genuinely missing native paths. Case-insensitive
asset lookup remains supported.

Migration for CNA-DEMOS-ISSUE-145: code that previously passed an absolute path
must supply a title-relative name (or use the general `System::IO::FileStream`
API when it intentionally needs filesystem access). No API signature/layout is
changed. The C title-read route reports invalid names as `INVALID_ARGUMENT`, while
missing/unreadable files retain its documented `IO` category and do not overwrite
the destination buffer. The native absolute-path acceptance test was corrected
against genuine XNA, not treated as an intentional extension.

This OpenStream contract does not redefine every CNAEXT helper on TitleContainer. In particular,
ReadToPointer uses a separate native-file path and must not be treated as the same validated
OpenStream entry point.

### Title location versus save storage

TitleLocation initializes from the selected platform filesystem's base path, falling back to
the current directory only when no base is supplied; an explicit title path updates the internal
media/content bridge. Title-relative access is not the save-root policy.

StorageDevice resolves a separate per-game root, with browser persistence readiness, Android's
private storage service and native per-user environment fallbacks. Selecting SDL3/HEADLESS/TERMINAL
does not by itself relocate native saves to the title directory. See
[C API storage contract](c-api/STORAGE.md) for the shared native storage/lifetime boundary.

---

## Proving a path is right

A terminal that cannot render a name is not evidence that the name is wrong. Two findings in the
preceding workstream were false alarms caused by the harness rather than the library, so path
correctness is asserted by:

- reading a known payload back out of the file through the reconstructed path;
- comparing bytes or hashes, never rendered text;
- asking the wide Win32 API (`GetFileAttributesW`, `CreateFileW`) directly;
- redirecting a test's output to a file and reading the raw bytes, rather than reading it through a
  console or a PowerShell pipeline that re-encodes.

## Audit verification

On 2026-11-01, focused Core/Runtime targets build in the existing multi-renderer tree. All 71
UTF-8/validation/containment/Unicode/case-resolution cases and all 18 TitleContainer/TitleLocation
cases pass through the private runner. This is current Linux evidence. The CP1252/MSVC, native
Windows path-root/long-path and Android packaged-asset observations are historical or source
boundaries, not newly executed platform qualification.
