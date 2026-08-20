# Lane card — `ext` · **THIRD INTEGRATION LANE** · **ADAPTED**

| Field | Value |
|---|---|
| Logical lane | `ext` |
| Refs | **remote-only** `refs/remotes/origin/feature/ext` — no local branch existed or was created; **unmodified by the integration** |
| Head | `05ab5d3d002945c603fc28f2a5a23f8027773d63` |
| Archive tag | **`archive/preintegration/ext-20260804`** → `05ab5d3d` · annotated · GPG-signed · verifies good · local only · **unchanged** |
| Merge base with checkpoint | `ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c` (`origin/develop`) — **develop-forked**, not audit-stacked |
| Own commits / files | **1 / 1** · `NOXNA.md`, `+568, −241` |
| Subsystem | **documentation only** — the extended-graphics (NOXNA) design document |
| Shared interfaces | **none** — no `GraphicsDevice.cpp`, no `IGraphicsBackend.hpp`, no `GraphicsCapability.hpp` |
| `GpuDrawParams` cost | **zero** — the lane changes no compiled source at all |
| Dependencies | **none**, internal or external |
| Conflict class | **LOW** as recorded — but **not zero**, see the conflict below |
| Development status | DEVELOPMENT COMPLETE |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED (total)** — 1/1 |
| Integration readiness | **ADAPTED AND MERGED — see §Integration record** |

## Dependency chain

None. `ac3aaaeb` is an ancestor of `integration/post-audit-phase1`; nothing gates this lane and it
gates nothing.

---

# Phase 1 — original commit audit

## Commit inventory — the complete lane

`git rev-list --count ac3aaaeb..origin/feature/ext` returns **exactly 1**. Expected, and verified
rather than assumed.

| Field | Value |
|---|---|
| SHA | `05ab5d3d002945c603fc28f2a5a23f8027773d63` |
| Parent | `ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c` |
| Subject | `docs(NOXNA.md): rewrite as the final extended-graphics design` |
| Author | **`Claude <noreply@anthropic.com>`**, 2026-07-20T13:49:26Z |
| Committer | **`Claude <noreply@anthropic.com>`**, 2026-07-20T13:49:26Z |
| Signature | **SSH signature**, key `ssh-ed25519 …rLzsfFISF4by8Q+FKz27YpkK1USsBB+mamu1QkJnbDs` — **not** the maintainer's GPG key `FB9CE8E20AADA55F` |
| `%G?` | **`N`**, emitted together with `error: gpg.ssh.allowedSignersFile needs to be configured and exist for ssh signature verification` |
| Files changed | `M NOXNA.md` — **one file**, `+568 / −241` |
| Trailers | `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` · `Claude-Session: https://claude.ai/code/session_016xrqJH1Kb9fGnRMRRHmFz1` |

### Correction to the inventory — the commit is *not* unsigned

`INTEGRATION_BRANCH_INVENTORY.md` §5 classes `ext` with `opengl4`/`depthcrt`/`magnum`/`wicked` as
"**all unsigned**". For `ext` that is **not accurate**. The raw object carries a
`gpgsig -----BEGIN SSH SIGNATURE-----` block.

It makes **no difference to the required action** — an SSH signature from a non-maintainer key is
not a maintainer GPG signature, so the adapted commit had to be signed with `FB9CE8E20AADA55F`
either way — but the distinction matters for two reasons and is recorded rather than smoothed over:

- `%G?` reported **`N`** here, not the `E` that `INTEGRATION_HISTORY_POLICY.md` §8 predicts for "an
  SSH-signed commit hitting the unconfigured `gpg.ssh.allowedSignersFile`". The `error:` line is
  emitted on stderr and the status still degrades to `N`. **A tally that only counts `%G?` letters
  cannot distinguish "no signature" from "SSH signature that cannot be checked."** Any later lane
  audited by `%G?` alone will inherit that blind spot.
