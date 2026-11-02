# C API header compatibility matrix

This file is generated from `tools/c-api/compatibility_matrix.json` by
`tools/c-api/generate_compatibility_matrix.py`. It records where the public C headers are
actually compiled as source, and — just as deliberately — what that does not prove.

## What one cell proves

The `CApiHeaderCompatibility` test compiles every public header **on its own** and the
umbrella twice, in every declared language mode: 62 translation units per cell.
Two rules keep the result honest:

- **A toolchain that is installed is binding.** If it is present and rejects a header, the
  gate fails, whether the row is required or optional.
- **A toolchain that is absent is skipped, not passed.** The run says so by name, so a
  machine without Clang cannot look like a machine where Clang agreed.

A `required` row is one the host toolchain always provides, so its absence is a broken
environment rather than a narrower matrix, and fails.

A passing cell proves header parsing, self-containment, include guards and language-floor
compatibility. It does not link CNA, create a runtime or exercise a renderer; those are
separate build, CTest and platform qualifications.

## Compilers and language modes

| Toolchain | Language | Modes | Role | Why |
|---|---|---|---|---|
| `cc` | C | `c99`, `c11`, `c17` | required | The host C compiler CMake already uses. C99 is the floor: the public headers use nothing newer, and the gate is what keeps it that way. |
| `c++` | C++ | `c++11`, `c++14`, `c++17` | required | The host C++ compiler. A C ABI that a C++ consumer cannot include is only half an ABI, so every header is compiled in both languages. |
| `gcc` | C | `c99`, `c11`, `c17`, `c23` | optional | GCC by name, including C23, which the host compiler may predate. |
| `g++` | C++ | `c++17`, `c++20`, `c++23` | optional | GCC's C++ driver at the standards CNA itself builds with. |
| `clang` | C | `c99`, `c11`, `c17`, `c23` | optional | A second front end catches what one compiler's extensions would hide. |
| `clang++` | C++ | `c++17`, `c++20`, `c++23` | optional | Clang's C++ driver, for the same reason. |
| `x86_64-w64-mingw32-gcc` | C | `c99`, `c11`, `c17` | optional | A Windows target reached by cross-compiling. Headers only: nothing here links or runs a Windows binary, and the matrix says so rather than implying a tested platform. |
| `x86_64-w64-mingw32-g++` | C++ | `c++17`, `c++20`, `c++23` | optional | The matching Windows-target C++ driver. It proves the same public C headers remain consumable from Windows C++ translation units; like the C row, it compiles only. |

## What this matrix does not claim

These boundaries are part of the contract: evidence from another build or platform is not
silently promoted into a header cell here.

- **Runtime and renderer behaviour.** This is a source-compatibility matrix for the public headers, not a runtime matrix. C API smoke, package, sanitizer, renderer and platform tests are registered in the applicable CMake configurations and carry their own evidence; this document does not claim that every smoke program runs under every renderer.
- **MinGW linking and execution.** The two MinGW rows cross-compile headers for a Windows target but do not link or run a Windows binary. Native Windows and Wine/DXVK runtime evidence belongs to their platform workflows, not to these header cells.
- **MSVC and Apple Clang.** Neither native compiler is a declared cell. Their platform builds and tests are useful independent evidence, but do not prove this matrix's per-header, per-language-mode contract.
- **Emscripten and Android NDK Clang.** Neither target compiler is a declared header cell. The separately built WebAssembly artifact and Android CNA builds do not imply that every public C header has been compiled alone in each language mode with those target macros and system headers.
- **C89.** The headers use `//` comments and mixed declarations, so C99 is the floor by design rather than by accident.
