# C API compatibility matrix

This file is generated from `tools/c-api/compatibility_matrix.json` by
`tools/c-api/generate_compatibility_matrix.py`. It records what the C ABI is **actually**
compiled and run against, and — just as deliberately — what it is not.

## How a cell is decided

The `CApi_HeaderCompatibility` test compiles every public header **on its own** and the
umbrella twice, in every declared language mode: 60 translation units per cell.
Two rules keep the result honest:

- **A toolchain that is installed is binding.** If it is present and rejects a header, the
  gate fails, whether the row is required or optional.
- **A toolchain that is absent is skipped, not passed.** The run says so by name, so a
  machine without Clang cannot look like a machine where Clang agreed.

A `required` row is one the host toolchain always provides, so its absence is a broken
environment rather than a narrower matrix, and fails.

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

## Build and run configurations

Compiling a header proves it parses. Running the C smoke programs is what proves the ABI
behaves, and every one of them runs in all four configurations below — the same source, four
different answers from the runtime underneath it.

| Configuration | Renderer | `CNA_DEVICES` | Role | What it is for |
|---|---|---|---|---|
| `headless` | `HEADLESS` | OFF | required | No window, no GPU. Every route that needs neither is proven here, and every route that does is proven to refuse. |
| `sdlrenderer` | `SDL_RENDERER` | ON | required | SDL's own 2D renderer under the dummy video driver, with the CNA_DEVICES half of devices-ext compiled in. |
| `software` | `SOFTWARE` | OFF | required | A CPU rasteriser: a third backend answer for every capability query, and the compiled-out devices half again. |
| `asan` | `SOFTWARE` | ON | required | AddressSanitizer and UndefinedBehaviorSanitizer with leak detection on. This is what catches a handle that outlives its owner or an operation nobody released. It runs the CPU rasteriser with the devices half compiled in, which is the one pairing the other three rows leave out: SOFTWARE is therefore exercised in both CNA_DEVICES states. |

## What is not covered

A matrix that only lists successes is not a matrix. These are the combinations this
repository does **not** exercise, and the reason each one is absent:

- **Running a Windows binary.** The cross-compiler proves the headers parse for a Windows target; nothing here links or executes one, so no Windows behaviour is claimed.
- **macOS, iOS, Android and the web targets.** No toolchain for any of them is present, so no cell exists for them at all rather than an untested claim.
- **C89.** The headers use `//` comments and mixed declarations, so C99 is the floor by design rather than by accident.
- **Renderers other than the four configured.** CNA has 46 renderer identities; four are built and run here. The other 42 share the same C surface, and the capability queries are what a caller uses to find out what any of them supports.
