# CNA final 21-lane reconciliation and stabilization

Date: 2026-08-09

Candidate branch: `integration/post-audit-phase1`

Decision: **READY FOR DEVELOP MERGE**

The integration campaign itself is complete: exactly 21 accepted lane merges are present, no lane
is missing or duplicated, Batch 0 through Batch 6 history is intact, and the public registry has
exactly 41 identities. `FINAL-STAB-001` closed the three final-tree build/sanitizer failures and
the bounded retake is green. No merge into `develop`, push, force operation, history rewrite,
stash operation, new renderer work, modularization, or `audit/` change occurred.

## 1. Starting state and safety

| Repository / branch | Worktree | Head at start | Status at start |
|---|---|---|---|
| CNA integration | `/rv/data/development/github.com/openeggbert/cnaintegration` | `012b158eb8246ce267887acbd4fc7a2468d89e52` | clean |
| CNA planning `feature/audit` | `/rv/data/development/github.com/openeggbert/cnaaudit` | `449316aaa0cc8bcc903d513ee4a2403672b9af83` | tracked-clean; owner `?? AGENTS.md` |
| CNA `develop` | `/rv/data/development/github.com/openeggbert/cna` | `ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c` | pre-existing tracked and untracked owner changes |

The `develop` dirt is `cmake/Tests/EasyGLTests.cmake`,
`cmake/Tests/SdlRendererTests.cmake`, `AGENTS.md`, and
`examples/xvfb_screenshot_demo.cpp`. This session did not touch it. Git listed 26 worktrees,
including one stale/prunable `/tmp/cnaaudit-gfx098-prefix` entry; none was pruned.

The integration and planning `audit/` subtree remained
`168c9b668763b78e63106e27d942a76d2457f41d`. The four protected stash objects, newest first,
remained exactly:

1. `888c3dcc8fb4fc6949bf3790a1483862328b6033`
2. `d3b92226e00deb239c7587592c0c5bfc73078aaf`
3. `5623d2202fea60b64eb50afa120745595b75d89b`
4. `8f8b8f55c647eb9e57a14093e4f5e30f55fe4157`

GPG preflight succeeded for fingerprint
`255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`.

The bounded `FINAL-STAB-001` continuation began from exact CNA head
`b6ea782fdc35ea02ceba755a48cf8a17e30c9112`, with a tracked-clean index, no lock/writer, and the
same `audit/` subtree. Its sibling sharp-runtime prerequisite is signed commit
`1e51c2d869697fd827af7ca342ffabf77d30faf8`; that worktree was clean and the commit verified Good
under the same fingerprint before CNA consumed its public capability macro.

## 2. Exact lane inventory

Notation: O = original ref/head; AR = signed CNA archive tag object → original; AD = adaptation
head; M = accepted first-parent integration merge. All 21 rows are **accepted**.

