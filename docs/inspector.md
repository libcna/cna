# CNA Inspector

Status: INSP-0001 complete (2026-09-17).

## Purpose and boundary

CNA Inspector is an optional development tool that observes a running CNA application through the
version-1 diagnostics provider. It does not replace a debugger or a general-purpose trace viewer,
and it is not an editor, scene browser, ECS browser, scripting console, input injector, or remote
administration interface.

The profiler foundation at `b7e856221b61c273e0ac085ec67d103e0ac7184a` remains unchanged.
Inspector consumes `IDiagnosticsProvider::CaptureSnapshot()`, cursor-based `ReadEvents()`, and
`ResolveName()`. It preserves every `Accuracy` value and exposes all producer-drop, history-gap,
history-overwrite, malformed-use, and source-failure counters. Transport, authentication, binary
serialization, preview authorization, HTTP, JSON, and browser assets all live in the optional
`modules/inspector` module, not `modules/diagnostics`.

```text
instrumented CNA application                 separate cna-inspector process

IDiagnosticsProvider v1                      compact binary client
        ^                                            |
        | pull, on demand                            v
CNA::Inspector::Agent  <--- authenticated TCP --->  localhost HTTP bridge
(one background thread)                              |
                                                     v
                                             offline browser UI
```

The split is deliberate. A browser refresh does not restart the game. A bridge or browser crash
does not affect the game. The application-side agent neither embeds a web server nor constructs
JSON. It services one bounded request at a time on its own thread and never runs a consumer
callback on an instrumentation or render path.

## Build and activation

Inspector is compiled out by default. Configure diagnostics and the optional module explicitly:

```sh
cmake -S . -B build-inspector \
  -DCNA_DIAGNOSTICS=FULL \
  -DCNA_BUILD_INSPECTOR=ON
cmake --build build-inspector --target cna-inspector
```

`STATS` is sufficient for frames, metrics, memory estimates, and resource metadata. `FULL` also
enables CPU zones, markers, and the event timeline. `OFF` can still negotiate a session but has no
useful live diagnostics.

The module is intentionally absent from the historical `CNA` umbrella. An application opts in on
its own link line:

```cmake
target_link_libraries(my_game PRIVATE CNA CNA::Inspector)
```

It must also start the endpoint explicitly. A typical game maps its own `--cna-inspector` argument
to code like this:

```cpp
#include "CNA/Inspector/Agent.hpp"

CNA::Inspector::AgentConfiguration configuration;
configuration.applicationName = "My game";
configuration.metadata = {
    {"Resolution", "1920x1080"},
    {"Window mode", "windowed"},
    {"Runtime flags", "development"}
};

std::string error;
auto inspector = CNA::Inspector::Agent::Start(configuration, error);
if (!inspector)
{
    // Log error and continue running without Inspector.
}
else
{
    // Transfer these through a local developer-only channel.
    std::cout << "Inspector port: " << inspector->GetPort() << '\n';
    std::cout << "Inspector token: " << inspector->GetAuthenticationToken() << '\n';
}
```

There is no static initializer, environment-variable hook, or automatic `Game` modification. If
the call is absent, no listener or Inspector thread exists. If the option is off, no Inspector
target is linked. Applications decide how their development-only command-line parsing and token
delivery work.

Start the separate bridge with the values from the application:

```sh
# Prefer a mode that does not put the token in shell history or a process listing.
export CNA_INSPECTOR_TOKEN='<token>'
build-inspector/modules/inspector/cna-inspector --agent-port <port>

# Or use a local owner-readable file containing only the token.
build-inspector/modules/inspector/cna-inspector \
  --agent-port <port> --token-file /path/to/token
```

The tool prints an ephemeral `http://127.0.0.1:<port>/` URL. `--http-port` selects a fixed browser
port. `--help` lists the complete command line.

### Trying it without a game of your own

`cna_inspector_demo` is a small game that opts in exactly as above. It draws sprites into a render
target, owns textures and buffers, publishes CPU zones and markers, and creates and destroys a
texture every two seconds, so every view has live data. It runs under any platform, including
`HEADLESS`, and is built with `CNA_BUILD_EXAMPLES` (on by default):

