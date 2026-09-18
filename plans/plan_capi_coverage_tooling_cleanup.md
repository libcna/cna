# Plan — C API coverage tooling cleanup

Branch `capi-coverage-tooling-cleanup`, cut from **`9ff1c6ec3`** (`docs(capi-audit): the backlog is
250 missing bindings, not 3,007`). That commit is not in `next`, so it is the base.

Scope: make the C API coverage, limitations and ownership tooling report the current tree
truthfully. **No C binding is implemented here** — not the genuinely missing runtime routes, not
the Content Pipeline, not `Microsoft::Phone`. No public C ABI change: `0.29.0`, 4,055 exports, 221
structs throughout.

The branch moves the tooling from *red gates with misleading ownership and inflated counts* to
*trustworthy data describing the actual runtime C API backlog*. Success is not "coverage is 100 %";
success is that implemented routes, genuine missing bindings, and intentionally out-of-scope C++
surface are told apart and each is counted correctly.

| Task | Subject | Status |
|---|---|---|
| CTC-1 | Baseline: reproduce every failure and count at the starting SHA | ✅ |
| CTC-2 | Model runtime C API scope explicitly, by module, and make the model total | ✅ |
| CTC-3 | Fix the case-sensitive internal-path exclusion | ✅ |
| CTC-4 | Remove the `owner_task()` fallthrough to a completed task | ✅ |
| CTC-5 | Resolve rule ambiguity at its source: ownership is per symbol, not per pattern | ✅ |
| CTC-6 | Map the already-bound-but-unmapped APIs; classify the non-bindable ones | ✅ |
| CTC-7 | Record the Content Pipeline and `Microsoft::Phone` scope decisions (CBIND-117) | ✅ |
| CTC-8 | Regenerate `COVERAGE.md`; restore the limitations, coverage and release gates | ✅ |
| CTC-9 | Regression tests for every defect class repaired here | ✅ |
| CTC-10 | Full validation: ABI, renderer invariants, TERMINAL + SOFTWARE, CTest corpus | ✅ |

---

## CTC-1 — the measured baseline

Measured at `9ff1c6ec3`, working tree clean. Every number below was reproduced from the tree, not
copied from `docs/c-api/COVERAGE_AUDIT.md`.

### Scanner and matrix

| | |
|---|---|
| Public headers scanned | 684 |
| Headers already excluded (`platform`, `Internal`, `Detail`) | 159 |
| Public C++ declarations | **12,733** |
| `docs/c-api/COVERAGE.md` (stale, 2026-09-01 snapshot) | 9,499 |
| implemented / partial / planned / not-applicable | 8,323 / 15 / 3,895 / 500 |

Of the 3,895 `planned` rows, **3,007 are owned by tasks the plan records as ✅ complete** and 888 by
open tasks.

### The three red gates

| Gate | Result |
|---|---|
| `generate_coverage_inventory.py --check` (`CApiCoverageMatrix`) | exit 2 — 14 complete tasks own planned rows |
| `generate_limitations.py --check` (`CApiLimitations`) | exit 2 — same underlying error |
| `generate_coverage_inventory.py --approve-rule-symbols` | exit 2 — ambiguous rules on `CNA::Graphics::PbrMaterial` |

`validate_planned_row_owners` names all fourteen: CBIND-034 (17), **CBIND-044 (2,611)**, CBIND-035
(198), CBIND-080 (44), CBIND-084 (38), CBIND-036 (32), CBIND-093 (20), CBIND-037 (19), CBIND-104
(15), CBIND-108 (4), CBIND-114 (4), CBIND-083 (2), CBIND-110 (2), CBIND-105 (1).

### Where the backlog actually comes from

| Category | Rows | Logical APIs |
|---|---:|---:|
| Build-time Content Pipeline, awaiting the scope decision | 2,988 | 2,567 |
| **Genuinely missing runtime bindings** | 420 | 388 |
| lowercase `detail/` tooling artifact | 316 | 66 |
| Already bound; only the mapping rule is missing | 83 | 27 |
| `Microsoft::Phone`; owned by no task | 57 | 54 |
| No meaningful C form (`operator=`, copy ctor) | 15 | 14 |
| Test seams / friend declarations | 12 | 9 |
| POD bound by `_init`; `Dispose` has no C form | 4 | 4 |
| Unknown | 0 | 0 |

