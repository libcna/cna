# Notification regressions

`CnaNotificationLifecycleTests` exercises actual listener ownership, cancellation
and queued notification delivery; its syscall wrappers observe entry into a real
blocked receive/event wait rather than substituting the implementation.

`CnaNotificationOpenFailureTests` injects one failing socket setup operation at
a time (socket, bind, listen, getsockname). It verifies queued ChannelOpenFailed
events, HRESULT/native diagnostics preserved across cleanup, descriptor ownership,
no published URI/registry entry, caller-thread dispatch, reentrant Close, and a
subsequent successful open. No resource-exhaustion or timing trick is used.
The SDK contract evidence is in [REFERENCE.md](REFERENCE.md).

`CnaNotificationSenderTests` compiles the actual sender and scripts transport
responses at the syscall boundary. A real loopback socket accepts the outgoing
bytes. The receiver acknowledgement, not local enqueue success, decides the public
return value. Tests cover fragmented/interim/success/refusal/redirect responses,
EOF, nonblocking no-response, incomplete/malformed headers, partial writes and
interrupted reads/writes. These transport-level tests have no sleeps or retries.

```sh
cmake --build cmake-build-multi --parallel 1 \
  --target CnaNotificationLifecycleTests CnaNotificationSenderTests CnaNotificationOpenFailureTests CnaPhoneTests
tools/platform/run_gpu_tests_private.sh cmake-build-multi \
  -R '^CnaNotification(Sender|Lifecycle|OpenFailure)_' --output-on-failure -j1
tools/platform/run_gpu_tests_private.sh --exec cmake-build-multi/CnaPhoneTests
```

The Linux/GNU-compatible scripted sender tests supplement, not replace, the
existing native end-to-end raw/toast sender/receiver tests in `CnaPhoneTests`.

The opt-in Emscripten executable uses **real browser socket behavior**, with no
transport interposition. It sends both message types to the museum's unreachable
loopback endpoint and requires false results. It also opens a channel, requires
one queued ChannelOpenFailed event and no published URI, and dispatches twice
to guard against duplicate errors. With an existing Emscripten build,
Node 22+ and Chrome:

```sh
cmake --build cmake-build-wasm-webgpu --parallel 1 --target cna_notification_browser_probe
tools/platform/run_gpu_tests_private.sh --exec node tools/phone/run_browser_notification_test.mjs \
  cmake-build-wasm-webgpu/modules/phone/cna_notification_browser_probe.js \
  cmake-build-wasm-webgpu/notification-browser-result.json
```

The report path must be new. The driver starts its own headless Chrome profile
under the build directory and a loopback HTTP server, records console output and
uncaught exceptions, then terminates both. Override `CNA_CHROME` for another Chrome
executable. The test does not contact a push service or show anything on the live
desktop. It verifies CNA's browser sender refusal and failed-open event, **not**
Windows Phone's retired push service.

The sender accepts a final HTTP 2xx response after complete headers; its small
acknowledgement parser caps the combined response headers at 64 KiB. This API
does not consume or return a response body. `SendAsync` retains its existing
synchronous bool signature; no asynchronous completion handle is implied.
