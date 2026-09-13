# BATCH_5_STABILIZATION.md — Glide + GDI + HTML DOM · 2026-08-08 · `debian`

> ## CHECKPOINT RETAKE — **READY; LOCAL SIGNED TAG TAKEN**
>
> The required Content safety retake completed later on 2026-08-08. `REMED-CONTENT-007` and
> `REMED-CONTENT-008` are DONE, the bounded same-pattern finding `REMED-CONTENT-011` is also DONE,
> and the final integration target is `c805fd737f4321568fba378e8d1b8fe5b5270666`. Signed annotated
> tag `integration/checkpoint-batch5-20260808` is tag object
> `307c9ad511015c64ce55184cdf0d5ebd7b1cb575`, peels to that target, and verifies Good. Nothing was
> pushed and no nineteenth lane began. Section 7 is the authoritative retake; §§1–6 preserve the
> first, technically-green-but-checkpoint-BLOCKED decision as historical evidence.

**Historical first decision:** Batch 5 was complete: Glide, GDI, and HTML DOM were all ACCEPTED.
Technical stabilization passed, but the checkpoint decision was BLOCKED by the pre-existing mandatory
`REMED-CONTENT-007/-008` gate. No Batch 5 tag was created, nothing was pushed, and no nineteenth
lane began.** Full lane records: `integration/lanes/glide.md`, `integration/lanes/gdi.md`, and
`integration/lanes/html-dom.md`.

## 1. Membership and history

| Lane | Accepted history |
|---|---|
| Glide | original `2f9b47e1`; adaptation `e891e105`; signed merge `677f4c59`; genuine 32-bit native Glide ABI identity |
| GDI | original `adc9cc2a`; adaptation `625f4ad5`; signed merge `ba5fa601`; private CPU Software-2D core plus classic Win32 GDI presentation |
| HTML DOM | original `8e4e4293`; adaptation `a32977f3`; signed merge `24bf4786`; real Emscripten DOM/CSS sprite surface plus private Canvas2D render targets |

Integration moved `ba5fa60166bef2214a4c08b64d50570d1120b7b9` →
`24bf4786af1ff6b1cf86640e85a22f76c7315818`. The first-parent history now contains exactly
**18 logical lane merges** from the phase-1 checkpoint; the inventory is **18/21 integrated,
3 pending** (`direct2d`, `llgl`, `metal`). No pending lane was replayed, adapted, merged, or
otherwise begun.

Each original branch remains unchanged and each sole archive tag still peels to its original head.
All recreated HTML DOM commits and all three Batch 5 merge/adaptation heads verify with Good GPG
signatures. The HTML DOM merge is a signed two-parent `--no-ff` merge and its tree is byte-identical
to `adapt/html-dom`.

## 2. Stabilization gates

| Gate | Result |
|---|---|
| Glide accepted state | intact; unit/capability suites green, i686 fake-DLL ABI loader builds and exits 0 under Wine; production runtime remains truthfully unavailable without Voodoo hardware or an external runtime |
| GDI accepted state | intact; seven focused current MinGW/Wine/Xvfb controls exit 0, including public stencil and exact 4x MSAA |
| HTML DOM accepted state | green within the truthful boundary: 57/57 current host contracts; historical real-browser matrix retained; current adapted browser run unavailable because Emscripten/Node are absent |
| Shared Glide/GDI continuity | capability and interface compile/runtime controls remain green; no accepted CPU Software-2D, GDI presentation, or Glide ABI behavior was changed |
| OPENGLES/EasyGL principal continuity | 110 selected tests across 17 suites: **109 pass + 1 intentional WireFrame-capability skip** under Xvfb |
| `REMED-GFX-223` | green: cache-isolation 2/2 plus Texture2D cache controls 8/8; HTML DOM changes no shared Texture2D authority code |
| Sanitizers | HTML DOM host contracts **57/57** with linked ASan and UBSan runtimes, leak detection enabled, zero CNA-originating reports |
| Other required all-interface controls | Diligent, Skia, and Sokol changed capability translation units compile with their backend-specific definitions; Glide/GDI controls above pass |
| Findings | HTMLDOM-121/-122/-123 resolved; no supported-path lane defect open; existing findings retain their recorded identities and states |
| Provenance | 49 chronological HTML replays account for all meaningful history; six Canvas-only commits and one Canvas-only mixed hunk are explicitly omitted; scoped historical HTML tree equivalence exact; signatures/attribution/trailers clean |
| History/count | exactly 18 lane merges; Batch 5 exactly 3/3; no nineteenth lane begun |
| Repository safety | `audit/` tree unchanged; four exact stash objects retained; `git diff --check` clean; relevant worktrees clean except the preserved historical SDL checkout-pointer mismatch |