### CBIND-044 owns nothing it can fix

`CBIND-044` is recorded ✅ ("Closed 2026-08-16 … 0 planned"). It now owns 2,611 planned rows, and
measuring where they come from settles what they are:

| Module | Rows |
|---|---:|
| `content-pipeline` | 2,554 |
| `phone` | 57 |
| **anything else** | **0** |

**Zero of CBIND-044's rows are genuinely missing runtime bindings.** They are the whole of two
modules that the runtime C API does not link.

The root cause is the guard in `owner_task()`:

```python
if header.parts[1] == "content" and ("Pipeline" in header.parts or "Import" in header.parts):
    return "CBIND-117"
```

`header.parts[1]` is the module directory. When the pipeline became its own module on 2026-09-03
the path became `modules/content-pipeline/…`, the guard stopped matching, and 2,554 rows fell
through every branch of the function to its final `return "CBIND-044"` — a default that assigns
unowned symbols to a *completed* task. `modules/phone`, created 2026-09-07, was never in the
function at all and landed in the same place.

### Scope evidence: what the runtime C API actually links

`modules/c-api/CMakeLists.txt:320`:

```cmake
target_link_libraries(cna_c_api PRIVATE cna_core cna_platform cna_runtime cna_graphics_ext
    cna_storage cna_content cna_media cna_devices cna_devices_ext CNA_Net CNA_GamerServices)
```

`cna_content_pipeline` is not there, and `cna_phone` is linked by nothing in the repository. Neither
module's symbols can be missing *runtime* C bindings, because neither is in the runtime.

### Rule ambiguity is 34 pairs, not one

`--approve-rule-symbols` reports only `PbrMaterial`, because it raises on the first conflict.
Measured across the whole rule set: **34 ambiguous rule pairs affecting 296 symbols.**

Every pair has the same shape — a baseline *contract* rule (`qualified_name_regex: ".*"` scoped to
one header, or an enumerated whole-type member list) against a later *carve-out* rule that names
specific members. The largest are `graphics-ext-settings-values` (CBIND-035F7) against
`render-pipeline-settings` (CBIND-088A, 77 symbols) and against `pbr-material-value` (CBIND-087B,
53 symbols).

The decisive measurement is that **no symbol is approved by two rules** — `both = 0` on all 34
pairs. The `approved_symbols` sets already partition ownership correctly and by hand; only the
*patterns* overlap. 43 of the 296 symbols are approved by the baseline rule alone and 248 by the
carve-out alone, so a blanket "the narrower rule wins" precedence would silently de-cover 43
symbols. Ambiguity is therefore a defect in how `--approve-rule-symbols` reads the data, not in the
data.

Exactly **5** symbols in an overlap are approved by neither side and are genuinely unresolved:
`Effect::Effect(GraphicsDevice&, const std::string&)`, `TextureCube`'s move constructor and move
assignment, and two `VertexBuffer::SetData` template overloads.

### Out-of-scope surface, located

| Surface | Where | Rows |
|---|---|---:|
| Content Pipeline module | `modules/content-pipeline/**` | 2,554 |
| Content Pipeline inside a linked module | `modules/content/include/CNA/Content/{Pipeline,Import}/**` | 874 (already CBIND-117) |
| Windows Phone 7 | `modules/phone/include/Microsoft/Phone/**` | 57 |

The lowercase `detail/` leak is 316 rows in 4 headers, and both offending directories are inside
`modules/content-pipeline`. `path_is_explicitly_internal` compares against `("Internal", "Detail")`
case-sensitively, so `…/Pipeline/Graphics/detail/` and
`…/Serialization/Intermediate/detail/` are admitted as public API.

### ABI, renderers, build

| | |
|---|---|
| ABI version | `0.29.0` |
| Exported `cna_*` symbols | 4,055 |
| Public structs | 221 |
| `cna_c_api` target | builds (see CTC-10) |

---

## CTC-2 — scope is declared, total, and checked

