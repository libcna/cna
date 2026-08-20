# Lane card — `gl` · **CROSS-REPOSITORY**

> **Post-campaign external-history addendum (2026-08-09).** The owner subsequently completed and
> pushed the authorized MetaGL/EasyGL public-history rewrite. Current public authority is MetaGL
> `develop` `571d3a62fe166b9781ac6193d137b12ff3757620` (tree
> `a7771c5593a4ec4b71283d38523a0cde3fbf6d4b`) and EasyGL `develop`
> `0b46d35c394a9fb6aea6a85c6587894b5013da33` (tree
> `e89ff546d3782e2b32e02f4b9dc56da42c4c463a`). Those trees equal the content accepted during
> Batch 4. The external archive tags were also rewritten and are now unsigned targets with legacy
> annotations; old external SHAs and signature/tag claims below remain dated evidence of the
> integration event, not current dependency pins or current-ref assertions. No further external
> rewrite is planned. CNA's own 21 original refs/archive tags and all checkpoint history are
> unchanged. See `integration/FINAL_RECONCILIATION.md`.
>

| Field | Value |
|---|---|
| Logical lane | `gl` |
| Refs | `refs/heads/feature/gl` and `refs/remotes/origin/feature/gl` — **identical** |
| Head | `f8efb9b46f7b0d516eaf150e7e8e93f2bd74e795` |
| Archive tag | **`archive/preintegration/gl-20260804`** → `f8efb9b4` · annotated · GPG-signed · verifies good · local only |
| Merge base with checkpoint | `ac3aaaeb` (= `origin/develop`) — **develop-forked** |
| Own commits / files | **28 / 51** |
| Ahead / behind `develop` | 28 / 0 |
| Tip subject | `docs(Task GLB-40): confirm final 236/241 count from full-suite run` |
| Subsystem | GL-family public backends (OPENGLES / OPENGL33 desktop) via EasyGL |
| Shared interfaces | `GraphicsDevice.cpp`, `IGraphicsBackend.hpp` — **not** `GraphicsCapability.hpp` |
| Plan file / namespace | `plans/plan_glbackends.md` / `GLB-*` |
| Conflict class | **CROSS-REPOSITORY** (MEDIUM within CNA) |
| Development status | DEVELOPMENT COMPLETE |
| Integration readiness | **INTEGRATED 2026-08-07 — merge `0a51f8647`, Batch 4 checkpoint taken** |

## The three repositories

**MetaGL and EasyGL implementation development for this lane is complete.** Neither project needs
further feature implementation here. What is outstanding is that their completed histories have not
been adapted into their `develop` branches, so CNA `feature/gl` builds against non-`develop`
revisions.

| Repository | Path | `develop` | Completed branch | Head | Ahead | Behind | Merged? |
|---|---|---|---|---|---|---|---|
| **MetaGL** | `.../meta-gl` (+ linked worktree `.../meta-gl-followup-audit`) | `d51fcd7f` | **`feature/followup-audit`** | **`d5bc155f`** | **16** | 0 | **NO** |
| **EasyGL** | `.../easy-gl` (+ linked worktree `.../easy-glrvc`) | `62c0a248` | **`rvc`** | **`b52f671379c0fe6d71d8c091ee4334c348beec8e`** | **5** | 0 | **NO** |
| **CNA** | `.../cnaaudit` | — | `feature/gl` | `f8efb9b4` | 28 | 0 | **NO** |

Both were fetched this session: `git fetch --all --prune --tags`, exit 0, **no ref changed** in
either. All four heads are unchanged from the previous snapshot.

**Exact branch names matter.** EasyGL's is `rvc` — no `feature/` prefix. MetaGL's is
`feature/followup-audit`, checked out at the sibling worktree path `meta-gl-followup-audit`.

## Dependency chain — read from the build files, not assumed

`cmake/BackendSelection.cmake` at `f8efb9b4`:

- line 157 — `if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easy-glrvc/CMakeLists.txt")` → a hard
  `FATAL_ERROR` naming *"a separate git checkout (branch `rvc` of easy-gl)"*
- line 167 — `add_subdirectory(../easy-glrvc easy-gl)`
- lines 134–136 — marked `GLB-7 TEMPORARY`, naming **`GLB-38`** as the task that switches back to
  `../easy-gl`

### Where the MetaGL redirect actually lives

`easy-glrvc`'s **committed** `CMakeLists.txt` line 17 reads `add_subdirectory(../meta-gl meta-gl)` —
MetaGL **`develop`**, identical to what `easy-gl` `develop` uses. The redirect to the follow-up audit
branch exists **only as an uncommitted working-tree modification**:

```diff
-add_subdirectory(../meta-gl meta-gl)
+#add_subdirectory(../meta-gl meta-gl)
+add_subdirectory(../meta-gl-followup-audit meta-gl)
```

So the *committed* `rvc` branch already targets MetaGL `develop`; only the **working configuration
currently in use** builds against `meta-gl-followup-audit`. The MetaGL adaptation is still required,
because that working configuration is what `feature/gl` is actually built against today.

## Mandatory integration sequence

1. Preserve the original MetaGL completed head — **DONE**:
   `archive/preintegration/metagl-followup-audit-20260804` → `d5bc155f`, signed, verifies good.
2. Adapt MetaGL `feature/followup-audit` (16 commits) into MetaGL `develop`. **Owner-only.**
3. Validate MetaGL `develop`.
4. Preserve the original EasyGL completed head — **NOT DONE**, see the blocker below.
5. Adapt EasyGL `rvc` (5 commits) into EasyGL `develop`. **Owner-only.** Resolve the uncommitted
   `CMakeLists.txt` redirect first.
6. Validate EasyGL `develop` against the new MetaGL `develop`.
7. Update CNA `feature/gl` to those `develop` revisions — **`GLB-38`**: repoint
   `cmake/BackendSelection.cmake` from `../easy-glrvc` to `../easy-gl`. **Owner-only.**
8. Build and test CNA `feature/gl`.
9. Integrate CNA `feature/gl` into `integration/post-audit-phase1`.

**None of these merges may be performed autonomously.** `plans/plan_glbackends.md` records the same
constraint independently: *"`GLB-38` … Decided: leave to the project owner — do not attempt to
merge/push between repos autonomously."*

## Open blocker — the one provenance gap in the campaign

**EasyGL `rvc` @ `b52f671379c0fe6d71d8c091ee4334c348beec8e` has no archive tag.**

Phase 2's precondition for the external repositories is *"only where the completed head is
unambiguous **and the worktree is clean**"*. The head is unambiguous; the `easy-glrvc` worktree is
not clean — it carries the ` M CMakeLists.txt` redirect above.

The precondition was respected rather than reinterpreted. **This is a deliberate omission, not an
oversight.** Step 5 must not begin before it is closed. It is closed by one owner decision — discard
or commit the redirect — after which the tag takes seconds.

## History-cleanup classification

| Repository | Class | Detail |
|---|---|---|
| **CNA `feature/gl`** | **MESSAGE CLEANUP REQUIRED** | 28/28 Robert-authored, committed and GPG-signed. But five commit **bodies** carry process narrative: *"decision, not autonomous agent action"*, *"found another agent already implemented all 6 webgl.md findings directly in easy-glrvc (commit 14109db, co-authored by…)"*, *"reconcile with other-agent commits"*. Not trailers, not authorship claims — prose about how the work was produced. It still must not survive into the final history |
| **MetaGL `feature/followup-audit`** | **HISTORY CLEAN** | 16/16 Robert-authored and GPG-signed (all `U`). No attribution text. **No cleanup required** |
| **EasyGL `rvc`** | **HISTORY CLEAN** | 5/5 Robert-authored and GPG-signed (all `U`). No attribution text. **No cleanup required** |

Rewrite the five CNA bodies to keep their technical content — which GL findings landed where, and
that `GLB-38` remains open — while dropping the process narration.

## Adaptation strategy

- `GpuDrawParams`: **cost applies.** `feature/gl` forks from `develop` and does not contain
  `fc0dd2a2`.
- Post-audit backend obligations apply: `RequireFaithfulDeclarationEXT` at draw time, truthful
  `WireFrame`, header-only helper. Note that **EasyGL under-reports its own working wireframe**
  (`REMED-GFX-219`) — resolve that against the EasyGL backend's measured behaviour rather than
  copying another backend's answer.
- Only after steps 1–8 does the CNA-side adaptation begin.

## Test matrix

| Check | Expected |
|---|---|
| MetaGL `develop` standalone | validated after step 2 |
| EasyGL `develop` against new MetaGL `develop` | validated after step 5 |
| `GLB-38` repoint | a CNA configure that no longer references `../easy-glrvc` anywhere |
| CNA `feature/gl` build | clean for each of the four GL-family profiles |
| Suite | the branch's own recorded **236/241** full-suite result reproduced, or any difference explained |
| Declaration guard / `WireFrame` | present and **measured**; `REMED-GFX-219` addressed for EasyGL |
| `range-diff` per repository | attached for all three |
| Attribution sweep | zero hits across all three ranges |

