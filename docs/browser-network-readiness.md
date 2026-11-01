# Browser networking boundary

Browser multiplayer and remote GamerServices remain outside the approved scope by the owner's
2026-09-28 decision, confirmed in the Xbox-fidelity pass (GSX-E7). A browser build can run local
profiles and games; native SystemLink and online-service implementation does not make those
transports usable from the browser. The current limitation is
[gamer-services-known-limitations.md §26](gamer-services-known-limitations.md#26-browser-webassembly-multiplayer-and-services).

## Current implementation

| Boundary | Emscripten behavior |
|---|---|
| Service configuration | An empty endpoint allows local use; a nonempty deployment is refused with `BROWSER_SERVICE_TRANSPORT_UNAVAILABLE`. |
| Service work/identity | Queued work is pumped by the dispatcher without a native background worker. Unavailable authentication reports failure and produces no service identity. |
| Realtime relay | Endpoint lookup and relay construction refuse with `BROWSER_RELAY_TRANSPORT_UNAVAILABLE`; removing one check would not implement the native relay worker/datagram route in a browser. |
| SystemLink discovery | The browser path produces no discovered peers. An empty result is not successful discovery or a playable session. |
| Voice and service event channel | Not compiled as browser transports. |
| Guide pending operations | EndShowMessageBox/EndShowKeyboardInput called before user completion refuses instead of blocking the browser frame. |

The implementation points are `ServiceConfiguration.cpp` / `GamerServicesBackend.cpp` under
`modules/gamer-services/src/Internal/`, and `RelayWebSocket.cpp`, `RelayTransport.cpp`,
`ENetDiscoveryService.cpp` and `ENetBackend.cpp` under `modules/net/src/Internal/`.

The native modules remain in browser builds when selected; they do not import the host libcurl.
On non-Emscripten targets libcurl is required except Android, where an unavailable target libcurl
leaves the service/relay transport explicitly unavailable. Host package discovery must not inject
native headers/libraries into a cross build. `cmake/HeaderOnlyJson.cmake` exposes the nlohmann
header directory rather than the host `/usr/include` root.

The configured native CNA service already exists; see
[gamer-services-server.md](gamer-services-server.md). Its existence is distinct from a browser
control/realtime client and does not close this platform limitation.

## Historical SAMPLE-104 disposition

The samples owner accepted a playable WEBGL2 Performance Utility release marked partially
implemented, with networking explicitly unavailable. This replaced deferral of the whole browser
sample release; it did not approve browser multiplayer implementation.

The original Windows-client source/NET dependency was retained. An empty deployment allowed the
local utility to run, while its remote command still required genuine sign-in. These are
historical sample observations, not current authentication or peer-play evidence. Original
commands/results belong to that consumer's SAMPLE-104 evidence, rather than a nonexistent CNA-local
`evidence/web-partial-20260928/` directory.

If browser networking is separately authorized in future, acceptance must cover real account/
authority handling, asynchronous control and realtime lifecycle, public discovery/join semantics
and native regressions. SAMPLE-104 additionally requires the original Xbox-host role against a real
browser client, all six packet headers and ReliableInOrder replies, disconnect/session cleanup and
reconnection. Two native clients or a linked browser binary cannot satisfy that evidence.
Consumer gallery/plan changes belong to the consumer owner after those results exist.

## Regression evidence and its limits

`modules/net/tests/Microsoft/Xna/Framework/Net/BrowserServiceTransportTests.cpp` contains five
Emscripten-only tests for empty configuration without workers, configured-authority refusal,
failed authentication without an identity, relay-endpoint refusal and relay-construction refusal.

The original campaign ran these as a diagnostic WEBGL2 artifact in Chrome. They assert the current
unavailable-transport contract; they are not positive network tests, a server deployment check or
proof of a maintained standalone browser-network CTest gate. The separate
[Wasm C API browser probe](c-api/WASM_ARTIFACT.md) verifies artifact/lifecycle/rendering mechanics,
not multiplayer.