`MODULE_SCOPE` classifies every module that publishes an `include/` tree as runtime scope or out of
it, each exclusion carrying the decision that made it. `OUT_OF_SCOPE_SUBTREES` does the same for
build-time surface living *inside* a linked module (`content/CNA/Content/{Pipeline,Import}`).
`validate_module_scope` fails unless the classification is total in both directions — an
unclassified module, a classification with no module, an exclusion with no reason, or a subtree rule
pointing at a path that does not exist.

That is the whole repair for the incident: the defect was never a wrong exclusion, it was a missing
one, and a table that *must* name every module cannot go missing quietly.

Scope evidence is the link closure, not a path heuristic. `cna_c_api` links eleven modules
(`modules/c-api/CMakeLists.txt:320`); `cna_content_pipeline` is not among them and `cna_phone` is
linked by nothing in the repository.

**Scanner: 12,733 → 9,355 symbols; 684 → 556 headers; 159 → 287 excluded headers.**

## CTC-3 — internal-path exclusion

`EXCLUDED_PATH_SEGMENTS` is compared case-insensitively. It is still an exact *segment* match, so
`Details/`, `DetailLevel.hpp` and `InternalFormat/` stay public — the rule is about a directory
called `detail`, not about the word appearing somewhere. Both offending directories were inside
`modules/content-pipeline`, so the scope decision would have hidden the symptom; the exclusion is
fixed anyway, because the next lowercase `detail/` will not be so conveniently placed.

**316 rows in 4 headers, gone.**

## CTC-4 — ownership has no silent default

`return "CBIND-044"` is gone, and so is the `graphics` module's CBIND-034/CBIND-035 split, which
answered "which phase would have bound this" — history, not ownership — for 74 declarations nothing
binds today. The single answer of last resort is `UNMAPPED_RUNTIME_SURFACE_TASK` = `CBIND-127`, a
task the plan records as open.

`REOPENED_SLICES` names the eleven header slices whose owning task finished while declarations in
them stayed unbound, and routes those rows to the same live task. The slice tables keep their
historical record; `validate_reopened_slices` fails when a key there has no unbound declaration
left, so the set shrinks as the backlog is bound instead of accumulating.

**Rows owned by a completed task: 3,007 → 0.** `CBIND-044`: 2,611 → 0.

## CTC-5 — ambiguity resolved by approved ownership

Root cause: `--approve-rule-symbols` ran with `ignore_approval=True` and raised whenever two
patterns reached one symbol — discarding `approved_symbols`, which is the field that records *which*
rule owns it. Broad patterns are intended ("this header's whole contract"), so the overlap was not
the defect.

The measurement that decided the fix: across 34 ambiguous pairs and 296 symbols, **no symbol was
approved by two rules**. 43 were approved by the baseline rule alone and 248 by the carve-out alone,
so the "narrower rule wins" precedence would have silently de-covered 43 declarations. The data was
already a clean partition, hand-made and correct.

`resolve_rules()` therefore resolves per symbol: the rule that approved it owns it; two approvals is
an error; an overlap nobody approved is an error naming the symbol and both candidates. Precedence
is never positional.

Five symbols were genuinely unresolved. Four were settled by recording the ownership verification
found (CTC-6) and one by the move-semantics disposition.

**34 ambiguous pairs / 296 symbols → 0. `--approve-rule-symbols` completes with exit 0.**

Its output is deliberately *not* committed: re-approval would extend 49 rules by 95 declarations
nobody has reviewed, which is the CBIND-050 failure the approval pinning exists to prevent. The tool
being runnable is the deliverable; running it is a reviewer's decision.

## CTC-6 — mapping and classification

The 83 rows the audit called "already bound; only the mapping rule is missing" were verified one at
a time against the 4,055 exported symbols, the public C headers and the route implementations.
**The bucket was wrong for 42 of them.**

| verdict | rows |
|---|---:|
| genuinely already bound | 41 |
| no exported route answers it | 38 |
| C++ move constructor, no C form | 4 |

