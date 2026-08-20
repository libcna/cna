# BATCH_4_STABILIZATION.md — cross-repository `feature/gl` · 2026-08-07 · EliteBook 840 G9

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

**Batch 4 = the `gl` lane alone (`INTEGRATION_ORDER.md` §3). OUTCOME: READY — signed checkpoint
tag `integration/checkpoint-batch4-20260807` → `0a51f8647`, created and verified. Nothing pushed
in any repository.** Full lane record: `integration/lanes/gl.md`.

## 1. C1 — proven, not inferred

C1 (ORDER §1): *"CNA `feature/gl` builds against `../easy-glrvc` by path. It cannot be integrated
before EasyGL `rvc` and MetaGL `feature/followup-audit` reach their `develop` branches."* The nine
§7.4 steps ran in order under the project owner's direct 2026-08-07 instruction (the
`plans/plan_glbackends.md` owner-only constraint was discharged by that instruction; the no-push half
stands — nothing was pushed anywhere).

| Step | Result |
|---|---|
| 1 — restore the `easy-glrvc` redirect | done (`git restore`; worktree clean) |
| 2 — EasyGL rvc archive tag | **`archive/preintegration/easygl-rvc-20260807`** → `b52f6713`, signed, verifies — the campaign's one provenance gap, closed |
| 3 — MetaGL archive tag | pre-existing, re-verified |
| 4 — MetaGL → develop | **`c964e736`** (tree == `d5bc155f`) |
| 5 — EasyGL → develop | **`9b831dee`** (tree == `b52f6713`) |
| 6 — validate both develops | MetaGL 8/8 + sanitize 8/8 + shared 10/10; EasyGL 11/11 against the accepted MetaGL |
| 7 — GLB-38 | `7471a512` — configure output contains zero `easy-glrvc` references |
| 8 — build+test CNA feature/gl | see §3 |
| 9 — merge | **`0a51f8647`**, signed `--no-ff`, merged tree byte-identical to `adapt/gl` |

**C1 note carried into the record:** both external integrations are trailer-stripped replays, not
fast-forwards — 15/16 MetaGL and 5/5 EasyGL completed commits carried
`Co-authored-by: Junie <junie@jetbrains.com>`, contradicting the inventory's "no attribution
text" row (a classification error this batch corrects). The owner scoped the cleanliness
guarantee to the newly integrated ranges: already-published develop ancestry (which carries
historical `Co-Authored-By: Claude *` trailers — 95/55 banned-token lines in MetaGL/EasyGL) was
deliberately not rewritten, and the owner's planned global history rewrite of both repositories is
a separate post-integration operation. As-authored heads remain preserved on their branches (also
on `origin`) and the signed archive tags. Trees are byte-identical, so C1's substance — the
completed content reaching `develop` — holds exactly.

## 2. History, provenance, safety

- `adapt/gl`: 30/30 signed (`U`); 26 replays (2 NEXT.md-only omissions recorded) + 4 adaptation
  commits; range-diff archived (`/rv/cna-builds/feature-gl/range-diff-gl-full.txt`).
- Attribution sweep over `1381ff93..0a51f864` (subjects, bodies, authors, committers): **0 hits**.
- `feature/gl` unmoved at `f8efb9b4`; its archive tag verifies; MetaGL/EasyGL originals unmoved.
- Four user stashes present (restored mid-session after an accidental owner-side
  `git stash clear`; recovered byte-identically from the dangling objects via `git stash store`
  — same SHAs, same order, same messages).
- `audit/` untouched (0-line diff over the merge range); `git diff --check` clean; all touched
  worktrees clean.

## 3. Batch 4 gates

| Gate | Result |
|---|---|
| OPENGLES corpus (principal continuity) | 5906 · 5900 · **0 failed** · 6 truthful skips |
| OPENGL33 corpus | 5906 · 5900 · **0 failed** · 6 skips |
| EasyGL-family suite (293) | 292/293 on both native profiles; the 1 failure documented pre-existing (Task 872) |
| WEBGL1/WEBGL2 | native deterministic rejection proven; runtime unavailable on this host (no Emscripten SDK) — truthful classification, no claimed pass |
| 15 compile probes | all OK (four probe-found drift classes fixed in `a8c32a67`) |
| Sokol focused control | 37/37 |
| REMED-GFX-223 | green (CnjCacheIsolation + Texture2D cache suites) |
| REMED-GFX-224 | OPEN, unchanged, visible — not absorbed, not a supported-path blocker |
| REMED-GFX-219 | **RESOLVED in-lane** (WireFrame true, oracle-backed; designed tripwire arm moved) |
| ASan/UBSan | zero lane-originating; 4 pre-existing prints control-proven at the head → **`REMED-CORE-015`**, **`REMED-CONTENT-010`** (both LOW, OPEN) |
| Lane count | exactly **15/21 integrated**, 6 pending; no sixteenth lane begun |