| # | Lane | Batch / group | O | AR | AD | M | Carried state / final blocker |
|---:|---|---|---|---|---|---|---|
| 1 | depthcrt | B0 / A | `feature/depthcrt@f4804469a6c14fac6215965794ba6786fc6c5b48` | `archive/preintegration/depthcrt-20260804@796c0c16a12abd8c467a02b4252baf7a10aaf2c2` → O | `3cca0b190e6ed0a33fb2023f6e0952d4ee65de7c` | `61bd1a1b6c81e299251443e738699908af158e1f` | none |
| 2 | gltf | B0 / A | `feature/gltf@86ada7a7bdc7c8e76fff536be4f6c1f5bff3df43` | `archive/preintegration/gltf-20260804@15f584e5ef548c48608d000df6c9b620788a1e51` → O | direct | `722a2f5adb07a0e75616c72ebc528ca628b19198` | none |
| 3 | ext | B0 / A | `origin/feature/ext@05ab5d3d002945c603fc28f2a5a23f8027773d63` | `archive/preintegration/ext-20260804@90bde6b38eb9cb24d9462e7b915e95e9b7af820e` → O | `c6a280367eff3ac555f5af03c95ae3f1dce86dd2` | `8a374b9f81d4a48779d5cdfb609f84a5007fdda3` | none |
| 4 | dxold | B0 / A | `feature/dxold@36289bb2eec7470fac53c2ff517181fe3ecf9af2` | `archive/preintegration/dxold-20260804@afa009271a4a3e19006f1ef3fa6e8200388a2d68` → O | `9256e6069bfb792e3ac7517456ed313522c2c7d3` | `990d6b8aa42fffe8525f5daf98f0737a31d7af4f` | FreeDirect pixel residual; nonblocking |
| 5 | stub | B1 / B | `feature/stub@a35651e8de2caf79f00744edc03ad581cffb7281` | `archive/preintegration/stub-20260804@62235ea8b9cc12243666bbc4fb475ffe29e8698a` → O | `c29ef117eddf4e696dc9797fa601951fd4183f6e` | `99ae7d11a92c0c93837c33e5da993b602cd90a24` | no pixel oracle by design |
| 6 | opengles1 | B1 / B | `feature/opengles1@3d576da20cbadf87d826f01e2b93eeca6dd01629` | `archive/preintegration/opengles1-20260804@27aa94167134cb722fe6e6559562368001b33c3f` → O | `b811d76ddcc691ca39cf2e673ea7ef32ab25978f` | `df6b7cc627c5eeaa166858d66ebf1c9fd22a6d65` | runtime/sanitizer and permanent capability boundary; external/future |
| 7 | opengl4 | B1 / B | `feature/opengl4@c49e0ba223fa36f8fa9f7cd643305ea3367bf521` | `archive/preintegration/opengl4-20260804@1c2ba44a59b06ea761adcd25d2fd08dbf4d84cdc` → O | `3f1035dec022886d5e4eb9cfdf33886ef2e32dee` | `bc29a9764ea38212cbc9f9bed65a63f7a27f399d` | MSAA cube plus Win/macOS validation; external |
| 8 | opengl1 | B1 / B | `feature/opengl1@fc14f37b98706befb6c98a713be6dc107c029199` | `archive/preintegration/opengl1-20260804@9f5284ed93f2517fd2aca96195bd425ffe95f725` → O | `91344935ad6bdd44489ce3aefa77604c8252d581` | `c0876fca3291d5edd897889dd59ff79db7927d06` | GLX swap control and Windows validation; external |
| 9 | opengl2 | B1 / B | `feature/opengl2@77d36d9e3bb402fcf12c093177e4007c7bf11fbc` | `archive/preintegration/opengl2-20260804@a77a7a069c3e676f31820746898cd30809e5bfc2` → O | `289410a650b5eeaca6513deea4f186aae773f50d` | `9e6d62edbf5773d238941e3d9042362c56a3e605` | prose/process residual only |
| 10 | wicked | B2 / C | `origin/claude/wicked-engine-cna-backend-5ffqzd@91d8587e9a1a760c3275713f15f65bfafa387082` | `archive/preintegration/wicked-20260804@efd171b1f12f9cada870a90fce9f4a79b5de8e78` → O | `97d5a644d4f085fbdfa521cf0b9f1c3d1e7355a8` | `683a00a51e8c8c3bb227b084ff5bf802d009195e` | WICKED-18/74/75/76 and hardware/display; nonblocking/external |
| 11 | magnum | B2 / C | `origin/claude/cna-magnum-gr-backend-211xsx@9b903db8cf16988e3fbc955a429bab6c6a5b191e` | `archive/preintegration/magnum-20260804@02fb59f7ea065ff4063359c5d1fb800e6379c920` → O | `b7fe9b2489874804d09474027a515fcea8460687` | `e7d46c4cd5f1da34d591971256c15770b8042300` | MAGNUM-54/55/58/59; nonblocking/external |
| 12 | sokol | B3 / D | `feature/sokol@261ea70027d04c55519f82f435c28705beb0b8c6` | `archive/preintegration/sokol-20260804@c872f677ea7ef3437d40fc92b23c23d19d545a5e` → O | `9fb83a991faea1c954949d35f83ebe06fc9ad677` | `37066e453e616f999257ca5025e2f62c1e99540d` | SOKOL-30/31/49 and upstream limitations; future/external |
| 13 | diligent | B3 / D | `feature/diligent@1ab12b505e40e61101a260ae43d3b9911f219e8a` | `archive/preintegration/diligent-20260804@ca46449c72c1004cd4712e8d1297fd6122d25c2f` → O | `27f7dcefedf4b5e3183b1ec76ab96f1801c8413b` | `aa9f3fb51b79882334fc2e8c40ffe05faa08d1b4` | DILIGENT-66/69 and retained limits; external/future |
| 14 | skia | B6 / G (early) | `feature/skia@ca046f013bfd9797aab0292194e547d1caa4fef8` | `archive/preintegration/skia-20260804@9ff0170382732ae1c1eeb6f6b563962fe8f692b4` → O | `a071e1e23120a142840d54777d41c4e58fc2345c` | `1381ff930a88cdda2a17a25136a1b1fd93b3adcf` | REMED-GFX-224 accepted residual; Ganesh/future |
| 15 | gl | B4 / E | `feature/gl@f8efb9b46f7b0d516eaf150e7e8e93f2bd74e795` | `archive/preintegration/gl-20260804@650dbbfb787605aaf7fe8d2ff7d7537adaf95464` → O | `a8c32a67c76200c60aa0be6878dfa0b5ddcc513f` | `0a51f8647eb4ddf2fdcd2102756ea79bb49625b7` | browser/WebGL external; carried global findings |
| 16 | glide | B5 / F | `feature/glide@2f9b47e1281590e6735b5f76ef1e13dd781d8981` | `archive/preintegration/glide-20260804@8e96bb5648333797c895611bb2f9a2ad92755484` → O | `e891e105dd2567dd5bd8397996cb8c830c127b18` | `677f4c59e066fc9a7ed79430d0fee5ffd69b531c` | i686 compile/link and fake-ABI gates PASS; hardware runtime external |
| 17 | gdi | B5 / F | `feature/gdi@adc9cc2a2e496d162202733b05ab659199a857b8` | `archive/preintegration/gdi-20260804@efa3cedc153d118dd44c5b683c82a2f2c85f4358` → O | `625f4ad59c9d898eb01a50e22dc872447590989a` | `ba5fa60166bef2214a4c08b64d50570d1120b7b9` | physical Windows/MSVC; external |
| 18 | html-dom | B5 / F | `claude/html-dom-cna-backend-xefzwf@8e4e42935a0962bd5eb6178fe4698334075f94ee` | `archive/preintegration/html-dom-20260804@cb73cb0b541ffdd3ded44e16df278cacb813e087` → O | `a32977f397da0c667a4162ee73f9d2363e4981d2` | `24bf4786af1ff6b1cf86640e85a22f76c7315818` | Emscripten/browser/DPR; external |
| 19 | direct2d | B6 / G | `feature/direct2d@9b17e783e74e87a3f23b9cc47bd3c7cd6dad9d81` | `archive/preintegration/direct2d-20260804@e16c478dae4f37cb77449c98826a0c184598709e` → O | `1b740d962d85bb648d6ae2997bba9b1ba09dfd87` | `7af760bee2896960270cfd7bd6c822b96c13be94` | REMED-GFX-224 carried; physical Windows/COM external |
| 20 | llgl | B6 / G | `feature/llgl@fa26e72dcda612de2a8cff814e748c7479e45836` | `archive/preintegration/llgl-20260804@8f2945091aacf294c104d4117447bc6a97232ea2` → O | `c74fbaebb93745de08130d050e11230639df3259` | `4ac696c748fb18eef7dd06cca82a0486549bcd5d` | accepted Linux/X11/OpenGL route; other routes future/external |
| 21 | metal | B6 / G | `feature/metal@48928d113cb864f78d754256d2d559d914d4f1a7` | `archive/preintegration/metal-20260804@43f6eab8d40c6006265cd4e19223cdd3d68c1fc3` → O | `e2ffe7290ddf5aab5c211b1fc2c00f0e09bd42f1` | `012b158eb8246ce267887acbd4fc7a2468d89e52` | adapted Apple validation external |

