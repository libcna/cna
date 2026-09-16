# CNA's filesystem path model

What a path is inside CNA, what a `std::string` holding a path means, and where the conversion
between them happens. This exists because the answer used to be different in different modules, and
on Windows that is not a style question — it decides whether a file opens.

The measurements behind every claim here are in `spikes/windows-unicode-path-spike/`, and the
migration that applied them is `plans/plan_windows_portability.md`.

---

## The three rules

**1. `std::filesystem::path` is the representation for anything that touches the filesystem, and it
stays native all the way to the call.** On Windows a `path` holds UTF-16; on POSIX it holds the raw
bytes the kernel stores. Nothing is narrowed in order to open, stat, create, enumerate, rename or
delete.

```cpp
std::ifstream in(path, std::ios::binary);            // yes
std::ifstream in(path.string(), std::ios::binary);   // no  -- throws on Windows
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
| `IsWellFormedUtf8(text)` | `bool` | validating text that arrived from a game or a file |

`ContentPathToUtf8()` / `ContentPathFromUtf8()` in `CNA/Internal/ContentPath.hpp` are the content
pipeline's existing spellings of `PathToGenericUtf8()` / `PathFromUtf8()` and forward to them
unchanged. Roughly a hundred pipeline call sites use them and are already correct.

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

## The ten questions, answered

**1. What encoding does a public CNA `std::string` path mean?** UTF-8. Where the type is fixed by
the XNA 4.0 API — `ContentManager.RootDirectory`, `Texture2D(const std::string& assetName)`,
`WaveBank`, `SoundBank` — the signature is preserved and the meaning is declared, rather than the
signature being changed. XNA's own equivalents are UTF-16 `String`; UTF-8 is the narrow encoding
that loses nothing relative to them.

**2. Are paths entering CNA expected to be UTF-8?** Yes. A game that hard-codes an ASCII asset name
is unaffected, because ASCII is UTF-8.

**3. How are Windows native paths converted?** Through `std::filesystem::path`'s own UTF-8 ↔ UTF-16
conversion (`u8string()` / `path(std::u8string)`), which is `MultiByteToWideChar(CP_UTF8, …)` and
its inverse. No `std::codecvt`, no locale, no ANSI code page. Where CNA calls Win32 directly it uses
the wide entry points and `Win32Utf::ToWide()` / `ToUtf8()`, which are strict `CP_UTF8`
conversions.

**4. How are paths displayed in logs?** As UTF-8, via `PathToUtf8()`, preserving the separators the
path actually has. CNA's log sinks are UTF-8. A Windows console that renders that as mojibake is a
console setting, not a corrupted path — see "Proving a path is right", below.

**5. How are paths serialized?** As generic-form UTF-8, via `PathToGenericUtf8()`. That is what
content manifests, XNB external references, `cna-buildcontent.json` and CNB asset names already
store, and the migration did not change any on-disk format.

**6. Are serialized paths portable between Windows and Linux?** Yes, and that is the reason for
generic form: `content/textures/a.png` is written on both, so content built on Linux loads on
Windows and the other way round.

**7. Are separators normalized?** Only by `PathToGenericUtf8()`, and only separators. Conversion
never resolves `.` or `..`, never canonicalises, never touches case, and never applies Unicode
normalisation. Those are separate decisions with separate call sites.

**8. What happens with invalid UTF-8?** `PathFromUtf8()` does not validate, and the two platforms
then differ because they genuinely differ (see question 9). On Windows the standard library throws
`std::filesystem::filesystem_error` — measured, deterministic, never silent mojibake. On POSIX the
bytes are preserved. A public API that wants one answer on both platforms calls
`IsWellFormedUtf8()` first and refuses explicitly.

**9. What about filesystem names not representable as UTF-8 on POSIX?** They are preserved. A POSIX
filename is an arbitrary byte sequence; rejecting non-UTF-8 bytes in the conversion would make files
that genuinely exist unopenable, which is a Linux regression traded for a Windows feature. This is
the one place the model is deliberately platform-asymmetric, and it is asymmetric because the
platforms are.

**10. Is lexical normalization part of conversion, or separate?** Separate. `lexically_normal()`,
`weakly_canonical()` and the containment helpers in `CNA/Internal/PathContainment.hpp` are called
where that behaviour is wanted. Note that on Windows `lexically_normal()` also rewrites `/` as `\`,
so a key built from `lexically_normal().string()` does not match one built from `generic_string()` —
another reason identities go through `PathToGenericUtf8()`.

---

## Third-party boundaries

Each library has its own contract, verified against its source rather than assumed.

| Library | What it expects on Windows | How CNA meets it |
|---|---|---|
| SDL3, SDL3_mixer | UTF-8 (`WIN_UTF8ToStringW` → `CreateFileW`) | pass `PathToUtf8()` |
| FFmpeg (libavformat) | UTF-8 (`ff_win32_open` widens with `utf8towchar`) | pass `PathToUtf8()` at the call |
| stb_image, stb_image_write | **ANSI** — the `_wfopen` branch needs `STBI_WINDOWS_UTF8`, which CNA does not define | do not pass a filename; use the memory entry points |
| cgltf | **ANSI** — `cgltf_default_file_read` is a bare `fopen` | install CNA-owned `cgltf_file_options` callbacks that open a native `path` |
| FreeType | documented as `fopen` semantics; the CMake build ships wide semantics, so it is build-dependent | use `FT_New_Memory_Face` and remove the question |
| zlib, Draco, dr_flac, dr_mp3, stb_vorbis | no path crosses — memory APIs only | nothing to do; keep it that way |
| shaderc | the `filename` argument is a diagnostic label, never opened | nothing to do |

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

Containment is asked as a path question, not a string question: normalise both sides, take
`lexically_relative()`, and treat a result that is empty or begins with `..` as an escape. That
answer is identical on both platforms, including for the `..config` case, where a directory whose
name merely begins with two dots is correctly *not* an escape.

Case sensitivity is not part of this model. Windows is usually case-insensitive and case-preserving,
POSIX usually is not, and CNA does not normalise case anywhere to paper over that. See
`CNA/Internal/CaseInsensitivePath.hpp` for the one place case is deliberately resolved, and
`plans/plan_win32_native_validation.md` WINNATIVE-F26 for the divergence that is recorded rather
than fixed.

Long paths are a separate concern from encoding and are tracked separately. With
`LongPathsEnabled=0`, which is the default, `std::filesystem` on MSVC enforces the legacy 260-character
`MAX_PATH` and `create_directories` fails beyond it. Unicode correctness does not imply long-path
support and this document does not claim it.

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
