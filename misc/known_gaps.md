# Known gaps

Capabilities XNA has and CNA does not, **measured** against a specific XNA behaviour and recorded
with the case that found them. Distinct from [`known_bugs.md`](known_bugs.md): nothing here is
wrong, it is absent — or present as a deliberate divergence — and closing it is a decision rather
than a repair.

A row leaves this file when the capability lands, or when the owner records that CNA will not have
it.

Most of these surface through the `cna-samples` campaign, because porting a sample is what puts
weight on a corner of the API nothing else reaches. The sample that found a gap is named, but a gap
outlives that sample's own fate: several were found through rows that were then cancelled for
unrelated reasons, and the gap is not cancelled with them.

---

## 1. `SurfaceFormat::Alpha8` render targets are refused

**Found:** 2026-09-09, through SAMPLE-087 (AvatarShadows), whose row was cancelled for an unrelated
reason — the sample is Xbox-360-only. The gap is not.

`GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)` returns **false**,
asserted by `GraphicsCapabilityFloatRenderTargetTest.NonColourNonFloatFormatsAreNotRenderTargets`.
The test's own comment gives the reasoning: "claiming otherwise would put the caller back in the
position MOD-100 exists to end — asking for one format and silently receiving another."

**XNA does the opposite.** Its `RenderTarget2D` silently substitutes when it cannot honour a
requested format — the behaviour this project records as a defect in `MOD-107`, where a WebGPU
`RenderTargetCube(HdrBlendable)` reported the float format it was asked for while holding 8-bit
texels.

So CNA's refusal is principled and knowing, and it is still a divergence. It costs any technique
that renders into a single-channel target: SAMPLE-087's planar avatar shadows drew into a
full-screen `Alpha8` target and sampled it to darken the ground by 50 %, which is an ordinary
shadow-mask idiom rather than anything exotic.

**Closing it** means either supporting `Alpha8` as a render-target format on the renderers whose
API can hold it — GL has `GL_R8`, and a swizzle or a shader read gives the alpha semantics — or an
owner decision to substitute as XNA does and give up the MOD-100 guarantee for this format. The
first keeps both promises; the second is smaller.

---

## 2. No `FontTextureProcessor`: a marker bitmap cannot become a `SpriteFont`

**Found:** 2026-09-09, through SAMPLE-090 (BitmapFontMaker), whose row was cancelled the same day as a
design-time WinForms tool. The gap is not cancelled with it: it is about the format, not the tool.

XNA has two routes from authored content into a `SpriteFont`:

| route | XNA processor | CNA |
|---|---|---|
| `.spritefont` XML → rasterise an installed TrueType face | `FontDescriptionProcessor` | **present** — `CNA::Content::Pipeline::FontDescription`, `modules/content-pipeline` |
| a pre-rendered bitmap whose glyphs are separated by a marker colour | `FontTextureProcessor` | **absent** |

The second route is how a project ships a hand-drawn or pixel-art font, or one rendered by a tool
rather than by the pipeline. SAMPLE-090 is such a tool: it clears an atlas to `Color.Magenta` and
blits each glyph over it with `CompositingMode.SourceCopy`, saving 32-bit ARGB BMP
(`MainForm.cs:219`). But the gap is not about that tool — **any** bitmap produced by any means is
equally unusable, because CNA has no importer for the format.

**Closing it** means a processor that takes an image, splits glyphs on the marker colour, and emits
the same `SpriteFont` the description route already emits. The runtime side needs nothing: the
`SpriteFont` it would produce is the one CNA already loads.

---

## 3. `IntermediateSerializer` XML is read for exactly one schema

**Found:** 2026-09-09, through SAMPLE-093 (CurveEditor), whose row was cancelled the same day as a
design-time WinForms tool. The gap is not cancelled with it: it is about the envelope, not the editor.

XNA's content pipeline can take a `.xml` source asset in `IntermediateSerializer` form — an
`<XnaContent><Asset Type="...">` document — and import **any** type it names. That is how a project
ships hand-authored or tool-authored content: a `Curve`, a level description, a custom data class.

CNA reads that envelope for **one** asset type. `ParseFontDescription`
(`modules/content-pipeline/src/SpriteFontContentPipeline.cpp:115`) checks the root really is
`XnaContent`, finds `<Asset>`, and then requires its `Type` attribute to be a font description,
reading `<Size>`, `<Style>` and the character regions by hand. It is a bespoke parser for the
`.spritefont` schema, not a reader of the format.

There is no `IntermediateSerializer` anywhere else in the tree: the name appears once, in a comment
in `ReflectiveTypeReader.hpp` explaining why XNB field order is what it is.

**What that costs.** Every `.xml` content asset that is not a `.spritefont` is unimportable, whatever
produced it. SAMPLE-093's editor round-trips `IntermediateSerializer<Curve>` XML with keys,
tangents, continuity and loop types — CNA has the whole `Curve` type family and loads curves happily
from **XNB**, so only the authoring format is missing, not the runtime.

**Closing it** means a general reader over the envelope CNA already parses, dispatching on the
`Type` attribute the way `ReflectiveTypeReader` already dispatches for XNB. The two would then agree
on one type table.


---

## 4. There is no online matchmaking or invitation service

**Found:** 2026-09-09, through SAMPLE-096 (Invites).
**Partly closed the same day:** the divergence half — see below.

CNA implements real transport for exactly one `NetworkSessionType`. `ENetBackend::
RealNetworkingEnabled` (`modules/net/src/Internal/ENetBackend.cpp`) is