Git, not document arithmetic, produced exactly 21 first-parent two-parent merges after
`d79214e7600c0411ce912be11f8e762866be23ee`. The 32 first-parent commits are those 21 lane merges
plus 11 stabilization/remediation commits. There is no 22nd lane, duplicate lane, or renderer merge
after Metal. For every adapted lane, merge parent 2 equals AD and the trees are identical; the
direct glTF merge has an identical stable patch ID to its original delta.

## 3. Checkpoints, signatures, and provenance

| Tag | Annotated tag object | Peeled target | Result |
|---|---|---|---|
| `cna-post-audit-remediation-phase1` | `8fc335121a83e6b4dd4ee1e3f66ffd533854a980` | `d79214e7600c0411ce912be11f8e762866be23ee` | annotated, GPG-good, ancestor |
| `integration/checkpoint-batch0-20260804` | `9cd5c3465c8ad877695d14019f4e3b06c2999d95` | `e03322147ff9f708fe729e2f045cc31798f5de06` | GPG-good |
| `integration/checkpoint-batch0-complete-20260804` | `86b255538f6572e20e5d787d481b5441471ba375` | `990d6b8aa42fffe8525f5daf98f0737a31d7af4f` | GPG-good |
| `integration/checkpoint-batch1-20260805` | `fc52a0ed749a2bf9f757debe28edd2e9fb9c41e4` | `ed607602eee01aae8255bddb133f68fafacda4fb` | GPG-good |
| `integration/checkpoint-batch2-20260806` | `a2eb20dc7a22744adac1682ca42442451965d74b` | `ebd04ae307ed45a0a912aa86887f1f956a25e10a` | GPG-good |
| `integration/checkpoint-batch3-20260807` | `1c8b1a05e0754345afe4af0f30376de172c65567` | `aa9f3fb51b79882334fc2e8c40ffe05faa08d1b4` | GPG-good |
| `integration/checkpoint-batch4-20260807` | `b5c1a4ad4da03560acac1043b669477a2b377f93` | `0a51f8647eb4ddf2fdcd2102756ea79bb49625b7` | GPG-good |
| `integration/checkpoint-batch5-20260808` | `307c9ad511015c64ce55184cdf0d5ebd7b1cb575` | `c805fd737f4321568fba378e8d1b8fe5b5270666` | GPG-good |
| `integration/checkpoint-batch6-20260809` | `8d347c933a3da3c39f22711e40e80cf7a29c4682` | `012b158eb8246ce267887acbd4fc7a2468d89e52` | GPG-good; exact pre-reconciliation head |

