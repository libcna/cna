# GPS / Location — Future Plan (Not Implemented)

## This does not belong in `Microsoft.Devices.Sensors`

GPS/location is **not part of the real `Microsoft.Devices.Sensors` API** on Windows
Phone 7. It lives in a separate WP7 namespace/assembly: `System.Device.Location`
(`Microsoft.Devices.dll` vs. `System.Device.dll` are distinct real WP7 assemblies).
`Microsoft::Devices::Sensors` in this codebase (`Accelerometer`, `Compass`, `Gyroscope`,
`Motion`) and `Microsoft::Devices::VibrateController` correctly have no location
member, property, or class — this is not an omission, it is the accurate API surface.

**This document exists so a future session doesn't accidentally "complete" location
support by bolting it onto `Microsoft::Devices::Sensors`** (an easy, plausible-looking
mistake — location conceptually feels like "another sensor") — and so there's a
concrete starting sketch if/when location support is ever actually scoped as its own
task. **Nothing in this document is implemented. No code exists for any of this yet.**

---

## If ever implemented: where it belongs

A future compatibility layer for this should live under a `System::Device::Location`
namespace (mirroring the real WP7 assembly/namespace split), e.g.:

```text
include/System/Device/Location/GeoCoordinateWatcher.hpp
include/System/Device/Location/GeoCoordinate.hpp
include/System/Device/Location/GeoPositionChangedEventArgs.hpp
include/System/Device/Location/GeoPositionStatusChangedEventArgs.hpp
include/System/Device/Location/GeoPositionStatus.hpp
include/System/Device/Location/GeoPositionAccuracy.hpp
```

Likely real API members (from the documented WP7 `System.Device.Location` surface —
not independently re-verified against an archived MSDN page the way
`Microsoft::Devices::Sensors` was in `plan_devices_phase2.md` Task P2-2; treat these as
a starting sketch, re-verify before actually implementing):

- **`GeoCoordinateWatcher`** — the main entry point, roughly mirroring this codebase's
  existing sensor-class shape (`Start()`, `Stop()`, a `PositionChanged` event,
  a `Status`/`StatusChanged` pair) but for location specifically. Constructor takes a
  `GeoPositionAccuracy` (`Default`/`High`) desired-accuracy hint.
- **`GeoCoordinate`** — `Latitude`, `Longitude`, `Altitude`, `HorizontalAccuracy`,
  `VerticalAccuracy`, `Speed`, `Course`, plus `IsUnknown`/`Unknown`.
- **`GeoPositionChangedEventArgs<GeoCoordinate>`** — generic over the position type in
  real WP7 (`GeoPosition<T>`), carrying `.Position.Location` (the `GeoCoordinate`) and
  `.Position.Timestamp`.
- **`GeoPositionStatusChangedEventArgs`** / **`GeoPositionStatus`** enum
  (`Disabled`/`Initializing`/`NoData`/`Ready`) — mirrors this codebase's existing
  `SensorState` shape closely enough that `SensorState`-adjacent naming conventions
  already established for `Microsoft::Devices::Sensors` would translate directly.

## Android path (if ever implemented)

- **AOSP / no-GMS baseline:** `android.location.LocationManager`
  (`requestLocationUpdates()`, `GPS_PROVIDER`/`NETWORK_PROVIDER`) — works on any Android
  device, no Google Play Services dependency. This should be the default/required
  backend, matching this project's general "don't require proprietary vendor services
  for a core feature" posture (see e.g. `VibrateController`'s SDL3-only approach, no
  vendor-specific rumble APIs).
- **Optional, `NOXNA`/opt-in enhancement:** Google's Fused Location Provider API
  (`com.google.android.gms.location.FusedLocationProviderClient`) — better accuracy/
  battery behavior, but requires Google Play Services, so it must be a strictly
  optional, separately-buildable backend, never the only path, to keep AOSP/no-GMS
  devices fully supported.
- Requires `ACCESS_FINE_LOCATION`/`ACCESS_COARSE_LOCATION` runtime permission
  (Android 6.0+) — a JNI/Kotlin bridge would need to surface permission-denied as a
  `GeoPositionStatus` value (likely `Disabled` or a new status), not a silent failure.

## iOS path (if ever implemented)

- `CoreLocation` (`CLLocationManager`, `CLLocationManagerDelegate.locationManager(_:didUpdateLocations:)`),
  requesting `requestWhenInUseAuthorization()` (or `requestAlwaysAuthorization()` if
  background location is ever needed, which XNA/WP7 parity would not require).
- `CLLocation` → `GeoCoordinate` is a near-direct field mapping (`coordinate.latitude`/
  `.longitude`, `altitude`, `horizontalAccuracy`, `verticalAccuracy`, `speed`, `course`).

## Explicitly not doing right now

- No `GeoCoordinateWatcher`/`GeoCoordinate`/any `System::Device::Location` type exists
  in this codebase yet. This document is a placeholder for a future, separately-scoped
  task — not a task itself.
- No GPS/location member should be added to any `Microsoft::Devices::Sensors` class
  under any circumstances, including as a `NOXNA` extension — the real WP7 API draws
  this line clearly (separate assembly), and this codebase should preserve that
  separation even where CNA takes liberties elsewhere.
