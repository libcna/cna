# REMEDIATION_TRACEABILITY.md — Audit Finding → Task Mapping

Proves that every actionable audit finding is accounted for, and shows where deduplication collapsed
many findings into one task.

## Method

Two independent passes, deliberately not merged into one:

1. **Synthesis pass** — read `AUDIT_FINDINGS_INDEX.md` (758 lines), `AUDIT_CROSS_CUTTING_FINDINGS.md`
   (2689 lines), `AUDIT_GRAPHICS_BACKEND_MATRIX.md`, `AUDIT_FINAL_REPORT.md`, `AUDIT_DECISIONS.md`,
   `AUDIT_EXCEPTIONS.md` in full.
2. **Exhaustive per-file pass** — all 542 per-file `*.audit.md` reports containing a severity-tagged or
   `F<n>`-numbered finding, out of 2297 total reports, swept in 6 parallel batches.
   **686 raw finding records** extracted.

The second pass existed to answer one question: *does the audit's own synthesis layer capture
everything its per-file layer found?* It does not — see § Synthesis gap.

## Raw finding volume

| Source batch | Reports read | Findings extracted |
|---|---|---|
| `src/` | 67 | 142 |
| `include/` | 56 | 81 |
| `examples/` batch 1 | 129 | 135 |
| `examples/` batch 2 | 125 | 138 |
| `examples/` batch 3 | 129 | 145 |
| `docs/` + `tests/` + `tools/` | 36 | 45 |
| **Total** | **542** | **686** |

By severity across all batches: **0 CRITICAL** (the one CRITICAL was raised at synthesis level from
Pass 6 runtime evidence, not in a per-file report), **76 HIGH**, **281 MEDIUM**, **297 LOW**, **32 INFO**.

Roughly 40% of the `examples/` findings are test-authoring defects rather than production bugs — expected,
since that tree is predominantly backend integration tests.

## Deduplication: 686 raw → 104 tasks

**582 raw findings collapsed (85%).** The four largest collapses:

| Root cause | Raw findings | Task | Why they collapse |
|---|---|---|---|
| SkinnedEffect world-space normal transform | ~24 | `REMED-GFX-006` | One conceptual mistake, copy-ported across 14 backends and 2 variants. Traceable via self-documented "ported line-by-line" comments in 3 of them. |
| Fog formula mirrored | ~22 | `REMED-GFX-005` | One wrong formula in 4 backend groups, ~20 shader files, each with its own per-file report. |
| Raw `std::` exceptions | ~60 | `REMED-CORE-002` | One missing convention across ≥10 areas and dozens of files. |
| Stale "known bug" comments | ~35 | `REMED-DOCS-001` | One process gap (behavior fixed without sweeping comments) across 4 mechanical batches. |
| EnvironmentMapEffect emissive re-multiply | ~14 | `REMED-GFX-007` | One formula error in 5 backend groups. |
| Center-pixel-only / weak test assertions | ~40 | `REMED-TEST-005` + test requirements on GFX-005/-006/-007/-011 | One test-design blind spot; partly folded into the tasks whose verification requires fixing it. |
| `NetworkSession` dispose-without-delete | 10 | `REMED-NET-007` | One ownership-contract gap in 10 files. |
| Sensor `Dispose(bool)` public | 4 | `REMED-DEVICES-002` | One declaration mistake in 4 sibling classes. |
| `fs::path` containment | 3 | `REMED-CONTENT-002` | One C++ pitfall in 3 unrelated subsystems. |

Other collapses of note: `PackedVector` Pack rounding (3 types → `REMED-GFX-033`); `GetTypeName()`
omissions (4 classes → `REMED-GFX-050`); `GetHashCode()` overflow (4 types → `REMED-CORE-005`);
`WILL_FAIL` absence (6+ tests → `REMED-BUILD-003`); duplicate NOXNA surfaces (2 features × 2 namespaces
→ `REMED-DEVICES-003`).