- The remaining lanes in this class (`opengl4` 28, `magnum` 13, `wicked` 10) have **not** been
  re-checked at the object level. Their "unsigned" classification should be re-derived with
  `git cat-file -p`, not restated from the inventory.

## Attribution scan

Both the commit metadata **and** the document body were swept for every banned token in
`INTEGRATION_HISTORY_POLICY.md` §2.

```
git log --format='%H%nSUBJ:%s%nBODY:%b%nAUTH:%an <%ae>%nCOMM:%cn <%ce>%nTRLR:%(trailers)' \
  ac3aaaeb..origin/feature/ext | grep -inE '<banned>'
→ 2 hits, both in the TRAILERS (the Co-Authored-By and Claude-Session lines)

git show 05ab5d3d:NOXNA.md | grep -inE '<banned>'
→ 2 hits, both the string "CLAUDE.md" (lines 194, 440)
```

The document body was scanned as well as the message because this lane's entire payload **is** prose.

**Neither document hit is a violation.** `CLAUDE.md` is a real tracked file at the repository root
and both sentences describe it as one — *"(CLAUDE.md rules apply verbatim to this layer too)"* and
*"CLAUDE.md forbids designing for hypothetical…"*. `INTEGRATION_HISTORY_POLICY.md` §2.1 names this
case explicitly.

The commit **subject and body are entirely technical** and contain zero banned tokens. No process
narration, no session text, no status text. Only the two trailers had to go.

## Required cleanup — determined by inspection, not by the lane card

| Class | Required? | Why |
|---|---|---|
| **AUTHOR cleanup** | **YES** | author is `Claude <noreply@anthropic.com>` — policy A2/A3 |
| **COMMITTER cleanup** | **YES** | committer is the same identity |
| **TRAILER cleanup** | **YES** | `Co-Authored-By:` + `Claude-Session:` |
| **SIGNATURE addition** | **YES** | original carries an SSH signature from a non-maintainer key, not a maintainer GPG signature — policy A4 |
| **MESSAGE cleanup** | **NO** | subject and body are clean technical prose and were preserved verbatim |

Direct merge was therefore **not permitted** for this lane — four of the five classes apply.

---

# Phase 2 — actual lane scope

Determined from the commit, not from the branch name.

| Class | Present? |
|---|---|
| Production source | **no** |
| Public / internal headers | **no** |
| Namespaces, macros | **no** — the document *describes* `CNA::Graphics` and `NOXNA`; it defines nothing |
| CMake options / build registration | **no** — it *documents* `CNA_NOXNA`, which already exists (`CMakeLists.txt:40`, default `OFF`) |
| Tests / test registration | **no** |
| Examples | **no** |
| Generated assets, dependencies, submodules | **no** |
| Documentation | **yes — the entire lane** |

**Classification: planning/documentation only.** It is a *design and backlog* document, not an
implementation. It adds no EXT functionality, no capability, and closes no ticket.

**Proven, not asserted:** the adapted commit's tree is byte-identical to the integration head's tree
in **every path except `NOXNA.md`**:

```
git ls-tree -r 722a2f5a | grep -v NOXNA.md | git hash-object --stdin  →  39fc831a…
git ls-tree -r c6a28036 | grep -v NOXNA.md | git hash-object --stdin  →  39fc831a…
```

`LANE CLASSIFICATION INVALID` was **not** raised: the lane touches no shared high-conflict interface
and its scope is exactly what the inventory recorded.

## What the document actually says

It reframes `NOXNA.md` from a "scaffolding / not started" draft into a final design, and its central
contribution is a **boundary that did not previously exist in writing**:

| | **NOXNA marker convention** | **`CNA_NOXNA` engine layer** |
|---|---|---|
| What | the `NOXNA` macro + `*EXT` suffix | a CMake option gating a `CNA::Graphics` namespace |
| Compiled | **always** — a documentation/lint marker | **opt-in**, all under `#ifdef CNA_NOXNA` |
| Lives in | `Microsoft::Xna::Framework::Graphics` | `CNA::Graphics` |

