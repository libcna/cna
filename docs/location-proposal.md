# Location (GPS) for CNA: a proposal

**Status: proposal with the owner's decisions taken (2026-10-25); nothing implemented,
rechecked 2026-11-01.** No Location capability/service or GeoCoordinate/GeoCoordinateWatcher
implementation exists in CNA or the local Sharp Runtime checkout. It
replaces the five-line TODO that used to be this file, and it extends
[`location-future-plan.md`](location-future-plan.md), which records where location
support belongs and why. The decisions are listed at the end; the sections below already follow
them.

## In one paragraph

A game asks for the device's position through the API Windows Phone 7 games used,
`System.Device.Location`: create a `GeoCoordinateWatcher`, start it, and read `Position` or handle
`PositionChanged`. The .NET types live in Sharp Runtime, beside the rest of `System.*`. The position
itself comes from a new platform service, `IPlatformLocation`, with one backend per operating
system: Android's `LocationManager`, Apple's Core Location, Windows' `Geolocator`, GeoClue on Linux
and the browser's Geolocation API. These are proposed integrations, not implemented platform services
or qualified host support. The design consumes the operating system's available position and
permission model; it does not assume every provider has satellite, Wi-Fi and cell data.

## Why not `Location : SensorBase<LocationReading>`

The TODO this file used to hold proposed exactly that: a `LocationReading` and a `Location` sensor
next to `Accelerometer` and `Compass`. It is the one design that is ruled out. On Windows Phone 7
location was never part of `Microsoft.Devices.Sensors`. It lived in its own namespace and assembly,
`System.Device.Location` (`System.Device.dll`), with a different shape: a watcher with a permission
state, a desired accuracy, a movement threshold and an unknown position, not a sensor with a reading
rate. `docs/location-future-plan.md` records that decision, and the devices plan
(`COMPASS-003`) re-confirmed it. The TODO's fields all have a home in the real API:

| TODO item | Real API |
|---|---|
| `LocationReading` | `GeoPosition<GeoCoordinate>`: a `GeoCoordinate` plus a timestamp |
| `Location : SensorBase<…>` | `GeoCoordinateWatcher` |
| latitude, longitude | `GeoCoordinate::Latitude`, `Longitude` |
| altitude, speed, heading | `Altitude`, `Speed`, `Course` (not-a-number when unknown) |

## The API a game sees

