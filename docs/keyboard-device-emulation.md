# Keyboard device emulation

These are two independent, owner-requested CNA extensions, disabled by default. They use the
selected platform's once-per-frame keyboard snapshot and Game's event pump, before Update.
They do not add sample-specific input branches or change physical-device availability when off.

## Accelerometer

```cpp
using Microsoft::Devices::Sensors::Accelerometer;
Accelerometer::setKeyboardEmulationEnabledEXT(true);
```

`getKeyboardEmulationEnabledEXT()` reports the process-wide mode. With it enabled,
`Accelerometer::getIsSupportedProperty()` is true on desktop and Browser, including without
physical sensor hardware or browser motion permission. The primary accelerometer is replaced by
an explicitly named software sensor. Gyroscopes and sided/controller sensors retain their physical
behavior. `CNA::Input::Sensors` enumeration and primary accelerometer polling use the same source.
Platform hardware capabilities remain hardware reports; emulation is selected above them.

Use ordinary Accelerometer instances, Start/Stop/Dispose, CurrentValue, IsDataValid,
CurrentValueChanged and legacy ReadingChanged. Objects constructed before the opt-in refresh
support when Start succeeds. Per-instance TimeBetweenUpdates and callback order remain unchanged.
Stop all running accelerometers before changing mode; otherwise InvalidOperationException is
thrown. Independent platform sessions retain their polling and callback lifetime barriers,
including reentrant close. The source requires no physical sensor subsystem acquisition.

Arrows produce `(Right - Left, Up - Down, -1)` normalized to one g. No keys yields `(0,0,-1)`;
opposite directions cancel and diagonals keep unit magnitude. Public Accelerometer values use g;
the platform and CNA Input polling use m/s² with standard gravity 9.80665. Readings use natural
emulation axes independently of display orientation and bypass the Android hardware-only remap.
Focus loss supplies neutral readings. Orientation emulation is not implicitly enabled.

New keyboard readings/events are delivered when Game pumps input, at its frame cadence. Outside
a running/pumped game the software source retains its last value, initially neutral. This is a
software input feature, not a claim to reproduce physical motion or an independent sensor clock.

## Window orientation

```cpp
getWindowProperty().setKeyboardOrientationEmulationEnabledEXT(true);
```

`getKeyboardOrientationEmulationEnabledEXT()` reports the per-window mode. Set it on the game
loop thread; it can be enabled before or after device creation. Up requests Portrait, Left requests
LandscapeLeft and Right requests LandscapeRight. XNA has no inverted-portrait value, so Down
is unused. Release retains orientation; repeats and simultaneous ambiguous requests are ignored.
Only a focused game accepts requests. Keys can therefore also drive an independently enabled
accelerometer, but the two features do not enable or update each other.

Requests honor GraphicsDeviceManager.SupportedOrientations. Default follows the preferred
backbuffer shape (portrait only or both landscapes). A disallowed request is remembered without
resetting the device and becomes effective when the game permits it again. The normal
ApplyChanges/PreparingDeviceSettings/GraphicsDevice.Reset path applies orientation to presentation
parameters and orders min/max preferred dimensions for portrait or landscape. OrientationChanged
is raised once after the applied backbuffer is coherent; resize events preserve explicit
LandscapeRight instead of re-deriving LandscapeLeft from width alone. Disabling returns to the
ordinary platform/preferred-size policy. Constructor opt-ins do not create/reset a device early.

## Cost and qualification

The disabled frame path performs a flag check for each feature and uses the existing snapshot;
it does not open sensors, allocate keyboard state, start threads or make per-key native calls.
Enabled processing scans the held-key snapshot, then publishes to the currently open streams.
Meaningful lifecycle/unit tests and real OPENGLES3/WEBGL2 clients are recorded in
`plans/plan_keyboard_device_emulation.md`. Physical Phone rotation and other renderer identities
are not qualification claims of this task.