All annotations match their intended Batch, every target occurs once on the first-parent chain,
every tag is an ancestor, and no conflicting duplicate or moved CNA checkpoint exists. All 21 lane
merges separately verify GPG-good; all 899 commits in the campaign range report `%G?=U`. All 21
CNA original-lane archive tags still exist, are signed, and peel to the recorded originals. The
campaign-owned attribution/trailer sweep is empty apart from factual references to the filename
`CLAUDE.md`.

The owner-rewritten external MetaGL/EasyGL histories are intentionally not required to match the
obsolete signed pre-rewrite commit IDs. Their current external archive tags were also rewritten
and are unsigned; current addenda record that fact instead of falsely claiming their old object
provenance remains authoritative.

## 4. Planning versus integration reconciliation

Common base is `d79214e7600c0411ce912be11f8e762866be23ee`. Planning changed 45 paths from it,
integration 1,148; eight overlap and seven of those are byte-identical. The only genuine both-side
content divergence was `plans/plan_postaudit.md`.

Selectively reconciled into the integration tree:

- current `NEXT.md`;
- all 21 lane cards and Batch 0–6 stabilization/completion records under `integration/`;
- `integration/INTEGRATION_BRANCH_INVENTORY.md`,
  `INTEGRATION_HISTORY_POLICY.md`, and `INTEGRATION_ORDER.md`;
- current `plans/plan_postaudit.md`, retaining the integration-side N50/N51 correction;
- `remediation/INTEGRATION_BRANCH_INVENTORY.md`, `REMEDIATION_EXIT.md`,
  `REMEDIATION_INDEX.md`, and `REMEDIATION_PROGRESS.md`;
- current renderer documentation and this final reconciliation record.

Intentionally omitted:

- the identical `audit/` tree;
- duplicate planning-side production/test changes already authoritative on integration;
- wholesale `feature/audit` history;
- session handoffs, transient planning noise, and owner `AGENTS.md`;
- any future modularization or renderer-plan implementation.

Historical BLOCKED → READY evidence was retained. Historical old SHAs were not mechanically
rewritten.

## 5. MetaGL and EasyGL after the owner's rewrite

No fetch or network write was performed; “origin/develop” below means the locally-known
remote-tracking state whose reflog records the owner's accepted rewrite push.

| Repository | Local `develop` = local `origin/develop` | Tree | Worktree |
|---|---|---|---|
| MetaGL `/rv/data/development/github.com/openeggbert/meta-gl` | `571d3a62fe166b9781ac6193d137b12ff3757620` | `a7771c5593a4ec4b71283d38523a0cde3fbf6d4b` | pre-existing `AM VERSION`; not in accepted tree |
| EasyGL `/rv/data/development/github.com/openeggbert/easy-gl` | `0b46d35c394a9fb6aea6a85c6587894b5013da33` | `e89ff546d3782e2b32e02f4b9dc56da42c4c463a` | pre-existing `?? VERSION`; not in accepted tree |