```cpp
return sessionType == NetworkSessionType::SystemLink;
```

`Local` and `LocalWithLeaderboards` are offline session types in XNA too, so a single machine
genuinely is the whole session and nothing is missing about them. `PlayerMatch` and `Ranked` are
different: they are Xbox LIVE matchmaking types, where the service finds the peers. There is no such
service here, and none is planned. The same absence covers invitations — nothing can raise
`NetworkSession::InviteAccepted`, because nothing delivers an invitation.

### What was closed, and what remains

The gap first recorded here was not the absence but the **silence about it**:

| | `NetworkSession.Create(PlayerMatch, 4, 16)`, one local profile signed in, offline |
|---|---|
| Real XNA 4.0 | throws; the message names the signed-in-gamer and LIVE-profile requirement |
| CNA before 2026-09-09 | succeeded, returning a session of type `PlayerMatch` with no port, no discovery and no peer that could ever arrive |
| CNA now | throws `GamerServicesNotAvailableException`, naming the missing service |

The XNA half is a capture, not a reading: SAMPLE-096's unchanged `Invites.exe` under the local
XNA 4.0 install on an isolated offline Xvfb signs in `Player1`, reaches the A/B menu, and answers A
with that error —
`/rv/tmp/samples/SAMPLE-096-InvitesSample_4_0/evidence/original-windows-reach/02-after-local-sign-in.png`
and `03-offline-player-match-create.png`.

`Create`, `Find` and `JoinInvited` (with their `Begin*` forms) now refuse; `EndJoinInvited` refuses
every result, because no `Begin` can produce one and completing a foreign result as an invited join
was a way around the refusal. Through the C ABI all of these answer `CNA_RESULT_NOT_SUPPORTED`,
whose own documented meaning is already "the platform has no gamer services at all".

**`Join(AvailableNetworkSession*)` deliberately still accepts a `PlayerMatch` entry.** Its input can
no longer come from `Find`, so such an entry can only be constructed directly by a binding, and
`NetworkSessionTest.JoinDoesNotActivateTransportForSyntheticPlayerMatchSession` exists to keep that
path from waiting on a handshake that will never arrive (a real SAMPLE-091 bug). Refusing there
would delete that regression guard without closing anything a game can reach.

**Closing the rest** means the service itself: identity, friends/presence, matchmaking, invitation
delivery and address handoff, for native and browser. That is the scope `SAMPLES-DEC-004` and
`SAMPLES-DEC-006` put to the owner, and it is why SAMPLE-096 is a non-port rather than a blocked
port.

---

## 5. `Microphone.All` carries a synthetic "Default Device" that XNA does not have

**Found:** 2026-09-09, auditing SAMPLE-098 (MicrophoneEcho), a row that is already `✅`.

CNA's SDL3 recording-device provider prepends one entry to the device list before the real
devices, named `"Default Device"` and bound to `SDL_AUDIO_DEVICE_DEFAULT_RECORDING`
(`modules/audio/src/Platform/Sdl3/Sdl3AudioRecordingDevice.cpp:256`). `Microphone::Default` is that
entry, so a game that takes the default microphone gets a device whose `Name` is the literal string
`"Default Device"`, and `Microphone::All` holds one more entry than the machine has devices.

**This is FNA, faithfully.** `FNA/src/FNAPlatform/SDL3_FNAPlatform.cs:1699,1707` allocates
`numDev + 1` microphones and fills the first with exactly that string, under the comment *"First mic
is always OS default"*. CNA reproduces it down to the literal.

**XNA does not do this**, and for once that is a capture rather than a reading of IL. SAMPLE-098
draws `Microphone.Name + " is " + state` as its entire HUD, and its artifact root holds the same
frame from both runtimes on this machine:

| | first line of the HUD |
|---|---|
| the unchanged XNA executable (`evidence/original-stopped.png`) | `PulseAudio Input is Stopped` |
| the CNA port (`evidence/cna-debug-stopped.png`) | `Default Device is Stopped` |

Everything else in the two frames matches. XNA's `Microphone.Default` is a real enumerated device
and reports the driver's own name for it; FNA's — and therefore CNA's — is a synthetic entry that
names itself. The count difference follows from the same source line and is read from FNA rather
than measured against XNA, because a screenshot of one name cannot show a list length.

**Why this is a gap and not a bug.** CNA matches its stated reference exactly. But `CLAUDE.md` has
made XNA the tie-break since 2026-09-04 — *"matching FNA is no longer a defence for a behaviour XNA
does not have"* — so this is a divergence to decide on, and it is the fourth of its kind after the
three `plans/plan_bindings_upstream.md` already lists (`GraphicsAdapter::getIsWideScreenProperty`,
`Microphone::setBufferDurationProperty`, `SoundEffectInstance::Apply3D`). It is the first found by a
sample rather than by a language binding, and the first with a side-by-side capture behind it.

**Closing it** means dropping the synthetic entry and making `Microphone::Default` the first real
device, as `FNA/src/Audio/Microphone.cs:35-45` already does for `Default` itself (`All[0]`). The
cost is that the OS's notion of "default recording device" stops being addressable through the XNA
API at all, which is presumably why FNA added it; a CNAEXT accessor could keep it reachable without
putting it in `All`. Two tests assert the current shape
(`Sdl3AudioRecordingDeviceTests.cpp:41`, `MicrophoneTests.cpp:89`) and would move with the decision.