## Consistency check — every severity accounted for

### CRITICAL — 1 of 1 mapped

| Finding | Task |
|---|---|
| Malformed/mutated `.xnb` `Texture2D` crashes Vulkan (stack smashing) and WebGPU (Rust panic) | `REMED-CONTENT-001` |

### HIGH — all mapped, none dropped

Every HIGH in `AUDIT_FINDINGS_INDEX.md` maps to at least one task. Full mapping:

| HIGH finding (index § or cross-cutting §) | Task |
|---|---|
| `gtest_discover_tests` no `WORKING_DIRECTORY` | `REMED-BUILD-001` |
| `cna_demo_xact` Content-copy build failure | `REMED-BUILD-002` |
| `MediaLibraryTestFixture` SEGFAULT | `REMED-MEDIA-002` |
| EasyGL `SetRenderTargets` 2 attachments | `REMED-GFX-016` |
| D3D11 specular asymmetry + black vertex color | `REMED-GFX-020` |
| D3D12 single-CTest coverage gap | `REMED-BUILD-008` |
| SdlGpu rejects EasyGL `ShaderEffect` GLSL | `REMED-GFX-035` |
| Bgfx default cull mode fails | `REMED-GFX-017` |
| Task 868 confirmed FIXED (matrix + cmake need updating) | `REMED-DOCS-001`, `REMED-DOCS-002` |
| Fog formula mirrored (Bgfx/Vulkan/D3DCommon) | `REMED-GFX-005` |
| SkinnedEffect world-normal transform, all backends | `REMED-GFX-006` |
| SdlGpu fog unimplemented | `REMED-GFX-009` |
| D3D12 Stencil/Scissor/DepthBias inert | `REMED-GFX-014` |
| D3D12 `OcclusionQuery` multi-draw overwrite | `REMED-GFX-015` |
| Vulkan `SpriteBatch` transform dropped | `REMED-GFX-012` |
| Vulkan missing Y-flip, 4 families | `REMED-GFX-011` |
| Vulkan scissor inert when RT bound | `REMED-GFX-013` |
| `EnvironmentMapEffect` emissive re-multiply, 5 backends | `REMED-GFX-007` |
| `SkinnedEffect` Ambient/Emissive misconsumed | `REMED-GFX-008` |
| WebGPU `SpriteBatch` clip space backbuffer-relative | `REMED-GFX-019` |
| Bgfx `Clear` ignores `ClearOptions` | `REMED-GFX-018` |
| EasyGL `RegisterForWindow` dangling registry (UAF) | `REMED-GFX-001` |
| D3D9 custom-shader object-space fog + PBR skinned normal | `REMED-GFX-010`, `REMED-GFX-006` |
| `BgfxVertexFormatHelper` dead code | `REMED-GFX-047` |
| State-mutation-before-fallible-call, 3 instances | `REMED-GFX-027` (2 XNA-layer) + `REMED-GFX-001` (backend) |
| `SpriteFont`/`SpriteBatch` `end()` deref | `REMED-GFX-002` |
| `SpriteBatch::DrawString` undersized `SpriteEffects` table | `REMED-GFX-003` |
| `EffectParameter` Matrix inverted | `REMED-GFX-022` |
| `EffectParameter` Elements/StructureMembers empty | `REMED-GFX-023` |
| `BasicEffect` never populates `Parameters` | `REMED-GFX-024` |
| `VertexBuffer`/`IndexBuffer` no destination offset | `REMED-GFX-025` |
| `GraphicsDevice.cpp` ~27 raw exceptions | `REMED-CORE-002` |
| `CNA::Logger::ToSDLPriority()` | `REMED-CORE-001` |
| `FileDialog`/`MessageBox` UAF | `REMED-DEVICES-001` |
| `StorageDevice::DeleteContainer()` recursive delete | `REMED-CONTENT-002` |
| `ENetBackend` no host-authority check | `REMED-NET-001` |
| `AudioTagParser` integer overflow (32-bit) | `REMED-MEDIA-001` |
| `TextureCubeContentTypeReader` missing validation | `REMED-CONTENT-003` |
| `ContentReader::ReadExternalReference` absolute-path bypass | `REMED-CONTENT-002` |
| `GameTests`/`GraphicsDeviceManagerTests` zero coverage | `REMED-TEST-002` |
| `Game::UnloadContent()` dead hook | `REMED-CORE-006` |
| `GraphicsDeviceManager` event-forwarding gap | `REMED-CORE-007` |