The new MetaGL tree equals old signed adaptation `c964e73622db141e052300e3805933585ebb1054`;
the new EasyGL tree equals old signed adaptation `9b831dee6ed99dafcee3f33e4fcffb2b984cd898`.
Commit-by-commit trees/authors/dates/subjects are preserved; the accepted rewrite removed old
signatures and attribution trailers. No second rewrite is planned or permitted.

Dependency resolution is sibling-path based, not a branch, commit pin, FetchContent revision, or
submodule:

```text
CNA CMake -> add_subdirectory(../easy-gl easy-gl)
          -> EasyGL add_subdirectory(../meta-gl meta-gl)
```

A clean OPENGLES configuration/build consumed those current committed trees successfully. Old SHA
disposition:

- **A, historical evidence:** the pre-rewrite accepted/original SHAs in Batch 4, the GL lane,
  integration inventory/order/history, and plans remain because they truthfully record the event;
- **B, active pins:** none;
- **C, stale current claims:** README's `../easy-glrvc` path and current external-provenance
  assertions were corrected with a present-day addendum;
- **D, irrelevant:** MetaGL/EasyGL website-repository SHA text is not a CNA dependency.

## 6. Public renderer identity and capability inventory

Canonical count: **41**.

1. SDL_RENDERER
2. OPENGLES
3. OPENGL33
4. WEBGL1
5. WEBGL2
6. BGFX
7. VULKAN
8. WEBGPU
9. MAGNUM
10. HEADLESS
11. SOFTWARE
12. STUB
13. D3D11
14. D3D12
15. DIRECT2D
16. CANVAS
17. HTML_DOM
18. SKIA
19. ASCII
20. FREEDIRECT
21. D3D9
22. DX1
23. DX2
24. DX3
25. DX5
26. DX6
27. DX7
28. DX8
29. D3D10
30. SDL_GPU
31. OPENGLES1
32. OPENGL4
33. OPENGL1
34. OPENGL2
35. WICKED
36. SOKOL
37. DILIGENT
38. GLIDE
39. GDI
40. LLGL
41. METAL

Enum ordinal/name, CMake selector, compile definition, target/factory, and platform/dependency gate
were reconciled for every identity. Names/selectors are unique and every registered identity
reaches its own accepted factory route subject to its explicit gate. The four public GL profiles
share one EasyGL implementation/factory and one implementation macro but differ in context/shader
profile; consequently 41 public identities map to 38 implementation factories/macros. EasyGL
itself is internal. LLGL/Diligent/Sokol/bgfx/Skia native-API subchoices do not add CNA identities.

The registry tests now cover Metal and 41 unique canonical names, the compile-definition test now
counts Glide, and the WebGL2 benchmark path no longer tests the withdrawn `EASYGL` selector.
Capability classes are kept truthful: STUB/HEADLESS are no-output or validation routes; the 2D-only
family is SDL_RENDERER, CANVAS, HTML_DOM, SKIA raster, ASCII, FREEDIRECT, DX1, DIRECT2D, and GDI;
SOFTWARE is bounded CPU 3D; OPENGLES1/OPENGL1/DX2–DX8/GLIDE are legacy bounded 3D; the remaining
programmable backends retain their documented backend-specific limitations. WebGPU remains
experimental, Sokol's accepted native API is GLCORE, LLGL's is Linux/X11/OpenGL, and Metal keeps
its conservative no-Mac-claim boundary.

## 7. OPEN finding reconciliation

### A — mandatory set closed by `FINAL-STAB-001`

- **Glide current-tree compile gate — DONE.** Sharp-runtime now publishes
  `SHARP_RUNTIME_HAS_NATIVE_INT128` from a compiler capability probe. CNA directly includes that
  public definition and gates only Decimal reader declaration, registration, and Decimal-specific
  tests on it; TimeSpan and DateTime remain available. The exact i686 GLIDE graph selects value 0,
  compiles the Decimal reader translation unit, completes its 540-step Release build, and links
  the real Glide smoke executable. The PE32 i386 fake DLL/loader builds and its 39-export contract
  exits zero under Wine. This is compile/ABI evidence, not a rendering claim.