## Completion criteria

1. EasyGL `rvc` archive tag created (blocker closed).
2. MetaGL `develop` and EasyGL `develop` carry the completed work; both validated.
3. `GLB-38` done — no `../easy-glrvc` reference remains.
4. CNA `feature/gl` builds and its suite reproduces the recorded result.
5. Five commit bodies rewritten; authorship and signatures preserved.
6. `range-diff` attached for each repository; no original branch rewritten anywhere.
7. Zero attribution hits.

---

## INTEGRATED 2026-08-07 — Batch 4 · merge `0a51f8647` · checkpoint `integration/checkpoint-batch4-20260807`

**All nine §7.4 steps executed in order, under direct project-owner instruction of 2026-08-07**
(discharging the plans/plan_glbackends.md owner-only constraint for this session; nothing was pushed).

### The external repositories — and the classification this lane corrects

**The HISTORY CLEAN classification above was wrong at the object level if read as whole-history
cleanliness.** 15 of MetaGL's 16 completed commits and 5 of EasyGL's 5 carry a
`Co-authored-by: Junie <junie@jetbrains.com>` AI-agent trailer the inventory's "no attribution
text" row missed — and the banned-token sweep this card itself requires hits the generic
`Co-authored-by` alternation. Additionally, the already-published `origin/develop` ancestries of
both repositories carry historical `Co-Authored-By: Claude *` trailers (95 banned-token lines
across MetaGL's 119 published commits, 55 across EasyGL's 80), so whole-ancestry cleanliness is
unattainable without rewriting published history.

**Owner decisions (2026-08-07):** the attribution-clean requirement is range-scoped to the newly
integrated MetaGL/EasyGL ranges; published ancestry is deliberately not rewritten; a global
history rewrite of both repositories is a separate owner-controlled post-integration operation,
not part of this lane. Both develops therefore carry the completed work as trailer-stripped
replays — authorship, author dates, subjects and technical bodies preserved; only the AI trailer
removed; every replayed commit GPG-signed; trees byte-identical to the as-authored heads:

| Repo | develop before | develop after | Tree identity | As-authored head preserved on |
|---|---|---|---|---|
| MetaGL | `d51fcd7f` | **`c964e736`** (16: `586972f` kept as the original object + 15 replays) | == `d5bc155f` | `feature/followup-audit`, `origin/feature/followup-audit`, archive tag |
| EasyGL | `62c0a248` | **`9b831dee`** (5 replays, adapt/rvc) | == `b52f6713` | `rvc`, `origin/rvc`, `archive/preintegration/easygl-rvc-20260807` (created this session, signed) |

Both moves used `git branch -f develop <verified-adapt-tip>` under explicit owner approval after
proving: live `origin/develop` still at the pre-integration tips, the ff'd tips never pushed, the
clean tips descend from published develop (a future push stays fast-forward), and originals
preserved. Validation: MetaGL 8/8 (+ sanitize 8/8, shared 10/10 with SONAME/export tests armed,
real EGL smoke); EasyGL 11/11 against the accepted MetaGL, including the WebGL suite —
`easy-gl-resource-smoke-tests`, the one failing binary of the Skia session's principal control,
passes with the rvc content. Clean-range sweeps: MetaGL 0 hits; EasyGL 1 hit = the sanctioned
`CLAUDE.md` filename reference (policy §2.1).

### §7.4 steps 1–2

The uncommitted `easy-glrvc` `CMakeLists.txt` redirect was restored (`git restore`, never stash) —
classification B, temporary development-only; its replacement is MetaGL develop itself now carrying
the accepted content. The missing EasyGL archive tag was then created (the campaign's one
provenance gap, closed).

### CNA adaptation — `adapt/gl`, worktree `cnaintegration-gl`, 30 signed commits

26 of 28 originals replayed chronologically (both NEXT.md-only handoff commits OMITTED with
justification — the head's NEXT.md is the campaign's live record and the replay would have deleted
2000+ post-fork lines; `opengl4` precedent). The five narrative bodies were reworded per policy
§2.2 (technical content kept; "another agent"/"autonomous agent action"/"co-authored by Junie"
prose removed). Plus four adaptation commits: the compile-probe-driven interface adaptation
(`112a0326`), GLB-38 (`7471a512`), the REMED-GFX-219 test-contract move (`8dc52c15`), and the
cross-backend probe fixes (`a8c32a67`). Range-diff (28 pairs; 2 dropped `<`, content differences
confined to the recorded conflict adaptations) archived at
`/rv/cna-builds/feature-gl/range-diff-gl-full.txt`.

