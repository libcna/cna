# CNA C API Renderers and Capabilities

## CNA owns renderer selection

The C API uses the renderer selected by CNA's build configuration. It does not expose a renderer
pointer or create a separate runtime renderer-selection path. The C API reports the active renderer
identity, build/platform capabilities and supported feature flags using fixed-width POD output.

An operation unavailable on the active renderer returns `CNA_RESULT_NOT_SUPPORTED` and leaves any
documented output parameter unchanged. It must not silently substitute different graphics behavior.

## Capability queries

The initial core API will expose a versioned `CNA_RendererInfo` and a capability bit/query API.
Names are returned through the UTF-8 query/copy protocol rather than raw native string pointers.
Capability reporting covers the actual selected build, including meaningful limits such as 2D-only
rendering, depth/stencil, effects, render targets, texture formats, input devices and platform
services. It does not hard-code renderer facts in C headers that can drift from CNA.

## Test policy

HEADLESS is the deterministic lifecycle/state control. It can prove C callback order, handle
lifetime, error behavior and native command/state observations. It cannot by itself prove visual
pixel correctness. Any C API rendering feature that claims visual output requires an appropriate
real renderer test in addition to its HEADLESS test. Renderer-specific unavailability is recorded
in the public C API coverage matrix and feature documentation.
