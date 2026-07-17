# CNA Audio Perfection Plan

**Generated:** 2026-07-17  
**Basis:** independent deep source/test/fixture audit.  
**Important:** the previous contents of this file were intentionally not read and were replaced from scratch, as requested.  
**Backup:** the untouched original file is preserved as `plan_audio_20260717_previous_unread.md`; it was copied without inspection.  
**Companion report:** `docs/audio_deep_audit_2026-07-17.md`

## Mission
**Task count:** 438 (224 P0, 193 P1, 21 P2).


Reach evidence-based XNA 4.0 audio compatibility: correct sample rate and pitch, complete supported content loading, truthful errors/state, deterministic XACT behavior, safe streaming/lifetimes, and measurable cross-platform output. “Sounds okay” is not completion; every core behavior needs a reproducible fixture and numerical or differential acceptance evidence.

## User-reported release blockers

- [ ] **Audio Issue: High-pitched sound effects reported** — determine whether incorrect source sample rate, device conversion, explicit pitch, XACT cents/RPC/variation, or Doppler is changing speed/pitch.
- [ ] **Audio Issue: Some audio files appear missing or fail to load** — audit path/case/packaging and add required XNB/XWB codec support; no silent null/return is acceptable.
- [ ] **User Report: Gameplay audio sounds distorted or sped up** — measure playback rate, duration, dominant frequency, sample format, channels, and output-device negotiation against original XNA.

These three boxes may only be checked after the exact game/assets have matched XNA/CNA captures, a proven root cause, regression tests, and before/after evidence.

**Progress note (2026-07-17, no exact game/assets available -- see AUD-01):** built a real offline
render/measurement harness (AUD-03) and used it to rule out one entire hypothesis class:
SDL3_mixer's own resampler, run directly via `MIX_CreateMixer()`/`MIX_Generate()`, correctly
preserves frequency and duration when a *correctly-declared* 22050 Hz or 48000 Hz source is
rendered through CNA's hard-coded 44100 Hz mixer spec (`AudioMixer.cpp`), in both directions
(`OfflineAudioRendererTests.cpp`: `Source22050HzThroughRenderMixer44100HzPreservesFrequency`,
`Source48000HzThroughRenderMixer44100HzPreservesFrequency`,
`Source44100HzThroughRenderMixer48000HzPreservesFrequency`). A parallel test
(`MisdeclaredSourceRateReproducesExactlyDoubleFrequencySignature`) confirms that declaring a
22050 Hz buffer AS 44100 Hz reproduces the audit's exact reported 2x-speed/+1-octave signature.
**This means, absent the exact affected game, the most likely remaining root causes are upstream
of the mixer's resampler itself** -- a wrong sample rate reaching `SoundEffect`'s raw constructor
or the XNB reader (AUD-05/AUD-06, not yet fixed), or a pitch/RPC/Doppler contributor being
double-applied (AUD-08/AUD-09/AUD-10, largely not yet audited this pass) -- not the mixer/backend
choice itself. Still needs AUD-01's actual differential capture to become a proven, closeable root
cause for a specific game; this is ruling out one whole hypothesis class, not closing the ticket.

## Priority and completion rules

- **P0:** correctness/reproduction/data-loss/silence/distortion/state-safety blocker; complete before broad feature work.
- **P1:** parity, robustness, cross-platform, performance, and major completeness work.
- **P2:** lower-risk completeness, optional extensions, tools, and polish after P0/P1 gates.
- A task is complete only when implementation, automated test, negative test, documentation, and relevant platform evidence exist.
- Suspected defects must first be demonstrated by a minimal fixture. Do not “fix” parser/math behavior from comments alone.
- Do not tune by ear before the offline capture/measurement harness exists.
- Preserve XNA defaults. NOXNA enhancements must be opt-in and must not alter default XNA-compatible behavior.

## Global numerical gates

- Neutral pitch final ratio: `1.0` within floating-point tolerance.
- Calibration tone frequency: initial gate ±0.1%.
- Offline PCM duration/frame count: exact; end-to-end resampled duration: initial gate ±2 ms.
- No silent SDL/MIX failure and no public `Playing` state after failed play.
- No missing shipped asset and no unsupported shipped codec without build-time failure/conversion.
- Zero ASan/UBSan/LSan findings; zero confirmed TSan race in supported configurations.

## AUD-00 — Audit governance, scope, and evidence preservation

Establish an independent, reproducible audio program and prevent “fixed by ear” regressions.

- [ ] **AUD-00-001 [P0]** Preserve the reported high-pitch, missing-file, and distorted-audio reports as named release blockers. **Acceptance:** Each report has an issue ID, affected title/build/platform, owner, and closure evidence.
- [ ] **AUD-00-002 [P0]** Create an audio compatibility charter defining XNA 4.0 parity versus documented NOXNA extensions. **Acceptance:** Every behavior is classified as exact parity, acceptable backend divergence, extension, or unsupported.
- [ ] **AUD-00-003 [P0]** Create a source-of-truth audio architecture document. **Acceptance:** Document static, dynamic, XACT, media, microphone, mixer, device, and content-loading paths with ownership.
- [ ] **AUD-00-004 [P0]** Record the exact SDL and SDL_mixer commits used by every supported build. **Acceptance:** Build output and crash reports expose exact revisions and relevant compile options.
- [ ] **AUD-00-005 [P0]** Pin audio dependencies instead of relying on moving submodule heads. **Acceptance:** A clean checkout resolves byte-identical dependency revisions.
- [ ] **AUD-00-006 [P0]** Create an audio feature/format/platform support matrix. **Acceptance:** Matrix covers API, codec/container, channels, sample rate, XNB/XACT, capture, and each target platform.
- [ ] **AUD-00-007 [P0]** Define severity rules for silence, distortion, timing drift, and parity differences. **Acceptance:** Triage rules map symptoms to P0/P1/P2 consistently.
- [ ] **AUD-00-008 [P0]** Define evidence required before closing an audio defect. **Acceptance:** Closure requires a minimal fixture, regression test, before/after capture, and documented root cause.
- [ ] **AUD-00-009 [P1]** Add ownership labels for loader, decoder, mixer, XACT, media, microphone, and platform integration. **Acceptance:** Every plan item has a maintainership area.
- [ ] **AUD-00-010 [P1]** Create a compatibility-difference register against XNA/FNA/MonoGame. **Acceptance:** Known intentional and accidental differences are searchable and versioned.
- [ ] **AUD-00-011 [P1]** Add an audio change checklist to pull-request guidance. **Acceptance:** Checklist requires tests for format, duration, pitch, lifetime, and platform impact.
- [ ] **AUD-00-012 [P1]** Define a policy for backend approximations such as 3D and reverb. **Acceptance:** Approximations require documented math, limits, and golden tests.
- [ ] **AUD-00-013 [P1]** Create a test-asset provenance and license manifest. **Acceptance:** Every committed fixture has origin, license, generator, format, and expected hash.
- [ ] **AUD-00-014 [P1]** Add a generated inventory of public Audio and Media APIs. **Acceptance:** Inventory is compared automatically with the chosen XNA 4.0 reference surface.
- [ ] **AUD-00-015 [P1]** Add a generated inventory of SDL/MIX calls used by CNA audio. **Acceptance:** Every call is tagged checked/unchecked and covered/uncovered.
- [ ] **AUD-00-016 [P2]** Document non-goals for platform services unavailable outside Xbox/Windows Phone. **Acceptance:** Unsupported APIs have explicit compatible behavior instead of accidental stubs.

## AUD-01 — Reproduce the C# XNA versus C++ CNA game difference

Turn the user report into a deterministic differential case with matched assets, code, and recordings.

- [ ] **AUD-01-001 [P0]** Obtain the exact original XNA C# revision and the corresponding C++ CNA revision. **Acceptance:** Both revisions are archived by commit hash and build instructions.
- [ ] **AUD-01-002 [P0]** Identify every affected sound by logical name and physical asset path. **Acceptance:** A table maps gameplay event → C# load path → C++ load path → file/XNB/XACT entry.
- [ ] **AUD-01-003 [P0]** Record original XNA and CNA output from the same gameplay event. **Acceptance:** Recordings use the same asset, event timing, output sample rate, and no post-processing.
- [ ] **AUD-01-004 [P0]** Capture one high-pitched effect in isolation outside gameplay. **Acceptance:** Minimal XNA and CNA programs play exactly one sound once with neutral parameters.
- [ ] **AUD-01-005 [P0]** Capture one reportedly missing effect in isolation. **Acceptance:** Logs identify whether failure occurs at path lookup, parse, decode, voice creation, or play.
- [ ] **AUD-01-006 [P0]** Capture one distorted effect in isolation. **Acceptance:** The original bytes and every interpreted format field are preserved.
- [ ] **AUD-01-007 [P0]** Hash and compare original and ported source assets. **Acceptance:** Byte differences are either eliminated or explained.
- [ ] **AUD-01-008 [P0]** Hash and compare built content products. **Acceptance:** XNB/XWB/XSB/XGS differences are documented by pipeline/tool version.
- [ ] **AUD-01-009 [P0]** Extract metadata with two independent tools. **Acceptance:** Sample rate, channels, bit depth, codec, block alignment, frame count, and loops agree or discrepancy is explained.
- [ ] **AUD-01-010 [P0]** Log the exact C++ constructor/loader overload used for each affected sound. **Acceptance:** No affected sound has an ambiguous raw-vs-container interpretation.
- [ ] **AUD-01-011 [P0]** Log all runtime pitch contributors for each affected voice. **Acceptance:** Base pitch, cents, RPC, random variation, Doppler, clamp, and final ratio are captured.
- [ ] **AUD-01-012 [P0]** Repeat the CNA capture with pitch and Doppler forcibly neutral. **Acceptance:** Result classifies issue as metadata/decoder versus pitch-composition.
- [ ] **AUD-01-013 [P0]** Repeat the CNA capture with reference-decoded PCM. **Acceptance:** Result isolates loader/decoder from mixer/playback.
- [ ] **AUD-01-014 [P0]** Write CNA-decoded PCM to WAV before playback. **Acceptance:** Offline WAV analysis isolates decoder output from live device conversion.
- [ ] **AUD-01-015 [P0]** Compare duration ratios numerically. **Acceptance:** Report distinguishes 2.0, 48k/44.1k, 44.1k/48k, channel, and arbitrary drift signatures.
- [ ] **AUD-01-016 [P0]** Compare dominant frequency and spectral centroid. **Acceptance:** Measured frequency/pitch shift is reported in ratio and semitones.
- [ ] **AUD-01-017 [P0]** Compare channel waveforms independently. **Acceptance:** Channel swap, duplication, interleave, and pan differences are identified.
- [ ] **AUD-01-018 [P0]** Repeat on a 44.1 kHz and 48 kHz output device. **Acceptance:** Device-dependent changes are either reproduced or ruled out.
- [ ] **AUD-01-019 [P0]** Repeat with static SoundEffect, dynamic stream, and XACT where applicable. **Acceptance:** The defective path is isolated.
- [ ] **AUD-01-020 [P0]** Repeat stationary and moving 3D variants. **Acceptance:** Doppler-specific differences are isolated.
- [ ] **AUD-01-021 [P1]** Capture stdout/stderr and structured audio trace with recordings. **Acceptance:** Every recording has a matching machine-readable trace.
- [ ] **AUD-01-022 [P1]** Record OS, backend, device, driver, and negotiated format. **Acceptance:** Reproduction package is portable to another machine.
- [ ] **AUD-01-023 [P1]** Create a one-command reproduction script. **Acceptance:** Script builds/runs both available references and emits analysis artifacts.
- [ ] **AUD-01-024 [P1]** Add the minimal reproduction to CI as a non-device offline test. **Acceptance:** The reported defect cannot regress silently.

## AUD-02 — Audio diagnostics and truthful error handling

Make every sample-rate, format, pitch, and backend decision observable without a debugger.