```sh
cmake --build <build-dir> --target cna_inspector_demo cna-inspector
<build-dir>/cna_inspector_demo --port 47001          # prints the port and token; --seconds N exits
CNA_INSPECTOR_TOKEN='<token>' <build-dir>/modules/inspector/cna-inspector --agent-port 47001
```

## Security model

Inspector exposes application names, build facts, timing data, resource labels, resource sizes,
and optionally manually requested images. Treat that information as sensitive.

- Both build support and application activation are off by default.
- The agent binds `127.0.0.1` by default. A non-loopback bind is rejected unless the application
  explicitly sets `allowRemote=true`.
- A 256-bit token is generated from the operating system's secure random source when the
  application does not provide one. Authentication happens
  before the provider is queried or session data is returned. Comparison does not short-circuit
  on the first differing byte.
- The browser bridge always binds `127.0.0.1`. It validates the HTTP `Host`, sends no agent token
  to the browser, sets a restrictive Content Security Policy, exposes no CORS policy, and requires
  an unguessable per-process header on every `/api/` request to prevent cross-origin preview
  requests.
- The binary protocol is plaintext. Remote mode is an explicit advanced setting, not an Internet
  service. Use an authenticated local tunnel or VPN; never expose the port directly to an
  untrusted network.
- There are no arbitrary memory reads, arbitrary buffer-content reads, code-execution messages,
  scripting commands, input injection, or generic method invocation.
- The parser validates magic, versions, message types, flags, lengths, counts, enums, UTF-8, and
  trailing bytes. Invalid clients are disconnected or receive a bounded structured error.
- The agent handles one authenticated client and one request at a time, caps requests at 64 per
  second by default, caps the listen backlog at four, and has no unbounded application queue.
  Packet payloads are limited to 8 MiB and socket operations time out.
- The browser bridge serves connections concurrently, up to 32 at once; further connections are
  refused rather than queued. A connection that sends no request header is dropped after one
  second. Requests that reach the agent still serialize on the single agent link, because the
  agent itself serves one request at a time. Static assets need no agent and stay concurrent.
- Agent and bridge sockets are waited on with `poll()` on POSIX rather than a `select()` descriptor
  bitmap, so a host process holding more than `FD_SETSIZE` descriptors is served normally.

Authentication is protection against accidental or untrusted local access, not a replacement for
operating-system account isolation. Do not print the token to shared logs.

## Protocol version 1

Every message has a fixed 24-byte little-endian header:

```text
u32  magic = bytes "CNAI"
u16  major version = 1
u16  minor version = 0
u16  message type
u16  flags = 0
u32  payload byte count (maximum 8 MiB)
u64  request identifier
```

The first payload is `ClientHello`: supported major-version range, requested capability mask,
bounded client name, and token. `ServerHello` selects version 1.0, reports
`IDiagnosticsProvider::InterfaceVersion == 1`, intersects requested/available capabilities, states
the hard limits, and provides application/session identity. Failure returns a structured error and
closes the unauthenticated connection.

Post-negotiation messages are:

| Request | Response | Behavior |
|---|---|---|
| `SnapshotRequest` | `SnapshotResponse` | Calls `CaptureSnapshot()` once and transmits only requested metric, frame, and/or resource sections. Total counts remain available. |
| `EventsRequest` | `EventsResponse` | Calls `ReadEvents(afterSequence, maximum)` with a maximum of 4,096; each returned name is resolved. Cursor bounds and both history/producer loss values are unchanged. |
| `PreviewRequest` | `PreviewResponse` | Starts one explicit bounded preview through the separately installed source, or reports unavailable. |
| `PreviewPollRequest` | `PreviewResponse` | Polls an asynchronous ticket without waiting. |
| `Ping` | `Pong` | Checks the authenticated connection. |

All 64-bit integers become JSON strings in the browser bridge so JavaScript cannot silently round
resource IDs, event sequences, counters, or nanosecond timestamps above `2^53`.

Unknown capability bits can be added in a future minor version. An incompatible wire format needs
a new major version. The diagnostics provider and wire protocol versions are deliberately separate.

## Demand and backpressure model

