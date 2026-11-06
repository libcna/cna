# XACT reverb sends and DSP presets

XGS preset headers/parameters and XSB absolute DSP references are retained. Each engine owns
its preset state, and cues referencing the same preset in that engine share a wet bus. A different
engine cannot change or clear that history. Scheduled and loop-retriggered voices receive the route
before playback, alongside their filter/pan state. The output remains stereo; this is not multichannel
X3DAudio support.

The mixer generation has 32 bus slots. Presets are reserved lazily on first DSP use/engine Update;
reservation failure rolls back that engine's partial reservation and reports a resource failure.
Unknown preset codes, effect chains with more than one reference, non-22-parameter presets,
invalid/nonfinite effect parameters and unsupported effect sample rates are refused explicitly.
The current processor accepts 20–192 kHz. Empty DSP tables require no audio hardware. Ordinary
SoundEffects with no owned route remain dry.

Sound and track reverb RPC contributions sum in centibels; linear send is `10^(sum/2000)`.
Engine-global RPCs with targets 5–26 update the corresponding preset parameter (target minus5),
clamped to each preset's authored bounds, on every AudioEngine.Update, including when no cue is active.
Cue-local variables cannot modify shared engine presets. Global preset curves apply to each compatible
preset in that engine, following the owner-approved FAudio-derived policy. This targeting policy and
the malformed-resource refusal timing have not been established with native XACT captures.

The send taps stereo PCM after track gain/filter/pan and before master gain. It folds `(L+R)/2`
into the mono omnidirectional network and duplicates the result to left/right; master gain applies
exactly once to both branches. The original dry path remains present. Thus an effect WetDryMix of0
passes the send unchanged and adds it to dry output; it is not equivalent to disabling the send.
Pause/Stop/cue disposal stop feeding the bus while existing wet history drains. Authored source fades
attenuate both branches. Engine disposal synchronizes with rendering, removes only its buses, and
reclaims their delay storage on the control thread. Handle identities are not reused across slots or
mixer generations. Mixer shutdown clears remaining buses after its device barrier.

Both SDL3_mixer and CnaMixer render in slices of at most1024 frames. The SDL memory mixer keeps
identical source/destination sample rates and a unity master frequency ratio, so SDL's output stream
bypasses resampling and requests no padding. `AudioMixer.cpp` cites the bundled SDL/SDL_mixer lines
that establish the request bound. Its internal postmix dispatcher adds wet returns before forwarding
the existing user observer. CnaMixer accumulates/returns at each of its internal1024-frame blocks,
before its existing final observer. CNA wet processing allocates only at reservation, updates coefficients
under the existing mixer barrier, and performs no allocation/free in its render callbacks. The upstream
SDL mixer/decoder may still perform its own allocations; this is not a blanket realtime claim about SDL.
Changing the private native mixer's master frequency ratio outside CNA's facade violates this bounded
routing contract; framework pitch changes use the per-track ratio instead.

The DSP is an altered zlib-licensed extraction of FAudio FXReverb at commit
`6839b88e304a046ae1a609ff14a085371e02a4e0`, with its original notice and provenance under
`third_party/faudio-reverb/`. It retains the applicable mono network: delay/all-pass stages,
eight damped feedback combs, shelf/room filters, RT60 coefficients and reset. Parameter mapping
and CNA routing are separate MS-PL implementation files. Density and RoomSize are retained but
unused by the upstream network; their audible behavior remains unproven. Surround/rear controls
do not establish multichannel output. Native XACT timbre, all-parameter numerical parity and zero-delay
edge behavior remain unqualified; the port retains the upstream ring-delay behavior.

Evidence: literal-bank parser tests, offline impulses/DC and lifecycle tests run through both real
mixer adapters with a synchronous selected device. Early all-pass taps, the first late comb tap,
master gain and send conversions use hand-derived expectations; allocation-slot exhaustion and
an entered-callback removal latch exercise lifetime boundaries. No expected PCM is generated from
CNA output. Managed Refresh Xact.dll delegates processing to native XACT and is not a reverb-math
oracle. FAudio-derived output proof is not a claim of native Windows XACT parity.
