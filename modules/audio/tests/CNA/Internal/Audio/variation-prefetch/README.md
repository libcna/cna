# HND-XACT-02 variation prefetch inputs

These original CC0-1.0 fixtures were authored by the genuine XACT3.6 SDK builder.
Exact source, EXE, Microsoft DLL and fixture hashes are in PROVENANCE.json.
The Refresh-selected XACT3.6 x86/x64 engines supplied88 native cells; source probe
and driver are unchanged from HND-XACT-01. No Microsoft binaries are distributed.

Cells use the exact stem in PROVENANCE. Both architectures have retained TQ-XACT-31
and ordinary resident positive controls. samebank, ordered, ordered-streaming each
have normal lookahead50/250/500 repeats0..2, plus no-props, missingB, lateB (before
Play), lateB-playing (100ms after Play); ordered-streaming adds io1 at each lookahead.
The native checker validates all88, raw loader evidence and the following values:

* Prepare succeeds with one A0 prepared, zero played, cue4/SB128/A132/B4 (or absent0).
  After300ms preparation the successor is still not prepared.
* Near Play the next wave is prepared before first WAVE_STOP, for all lookaheads.
  Full WAVE_PLAY sequences are A0,A1,A0,A1 (samebank), A0,B0,A0,B0 (cross-bank).
  Native callbacks prove4 actual starts,4 stops,4 destructions, not merely handles.
* Missing B, including B loaded100ms after Play, yields only A0 prepared/played,
  then cue32/count0. B remains unused4 after its late load. Loading B after Prepare
  but before Play permits the complete sequence. B is irrelevant to samebank.
* Resolved bank ownership survives wave EOF and cue destruction; sound-bank
  destruction releases it. Both cross-bank owners are132 well before first EOF.
* IO callbacks forward real ReadFile/GetOverlappedResult. Their traces support
  streaming activity; reads do not independently identify RNG, preparation or PCM.

CNA regressions use the committed SDK bytes, actual voices/offline EOF and bank
ownership. They assert the measured candidate prefix and missing-B termination.
They do not assert a native700ms PCM duration, precise callback time or exact
lookahead-to-delay mapping. CNA's previous FAudio-derived finite budget was3 starts
for raw3 versus native4 here; the native checker retains4. HND-XACT-05's one-field
controls (../finite-cardinality/README.md) settled the cause -- raw n plays n+1 waves --
and HndXactPrefetch.SdkRawThreeProducesFourCompleteWaveActivations now asserts all four.
