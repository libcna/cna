# Audit: tools/avatar_asset_pipeline/convert_avatar.py

## Metadata
- Source file: `tools/avatar_asset_pipeline/convert_avatar.py` (447 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-avatar-asset-pipeline` shard
- File type: Python script (offline asset converter)
- XNA/FNA relevance: none directly — feeds `AvatarRenderer`'s real-rendering extension (NOXNA)
- Main related tests: N/A (offline developer tool; verified via manual runs per its own README)

## Purpose
Converts a MakeHuman body export (or CNA's own procedurally-generated `.glb`) plus Mixamo/embedded
animation clips into CNA's `.skinnedmodel.json`/`.skeleton.bin`/`.clip.bin` content format for
`AvatarRenderer`.

## Executive Verdict
Correct for the cases it has actually been verified against (a synthetic fixture and real generated
content from CNA's own Blender pipeline, both documented in the paired README as round-tripping
correctly end-to-end through the real C++ engine). One LOW/plausible edge case identified in
`build_node_hierarchy()`'s topological-sort logic that neither this file's own extensive bug-history
comments nor the paired README's verification history appears to explicitly cover — flagged as a
"worth double-checking against a fixture with the relevant shape," not a confirmed defect.

## Checklist Results
- `_invert4x4()`'s Gauss-Jordan elimination (lines 350-366) uses partial pivoting (`max(...,
  key=lambda r: abs(aug[r][col]))`) for numerical stability, and raises `ValueError` on a singular
  matrix rather than silently returning garbage — correct, defensive numerical code.
- `joint_index_remap` (line 182, `[node_to_bone[node_idx] for node_idx in skin.joints]`) is applied
  consistently to both `inverse_bind_global` (line 200) and every vertex's `JOINTS_0` indices
  (`_write_skinned_vertex_buffer`, line 386) — the same remap used in both places, correctly
  preventing the "bind pose remapped but vertex weights not" class of bug the file's own comment
  (lines 176-181) explains was found and fixed once already.
- `bind_pose_local`'s derivation-from-`inverse_bind_global` approach (lines 214-221) is explicitly
  chosen over an independent per-joint-TRS derivation specifically because the latter was found to
  disagree with itself at the rest pose (lines 201-213) — deriving from the already-verified-correct
  `inverse_bind_global` via matrix inversion is "correct by construction," a genuinely sound
  engineering choice to sidestep needing to find the original bug.

## Detailed Findings

### LOW — `build_node_hierarchy()`'s parent-lookup only checks the *immediate* scene-graph parent for joint-set membership, potentially dropping a joint's real ancestor relationship through an intermediate non-joint node
```python
node_parent = {}
for node_idx, node in enumerate(gltf.nodes):
    for child_idx in (node.children or []):
        if child_idx in joint_set:
            node_parent[child_idx] = node_idx if node_idx in joint_set else -1
```
(lines 93-98). If a joint's real glTF scene-graph parent is itself a joint, `node_parent` correctly
records that relationship. But if a joint's immediate parent is a *non-joint* transform node (e.g.
an intermediate "empty"/helper node between two joints in the scene graph — not itself part of
`skin.joints`), this code assigns that joint's `node_parent` entry to `-1` (treated as an unparented
root) rather than walking further up the scene-graph chain to find the nearest joint ancestor. Any
translation/rotation baked into that intermediate non-joint node would then be silently dropped from
the bind-pose hierarchy for that joint's subtree.

**Confidence and scope**: this is a plausible latent gap in the algorithm's design, not a
confirmed reproduction — I did not construct a test fixture with this exact shape (an intermediate
non-joint node between two joints) to verify it triggers. The paired README documents this script
successfully round-tripping both a synthetic fixture and real content from CNA's own procedural
Blender pipeline (`tools/avatar_builder/`) through the real C++ engine, which suggests neither of
those two content sources happens to exercise this shape — MakeHuman's "Mixamo" skeleton preset and
CNA's own generator likely make every skeleton node a joint with no non-joint intermediates. Worth a
targeted check (or a documented "not applicable to this project's actual content sources" note) if
a future content source ever introduces non-joint intermediate transform nodes in a rigged model.

## Cross-File Observations
See `tools/avatar_asset_pipeline/README.md.audit.md` for the four confirmed-and-fixed real bugs this
script's own comments document — none of them match the specific edge case flagged above, suggesting
it either doesn't occur in the content sources tested so far, or hasn't yet been exercised.

## Missing or Weak Tests
No unit test was located for this script (consistent with the project's documented "offline,
manually-verified tool" convention for this class of asset pipeline) — a small synthetic-fixture
test specifically constructing a skin with a non-joint intermediate node would directly resolve the
LOW finding above's open question.

## Positive Findings
The `bind_pose_local`-derived-by-construction fix and the consistent, single-source-of-truth
`joint_index_remap` application are both genuinely sound engineering responses to real, previously-
observed bugs, not superficial patches.

## Final Assessment
One LOW, unconfirmed-but-plausible finding: `build_node_hierarchy()` may drop transform information
through a non-joint intermediate scene-graph node, not verified to actually occur in this project's
tested content sources.
