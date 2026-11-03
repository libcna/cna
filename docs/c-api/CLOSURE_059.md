# ABI 0.59 exact-row implementation and qualification

This page preserves the ABI 0.59 snapshot. [CLOSURE_060.md](CLOSURE_060.md) records the later
2026-11-03 owner decisions and current four-row Windows qualification boundary. The seven owner
decisions described below as pending have since been resolved.

2026-11-03, MTH-CABI-009 / CABI-CLOSE-B. ABI 0.59 adds sixteen routes without changing
published layouts. Twenty-three exact declarations receive implementation mappings; eleven remain
planned after the 2026-11-03 owner execution and Follow-up 1 evidence review. No exclusions, route budgets, existing partial approvals or no-C-form decisions change.
The generated inventory records implementation, while the execution boundary below records which
new assertions have actually run. This is not an experimental release approval.

## Native cursors

`cna_gamer_enumerator_create_default` represents an ownerless native gamer cursor. The existing
bound gamer cursor now keeps the native enumerator instead of reconstructing one at an index.
Current returns its cached token, including a successful null before start and after exhaustion;
MoveNext/Reset refuse after mutation, even if count and element identities are restored. Releasing
the friend collection disposes/clears it, so subsequent cursor steps refuse its changed version.
The borrowed gamer token requires its original gamer handle to remain live.

`cna_model_effect_collection_create_enumerator` and
`cna_game_components_create_enumerator` accept an invalid source for a default cursor, or a valid
source for a native bound cursor. MoveNext, Reset, Current and destruction have separate routes.
Default MoveNext/Reset return `CNA_RESULT_INVALID_STATE`; null typed Current is a success. Each
bound cursor keeps its source alive. Mutation refusal leaves scalar output and cached Current
unchanged. Ordinary indexed access continues to read the live collection.

Effects return borrowed tokens and pin the cached effect against destruction until Current changes
or the cursor is destroyed. Components return independently owned Current views, released with
`cna_game_component_destroy`. A view keeps its native component and game alive after membership
removal, original-token destruction and cursor destruction. Boxed Current has its own route and
refuses before start/after exhaustion, matching the native interface. View destruction does not
unregister original membership. Add/Insert require original tokens; Contains/Remove/IndexOf and
component operations accept views. Callback functions/context must outlive all retaining views,
cursors and subscriptions, not merely the original token. Native collection-owned Add/Insert are
separate decisions and remain planned.

The native default-cursor null-reference refusal is represented deliberately as the C state error,
rather than the exception barrier's generic internal-error fallback. Opaque handles, typed release
and documented borrowing replace native C++ value-copy/boxed-pointer representation.

## Stream and declaration semantics

`cna_sound_effect_create_from_encoded_ext` now calls the inventoried
`SoundEffect::FromStream(System::IO::Stream&)` via a copied `MemoryStream`. It uses the native XNA
WAV parser, including PCM8/16, mono/stereo, 8–48 kHz, chunk/loop checks and whole-millisecond
duration. Int32 stream-length overflow refuses before allocation. The previous route called the
different permissive `std::istream` extension; formats that extension accepted beyond strict WAV
are no longer accepted. This and gamer Current's changed null/error behavior are intentional
experimental-minor compatibility changes, recorded in ABI_VERSIONING.md and the ABI baseline.

`cna_vertex_buffer_get_declaration` returns a live alias of the actual mutable declaration.
`cna_vertex_declaration_assign_ext`, `_dispose` and `_get_is_disposed` expose native resource
mutation. A second view and buffer metadata observe the assignment. Buffer destruction refuses
until all views are released. `cna_vertex_declaration_destroy` releases a view and disposes the
native declaration, just as it disposes a standalone declaration; it is not a detached layout copy.

## Exact implementation pins

| Native family | Exact IDs | Regression |
|---|---|---|
| SoundEffect stream | `CPP-772102AC1B85` | AudioSoundEffectSmoke: valid WAV must succeed with 100 ms duration; invalid RIFF size, format tag, channels, rate, alignment and bits refuse with zero output |
| Gamer default/Current/boxed Current | `CPP-B8F154CAE29F`, `CPP-91166B062F0A`, `CPP-B9A13B84C370` | GamersSmoke: default/null, traversal/reset/exhaustion, same-count mutation and cached Current, source disposal, null output/stale release |
| ModelEffectCollection enumerator | `CPP-AE1613EEB65F`, `CPP-A634E297F83B`, `CPP-D64BC09621CC`, `CPP-27C2C4E19A90`, `CPP-0895ED3EBC19`, `CPP-9C62086987CC`, `CPP-1D8E6057AE30`, `CPP-5AB5145FFC46` | GamersSmoke defaults; ModelMeshSmoke bound traversal, reset, mutation refusal, cached Current and independent indexed reads |
| GameComponentCollection enumerator | `CPP-65A7EF5BD183`, `CPP-053ABC01A2AC`, `CPP-9BADF5034E9E`, `CPP-38C968C51B1F`, `CPP-5A307E3EAFD7`, `CPP-2E664D30BB16`, `CPP-4178D51DF83A`, `CPP-6DBF2786215A` | GamersSmoke defaults; RuntimeComponentsSmoke boxed/typed Current, reset/exhaustion, same-count mutation, empty Clear version, retained Current after original/cursor release |
| VertexBuffer mutable declaration | `CPP-09107F5792D9` | VertexBufferSmoke: two views, live assignment/readback, self-assignment, invalid source/null output, disposal state and final-view destruction guard |
| Cube RenderTargetBinding constructor | `CPP-A0B649C7CD0A` | GraphicsSurfaceSmoke: descriptor bind/copy preserves NEGATIVE_Z, zero slice and exact cube target identity; executed in the focused owner run |
| Decoded Video destructor | `CPP-B6C659DC3E4D` | VideoSmoke: real decoded Video destruction, repeat-destroy refusal and preserved stale-token width sentinel; positive decoded-frame marker printed in the focused owner run |