The UI requests the metrics/frame overview and up to 512 new events twice per second. It advances
its cursor to the last event actually delivered so a bounded page cannot silently skip a backlog.
Its graph uses the provider's bounded 240-frame history. Its local event model discards the oldest
entries above 1,000. Resource metadata is transmitted only after the user opens the Resources view or
presses **Refresh metadata**. Resource tables are filterable and sortable without another game
request.

Provider v1 returns an owned, complete diagnostics snapshot. An overview call therefore copies
the diagnostics resource registry internally even when the protocol omits that section. This is
the provider contract explicitly handed to Inspector; it does not enumerate arbitrary engine
objects, touch GPU contents, or serialize those resources. At the modest two-hertz pull rate its
measured cost is small. No diagnostics-core redesign was required or justified.

Slow clients apply socket backpressure only to the agent thread. The agent constructs at most one
bounded request and one bounded response. It never lets a slow UI grow an application queue and
never waits for Inspector from the render thread.

## UI views

The embedded HTML, CSS, and JavaScript have no CDN or Internet dependency and use no frontend
framework. The responsive UI follows the operating-system light/dark preference and shows a clear
connected/reconnecting state.

- **Session**: CNA version, target OS, platform backend, renderer, build configuration, protocol,
  provider interface, limits, application metadata, and negotiated capabilities.
- **Performance**: FPS, latest frame time, bounded frame graph, current metrics, resource count,
  and diagnostics-owned memory.
- **CPU profiler**: top zones aggregated over at most 1,000 local events and a bounded recent zone
  timeline.
- **Graphics**: exact common draw metrics, resource counts, and the declared-byte aggregate. It
  explicitly states that provider v1 offers no universal shader reflection.
- **Resources**: manually refreshed, sortable/filterable texture, buffer, render-target, audio,
  and custom metadata with stable ID, format, dimensions, mips, bytes, and accuracy.
- **Audio**: published mixer metrics and any registered audio resources, with allocated voices
  described accurately rather than relabeled as audible voices.
- **Input**: an explicit unavailable state. Inspector does not probe backend internals or inject
  input when provider v1 publishes no input metrics.
- **Events**: bounded filterable log containing sequence, frame, thread, kind, category, name,
  duration, and value.

Events and the CPU profiler follow the live tail. On connect they start from the most recent 1,000
events instead of replaying the provider's history, and a view that falls more than 1,000 events
behind skips ahead rather than replaying events it would discard. Replaying at one bounded batch
per poll left these views seconds or minutes behind the game, and never caught up once the game
produced events faster than the view pulled them.

Any loss produces a visible discontinuity banner. It reports what this view actually missed since
it connected: events it did not receive, and producer drops and malformed profiling operations
that occurred after it connected. The provider's counters are cumulative for the whole process,
and ring-buffer overwrites are normal operation once the history wraps, so reporting either raw
would leave the banner on permanently and hide the losses that matter.

A metric the running build has not published is shown as `—`, never as a measured zero. In
particular, `SpriteBatch` submits sprites to the renderer directly rather than through
`GraphicsDevice` draw calls, so a game that draws only with `SpriteBatch` publishes
`Graphics/SpriteSubmissions` but no `Graphics/DrawCalls`. Estimated, exact, and unavailable values remain
visually distinct. The UI does not imply total process memory, driver memory, device state, shader
reflection, input state, or subsystem data that CNA has not actually published.

## Resource previews

Metadata never triggers readback. `IResourcePreviewProvider` is a separate Inspector-side seam and
no provider is installed by default. A renderer integration may implement `RequestPreview()` and
`PollPreview()` only if both return promptly: start asynchronous device work, return `Pending`, and
later return an already-available PNG, JPEG, or WebP. It must never wait for a device or queue.

Safeguards are enforced outside the source as well:

- only a currently registered texture or render target ID is accepted;
- the user must press **Preview** for each operation;
- the default UI requests at most 1,024 by 1,024 and 4 MiB;
- the absolute protocol bounds are 4,096 by 4,096 and 4 MiB;
- at most four tickets may be pending, with a default 250 ms start cooldown;
- the UI polls a returned ticket at 250 ms and stops after ten seconds;
- source results with a changed ticket, unsafe MIME type, invalid dimensions, or excessive bytes
  are rejected without terminating the game.