**No HIGH finding carries a "no action" disposition.** Every one maps to real work.

### MEDIUM — all accounted for

All 55 MEDIUM-severity tasks plus MEDIUM findings folded into larger tasks. The MEDIUM findings that
became **no-action** items rather than tasks are `REMED-NA-001` through `REMED-NA-015`, each with a
stated reason in `MASTER_REMEDIATION_PLAN.md` § Accepted / no-action items. Notably:

- `StorageContainer`'s unchecked path joins → `REMED-NA-005`, **confirmed FNA-faithful**, explicitly
  excluded from `REMED-CONTENT-002`'s scope despite superficial similarity to `DeleteContainer`.
- `.Content`'s 5 absent `ContentSerializer*Attribute` types → `REMED-NA-001`, a deliberate
  architectural consequence of CNA's hand-written reader registration.

### LOW — all accounted for

17 LOW tasks; the remainder are folded into thematic tasks (`REMED-DOCS-001`, `REMED-TEST-005`,
`REMED-CORE-013`, `REMED-GFX-049`) or recorded as no-action. The largest LOW cluster — recurring
test-authoring patterns across the 218-file EasyGL shard and the Bgfx/Vulkan/SdlRenderer batches — is
split deliberately between `REMED-TEST-005` (general hygiene) and the specific test requirements
attached to `REMED-GFX-005`/`-006`/`-007`/`-011`, because those particular weaknesses **must** be fixed
for those fixes to be verifiable at all.

### Cross-cutting findings — all represented

| Cross-cutting theme | Task |
|---|---|
| Bug propagation via explicit cross-backend porting | Addressed structurally: `REMED-GFX-005`/`-006`/`-007` each fix all instances under one owner, and each requires correcting the false precedent comments that caused the propagation |
| Known limitations disclosed, not hidden (positive) | **Preserved deliberately.** `REMED-BUILD-003` adds machine-readability without removing the existing in-source disclosure discipline |
| Documentation rot | `REMED-DOCS-001`, `REMED-DOCS-002` |
| Raw `std::` exceptions | `REMED-CORE-002`, `REMED-CORE-003` |
| `.gitignore` `build*` hazard | `REMED-BUILD-006` |
| CI-masking risk | `REMED-BUILD-003`, `REMED-BUILD-004` |
| Silent-default-degradation in `IGraphicsBackend` | `REMED-GFX-026` (+ noted in `REMED-GFX-012`, `-036`, `-051`) |
| `fs::path` concatenation pitfall | `REMED-CONTENT-002` |
| State-mutation-before-fallible-call | `REMED-GFX-027`, `REMED-GFX-001`, `REMED-GFX-029` |
| Duplicate NOXNA surfaces | `REMED-DEVICES-003` |
| Tests that bake in the bug | `REMED-TEST-001` |
| Center-pixel-only test blind spot | `REMED-TEST-005` + GFX task test requirements |
| SPDX inconsistency | `REMED-BUILD-007` |
| FNA-faithful-but-undocumented | `REMED-CORE-013`, `REMED-NA-004`, `REMED-NA-005` |

### Pass 3 API-surface gaps — all 7 mapped

