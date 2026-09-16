# `windows-unicode-path-spike` — what breaks when a path is not in the ANSI code page

The existence gate for `plans/plan_windows_portability.md` (WINPORT-0003). It exists because
WINNATIVE-F30 was recorded as a *prescription* ("narrow with `u8string()`, widen with
`path(std::u8string)`") that had never been measured, and one half of that prescription turns out
to be wrong.

`unicode_path_probe.cpp` creates a real file under a real directory for each of eleven path
classes, writes a known ASCII payload into it, and then asks fifteen questions per class. Every
PASS is a byte-for-byte read of that payload, never a string the probe printed — a terminal that
cannot render a name is not evidence that the name is wrong.

## How it was run

```bash
# Linux reference
g++ -std=c++20 -O1 -o unicode_path_probe unicode_path_probe.cpp && ./unicode_path_probe

# Windows (in the validation VM), from the repository root
tools/platform/windows_vm_exec.sh --push spikes/windows-unicode-path-spike/unicode_path_probe.cpp \
    C:/cna/tmp/unicode_path_probe.cpp
# then, inside vcvars64: cl /std:c++20 /EHsc /utf-8 /O1 unicode_path_probe.cpp
# and pull the redirected output back as raw bytes rather than reading it through PowerShell
```

Recorded output: `probe_linux.txt`, `probe_windows_cp1252.txt`.

## The result

```
Linux   (glibc, GCC 14)                 165 pass    0 fail
Windows (MSVC 19.44, ACP 1252, SDK 26100) 158 pass   51 fail
```

Nine of the eleven classes fail something on Windows. Seven are not representable in CP1252 at
all (`žluťoučký`, `e`+U+0301, `кириллица`, `日本語`, `中文`, `emoji-😀`, and the mixed one); two
more — `café` and precomposed `étude` — are representable, and still fail one case each.

### What never fails, on either platform

| Operation | |
|---|---|
| `fs::create_directories(path)` | native `path` throughout |
| `std::ofstream(path)` / `std::ifstream(path)` | the `path` overload, not `const char*` |
| `fs::exists(path)`, `fs::file_size(path)` | |
| `fs::directory_iterator` | returns correct native paths |
| `path::u8string()` | **never throws**, for any of the eleven |
| `path` → `u8string()` → `path(std::u8string)` → `exists()` | round-trips losslessly |
| `GetFileAttributesW`, `CreateFileW`, `_wfopen` | the wide Win32/CRT surface |

### What fails, and for which inputs

| Operation | Fails for | Why |
|---|---|---|
| `path::string()` | the 7 non-CP1252 classes | MSVC converts native UTF-16 → `CP_ACP` and throws when a character has no mapping |
| `path::generic_string()` | the same 7 | **it narrows through the ANSI code page too** — separator normalisation is orthogonal and fixes nothing here |
| round trip via `string()` | the same 7 | it threw; there is nothing to round-trip |
| `std::ifstream(const char*)` from `string()` | the same 7 | |
| `fopen()` from `string()` | the same 7 | |
| `GetFileAttributesA` from `string()` | the same 7 | |
| **`fopen()` given UTF-8 bytes** | **9 classes** | the CRT reads the bytes as CP1252 |

## The two findings that change the prescription

**1. `generic_string()` is not a fix.** It fails on exactly the same seven inputs as `string()`.
Separator style and text encoding are different problems; WINNATIVE-F25's `generic_*` change was
correct for separators and is irrelevant to this.

**2. Narrowing to UTF-8 and handing the bytes to a narrow C API is *worse* than the status quo
for some inputs, not better.** `café` is representable in CP1252, so `fopen(p.string())` opens it
today. `fopen(p.u8string())` does not: UTF-8 encodes `é` as `0xC3 0xA9`, and the CRT reads those
two bytes as `Ã©`. A mechanical `.string()` → `.u8string()` migration would therefore *break* a
class of paths that currently works, on top of failing the seven that already fail.

The conclusion the migration is built on: **UTF-8 is CNA's text encoding for paths, not a way to
call a narrow API.** Anything that opens, stats, creates, enumerates or deletes must hold a
`std::filesystem::path` all the way to the call. UTF-8 `std::string` is for display, logging,
serialization and identity — and it is converted back to a `path` before it touches the
filesystem again.