The one failed EasyGL attempt used `SDL_VIDEODRIVER=dummy`, which cannot create OpenGL; an immediate
Xvfb run passed and is the accepted control. A broad Diligent dependency build was stopped by
signalling only its exact session-owned Ninja process at 217/904 and replaced by the sufficient
focused changed-source compile probe. Neither supplied acceptance evidence and neither altered an
unrelated process.

## 3. HTML DOM runtime classification

The implementation is a genuine Emscripten/browser backend, not a host-native renderer. This host
has Chrome 151 and Xvfb, but no `emcc`, `em++`, `emcmake`, Node, npm, or npx. Consequently:

- current adapted CNA-owned C++ validation, deterministic platform rejection, ASan/UBSan, and
  cross-backend controls are **executed and green**;
- the original lane's smoke 69/69, pixel 35/35, stress 10/10, dispose 17/17, host integration 2/2,
  memory 6/6, and GTest 54/54 remain **historical real-browser evidence**; and
- no current browser rebuild/runtime pass is claimed.

This is an environment coverage limitation, not an unresolved supported-path defect. No SDK was
downloaded and no new browser-automation stack was invented.

## 4. Residuals

Lane-local HTML DOM supported paths are green. Carried residuals remain explicit:

- `REMED-GFX-224` — MEDIUM/OPEN, EasyGL-only render-target `SetData`; unchanged;
- `REMED-CORE-015` and `REMED-CONTENT-010` — LOW/OPEN; unchanged;
- browser DPR>1 was not independently tested; layout uses SDL canvas CSS dimensions and historical
  evidence covers the recorded browser configuration;
- physical Windows/MSVC GDI validation and physical Glide hardware/runtime remain external; and
- browser/JS lifetime absence is supported by the historical dispose/memory/churn tests, not by
  native ASan alone.

None is a supported-path defect introduced by HTML DOM or a Batch 5 technical-stabilization failure.

## 5. Compilation, storage, and process bounds

Every compile invocation used an explicit numeric bound of at most four jobs. The only nested SDL
bootstrap helper was inspected and invokes `--parallel 2`; no bare argument-less parallel mode ran.
The session-wide maximum is therefore **4**, `-j8` was never reached, and the required ≤8 ceiling
holds. Persistent lane-specific build trees live under `build-probe`; the
configure-time SDL bootstrap output was moved out of the source worktree into the same persistent
storage area rather than deleted.

Per the owner's explicit session override, temperature was never monitored after that instruction
and no start/peak/final Package value exists. Power mode was not changed. No global process signal,
process-name match, `pkill`, or `killall` was used.

## 6. Checkpoint decision

Technical Batch 5 stabilization passes, but `INTEGRATION_ORDER.md` states:

> `REMED-CONTENT-007` / `-008` must be closed before the Batch-5 checkpoint.

Both findings are still HIGH/P1 and OPEN. They cover missing root-containment enforcement for
Song/Video XNB-relative paths and raw manifest-supplied path joins in `ContentManager`. HTML DOM
touches neither implementation, and the session explicitly excludes unrelated remediation, so
silently repairing them here would violate scope.

**Decision: BLOCKED.** No conflicting Batch 5 tag existed and
`integration/checkpoint-batch5-20260808` was deliberately not created. The three accepted lane
merges remain valid; only the checkpoint is withheld.

**Exactly one bounded next task (not begun):** close `REMED-CONTENT-007` and
`REMED-CONTENT-008` together as the existing path-containment safety task, then retake the Batch 5
checkpoint decision. Do not begin Direct2D, LLGL, Metal, or any other lane as part of that task.

## 7. Required checkpoint retake — READY

### 7.1 Blocker closure and integration target

Both mandatory HIGH/P1 findings were reproduced before production changed and are now DONE.
`REMED-CONTENT-007` covers the independent Song and Video XNB media-reference callers;
`REMED-CONTENT-008` covers the recorded ContentManager manifest fields. The bounded same-pattern
audit created and closed `REMED-CONTENT-011` for the omitted Model/SkinnedModel and indirect asset
fields. Full containment contract, lexical/existing-symlink boundary, controlled oracles, and A–D
audit classification are in `remediation/REMEDIATION_PROGRESS.md`.