It then records what already ships, corrects four stale claims, specifies the remaining engine-layer
classes/enums/backend virtuals, and renumbers the backlog.

---

# Phase 2b — are the document's claims still true at the integration head?

The substantive question for a documentation lane. Answered by measurement in the integration
worktree, not by reading.

| Claim in the document | Measurement | Verdict |
|---|---|---|
| N05 `include/CNA/Graphics/NOXNA.hpp` "mislabeled done; **actually missing**" | path does not exist | **CORRECT** |
| N01 `CNA_NOXNA` CMake option exists | `CMakeLists.txt:40`, default `OFF` | **CORRECT** |
| N02/N03/N04 enums + `RenderPipelineSettings` + `PbrMaterial` exist | all five headers present in `include/CNA/Graphics/` | **CORRECT** |
| N06 `examples/noxna_settings_example.cpp` exists | present | **CORRECT** |
| N10 `GraphicsCapability::{FloatRenderTargets,ComputeShaders,StorageBuffers,SeamlessCubeMapFilter}` **absent** | 0 occurrences of all four in `GraphicsCapability.hpp` | **CORRECT** |
| `RenderPipelineSettings` **has no consumer** | referenced only by its own `.hpp`/`.cpp` and the example | **CORRECT** |
| HDR `SurfaceFormat`s already exist | `Vector4`, `HalfSingle`, `HalfVector4`, `HdrBlendable` all declared | **CORRECT** |
| `PbrEffect`, `SkinnedPbrEffect`, `ShaderEffect`, `MorphTargetDataEXT`, `SkinnedModelEXT`, `VertexPositionNormalTangentTexture`, `DrawInstancedPrimitives` already ship | every one present | **CORRECT** |
| Draco is optional and CMake-detected | `cmake/CnaLibrary.cmake:28` `find_package(draco CONFIG QUIET)` | **CORRECT** |

**Every testable claim holds.** Unlike `gltf`, this document pins no baseline date or commit, so
there is no dated citation to preserve or drift against.

## The one thing the document does not know about

It was authored against `ac3aaaeb`, which predates `depthcrt`. Its §5 *File layout* listing of
`include/CNA/Graphics/` therefore omits the five headers that lane added — `DepthEffect.hpp`,
`DepthEffectMode.hpp`, `DitherMode.hpp`, `CRTEffect.hpp`, `CRTMaskType.hpp` — and §3
(*What already ships*) and §3.5 (*What is deliberately NOT there yet*) do not mention them either.

**Left as written, deliberately.** That listing is a *design layout* for the engine-layer classes
being specified — every line is annotated `(exists)` or `[NEW]` against that design — not an
exhaustive directory inventory. `DepthEffect`/`CRTEffect` are shipped `ShaderEffect` subclasses that
do not participate in the `RenderPipeline`/`PostProcessPass` design the section describes. Extending
it would be authoring new design content this lane does not contain.

The **backlog** rows are a different matter, and were preserved — see below.

---

# Phase 3 — adaptation record

## Identity

| Field | Value |
|---|---|
| Integration base | `722a2f5adb07a0e75616c72ebc528ca628b19198` (`integration/post-audit-phase1`, after `gltf`) |
| Adaptation branch | **`adapt/ext`** — created from `722a2f5a`, **retained** after merge |
| Adaptation worktree | `/rv/data/development/github.com/openeggbert/cnaintegration-ext` — **retained**, clean |
| Adapted head | **`c6a280367eff3ac555f5af03c95ae3f1dce86dd2`** |
| Merge commit | **`8a374b9f81d4a48779d5cdfb609f84a5007fdda3`** — signed, `--no-ff` |

## Original-to-adapted commit mapping

| # | Original | Adapted | Subject | Disposition |
|---|---|---|---|---|
| 1 | `05ab5d3d` | `c6a28036` | `docs(NOXNA.md): rewrite as the final extended-graphics design` | **re-authored, trailers stripped, GPG-signed, one hunk adapted** |

