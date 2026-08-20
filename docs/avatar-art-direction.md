# Avatar art direction (Phase 7)

## Acceptance criteria (verbatim, from `plans/plan_net.md`'s User Decisions table)

| # | Topic | Decision |
|---|-------|----------|
| 4 | Avatar visual target | **Toy-like Xbox-Avatar-inspired** stylization, fully original CNA geometry/textures. |
| 4a | Proprietary Xbox Avatar assets | **Never use them, not even for private reference/measurement.** |
| 4b | Body types/hair/clothes/colors scope | Improve what `demo_avatar`/the asset pipeline already generates so it looks intentional, not "monsters." Body/head (and other) geometry should be generated as glTF via the sibling `../mesh-craft` tool going forward. |
| 4c | Phase 7 acceptance criteria | Accepted as originally stated: front/side/back screenshots, male + female, animation gallery, no mesh explosions, no distorted limbs. |

"Toy-like Xbox-Avatar-inspired" is a stylization *category* (approachable, slightly simplified,
not photorealistic — the same general category as countless non-proprietary toy/collectible
figure lines and stylized game avatars), not a reference to any specific proprietary asset.
Nothing in this document, or in any geometry generated from it, was derived by examining,
measuring, or reverse-engineering a real Xbox Avatar asset (decision 4a — zero exceptions).

## Baseline evidence (Task 7.1)

Six static screenshots (`male`/`female` × `front`/`side`/`back`, T-pose) and one mid-animation
capture (`male`, `Wave` clip, ~1.5s in) were captured via `cna_demo_avatar`'s new
`--yaw`/`--clip`/`--screenshot` flags. Three distinct, independent problems are visible, not one:

1. **Proportions** — arms are roughly 1.6-1.8× too long relative to the torso (in the T-pose
   screenshots, each arm's screen-space length is close to the *combined* torso+leg length) and
   noticeably too thin (uniform-diameter tubes, no shoulder/bicep/forearm taper at all). The head
   is small relative to the body. The torso reads as short and boxy.
2. **Broken topology** — the hip/crotch area shows dark, jagged, self-intersecting triangle
   fans in every screenshot, at every angle, regardless of pose. Feet taper to sharp points
   instead of reading as shoes. These are mesh-construction defects, not proportion problems —
   fixing proportions alone will not fix them.
3. **Skinning/deformation** — the mid-animation (`Wave`) capture shows severe dark ring artifacts
   at *every* joint (shoulder, elbow, hip, knee, ankle): a classic symptom of each mesh segment
   being rigidly single-bone-weighted with a hard boundary at the joint (a "candy-wrapper" pinch)
   rather than smoothly blended across 2+ bones near the joint. This is a skinning-weight problem
   (Task 7.8's own scope), independent of both proportions and topology.

All three must be addressed for Task 7.11's "no mesh explosions, no distorted limbs" criterion to
be genuinely met — proportion/topology fixes alone (mesh-craft geometry) will not fix the ring
artifacts, and vice versa.

## Proportion targets

Expressed in **head-heights** (the classic, non-proprietary figure-drawing/character-design unit —
"how many times does the head's own height fit into the total body height"), so they scale to
whatever absolute unit `generate_body.py` uses internally. Chosen for a "toy-like, approachable,
still recognizably human" stylization: noticeably more head-heavy than realistic human proportion
(~7-7.5 heads tall), but not chibi/super-deformed (~2-4 heads tall) — a well-established, widely
used middle stylization used across countless non-proprietary toy-figure and stylized-character
design traditions, chosen independently of any specific franchise.

| Measurement | Target | Rationale |
|---|---|---|
| Total height | 6.0 head-heights | The toy-like midpoint described above. |
| Head height | 1.0 (reference unit) | — |
| Neck length | 0.15 | Short — reads as friendly/approachable, avoids a "swan neck" look. |
| Shoulder width | 1.6 head-widths (head-width ≈ 0.8 × head-height) | Wide enough to read as shoulders without looking blocky. |
| Torso length (shoulder to hip) | 2.1 | Roughly a third of total height — the current baseline's torso reads noticeably shorter than this. |
| Hip width | 1.3 head-widths (male), 1.35 head-widths (female) | Slightly narrower than shoulders on both; female hip:shoulder ratio closer to 1:1 than male's ~0.8:1, the one deliberate per-gender silhouette difference beyond existing texture/hair/clothing variation. |
| Arm length, shoulder to wrist | 2.6 | Sized so **T-pose fingertip-to-fingertip span ≈ total height** (the standard general human-proportion rule of thumb, applied at this stylization's own scale) — roughly two-thirds of the current baseline's apparent arm length. |
| Upper arm : forearm ratio | 1 : 1 | Even split; avoids the current uniform-tube look by giving the elbow a real reason to exist as a taper point. |
| Arm thickness (bicep diameter) | 0.35 head-widths, tapering to 0.22 at the wrist | Current baseline is a uniform ~0.15 head-widths for the *entire* arm length — this is the single biggest contributor to the "breadstick arm" look. |
| Leg length, hip to ankle | 3.0 | Exactly half of total height — another standard rule of thumb. |
| Thigh : shin ratio | 1 : 1 | Even split, matching the arm's own upper:lower ratio for visual consistency. |
| Leg thickness (thigh diameter) | 0.55 head-widths, tapering to 0.3 at the ankle | Legs read reasonably in the baseline already; keep roughly current thickness but add the taper the arms are missing. |
| Foot length | 0.75 head-widths | Long enough to read as a shoe, not a point — directly fixes the "pointed cone foot" defect. |

## Topology/skinning requirements (not proportions, but equally required by decision 4b)

- No degenerate/self-intersecting triangles at the hip, crotch, shoulder, or ankle joins — the
  body must be a single closed, manifold-per-limb mesh at every join, not overlapping fan
  primitives (root cause of the dark faceted crotch artifact in every baseline screenshot).
- Every joint needs **smooth multi-bone vertex weighting** across a real blend region (a band of
  vertices spanning the joint, not a hard single-bone boundary) — this is what actually fixes the
  Wave-pose ring artifacts; geometry changes alone cannot.
- Feet need distinct heel/toe/instep geometry, not a tapering cone.

## Explicit non-goals for this pass

- Matching any *specific* published character's proportions, silhouette, or texture style,
  proprietary or otherwise — "toy-like, approachable, human-recognizable" is a category, not a
  target to imitate.
- Photorealism of any kind.