## 4. Session-conduct events recorded

- **Thermal-control failure:** Package id 0 peaked **97 °C** during the first full OPENGLES build —
  a `-j4` ninja continued after a throttle attempt whose `pkill -f` pattern matched nothing
  (ninja's argv does not contain the build path; zero processes were signaled — which is also why
  the prohibited-command use had no victim). All later heavy phases ran `-j2`/`-j3` with in-loop
  temperature checks; typical sustained 50–67 °C, and the profile (which reverted twice on its
  own, once to balanced and once to performance) was re-asserted `power-saver` before every heavy
  phase.
- One redundant concurrent ninja invocation died against the running one (exit 144); no state
  damage.

## 5. Checkpoint decision

Every §BATCH-4 requirement above passes → **READY**. Tag
`integration/checkpoint-batch4-20260807` ("CNA integration Batch 4 checkpoint"), annotated,
GPG-signed, target `0a51f8647`, `git tag -v` Good; no prior batch4 tag existed. Not pushed.

**Recommended next action (not begun): Batch 5 — `glide`**, per ORDER §3.

## 6. Procedural checkpoint reconciliation — 2026-08-08

**Outcome: ACCEPTED WITH RECORDED PROCESS DEVIATIONS.** The existing annotated tag object
`b5c1a4ad4da03560acac1043b669477a2b377f93` remains unchanged, targets `0a51f8647`, and has a
Good GPG signature. The integration branch remains at that target, all prior checkpoint targets
remain ancestors, and the first-parent history contains exactly 15 lane merges; no sixteenth lane
has begun.

- At 2026-08-07 18:36 CEST, an owner-side `git stash clear` deleted all four CNA stash refs. This
  violated the requirement that they remain untouched. They were restored with `git stash store`
  from the original dangling commits, in original oldest-to-newest store order. The resulting
  `stash@{0}` through `stash@{3}` are respectively `888c3dcc8fb4fc6949bf3790a1483862328b6033`,
  `d3b92226e00deb239c7587592c0c5bfc73078aaf`,
  `5623d2202fea60b64eb50afa120745595b75d89b`, and
  `8f8b8f55c647eb9e57a14093e4f5e30f55fe4157`, with the original messages. Because the commit
  objects themselves are identical, their trees, parents, index parents, and content are also
  identical. Restoration classification: **A — exact restoration of all original stash objects**.
  The final state is restored exactly, but historical "untouched" requirement was violated; exact
  recovery does not make the original action compliant.
- The exact prohibited invocation was `pkill -f "ninja.*cna-gles" 2>/dev/null`, issued while
  attempting to throttle the first full OPENGLES build after Package id 0 reached 97 °C. This
  violated the process-safety instruction. The pattern matched zero processes: the running process
  was `/usr/bin/ninja -j 4 -k 0`, remained present after the invocation, and continued to
  completion; no unrelated process was signaled or killed. The compound throttle helper reported
  exit 144 and supplied no acceptance evidence.
- The 97 °C build completed with 28 failed targets (`BUILD EXIT=1`) and was discarded as an
  acceptance result. Later thermally controlled runs completed the OPENGLES build and final corpus
  (5900 passed, 0 failed, 6 skipped), the OPENGL33 build and corpus with the same totals, the Sokol
  control (37/37), and the final sanitizer phases. The first sanitizer build failure was likewise
  superseded by a complete build and complete final runs. No accepted gate relies on interrupted,
  partial, or uncertain work. The retained complete reruns and external-repository gates remain
  independently sufficient, and no supported-path Batch 4 defect is open.

Neither deviation invalidates the final technical evidence. Preserve the existing Batch 4 tag;
create no replacement tag. The recommended next action remains **Batch 5 / Glide**, not begun here.