1 → 1. Nothing dropped, nothing recomposed, nothing added.

| Field | Original | Adapted |
|---|---|---|
| Author | `Claude <noreply@anthropic.com>` | **`Robert Vokac <robertvokac@robertvokac.com>`** |
| Committer | `Claude <noreply@anthropic.com>` | **`Robert Vokac <robertvokac@robertvokac.com>`** |
| Author date | 2026-07-20T13:49:26Z | 2026-08-04T17:46:00+02:00 |
| Signature | SSH, non-maintainer key | **GPG `FB9CE8E20AADA55F`, `%G?` = `U`** |
| Trailers | 2 prohibited | **none** |
| Subject | *(unchanged)* | *(unchanged)* |
| Body | technical, 7 bullet groups | **preserved verbatim** + one adaptation paragraph |

**Author date policy.** Fresh, not preserved — matching the `depthcrt` precedent. Policy A1
("preserve legitimate human authorship … with its original author date") is scoped to commits with a
legitimate human author. This commit has none, so A2 applies and there is no human author date to
preserve.

## The one real conflict, and how it was resolved

`git cherry-pick -n 05ab5d3d` onto `722a2f5a` produced **exactly one conflict**, in the only file the
lane touches.

**Cause.** `depthcrt` added four backlog rows to `NOXNA.md` — `N26`–`N29`, the `DepthEffect` and
`CRTEffect` entries — and that is the *only* difference between `NOXNA.md` at the merge base and at
the integration head (`+4, −0`; the checkpoint's blob `16e1cb03` is byte-identical to the merge
base's). The `ext` rewrite **renumbers the whole backlog** and, being authored against the older
file, replaces the `N20`–`N25` block those four rows were appended to.

**Left to a plain 3-way resolution favouring the incoming side, this lane would have silently
deleted four rows of already-integrated work.** That is precisely the failure mode
`INTEGRATION_ORDER.md` §4 warns about from `depthcrt`'s own experience with
`cmake/Examples.cmake`/`REMED-BUILD-002`.

**Resolution.** Take the incoming renumbered `N20`–`N25` block in full, then re-append `N26`–`N29`
**verbatim**. The renumbering happens to leave `N26`–`N29` free — the new backlog runs `N20`…`N25`
(*HDR pipeline & post-processing*) then jumps to `N30` (*Shadows*) — so the four rows keep both their
numbers and their structural position, immediately after `N25` and before `### Shadows`, exactly
where `depthcrt` put them. No renumbering of the preserved rows was needed and none was performed.

**Cross-reference check on the preserved rows.** `N27`'s text ends *"…without compute shaders (see
N70)"*. Old `N70` = `` `ComputeShader` NOXNA class (EasyGL compute) ``; new `N70` =
`` `IComputeShaderBackend`/`IStorageBufferBackend` + EasyGL (GLES 3.1) impl ``. Both are the entry
task of the *Compute* section, so the reference still resolves and was left unchanged.

## Hunk disposition — every original hunk accounted for

| Disposition | Count | Detail |
|---|---|---|
| **transferred unchanged** | all but one hunk | the whole rewrite: §1–§7, §9–§11, and the backlog outside `N20`–`N25` |
| **adapted** | **1** | the `N20`–`N25` hunk — incoming content taken in full, with the four `depthcrt` rows preserved as trailing context |
| **already present** | 0 | |
| **superseded** | 0 | |
| **omitted** | **0** | **no hunk was dropped** |

## Losslessness proof

The strongest available form for an adapted lane — compare the **resulting file**, not the patch:

```
diff  <(git show 05ab5d3d:NOXNA.md)  <(git show 8a374b9f:NOXNA.md)

  lines present in merged but absent from the original ext result : 4   (exactly N26–N29)
  lines lost from the original ext result                          : 0
```

**Zero lines of the original patch's output were lost**, and the only addition is the four preserved
`depthcrt` rows — themselves verified byte-identical to their integrated form:

