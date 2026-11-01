# Avatar art direction

This page keeps the original-art constraints and the visual direction of CNA's current avatar
catalog. The runtime/catalog contract is [avatars.md](avatars.md); the checked-in recipe is
`tools/avatar_builder/generate_avatar_catalog.py` and its `cna_avatar/` modules.
Catalog v3 revision 5 is the current asset revision.

## Owner constraints

The avatar decisions recorded in the original Net campaign require:

- Toy-like, Xbox-Avatar-inspired stylization using fully original CNA geometry and textures.
- **Never use proprietary Xbox Avatar assets, even for private reference or measurement.**
- Intentional, coherent body/head, hair, clothing and color choices rather than malformed figures.
- Front/side/back review for both body types, an animation gallery, no mesh explosions and no
  distorted limbs.

The stylization names a broad category of approachable, simplified characters. Asset provenance
must remain original; it is not permission to inspect, trace or reproduce a proprietary model.

The historical decision table also named `../mesh-craft` for the next geometry pass. That tool
request and the retired Blender baseline are historical records, not an accurate description of
the current checked-in Python catalog generator. Changes to art should identify the actual recipe
and evidence they use.

## Current proportions

The original six-head-height proposal predates catalog v3's present sculpted bodies and is
superseded as a description of the shipped art. Revision 2 was about 4.6–4.8 heads tall; revisions
3–5 use the refined head. The revision-5 audit rerun on 2026-11-01 measured:

| Measurement | Female body | Male body |
|---|---:|---:|
| Authored body height | 1.679 m | 1.797 m |
| Body height in head-heights | 4.78 | 4.94 |
| Chin-to-crown head height | 0.351 m | 0.364 m |
| Shoulder width in head-heights | 1.42 | 1.56 |

These are measurements of the authored body, not a requirement that every randomized height/build/
proportion morph reproduce the same ratios. Hair, hats and pose can change an apparent silhouette.
Use a new `avatar_audit.py` run for later asset work rather than assuming these measurements
remain current after a generator/catalog change.

The visual target is a recognizable stylized adult: a large but supported head, a defined neck and
shoulders, shaped torso, tapered limbs, and hands/feet that belong to the same figure. Limb length
and thickness must be assessed together in front/profile/back views and while animated.

## Visual language

| Area | Direction |
|---|---|
| Head | A coherent sculpted surface with forehead, brow, cheeks, jaw and chin; nose, sockets and lips read in profile rather than as attached primitives. Facets concentrate around the face. |
| Eyes and mouth | Eyeball/lid relief follows the atlas opening; the upper lid/crease and lower lid have distinct forms. Sculpted lips follow the painted parting line, with tucked corners and soft tint edges. |
| Brows and ears | Brows read as hair strokes with a tapered edge. Ears have a helix, bowl, antihelix, tragus and lobe; earrings attach to the chosen ear's lobe. |
| Hair | Clumped masses with ordered flow, thickness, grooves and deliberate tips; short styles end at a coherent hairline and long styles hang from the head. Avoid crossing/folded strips and scalp shells protruding through short hair. |
| Clothes | Garment-shaped volumes with fit, ease, thickness, collars, cuffs, hems and restrained folds. Sleeves/trousers cover their limb envelope through bends; hems must remain above underlying clothing during crouches. |
| Shoes | Recognizable heel, toe, instep and sole, with closed seams and purposeful openings for sandals. Avoid pointed-cone feet. |
| Hands | Palm thickness reaches the knuckles, fingers curl in a cascade, and the thumb pad has a clear transition to the wrist. |
| Face variation/makeup | Face presets move related forms together. Makeup uses soft washes except intentional liner; colors and features remain compatible with the catalog's expression tiles. |
| Surfaces | Broad readable forms and restrained relief/fabric detail, supported by the shared textures and current SkinnedEffect lighting. Material specular strength/sharpness belongs to its surface family. |

Revision 4 refined eyes, mouth, brows, head-texture relief and material shininess.
Revision 5 retained item identities while correcting earring attachment on different ears and
top/skirt weighting around deep crouches. Those changes are recorded in Git history; the table
above is the continuing visual direction, not a claim that every possible combination is flawless.

## Topology and deformation acceptance

Review proportions, topology and skinning separately. A proportion change cannot repair intersecting
hip fans or rigid single-bone seams at a joint.

- Body/limb joins need coherent closed forms without visible degeneracy or self-intersection.
- Joints need smooth multi-bone transitions across a real blend region; check shoulders, elbows,
  hips, knees, ankles and expressive hand poses.
- Garment, shoe, hair and headwear fit must be checked on both bodies and at relevant morph
  extremes, including deep crouches and hat/hair combinations.
- Earring position must follow every ear shape; glove/ring hiding and accessory-side rules must
  agree with the assembled result.

`avatar_audit.py` measures authored-body proportions, geometry budgets and bind-pose skin
show-through. Its default refusal requires both more than 2% show-through and more than 30
vertices; this tolerance is not proof of watertight topology, every animation pose or visual
acceptance. Open footwear is an intentional modeling case, not a reason to reinterpret every
detected intersection as acceptable.

The 2026-11-01 audit passes that default bind-pose budget for catalog v3 revision 5. It does not
prove every posed/morphed combination or replace the separate visual acceptance criteria.

## Review evidence

Use `avatar_review.py` sheets for portraits, profiles, presets, bodies, eyes, mouth, temples,
hands and wardrobe, plus random avatars drawn by `cna_avatar_review`. Review both body types
and front/side/back views, and run the animation gallery described in
[avatar-demos.md](avatar-demos.md).

The visual regression manifest/goldens and `avatar_acceptance.py` provide separate structural,
pixel and runtime checks. Passing static catalog metadata does not establish a fresh art review.
Intentional changes need inspected updated renders and the existing geometry/pose regressions,
rather than only regenerating a golden file.

The retired Blender baseline screenshots, early six-head proportion table and completed task
numbers remain in Git history. They explain why the art changed and are not an active repair list
for today's catalog.