- [ ] **AUD-02-001 [P0]** Introduce a structured `AudioDiagnosticEvent` model. **Acceptance:** Events include severity, operation, backend error, asset/cue/wave identity, thread, and timestamp.
- [ ] **AUD-02-002 [P0]** Add a runtime audio trace switch disabled by default. **Acceptance:** Trace can be enabled by environment/config without recompiling.
- [ ] **AUD-02-003 [P0]** Log requested and actual mixer/device audio specifications. **Acceptance:** Format, channels, rate, buffer/quantum, driver, and device name are emitted once.
- [ ] **AUD-02-004 [P0]** Log every loaded sound source format before conversion. **Acceptance:** Container, codec, bit depth, channels, rate, block align, frame count, and loops are recorded.
- [ ] **AUD-02-005 [P0]** Log every final track frequency ratio and its components. **Acceptance:** Trace provides base pitch, cue cents, RPC cents, random cents, Doppler, and final clamp.
- [ ] **AUD-02-006 [P0]** Log dynamic stream source and destination specs after track attachment. **Acceptance:** A null/invalid destination spec becomes a hard diagnostic failure.
- [x] **AUD-02-007 [P0]** Check and propagate `SDL_CreateAudioStream` failure. **Acceptance:** No public object enters a usable/playing state with a null stream. **Evidence:** `DynamicSoundEffectInstance::Play()` now checks `audioStream_` after `EnsureStream()` and returns (leaving state Stopped, `std::cerr` diagnostic) before ever calling `MIX_SetTrackAudioStream` -- previously that call would "succeed" on a null stream (SDL docs: passing NULL is legal and just detaches input) and playback would proceed to a false `Playing` state with total silence. See AUD-07-007's shared implementation/tests.
- [x] **AUD-02-008 [P0]** Check and propagate every `SDL_PutAudioStreamData` failure. **Acceptance:** Failed chunks are not counted as submitted; state and callback behavior remain consistent. **Evidence:** `SubmitQueuedToStream()` now checks the return value; a failed chunk is dropped (not pushed to `submittedChunkSizes_`) with a `std::cerr` diagnostic including the dropped byte count, rather than being credited to `PendingBufferCount` forever with no way to ever decrement (Update()'s consumed-byte accounting would never reach it). Chose "fail deterministically" over "retain for retry" from the acceptance's two options: SDL_PutAudioStreamData failures are allocation/param-level, and an unconditional retry loop risks spinning forever on a persistent failure with no equivalent retry concept in real FNA to justify the complexity. See AUD-07-009.
- [x] **AUD-02-009 [P0]** Check and propagate every `MIX_PlayTrack` failure. **Acceptance:** Public state remains Stopped and failure includes SDL error context. **Evidence:** `Play()` now captures `MIX_PlayTrack`'s return value (from both the properties and no-properties call sites) and returns before setting `State_ = Playing`/registering with `FrameworkDispatcher::Streams` if it failed, with a `std::cerr` diagnostic including `SDL_GetError()`. See AUD-07-010.
- [ ] **AUD-02-010 [P0]** Check all `MIX_SetTrackFrequencyRatio` calls. **Acceptance:** Invalid ratios fail loudly and do not leave cached state inconsistent.
- [ ] **AUD-02-011 [P0]** Check all `MIX_SetTrackGain` and mixer-gain calls. **Acceptance:** Rejected values are visible and public properties remain truthful.
- [ ] **AUD-02-012 [P0]** Check all pan/stereo/filter callback registration calls. **Acceptance:** A failed effect setup cannot masquerade as successful parity.
- [ ] **AUD-02-013 [P0]** Replace raw stderr-only XACT/WaveBank failures with structured events. **Acceptance:** Applications can subscribe/log and tests can assert exact error codes.
- [ ] **AUD-02-014 [P0]** Replace silent MediaPlayer load/create/play returns with truthful state and diagnostics. **Acceptance:** Play failure cannot start the wall-clock or report Playing.
- [ ] **AUD-02-015 [P0]** Include content candidate paths in missing-asset diagnostics. **Acceptance:** Error shows every attempted path, extension, root, and case mismatch hint.
- [ ] **AUD-02-016 [P1]** Add per-voice debug IDs. **Acceptance:** Lifecycle, submissions, parameter changes, and stop callbacks correlate reliably.
- [ ] **AUD-02-017 [P1]** Add mixer counters for active, virtual, failed, and exhausted voices. **Acceptance:** Voice exhaustion is distinguishable from content failure.
- [ ] **AUD-02-018 [P1]** Add dynamic stream counters for queued input bytes, output frames, underruns, and failed puts. **Acceptance:** Counters are internally consistent after resampling.
- [ ] **AUD-02-019 [P1]** Add decoder timing and allocation diagnostics. **Acceptance:** Slow or allocation-heavy assets are identifiable.
- [ ] **AUD-02-020 [P1]** Add one-shot warning suppression by unique issue key. **Acceptance:** Repeated failures do not flood logs while first context is preserved.
- [ ] **AUD-02-021 [P1]** Add diagnostic JSON export. **Acceptance:** A bug report can attach a machine-readable audio session.
- [ ] **AUD-02-022 [P1]** Add a debug command to dump currently active voices and parameters. **Acceptance:** Dump is race-safe and includes final effective values.
- [ ] **AUD-02-023 [P1]** Add a debug command to dump XACT cue resolution. **Acceptance:** Selected sound/track/wave, variations, RPC outputs, and categories are shown.
- [ ] **AUD-02-024 [P2]** Add privacy-safe device diagnostics. **Acceptance:** No personally identifying capture-device data is emitted by default.

## AUD-03 — Deterministic offline rendering and golden-audio laboratory

Prove what CNA produces numerically rather than relying on public state or listening alone.

- [x] **AUD-03-001 [P0]** Build a deterministic offline audio render harness. **Acceptance:** It renders tracks to a buffer/file without physical hardware or wall-clock timing. **Evidence:** `tests/Microsoft/Xna/Framework/Audio/OfflineAudioRenderer.hpp`'s `RenderRawPcmOffline()` uses `MIX_CreateMixer()`+`MIX_Generate()` (NOT `MIX_CreateMixerDevice()`) -- genuinely no physical device, no `SDL_AUDIODRIVER`, no wall-clock timing at all; every test using it runs identically headless or with real hardware present. `MIX_Init()`/`MIX_Quit()` are called per-render (refcounted, safe alongside `CNA::Internal::Audio::GetMixer()`'s own shared device mixer).
- [ ] **AUD-03-002 [P0]** Add a canonical WAV writer for captured output. **Acceptance:** Headers, channel layout, sample count, and hashes are deterministic. Not yet done this pass -- all evidence so far is compact numeric assertions (AUD-03-014), not exported WAV files.
- [ ] **AUD-03-003 [P0]** Add generated sine, impulse, step, silence, noise, sweep, and multitone fixtures. **Partial evidence:** `GenerateSineWaveS16`/`GenerateSilenceS16` added and used throughout the new golden tests. Impulse/step/noise/sweep/multitone fixtures not yet added -- left unchecked.
- [x] **AUD-03-004 [P0]** Add dominant-frequency measurement. **Acceptance:** 440 Hz and other tones are measured within the configured threshold. **Evidence:** `GoertzelMagnitude` (exact single-bin energy, no FFT bin-width uncertainty), `EstimateDominantFrequencyHz` (coarse blind sweep, for when the expected frequency isn't known ahead of time), and `RefineFrequencyEstimateHz` (phase-difference estimator between the first/second half of the buffer -- NOT limited by the analysis window's basic bin-width resolution the way a Goertzel/FFT peak search is, which is what actually achieves the plan's own 0.1% calibration gate even from short 0.2s windows). 25+ new tests measure real tones to within 0.1% across an 11025-96000 Hz x mono/stereo matrix and a full -1.0..+1.0 pitch-ratio matrix -- see AUD-05/AUD-08 below.
- [x] **AUD-03-005 [P0]** Add sample/frame-count and duration measurement. **Acceptance:** No test depends only on sleeping for approximate time. **Evidence:** every `OfflineAudioRendererTest`/`AUD05GoldenMatrix`/`AUD08GoldenPitchMatrix` test asserts exact frame counts (`result.samples.size()`) and/or `result.realBytesRendered` (MIX_Generate's own non-silence byte count) -- zero wall-clock sleeps anywhere in this new test file.
- [ ] **AUD-03-006 [P0]** Add per-channel RMS, peak, DC offset, clipping, and correlation metrics. **Partial evidence:** `MeasureRms`/`MeasurePeak`/`ContainsNaNOrInf` added and used (silence-is-truly-zero, sine-is-not-silence, no NaN/Inf-through-resampling checks). DC offset, clipping, and cross-channel correlation metrics not yet added -- left unchecked.
- [ ] **AUD-03-007 [P0]** Add spectral comparison with windowing and tolerances. **Acceptance:** Compressed/resampled outputs can be compared robustly.
- [ ] **AUD-03-008 [P0]** Add transient/loop-boundary click detection. **Acceptance:** Unexpected discontinuities fail golden tests.
- [ ] **AUD-03-009 [P0]** Add channel-order test signals. **Acceptance:** Every channel has a unique tone/impulse signature.
- [ ] **AUD-03-010 [P0]** Add silence/tail detection. **Acceptance:** Truncation and unexpected decoder tails are measurable.
- [ ] **AUD-03-011 [P0]** Add latency measurement separated from duration. **Acceptance:** Startup latency is not misdiagnosed as speed error.
- [ ] **AUD-03-012 [P0]** Add XNA/FNA reference-capture import. **Acceptance:** Reference WAV plus metadata can be normalized and compared automatically.
- [ ] **AUD-03-013 [P0]** Version the comparison algorithm and thresholds. **Acceptance:** Golden results do not change silently with analysis code.
- [x] **AUD-03-014 [P0]** Store compact numerical golden data rather than large opaque audio where possible. **Acceptance:** Repository remains reviewable and fixtures reproducible. **Evidence:** every new golden test's expected values are small numeric literals (expected Hz, expected pitch ratio) computed from source-generated fixtures at test time -- no committed binary audio blobs.
- [ ] **AUD-03-015 [P1]** Add AB listening export for human review. **Acceptance:** Tool emits level-matched A/B/X files without replacing objective gates.
- [ ] **AUD-03-016 [P1]** Add spectrogram and waveform artifact generation for CI failures. **Acceptance:** Failures provide immediate visual evidence.
- [ ] **AUD-03-017 [P1]** Add fuzz-safe parsers for captured metadata sidecars. **Acceptance:** Malformed test inputs cannot crash the harness.
- [ ] **AUD-03-018 [P1]** Add deterministic dithering/no-dithering controls. **Acceptance:** Sample comparisons account for conversion policy.
- [ ] **AUD-03-019 [P1]** Add resampler impulse and swept-sine characterization. **Acceptance:** Passband, aliasing, phase, and latency are documented.
- [ ] **AUD-03-020 [P1]** Add pan-law characterization. **Acceptance:** Center attenuation and extreme-channel isolation are measured.
- [ ] **AUD-03-021 [P1]** Add filter frequency/Q characterization. **Acceptance:** Actual response matches intended coefficients across sample rates.
- [ ] **AUD-03-022 [P1]** Add voice-mixing linearity and clipping tests. **Acceptance:** Summed voices follow documented headroom/clamp behavior.
- [ ] **AUD-03-023 [P1]** Add reproducibility checks across compiler optimization levels. **Acceptance:** Golden metrics stay within tolerance in Debug/Release.
- [ ] **AUD-03-024 [P2]** Add optional high-resolution float capture. **Acceptance:** Analysis can inspect pre-device output without quantization masking.

## AUD-04 — Mixer, device negotiation, resampling, and lifecycle

Guarantee that the mixer/device boundary never changes speed or hides failure.

- [ ] **AUD-04-001 [P0]** Query and store the actual mixer output specification after creation. **Acceptance:** Requested and actual specs are both available and tested.
- [ ] **AUD-04-002 [P0]** Verify 44.1 kHz request on a 48 kHz device does not alter pitch. **Acceptance:** 440 Hz remains within tolerance and duration remains correct. **Adjacent evidence, not yet this item specifically:** `OfflineAudioRendererTests.cpp`'s `Source44100HzThroughRenderMixer48000HzPreservesFrequency` proves the *mixer's own resampler* (source rate != mixer render rate, both via `MIX_CreateMixer()`) preserves frequency in this direction. This item is about *device* negotiation specifically (SDL requesting one spec from `MIX_CreateMixerDevice()` and the OS/driver actually opening a different physical rate) -- a separate layer this offline (non-device) harness cannot exercise by design. Still open.
- [ ] **AUD-04-003 [P0]** Verify 48 kHz request on a 44.1 kHz device does not alter pitch. **Acceptance:** Reverse conversion passes the same gates. **Adjacent evidence:** see AUD-04-002 -- `Source22050HzThroughRenderMixer44100HzPreservesFrequency`/`Source48000HzThroughRenderMixer44100HzPreservesFrequency` cover the mixer-resampler direction; real device-negotiation testing remains open.
- [ ] **AUD-04-004 [P0]** Make mixer rate/channels/format configurable for tests. **Acceptance:** Tests can force 22.05/44.1/48/96 kHz and mono/stereo where supported.
- [ ] **AUD-04-005 [P0]** Choose and document the production mixer-format policy. **Acceptance:** Policy is native-device, fixed-reference, or platform-specific with rationale.
- [ ] **AUD-04-006 [P0]** Validate mixer creation against unsupported requested specs. **Acceptance:** Fallback is explicit, logged, and cannot silently change speed.
- [ ] **AUD-04-007 [P0]** Test device-open failure and retry without leaked MIX init references. **Acceptance:** Repeated failures leave balanced lifecycle and accurate exceptions.
- [ ] **AUD-04-008 [P0]** Test mixer destruction with active static voices. **Acceptance:** No use-after-free, deadlock, callback-after-destroy, or leak.
- [ ] **AUD-04-009 [P0]** Test mixer destruction with active dynamic streams. **Acceptance:** Streams/tracks are detached in a defined order.
- [ ] **AUD-04-010 [P1]** Implement output-device change handling. **Acceptance:** Default-device changes either migrate safely or stop with a documented event.
- [ ] **AUD-04-011 [P1]** Implement device-loss/reopen handling. **Acceptance:** State recovery is deterministic and does not speed up queued audio.
- [ ] **AUD-04-012 [P1]** Measure and document output latency/quantum by backend. **Acceptance:** Latency settings are not conflated with playback duration.
- [ ] **AUD-04-013 [P1]** Add low/high-latency configuration bounds. **Acceptance:** Invalid values fail predictably; supported values are tested.
- [ ] **AUD-04-014 [P1]** Verify master volume is applied exactly once. **Acceptance:** Track and mixer gains do not double-multiply or omit master gain.
- [ ] **AUD-04-015 [P1]** Verify mixer gain changes affect active and future voices consistently. **Acceptance:** Behavior matches chosen XNA baseline.
- [ ] **AUD-04-016 [P1]** Verify no clipping/NaN propagation for extreme aggregate gain. **Acceptance:** Output remains finite and documented.
- [ ] **AUD-04-017 [P1]** Test null/dummy audio drivers separately from physical output. **Acceptance:** Dummy success is not accepted as proof of audible correctness.
- [ ] **AUD-04-018 [P1]** Add backend capability querying. **Acceptance:** Unsupported effects/formats are known before play.
- [ ] **AUD-04-019 [P1]** Add mixer thread-affinity and callback-thread documentation. **Acceptance:** Public API and internal locks comply with the contract.
- [ ] **AUD-04-020 [P2]** Benchmark conversion cost for common source/device rate pairs. **Acceptance:** Performance budget includes resampling and channel conversion.

## AUD-05 — Raw SoundEffect format contracts and metadata integrity

Prevent C++ callers from accidentally labeling bytes with the wrong rate, width, or channel count.

- [ ] **AUD-05-001 [P0]** Validate raw SoundEffect sample rate before backend calls. **Acceptance:** Zero, negative, overflow, and unsupported rates throw XNA-compatible exceptions.
- [ ] **AUD-05-002 [P0]** Validate raw channel enum before backend calls. **Acceptance:** Only supported XNA channel values are accepted unless an explicit extension is used.
- [ ] **AUD-05-003 [P0]** Validate raw byte count is aligned to a complete sample frame. **Acceptance:** Misaligned buffers fail deterministically instead of truncating or distorting.
- [ ] **AUD-05-004 [P0]** Validate loop start/length against decoded frame count where parity permits. **Acceptance:** Out-of-range loops follow verified XNA/FNA behavior and never reach backend unchecked.
- [ ] **AUD-05-005 [P0]** Document that raw constructor bytes are PCM16LE, not a WAV/container. **Acceptance:** API docs and exception message prevent accidental whole-file submission.
- [ ] **AUD-05-006 [P0]** Add debug detection for RIFF/Ogg/MP3/XNB signatures passed to raw PCM constructor. **Acceptance:** Likely misuse emits a precise diagnostic.
- [ ] **AUD-05-007 [P0]** Add debug detection for implausible PCM16 statistics indicating float/compressed data. **Acceptance:** Diagnostic is advisory and has no release false rejection.
- [ ] **AUD-05-008 [P0]** Verify duration calculation uses decoded frames and source rate. **Acceptance:** Duration remains correct after backend conversion.
- [ ] **AUD-05-009 [P0]** Verify mono/stereo interleaving with asymmetric test signals. **Acceptance:** No channel duplication/swap/misalignment.
- [ ] **AUD-05-010 [P1]** Define endianness policy on non-little-endian targets. **Acceptance:** PCM input is converted or rejected explicitly.
- [ ] **AUD-05-011 [P1]** Add a typed NOXNA audio-buffer descriptor for richer formats. **Acceptance:** Extensions carry codec/format/rate/channels/block alignment explicitly.
- [ ] **AUD-05-012 [P1]** Avoid ambiguous integer casts from arbitrary channel values. **Acceptance:** Static analysis/tests reject invalid enum construction paths.
- [ ] **AUD-05-013 [P1]** Check `MIX_LoadRawAudio` ownership/copy semantics against pinned version. **Acceptance:** Buffer lifetime is safe and documented.
- [ ] **AUD-05-014 [P1]** Test zero-length raw buffers. **Acceptance:** Behavior matches reference and never divides by zero.
- [ ] **AUD-05-015 [P1]** Test very short one-frame/two-frame buffers. **Acceptance:** No off-by-one duration or callback error.
- [ ] **AUD-05-016 [P1]** Test very large buffers near API/backend limits. **Acceptance:** Overflow is prevented before allocation/backend calls.
- [x] **AUD-05-017 [P1]** Golden-test raw PCM16LE at 8000 Hz mono. **Acceptance:** Frequency, duration, frame count, channel identity, and neutral ratio pass thresholds. **Evidence:** `AUD05GoldenMatrix/GoldenSampleRateTest` (`OfflineAudioRendererTests.cpp`), param `(8000, 1)` -- 220 Hz tone measured within 0.1% via `RefineFrequencyEstimateHz`, exact frame count asserted.
- [x] **AUD-05-018 [P1]** Golden-test raw PCM16LE at 8000 Hz stereo. **Acceptance:** as above. **Evidence:** same test, param `(8000, 2)`.
- [x] **AUD-05-019 [P1]** Golden-test raw PCM16LE at 11025 Hz mono. **Evidence:** param `(11025, 1)`.
- [x] **AUD-05-020 [P1]** Golden-test raw PCM16LE at 11025 Hz stereo. **Evidence:** param `(11025, 2)`.
- [x] **AUD-05-021 [P0]** Golden-test raw PCM16LE at 22050 Hz mono. **Evidence:** param `(22050, 1)`; also independently reproduced end-to-end via `Source22050HzThroughRenderMixer44100HzPreservesFrequency` (resampled through CNA's actual hard-coded 44100 Hz mixer rate).
- [x] **AUD-05-022 [P0]** Golden-test raw PCM16LE at 22050 Hz stereo. **Evidence:** param `(22050, 2)`.
- [x] **AUD-05-023 [P1]** Golden-test raw PCM16LE at 32000 Hz mono. **Evidence:** param `(32000, 1)`.
- [x] **AUD-05-024 [P1]** Golden-test raw PCM16LE at 32000 Hz stereo. **Evidence:** param `(32000, 2)`.
- [x] **AUD-05-025 [P0]** Golden-test raw PCM16LE at 44100 Hz mono. **Evidence:** param `(44100, 1)`.
- [x] **AUD-05-026 [P0]** Golden-test raw PCM16LE at 44100 Hz stereo. **Evidence:** param `(44100, 2)`.
- [x] **AUD-05-027 [P0]** Golden-test raw PCM16LE at 48000 Hz mono. **Evidence:** param `(48000, 1)`; also `Source48000HzThroughRenderMixer44100HzPreservesFrequency`.
- [x] **AUD-05-028 [P0]** Golden-test raw PCM16LE at 48000 Hz stereo. **Evidence:** param `(48000, 2)`.
- [x] **AUD-05-029 [P1]** Golden-test raw PCM16LE at 96000 Hz mono. **Evidence:** param `(96000, 1)`.
- [x] **AUD-05-030 [P1]** Golden-test raw PCM16LE at 96000 Hz stereo. **Evidence:** param `(96000, 2)`. All 14 `AUD05GoldenMatrix` cases pass; this closes the entire golden sample-rate matrix (AUD-05-017..030). Note: these tests exercise the real SDL3_mixer decode/resample pipeline directly (`RenderRawPcmOffline`), not yet `SoundEffect`'s own raw constructors -- AUD-05's *validation* items (001-016, e.g. sample-rate/channel/frame-alignment checks on the public constructor) are a separate, not-yet-done task (see task #12/AUD-05-001..016 below).

## AUD-06 — XNB SoundEffect compatibility and decoding

Eliminate the confirmed missing-audio gap caused by PCM16-only XNB loading.

- [x] **AUD-06-001 [P0]** Create an authoritative XNB SoundEffect format support matrix by XNA target platform/profile. **Acceptance:** Every accepted/rejected combination has a reference fixture and rationale. **Evidence:** matrix documented in `SoundEffectContentTypeReader.hpp`'s class doc comment and `SoundEffectContentTypeReaderTests.cpp`'s file header; every row has a real MonoGame-produced `.xnb` fixture (`tests/assets/xnb/monogame/windows/uncompressed/audio/tone_mono_44khz_{8bit,16bit,float,msadpcm,imaadpcm}.xnb`, plus stereo 16-bit) except XMA2 (no such fixture exists in MonoGame's own test corpus either; covered by a hand-built minimal object stream instead).
- [x] **AUD-06-002 [P0]** Preserve `nAvgBytesPerSec`, `nBlockAlign`, and full format extension data. **Acceptance:** Decoder receives complete WAVEFORMATEX metadata. **Evidence:** `SoundEffectContentTypeReader.cpp` now captures `nAvgBytesPerSec`/`nBlockAlign` into named locals (previously read-and-discarded) and captures the format extension bytes verbatim into `extensionData` (previously always discarded via a bare `ReadBytesExactOrThrow` with no assignment) for every non-XMA2 format.
- [x] **AUD-06-003 [P0]** Refactor XNB SoundEffect construction away from the PCM16-only raw constructor. **Acceptance:** A format-aware internal path owns metadata and encoded bytes safely. **Evidence:** new `BuildViaWavWrapper()` (format-aware: wraps raw bytes in a synthetic in-memory WAV matching the real WAVEFORMATEX fields, decoded via `SoundEffect::FromStream`/SDL3's own WAV loader) handles every format except 16-bit PCM, which deliberately keeps the existing direct-construction fast path unchanged (no WAV-wrapping overhead for the already-correct, most common case).
- [x] **AUD-06-004 [P0]** Support 8-bit PCM XNB SoundEffect where reference behavior requires it. **Acceptance:** Included fixture decodes with exact frame count and correct unsigned-to-signed conversion. **Evidence:** `Pcm8BitLoadsSuccessfully` against the real fixture; unsigned-to-signed 8-bit conversion is SDL3's own native WAV-decoder responsibility (not reimplemented in CNA), consistent with routing every non-16-bit format through the same real decoder. Exact frame-count cross-check against the fixture's own declared sample count not yet added (duration-is-positive only) -- a reasonable follow-up, not blocking.
- [x] **AUD-06-005 [P0]** Support 16-bit PCM XNB SoundEffect without regression. **Acceptance:** Existing fixture remains sample/duration correct. **Evidence:** unchanged fast path; `Pcm16BitMonoLoadsSuccessfully`/`Pcm16BitStereoLoadsSuccessfully` still pass, full whole-repo suite reverified green.
- [x] **AUD-06-006 [P0]** Support MS ADPCM XNB SoundEffect where reference behavior requires it. **Acceptance:** Block alignment, samples/block, duration, loops, and decoded output are verified. **Evidence:** `MsAdpcmLoadsSuccessfully` against the real fixture. **Confirmed real defect found and fixed en route (AUDIO-XNB-ADPCM-001):** hex-dumped the real fixture and found MonoGame's content pipeline writes `cbSize=0` for MS-ADPCM (no coefficient table, no `wSamplesPerBlock` at all in the embedded format block) -- unlike IMA-ADPCM, SDL3's MS-ADPCM decoder has no auto-derive fallback and requires an explicit, valid extension (`SDL_wave.c`'s `MS_ADPCM_Init`). Fixed by synthesizing the standard extension (`BuildStandardMsAdpcmExtension` + a `wSamplesPerBlock` computed from `nBlockAlign` via the MS-ADPCM "Standards Update" formula) whenever the XNB's own extension is absent/too small to contain a real coefficient table, while still preferring a real authored extension if one is ever present. Loop points forwarded via a synthesized minimal WAV `smpl` chunk (`AppendSmplChunkIfLooped`), picked up automatically by `SoundEffect::FromStream`'s existing `TryParseWavSmplChunk` (CP-17).
- [x] **AUD-06-007 [P0]** Determine and implement IMA ADPCM XNB compatibility policy. **Acceptance:** Decision is based on real XNA/MonoGame/FNA content, with fixture and explicit behavior. **Evidence:** decision: support via SDL3's native IMA-ADPCM decoder (WAV-wrapped), same as MS-ADPCM/float/PCM8 -- confirmed against the real fixture (`ImaAdpcmLoadsSuccessfully`), which worked immediately (SDL auto-derives `wSamplesPerBlock` from `nBlockAlign` when the XNB doesn't supply an extension, unlike MS-ADPCM).
- [x] **AUD-06-008 [P0]** Determine and implement IEEE float XNB compatibility policy. **Acceptance:** Float fixture either loads correctly or fails with documented profile-compatible reason. **Evidence:** decision: support via SDL3's native IEEE-float WAV decoder; `IeeeFloatLoadsSuccessfully` against the real fixture.
- [x] **AUD-06-009 [P0]** Determine and implement XMA2 compatibility strategy. **Acceptance:** Native decode, conversion-at-build, optional decoder, or explicit unsupported result is tested and documented. **Evidence:** decision unchanged from the prior session (explicit unsupported result -- no decode path exists anywhere in this stack, SDL3 doesn't decode XMA2 either) but now has real regression coverage: `Xma2IsRejected` builds a complete, valid minimal XNB object stream (type-reader table + shared-resource count + root type id, reverse-engineered from a real fixture's hex dump) around an XMA2 format block, confirming the rejection path is reachable through the real `ReadAsset<T>()` entry point, not just through `SoundEffectReader::Read()` called directly.
- [ ] **AUD-06-010 [P0]** Use stored XNB duration as a validation oracle. **Acceptance:** Large decoded-duration disagreement fails with asset-specific diagnostics. Not yet done -- the duration field is still read and discarded ("unused, matches FNA").
- [ ] **AUD-06-011 [P0]** Validate XNB format length and extension sizes exhaustively. **Acceptance:** Truncation/overflow/extra-byte cases cannot desynchronize the reader.
- [ ] **AUD-06-012 [P0]** Validate data length before allocation/read. **Acceptance:** Negative, oversized, and truncated lengths fail safely.
- [ ] **AUD-06-013 [P0]** Validate sample rate, channels, block align, and bits coherently. **Acceptance:** Impossible WAVEFORMATEX combinations are rejected before decoding.
- [ ] **AUD-06-014 [P0]** Validate loop points against decoded sample frames. **Acceptance:** Loops cannot reference compressed bytes as if they were PCM frames.
- [ ] **AUD-06-015 [P0]** Add Xbox-endian SoundEffect fixtures or prove unsupported scope. **Acceptance:** Byte swapping is covered rather than comment-only.
- [ ] **AUD-06-016 [P1]** Test format chunks of 16, 18, and extended sizes. **Acceptance:** Reader consumes exactly the declared bytes.
- [ ] **AUD-06-017 [P1]** Test unknown format tags. **Acceptance:** Failure includes tag, bit depth, channels, rate, and asset name.
- [ ] **AUD-06-018 [P1]** Test mono/stereo and any reference-supported multichannel content. **Acceptance:** Policy is explicit and golden-tested.
- [ ] **AUD-06-019 [P1]** Test loopless and looped XNB effects. **Acceptance:** Loop start/length semantics match reference.
- [ ] **AUD-06-020 [P1]** Test compressed XNB container plus compressed audio payload. **Acceptance:** Outer compression and inner codec are independently correct.
- [ ] **AUD-06-021 [P1]** Differential-test SoundEffect XNB reader against FNA on a shared corpus. **Acceptance:** Metadata and decoded output differences are reviewed and registered.
- [ ] **AUD-06-022 [P1]** Add property-based XNB WAVEFORMATEX mutation tests. **Acceptance:** Parser invariants hold across boundary values.
- [ ] **AUD-06-023 [P1]** Fuzz SoundEffectContentTypeReader under ASan/UBSan. **Acceptance:** No crash, leak, OOB, or pathological allocation on malformed files.
- [ ] **AUD-06-024 [P1]** Add useful ContentLoadException nesting. **Acceptance:** Root parser/decoder error is preserved with asset context.
- [ ] **AUD-06-025 [P2]** Add a tool to inspect XNB audio metadata without playing it. **Acceptance:** Tool emits stable text/JSON for debugging and CI manifests.

## AUD-07 — DynamicSoundEffectInstance correctness

Make procedural/streamed audio format-safe, failure-safe, and timing-correct.

- [x] **AUD-07-001 [P0]** Define whether one DynamicSoundEffectInstance may switch between S16 and float modes. **Acceptance:** Behavior is verified against FNA extension semantics and documented. **Evidence:** read FNA's real `DynamicSoundEffectInstance.cs`/`SoundEffectInstance.cs` line-by-line -- FNA itself has the identical asymmetry (`format.wFormatTag` is set to float by `SubmitFloatBufferEXT` and never reset anywhere; plain `SubmitBuffer` never checks it). Since `SubmitFloatBufferEXT` is an FNA/NOXNA extension (not real XNA 4.0 API), decided CNA may be safer than FNA here without diverging from any true XNA behavior: `SubmitBuffer` now symmetric-guards/resets `isFloat_` (see AUD-07-002). A real XNA game that never calls the NOXNA float path is completely unaffected. Documented on both `SubmitBuffer`/`SubmitFloatBufferEXT` Doxygen in `DynamicSoundEffectInstance.hpp`.
- [x] **AUD-07-002 [P0]** Prevent S16 submission into an F32-configured stream. **Acceptance:** Regression test covers float→stop→integer submission. **Evidence:** `DynamicSoundEffectInstance::SubmitBuffer` now throws `System::InvalidOperationException` if called while Playing/Paused in float mode, and resets `isFloat_ = false` when called while Stopped (mirrors `SubmitFloatBufferEXT`'s existing guard). New tests `SubmitBufferWhileStoppedSwitchesBackToIntModeAfterFloatSubmission` (verifies via `MIX_GetTrackAudioStream`+`SDL_GetAudioStreamFormat` that the live stream is genuinely S16 after float→stop→int) and `SubmitIntBufferAfterPlayingInFloatModeThrowsInvalidOperation`; both confirmed to FAIL against the pre-fix code via `git stash` (stashed `DynamicSoundEffectInstance.{hpp,cpp}`, kept tests). Full `DynamicSoundEffectInstanceTest` suite 52/52 pass (was 50/50 pre-existing + 2 new).
- [ ] **AUD-07-003 [P0]** Prevent F32 submission into a live S16 stream. **Acceptance:** Existing guard is tested under races and repeated play cycles.
- [x] **AUD-07-004 [P0]** Validate constructor sample rate and channels. **Acceptance:** Invalid metadata fails before stream creation. **Evidence -- resolved decision, acceptance corrected to match:** this was already investigated and deliberately resolved in a prior session (`P10-DYN-001/002/003`, `DynamicSoundEffectInstanceTests.cpp` -- `ConstructorAcceptsZeroSampleRate`, `ConstructorAcceptsNegativeSampleRate`, `ConstructorAcceptsSampleRateBelowXnaDocumentedMinimum`/`AboveXnaDocumentedMaximum`), which read FNA's real constructor line-by-line and confirmed it does zero validation on sampleRate/channels despite MSDN's documented 8000-48000 Hz contract. The plan's literal acceptance ("fails before stream creation") would diverge from real FNA behavior for no XNA-parity benefit -- re-verified this decision still holds and did not re-litigate it. What genuinely needed fixing instead was `Play()`'s downstream handling when a bad sampleRate (e.g. 0) makes `SDL_CreateAudioStream` fail at stream-creation time -- that is AUD-02-007/AUD-07-007's fix, proven by the new `PlayWithZeroSampleRateDoesNotReportPlayingOnStreamCreationFailure` test.
- [ ] **AUD-07-005 [P0]** Validate S16 submission byte count is frame-aligned. **Acceptance:** Partial samples/frames are rejected.
- [ ] **AUD-07-006 [P0]** Validate float submission element count is channel-frame-aligned. **Acceptance:** Partial multichannel frames are rejected.
- [x] **AUD-07-007 [P0]** Check `SDL_CreateAudioStream` and preserve backend error. **Acceptance:** Null stream cannot proceed to track setup. **Evidence:** see AUD-02-007. New test `PlayWithZeroSampleRateDoesNotReportPlayingOnStreamCreationFailure` (empirically confirmed `SDL_CreateAudioStream` rejects `freq=0` with "Parameter 'src_spec->freq' is invalid"); confirmed to FAIL against the pre-fix code via `git stash`. Full `DynamicSoundEffectInstanceTest` suite 53/53 pass (was 52/52 + 1 new); full whole-repo suite 4654/4656 pass (2 pre-existing hardware skips).
- [ ] **AUD-07-008 [P0]** After `MIX_SetTrackAudioStream`, query both stream specs. **Acceptance:** Source/destination are valid and expected before first put.
- [x] **AUD-07-009 [P0]** Check `SDL_PutAudioStreamData`; retain failed chunks or fail deterministically. **Acceptance:** No data loss and no false submitted count. **Evidence:** see AUD-02-008. No dedicated regression test added (forcing a real `SDL_PutAudioStreamData` failure from the public API without a fault-injection seam was not found to be reliably reproducible in this pass; the fix itself is a straightforward return-value check mirroring the already-tested `SDL_CreateAudioStream`/`MIX_PlayTrack` checks).
- [x] **AUD-07-010 [P0]** Check `MIX_PlayTrack`; set Playing only on success. **Acceptance:** State and framework dispatcher registration remain truthful. **Evidence:** see AUD-02-009. Covered transitively by `PlayWithZeroSampleRateDoesNotReportPlayingOnStreamCreationFailure` for the upstream stream-creation-failure path; a dedicated `MIX_PlayTrack`-only failure fixture was not found (no fault-injection seam from the public API), same limitation as AUD-07-009.
- [ ] **AUD-07-011 [P0]** Correct submitted-buffer completion accounting under resampling. **Acceptance:** PendingBufferCount changes only when complete source chunks are audibly consumed according to defined semantics.
- [ ] **AUD-07-012 [P0]** Test 22.05→44.1, 44.1→48, and 48→44.1 dynamic conversion. **Acceptance:** Frequency and duration stay correct.
- [ ] **AUD-07-013 [P0]** Test immediate queue-before-Play behavior. **Acceptance:** All queued buffers play once, in order, without gap/reordering.
- [ ] **AUD-07-014 [P0]** Test submit-while-playing behavior. **Acceptance:** No race, loss, duplicate submission, or format mismatch.
- [ ] **AUD-07-015 [P0]** Test starvation and BufferNeeded callback cadence. **Acceptance:** Callback counts and pending-buffer semantics match reference.
- [ ] **AUD-07-016 [P0]** Test pause/resume without consuming or replaying data incorrectly. **Acceptance:** Frame position and queue counts remain consistent.
- [ ] **AUD-07-017 [P0]** Test immediate Stop and subsequent replay. **Acceptance:** Old converted data cannot leak into the new playback session.
- [ ] **AUD-07-018 [P0]** Test Dispose during callback/update/submission. **Acceptance:** No deadlock, UAF, callback-after-dispose, or queue leak.
- [ ] **AUD-07-019 [P0]** Test `MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED=false` semantics with pinned mixer. **Acceptance:** Track remains feedable without spinning or false stop.
- [ ] **AUD-07-020 [P1]** Define queue memory limits and backpressure. **Acceptance:** Untrusted/buggy producers cannot grow memory without bound.
- [ ] **AUD-07-021 [P1]** Make buffer-needed dispatch thread explicit. **Acceptance:** Callbacks never unexpectedly execute on real-time audio thread unless documented.
- [ ] **AUD-07-022 [P1]** Avoid holding queue mutex during user callbacks. **Acceptance:** Reentrant submission cannot deadlock.
- [ ] **AUD-07-023 [P1]** Test zero-length submissions. **Acceptance:** Behavior matches reference and does not corrupt completion accounting.
- [ ] **AUD-07-024 [P1]** Test tiny and highly fragmented buffers. **Acceptance:** No accumulated timing gaps or pathological overhead.
- [ ] **AUD-07-025 [P1]** Test multi-second large buffers. **Acceptance:** No integer truncation in SDL length or counters.
- [ ] **AUD-07-026 [P1]** Test repeated Play calls while already Playing. **Acceptance:** Update/callback behavior matches reference without restart.
- [ ] **AUD-07-027 [P1]** Test Stop(false) exception semantics. **Acceptance:** Public behavior matches XNA/FNA exactly.
- [ ] **AUD-07-028 [P1]** Test master volume, volume, pan, pitch, and 3D state set before first Play. **Acceptance:** All effective properties apply on first frame exactly once.
- [ ] **AUD-07-029 [P1]** Test frequency-ratio changes while dynamically streaming. **Acceptance:** Future samples change speed as documented without queue corruption.
- [ ] **AUD-07-030 [P1]** Instrument underrun duration and recovery. **Acceptance:** CI can distinguish expected starvation from backend defects.
- [ ] **AUD-07-031 [P2]** Evaluate callback-driven stream feeding to reduce polling jitter. **Acceptance:** Decision includes real-time safety and parity tradeoffs.

## AUD-08 — SoundEffect and SoundEffectInstance API/audio parity

Prove lifecycle, pitch, volume, pan, loops, and voice limits against the reference behavior.

- [ ] **AUD-08-001 [P0]** Golden-test `SoundEffect::Play()` at neutral volume/pitch/pan. **Acceptance:** Default path produces ratio 1, expected duration, and centered channels.
- [ ] **AUD-08-002 [P0]** Golden-test parameterized fire-and-forget Play. **Acceptance:** Volume, pitch, and pan each affect output exactly once.
- [x] **AUD-08-003 [P0]** Verify `Pitch=-1,0,+1` maps to 0.5,1,2 consumption ratios. **Acceptance:** Frequency and duration both match octave semantics. **Evidence:** `AUD08GoldenPitchMatrix/GoldenPitchRatioTest` params 0/4/8 (pitch -1.0/0.0/1.0) -- real rendered-audio frequency measured within 0.1% of the expected 0.5x/1.0x/2.0x octave ratios.
- [x] **AUD-08-004 [P0]** Verify intermediate pitch values use exponential rather than linear mapping. **Acceptance:** ±0.5 produce expected square-root ratios. **Evidence:** same test, params for pitch=-0.5/+0.5 measure ratios of 0.7071068/1.4142136 (sqrt(0.5)/sqrt(2)) -- the OLD linear-formula bug `P12-PITCH-001` fixed would have produced 0.5/1.5 instead, clearly distinguishable from what's actually measured.
- [ ] **AUD-08-005 [P0]** Verify pitch range validation and exception type. **Acceptance:** Bounds match selected XNA 4.0 platform behavior.
- [ ] **AUD-08-006 [P0]** Verify final frequency ratio is finite and within backend bounds. **Acceptance:** NaN/Inf/invalid composite inputs cannot reach SDL.
- [ ] **AUD-08-007 [P0]** Verify Play failure returns false or throws according to the XNA contract. **Acceptance:** Voice exhaustion differs from content/backend failure.
- [ ] **AUD-08-008 [P0]** Test voice-limit behavior against XNA/FNA. **Acceptance:** Concurrent instance limits and exception/result behavior are documented and enforced.
- [ ] **AUD-08-009 [P0]** Make instance registry thread-safe or explicitly main-thread confined. **Acceptance:** Create/dispose/parent-dispose cannot race the raw pointer collection.
- [ ] **AUD-08-010 [P0]** Test disposing SoundEffect with live instances. **Acceptance:** All instances stop/dispose safely with reference-compatible semantics.
- [ ] **AUD-08-011 [P0]** Test fire-and-forget cleanup on normal end, stop, mixer destroy, and failure. **Acceptance:** No leaked track/audio/instance or stale callback.
- [ ] **AUD-08-012 [P1]** Golden-test pan extremes and center. **Acceptance:** Channel isolation and center law match baseline.
- [ ] **AUD-08-013 [P1]** Golden-test volume 0, fractional, 1, and master-volume combinations. **Acceptance:** Amplitude scales predictably without double application.
- [ ] **AUD-08-014 [P1]** Test property changes before Play, during Play, Paused, and Stopped. **Acceptance:** Timing and applicability match reference.
- [ ] **AUD-08-015 [P1]** Test repeated Play/Pause/Resume/Stop state transitions. **Acceptance:** No restart, stale track, or incorrect callback order.
- [ ] **AUD-08-016 [P1]** Test loop start/length and `IsLooped`. **Acceptance:** Loops use sample frames, not bytes, and have no boundary click.
- [ ] **AUD-08-017 [P1]** Test zero and full-buffer loop regions. **Acceptance:** Edge semantics match reference.
- [ ] **AUD-08-018 [P1]** Test track-ended callback races with explicit Stop/Dispose. **Acceptance:** Cleanup occurs once.
- [ ] **AUD-08-019 [P1]** Verify Duration for all supported codecs. **Acceptance:** Duration is decoded-frame accurate, not byte-size guessed.
- [ ] **AUD-08-020 [P1]** Verify Name, IsDisposed, and exception semantics after disposal. **Acceptance:** API parity tests cover every public member.
- [ ] **AUD-08-021 [P1]** Test copies/moves/shared implementation ownership in C++. **Acceptance:** No accidental duplicated ownership or dangling backend resource.
- [ ] **AUD-08-022 [P2]** Benchmark high-rate one-shot SFX churn. **Acceptance:** Allocation and cleanup meet an explicit frame-time budget.
- [ ] **AUD-08-023..031 [P1]** Golden-test static instance Pitch=-1.0/-0.75/-0.5/-0.25/0/0.25/0.5/0.75/1.0. **Acceptance (each):** Final ratio matches `2^Pitch` before Doppler and measured frequency/duration match. **Strong partial evidence, left unchecked -- literal acceptance not yet met:** `AUD08GoldenPitchMatrix/GoldenPitchRatioTest` (`OfflineAudioRendererTests.cpp`) proves, for all 9 of these exact pitch values, that feeding `2^pitch` into `MIX_SetTrackFrequencyRatio` on a real SDL3_mixer track produces rendered audio whose measured frequency matches the expected ratio to within 0.1% (via `RefineFrequencyEstimateHz`) -- combined with the pre-existing `SoundEffectInstance::INTERNAL_calculatePitchRatio` unit tests (already proving `Pitch` computes exactly `2^Pitch`, `P12-PITCH-001`), this closes the gap between "the math is right" and "the mixer really does what the math says" for every one of these 9 values. What remains unproven: an end-to-end capture through the actual public `SoundEffectInstance`/`SoundEffect` object graph itself (this harness renders via a private `MIX_CreateMixer()`, not the shared device mixer `SoundEffectInstance` actually uses) -- doing that would need either real-device capture or a track cooked-callback hook (same technique `T-4C`'s filter tests already use for sample-level verification). Not force-fit into this pass; a concrete, scoped near-term follow-up.

## AUD-09 — Apply3D, distance, panning, Doppler, and spatial fidelity

Eliminate velocity/unit-induced pitch bugs and document unavoidable spatial differences.

- [ ] **AUD-09-001 [P0]** Create a documented 3D math baseline from XNA/FNA/FAudio behavior. **Acceptance:** Coordinate system, handedness, units, channel mask, and formula ownership are explicit.
- [ ] **AUD-09-002 [P0]** Trace listener/emitter positions, velocities, orientations, and scales. **Acceptance:** Affected high-pitch cases expose every 3D input.
- [ ] **AUD-09-003 [P0]** Test zero velocities always yield Doppler ratio 1. **Acceptance:** No position-only pitch shift.
- [ ] **AUD-09-004 [P0]** Test `DopplerScale=0` always disables Doppler. **Acceptance:** Final ratio is independent of velocities.
- [ ] **AUD-09-005 [P0]** Test equal listener/emitter velocity yields no relative Doppler. **Acceptance:** Parallel motion does not shift pitch.
- [ ] **AUD-09-006 [P0]** Test approaching and receding axial motion. **Acceptance:** Ratios match baseline and are reciprocal within expected physics limits.
- [ ] **AUD-09-007 [P0]** Test tangential motion. **Acceptance:** No radial-velocity pitch shift.
- [ ] **AUD-09-008 [P0]** Test unit conversion from per-frame to per-second velocities in sample code. **Acceptance:** Reference examples cannot accidentally create frame-rate-dependent pitch.
- [ ] **AUD-09-009 [P0]** Verify global and emitter Doppler scales combine exactly once. **Acceptance:** No double multiplication.
- [ ] **AUD-09-010 [P0]** Verify Doppler clamp behavior and backend ratio range. **Acceptance:** Clamping matches selected reference and logs when reached.
- [ ] **AUD-09-011 [P0]** Test large/invalid velocities, coincident positions, and zero distance. **Acceptance:** No NaN, Inf, sign inversion, or unstable ratio.
- [ ] **AUD-09-012 [P0]** Verify `DistanceScale` semantics and validation. **Acceptance:** Attenuation and 3D calculations use consistent world units.
- [ ] **AUD-09-013 [P1]** Golden-test left/right/front/back source positions. **Acceptance:** Pan/spatial matrix is documented and stable.
- [ ] **AUD-09-014 [P1]** Test listener orientation normalization and degenerate vectors. **Acceptance:** Invalid bases fail or normalize deterministically.
- [ ] **AUD-09-015 [P1]** Test emitter orientation and cone behavior against XNA scope. **Acceptance:** Unsupported cone semantics are explicit rather than silently ignored.
- [ ] **AUD-09-016 [P1]** Test multiple listeners according to XNA overload semantics. **Acceptance:** Contribution/selection matches baseline.
- [ ] **AUD-09-017 [P1]** Characterize stereo-source Apply3D behavior. **Acceptance:** Reference restrictions/exceptions and output matrix are matched.
- [ ] **AUD-09-018 [P1]** Compare attenuation curves across distances. **Acceptance:** Curve and clamping match baseline within tolerance.
- [ ] **AUD-09-019 [P1]** Compare panning law with XNA/FAudio captures. **Acceptance:** Known divergence is either fixed or registered.
- [ ] **AUD-09-020 [P1]** Validate custom low-pass/filter response used by 3D/RPC. **Acceptance:** Cutoff/Q are correct at 44.1 and 48 kHz.
- [ ] **AUD-09-021 [P1]** Implement or explicitly scope speaker-channel matrices beyond stereo. **Acceptance:** 5.1/7.1 behavior is not accidental stereo duplication.
- [ ] **AUD-09-022 [P1]** Implement or explicitly scope LFE handling. **Acceptance:** Low-frequency routing matches documented capability.
- [ ] **AUD-09-023 [P1]** Evaluate FAudio/F3DAudio integration as an optional parity backend. **Acceptance:** Decision compares fidelity, licensing, maintenance, and platform support.
- [ ] **AUD-09-024 [P2]** Evaluate optional HRTF as NOXNA functionality. **Acceptance:** Extension cannot alter default XNA-compatible behavior.
- [ ] **AUD-09-025 [P2]** Add moving-source trajectory golden captures. **Acceptance:** Continuous parameter updates have no zipper noise or discontinuity.

## AUD-10 — XACT parser and runtime parity

Make cue selection, pitch, timing, variation, categories, and DSP behavior evidence-based.

- [ ] **AUD-10-001 [P0]** Build a real XACT corpus from legally redistributable projects and generated edge cases. **Acceptance:** Corpus includes XGS/XSB/XWB versions, compact/noncompact banks, variations, RPC, loops, and categories.
- [ ] **AUD-10-002 [P0]** Differential-parse every corpus file against FAudio/FACT tooling. **Acceptance:** All offsets, entries, formats, events, curves, and names agree or divergence is documented.
- [ ] **AUD-10-003 [P0]** Create an XACT cue execution trace format. **Acceptance:** Trace records selected sound/track/wave, event timestamps, pitch, volume, filters, loops, and stop reason.
- [ ] **AUD-10-004 [P0]** Capture corresponding XNA/FAudio cue traces or rendered output. **Acceptance:** Runtime behavior has an oracle beyond parser fields.
- [ ] **AUD-10-005 [P0]** Verify cents-to-ratio conversion is `2^(cents/1200)`. **Acceptance:** Known cents values produce exact ratios.
- [ ] **AUD-10-006 [P0]** Verify sound, track, wave, variation, RPC, and category pitch contributions combine exactly once. **Acceptance:** Trace and golden tone prove composition order.
- [ ] **AUD-10-007 [P0]** Verify random pitch/volume variation bounds and distribution. **Acceptance:** Seeded tests prove endpoints, inclusivity, and no bias from wrong integer math.
- [ ] **AUD-10-008 [P0]** Provide deterministic RNG injection for tests. **Acceptance:** Cue selection is reproducible without changing production randomness.
- [ ] **AUD-10-009 [P0]** Verify RPC curve interpolation and boundary handling. **Acceptance:** Linear/fast/slow/sin/cos or supported curve types match reference samples.
- [ ] **AUD-10-010 [P0]** Verify RPC variable units and update cadence. **Acceptance:** Global/cue variables drive parameters at correct times.
- [ ] **AUD-10-011 [P0]** Verify cue event timestamps, offsets, and relative/absolute timing. **Acceptance:** No frame-rate-dependent event drift.
- [ ] **AUD-10-012 [P0]** Verify wave event loop counts and infinite-loop semantics. **Acceptance:** Counts match XACT and stop cleanly.
- [ ] **AUD-10-013 [P0]** Verify pitch does not get reapplied on repeated Update calls. **Acceptance:** Stable parameter values cannot accumulate ratio exponentially.
- [ ] **AUD-10-014 [P0]** Verify pause/resume freezes event time and playback consistently. **Acceptance:** No skipped/replayed delayed event.
- [ ] **AUD-10-015 [P0]** Verify immediate versus release stop behavior. **Acceptance:** Fade/release semantics match baseline where supported.
- [ ] **AUD-10-016 [P0]** Verify category volume and hierarchy composition. **Acceptance:** Parent/child gain is applied exactly once.
- [ ] **AUD-10-017 [P0]** Verify category pause/resume/stop affects current and future cues correctly. **Acceptance:** State matches XACT reference.
- [ ] **AUD-10-018 [P0]** Verify instance limiting: fail, queue, replace-oldest, replace-quietest. **Acceptance:** Each policy has a real behavioral test, not a shared placeholder.
- [ ] **AUD-10-019 [P0]** Resolve unfinished REPLACE_QUIETEST behavior. **Acceptance:** Quietest selection is measured from effective audible gain and matches baseline.
- [ ] **AUD-10-020 [P0]** Resolve QUEUE versus REPLACE_OLDEST semantics. **Acceptance:** Queued cues start at the correct time/order.
- [ ] **AUD-10-021 [P0]** Verify wave variation selection modes. **Acceptance:** Ordered, ordered-from-random, random, and no-repeat modes match reference.
- [ ] **AUD-10-022 [P0]** Verify variation weights and malformed ranges. **Acceptance:** Selection probabilities and validation are correct.
- [ ] **AUD-10-023 [P0]** Verify track event ordering for equal timestamps. **Acceptance:** Stable order matches file/reference semantics.
- [ ] **AUD-10-024 [P0]** Verify marker/callback events if present in supported scope. **Acceptance:** Callbacks occur once on documented thread.
- [ ] **AUD-10-025 [P1]** Verify XGS category/variable/name table parsing across versions. **Acceptance:** No version-specific offset assumptions.
- [ ] **AUD-10-026 [P1]** Verify XSB simple and complex cues across versions. **Acceptance:** Cue resolution matches reference corpus.
- [ ] **AUD-10-027 [P1]** Verify transition tables if present in XNA 4.0 content. **Acceptance:** Implemented behavior or explicit unsupported error has a fixture.
- [ ] **AUD-10-028 [P1]** Verify filter type, cutoff, and Q parameter mapping. **Acceptance:** Measured frequency response matches intended XACT values.
- [ ] **AUD-10-029 [P1]** Implement or explicitly scope reverb-send events. **Acceptance:** No silent no-op without capability diagnostic.
- [ ] **AUD-10-030 [P1]** Implement or explicitly scope DSP preset events. **Acceptance:** Behavior is compatibility-registered.
- [ ] **AUD-10-031 [P1]** Test cue disposal while waves are active. **Acceptance:** No dangling callbacks or wave-bank references.
- [ ] **AUD-10-032 [P1]** Test wave-bank disposal while cues reference it. **Acceptance:** Reference-compatible stop/error with no UAF.
- [ ] **AUD-10-033 [P1]** Test sound-bank/audio-engine disposal ordering. **Acceptance:** All registrations and callbacks are cleaned once.
- [ ] **AUD-10-034 [P1]** Test hot global-variable updates under active cues. **Acceptance:** No lock inversion or parameter discontinuity.
- [ ] **AUD-10-035 [P1]** Fuzz XGS/XSB parsers with corpus-guided mutation. **Acceptance:** No OOB, leak, hang, or unbounded allocation.
- [ ] **AUD-10-036 [P1]** Add parser size/count/offset limits. **Acceptance:** Adversarial files cannot cause integer overflow or huge allocation.
- [ ] **AUD-10-037 [P1]** Add exact asset/cue/wave context to parser errors. **Acceptance:** Failure identifies file segment and offset.
- [ ] **AUD-10-038 [P1]** Benchmark hundreds of simultaneous cues and frequent Update. **Acceptance:** CPU/allocation budgets are explicit.
- [ ] **AUD-10-039 [P2]** Document unsupported Xbox-specific XACT codecs/features. **Acceptance:** Applications receive actionable compatibility diagnostics.

## AUD-11 — WaveBank/XWB formats, streaming, and extraction

Ensure every parsed wave has correct boundaries, codec metadata, and explicit playback support.

- [ ] **AUD-11-001 [P0]** Confirm compact XWB final-entry length-deviation semantics against FAudio/FACT. **Acceptance:** Authoritative fixture proves whether final deviation must be subtracted.
- [ ] **AUD-11-002 [P0]** Fix compact final-entry length if confirmed. **Acceptance:** Nonzero final deviation test decodes exact payload without padding.
- [ ] **AUD-11-003 [P0]** Validate compact alignment is nonzero and multiplication cannot overflow. **Acceptance:** Corrupt files fail before offset arithmetic.
- [ ] **AUD-11-004 [P0]** Validate every entry range lies fully inside wave-data segment/file. **Acceptance:** No OOB pointer or oversized decoder input.
- [ ] **AUD-11-005 [P0]** Validate compact/noncompact metadata sizes and segment overlap. **Acceptance:** Malformed tables cannot desynchronize parsing.
- [ ] **AUD-11-006 [P0]** Golden-test PCM8 wave-bank entries. **Acceptance:** Unsigned conversion, duration, and loops are correct.
- [ ] **AUD-11-007 [P0]** Golden-test PCM16 wave-bank entries. **Acceptance:** Sample output and metadata are exact.
- [x] **AUD-11-008 [P0]** Golden-test MS ADPCM wave-bank entries. **Acceptance:** Decode, block alignment, tail samples, duration, and loops pass. **Confirmed real P0 defect found and fixed (AUDIO-ADPCM-001):** `WaveBank.cpp`'s `BuildAdpcmWav()` wrapped raw MS-ADPCM bytes in a synthetic WAV with a `cbSize=2` fmt-chunk extension -- just `wSamplesPerBlock`, no coefficient table at all. Empirically confirmed via a standalone probe (`SDL_LoadWAV_IO` against the exact byte layout the old code produced) that SDL3's real MS-ADPCM decoder rejects this outright: "Could not read MS ADPCM format header"/"Missing required coefficients in MS ADPCM format header". This means **every MS-ADPCM-compressed XACT WaveBank entry silently failed to load** (`WaveBank::GetSoundEffect` caught the resulting exception and returned `nullptr`) -- a direct, high-value match for the audit's reported "missing audio" symptom class, since MS-ADPCM is XACT's standard compression codec for size-conscious games. Fixed by adding the standard 7-pair MS-ADPCM coefficient table (`CNA::Internal::Audio::BuildStandardMsAdpcmExtension`, new shared `WavWrapper.hpp`/`.cpp`) -- these are the fixed, industry-standard coefficients every MS-ADPCM encoder (including XACT's) uses and SDL3 validates against exactly, not something derived per-file. Independently verified XactParser.cpp's own `samplesPerBlock`/`blockAlign` derivation formula for compact XACT entries already satisfies SDL's block-size/samples-per-block consistency constraint (traced the exact arithmetic through `MS_ADPCM_CalculateSampleFrames`'s validation in SDL_wave.c) -- the coefficient table was the only missing piece. New test `WaveBankTest.GetSoundEffectForAdpcmEntrySucceeds` asserts `GetSoundEffect()` is non-null directly (not just inferred via `IsInUseProperty` after `Play()`, which conflates "no audio device" with "decode failed"); confirmed to FAIL against the pre-fix code via `git stash` with the exact same SDL error message reproduced. Full `WaveBankTest` suite 24/24 pass (was 23/23 + 1 new).
- [ ] **AUD-11-009 [P0]** Determine IMA ADPCM XWB support requirements. **Acceptance:** Real fixtures establish required decode path.
- [ ] **AUD-11-010 [P0]** Implement or explicitly handle XMA/XMA2 wave-bank entries. **Acceptance:** No parsed entry disappears as unexplained null.
- [ ] **AUD-11-011 [P0]** Implement or explicitly handle WMA entries. **Acceptance:** Behavior is platform/profile documented and diagnostic.
- [ ] **AUD-11-012 [P0]** Validate mini-wave-format bit extraction for every format. **Acceptance:** Channels/rate/block/bits match reference parser.
- [ ] **AUD-11-013 [P0]** Verify samples-per-block formulas for ADPCM compact and normal entries. **Acceptance:** Decoded frame count matches reference.
- [ ] **AUD-11-014 [P0]** Verify loop regions use sample frames and codec-aware mapping. **Acceptance:** Loop playback matches XACT reference.
- [ ] **AUD-11-015 [P0]** Verify streaming WaveBank offset/alignment reads. **Acceptance:** Partial reads and file seeking cannot shift audio data.
- [ ] **AUD-11-016 [P0]** Check all streaming file I/O failures. **Acceptance:** Cue reports the affected bank/wave and remains in truthful state.
- [ ] **AUD-11-017 [P1]** Test wave banks with and without entry names. **Acceptance:** Name lookup and index lookup agree.
- [ ] **AUD-11-018 [P1]** Test duplicate, empty, and unterminated names. **Acceptance:** Parser follows reference and remains safe.
- [ ] **AUD-11-019 [P1]** Test zero-length and tiny entries. **Acceptance:** No decoder crash or accidental remainder consumption.
- [ ] **AUD-11-020 [P1]** Test padding between entries and at segment end. **Acceptance:** Padding never becomes audible payload.
- [ ] **AUD-11-021 [P1]** Test old XWB versions and short metadata entries. **Acceptance:** Zero-init/partial-read semantics match FAudio.
- [ ] **AUD-11-022 [P1]** Test seek tables where applicable. **Acceptance:** Streaming/compressed seek and duration are correct.
- [ ] **AUD-11-023 [P1]** Cache decoded/static waves with bounded memory policy. **Acceptance:** Repeated cues avoid unnecessary decode without unbounded cache.
- [ ] **AUD-11-024 [P1]** Make wave-cache concurrency safe. **Acceptance:** Simultaneous first use decodes once or safely duplicates.
- [ ] **AUD-11-025 [P1]** Test disposal while cache/decode is in progress. **Acceptance:** No race or leaked decoder data.
- [ ] **AUD-11-026 [P1]** Fuzz XWB parser and WAV-wrapper builders. **Acceptance:** No malformed header, overflow, OOB, leak, or hang.
- [ ] **AUD-11-027 [P1]** Validate generated WAV wrapper fields independently. **Acceptance:** RIFF sizes, fmt chunks, fact chunks, block align, and data sizes are correct.
- [ ] **AUD-11-028 [P2]** Add an XWB inspection/extraction tool. **Acceptance:** Tool lists metadata and can export decoded waves for diagnosis.

## AUD-12 — Song, MediaPlayer, codecs, position, and Media API

Make music playback truthful, rate-correct, and compatible while closing explicit Media stubs.

- [ ] **AUD-12-001 [P0]** Make MediaPlayer load failure observable and state-truthful. **Acceptance:** State stays Stopped and error identifies song/path/decoder.
- [ ] **AUD-12-002 [P0]** Check track creation, input assignment, and play results. **Acceptance:** No failed operation starts timer or emits Playing state.
- [ ] **AUD-12-003 [P0]** Drive song completion from actual track/decoder state. **Acceptance:** MediaStateChanged and ActiveSong changes cannot race wall-clock guesses.
- [ ] **AUD-12-004 [P0]** Validate Song duration against decoder metadata. **Acceptance:** Duration is known before/after Play according to reference semantics.
- [ ] **AUD-12-005 [P0]** Test playback rate/pitch remains neutral for 44.1 and 48 kHz music. **Acceptance:** Dominant frequency and duration pass conversion gates.
- [ ] **AUD-12-006 [P0]** Test Play/Pause/Resume/Stop transitions with real decoder output. **Acceptance:** Position and state match audible playback.
- [ ] **AUD-12-007 [P0]** Test repeated Play with same and different Song. **Acceptance:** Old track/timer/callback cannot leak into new song.
- [ ] **AUD-12-008 [P0]** Test `IsRepeating` loop boundary. **Acceptance:** No gap, duplicate callback, or position drift beyond tolerance.
- [ ] **AUD-12-009 [P0]** Test `IsShuffled` and queue semantics. **Acceptance:** Selection and ActiveSong match reference.
- [ ] **AUD-12-010 [P0]** Test `MoveNext`/`MovePrevious` edge cases. **Acceptance:** Queue index and events are correct.
- [ ] **AUD-12-011 [P1]** Replace wall-clock-only position with backend/decoded-frame position where possible. **Acceptance:** Position remains correct under pause, device stall, seek, and resampling.
- [ ] **AUD-12-012 [P1]** Test volume, mute, and game-has-control semantics. **Acceptance:** Gain is applied once and state follows reference platform behavior.
- [ ] **AUD-12-013 [P1]** Test supported media containers/codecs by platform. **Acceptance:** Support matrix has golden decode/duration tests.
- [ ] **AUD-12-014 [P1]** Test Unicode, spaces, long paths, and case sensitivity. **Acceptance:** Song resolution is portable and diagnostic.
- [ ] **AUD-12-015 [P1]** Test malformed/truncated media. **Acceptance:** Failure is bounded and leaves no active track.
- [ ] **AUD-12-016 [P1]** Implement or document `GetVisualizationData`. **Acceptance:** No silent no-op if XNA-compatible data is required.
- [ ] **AUD-12-017 [P1]** Define decoder thread and callback ownership. **Acceptance:** Stop/dispose cannot race decode callbacks.
- [ ] **AUD-12-018 [P1]** Test application suspend/resume and device loss. **Acceptance:** Music state recovers or stops predictably.
- [ ] **AUD-12-019 [P2]** Implement missing MediaLibrary catalog classes where platform-feasible. **Acceptance:** Album/Artist/Genre/Playlist/Picture APIs no longer throw generic not-implemented errors.
- [ ] **AUD-12-020 [P2]** Provide platform-compatible fallback behavior for unavailable media catalogs. **Acceptance:** Exceptions/results match selected XNA platform contract.
- [ ] **AUD-12-021 [P2]** Add gapless-playback capability assessment. **Acceptance:** Supported/unsupported status and measurable gap are documented.

## AUD-13 — Microphone and capture correctness

Make microphone state, format, buffering, hotplug, and permission behavior reliable.

- [ ] **AUD-13-001 [P0]** Set Microphone state to Started only after capture stream opens successfully. **Acceptance:** Backend failure leaves Stopped and raises/returns reference-compatible failure.
- [ ] **AUD-13-002 [P0]** Distinguish no captured data from capture backend error. **Acceptance:** Diagnostics and API behavior are unambiguous.
- [ ] **AUD-13-003 [P0]** Validate capture format, rate, channels, and sample width. **Acceptance:** Returned bytes match documented XNA microphone format.
- [ ] **AUD-13-004 [P0]** Validate `GetData` buffer ranges and frame alignment. **Acceptance:** No partial sample/frame or out-of-bounds write.
- [ ] **AUD-13-005 [P0]** Test Start/Stop/Start cycles. **Acceptance:** No stale data, leaked device, or duplicate callback.
- [ ] **AUD-13-006 [P0]** Test device-open failure, unplug, and permission denial. **Acceptance:** State/events/errors remain truthful.
- [ ] **AUD-13-007 [P1]** Implement device-list refresh/hotplug policy. **Acceptance:** Enumeration cache cannot remain permanently stale.
- [ ] **AUD-13-008 [P1]** Test default microphone selection changes. **Acceptance:** Default property tracks platform behavior safely.
- [ ] **AUD-13-009 [P1]** Verify BufferDuration validation and callback cadence. **Acceptance:** Intervals match reference bounds and measured captured frames.
- [ ] **AUD-13-010 [P1]** Ensure BufferReady is dispatched on the documented thread. **Acceptance:** User callback cannot block real-time capture unexpectedly.
- [ ] **AUD-13-011 [P1]** Test concurrent GetData and Stop/Dispose. **Acceptance:** No deadlock, UAF, or data race.
- [ ] **AUD-13-012 [P1]** Bound capture buffering and define overflow policy. **Acceptance:** Slow consumers do not cause unbounded memory.
- [ ] **AUD-13-013 [P1]** Test mono calibration recording for frequency, level, and duration. **Acceptance:** Capture path does not alter sample rate.
- [ ] **AUD-13-014 [P1]** Test privacy/permission flows on desktop/mobile/web. **Acceptance:** Denied access is explicit and recoverable.
- [ ] **AUD-13-015 [P2]** Add optional NOXNA capture-device diagnostics. **Acceptance:** Device capability details are available without changing XNA API.

## AUD-14 — Content lookup, asset deployment, and build-pipeline integrity

Eliminate “missing audio” caused by path, case, packaging, or unsupported content products.

- [ ] **AUD-14-001 [P0]** Generate an audio asset manifest at build time. **Acceptance:** Manifest lists logical name, deployed path, hash, format, rate, channels, and content kind.
- [ ] **AUD-14-002 [P0]** Fail CI on case-only asset/reference mismatches. **Acceptance:** Linux and Windows resolve the same logical names.
- [ ] **AUD-14-003 [P0]** Detect duplicate logical assets differing only by case. **Acceptance:** Ambiguous deployment cannot pass.
- [ ] **AUD-14-004 [P0]** Verify extension candidate ordering cannot select the wrong file. **Acceptance:** Tests cover `.xnb`, `.cnb`, `.wav`, compressed media, and duplicate stems.
- [ ] **AUD-14-005 [P0]** Verify ContentRoot normalization and traversal protection. **Acceptance:** Paths remain inside allowed roots and normalize portably.
- [ ] **AUD-14-006 [P0]** Verify all game-referenced sounds are packaged. **Acceptance:** Static scan/runtime manifest reports zero missing production assets.
- [ ] **AUD-14-007 [P0]** Verify XACT banks and loose sounds are copied by install/package rules. **Acceptance:** Clean packaged build contains every manifest entry.
- [ ] **AUD-14-008 [P0]** Add a startup or tool-mode audio asset validation pass. **Acceptance:** Missing/unsupported assets are reported before gameplay.
- [ ] **AUD-14-009 [P1]** Add content lookup trace with attempted candidates. **Acceptance:** A missing sound can be diagnosed from one log.
- [ ] **AUD-14-010 [P1]** Test Unicode and non-ASCII asset names. **Acceptance:** Lookup and decoder opening are portable.
- [ ] **AUD-14-011 [P1]** Test paths with spaces and long components. **Acceptance:** No truncation or shell/build-script issue.
- [ ] **AUD-14-012 [P1]** Test read-only packaged assets and virtual filesystems. **Acceptance:** Loaders do not require writable filesystem paths.
- [ ] **AUD-14-013 [P1]** Test archives/bundles on Android/Web where applicable. **Acceptance:** Content access path does not silently bypass audio assets.
- [ ] **AUD-14-014 [P1]** Add format inspection to content build. **Acceptance:** Unsupported codecs are rejected or converted before shipping.
- [ ] **AUD-14-015 [P1]** Record content-pipeline/tool versions in asset metadata. **Acceptance:** Differences between XNA and ported builds are attributable.
- [ ] **AUD-14-016 [P2]** Add optional content conversion recipes for unsupported XMA/WMA. **Acceptance:** Conversion is deterministic, licensed, and preserves loops/duration.

## AUD-15 — Thread safety, lifetime, memory, and performance

Make audio robust under stress and safe for long-running games.

- [ ] **AUD-15-001 [P0]** Run all audio tests under ASan, UBSan, and LSan. **Acceptance:** Zero sanitizer findings in parsers, queues, callbacks, and teardown.
- [ ] **AUD-15-002 [P0]** Run audio concurrency tests under ThreadSanitizer where supported. **Acceptance:** No races in mixer singleton, instance registries, queues, events, or callbacks.
- [ ] **AUD-15-003 [P0]** Define lock ordering for mixer, track, queue, dispatcher, engine, bank, and cue locks. **Acceptance:** Documented order is enforced by review/tests.
- [ ] **AUD-15-004 [P0]** Audit callbacks for use-after-free and reentrancy. **Acceptance:** Every callback owns or validates lifetime and can safely trigger Stop/Dispose where allowed.
- [ ] **AUD-15-005 [P0]** Stress create/play/destroy thousands of short instances. **Acceptance:** No leak, stale callback, or unbounded registry growth.
- [ ] **AUD-15-006 [P0]** Stress dynamic producer/consumer with random pause/stop/dispose. **Acceptance:** No deadlock, lost data outside documented stop, or corrupt counters.
- [ ] **AUD-15-007 [P0]** Stress AudioEngine/SoundBank/WaveBank disposal permutations. **Acceptance:** No UAF, double unregister, or dangling cue.
- [ ] **AUD-15-008 [P1]** Remove avoidable allocations from real-time callbacks and hot mix paths. **Acceptance:** Instrumentation proves zero forbidden allocations per callback.
- [ ] **AUD-15-009 [P1]** Define real-time-safe logging strategy. **Acceptance:** Audio thread never blocks on I/O or allocator-heavy formatting.
- [ ] **AUD-15-010 [P1]** Benchmark simultaneous static voices at 16/32/64/128+. **Acceptance:** CPU, latency, and failure policy are documented.
- [ ] **AUD-15-011 [P1]** Benchmark XACT cue update with large active cue counts. **Acceptance:** Frame-time budget and scaling curve are recorded.
- [ ] **AUD-15-012 [P1]** Benchmark dynamic tiny-buffer workload. **Acceptance:** Minimum practical buffer size and callback overhead are documented.
- [ ] **AUD-15-013 [P1]** Benchmark decoding/loading each supported codec. **Acceptance:** Cold/warm time and allocations guide caching/preload policy.
- [ ] **AUD-15-014 [P1]** Bound decoded-wave and streaming caches. **Acceptance:** Memory budget and eviction are deterministic.
- [ ] **AUD-15-015 [P1]** Test allocation failures at key boundaries. **Acceptance:** Objects remain valid/disposed and errors are contextual.
- [ ] **AUD-15-016 [P1]** Test process shutdown with audio callbacks in flight. **Acceptance:** No late access to static destruction order.
- [ ] **AUD-15-017 [P1]** Test repeated mixer init/destroy cycles. **Acceptance:** No leaked device, thread, handle, or MIX refcount.
- [ ] **AUD-15-018 [P1]** Audit all integer conversions to SDL `int` lengths. **Acceptance:** Buffers larger than INT_MAX are split/rejected safely.
- [ ] **AUD-15-019 [P1]** Audit frame/byte/sample conversions for overflow. **Acceptance:** Checked arithmetic covers duration, loops, offsets, and queue totals.
- [ ] **AUD-15-020 [P2]** Add continuous audio performance regression tracking. **Acceptance:** CI dashboard flags significant CPU/memory/latency changes.

## AUD-16 — Cross-platform and hardware validation

Prove parity and truthful fallback on real devices, not only SDL dummy mode.

- [ ] **AUD-16-001 [P0]** Create a platform/backend audio test matrix with required gates. **Acceptance:** Linux, Windows, macOS, Android, iOS, and Web coverage is explicit.
- [ ] **AUD-16-002 [P0]** Run offline golden tests on every compiler/platform. **Acceptance:** Core decode/mix math remains within tolerance.
- [ ] **AUD-16-003 [P0]** Run physical-device calibration on Linux ALSA/PipeWire/PulseAudio as applicable. **Acceptance:** Rate, duration, channels, and latency are recorded.
- [ ] **AUD-16-004 [P0]** Run physical-device calibration on Windows WASAPI. **Acceptance:** Results compare with original XNA capture where possible.
- [ ] **AUD-16-005 [P1]** Run physical-device calibration on macOS CoreAudio. **Acceptance:** Native 44.1/48 behavior is validated.
- [ ] **AUD-16-006 [P1]** Run Android output/capture calibration across common device rates. **Acceptance:** Mobile resampling and lifecycle pass.
- [ ] **AUD-16-007 [P1]** Run iOS output/capture calibration across route changes. **Acceptance:** Speaker/headphones/Bluetooth transitions are tested.
- [ ] **AUD-16-008 [P1]** Run WebAudio/browser calibration where audio is supported. **Acceptance:** Autoplay, resume, rate, and latency limitations are documented.
- [ ] **AUD-16-009 [P1]** Test headphones/speakers/Bluetooth route changes. **Acceptance:** No persistent wrong rate or lost mixer state.
- [ ] **AUD-16-010 [P1]** Test mono, stereo, 5.1, and 7.1 devices where supported. **Acceptance:** Channel mapping/fallback is explicit.
- [ ] **AUD-16-011 [P1]** Test default 44.1, 48, and 96 kHz devices. **Acceptance:** No pitch shift or duration drift.
- [ ] **AUD-16-012 [P1]** Test suspend/resume, focus loss, and backgrounding. **Acceptance:** Streams and timers recover consistently.
- [ ] **AUD-16-013 [P1]** Test no-audio-device/headless environments. **Acceptance:** Exception/fallback behavior supports tests and servers without false success.
- [ ] **AUD-16-014 [P1]** Test locale-independent numeric parsing/configuration. **Acceptance:** Audio ratios/settings cannot change with locale.
- [ ] **AUD-16-015 [P1]** Record backend capability/version in test artifacts. **Acceptance:** Failures are attributable to an exact environment.
- [ ] **AUD-16-016 [P2]** Create a manual perceptual QA script. **Acceptance:** Human checks complement, but never replace, numerical gates.

## AUD-17 — Malformed content, fuzzing, and security hardening

Treat audio files as untrusted binary input and eliminate parser/decoder denial-of-service risks.

- [ ] **AUD-17-001 [P0]** Fuzz WAV/RIFF loading including chunk order, padding, and sizes. **Acceptance:** No crash/OOB/leak/hang/pathological allocation.
- [ ] **AUD-17-002 [P0]** Fuzz XNB SoundEffect payloads and format extensions. **Acceptance:** Reader remains memory-safe and bounded.
- [ ] **AUD-17-003 [P0]** Fuzz XGS, XSB, and XWB independently and as a linked set. **Acceptance:** Cross-file indices cannot escape bounds.
- [ ] **AUD-17-004 [P0]** Add maximum asset size, channel count, sample rate, and duration limits. **Acceptance:** Limits prevent denial-of-service and are configurable/documented.
- [ ] **AUD-17-005 [P0]** Use checked arithmetic for RIFF and bank size computations. **Acceptance:** No wraparound can produce small accepted ranges or huge allocations.
- [ ] **AUD-17-006 [P1]** Test truncated files at every byte boundary for small fixtures. **Acceptance:** Failure is deterministic and leak-free.
- [ ] **AUD-17-007 [P1]** Test unknown/duplicate RIFF chunks and odd-byte padding. **Acceptance:** Parser remains synchronized.
- [ ] **AUD-17-008 [P1]** Test NaN/Inf float samples and parameter values. **Acceptance:** Mixer output remains finite or input is rejected.
- [ ] **AUD-17-009 [P1]** Test malicious loop points and event counts. **Acceptance:** No infinite CPU loop or OOB region.
- [ ] **AUD-17-010 [P1]** Test decompression bombs and extreme metadata ratios. **Acceptance:** Resource use is bounded before decode.
- [ ] **AUD-17-011 [P1]** Add fuzz corpus minimization and regression promotion. **Acceptance:** Every found crash becomes a small committed test.
- [ ] **AUD-17-012 [P1]** Run fuzzers with ASan/UBSan in scheduled CI. **Acceptance:** Coverage and finding status are tracked.
- [ ] **AUD-17-013 [P2]** Audit external decoder CVE/update policy. **Acceptance:** Pinned versions have an explicit security maintenance process.

## AUD-18 — API parity, documentation, migration, and release gates

Convert the work into a maintainable, auditable definition of “perfect audio.”

- [ ] **AUD-18-001 [P0]** Generate a public API signature diff against XNA 4.0 Audio/Media assemblies. **Acceptance:** Missing/extra/type/default/constness differences are reviewed.
- [ ] **AUD-18-002 [P0]** Generate behavior tests for every public Audio property/method/exception. **Acceptance:** Coverage matrix contains no unreviewed member.
- [ ] **AUD-18-003 [P0]** Define a “CNA Audio Correctness” release gate. **Acceptance:** High-pitch, missing-asset, rendered-golden, sanitizer, and platform gates are mandatory.
- [ ] **AUD-18-004 [P0]** Require zero unresolved silent-failure paths in core playback. **Acceptance:** Static audit and tests prove every backend failure is handled.
- [ ] **AUD-18-005 [P0]** Require zero unexplained unsupported formats in shipped asset manifests. **Acceptance:** Build cannot ship assets that runtime silently cannot play.
- [ ] **AUD-18-006 [P0]** Publish the final XNA/CNA differential report for the reported game. **Acceptance:** Root cause, fix, recordings, and numerical evidence are included.
- [ ] **AUD-18-007 [P1]** Document exact raw PCM constructor requirements with examples. **Acceptance:** C++ porters cannot confuse container bytes and PCM frames.
- [ ] **AUD-18-008 [P1]** Document XNB/XACT supported format matrix and conversion guidance. **Acceptance:** Users know what content pipeline products are valid.
- [ ] **AUD-18-009 [P1]** Document pitch composition and 3D velocity units. **Acceptance:** Examples show neutral pitch and correct per-second velocity calculation.
- [ ] **AUD-18-010 [P1]** Document audio troubleshooting using trace and asset inspector. **Acceptance:** Guide maps common symptoms to concrete measurements.
- [ ] **AUD-18-011 [P1]** Document backend/device limitations and intentional divergences. **Acceptance:** No hidden approximation is marketed as exact parity.
- [ ] **AUD-18-012 [P1]** Add runnable samples for static, dynamic, XACT, media, 3D, and microphone paths. **Acceptance:** Each sample doubles as a manual/CI smoke target.
- [ ] **AUD-18-013 [P1]** Add a calibration sample showing detected rate/frequency. **Acceptance:** Users can validate a machine without the affected game.
- [ ] **AUD-18-014 [P1]** Add migration guidance from XNA C# audio loading to CNA C++. **Acceptance:** Guide highlights metadata, case, lifetime, and content-pipeline traps.
- [ ] **AUD-18-015 [P1]** Create changelog entries for behavior-affecting audio fixes. **Acceptance:** Applications can identify changes in pitch/format/voice semantics.
- [ ] **AUD-18-016 [P1]** Establish semantic versioning policy for audio behavior and extensions. **Acceptance:** Breaking backend/parity changes are not silent.
- [ ] **AUD-18-017 [P1]** Review licenses/patents for optional codecs. **Acceptance:** Codec support decisions are legally and technically documented.
- [ ] **AUD-18-018 [P1]** Package exact dependency notices and source obligations. **Acceptance:** Distributions are reproducible and compliant.
- [ ] **AUD-18-019 [P2]** Complete or explicitly scope every remaining MediaLibrary stub. **Acceptance:** No generic `not implemented` survives without compatibility rationale.
- [ ] **AUD-18-020 [P2]** Perform a final independent audit after all P0/P1 tasks. **Acceptance:** Fresh reviewer reproduces gates without relying on implementation author assumptions.

## Final definition of done

- [ ] The reported C# XNA and C++ CNA game build have matched, archived captures for every formerly affected sound.
- [ ] No affected sound is high-pitched, sped up, distorted, missing, or silently skipped.
- [ ] Static, dynamic, XACT, and media calibration tests pass at 22.05/44.1/48 kHz source rates and 44.1/48 kHz device rates.
- [ ] The supported XNB/XWB/XACT format matrix is implemented and validated by real fixtures.
- [ ] Every core SDL/MIX operation has checked failure handling and truthful public state.
- [ ] Pitch, XACT cents/RPC/random variation, and Doppler composition are traceable and differential-tested.
- [ ] Audio parsers and lifecycle pass sanitizer/fuzz/stress gates.
- [ ] Required physical-device tests pass on supported desktop/mobile/web targets.
- [ ] Remaining divergences from XNA are intentional, documented, tested, and accepted.
- [ ] A fresh independent audit finds no unresolved P0/P1 item or unexplained audible divergence.