The 41 were recorded against the rules that own them — twelve existing rules extended, two new ones
(`sprite-batch-dispose-pattern`, `graphics-device-clear-vector4`) where no pattern reached them.
Each carries why the route answers that overload, and where the C shape is narrower than the C++
declaration it says so: `cna_index_buffer_set_data_at` refuses a non-`None` `SetDataOptions`; the
raw vertex window cannot space destination slots wider than the element; a C caller cannot bind a
new `DirectionalLight` to three parameter handles of its own choosing.

The 38 are genuine missing bindings and moved to `CBIND-127` — mostly `TextureCube` and `Texture3D`
typed transfers, which have no analogue of `CNA_TextureDataType`, so a compressed cube map cannot be
uploaded from C at all.

Forty declarations with no meaningful C form carry recorded dispositions: the four state PODs' copy
constructor, copy assignment and `Dispose`; move construction and move assignment on the five
resource types; five friendship declarations; and the `*Internal` helpers that exist only because
the public operation above them is a template. None invents a C function to make a percentage green.

The audit script's own two defects, found by the same verification, are fixed: it missed move
constructors entirely, and its export probe credits a route that shares a name token without
checking it carries the arguments the overload is about. Its docstring now records that measured
error rate rather than implying the partition is evidence.

## CTC-7 — CBIND-117 and the Phone scope decision

`CBIND-117` asked two questions. The build-time pipeline half is decided — out of runtime C API
scope — and recorded in its plan row with the link-closure evidence. The Model-v2 CPU API half is
still open and still owns 134 rows, so the row is 🟨 rather than ✅: `CNA/Content/Cnb` stays *in*
scope deliberately, because `DecodeModelV2FromCnb` and the CNB source importers are a
runtime-adjacent question the pipeline decision does not answer.

`Microsoft::Phone` is out of scope. No repository policy promises it C parity — the only prior
mention, `CBIND-124`, bound `Microsoft::Devices::Environment::DeviceType` from the *devices* module,
not this one — and nothing links `cna_phone`. The exclusion is mechanically testable rather than a
convention.

## CTC-8 — regenerated artifacts and restored gates

| Artifact | Before | After |
|---|---|---|
| `COVERAGE.md` | 9,499 symbols (2026-09-01 snapshot) | 9,355 symbols, current |
| coverage gate | exit 2 | **exit 0** |
| limitations gate | exit 2 (traceback on ownership) | **exit 0** |
| release gate | 1 criterion disagreed | **exit 0** |
| `--approve-rule-symbols` | exit 2 (ambiguity) | **exit 0** |

`9,499 → 9,355` is not a small correction to one number; the two are measurements of different
sets. The old figure was a stale snapshot of a tree that has since grown, and the new one excludes
3,428 build-time pipeline declarations, 57 Windows Phone declarations and 316 lowercase-`detail`
declarations that were never runtime C API obligations.

The release gate is green **without hiding anything**. Its `coverage-closed` criterion demands zero
planned rows and is recorded "not met"; it still measures "not met", and that agreement is what the
gate checks. 468 genuine limitations remain visible and counted.

## CTC-9 — regression tests

`tools/c-api/test_coverage_scope.py`, 21 fixture tests, registered as the CTest gate
`CApiCoverageScopeModel`. No Doxygen, well under a second. One test per defect that shipped: a new
module stops the gate; a removed one does too; an exclusion without a reason is refused; no
out-of-scope header reaches the inventory; `detail` matches in every capitalization but only as a
whole segment; the last-resort owner is not a completed task; an unrecognised module lands on the
live backlog; a completed task owning a planned row fails; overlapping rules resolve by approval,
double approval is refused, an unapproved overlap is refused by name, and rule order changes
nothing.

## CTC-10 — validation

| Check | Result |
|---|---|
| `cna_c_api` build | clean |
| `generate_coverage_inventory.py --check` | **exit 0** |
| `generate_limitations.py --check` | **exit 0** |
| `check_release_gate.py --check` | **exit 0** (recorded "not ready", correctly) |
| `--approve-rule-symbols` | **exit 0** (output reviewer-gated, not committed) |
| `generate_abi_baseline.py --check --library` | 221 structs, **4,055 exports** |
| `check_declared_exports.py --library` | declared and exported agree exactly: 4,055 |
| `check_doc_export_counts.py` | 6 prose counts agree with 4,055 |
| `check_renderer_identities.py` | 25 identities / 21 families / 26 retired / next free 52 |
| `ctest -R '^CApi'` | **111/111** |
| `ctest -R 'Terminal\|Software'` | **157/157** |
| `CApiCoverageScopeModel` | **21/21** |
| full corpus, `ctest -j8` | 9,931 of 9,934 reported before a 50-minute cap; 9,408 passed, 507 skipped, **16 failed** |

