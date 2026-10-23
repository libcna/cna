# `CNA::Devices` — device extensions beyond XNA 4.0

`CNA::Devices` holds device and system capabilities that have no XNA 4.0/Windows Phone 7
equivalent: battery, locale, clipboard, URL launching, CPU/RAM, display scale and safe area,
native file dialogs, tray icon, message boxes and camera capture. The real XNA surface stays in
`Microsoft::Devices`/`Microsoft::Devices::Sensors` and is never extended with these classes.

## Build and layout

- CMake option `CNA_DEVICES` (OFF by default) defines the `CNA_DEVICES` compile definition.
  Every header under `CNA/Devices/` is wrapped in `#ifdef CNA_DEVICES`, so with the option off the
  module compiles to nothing.
- Module `modules/devices-ext/` (target `cna_devices_ext`, alias `CNA::DevicesExt`). It depends on
  runtime, graphics, core and math, never on the XNA devices base (`docs/physical-modules.md`).
- Tests: `modules/devices-ext/tests/CNA/Devices/`, built as `CnaDevicesExtTests`. The
  `devices-asan`, `devices-ubsan` and `devices-tsan` presets in `CMakePresets.json` build with
  `CNA_DEVICES=ON`.

## Design rules

- **The namespace marks it as an extension.** Members carry no per-member `CNAEXT` tag. Doxygen
  opens with "CNA extension — no XNA/WP7 equivalent exists."
- **Host access goes only through the platform contract.** Each class calls a
  `CNA::Platform::IPlatform` service (`IPlatformSystemInfo`, `IPlatformClipboard`,
  `IPlatformDialogs`, `IPlatformTray`, `IPlatformCamera`, …). The module links no native library
  and includes no SDL header; SDL3 is the default platform implementation behind those services
  (`docs/platform-abstraction.md`).
- **Support is reported, never faked.** A capability that a platform may lack exposes
  `getIsSupportedProperty()`. Headless and terminal platforms report false rather than doing
  nothing silently.
- **Tests never reach a real dialog, tray or camera.** They install a canned platform
  (`CannedDialogPlatform`, `CannedTrayPlatform`, `CannedCameraPlatform` through
  `ScopedCurrentPlatform`, under `modules/platform/tests/CNA/Platform/`). The public classes have
  no test-only hooks. A real file dialog once launched `zenity` from a test and waited forever.
- **Do not duplicate XNA.** Orientation and window bounds stay with `GameWindow`, storage stays
  with `StorageDevice`/`TitleContainer`, and microphone and touch input stay with their XNA classes.

## Capabilities

| Class | What it does | Availability and caveats |
|---|---|---|
| `PowerInfo` | Power state, battery percent, seconds remaining (static). | All platforms; desktops without a battery report `NoBattery`; -1 when unknown. |
| `Locale` | Preferred locales (language, country) in user order. | All platforms. |
| `Clipboard` | Get/set/has text. | Main thread only. On Web it needs a user-gesture context and returns false/empty otherwise. |
| `UrlLauncher` | `Open(url)` through the platform's external-URL service. | Desktop shell, mobile intent or browser; false when nothing could handle it. |
| `SystemInfo` | Logical CPU cores, system RAM in MB. | All platforms. SIMD flags are deliberately not exposed. |
| `DisplayInfo` | Content scale and safe area of the game's own `GameWindow`. | Read-only over the existing window; never creates one. |
| `FileDialog` | Asynchronous open-file, save-file and open-folder dialogs. | Windows, Linux, macOS and Android; not iOS or Web. Non-modal. Cancel and error both deliver an empty list. |
| `SystemTray` | One tray icon with a flat menu, owned by the instance. | Desktop only. |
| `MessageBox` | `ShowSimple` and `Show` with custom buttons. Synchronous: it blocks the calling thread. | Every desktop, mobile and web platform; not headless or terminal. |
| `Camera` | Enumerates cameras, opens the first one and polls RGBA8 frames into a `Texture2D` (`TryAcquireFrame` once per frame). States: `NotSupported`, `Closed`, `Opening`, `Denied`, `Ready`, `Lost`. | Wherever the platform has a camera provider. Web needs HTTPS and a user gesture. Design: `docs/cna-devices-camera-design.md`. |

`MessageBox.Show` marks the first custom button as the Return default and the last as the
Escape default (both keys choose index zero for one button). These are native-provider hints:
SDL's Zenity implementation currently ignores keyboard-default flags, so that route does not
guarantee these key-to-button choices. The native SDL X11 toolkit can be regression-tested, without touching
the desktop, with `bash tools/platform/run_message_box_tests_private.sh cmake-build-multi`.
That harness requires xdotool, opts into the otherwise noninteractive test suite, and deliberately
selects the X11 toolkit rather than Zenity; it is not a Zenity, Windows or macOS validation claim.

`SystemTray.getIsSupportedProperty()` reports the selected platform's tray-service capability,
not the presence of a running desktop notification-area host. On Linux it can be true with no
session bus or StatusNotifier watcher. Constructing `SystemTray` still reports that failure
through `CNA::Platform::PlatformException`; handle it and continue without the icon when a tray
is optional. The query does not initialize a tray or probe D-Bus. This preserves the documented
support-query link boundary and avoids treating a point-in-time host probe as an allocation guarantee.

## Considered and rejected

These were rejected in the original analysis (2026-07-06) and its second round (2026-07-07).
Reopen one only for a concrete need.

- **Geolocation/GPS; Bluetooth, NFC, biometrics, phone/SMS, contacts, push notifications.**
  SDL3 has no API for any of them; each would be its own per-platform native integration.
- **Storage paths and `SDL_Storage`.** XNA `StorageDevice`/`TitleContainer` already answer
  "where do I load and save". A second way would be worse than none.
- **Microphone and touch-device enumeration.** XNA `Microphone` and `TouchPanel` cover them.
- **Raw HID (`SDL_hidapi`).** A whole low-level, security-sensitive subsystem with no consumer.
- **OpenXR/VR.** A renderer-level runtime integration, not a device wrapper.
- **Dynamic library loading.** Forbidden in practice on iOS and unavailable in static Emscripten
  builds, which leaves only a desktop niche.
- **Asynchronous file I/O.** Not a device capability; it would belong to a separate `CNA::IO`.
- **Multi-monitor enumeration, process control and pen-device queries.** Rejected by the owner as
  not a fit for the project.

## History

Task records are in `plans/plan_cna_devices.md` (`DEVICES-CNA-000`..`012`). The original
analysis that proposed this layer was the root `noxna_devices.md`; Git history keeps it.