- **Sanitizer build graph — DONE.** The unchanged canonical dynamic-voice target originally linked
  `libCNA.a`, then `libSHARP_RUNTIME.a`, then `libcna_backend_graphics_headless.a` and failed on
  `CNA::Logger::Warn` referenced by `HeadlessGraphicsBackend.cpp.o`. CNA publicly owns the selected
  backend, while HEADLESS's concrete object instantiates an interface default with the reverse
  Logger edge. A prior `CMAKE_CXX_STANDARD_LIBRARIES=<absolute libCNA.a>` diagnostic merely placed
  another scan after the backend. The tracked fix adds HEADLESS to the existing backend-private-CNA
  ownership rule, so CMake emits the real static-library component twice. Fresh and incremental
  canonical graphs now link dynamic/static/no-hardware audio harnesses in the component order
  `libCNA.a` → `libcna_backend_graphics_headless.a` → `libCNA.a` →
  `libcna_backend_graphics_headless.a`, without a generated-tree edit or whole-archive.
- **`REMED-GFX-221` — DONE.** Strict ASan reproduced an initialization-order-fiasco before
  `main()` at `GestureDetector.cpp:52`, reading `Vector2::Zero` while its old definition still
  required dynamic initialization. `Vector2(float,float)` is now `constexpr`, and Zero/One/UnitX/
  UnitY are `constinit const`. They reside in read-only data and have no dynamic initializer; the
  fix removes the root cross-translation-unit order dependency rather than special-casing the five
  GestureDetector initializers.

All three mandatory failures are resolved and their bounded retakes pass.

### B — accepted nonblocking residuals

- `REMED-GFX-224` (MEDIUM): EasyGL render-target `SetData` no-op;
- `REMED-CORE-015` (LOW): defined-wrapping gap in Vector3/Matrix hash arithmetic;
- `REMED-CONTENT-010` / duplicate `REMED-NA-016` (LOW): vendored cgltf misaligned sparse load;
- Task 872: EasyGL independent ReferenceStencil wrong-pixel gap;
- `REMED-GFX-056`, `-114`, `-115`, `-120`, `-121`, `-126`, `-132`, `-133`,
  `-137`, `-139`, `-171`, `-178`, and `-199`, each with the exact support limitation or
  tool/test boundary already recorded in the remediation index;
- `REMED-TEST-008`, networking Outcome C, and the wall-clock audio flake; final gates run serially
  where required;
- `REMED-BUILD-016` remains a preset-maintenance issue, but this session explicitly enabled
  `float-cast-overflow`, so it did not weaken this sanitizer run.

These are not silently called fixed and were not remediated merely to green the campaign.

The current authoritative OPEN inventory was rechecked after the dated phase-1 records and later
post-campaign closures were reconciled. `REMED-CONTENT-007/-008` and `REMED-GFX-223` are closed;
`REMED-GFX-217/-218` remain HIGH/OPEN only for their accepted deferred full-translator scope after
the dangerous checkpoint paths were guarded; the items in B–D retain their recorded nonblocking,
external, or future dispositions. There is **no HIGH/P1 mandatory final-development blocker**.

### C — external validation gates

- adapted Metal: real macOS Objective-C++/framework link, MSL, validation, pixels, lifetime,
  Retina/frame pacing, and Intel/Apple-Silicon;
- Direct2D/GDI/D3D family: native MSVC/physical Windows, DPI, COM/debug-layer/live-object and
  device-lost behavior as applicable;
- Glide: caller-supplied runtime or physical hardware; the restored compile/fake-ABI gates do not
  substitute for rendering;
- WEBGL1/WEBGL2/CANVAS/HTML_DOM: Emscripten/browser, JavaScript lifetime, and DPR > 1;
- OpenGLES1 and real-GPU/display gates for Magnum, Sokol GLCORE, Wicked, Diligent, LLGL and other
  hardware-dependent routes;
- `REMED-BUILD-012`, `REMED-MEDIA-005`, `REMED-DEVICES-004`, and D3D12 runtime evidence for
  `REMED-GFX-199`.

No Linux sanitizer claim is extended to Metal or Win32 COM runtime behavior.

### D — future/unclaimed work

- `REMED-GFX-053`, `-085`, `-086`, `-163`, `-165`, `-203` through `-208`, `-210`,
  `-214`, `-217`, and `-218`;
- `REMED-NET-008` and other explicitly assigned non-campaign remediation;
- Skia Ganesh/SKIA-163+, Sokol non-GLCORE/PBR, Magnum format/sample-mask/context-loss, Diligent
  deferred effect/volume/stride scope, Wicked remaining features/performance, LLGL unsupported
  modules/platforms, and Metal capabilities that explicitly report false;