| Gap | Task |
|---|---|
| `DisplayMode.TitleSafeArea` / `ToString()` (MEDIUM) | `REMED-GFX-044` |
| All 16 `PackedVector` types lack `Equals`/`GetHashCode`/`ToString` (MEDIUM) | `REMED-GFX-034` |
| `VertexPositionColor` missing `IVertexType` (re-confirmation) | `REMED-GFX-042` |
| `GraphicsDeviceInformation` missing `Equals`/`GetHashCode` (LOW) | `REMED-NA-003` (FNA-inherited) |
| `AudioCategory.ToString()` (LOW) | `REMED-AUDIO-002` |
| `NetworkSession.MaxSupportedGamers`/`MaxPreviousGamers` NOXNA-mistagged (LOW) | `REMED-CORE-012` |
| `KeyboardState::ToString()` NOXNA-mistagged (LOW) | `REMED-CORE-012` |

## Synthesis gap — findings recovered only by the per-file sweep

**Three findings exist in per-file audit reports but appear in no synthesis document.** They were
correctly recorded by the audit's per-file work and simply never propagated upward into
`AUDIT_FINDINGS_INDEX.md` or `AUDIT_CROSS_CUTTING_FINDINGS.md`.

| Finding | Per-file source | Task | Why it matters |
|---|---|---|---|
| `XnbTypeName::ParseOne()` unbounded recursion → stack-exhaustion DoS from a crafted `.xnb`; `XnbReadLimits::maxObjectNestingDepth` and `maxStringBytes` are **declared security controls with zero consumers** | `XnbReadLimits.hpp.audit.md` F1/F2, `XnbTypeName.hpp.audit.md` F1, `XnbTypeReaderTable.hpp.audit.md` F1 | `REMED-CONTENT-006` | **A security finding.** A declared-but-unenforced control is worse than none, because reviewers rely on it. Scheduled P0. |
| `GraphicsDevice::DrawUserPrimitives`'s explicit `VertexDeclaration` overload never propagates the declaration to the backend | `easygl_draw_user_primitives_custom_test.cpp.audit.md` F1 | `REMED-GFX-043` | A documented XNA overload silently ignores its own parameter. HIGH, scheduled P1. |
| Several weak-test patterns (undocumented retry loops producing **false PASS**, alpha never compared, non-analytically-derived goldens) | multiple `examples/*.audit.md` | folded into `REMED-TEST-005` | These are why several P1 defects survived a green CI |

**Implication for the audit baseline:** the per-file layer is more complete than the synthesis layer.
Anyone using `AUDIT_FINDINGS_INDEX.md` alone as the definitive finding list will miss these. The
per-file reports remain the authoritative evidence.

This does not undermine the audit — its per-file discipline is exactly what made recovery possible.
But it is a real limitation of the synthesis documents, recorded here rather than glossed over.

## Invariants verified at plan close

| Invariant | Result |
|---|---|
| Every CRITICAL finding maps to ≥1 task | ✅ 1/1 |
| Every HIGH finding maps to ≥1 task or a justified non-action | ✅ all mapped to real tasks; zero non-action |
| Every MEDIUM finding accounted for | ✅ task or explicit `REMED-NA-*` |
| Every LOW finding accounted for | ✅ task, folded, or `REMED-NA-*` |
| Every actionable cross-cutting finding represented | ✅ 14/14 themes |
| Duplicate tasks consolidated | ✅ 686 → 104 (85% collapse) |
| No root cause assigned to multiple owners | ✅ 9 cross-subsystem root causes each have exactly one owner (see `REMEDIATION_DEPENDENCIES.md`) |
| All task references point to concrete audit evidence | ✅ every task cites a file, section, or per-file report |
| **No production code modified** | ✅ verified — `git diff HEAD -- audit` empty, `git status` shows only untracked `remediation/`, HEAD unchanged at `74ebf356` |
| `audit/` left frozen | ✅ zero files created, modified, or deleted under `audit/` |
