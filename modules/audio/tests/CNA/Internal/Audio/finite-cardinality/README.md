# HND-XACT-05 finite-loop cardinality inputs

Original CC0-1.0 fixtures, authored by the genuine XACT3.6 SDK builder (XactBld3) from the
`shapes.xap` next to each `shapes.xsb`. Exact probe, EXE, Microsoft DLL, builder, bank and
matrix hashes are in PROVENANCE.json; the measured lifecycle of every variant is ORACLE.json.
No Microsoft binaries are distributed.

The starting point is HND-XACT-02's samebank shape, which played four waves for raw loop
byte3: one ordered PlayWaveTrackVariation (type3) with NEW_ON_LOOP over A0/A1 in bank A,
flags byte0x0c, plus a marker event at 250 ms. `base/raw3` is byte-identical to that bank apart
from its build timestamp. Each other variant changes exactly one authored field, which
check-xact-finite-cardinality-oracle.py verifies byte by byte:

* `base/raw0..4` -- the loop byte.
* `flags0/raw0..4` -- the flags byte 0x00 (`Use Center Speaker = 0`, `New Speaker Position On
  Loop = 0`). `explore/` shows the builder's bit mapping: 0x04 Use Center Speaker, 0x08 New
  Speaker Position On Loop; both default to on, which is the SDK's 0x0c.
* `nol0/raw0..4` -- NEW_ON_LOOP off (variation bit22).
* `type1/raw0..4` -- a PlayWave (type1) of A0 instead of the two-candidate type3 event.

`shared/` holds base/raw3's global settings and wave banks with the WAV inputs. Every other
variant's own XGS/XWB differ from them only in the build FILETIME (archived under
`cnaplan/evidence/rev002/hnd-xact/banks/`), so CNA's regressions load each variant's own sound
bank with these shared banks.

Both genuine engines (x86/x64), 56 cells with the retained TQ-XACT-31 control, the one-wave
resident control and a rerun of the HND-XACT-02 samebank bytes, two repeats of the base sweep:

* Raw byte n plays the wave n+1 times; XACT_TRACK_PROPERTIES reports loopCount n and
  duration (n+1) x 1000 ms.
* With NEW_ON_LOOP (base, flags0) every pass is a separate complete wave: n+1 PREPARED,
  PLAY, STOP and DESTROY, candidates A0,A1,A0,...
* Without it (nol0, type1) one A0 wave is held for n+1 passes: a single PREPARED/PLAY/STOP/
  DESTROY, no second candidate.
* The flags byte changes nothing.

A pass is the hold of the same architecture's one-wave control (660-700 ms under Wine's
XAudio2); the captures bound callback delivery and polled properties, not native PCM.
Not measured here: effect families4/6 and their effect NEW_ON_LOOP bits, waves with a loop region,
random/shuffle variations, raw bytes above4, streaming or cross-bank candidates. HND-XACT-06
captured the archived effect and loop-region banks and the other extensions
(`../loop-extensions/`).