Signed test/fix commits `569beedd`/`062ca70c` on `feature/audit` were cherry-picked with signatures
as `2d795473`/`c805fd73` on `integration/post-audit-phase1`. Integration therefore moved
`24bf4786af1ff6b1cf86640e85a22f76c7315818` →
`c805fd737f4321568fba378e8d1b8fe5b5270666`. The delta contains only the shared containment header,
Song/Video readers, ContentManager, and their two test files—no Graphics, backend, lane, or
`audit/` path.

### 7.2 Retake controls

| Gate | Retake result |
|---|---|
| Content containment focused | **46/46** on final integration HEAD: 28 primitive + 18 public-caller tests |
| Relevant Content/Song/Video shard | **116/116 across 18 suites**, including valid normalized paths, all affected ContentManager routes, independent Song/Video XNB loads, cache recovery, explicit external bundles, and shared clips |
| Sanitizers | Both binaries link `libasan.so.8` and `libubsan.so.1`; focused and broad shards run with ASan + UBSan + LeakSanitizer enabled and report zero CNA-originating finding |
| Cross-platform path syntax | portable tests pass for POSIX absolute, Windows drive/root/UNC, mixed and repeated separators, root equality, sibling prefix, deep traversal, ordinary `..` filename text, and existing-symlink escape; no Windows filesystem API is used, so no invented MinGW/Wine filesystem claim |
| Glide continuity | exact portable unit/capability slice rerun **78/78** under the final sanitizer binary; accepted runtime/ABI boundary from §2 remains unchanged |
| HTML DOM continuity | exact native host-contract target rebuilt from final sources and rerun **57/57** with ASan + UBSan + LeakSanitizer; browser boundary remains as §3 records |
| GDI continuity | accepted 19/19 Wine/Xvfb record remains authoritative; the retake changes no Graphics/backend/build path and does not reopen the accepted lane |
| Provenance/signatures | Glide `677f4c59`, GDI `ba5fa601`, HTML DOM `24bf4786`, both integration Content commits, and all planning Content/docs commits verify Good under fingerprint `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F` |
| History/count | exactly **18 first-parent lane merges**, **18/21 integrated**, 3 deferred pending (`direct2d`, `llgl`, `metal`); no `adapt/` ref or merge for any pending lane |
| Repository safety | four stash objects remain byte-identical by object ID; `audit/` tree remains `168c9b668763b78e63106e27d942a76d2457f41d`; `git diff --check` clean; integration worktree clean |

The planning worktree is tracked-clean after its signed commits. An externally created untracked
`AGENTS.md` appeared during the build, was not present at the formal start gate, and was preserved
without staging, editing, deleting, restoring, resetting, or stashing it. No tracked foreign change
occurred.

### 7.3 Build/process bounds

The persistent sanitizer trees are
`/rv/data/development/github.com/openeggbert/cnaaudit/build-asan` and
`/rv/data/development/github.com/openeggbert/cnaintegration/build-asan`; both are HEADLESS Debug,
ccache-enabled, combined ASan/UBSan builds. Every build had a numeric bound of
`--parallel 3`; session maximum compilation parallelism is **3 ≤ 8**. The clean integration build
exposed an unrelated pre-existing circular static-library order in one audio subprocess harness;
only generated build rules were reconfigured to repeat static libraries, no tracked file changed,
and the complete `CnaTests` target then linked. No global signal or process-name match was used.

### 7.4 Final decision and tag

Glide, GDI, and HTML DOM remain ACCEPTED; technical stabilization remains PASS; both mandatory
Content blockers and the same-pattern follow-up are DONE; no other mandatory Batch 5 blocker is
open. The inventory remains 18/21 and no nineteenth lane began.

**Decision: READY.** With no conflicting tag present, local signed annotated tag
`integration/checkpoint-batch5-20260808` was created with message
`CNA integration Batch 5 checkpoint`. Tag object
`307c9ad511015c64ce55184cdf0d5ebd7b1cb575` peels to
`c805fd737f4321568fba378e8d1b8fe5b5270666`; `git tag -v` reports a Good signature from the key
above. Nothing was pushed. Batch 6 / Group G remains deferred: the next lane listed by the
authoritative order is **Direct2D**, but it is not scheduled and requires the recorded owner
decision; it was not begun here.
