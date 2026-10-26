# Structure-parameter defaults, as genuine XNA 4.0 reads them

- `effect_fx_struct_defaults.xnb` — XNA Game Studio 4.0 Refresh's own `EffectImporter` +
  `EffectProcessor` build (Windows, HiDef) of `tests/assets/xna40/source/fx_struct_defaults.fx`,
  produced by `tools/xna-pipeline-oracle/differential/run-differential-oracle.sh
  effect_struct_defaults <out>` with that manifest.
- `xna-effect-struct-defaults.txt` — what the real XNA 4.0 runtime reports for that effect's
  `Lighting` parameter through its public `EffectParameter` API:
  `tools/xna-oracle/EffectStructDefaultsOracle.exe <the .xnb> <out.txt>`, under Wine in the
  `~/.wine-cna-xna40` prefix, inside `tools/platform/run_gpu_tests_private.sh --exec`.
  Measured 2026-10-26.

`Fna3dCompiledEffectTest.CompiledStructDefaultsMatchGenuineXna` loads the XNB through CNA's
`ContentManager` and expects these values (2026-10-26 dependency audit, A2v2 A-09).
