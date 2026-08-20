# glTF campaign retrospective

Status: written 2026-08-18 (`GLTF-460`). Scope: `plans/plan_gltf.md`'s 475 rows, from the 2026-08-11
forensic audit to the milestone in force today.

This is not a summary of what was done — `plans/plan_gltf.md` is that, row by row, and it is the record.
This is the part a row cannot carry: **which kind of evidence caught which kind of defect**, and what
that implies for the next subsystem campaign. It is written to be reusable, so every claim below is
tied to a specific defect that actually happened rather than to a principle that sounds right.

---

## 1. What the audit found, and what the fixes cost

The campaign opened with eight reproduced defects (`D1`–`D8`), and the observation that shaped
everything after it: **every one of them produced a model that rendered.** None crashed, none threw,
none produced an obviously broken image. That is the defect class this subsystem generates, because a
glTF importer's failure mode is not a crash — it is a plausible surface.

The cost was not distributed the way the row count suggests. 475 rows closed, but the expensive ones
were not the hard features; they were the **rules that were already implemented and quietly wrong**:

| Defect shape | Example | Cost |
|---|---|---|
| Implemented, and wrong in a way that renders | `GLTF-461`: flat normals *averaged* at shared vertices instead of split per face | A vertex-split that renumbers every per-vertex stream including morph deltas |
| Implemented, and abandoned under a combination | `GLTF-462`/`463`: `COLOR_0` on a metallic-roughness material dropped the whole material model, taking the authored `NORMAL` with it | Two new vertex strides and an ABI change across seventeen renderers |
| Declared, and unreachable | `GLTF-472`: two renderers' complete `COLOR_0` shaders sat behind a draw route that never selected them | One predicate each — and a new class of test |
| Refused for the wrong reason | `GLTF-473`: `OPENGLES1` read a PBR record's `NORMAL` floats as a vertex colour | A shared guard, and a rule about *when* a refusal counts |

The pattern across all four rows: **the implementation was present and the wiring was not.** That is
worth naming because it is invisible to the review style this project otherwise does well — reading
the code that implements the feature.

---

## 2. Which oracle layer caught what

The campaign's seven-layer ladder (L0–L7, `docs/gltf-conformance.md`) is the reusable part, and its
value was not evenly distributed either.

| Layer | What it is | What it actually caught |
|---|---|---|
| L0–L2 | container, buffer, accessor decode | Malformed-input handling; the sparse-accessor and index-decode defects. Cheap, and it found real bugs. |
| L3 | the semantic mesh a conforming reader must produce | **The highest-yield layer.** It is where a *derived* value belongs — flat normals, generated tangents, the vertex split — and where `flatnormals.py` gives a second opinion instead of restating CNA. |
| L4–L5 | draw state and byte-exact vertex buffers | Stride/ABI regressions; the layout table's own consistency. |
| L6 | captured `GpuDrawParams` | Effect selection and factor transport — `GLTF-372`'s alpha-test vector reached the effect and nothing consumed it. |
| L7 | rendered pixels vs committed goldens, two independent processes | Renderer-specific divergence, and the only layer that can prove a shader is *evaluated* rather than *written*. |

**The gap the ladder did not cover, and the lesson of the last week:** every layer above reads either
data or source. None of them asks whether a draw *arrives*. Five defects lived in exactly that gap —
`SDL_GPU` and `DILIGENT`'s unreachable `COLOR_0` (`GLTF-472`), `OPENGLES1`'s misread PBR record
(`GLTF-473`), and `OPENGL4`'s and `DILIGENT`'s stride-chosen programs (`GLTF-475`) — and **every
static inventory in the repository reported all five as correct**, because each renderer genuinely
declared the layout and genuinely contained the shader expression the audit greps for.

The fix was a new tier, not a new assertion: the live draw tests — `RendererStrideConformance`
(`docs/gltf-renderer-stride-conformance.md`) and
`CrossRendererContract.NoRendererPaintsAVertexAttributeInsteadOfTheEffectsDiffuseColour`. They draw
through a real device and require the draw to succeed, or to be refused by name, or to answer the
exact colour the caller asked for. They are cheap, the stride half runs in CI on five renderers, and
between them they found the five defects above.

**The fifth one is the sharpest argument for the tier**, because the row that predicted the fourth
got the fifth wrong. `GLTF-475` was opened from a source reading of `OPENGL4` alone and named one
renderer; the probe that closed it answered eight, and `DILIGENT` — which the row never mentioned —
was drawing the same input **black**. A source audit finds the instance it was looking for. A live
draw enumerates the instances that exist.

**And a sixth, which is a different lesson: a label is not evidence either.** `GLTF-476` began by
asking what the specular inventory's "factor-only" label for `IGL` was based on. Nothing: the
partition asked whether a renderer's sources mention `pbrNormalMap`, and `IGL` did. Counting the PBR
draw parameters each renderer actually names put `IGL` at **6 of 20** against 14–20 for the other
fifteen, and the fourteen it dropped included four **core** glTF 2.0 material inputs — normal scale,
occlusion strength, sRGB encoding, and per-map texture transforms — which it neither applied nor
refused. Two of its own pixel witnesses were passing *on* the defect: one expected the linear BRDF
value without disabling the sRGB switches its cited reference disables, and read the right number
only because no encode existed to disable.