```
diff <(git show 722a2f5a:NOXNA.md | grep -E '^\| N2[6-9] \|') \
     <(git show 8a374b9f:NOXNA.md | grep -E '^\| N2[6-9] \|')   →  identical
```

## Patch-id

| Range | `git patch-id --stable` |
|---|---|
| `ac3aaaeb..05ab5d3d` (original) | `fcc1db98de59f8f47dd1dedb9a0ba72310cf2ba2` |
| `722a2f5a..c6a28036` (adapted) | `6d6fe562d215c40ec5ce58509d4d9a3c70d66487` |

**They differ, and exact equality was not expected here.** The four preserved rows sit inside the
conflicted hunk's window, so they appear as *context* lines in the adapted patch and are absent from
the original's. `patch-id` hashes context as well as payload. The insertion/deletion counts are
nonetheless **identical** (`+568, −241` in both) — the four rows are context on both sides of the
adapted hunk, not payload.

## Range-diff

`git range-diff ac3aaaeb..05ab5d3d 722a2f5a..c6a28036` — **38 lines total**, containing exactly
three differences and nothing else:

1. `Author: Claude <noreply@anthropic.com>` → `Author: Robert Vokac <robertvokac@robertvokac.com>`
2. the two prohibited trailers removed; the adaptation paragraph added
3. the `N20`–`N25` hunk's trailing context: blank + `### Shadows` in the original, `N26`/`N27`/`N28`
   plus a second hunk header in the adapted

No `+`/`−` payload line of the original patch appears as removed. Every difference is confined to
author, trailers, message, and the recorded adaptation — as `INTEGRATION_HISTORY_POLICY.md` §P5
requires.

---

# Phase 4 — validation

## Build and test matrix — derived from the real commit

| Check | Applies? | Basis |
|---|---|---|
| Build targets changed by the lane | **none** | the lane changes zero build inputs |
| Tests added or changed by the lane | **none** | the lane adds no test |
| Focused subsystem tests | **none applicable** | no subsystem source is touched |
| Configurations consuming the affected headers | **none** | no header is affected |
| Feature-disabled configuration unaffected | **trivially** | nothing compiles differently with `CNA_NOXNA` on or off |
| Sanitizer coverage | **not warranted** | no lifetime, pointer, resource, parser or memory-sensitive behaviour is introduced |
| Wider suite | **not warranted** | none of the four triggers in the session's matrix rule is met |

**No build was run, and that is a measured conclusion rather than an assumption.** Two independent
measurements support it:

1. `git diff --name-only 722a2f5a c6a28036` → `NOXNA.md`, and nothing else.
2. Tree-hash equality outside `NOXNA.md` between the base and the adapted commit (§Phase 2).

`NOXNA.md` is referenced by **no** `CMakeLists.txt`, `.cmake`, `.cpp`, `.hpp`, `.in`, `.py` or `.sh`
build input. Its only occurrence in compiled source is a **Doxygen comment** in
`include/CNA/Graphics/DitherMode.hpp:14`, which cites it as prose and does not include or parse it.

This mirrors `gltf`: a documentation-only lane offers nothing to build.

## Residuals

Both pre-existing base residuals are **unchanged and untouched** by this lane, which changes no
compiled source and so cannot have affected either in any direction:

- `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly` — the escaping
  exception is the correct `REMED-GFX-DECL-GUARD` rejection; the fuzz test's expected-exception set
  predates the guard. **A test-side gap, not a production defect.** Owner triage for
  `plans/plan_postaudit.md`.
- `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`
  — 30 s timeout in a real two-process networking test, environment-dependent.

Neither was re-run, for the same reason `gltf` did not re-run them.

---

# Findings

## No new remediation ticket — no independent production defect

Per the session rule, a ticket is opened only for an **independent production defect**. This lane
changes no compiled source and introduces none. **None found.**