Measured corrections to this card: `GpuDrawParams` cost was **zero** (the lane's diff references
none of the four removed fields — "cost applies" above was a fork-position prediction, not a
measurement). The Dx3 files' changes were redirected to the head's **FreeDirect** backend (the
head's `Dx3` is the real DirectX 3); the registration union (the ninth) kept all 31 other public
identities token-exact.

### Public identities and capability truth

Public backend count moves **32 → 35**: `EASYGL` withdrawn from public selection, `OPENGLES`
(Linux default), `OPENGL33`, `WEBGL1`, `WEBGL2` (Emscripten default) added — one shared internal
EasyGL implementation selected by `CNA_GL_PROFILE_*`; **EasyGL remains internal and hidden**
(§7.0 upheld; `CNA_BACKEND_EASYGL` stays the internal compile identity). `RequireFaithfulDeclarationEXT`
refusal-guard: **not applicable by the OPENGL2 precedent** — the EasyGL family name-binds real
declarations (REMED-GFX-201 multi-stream). **REMED-GFX-219 RESOLVED**: WireFrame now reports
true — the GL_LINES re-expansion renders the oracle-measured correct wireframe (interior 0/1089,
all three edges) — and the deliberately-failing capability arm was moved to the true side as its
own message instructs. WEBGL1 statically reports false for MultiStreamVertexInput, MRT,
OcclusionQuery, Texture3D and Instancing (GLES 2.0 lacks the entry points; refusal up front
instead of failure inside GL).

### Validation

| Instrument | Result |
|---|---|
| OPENGLES corpus (principal continuity) | **5906 · 5900 · 0 failed · 6 truthful skips** |
| OPENGL33 corpus | **5906 · 5900 · 0 failed · 6 skips** |
| EasyGL-family example suite (the 236/241 instrument, grown to 293) | OPENGLES **292/293** · OPENGL33 **292/293** — the one failure is `EasyGL_GraphicsDevice_ReferenceStencil`, documented pre-existing (plan_graphics Task 872) |
| Pre-adaptation baseline (feature/gl @ `f8efb9b4`, OPENGL33, vs recorded 236/241) | **237/241** — 4 of the 5 recorded pre-existing failures reproduce; the 5th (`cna_oracle_render_easygl`) passes on this host |
| WEBGL1/WEBGL2 | **deterministic configure-time rejection proven natively**; build/runtime **unavailable on this host** (no Emscripten SDK anywhere — the fork-era `~/emsdk` belonged to the previous sandbox) |
| Compile probes (15) | FREEDIRECT, SDL_RENDERER, ASCII, SOFTWARE, STUB, HEADLESS, VULKAN, OPENGL1, OPENGL2, OPENGL4, OPENGLES1, SOKOL, MAGNUM, SKIA, DILIGENT — **all OK** after four probe-found drift fixes |
| Sokol focused control | **37/37** |
| CnjCacheIsolation + Texture2D cache (REMED-GFX-223) | green (explicit runs + in-corpus) |
| REMED-GFX-224 | **OPEN, unchanged, visible** — `EasyGLRenderTargetBackend` still has no `UpdatePixels` override; the documenting suite (8/8) passes; not a supported-path blocker for this lane |
| ASan/UBSan | 8 focused suites + 1254-test graphics filter: **zero lane-originating findings**; the four UBSan prints reproduce byte-identically on the head-content control binary → pre-existing, filed as `REMED-CORE-015` / `REMED-CONTENT-010`; leaks 100 % `libGLX_mesa`-rooted, `detect_leaks=0` green |

### New findings

- **`REMED-CORE-015` (LOW, OPEN, pre-existing at `1381ff93`)** — `Vector3::GetHashCode`
  (`Vector3.cpp:117`) and `Matrix::GetHashCode` (`Matrix.cpp:249`) sum float bit patterns in
  `int`, overflowing (UB in C++; C# wraps legally). Control-proven on the head-content sanitizer
  binary.
- **`REMED-CONTENT-010` (LOW, OPEN, pre-existing, third-party)** — vendored `cgltf.h:2250`
  misaligned `const float` load during sparse-accessor resolution. Control-proven likewise.

### Final dependency chain

```
CNA integration/post-audit-phase1 @ 0a51f8647
  -> ../easy-gl @ develop 9b831dee   (GLB-38; configure references no easy-glrvc)
      -> ../meta-gl @ develop c964e736
```
Sibling-path consumption is each repository's explicit committed policy; both develops carry the
accepted revisions and a clean configure resolves exactly them.