## Remaining eleven rows

| Exact IDs | Why still planned |
|---|---|
| `CPP-BEE11E050E22`, `CPP-180E79B6B641`, `CPP-7B5289714D25`, `CPP-613C6A070826` | Four UTF-8/native filesystem-path overloads require Windows qualification. Linux/header cross-compiles do not supply it. |
| `CPP-A6A4484C5EFD`, `CPP-A06AE344A147` | CONTRACTS.md §27 reserves owner approval for collection-owned component membership and callback/context end conditions. Existing Add/Insert use explicit-token lifetime; cursor Current ownership does not change that. |
| `CPP-921CC48EF0E4`, `CPP-05AB6F76F931`, `CPP-574584031EA4`, `CPP-9F69184480D2` | CONTRACTS.md §31 reserves owner approval for guest/private-slot mutation and shared-machine identity. Existing machine routes copy values; they do not represent shared identity. |
| `CPP-30BE369B0F64` | CONTRACTS.md §28 reserves owner approval for logical-size provider replacement, callback results and context/teardown lifetime. Existing logical-size queries do not install a provider. |

No partial disposition is self-approved. Concrete proposals are recorded in the lane REPORT.md;
none is an inventory rule. The four Windows rows remain planned unconditionally in this lane.

## Validation and retained owner execution

Strict-C gamer/default-cursor and generated-route suites ran without a display and passed in this
lane. The new device-dependent assertions were **executed on 2026-11-03** by the integration
owner: the focused private SDLGPU run passed **6 / 0 failed / 0 skipped** across
AudioSoundEffectSmoke, ModelMeshSmoke, RuntimeComponentsSmoke, VertexBufferSmoke,
GraphicsSurfaceSmoke and VideoSmoke. VideoSmoke printed
`real frame identity: exercised against a decoded video`; this qualifies the actual decoded branch,
rather than optional decoder refusal. GraphicsSurfaceSmoke passed its cube descriptor face,
zero-slice and exact target-identity assertions. These two exact evidence rows are now implemented.

Retained verbose log: [owner-focus.log](/tmp/claude-1000/-rv-data-development-github-com-libcna-cna-tmp/8bc64a95-3ef2-4a9a-9163-86efed75e51e/scratchpad/codex/cabiclose-b/owner-focus.log), SHA-256
`c328b745d7292b1bd9c5aa3475b05b0b574f00fd779886586c339be4739c36ab`. It records private Xwayland/Weston execution and the six successful test commands.
The [owner summary](/tmp/claude-1000/-rv-data-development-github-com-libcna-cna-tmp/8bc64a95-3ef2-4a9a-9163-86efed75e51e/scratchpad/codex/cabiclose-b/owner-summary.txt), SHA-256
`cd97f88c352072ee935d097d06864463ff9b9ea73c83affa22ccb487aa4a5c24`, additionally reports the full CApi_ suite at **176/176** in
cmake-build-sdlgpu and **174/174 each** under runtime OPENGLES3, OPENGL33, SDLGPU, WEBGPU,
FNA3D and SOFTWARE in cmake-build-multi. Those broader counts are owner-reported retained
execution, distinct from the inspected focused verbose log; no renderer run was launched by this lane.

The focused suite set can be reproduced with:

```sh
tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu \
  -R '^CApi_(AudioSoundEffectSmoke|ModelMeshSmoke|RuntimeComponentsSmoke|VertexBufferSmoke|GraphicsSurfaceSmoke|VideoSmoke)$' -V -j4
```

All 26 declared header compatibility cells compile (1,664 translation units). Follow-up 1 reran
coverage, route-name coverage, generation, release-record, documentation/export, ABI baseline and
header compatibility gates; all passed. Exact commands and outcomes are appended to the lane REPORT.md.
The gate remains **Not ready** for the seven owner-decision rows and four Windows path rows.
No partial owner approval is inferred from successful execution.

No GPU/window run, wasm build, Windows execution, installed-consumer run, publication or Git
mutation was performed by this lane. The wasm export list is generated from the public headers;
the regenerated artifact is retained in the lane directory for inspection. ABI 0.59 still exports
3,394 symbols with unchanged layouts; this follow-up adds evidence mappings, not public routes.
