# C API coverage backlog — audit

**Branch** `capi-coverage-audit` · **baseline** `23801d2baacec3debc9e9e2a26dbc3c273b1f459` ·
**measured** 2026-09-18 · plan: [`plans/plan_capi_coverage_audit.md`](../../plans/plan_capi_coverage_audit.md)

Reproduce with `python3 tools/c-api/audit_coverage_backlog.py`.

---

## The answer

> **Are there ~3,000 missing C bindings, or ~3,000 rows of mis-modelled evidence?**

Overwhelmingly the second. Of the 3,007 planned rows owned by tasks `plans/plan_binding.md` marks
complete, **250 are genuinely missing logical C APIs** — 8.3 % of the rows. The other 91.7 % is one
architectural question that was never routed to the task that owns it, plus four mechanical tooling
defects.

Counting every planned row, open tasks included, the genuine gap is **388 logical APIs** across
420 declarations.

| | rows | logical APIs |
|---|---:|---:|
| Raw tooling backlog (all `planned`) | 3,895 | 3,128 |
| — owned by ✅ tasks (the headline "3,007") | 3,007 | — |
| — owned by open tasks | 888 | — |
| **Genuinely missing C bindings** | **420** | **388** |
| Already bound; only the mapping rule is missing | 83 | 27 |
| Intentionally no C form (value semantics, POD lifetime) | 19 | 18 |
| Awaiting an owner decision: build-time Content Pipeline | 2,988 | 2,567 |
| Tooling artifact (`detail/` leak) | 316 | 66 |
| Test seam / friend declaration | 12 | 9 |
| Unowned scope (`Microsoft::Phone`) | 57 | 54 |
| Unknown | 0 | 0 |

---

## Baseline, reproduced

Every figure below was measured on this branch, not carried over.

| Fact | Measured |
|---|---|
| Public headers scanned / excluded | 684 / 159 |
| Public C++ symbols | 12,733 |
| implemented / partial / planned / not-applicable | 8,323 / 15 / 3,895 / 500 |
| Coverage rules | 684 |
| ABI version | **0.29.0** (encoded 7424) |
| Exports / structs | **4,055** / **221** |
| Renderers | 25 identities, 21 families, 26 retired, next free 52 |
| `cna_c_api` (HEADLESS debug) | builds clean |

### Gates

| Gate | Result |
|---|---|
| `CApiCoverageMatrix` | 🔴 `validate_planned_row_owners` |
| `CApiLimitations` | 🔴 same error, inherited |
| `CApiReleaseGate` | 🔴 same error, via the `limitations-matrix` criterion |
| `CApiCompatibilityMatrix`, `CApiDocExportCounts`, `CApiRouteTestCoverage`, `CApiBoolContractCurrent` | 🟢 |

**The three red gates are one defect, not three.** Limitations and the release gate read the
coverage inventory; they fail because it fails. Route-test coverage is independently green at
4,055/4,055, and the six prose export counts agree with the measured 4,055.

---

## Why 9,499 became 12,733

`docs/c-api/COVERAGE.md` was last regenerated on **2026-09-01** (`7712534d3`). It is stale, and it
is stale in a completely ordinary way: the tree grew. The divergence reconciles **to the unit**.

| Module | old | new | Δ | Δ headers |
|---|---:|---:|---:|---:|
| **content-pipeline** | 0 | 2,573 | **+2,573** | +104 — *new module* |
| content | 1,267 | 1,528 | +261 | +8 |
| graphics | 2,642 | 2,821 | +179 | +2 |
| graphics-ext | 1,406 | 1,549 | +143 | +5 |
| **phone** | 0 | 61 | **+61** | +9 — *new module* |
| runtime | 302 | 313 | +11 | +2 |
| input | 864 | 874 | +10 | +0 |
| math | 928 | 938 | +10 | +1 |
| core | 157 | 143 | **−14** | +0 |
| audio, devices, devices-ext, gamer-services, media, net, storage | — | — | 0 | 0 |
| **TOTAL** | **9,499** | **12,733** | **+3,234** | **+131** |

`9,499 + 3,234 = 12,733` and `553 + 131 = 684`. Both exact.

- **81.4 % of the growth is two new modules** that did not exist at the snapshot:
  `modules/content-pipeline` (created 2026-09-03, `2ef9f1773`, explicitly "build-time-only") and
  `modules/phone` (created 2026-09-07, `bbd9ef0ef`).
