# Browser network readiness — SAMPLE-104 partial release

## Accepted scope — 2026-09-28

The samples owner requested a playable WEBGL2 Performance Utility release, explicitly marked
**🟡 partially implemented**, while documenting that its network portion is not functional.
This supersedes the earlier deferral of the whole browser release. It does not accept full network
support or authorize fake identities, sessions, packets or a sample-specific transport.

The entire original Windows-client source and NET dependency remain. Empty service deployment
allows the local utility to run; the original `remote` command says `Please signed in.` without
a genuine signed-in gamer. That is not successful authentication or peer discovery.

## Current framework platform boundary

| Location | Current browser behavior / next owner |
|---|---|
| `modules/gamer-services/CMakeLists.txt`, `modules/net/CMakeLists.txt` | libcurl remains mandatory on native targets. Browser builds compile the genuine modules with explicit unavailable-transport boundaries; they do not link host libcurl or turn NET off. |
| `cmake/HeaderOnlyJson.cmake` | Resolve the installed header-only nlohmann package and expose only its `nlohmann/` directory to Emscripten. Never add the host `/usr/include` root to the cross compiler. Native imported-package behavior remains unchanged. |
| `modules/gamer-services/src/Internal/ServiceConfiguration.cpp` | Empty endpoint is usable locally. A configured browser deployment throws `BROWSER_SERVICE_TRANSPORT_UNAVAILABLE`; native secure URL validation remains unchanged. Replace this refusal with the eventual browser deployment/authority adapter and its validation tests. |
| `modules/gamer-services/src/Internal/GamerServicesBackend.cpp` | Browser queued work is pumped at the ordinary dispatcher boundary without native worker creation. Account exchange throws an explicit unavailable error. Implement secure asynchronous browser control traffic, negotiation, login/refresh/logout, cancellation and genuine identities here or in the shared transport adapter. Native verified HTTPS remains intact. |
| `modules/net/src/Internal/RelayWebSocket.cpp` | Browser endpoint/connection/send/receive explicitly reject with `BROWSER_RELAY_TRANSPORT_UNAVAILABLE`. Implement the shared browser realtime adapter with validated authority/tickets, bounded frames/queues and normal lifecycle. Native verified WSS remains intact. |
| `modules/net/src/Internal/RelayTransport.cpp` | The current private native worker/loopback-datagram routing must not be treated as a browser implementation. Browser integration needs a suitable event-driven route, not merely removal of the WebSocket refusal. |
| `modules/net/src/Internal/ENetDiscoveryService.cpp`, `ENetBackend.cpp`, `modules/net/src/Xna/NetworkSession.cpp` | Connect the common session directory and browser-capable transport to public SystemLink find/join/host/address-handoff semantics. The existing empty browser discovery and private native progress do not qualify these routes. |

This is deliberately incomplete platform support, not a completed network feature. No public XNA
signature, C ABI or framework class layout changes are required by this partial release. No sample
name/command checks occur in these shared boundaries. Browser-specific refusals are temporary
readiness markers; native behavior must not be weakened when replacing them.

The larger service work remains owned by the living
[Gamer Services server plan](../plans/plan_gamer_services_server.md) and its dedicated worktree.
This samples task does not implement or change that server, its protocol or its task statuses.

## Acceptance after shared services are ready

1. Supply the title's deployment configuration and genuine accounts through the common host/Guide
   integration. The original sample expects an already signed-in gamer; do not add a new sample
   sign-in screen, invented gamer or manual-IP command.
2. Replace explicit browser refusals with the implemented platform adapters; qualify HTTPS origin,
   CORS/allowed origin, authentication/refresh, asynchronous pump, cancellation and error behavior.
   Keep native regressions and adapt the current browser refusal tests to the new supported contract.
3. Qualify public SystemLink discovery/find/join and a reachable host with the original Xbox-host
   branch, one local gamer and two total gamers. Exercise a real browser client plus a genuine
   counterpart; two Windows-client builds do not test the original host role.
4. Qualify all six sample packet headers, ReliableInOrder commands and replies, errors/warnings,
   start/quit, peer loss, session-ended cleanup and reconnection. Preserve the existing C++ session
   lifetime protection while callbacks unwind. An empty session list is not a positive test.
5. Rebuild the unchanged sample in its stable native/web roots. Re-run local original/native/web
   visual and interaction gates as well as real-peer acceptance. Replace the gallery bundle and
   remove its network limitation only once verified. Update sample `diff.md`, `missing.md`, the
   samples master plan/handoff and this readiness record; 🟡 must not become ✅ from a link result.

## Regression evidence

Five browser-only Google Tests in
`modules/net/tests/Microsoft/Xna/Framework/Net/BrowserServiceTransportTests.cpp` exercise empty
deployment without workers, explicit configured-deployment rejection, failed authentication with
no identity, and relay endpoint/constructor refusal before network work. They run as a diagnostic
WEBGL2-linked artifact in real system Chrome, not as an altered sample or a production gallery file.
Native configuration/authority and relay regressions use the existing private GPU runner.
Exact results and commands live with SAMPLE-104 under `evidence/web-partial-20260928/`.
