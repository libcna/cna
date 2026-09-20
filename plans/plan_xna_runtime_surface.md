# Microsoft XNA 4.0 runtime public type surface

| Task | Status | Result |
| --- | --- | --- |
| XNA-ENUM-001 | Complete | Implement and test the four graphics, touch, and gamer nested collection enumerators; add a Microsoft XML type census and remove obsolete type-coverage claims. |
| XNA-MEMBER-001 | Baseline complete | Inventory and classify all 3,627 documented runtime members before production API fixes; preserve the 331/331 type result. See `docs/xna-4-runtime-member-coverage-baseline.md` and its per-entry JSON. |
| XNA-MEMBER-002 | Complete | Correct the four small, documented gaps (scalar-left Matrix multiplication and three enum values), preserve renderer/C-ABI translations, review all remaining gaps by tier, and publish the final member census. |

The original Microsoft runtime XML set contains 331 distinct documented
public types across ten runtime assemblies. The build-time Content.Pipeline
XML is excluded. The earlier 329 estimate collapsed two generic/non-generic
CLR name pairs. The starting HEAD had 326/331 declaration coverage: five
nested enumerators were absent, while the gamer nested struct already
existed but had an incompatible interface and cursor contract. The final
audit reports 331/331 represented and zero missing. See
`docs/xna-4-enumerator-reference.md` for metadata and behavior findings.

`tools/audit_xna_runtime_surface.py` exports the complete type inventory,
reference XML and DLL checksums, normalization, missing types, and a
signature-aware member inventory as JSON. The runtime member inventory has
253 constructors, 1,518 methods, 1,040 properties, 753 fields, and 63 events.
The pre-fix XNA-MEMBER-001 baseline is **3,463/3,627 documented members
represented (95.48%)**: 1,658 exact, 1,747 semantic, 58 host-language
substitutions, 164 missing, zero not applicable, and zero needing review.
These numbers measure API representation, not behavioral parity. The
Content Pipeline XML is excluded and remains separately measured. Run
`python3 tools/audit_xna_runtime_surface.py --write-reports` and
`python3 -m unittest discover -s tools -p test_audit_xna_runtime_surface.py`.

After XNA-MEMBER-002, the current report is **3,467/3,627 members (95.59%)**:
1,662 exact, 1,747 semantic, 58 host-language substitutions, 160 missing,
zero not applicable, and zero needing review. Constructors are 236/253,
methods 1,380/1,518, properties 1,038/1,040, fields 751/753, and events
62/63. Operators are 145/145 and enum values 661/661. Each remaining
missing member and its implementation tier appear in
`docs/xna-4-runtime-member-coverage.md`; the corresponding JSON contains
one record per documented member. The baseline remains a frozen pre-fix
snapshot from commit `a17e954c8`.

The four Tier A fixes are the Microsoft-documented
`Matrix.op_Multiply(Single, Matrix)` operator (forwarded to the existing
`Matrix::Multiply` result), `GamePadType.BigButtonPad = 768`, and
`BlendFunction.Min = 3` / `Max = 4`. FNA's source omits the scalar-left
operator and uses different enum ordinals, so the Microsoft runtime XML and
DLL metadata govern these public shapes. Focused Math, Input, and Graphics
tests pin them. Renderer-local blend translators now map Microsoft ordinals
to SDL_Renderer, SDL_GPU, Software, OpenGL4, EasyGL, PortableGL, Vulkan,
WebGPU, and FNA3D operations; the C API converts to its previously published
Max=3 / Min=4 constants without changing the C ABI. The C graphics surface
smoke test checks both constants with valid Reach blend states. No new XNA
types, Sharp Runtime stubs, or Content Pipeline APIs were added.

Validation commands:

```bash
cmake -S . -B build-probe -DCNA_BUILD_C_API=ON
CCACHE_DIR=/tmp/cna-member-ccache cmake --build build-probe --target CnaCoreTests CnaMathTests CnaRuntimeTests CnaGraphicsTests CnaInputModuleTests CnaAudioTests CnaMediaTests CnaContentTests CnaStorageTests CnaGamerServicesTests CnaNetTests cna_c_api_graphics_surface_smoke --parallel 6
python3 -m unittest discover -s tools -p test_audit_xna_runtime_surface.py -v
python3 tools/audit_xna_runtime_surface.py --write-reports
```

The HEADLESS+C API build of Core, Math, Runtime, Graphics,
Input, Audio, Media, Content, Storage, GamerServices, Net and the C graphics
surface smoke target passed. Unit-test binaries passed Core 106/106, Math
858/858, Runtime 174 pass/2 skip, Graphics 2,326 pass/454 skip, Input 471
pass/3 skip with `FakeHapticTest.*` excluded (the existing host-dependent
fixture), Audio 225 pass/8 skip, Media 289/289, Storage 10/10, and
GamerServices 367 pass/1 skip. Content had 1,795 pass/16 skip/6 failures
limited to unsupported HEADLESS cube/3D texture and authored DXT atlas
routes. Net had 242 pass/68 failures when sandboxed UDP/socket creation
failed. The C graphics surface smoke check and all 19 audit regression tests
passed. SDL_Renderer, Software, PortableGL, OpenGL4, EasyGL, Vulkan, and
WebGPU translator translation units passed C++ syntax checks; the FNA3D
Min/Max converter passed a focused compile-and-run probe. SDL_GPU was not
syntax checked because `SDL_shadercross.h` is unavailable in this HEADLESS
environment. These tests verify the affected API shapes and local mappings;
they do not assert renderer-wide or behavioral XNA parity.

The six Content failures were
`CnbTextureContentManagerTest.ATextureCubeCnbLoadsAllSixFacesDistinctly`,
`CnbTextureContentManagerTest.ATexture3DCnbLoadsAsASharedPointerWithItsDepth`,
`CnbTextureCubeProducerTest.ATextureCubeCnbLoadsThroughContentManager`,
`CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`,
`CnjTexture3DTest.LoadsRealCnjFixture`, and
`ContentManagerSpriteFontXnbTest.ReachLoadsAnAuthoredNpotDxtSpriteFontAtlas`.
They throw unsupported HEADLESS texture-storage errors or a different
exception wrapper than those tests expect. The 68 Net failures include
ENet host and discovery UDP socket creation errors and loopback children
that cannot bind; the other 242 Net tests passed. No Content or Net source
files were changed by XNA-MEMBER-002.

The earlier XNA-ENUM-001 validation used the HEADLESS renderer build. The six affected module and
dependency binaries passed: Graphics 2,318 pass/455 renderer-specific skips;
Input 474 pass with the unrelated `FakeHapticTest.*` fixture excluded;
GamerServices 368 pass with `XDG_DATA_HOME` in `/tmp`; Core 106 pass; Math
857 pass; Runtime 175 pass/2 skips. All twelve new enumerator cases pass.
The unfiltered Input binary has 25 failures confined to
`FakeHapticTest.*` because its canned platform forwards haptic subsystem
acquisition to the unavailable host backend. The aggregate `CnaTests`
build fails in an unrelated EasyGL test that includes unavailable
`metagl/metagl.hpp` in this HEADLESS configuration. No haptics or renderer
code was changed.