### The 16 corpus failures are pre-existing, and none is in this branch's reach

Two were flaky and passed on a re-run (`SoundBankTest.IsInUseFalseSoonAfterFireAndForgetCue…`,
`TwoProcessLoopbackTest.HostMigration…`). The other fourteen reproduce, and all of them are C++
runtime behaviour:

| Failure | What it is |
|---|---|
| `CnbTextureContentManagerTest` ×2, `CnbTextureCubeProducerTest`, `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`, `CnjTexture3DTest.LoadsRealCnjFixture` | `TextureCube::SetData: this graphics renderer did not store the complete requested cube face region` — the HEADLESS renderer's cube/volume face support |
| `CNAEXT_NoPosixSetenv` | a lint gate over `modules/platform/src/Wayland/` and its tests using POSIX `setenv`/`unsetenv`, which MinGW-w64 lacks |
| `Headless_Smoke` | `SDL_INIT_VIDEO was never initialized under the Headless renderer`; 9 of its 10 sub-checks pass |
| `CueTest` ×2, `WaveBankTest` | audio playback timing |
| `ENetDiscoveryServiceTest`, `NetworkSessionTest.FindReturnsEmptyCollection` | networking |
| `XnaPipelineGenuineRuntimeBuiltFamilies`, `CnaXnbModelCorpusSweep` | content pipeline runtime |

Two facts settle the attribution. `git diff 9ff1c6ec3..HEAD -- '*.cpp' '*.hpp' '*.h' '*.c'` is
**empty** — this branch changes Python, JSON, Markdown and one `add_test` line and nothing else —
and the test binaries were compiled before the first edit and never rebuilt, so what failed is the
pre-existing tree. Per the branch's scope they are left alone, not fixed and not suppressed.

The cube-face failures are worth noting for a different reason: they are the same capability gap
the binding verification found independently — `cna_texturecube_set_data` takes `const CNA_Color*`
only, and 23 of `CBIND-127`'s rows are the typed cube transfers that have no C route. The C++ side
has a matching renderer gap.

ABI `0.29.0`, 4,055 exports, 221 structs — unchanged. `git diff` over `modules/c-api/` and
`tools/c-api/abi_baseline.json` is empty, so no C ABI or API expansion occurred.

### The backlog this leaves, honestly stated

| Owner | Status | Rows | Logical APIs |
|---|---|---:|---:|
| `CBIND-127` — runtime surface added after the campaign closed | ⬜ | 320 | 259 |
| `CBIND-117` — Model-v2 CPU API scope question | 🟨 | 134 | 133 |
| `CBIND-122`, `CBIND-125`, `CBIND-120`, `CBIND-121` — open dispositions | ⬜ | 14 | 14 |
| **total planned** | | **468** | **406** |

`CBIND-127` concentrates where the tree grew after its slices closed: `graphics-ext` 133,
`graphics` 113, `content` 43, `runtime` 11, `input` 10, `math` 10. The largest single families are
`StorageBuffer` (30), `ShaderPackageEXT` (28), `StorageTexture2D` (26), `Texture2DArray` (25) and
`TextureCube`'s typed transfers (23).

The audit estimated 388 genuinely missing logical APIs. The measured figure is **259** for
`CBIND-127`, and the difference is accounted for rather than assumed: 41 rows were verified as
already bound and recorded, 40 as having no C form, and the audit's H2 bucket also contained rows
that belong to the still-open `CBIND-117` scope question rather than to missing bindings. Moving in
the other direction, 38 rows the audit had placed in H1 turned out to be genuinely missing. The
recomputation was done from the rules, not from the heuristic that produced the estimate.