- modularization and the planned post-campaign renderer expansion.

### E — stale/closed records

`REMED-GFX-219`, `-221`, `-223`, `-225` through `-233`, `REMED-BUILD-011`, `-017`, `-018`,
`REMED-CONTENT-007`, `-008`, `-011`, `REMED-GFX-172`, and `WICKED-80` are resolved.
`REMED-GFX-055` duplicates `-054`; `REMED-GFX-184` duplicates `-163`; the historical
Metal findings have their accepted implemented/disabled/external dispositions. Historical prose
is retained where it is clearly dated rather than rewritten into false present-tense evidence.

## 8. Final build/configuration matrix

All top-level compilations used explicit `-j4` or less; vendored dependencies used at most two
jobs. Stable in-repository `cmake-build-<variant>` trees and ccache were used.

| Route | Current-tree result | Boundary |
|---|---|---|
| HEADLESS | CNA, CnaTests and glTF tool build PASS | native |
| OPENGLES / EasyGL / current external chain | build PASS | native principal route |
| VULKAN | build PASS | native llvmpipe/X11 |
| WEBGPU | build PASS with pinned local wgpu-native v29.0.1.1 | native experimental |
| SOFTWARE | backend build PASS | native |
| SOKOL | GLCORE backend build PASS, local pinned dependency | accepted route |
| DILIGENT | 904-step backend build PASS, Vulkan+OpenGL engines | accepted build boundary |
| SKIA | raster backend build PASS | accepted raster route |
| LLGL | OpenGL+Null dependency and CNA/CnaTests/backend build PASS | accepted Linux/X11 route |
| D3D11 x64 MinGW | 487-step target build PASS | representative historical Windows backend |
| DIRECT2D x64 MinGW | 221-step backend build PASS | accepted cross-build boundary |
| GDI x64 MinGW | 483-step backend build PASS | accepted cross-build boundary |
| GLIDE i686 MinGW | configure PASS; 540-step Release build PASS; PE32 smoke + fake-ABI clients link; Wine ABI 1/1 PASS | compile/ABI only; runtime rendering external |
| HTML_DOM on native host | expected configure rejection PASS | Emscripten required |
| METAL on non-Darwin host | expected configure rejection PASS | macOS required |

No unavailable runtime was faked and not all 41 renderers were claimed executable on Linux.

## 9. Principal tests and smoke arithmetic

Suites overlap, so their counts are reported per gate rather than added into a misleading grand
total.

| Gate | Arithmetic | Result |
|---|---:|---|
| HEADLESS full CnaTests | 6,066 = 6,021 pass + 44 skip + 1 known REMED-GFX-133 failure | accepted residual visible |
| OPENGLES full principal corpus | 6,225 = 6,219 pass + 6 intentional skip + 0 fail | PASS |
| OPENGLES forced isolated X11/Xvfb focus | 154 = 153 pass + 1 intentional skip + 0 fail | PASS; SDL reported `x11` |
| OPENGLES device/draw/effect/lifetime dedicated binaries | 11 + 17 + 9 + 3 + 22 + 17 + 49 plus smoke/RT | PASS |
| Vulkan forced isolated focus | 201 = 200 pass + 1 intentional skip + 0 fail | PASS |
| WebGPU isolated backend/registry | 26/26 | PASS |
| LLGL forced X11/Xvfb focus | 31/31 | PASS |
| FINAL-STAB strict sanitizer representative corpus | 215 = 214 pass + 1 intentional HEADLESS skip | PASS; initialization-order check enabled, no suppression/report |
| FINAL-STAB strict focused regressions | Vector2 4/4; Decimal/DateTime 4/4; audio 8/8; 3/3 harness exits | PASS |
| FINAL-STAB Glide continuity | portable 78/78; shared identity/clear 14/14; Wine fake-ABI 1/1 | PASS; compile/ABI only |
| FINAL-STAB OPENGLES affected smoke | 9/9 serial CTests under isolated Xvfb | PASS |
| FINAL-STAB OPENGLES representative corpus | 217 = 216 pass + 1 intentional non-rejection-arm skip | PASS; SDL reported `x11` |

The selected corpus covers Texture2D authority/cache, Content containment, draw/stream/declaration,
Basic/Sprite effects and VertexColor, resource lifetime/order, renderer registration/capabilities,
Content/Audio/Input/platform shared changes, and representative device/smoke lifecycle. The strict
215-case filter includes both registry suites. Final windowed evidence used forced isolated X11
through Xvfb, not the owner's real desktop display.

