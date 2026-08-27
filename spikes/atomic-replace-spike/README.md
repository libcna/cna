# atomic-replace-spike

`plans/plan_cnb.md` `CNBF-123`. Two probes that answer, by measurement rather than by reading,
whether `tools/common/CnaToolAtomicWrite.hpp` actually does what it says on Windows.

`CNBF-122` introduced that header with a comment claiming `std::filesystem::rename` "on every
platform CNA targets replaces the existing file in one step". Nothing had ever executed the header
on Windows: `CnbSourceToolTests.cpp` is POSIX-only (`posix_spawn`), so the whole Windows path was
implemented and untested.

## What was measured

Cross-compiled with `x86_64-w64-mingw32-g++` (GCC 14.2, libstdc++) and run under Wine.
**Wine is not Windows** — it is the Win32 API reimplemented — so these results are strong evidence
about the Win32 contract and the libstdc++ mapping onto it, not a certification on Microsoft
Windows. Nothing here was run on a real Windows machine or against the MSVC STL.

### `probe_rename.cpp` — what `std::filesystem::rename` does with an existing destination

```
absent-destination  : ec=0 (ok) exists=1 contents=NEW
existing-destination: ec=0 (ok) dest=NEW tempStillThere=0
crt-rename          : rc=-1 dest=OLD
```

Two findings:

* The **C runtime's `rename()` fails** when the destination exists (`rc=-1`, destination untouched).
  So "rename replaces the destination" is *not* a property of Windows.
* libstdc++'s `std::filesystem::rename` **does** replace it — because it does not use the CRT.
  Disassembly shows why:

  ```
  _ZNSt10filesystem6renameERKNS_7__cxx114pathES3_RSt10error_code:
      mov    $0x3,%r8d
      call   *__imp_MoveFileExW
  ```

  `0x3` is `MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED`.

That is a library detail, not a guarantee, and CNA also builds with MSVC — which could not be
exercised here. The helper therefore calls `MoveFileExW` itself, with `MOVEFILE_REPLACE_EXISTING`
and deliberately **without** `MOVEFILE_COPY_ALLOWED`: the temporary is always a sibling of the
destination, so a cross-volume move cannot legitimately arise, and permitting one would let
publication degrade silently into a non-atomic copy-then-delete.

### `probe_helper_contract.cpp` — the real header's own contract

Compiles `tools/common/CnaToolAtomicWrite.hpp` itself (`-I tools/common`) and asserts every clause.
The first run found that **the `CNBF-122` helper did not work on Windows at all**:

```
terminate called after throwing an instance of 'std::runtime_error'
  what():  cannot create a temporary file beside 'C:\...\created.cnb'
```

The cause is `std::ios::noreplace`. Measured directly:

| open | Linux | Windows (MinGW) |
|---|---|---|
| `noreplace` on a **free** name | succeeds | **fails** |
| `noreplace` on a taken name | fails | fails |
| plain `out` on a free name | succeeds | succeeds |
| CRT `fopen("wbx")` on a free name | succeeds | succeeds |

libstdc++'s Windows filebuf refuses a `noreplace` open even when the name is free, while the CRT's
own exclusive mode works on the same path — a library gap, not an OS limitation. Every one of the
helper's 64 attempts failed, so on a MinGW Windows build `cna_tool_source_to_cnb` and
`cna_tool_gltf_to_cnb` could not write **any** output.

The temporary is now created and written through the platform's own exclusive-create primitive
(`O_EXCL` / `CREATE_NEW`), and the contract passes identically on both platforms:

```
absent destination is created                              ok
no temporary remains after creating                        ok
existing destination is replaced whole                     ok
no tail of the longer previous file survives               ok
an empty payload replaces with 0 bytes                     ok
a destination in a missing directory is refused            ok
the refusal created nothing                                ok
publishing over a directory is refused                     ok
the refused publication left the destination alone         ok
the failed publication left no .cnatmp- file behind        ok
a taken temporary name does not stop the write             ok
the squatting file was not overwritten                     ok
ALL PASS (0 failure(s))
```

## Reproducing

```bash
x86_64-w64-mingw32-g++ -std=c++23 -static -O0 \
    spikes/atomic-replace-spike/probe_rename.cpp \
    -o spikes/atomic-replace-spike/probe_rename.exe
x86_64-w64-mingw32-g++ -std=c++23 -static -O1 -I tools/common \
    spikes/atomic-replace-spike/probe_helper_contract.cpp \
    -o spikes/atomic-replace-spike/probe_helper_contract.exe

unset WAYLAND_DISPLAY                     # or wine hijacks the session's Wayland display
WINEDEBUG=-all wine spikes/atomic-replace-spike/probe_rename.exe
WINEDEBUG=-all wine spikes/atomic-replace-spike/probe_helper_contract.exe

# The same contract natively, which must agree:
g++ -std=c++23 -O1 -I tools/common \
    spikes/atomic-replace-spike/probe_helper_contract.cpp -o /tmp/contract && /tmp/contract
```

The built `.exe` files are gitignored; the sources and this record are what is kept.