## But the renumbering does invalidate cross-references — recorded, deliberately not fixed

This is the lane's one substantive consequence and it is **not** cosmetic drift: other files at the
integration head cite `NOXNA.md` task numbers by value, and the renumbering silently changes what
four of those citations resolve to. Swept with
`git grep -nE 'N[0-7][0-9]\b' -- '*.hpp' '*.cpp' '*.md' '*.cmake' CMakeLists.txt`:

| Location | Cites | Old meaning | New meaning | Verdict |
|---|---|---|---|---|
| `include/CNA/Graphics/DitherMode.hpp:14` *(public header)* | `N70` | `ComputeShader` NOXNA class (EasyGL compute) | `IComputeShaderBackend`/`IStorageBufferBackend` + EasyGL | ✅ **still resolves** — both are the Compute section's entry task |
| `include/CNA/Graphics/PbrMaterial.hpp:19` *(public header)* | `N11` | `PbrEffect` — NOXNA `Effect` subclass using the PBR shader | thread `SurfaceFormat` into `CreateRenderTarget2DEx`; EasyGL RGBA16F/32F FBOs | ❌ **broken** |
| `noxna_devices.md:93` | `N11` | *(quotes the header)* | *(same)* | ❌ **broken** |
| `docs/surface-format-support.md:184` | `N20` | RGBA16F `RenderTarget2D` support in EasyGL | `RenderPipeline` + `HdrSceneTarget` | ⚠️ **drifted** — the float-RT work is now `N11`/`N12` |
| `docs/surface-format-support.md:220` | `N20` | *(same)* | *(same)* | ⚠️ **drifted** |
| `plans/plan_postaudit.md:1572-74` | `N50`/`N51`/`N52` + "§4.4 *Geometry & Instancing*" | `DrawInstancedPrimitives` overload · instance-VB helper · LOD helper | `InstancedRendererEXT` · `LodGroupEXT` · glTF→`PbrMaterial` bridge; section is now §8 *Geometry helpers* | ❌ **broken** — it quotes the old titles and the old section number inline |
| `audit/include/CNA/Graphics/PbrMaterial.hpp.audit.md`, `audit/examples/noxna_settings_example.cpp.audit.md` | `N11` | *(quote the header)* | *(same)* | ❌ **broken — but `audit/` is frozen and must not be modified** |

**Not fixed here, on purpose.** Repairing them means editing four files outside a one-file lane, and
two of the seven citations live under `audit/`, which this session may not touch at all. Widening the
lane to chase them would be exactly the silent scope broadening the session forbids.

**One nuance worth separating.** `PbrMaterial.hpp:19` was *already* partly stale at the base: it
frames `PbrEffect` as something a future task must build, but `PbrEffect` ships at the integration
head (the new document records it under *Already shipped*, CNB-56…60). The **pre-existing** half —
"no production consumer", recorded in `audit/include/CNA/Graphics/PbrMaterial.hpp.audit.md` — is a
checkpoint residual, not this lane's doing. This lane adds only the wrong **number**.

**Recommended owner:** the **Batch 0 stabilization checkpoint**, which is the recommended next action
independently. It is the natural place: it already reviews the batch's combined effect, and the
`audit/` citations need an owner decision that a lane session cannot make.

> **✅ RESOLVED 2026-08-04 by the Batch 0 stabilization checkpoint.** All four live non-`audit`
> citations were repaired semantically — `PbrMaterial.hpp:19` and `noxna_devices.md:93` `N11`→**`N52`**
> (the `applyMaterial` binding, because `PbrEffect` itself now ships),
> `docs/surface-format-support.md:184` `N20`→**`N11`** and `:220` `N20`→**`N11`/`N12`**, and
> `plans/plan_postaudit.md:1572-74` to **`N50`/`N51`** under **§8 *Geometry helpers***. `DitherMode.hpp:14`'s
> `N70` was confirmed to still resolve and was left alone. The three `audit/` citations (two files)
> remain **untouched by owner decision** — they are dated evidence of what the header said when it was
> audited. One point this card did not anticipate: the same citations on `feature/audit` are
> **correct there**, because that branch still carries the pre-`ext` `NOXNA.md`, so the repair is
> integration-branch-only. See `integration/BATCH_0_STABILIZATION.md` §3.