- The remaining 600 symbols are ordinary API growth in six existing modules.
- `core` **shrank** by 14: `fc1b6a537` (2026-09-17, RRC-002/RRC-003) retired 25 renderer
  identities, removing 202 lines from three enum headers. That is the renderer curation showing up
  correctly in the inventory.

**Conclusions.** 9,499 was a correct measurement of a smaller tree. 12,733 is a correct measurement
of today's tree **given the scanner's current scope rules** — and two of those scope rules are
wrong (below). The scanner is not overcounting by overload or template expansion in any meaningful
way; it is counting a build-time module and a `detail/` directory it should not be counting.
Neither number is a lie; the stale one is simply 17 days old.

---

## Task ownership, complete

| Task | Plan | Rows | What those rows actually are |
|---|:--:|---:|---|
| `CBIND-044` | ✅ | 2,611 | 2,554 content-pipeline + 57 phone — **catch-all misrouting**, see below |
| `CBIND-117` | ⬜ | 874 | content-module Pipeline/Import + CNB Model-v2; correctly owned |
| `CBIND-035` | ✅ | 198 | **genuine** post-completion growth: storage textures, texture arrays, shader packages |
| `CBIND-080` | ✅ | 44 | **0 missing** — already bound; rule never extended |
| `CBIND-084` | ✅ | 38 | 31 genuine (engine-layer compute/diagnostics tail) |
| `CBIND-036` | ✅ | 32 | 32 genuine (content serializer attributes) |
| `CBIND-093` | ✅ | 20 | 4 bound, 12 genuine, **4 test peers** |
| `CBIND-037` | ✅ | 19 | 19 genuine (clipboard EXT, file-drop args) |
| `CBIND-034` | ✅ | 17 | 5 genuine; 8 copy-ctor/`operator=`; 4 POD `Dispose` |
| `CBIND-104` | ✅ | 15 | 1 genuine, 9 bound, **5 `*Internal` seams** |
| `CBIND-122` `CBIND-125` | ⬜ | 5 + 5 | correctly owned, awaiting disposition |
| `CBIND-108` `CBIND-114` | ✅ | 4 + 4 | genuine CNB/texture tail |
| `CBIND-120` `CBIND-121` | ⬜ | 3 + 1 | correctly owned, awaiting disposition |
| `CBIND-110` `CBIND-083` `CBIND-105` | ✅ | 2 + 2 + 1 | small genuine tail |

---

## CBIND-044 — 2,611 rows, and not one of them belongs to it

`CBIND-044` is *"Close the public API coverage matrix"*, **closed 2026-08-16**. It owns 2,611 rows
because it is the **fallthrough default** of `owner_task()`:

```python
if module in {"storage", "net", "content"}:      return "CBIND-036"
if module in {"runtime", "devices", …}:          return "CBIND-037"
return "CBIND-044"                                # ← everything else
```

`owner_task()` routes the Content Pipeline like this (line 827):

```python
if header.parts[1] == "content" and ("Pipeline" in header.parts or "Import" in header.parts):
    return "CBIND-117"
```

That guard is keyed on the module name **`content`**. On 2026-09-03 the pipeline was given its own
module, `modules/content-pipeline` — so `header.parts[1]` became `"content-pipeline"`, the guard
stopped matching, and 2,554 rows fell through to the catch-all. **The routing rule was never
followed through the module split.** It is a one-line defect with a four-figure blast radius, and it
is the single largest fact in this audit.

| CBIND-044's 2,611 rows | count |
|---|---:|
| content-pipeline, belongs to `CBIND-117` | 2,238 |
| content-pipeline, inside a lowercase `detail/` directory | 316 |
| `Microsoft::Phone`, belongs to no task at all | 57 |
| **Genuinely missing runtime bindings** | **0** |

**Not one row of CBIND-044's 2,611 is a missing runtime C binding.**

### The pipeline is not a backlog — it is an open question

`CBIND-117` is ⬜ and titled *"Design or disposition the experimental Content Pipeline C boundary"*:
decide **whether** C consumers need a build-time pipeline API at all. The architecture answers
strongly:

- `modules/content-pipeline/CMakeLists.txt` states the boundary is the point — *"a font rasterizer
  is needed only while content is being built… only `cna_content_compiler` links it, so the
  boundary is enforced by the dependency graph rather than by convention."*
- **`modules/c-api` does not link `cna_content_pipeline`.** A C route into it would not be in the
  shared library's link closure.
- The generator's own `B12_SLICE_OWNERS` comment already records the policy: *"recording those
  declarations does not authorize inventing C ABI routes during an integration-only task."*

So 2,988 rows (2,567 logical) are neither bound nor missing: they are a **build-time C++ surface
awaiting a recorded scope decision**. Binding them would roughly double the C ABI to expose an
offline asset compiler.

---

## CBIND-035 — a different failure, and a real one

`CBIND-035` fails for the opposite reason, which is why the two must not be summarised together.
Its 198 rows are **in-scope runtime graphics API that genuinely has no C route**, added after the
task closed and attributed to it by module (`graphics`, `math`, `graphics-ext` → `CBIND-035`).

| Verdict | rows |
|---|---:|
| Genuinely missing | **169** (153 logical) |
| Already bound; rule not extended | 23 |
| Value semantics / test seam | 6 |

Verified by direct export probe — these types have **zero** `cna_*` exports:

| Type | exports |
|---|---:|
| `CNA::Graphics::StorageTexture2D` | 0 |
| `CNA::Graphics::Texture2DArray` | 0 |
| `CNA::Graphics::ShaderPackageEXT` / `ShaderCodeEXT` / `ShaderDiagnosticEXT` | 0 |
| `Microsoft::Xna::Framework::Graphics::Texture3D` | 0 |
| `CNA::RendererLimit` (12 enum values) | 1 accessor only |

This is the honest core of the backlog. `CBIND-035` is recorded ✅ while owning 169 rows of work
nobody did — the exact state `CBIND-079`'s gate was built to catch, and it *is* catching it.

---

## The `PbrMaterial` ambiguity — root cause

`--approve-rule-symbols` aborts on:

```
Ambiguous explicit mappings for …/PbrMaterial.hpp|class|CNA::Graphics::PbrMaterial|:
  graphics-ext-settings-values, pbr-material-value
```

| | `graphics-ext-settings-values` | `pbr-material-value` |
|---|---|---|
| task | `CBIND-035F7` | `CBIND-087B` |
| `qualified_name_regex` | **`.*`** | `^CNA::Graphics::(?:PbrMaterial(?:::…)?…)$` |
| `header_regex` | `^…/(?:PbrMaterial\|RenderPipelineSettings)\.hpp$` | none |
| approved | 50 | 41 |

**The source-of-truth error is `graphics-ext-settings-values`'s `qualified_name_regex: ".*"`.** It
was written when those two headers contained only the frozen POD subset, and it claims *the entire
contents of both headers forever*. When `CBIND-087B` and `CBIND-088A` later bound the **remainder**
of both types, nobody narrowed the older rule to the scope it actually owns.

Two corroborating facts:

1. `render-pipeline-settings` (`CBIND-088A`) **tried** to stay disjoint — its regex carries a
   negative lookahead naming all 20 members `CBIND-035F7` had approved. That only excludes the
   older rule's symbols from the newer rule; it cannot stop `.*` from matching the newer rule's 77.
   Somebody saw the collision and patched the wrong side of it.
2. `pbr-material-value` (`CBIND-087B`) has no such lookahead, so it collides on all 53.

**This is not one stale rule pair. 34 rule pairs overlap at pattern level**, covering 296 symbols —
the media identity overrides against seven media types, the effect contracts against the shadow
receivers, every math compound-assignment rule against its operations rule, and so on. `PbrMaterial`
is merely the first the mapper reaches alphabetically.

**Nothing is actually mis-mapped today.** I verified that **zero** symbols are approved by more than
one rule: `approved_symbols` keeps every effective mapping unique, which is why `--check` is
unaffected and only `--approve-rule-symbols` (which ignores approval by design) is blocked.

The fix is **not** rule ordering. It is narrowing each older broad rule's pattern to the scope it
was actually reviewed for — the same correction `CBIND-088A` half-made. **Not done here:** it is 34
rules, not one, and fixing a single pair would only move the abort to the next pair while implying
the problem was solved.

---

## Two scanner scope defects

