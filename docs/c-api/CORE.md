# CNA C API Core Contract

## Scope

`CNA/C/core.h` is the C API's own substrate — results, error categories, the per-thread diagnostic,
`CNA_StringView` and the small shared value types. `CNA/C/core_ext.h` is a different thing: it maps
CNA's own `core` module, the part of the framework that is deliberately not XNA — the logger, the
compile-time platform and renderer identity, and the two backend classifications. Together they
close the `core` module: no planned row remains.

Everything here is process-level or compile-time state. Nothing in this header needs a runtime, a
game, a graphics device or any initialization, and nothing here owns a handle.

## Logging

Each canonical static gets its own route, so C never depends on a defaulted argument:

- `cna_logger_log` takes level, message, category and condition explicitly;
- `cna_logger_fatal` … `cna_logger_experiment` take message and category;
- `cna_logger_fatal_if` … `cna_logger_experiment_if` take message and condition, because the
  canonical conditional overloads accept no category and always log under `APPLICATION`.

A message crosses as a borrowed `CNA_StringView` and is copied into an owned string before the
canonical `std::string_view` is formed, so no view a caller passes in can outlive its call.

`CNA_LogLevel` carries the canonical ordinals exactly, and they are deliberately not contiguous:
`CNA_LOG_LEVEL_EXPERIMENT` is **100**, not 6. The value 6 is not an identity and is refused. The
identities are pinned by C and C++ ABI assertions.

`cna_logger_set_minimum_level` and `cna_logger_get_minimum_level` map the threshold. It is
**process-wide static state** in the canonical logger — setting it affects every thread and every
CNA subsystem, not just the caller — so a consumer that changes it is expected to put it back.

`CNA::Logger` is a static-only utility that is never instantiated, so there is no logger handle and
nothing to release.

## Platform and desktop operating system

`cna_platform_get_current` maps a compile-time constant, so the answer never changes for a given
binary.

`cna_desktop_os_get_current` preserves the canonical refusal rather than inventing a fallback: the
canonical function **throws** when the platform is not desktop, and the firewall converts that to
`CNA_RESULT_INVALID_STATE` with `CNA_ERROR_CATEGORY_STATE`. A caller that may run on mobile or in a
browser checks the platform first.

That conversion is the mapping for `CNA::CNAException` in general: it becomes
`CNA_RESULT_INVALID_STATE` with the message in the per-thread diagnostic, and no exception object,
type name or C++ throw is ever exposed.

## Backend classification

Two orthogonal classifications are mapped, each with the same three-route shape:

- `cna_graphics_backend_get_category` / `_get_current_category` — implementation technology
  (native, translation layer, software, web, diagnostic);
- `cna_graphics_backend_get_maturity` / `_get_current_maturity` — recommendation confidence
  (production, supported, experimental, historical, deprecated).

Both classify **any** of the 49 public renderer identities, not only the one compiled into this
build, exactly as the canonical functions do. An identity outside that set — including
`CNA_GRAPHICS_RENDERER_UNKNOWN` — is refused rather than silently classified.

`cna_graphics_renderer_get_current_type` reports the compiled-in identity without needing a device
or any graphics initialization, unlike `cna_graphics_device_get_info`. **An identity is not a
capability claim.** Probe the behavior a consumer actually depends on rather than branching on this
value; the C API's own tests follow that rule, which is why they run unchanged across every
verification tree.

## Names

The canonical name functions return a `std::string_view` at static storage. C gets the project's
count/copy pair instead — `_get_name_size` plus `_copy_name`, and
`cna_graphics_renderer_get_current_name_size` plus `cna_graphics_renderer_copy_current_name` — so
no pointer into CNA's static storage crosses the ABI. As everywhere else in this API, the copy
writes **no terminator**, reports the required byte count, and writes nothing at all when the
capacity is too small.

The renderer name matches the `CNA_GRAPHICS_RENDERER` build option exactly, for example
`"HEADLESS"` or `"SDL_RENDERER"`.

## The `CNAEXT` marker

`CNAEXT` has no C mapping and is recorded as not-applicable. It is a documentation-only marker that
expands to nothing in a normal build and tags a C++ declaration as a CNA extension rather than XNA
4.0; it names no callable behavior. The C API carries the same distinction in its route names,
where an `_ext` suffix marks a route with no canonical counterpart.
