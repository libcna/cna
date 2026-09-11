# WebGPU device-loss spike — `plans/plan_webgpu.md` `WEBGPU-180`

Three questions the device-loss recovery inventory could not answer from source, and what running
against the pin actually says. Measured on **wgpu-native v29.0.1.1** (`~/deps/wgpu-native-v29.0.1.1`),
AMD RADV under `Xvfb :131`, on 2026-09-06.

## Build and run

```sh
ccache g++ -std=c++23 -O1 -o spikes/webgpu-devicelost-spike/webgpu_devicelost_spike \
  spikes/webgpu-devicelost-spike/webgpu_devicelost_spike.cpp \
  -I$HOME/deps/wgpu-native-v29.0.1.1/include $(pkg-config --cflags sdl3) \
  -L$HOME/deps/wgpu-native-v29.0.1.1/lib -lwgpu_native $(pkg-config --libs sdl3) \
  -Wl,-rpath,$HOME/deps/wgpu-native-v29.0.1.1/lib

DISPLAY=:131 SDL_VIDEODRIVER=x11 ./spikes/webgpu-devicelost-spike/webgpu_devicelost_spike
```

The binary is gitignored; the source and this file are the artefacts.

## Q1 — object graph: does a device replace reach back into the platform layer?

**No. Recovery is renderer-internal.**

The spike configures the surface with device 1, acquires a texture, calls `wgpuDeviceDestroy` on it,
requests a **second device from the same `WGPUAdapter`**, re-configures the **same `WGPUSurface`**
with it, and acquires again:

```
    acquire on device 1: status=0x00000001 (success)
  destroying device 1 (wgpuDeviceDestroy)...
  device 2 created from the SAME adapter
    acquire on device 2, same surface: status=0x00000001 (success)
```

So `WGPUInstance`, `WGPUAdapter` and `WGPUSurface` all survive a device replace, and
`wgpuSurfaceConfigure` accepts a new device on an already-configured surface. `WEBGPU-182` can
rebuild the device and everything below it without touching `CNA::Platform` or the window.

## Q1a — a finding the question did not ask for, and the one that constrains the implementation most

**`wgpuSurfaceGetCurrentTexture` on a surface whose device is lost does not return an error status —
it aborts the process.**

```
thread '<unnamed>' panicked at src/lib.rs:605:5:
Error in wgpuSurfaceGetCurrentTexture: Validation Error

Caused by:
  Parent device is lost

note: run with `RUST_BACKTRACE=1` ...
fatal runtime error: failed to initiate panic, error 5, aborting
```

The panic cannot unwind across the C ABI, so there is no status to read and no exception to catch.
The probe that would have measured that status is therefore deliberately absent from the spike, and
its absence is the answer.

**What this costs `WEBGPU-182`:** "the device is lost" must be a gate the renderer checks **before**
the acquire, from its own `deviceLost_` flag, never a status the acquire hands back. `WEBGPU-181`'s
`CanBeginDrawEXT()` is not a convenience for the framework — on this pin it is the only thing
standing between a lost device and a process abort.

## Q2 — event classification: which reason, which mode, which thread?

**None of them: this pin delivers no device-lost callback at all for an application-initiated
destroy.** Four escalating attempts, each with the callback installed through
`WGPUDeviceDescriptor::deviceLostCallbackInfo`:

```
  wgpuDeviceDestroy + wgpuInstanceProcessEvents  -> no callback
  wgpuDeviceDestroy + wgpuDevicePoll             -> no callback
  wgpuDeviceRelease (last reference)             -> no callback
  AllowSpontaneous + wgpuDeviceDestroy           -> no callback
  TOTAL device-lost callbacks observed across every attempt: 0
```

The header pins the vocabulary — `WGPUDeviceLostReason_{Unknown,Destroyed,CallbackCancelled,
FailedCreation}`, with the callback documented to fire — but v29.0.1.1 does not raise it here under
either callback mode, whether the instance is pumped, the device is polled, or the last reference is
released.

**What this costs `WEBGPU-182`:** the debug `DebugSimulateContextLoss`/`DebugRestoreContext` path
cannot be built on the callback. The renderer has to set its own `deviceLost_` flag at the point it
destroys the device and raise `RendererDeviceEvent` itself. Because the callback never arrives, the
"which thread does it arrive on" half of the question is moot on native — and the driver-reported
path is left unverified by this spike, which is `WEBGPU-196`'s to settle against real hardware.

This is a statement about **wgpu-native v29.0.1.1**, not about WebGPU. A later pin may fix it; the
check is the four lines above.

## Q3 — browser semantics

**The browser path is the opposite of native: it *does* deliver the callback.** Answered from the
port's own source rather than from a browser run, which the row permits provided it says so — and
it says so: this one is **source-derived, not measured**.

`emdawnwebgpu` (`emsdk .../cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/src/`) bridges the real
`GPUDevice.lost` promise straight through to the C callback:

* `library_webgpu.js` — `emwgpuAdapterRequestDevice` registers
  `device.lost.then((info) => ... _emwgpuOnDeviceLostCompleted(deviceLostFutureId,
  emwgpuStringToInt_DeviceLostReason[info.reason], messagePtr))`;
* `emwgpuDeviceDestroy` is literally `device.destroy()`, and the WebGPU specification resolves
  `device.lost` with reason `"destroyed"` for exactly that call;
* `library_webgpu_enum_tables.js` maps only `{'undefined': 1, 'unknown': 1, 'destroyed': 2}`, i.e.
  `Unknown` and `Destroyed` — `CallbackCancelled` and `FailedCreation` are not reachable from a
  browser's own reason string.

So `DebugSimulateContextLoss()` on the web target can be a real `wgpuDeviceDestroy` and the callback
will arrive, while the same call on native must be paired with the renderer's own flag. **A browser
confirmation of this reading has not been run and belongs with `WEBGPU-196`.**

## Summary for `WEBGPU-181` / `WEBGPU-182`

| Question | Answer | Consequence |
|---|---|---|
| Q1 object graph | instance, adapter and surface all survive; `wgpuSurfaceConfigure` takes a new device | recovery is renderer-internal, no platform-layer change |
| Q1a acquire after loss | **aborts the process** (Rust panic across the C ABI) | `CanBeginDrawEXT()` must gate the acquire; it is a safety mechanism, not a convenience |
| Q2 native callback | never fires, under any mode/pump/poll/release | the renderer raises `RendererDeviceEvent` itself; the driver path needs `WEBGPU-196` |
| Q3 browser callback | fires, via `GPUDevice.lost`, reasons limited to `Unknown`/`Destroyed` | the two targets need different wiring, and the web one is the more capable — source-derived, unconfirmed in a browser |
