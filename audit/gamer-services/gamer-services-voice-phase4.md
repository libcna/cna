# Voice policy and transport qualification — GS-AUDIT-P4

## Confirmed defects and minimal fixes

**GS-AUDIT-019 FIXED:** OnlineBackend heartbeat formerly ignored its reply. Credential renewal refreshed private slots but did not parse/publish policy to existing SignedInGamer objects. External block/privilege changes could remain stale indefinitely while voice used the published identity. Server `auth.ping` now returns a capability-advertised current own-account privilege/block snapshot. Native backend validates it and emits PolicyChanged; Dispatcher.Update applies it to the same non-guest user/slot, preserving object identity and independent local mute. Renewed credentials parse privileges, refresh blocks and publish the same event. Failed optional block renewal retains the previous list rather than clearing it.

The loopback HTTP controller changes policy after real OnlineBackend sign-in. `HeartbeatRefreshesPublishedPolicyWithoutReplacingTheGamer` failed before the fix, then passes revocation, new blocks, removal/restoration, same gamer pointer and preserved local mute. `CredentialRenewalPublishesCurrentPrivilegesAndBlocks` verifies the renewal branch following a refused heartbeat. Server `service_privacy` now has **73 checks**, including current policy under an existing credential and third-actor block/unblock/operator changes. These are layered software qualifications, not a claim of a physical microphone-to-speaker service run.

**GS-AUDIT-020 FIXED:** server block capacity is 1,024 but the native shared parser's per-array limit was 256; the optional sign-in read swallowed the error and published an empty list. A real HTTP 1,024-tag response reproduced it. The byte-identical canonical/vendored parser now accepts an explicit per-array limit; only native `privacy.list` and `auth.ping` responses select 1,024. Incoming requests/default parsing retain 256. 64 KiB, depth 16, duplicate-key/UTF-8 and 256 object-key guards remain. `AFullServiceBlockListIsNotSilentlyDroppedAtSignIn` failed before the fix and passes afterward. `FullPolicyResponsesRetainRequestSizeDepthAndDuplicateGuards` covers boundaries. No control version/operation/relay framing change.

## Policy evaluation and accepted trust boundary

| Layer/state | When evaluated | What is qualified / residual |
|---|---|---|
| Signed-in privileges/block snapshot | sign-in, credential renewal; healthy heartbeat normally every 30 seconds, then Dispatcher.Update | source + actual HTTP/controller/native publication tests; matched server advertises `policy-refresh`; older servers lack fresh heartbeat policy |
| communication Everyone/Blocked/FriendsOnly | VoiceChat permission refresh every 5 seconds; FriendsOnly calls current `IsFriend`/friends service read | source verified; cached permission can remain up to one interval after publication/relationship change; failed friend read denies |
| local Guide mute/block | local mute synchronous; local block after successful service completion; current map checked in `hears` for each receive and each send target | real ENet + synthetic Opus test proves subsequent submitted frames are suppressed and restoration works; already queued audio is not erased |
| external block change | server stores current list; next healthy policy snapshot replaces local blocked set | bounded polling, not instant remote revocation; 1,024 list boundary tested; block refresh does not reset local mute |
| session voice identity/channel | each decoded voice packet checks sender machine/roster/recipient and unreliable channel | existing and final Net tests, including impersonated sender refusal |
| service relay | redeem ticket/current participant credentials; periodic grant revalidation (5 s), not voice content parsing | fresh membership/token/online permission, session deletion and relay lifetime tests; ENet payload remains opaque |
| recipient/playback | receive checks local current mute/block map and cached communication permission before decoding/submission | tested with continuous synthetic voice; real devices, adapter latency and queued playback behavior require manual qualification |

Local mute is distinct from server authorization and communications privilege. CNA currently also suppresses outgoing speech toward a muted gamer, while presence/HasVoice keepalives remain independent. Everyone/Blocked/accepted-friend policy is enforced by cooperative native endpoints. The relay authorizes transport membership, not every encrypted/opaque voice frame against bilateral privacy policy. No voice-route server anti-cheat or content-inspection redesign was added.

**ACCEPTED DESIGN:** normal remote policy freshness is the next successful heartbeat + Dispatcher.Update + up to the 5-second permission interval, with network/scheduler slack. There is no hard 35-second SLA. During service outages/backoff or a game that stops pumping, last-known policy may survive longer; already delivered/queued audio cannot be revoked. Older servers/clients lack the full new freshness behavior. Deployments requiring immediate centrally enforced privacy against modified clients need a separately selected authority model; the current model must not be advertised as that guarantee. This is a documented MEDIUM architectural limitation, not an unexplained fixed claim.

## Implemented transport path

| Step | Status | Evidence / qualification boundary |
|---|---|---|
| microphone selection/capture | IMPLEMENTED; PLATFORM DEPENDENT; DEVICE QUALIFICATION REQUIRED | MicrophoneCapture recording adapter, one owner (Player One else first local gamer), resampling; synthetic capture passes, physical microphone unrun |
| speech detection/Opus encoding | TESTED / IMPLEMENTED | genuine optional libopus, 20 ms frames, bounded payload; tone→actual Opus tests pass; no broad quality audit |
| packet/authorization/routing | TESTED / IMPLEMENTED | voice codec bounds/flags/sequences, machine ownership, local policy gates, one target per remote machine |
| session transport | TESTED on loopback | ENet channel 1 best-effort voice; online path through session/relay machinery; full TLS/WSS service gates pass where executed; WAN/NAT separate |
| receive/Opus decode/loss concealment | TESTED / IMPLEMENTED | real payload, sequence/drop/conceal tests; new policy test stops subsequent Bob frames while blocked/muted |
| speaker submission | TESTED synthetic; IMPLEMENTED actual adapter; DEVICE QUALIFICATION REQUIRED | DynamicSoundEffectInstance per talker; no physical sound-output qualification |

`OnlineNetworkSessionTest.LiveBlockAndMuteSnapshotsSuppressSubsequentVoiceFrames` uses real loopback ENet, actual Opus and injected capture/playback counters. It applies a published-policy-style block snapshot, then local mute, verifies suppression, then restores playback. Initial test setup used a hardcoded ID from a different roster (Dana while blocking Bob); corrected to actual Bob/local wire IDs. The corrected test and five neighboring voice cases pass. This was an oracle error, not a new production voice failure.

Friends-only relationship refresh is source verified through uncached backend friends reads at the permission interval and server policy tests; no physical two-device friend/unfriend voice run is claimed. The manual plan specifies that run, live operator revocation and third-device block changes exactly. No audio-quality/device qualification was faked.