There is no continuous texture/render-target readback and buffer contents are never exposed.
Current CNA renderers do not yet install a preview provider, so production sessions truthfully show
the feature as unavailable. The seam exists without changing diagnostics provider v1 or renderer
behavior.

## Platform status

The optional agent and bridge use only the C++ standard library plus operating-system socket and
secure-random services:

| Target | Implementation | Current validation |
|---|---|---|
| Linux/POSIX desktop | POSIX sockets | Built, unit/integration tested, benchmarked on Linux x86-64 |
| Windows desktop | Winsock 2 plus system RNG, `ws2_32`/`bcrypt` private links | MinGW-w64 cross-build gate |
| macOS desktop | POSIX sockets with `SO_NOSIGPIPE` | Implemented; native runtime validation remains pending |
| Web/Emscripten | Not built | Unsupported: no separate local process/socket model |
| Android/iOS | Not built | Unsupported: app sandbox/lifecycle and device transport need a separate design |

`CNA_BUILD_INSPECTOR=ON` fails at configure time on Web, Android, and iOS rather than silently
compiling a nonfunctional endpoint. Ordinary CNA builds on every target keep the option off.

## Performance

Compiled-out mode has no Inspector definition, target, object, thread, listener, state, or check in
the CNA application. Compiled-in support is a separately linked static library; no global object
starts work. A linked application that never calls `Agent::Start()` performs no Inspector work. A
started agent with no client blocks in the operating system and does not call the provider.

Measured results and reproduction commands are in
[`inspector-benchmark.md`](inspector-benchmark.md). On the recorded system, linked-but-disabled and
enabled-without-client application loops were within noise of the compiled-out control, with zero
idle provider calls. Live snapshot/event costs occur on the agent thread. Preview measurements are
reported separately and do not claim renderer GPU-readback cost.

## Verification

- Native Debug protocol, frontend-model, and real-loopback agent integration tests pass 24/24;
  the same 24/24 pass under AddressSanitizer and UndefinedBehaviorSanitizer.
- The immutable diagnostics provider regression suite passes 17/17.
- Native `-Wall -Wextra -Wpedantic -Werror` and MinGW-w64 cross-builds pass, including the
  Winsock and Windows system-RNG path.
- The default-OFF configuration builds the standalone compiled-out benchmark and exposes no
  `cna-inspector` target.
- Embedded JavaScript passes Node 20 parsing. A live HTTP smoke test verifies the offline assets,
  CSP, UI-token gate, bad-Host rejection, and disconnected-agent response.

## Troubleshooting

- **`CNA::Inspector` target missing**: configure with `-DCNA_BUILD_INSPECTOR=ON`.
- **No events/CPU zones**: build and run with `CNA_DIAGNOSTICS=FULL`; STATS intentionally has no
  event stream.
- **Connection refused**: the application did not start the agent, has stopped, or the printed port
  belongs to a previous run.
- **Authentication failed**: use the token from the same application run; generated tokens change
  on every start.
- **Browser says reconnecting**: the bridge retains no fake state. It reconnects on its next poll
  after the application and agent become available.
- **Preview disabled/unavailable**: this is expected unless the application installed a
  renderer-specific non-blocking preview source and the selected resource kind supports it.
- **Limit error**: reduce event count or preview bounds. Oversized complete snapshots are refused
  rather than partially mislabeled.

## Extension points and deliberately deferred work

New cheap statistics belong in the diagnostics foundation or an `IDiagnosticsSource`. Asynchronous
GPU timing stays renderer-local behind that source and must never wait. A renderer resource-preview
implementation belongs behind `IResourcePreviewProvider`, not in `IDiagnosticsProvider`.

Deferred on purpose: renderer preview implementations, macOS runtime evidence, mobile/device
transport, TLS, multi-client fan-out, input-state metrics, universal effect/shader reflection,
game-object plugins, and offline trace comparison. A future game-specific data extension must be a
separately versioned, bounded, opt-in capability; it must not turn Inspector into an editor or
generic object/memory browser.