The surface below is a planning summary of Microsoft's
[GeoCoordinateWatcher](https://learn.microsoft.com/en-us/dotnet/api/system.device.location.geocoordinatewatcher)
and [GeoCoordinate](https://learn.microsoft.com/en-us/dotnet/api/system.device.location.geocoordinate?view=netframework-4.8.1)
references. It is not a complete declaration/visibility matrix. Before implementation, verify the
archived Windows Phone member set and behavior separately from the desktop .NET Framework API.

| Type | Members |
|---|---|
| `GeoCoordinateWatcher` (`IDisposable`, `IGeoPositionWatcher<GeoCoordinate>`, `INotifyPropertyChanged`) | constructors `()` and `(GeoPositionAccuracy)`; `DesiredAccuracy`, `MovementThreshold` (metres), `Permission`, `Position`, `Status`; `Start()`, `Start(bool suppressPermissionPrompt)`, `TryStart(bool, TimeSpan)`, `Stop()`, `Dispose()`; public PositionChanged/StatusChanged; PropertyChanged through its explicit interface in the desktop reference |
| `GeoCoordinate` (`IEquatable<GeoCoordinate>`) | constructors `()`, `(lat, lon)`, `(lat, lon, alt)`, `(lat, lon, alt, hAcc, vAcc, speed, course)`; `Latitude`, `Longitude`, `Altitude`, `HorizontalAccuracy`, `VerticalAccuracy`, `Speed`, `Course`, `IsUnknown`; static `Unknown`; `GetDistanceTo`; equality on latitude and longitude only; `ToString` |
| `GeoPosition<T>` | `Location`, `Timestamp` (`DateTimeOffset`) |
| `GeoPositionChangedEventArgs<T>`, `GeoPositionStatusChangedEventArgs` | `Position`; `Status` |
| `GeoPositionAccuracy` | `Default`, `High` |
| `GeoPositionStatus` | `Ready`, `Initializing`, `NoData`, `Disabled` |
| `GeoPositionPermission` | `Unknown`, `Granted`, `Denied` |

In C++ the members follow the project conventions: properties as `getXProperty()`/`setXProperty()`,
events as `System::EventHandler<T>`, `System::IDisposable`. A game would write:

```cpp
using namespace System::Device::Location;

GeoCoordinateWatcher watcher(GeoPositionAccuracy::High);
watcher.setMovementThresholdProperty(20.0);   // metres between PositionChanged events
watcher.PositionChanged += [](System::Object*, const GeoPositionChangedEventArgs<GeoCoordinate>& e) {
    const GeoCoordinate& here = e.getPositionProperty().getLocationProperty();
    // here.getLatitudeProperty(), here.getLongitudeProperty(), here.getHorizontalAccuracyProperty()
};
watcher.Start();
```

`CivicAddress` and `CivicAddressResolver` (street addresses from coordinates) are left out: they
need separate reverse-geocoding integration and have no identified consumer in this scope.

## Where the code lives

The project rule is that a `System.*` type belongs in Sharp Runtime, and that rule fits here once
the platform part is kept out of it. The split (decision 1):

- **Sharp Runtime, a new `device-location` component**: the types above, `GeoCoordinate`'s
  validation and `GetDistanceTo`, the watcher's state machine, movement-threshold filtering, and a
  narrow seam, `System::Device::Location::Detail::IGeoPositionProvider`, that something else fills.
  The proposed no-provider behavior is Disabled status and unknown Position; verify exception,
  permission and event details during LOC-1 rather than treating that sketch as full .NET parity.
  Existing prerequisites include
  `System::DateTimeOffset` in `core` and `INotifyPropertyChanged` in `component-model`.
- **CNA**: the `IPlatformLocation` platform service and its backends, and the small adapter that
  installs a provider over it when a `Game` starts.

Putting everything in CNA's `modules/devices` under `System::Device::Location` would wire up more
simply, but it would break the rule and leave the types unusable from Sharp Runtime on its own; it
was rejected.

## The platform service

The pinned SDL3 tree has no location service, so these providers cannot come from its device API.
They would sit inside the platform
implementations, like the JNI calls `Sdl3SystemServices` already makes on Android. The contract
follows the rules in `docs/platform-abstraction.md`:

- a new `PlatformCapability::Location`, and `IPlatform::GetLocation()` non-null exactly when it is
  true; `HEADLESS` and `TERMINAL` report false;
- `OpenSession(desiredAccuracy, movementThreshold)` returns an owning session that reports fixes,
  status and permission changes through one callback, never through per-field queries in a loop;
- a fix is a plain value: latitude, longitude, altitude, horizontal and vertical accuracy, speed and
  course (not-a-number when the source does not report one), and a UTC timestamp;
- permission is part of the contract, not an afterthought: query it, request it, and report a
  refusal as a status, never as a silent absence of fixes.

| Host | Backend | Notes |
|---|---|---|
| Android | `android.location.LocationManager` through JNI | AOSP only (decision 4), with no Google Play services dependency. Plan a small platform Java helper and explicit runtime permission handling; recheck SDK/permission and approximate-location behavior during LOC-5. This does not guarantee identical providers/fixes on every device. |
| iOS, macOS | Core Location (`CLLocationManager`) in Objective-C++ | Needs the usage-description keys in `Info.plist` and, on macOS, an app bundle. When-in-use authorisation only. |
| Windows | `Windows.Devices.Geolocation.Geolocator` | Usable from desktop applications, subject to the system's location privacy setting. It needs C++/WinRT, so it is built with MSVC or clang-cl only; a MinGW build reports `PlatformCapability::Location` as false (decision 3). The older COM Location API is deprecated and not used. |
| Linux desktop | GeoClue 2 over D-Bus | An optional dependency, detected like the other optional ones. GeoClue asks for the application's desktop ID; sandboxed (Flatpak) builds go through the xdg-desktop-portal location portal. |
| Linux with a GPS receiver | `gpsd` | The one route that really is "via GPS": kiosks, vehicles, single-board computers. Not planned (decision 5); added only for a concrete need. |
| Browser | `navigator.geolocation` | Subject to secure-context, permission and embedding policy. Check the actual target/browser contract during LOC-6. |
| Tests | a fake provider and a recorded-track replay (GPX or NMEA) | Deterministic positions, permission refusals and signal loss without hardware. |

## Events, threads and lifecycle

- **Which thread raises the events.** CNA's sensors raise theirs on whatever thread the operating
  system calls back on (`docs/devices-event-contract.md`). Location does the opposite (decision 2):
  fixes are queued and `PositionChanged` and `StatusChanged` are raised on the game thread before
  the next `Update`. Native arrival rate is provider-dependent; implementation must define bounded
  queue/coalescing, cancellation and teardown behavior. Game-thread delivery avoids making event
  handlers mutate game state from a native provider thread. LOC-1 still records the WP7 behavior
  and any intentional deviation; no measured WP7 event-thread result is claimed here.
- **Movement threshold.** The shared Sharp Runtime layer applies `MovementThreshold` with
  `GetDistanceTo`, so every backend filters the same way whatever the operating system offers.
- **`TryStart(bool, TimeSpan)` blocks.** On the game thread it stalls a frame for up to its timeout,
  and in the browser the main thread must never block, so there it returns `false` at once and the
  game waits for `StatusChanged` instead.
- **Background.** The watcher stops delivering while the game is suspended (the platform's
  `WillEnterBackground`/`DidEnterForeground` events) and resumes afterwards. Background location
  is not supported.

## Privacy

Position is personal data. Coordinates are never written to CNA's log, kept on disk by CNA or sent
anywhere by it. `Start(true)` never shows a permission prompt. Location stays off unless a game
creates a watcher, and the manifest and `Info.plist` entries a store requires are documented in the
app templates rather than added silently to every build.

## Compass true heading

`Compass::TrueHeading` currently equals `MagneticHeading`, because magnetic declination needs a
position. `docs/location-future-plan.md` already describes how a compass backend would use this
work: through a narrow, optional `Detail::IDeclinationSource` that evaluates the NOAA World Magnetic
Model (public domain) for the current position and date, with no new public member on `Compass`.
It is a follow-up, not part of the first delivery.

## Native C API boundary

A future native C API route family can be scoped after the C++ surface is stable, with explicit
session ownership, callback cancellation and error/permission contracts. No geo routes exist now.
Language-specific bindings are outside the current approved planning boundary.

## Tests

- `GeoCoordinate`: range validation and its exceptions, `Unknown` and `IsUnknown`, equality on
  latitude and longitude only, `GetHashCode` consistency, `ToString`, and `GetDistanceTo` against
  known distances.
- The watcher, against the fake provider: every status transition, permission granted and denied,
  `Start(true)` without a prompt, `TryStart` timing out, movement-threshold filtering, `Stop`, and
  disposing the watcher from inside its own handler.
- Real backends: the Android emulator (`adb emu geo fix`), the iOS simulator's simulated location,
  and headless Chrome with a DevTools geolocation override, all inside the private test runner and
  never on the live desktop.

## Delivery order

| Step | Content |
|---|---|
| LOC-1 | Check the Windows Phone 7 surface and record its event-thread behaviour. |
| LOC-2 | Sharp Runtime `device-location`: the value types, `GetDistanceTo`, their tests. |
| LOC-3 | `GeoCoordinateWatcher` with the provider seam, the fake provider and track replay, their tests. |
| LOC-4 | `IPlatformLocation`, `PlatformCapability::Location`, refusal on `HEADLESS`/`TERMINAL`, conformance tests. |
| LOC-5 | Android backend and the template's Java helper; emulator test. |
| LOC-6 | Browser backend; headless Chrome test. |
| LOC-7 | Linux GeoClue backend. |
| LOC-8 | Core Location backend; needs the Apple machine. |
| LOC-9 | Windows `Geolocator` backend (MSVC/clang-cl). |
| LOC-10 | Optional, separately scoped: compass declination and native C API routes. |
| LOC-11 | Merge this proposal (`docs/location-proposal.md`) and `docs/location-future-plan.md` into one `docs/location.md`. |

## Decisions (owner, 2026-10-25)

1. **Placement:** the .NET types and the watcher go into a new Sharp Runtime `device-location`
   component; CNA supplies `IPlatformLocation` and the adapter.
2. **Event thread:** `PositionChanged` and `StatusChanged` are raised on the game thread before the
   next `Update`, unlike the sensors.
3. **Windows:** `Geolocator` with MSVC or clang-cl only; MinGW builds report no location.
4. **Android:** AOSP `LocationManager` only, no Google fused provider.
5. **`gpsd`:** not planned.
6. **Documents:** when implementation starts, this proposal and `docs/location-future-plan.md`
   become a single `docs/location.md` (LOC-11).
