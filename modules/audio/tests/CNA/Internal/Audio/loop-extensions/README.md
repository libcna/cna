# HND-XACT-06 finite-loop extension inputs

Original CC0-1.0 fixtures, authored by the genuine XACT3.6 SDK builder (XactBld3) from the
`shapes.xap` next to each `shapes.xsb`. Exact probe, EXE, Microsoft DLL, builder, bank and matrix
hashes are in PROVENANCE.json; the measured lifecycle of every variant is ORACLE.json. No
Microsoft binaries are distributed.

Every variant is one authored field away from an HND-XACT-05 bank (`../finite-cardinality/`),
which `tools/xna-oracle/check-xact-loop-extensions-oracle.py` verifies byte by byte:

* `type4/raw0..4` -- the type1 PlayWave of A0 plus one Pitch Variation (-100..100 cents, its New
  Variation On Loop off): event family 4. `type4nol` -- the same with that bit on (effect flags
  0x1100). `type4/raw100`, `type4nol/raw100` -- the loop byte.
* `type6/raw0..4` -- the base shape (ordered NEW_ON_LOOP over A0/A1) plus the Pitch Variation:
  family 6. `type6nol0` -- nol0 plus a Pitch Variation whose own New Variation On Loop is on.
* `type1region`, `baseregion` -- type1 and base with A waves that carry a smpl loop region
  [5512, 16537] (`region/`); the sound banks are unchanged.
* `random`, `shuffle` (variation type 2 and 4) from base, `randomnol0`, `shufflenol0` from nol0,
  raw 1 and 3.
* `base/raw5,100,254`, `nol0/raw5,100,254` -- the loop byte.

Non-region variants replay with `../finite-cardinality/shared/` (base/raw3's XGS/XWB); the region
variants with `region/A.xwb`, which differs from shared/A.xwb only in its build time and the two A
entries' loop regions.

Both genuine engines (x86/x64), 136 cells in five groups, each group rerunning the retained
TQ-XACT-31 control, the one-wave resident control and the HND-XACT-02 samebank bytes:

* Every family plays raw n as n+1 passes (0..5, 100 and 254 measured); XACT_TRACK_PROPERTIES
  report loopCount n.
* Only track NEW_ON_LOOP separates the passes into complete waves, each the next candidate
  (random may repeat one, shuffle alternates the two). Without it one wave of the first drawn
  candidate is held, whatever the variation type and whatever the effect NEW_ON_LOOP bit.
* A held wave with a loop region plays prefix + (n+1) region passes + suffix; NEW_ON_LOOP waves
  each play the whole wave once. The track duration property is 1000 + 500n ms for both.
* With the pitch NEW_ON_LOOP the held wave's 101-pass hold varies by about 1 % between six runs,
  without it by about 10 %: its pitch varies over the passes (statistical, from holds).

A pass is the hold of the same matrix and architecture's one-wave control (660-700 ms under Wine's
XAudio2); the captures bound callback delivery and polled properties, not native PCM. Not
measured: volume/filter effect NEW_ON_LOOP bits, ADD operators, more than two candidates or other
selection types, raw 255, streaming, cross-bank candidates.