### `detail/` escapes the exclusion list — 316 rows

```python
EXCLUDED_PATH_SEGMENTS = ("Internal", "Detail")   # exact, case-sensitive
```

Every CNA `Detail/` directory is capitalised; the content-pipeline's are **lowercase `detail/`**.
316 symbols the project has itself marked internal are therefore in the public inventory — 267 of
them in one file, `Graphics/detail/PixelTraits.hpp`, whose per-pixel-format template
specializations collapse to **66 logical identities across the whole leak**. There are no lowercase
`internal/` directories, so this is the only instance.

### `Microsoft::Phone` — 57 rows, owned by nobody

`PUBLIC_ROOTS = ("Microsoft", "CNA")` admits `Microsoft/Phone/**`, which is **Windows Phone 7 API,
not XNA**. `owner_task()` has no rule for the module, so it lands on the catch-all. Whether a
Windows Phone application-lifecycle service belongs in a C ABI for an XNA runtime is a scope
question nobody has been asked.

### Test seams — 12 rows

`GraphicsDevice::…TestPeer` (4), `VertexBuffer::SetDataAtInternal` (5), `Effect::CompiledEffectTestAccess`,
`Effect::ThrowIfDisposedForCloneInternal`, `IndexBuffer::SetDataBytesAtInternal`. `load_rules`'
own docstring says a friendship declaration should be an explicit `not-applicable` rule; these have
none, so they are counted as unbound public API.

---

## Genuinely missing bindings — 388 logical APIs

| Complexity class | rows | logical |
|---|---:|---:|
| A. enum / identity exposure | 108 | 108 |
| C. POD field / property forwarding | 71 | 71 |
| K. other method (needs a C shape decision) | 67 | 57 |
| 0. trivial scalar / property forwarding | 61 | 53 |
| D. new handle or POD type design | 29 | 29 |
| E. lifetime / ownership route | 27 | 25 |
| F. named value operation | 22 | 16 |
| G. collection (count/copy protocol) | 17 | 16 |
| I. template-heavy (C shape decision) | 8 | 6 |
| B. constant exposure | 5 | 5 |
| H. ownership-sensitive pointer | 3 | 3 |
| J. callback / event registration | 2 | 2 |
| **TOTAL** | **420** | **388** |

By module: content 172, graphics-ext 115, graphics 80, runtime 11, input 8, math 2.

**Effort shape.** 237 of 388 (61 %) are the cheap classes — enums, POD fields, trivial forwarding,
constants. These are mechanical, and CNA has done runs of this size before (`CBIND-108` bound 67
rows in one slice). The expensive residue is ~100 APIs needing a C shape decision (class K, new
handles, collections, templates) concentrated in three coherent areas: **storage textures /
texture arrays / Texture3D**, the **shader-package EXT surface**, and **CNB Model-v2**.

This is a **medium campaign — on the order of one to three focused weeks** — not the multi-month
undertaking 3,007 implies. Excluded from that estimate: the 2,988 pipeline rows, which are a scope
decision rather than an effort, and would be a far larger undertaking if answered "yes".

---

## Is the tooling trustworthy?

**1. Does the generator measure what it claims?** *Partly.* It measures "public C++ declarations
without an approved mapping rule" accurately and its arithmetic is exact. It does **not** measure
"missing C API functionality", which is how its output is being read. Its scope rules also admit
three things they should not: a build-time-only module, lowercase `detail/`, and `Microsoft::Phone`.

**2. Is the percentage meaningful?** *No.* A coverage figure computed over a denominator that
includes an offline asset compiler the C API does not link is not a C API coverage figure.

**3. Are task ownership rows reliable?** *No.* 3,007 of 3,895 planned rows name a task recorded as
complete. `CBIND-044` — a task closed 2026-08-16 — owns 2,611 rows of which **zero** are its work.

**4. Can `COVERAGE.md` be trusted?** *No — it is 17 days stale* (2026-09-01). Its numbers were
correct when written and reconcile exactly to today's, but it is published as current state and is
not. Regenerating it **now would make it worse**: it would publish the `detail/` leak and the
misrouted pipeline as reviewed fact.

**5. Can the limitations gate be a release gate today?** *No.* It cannot run. It is red for a real
reason, and the release gate correctly refuses to call itself ready.