---

# Integration record

## Merge

| Field | Value |
|---|---|
| Merge commit | **`8a374b9f81d4a48779d5cdfb609f84a5007fdda3`** |
| Parents | `722a2f5a` (integration) + `c6a28036` (`adapt/ext`) — **true `--no-ff` merge** |
| Author / committer | `Robert Vokac <robertvokac@robertvokac.com>` |
| Signature | **GPG `FB9CE8E20AADA55F`**, `%G?` = `U` |
| Subject | `merge(integration): integrate the NOXNA extended-graphics design lane` |

## Post-merge verification

| Check | Result |
|---|---|
| Checkpoint `d79214e7` still an ancestor | ✅ |
| `depthcrt` merge `61bd1a1b` still an ancestor | ✅ |
| `gltf` merge `722a2f5a` still an ancestor | ✅ |
| `gltf` original commit `86ada7a7` still an ancestor | ✅ (direct-merge object identity intact) |
| Adapted `c6a28036` an ancestor | ✅ |
| Published ancestor `61bd1a1b` unrewritten | ✅ — same object, parents `d79214e7` + `3cca0b19` |
| Merges on the branch | exactly **3** — `depthcrt`, `gltf`, `ext` |
| Signatures `d79214e7..8a374b9f` | **10 / 10 `U`** — no `N`, no `E` |
| Attribution sweep `d79214e7..8a374b9f` | **ZERO HITS** |
| `origin/feature/ext` | `05ab5d3d` — **unchanged** |
| `archive/preintegration/ext-20260804` | `05ab5d3d` — **unchanged**, still verifies good |
| Tree equality outside `NOXNA.md` | ✅ identical to `722a2f5a` |
| `depthcrt` rows `N26`–`N29` at the merged head | ✅ all four, byte-identical |
| `git diff --check` | ✅ clean |
| Worktrees clean | ✅ all four |
| Pushed | **nothing** |

## Conditional blockers re-checked for this lane

`REMED-CONTENT-007` / `-008` (`INTEGRATION_ORDER.md` §6) — **not blockers for `ext`**, re-checked
rather than waved through. The lane's single changed path is `NOXNA.md`; it touches
`ContentManager.cpp`, `ContentReader.cpp`, `SongContentTypeReader.cpp`,
`VideoContentTypeReader.cpp` and `PathContainment.hpp` **not at all**, and changes no compiled source
in which a path could be resolved. **Both findings remain OPEN, HIGH / P1.**

## Completion criteria

| # | Criterion | Status |
|---|---|---|
| 1 | Refs fetched, original head and signed archive tag verified | ✅ |
| 2 | Exact original commit audited; inventory's "unsigned" claim corrected | ✅ |
| 3 | Precise cleanup requirements established by inspection | ✅ author + committer + trailers + signature |
| 4 | Original history unchanged | ✅ `origin/feature/ext` and the archive tag both at `05ab5d3d` |
| 5 | One clean adapted commit, GPG-signed, human-authored, no trailers | ✅ `c6a28036` |
| 6 | Zero AI attribution in the adapted history | ✅ |
| 7 | Every original hunk accounted for; none omitted | ✅ 1 adapted, 0 dropped |
| 8 | `range-diff` produced and reviewed | ✅ 38 lines, three expected differences |
| 9 | Validation matrix derived and executed | ✅ documentation-only — no build input, proven twice |
| 10 | One signed `--no-ff` merge extending published history | ✅ `8a374b9f` |
| 11 | Checkpoint, `depthcrt`, `gltf` remain ancestors | ✅ |
| 12 | Nothing pushed; no other lane begun; `audit/` untouched | ✅ |