> **Reusable rule.** When an inventory partitions components, check the partition's own discriminator
> against the property it claims to measure. "Mentions the normal map" is not "implements the
> material model", and the gap between the two is where a renderer can sit for months looking
> finished. The cheap version of this check — does each component name each input at all? — is one
> test, and it would have caught this one on the day `IGL` landed.

> **Reusable rule.** A source audit can only see what a component *declares*. If a subsystem has a
> dispatch step between "the feature exists" and "the feature runs", the audit must include one test
> that executes the dispatch. Adding a table row to an inventory is not that test.

---

## 3. Three things that cost more than they should have

**Inventories drift toward fiction, and prose counts drift fastest.** `GLTF-344`'s row said "3 of 15"
while the machine-checked test beside it already said otherwise; §27.2's row 12 said four renderers
were missing on the strength of a continuity summary whose own data was a week old. The correction
that worked was structural: *the count comes from the state column, the evidence comes from the rows,
and a prose number is a comment.* Where a claim is worth keeping honest, it is written as a test —
`SpecularTextureInventoryClassifiesEveryPbrRenderer` names the unfinished set, so finishing one is a
deliberate edit rather than silent drift.

**Recorded environment blockers were wrong more often than they were right.** Four were checked in
the last week; **three were false**:

| Recorded blocker | Reality |
|---|---|
| DirectX11 L7 "needs a DXVK'd Wine prefix this environment does not have" | It had one (`GLTF-471`) |
| `OPENGLES1` cannot be run — no ES 1.1 driver | The side-by-side Mesa build from 2026-07-22 was still installed (`GLTF-473`) |
| DirectX9 shaders "regenerated only through the pinned `d3dcompiler_47.dll` … which this environment does not have" | The DLL was in `~/.cache/winetricks`, SHA-256-identical to the pinned value (`GLTF-465`) |
| WebGPU needs a wgpu-native artifact | **True** — it was downloaded, once, into `~/deps` |

> **Reusable rule.** A blocker is a measurement with a date, not a property. Re-check it before
> planning around it; the check is usually one command and the plan built on a stale one is not.

**A ratchet must be able to subtract.** `InlineGltfDocumentsDoNotGrowWithoutADecision` counted inline
test documents and only ever went up, so two documents that belonged in the corpus stayed inline for
weeks behind a blocker that had already been disproved. `GLTF-464` promoted them and lowered the
ceiling — the first subtraction in that list, and what the ratchet was for.

---

## 4. What the milestone name cost, and why it was worth it

`GLTF CORE 2.0 CORRECT` was declared on 2026-08-15, withdrawn, reclaimed, and withdrawn again. That
looks like churn and was not. Each cycle was driven by a specific, checkable argument:

1. **2026-08-17** — a re-audit against the pinned specification found four core divergences inside
   rows that were already green (§27.1.2). The declaration had been made from the row set, and the
   row set did not ask about them.
2. **2026-08-18** — the project owner rejected the unqualified name over one row: a renderer that
   *accepts* a valid asset and substitutes the white identity renders a visibly wrong surface, and a
   correct importer plus eight correct renderers does not make CNA correct. That argument produced
   the two-state partition with a forbidden third state, which is now the project's standing bar.
3. **Later the same day** — the name was reclaimed on the strength of that partition holding, and
   `GLTF-472` showed it did not hold: two renderers were in neither state.

The lasting output is not the name. It is the partition, its qualifier — *an explicit refusal counts
as one only if it happens before any incompatible vertex-layout interpretation and before any GPU
submission* — and the fact that both are machine-checked rather than remembered.

**The milestone in force is `GLTF CORE 2.0 IMPORT/RUNTIME MODEL CORRECT`**, with renderer coverage
stated beside it rather than inside it. `GLTF ROBUST` remains open on §27.2's rows 3, 6 and 12.

---

## 5. Method, for the next subsystem campaign

1. **Build the corpus before the fixes.** A generated, byte-reproducible fixture set with derived
   expectations is what makes a golden a second opinion instead of a restatement.
2. **Put the derived values at L3.** Anything the reader must *compute* — normals, tangents, splits —
   belongs in an independent implementation, not in an assertion about the implementation.
3. **Add one executing test per dispatch step.** See §2.
4. **Write the residue down as a partition, not as prose.** Name the unfinished set in a test.
5. **Re-check blockers before planning around them.** See §3.
6. **Let the ratchet subtract.**
7. **Distinguish "compiles" from "runs" from "renders correctly" in every claim.** The evidence tiers
   in `docs/gltf-renderer-pbr-fallbacks.md` exist because collapsing them is how a renderer table
   becomes fiction.
