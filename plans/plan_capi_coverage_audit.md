# Plan — C API coverage backlog audit

**Branch:** `capi-coverage-audit` · **Baseline SHA:** `23801d2baacec3debc9e9e2a26dbc3c273b1f459`
(the `next` merge commit containing `e30408dfc`, the completed C API smoke/lifetime repair).

This is an **audit and evidence branch**. Its question is not "how do we bind the backlog" but
"is there a backlog". No C bindings are implemented here, no public C++ API changes, no ABI change.

The decision this audit exists to inform: the coverage tooling reports ~3,000 planned rows owned by
tasks the plan records as finished. Before anyone spends weeks writing C bindings, establish how
many of those rows are missing *functionality* rather than missing *bookkeeping*.

---

## Phase 0 — Baseline (done)

Everything below was measured on this branch at the baseline SHA, with a clean tree.

| Fact | Measured | Command |
|---|---|---|
| Public C++ headers scanned | **684** | `generate_coverage_inventory.py` |
| Explicitly excluded internal/detail headers | **159** | same |
| Public C++ symbols | **12,733** | same |
| implemented / partial / planned / not-applicable | **8,323 / 15 / 3,895 / 500** | same |
| Planned rows owned by tasks marked ✅ complete | **3,007** across **14** tasks | `--check` failure list |
| Coverage mapping rules | **684** | `coverage_mappings.json` |
| ABI version | **0.29.0** (encoded 7424) | `abi_baseline.json` |
| Exported `cna_*` routes | **4,055** | `abi_baseline.json`, `check_doc_export_counts.py` |
| ABI structs | **221** | `abi_baseline.json` |
| Renderer identities | 25 public / 21 families / 26 retired / next free 52 | `scripts/check_renderer_identities.py` |
| C API build (`cna_c_api`, HEADLESS debug) | builds clean | `cmake --build cmake-build-debug` |

### Gate status at baseline

| Gate | Result | Cause |
|---|---|---|
| `CApiCoverageMatrix` | 🔴 exit 2 | `validate_planned_row_owners` |
| `CApiLimitations` | 🔴 exit 2 | same, inherited from the inventory |
| `CApiReleaseGate` | 🔴 exit 1 | same, inherited via the `limitations-matrix` criterion |
| `CApiCompatibilityMatrix` | 🟢 | — |
| `CApiDocExportCounts` | 🟢 | 6 prose counts agree with measured 4,055 |
| `CApiRouteTestCoverage` | 🟢 | 4,055/4,055 routes named by a test |
| `CApiBoolContractCurrent` | 🟢 | — |

**All three red gates share one root cause.** Limitations and the release gate are downstream
readers of the coverage inventory; there is one defect here, not three.

`--approve-rule-symbols` is separately blocked: it runs the mapper with approval ignored, and
**34 rule pairs** overlap at pattern level. It aborts on the first, `CNA::Graphics::PbrMaterial`.

---

## The distinction this audit must hold

> An **unmapped symbol** is not a **missing C binding**.

`planned` means "no rule claims this symbol". A rule is written and approved by a person. A symbol
can therefore be `planned` while a perfectly good C route exists, simply because nobody wrote the
rule — and the whole `approved_symbols` mechanism (`CBIND-050`) exists precisely because the
project decided broad rules must *not* silently inherit new symbols. That design is sound, but it
guarantees that inventory growth shows up as `planned` by default.

Every count below is therefore reported twice: as raw rows, and as unique logical APIs after the
semantic question is answered.

---

## Workstreams

| # | Workstream | Output |
|---|---|---|
| A | Coverage model and data flow | documented pipeline, scanner/rule/ownership semantics |
| B | Reconcile 9,499 (COVERAGE.md) vs 12,733 (scanner) | exact per-module delta, cause of each |
| C | Enumerate every contributing task group | ownership table |
| D | Deep audit `CBIND-044` (2,611 rows) | classification + root cause |
| E | Deep audit `CBIND-035` (198 rows) | classification + root cause |
| F | Remaining 12 task groups | per-task cause |
| G | `PbrMaterial` rule ambiguity | root cause, source-of-truth error named |
| H | Semantic classification H1–H7 | every planned row partitioned |
| I | Machine-readable C++ vs C inventory comparison | deterministic audit scripts |
| J | Sample verification | manual checks against automated buckets |
| K | True size of missing C API work | honest counts |
| L | Effort estimate by complexity class | counts per class |
| M | Is the coverage gate trustworthy | explicit verdict + minimum repairs |

## Allowed changes on this branch

Permitted: this plan and the audit report; narrowly scoped tooling fixes needed to *measure*
correctly; correction of unambiguous stale ownership metadata; deterministic audit scripts where
they earn their place; regenerated documentation **only after** its source-of-truth problem is
understood and named.

Forbidden: implementing bindings, changing public C++ API, changing the C ABI, changing renderer
identities or IDs, touching sibling repositories, pushing, and making a gate green by weakening it.
A green but meaningless coverage gate is worse than a red honest one.

## Definition of done

The audit is done when every number above is reproduced from the tree, the 9,499/12,733 divergence
is explained rather than regenerated away, all 3,007 rows are partitioned into meaningful
categories, genuinely missing bindings are counted as unique logical APIs, the `PbrMaterial`
ambiguity is root-caused to a source-of-truth error, and the trustworthiness of `plan_binding.md`,
`COVERAGE.md` and the limitations generator is stated without softening.

The findings are recorded in [`docs/c-api/COVERAGE_AUDIT.md`](../docs/c-api/COVERAGE_AUDIT.md).