**6. What is the honest summary?** The gate is **red for a true reason, reported against the wrong
task, at roughly ten times the real magnitude.** `CBIND-079`'s invariant — *a finished task may not
own unfinished work* — is doing exactly its job: it caught a genuine defect. What it cannot do is
say which kind of defect, and the answer this time is mostly "routing", not "unwritten code".

---

## Minimum repairs to make the tooling trustworthy

Ordered by ratio of truth restored to risk taken. **None were made on this branch** — each changes
published coverage figures, and two need an owner's scope decision first.

| # | Repair | Effect | Risk |
|---|---|---|---|
| 1 | Key the Pipeline guard in `owner_task()` on the module set `{"content", "content-pipeline"}` instead of `"content"` | moves 2,238 rows from ✅ `CBIND-044` to ⬜ `CBIND-117` | none — one line, restores intended routing |
| 2 | Make `EXCLUDED_PATH_SEGMENTS` case-insensitive (or add `"detail"`) | removes 316 artifact rows | none — matches the rule's stated intent |
| 3 | Add explicit `not-applicable` rules for the 12 test-seam declarations | removes 12 rows | none — `load_rules` already documents this shape |
| 4 | Give `Microsoft::Phone` an owner, or exclude it | resolves 57 rows | **needs a scope decision** |
| 5 | Record the pipeline disposition in `CBIND-117` | resolves 2,988 rows | **needs the owner's decision** |
| 6 | Reopen `CBIND-035` (or open a successor) for its 169 genuine rows | makes the plan honest | none — bookkeeping |
| 7 | Narrow the 34 broad rule patterns to their approved scope | unblocks `--approve-rule-symbols` | low, mechanical, but 34 rules |
| 8 | Regenerate `COVERAGE.md` — **only after 1–3** | publishes a defensible snapshot | must not precede 1–3 |

After 1–3 and 6, the gate would report roughly **420 planned rows against real open tasks** instead
of 3,007 against closed ones — red, smaller, and true.

---

## Options — costs and consequences

Stated as facts. **The product decision is the maintainer's.**

**A. Fix the tooling only.** Repairs 1–3, 6–8. Cost ~1–2 days. The backlog becomes ~420 rows
honestly owned; no C API changes. Leaves 388 real APIs unbound and the pipeline question open.

**B. Fix tooling, then bind a bounded subset.** A, then the 237 cheap APIs (enums, POD fields,
trivial forwarding). Cost ~1 week beyond A. Grows the ABI by a few hundred exports. Leaves ~150
design-bearing APIs.

**C. Full runtime coverage.** A + all 388, including three new type families. Cost ~2–4 weeks.
Requires new ABI structures and handle kinds; `TextureCube::GetData/SetData`, storage textures and
texture arrays are real gaps a 3D C consumer would hit. Does **not** include the pipeline.

**D. Leave scope deliberately incomplete.** Do A, record the pipeline and `Microsoft::Phone` as
out of scope, publish the 388 as known limitations. Cost ~2 days. Honest and cheap; the C API stays
a runtime API, which is what its link closure already says it is.

Not a real option: binding the 2,988 pipeline rows. It would roughly double the ABI to expose an
offline asset compiler that the C API does not link and that no C consumer has asked for.

---

## Verification

- **ABI unchanged:** 0.29.0, 4,055 exports, 221 structs.
- **Renderers unchanged:** 25 identities / 21 families / 26 retired / next free 52.
- **No executable behaviour changed.** No public C++ API, no C ABI, no renderer API, no binding
  implementation, no coverage rule, no generator logic. The only additions are this document, the
  audit plan, and one read-only script that no gate depends on.
- **Gates re-run after the additions:** unchanged — the same three red, the same four green.

### Remaining uncertainties

- The export probe is a heuristic. It is calibrated against `CBIND-080`, where the plan
  independently records "no new C code was needed" and the probe independently returns zero missing;
  hand-verification of ~60 further verdicts found no residual error. Expect a small error bar on
  388, not a different order of magnitude.
- The 2,567 pipeline logical APIs are counted, not designed. If the owner ever rules "bind it", that
  count is a scope measurement, not an effort estimate.
- `CBIND-117`'s plan row declares 654 rows; it owns 874 and would own 3,112 after repair 1. The
  declared figure predates the module split.