## 10. Sanitizer gate

The stable `cmake-build-headless-asan-ubsan` tree used Debug, ccache and
`address,undefined,float-cast-overflow`. `ldd CnaTests` proves both `libasan.so.8` and
`libubsan.so.1` are linked.

The unchanged baseline was reproduced first. The dynamic-voice harness link line scanned
`libCNA.a`, `libSHARP_RUNTIME.a`, then `libcna_backend_graphics_headless.a` and failed on
`CNA::Logger::Warn` from `HeadlessGraphicsBackend.cpp.o`. Removing the old diagnostic
`CMAKE_CXX_STANDARD_LIBRARIES` cache entry removed the accidental second absolute `libCNA.a` scan
that had made the generated tree appear green. After the tracked ownership correction, both a
fresh canonical configuration and the stable incremental configuration generate the component
order `libCNA.a` → `libcna_backend_graphics_headless.a` → `libCNA.a` →
`libcna_backend_graphics_headless.a`; dynamic/static/no-hardware harnesses link, and the immediate
rebuild reports no work.

The second unchanged baseline ran with
`ASAN_OPTIONS=check_initialization_order=1:strict_init_order=1` and failed before `main()` at
`GestureDetector.cpp:52` on the dynamically initialized `Vector2::Zero`. After the constant-
initialization fix, every accepted runtime gate used the exact unsuppressed environment
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:check_initialization_order=1:strict_init_order=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. The representative corpus is
**215 = 214 pass + 1 intentional HEADLESS no-pixel-route skip**, with zero sanitizer report.
Vector2 is 4/4, Decimal/DateTime is 4/4, affected audio is 8/8, and each audio harness exits zero.

The post-fix binary still places GestureDetector's translation-unit initializer before Vector2's,
which proves the result is not link-order masking. `nm` instead reports Zero/One/UnitX/UnitY as
8-byte read-only (`R`) objects and no dynamic initializer for those constants. A bounded scan of
255 CNA objects, 36 static-initializer seed objects and 355 reachable functions found no remaining
`B`/`D` XNA value-type constant read across translation units. The required sanitizer result is
therefore **PASS**.

## 11. Documentation result and develop merge simulation

Current documentation now states 21/21 integrated, 0 pending, Batch 0–6 complete, exactly 41 public
identities, the current rewritten MetaGL/EasyGL heads and sibling dependency chain, remaining
findings/external gates, and **READY** develop readiness. Modularization and every additional
renderer remain future work. Historical lane/Batch evidence and BLOCKED → READY transitions remain.

Before the final reconciliation commit, the read-only merge shape was:

- `develop`: `ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c`;
- merge-base: that same commit;
- ahead/behind: integration 1,679 ahead, 0 behind before this final commit;
- graph: `develop` is an ancestor, so a future committed-tree merge is a fast-forward;
- prospective tree: the integration candidate tree, containing all 21 lanes, the bounded
  `FINAL-STAB-001` repairs, reconciled current docs and dependency references, with no
  planning-only code or `audit/` delta.

The final signed `FINAL-STAB-001` commit advances only the integration side; its exact SHA,
signature, final ahead count and tree are recorded in the session handoff after committing. The
pre-existing dirty `develop` worktree remained exactly:

- tracked owner-local edits to `cmake/Tests/EasyGLTests.cmake` and
  `cmake/Tests/SdlRendererTests.cmake`; and
- untracked owner-local `AGENTS.md` and `examples/xvfb_screenshot_demo.cpp`.

The tracked files overlap integration history. All four paths must be preserved, and any overlap
must be resolved by their owner before running the eventual fast-forward command.

No final campaign checkpoint tag is created: the repository convention has per-Batch checkpoints,
Batch 6 already exists, and the bounded stabilization follow-up does not justify inventing another
tag.

## 12. Decision and exact next action

**READY FOR DEVELOP MERGE.**

Exact next action: preserve the owner's pre-existing dirty `develop` worktree changes, then
fast-forward `develop` to the signed `integration/post-audit-phase1` tip, then run the bounded
post-merge smoke gate. Keep every accepted nonblocker and external native-runtime gate in its
recorded scope; do not fold modularization, new renderer work, a history rewrite, or a force-push
into that owner-controlled promotion.
