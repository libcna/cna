# Microsoft XACT bank lifetime controls

TQ-XACT-31's own inputs are CC0-1.0: mono S16 PCM at22050Hz, A has2205 samples
of1024 and B has22050 samples of2048. `staggered` has A at0ms and B at400ms on
separate tracks; `simple` has only A, with B retained as an unused-bank control.
The XAP projects and untouched SDK XGS/XSB/XWB output are committed together.
Build each project with the existing June2010 SDK:

```text
XactBld3.exe lifetime.xap native /WINDOWS /F
```

`tools/xna-oracle/XactBankLifetimeProbe.cpp` uses the same SDK/clang/MinGW setup
as the instance-count probes. It accepts a genuine3.6 DLL path, a profile directory
and a mode. Use `keep`, `destroy-finished`, `stop`, `no-props`, `unplayed-stop`,
`unplayed-destroy`, `retryB`, `missingA` or `missingB` on staggered; simple uses
keep/unplayed-destroy/missingA/missingB. Missing A, or missing staggered B, must
fail at Prepare8ac70013 with no cue; the probe exits2 intentionally. All other
cells exit0, including simple missingB because that bank is unused.

The26-cell x86/x64 matrix has20 positives and6 expected missing-wave failures.
SoundBank stays INUSE128 while the stopped cue handle exists, then becomes0 after
Cue.Destroy. Used wave banks remain132 until SoundBank.Destroy, even after2000ms
without any cue. A partial missingB failure leaves A132 despite no cue being
returned; loading B then retry succeeds. Unused B stays4. Never-played and
no-property-getter controls preserve the same boundary. These flags specify
observable lifetime; native internal cache pointers were not inspected.

An initial prototype attempted to destroy A after its short wave ended while A
still reported INUSE; that native process exited5. The final `destroy-finished`
control checks the flag and records that destruction is skipped. It is an ownership
control, not a successful early-destruction qualification. Raw diagnostic and final
logs are preserved in campaign evidence.

Validate exact source/engine/bank identities and outcomes from the repository root:

```text
python3 tools/xna-oracle/check-xact-bank-lifetime-oracle.py <matrix.json>
```

Eight altered-evidence controls are rejected. PROVENANCE.json records hashes and
scope. Native execution uses Wine, private display and null audio. No managed,
native-Windows, PCM/gain, weighted/interactive/streaming or exact prefetch-timing
claim follows. CNA's current bank flags still retire too early; this reference task
does not repair the runtime. See `docs/xact-instance-limits.md`.
