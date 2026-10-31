# C graphics extensions

`CNA/C/graphics_ext.h` declares the retained ASCII, CRT, colour-depth and DebugDraw routes.
`cna_graphics_ext_is_available` reports whether `CNA_CNAEXT` was enabled. The effect routes
return `CNA_RESULT_NOT_SUPPORTED` when it is off. CRT and Depth use ordinary `CNA_EffectHandle`
values; ASCII and DebugDraw have their own owned handles.

The ASCII draw route ignores source alpha: source RGB selects each glyph and its colors, and the
whole destination rectangle is painted opaque. It is a full-area stylization pass rather than
alpha-preserving compositing. The C++ effect contract and usage examples are documented in
[`docs/ascii-post-process-effect.md`](../ascii-post-process-effect.md).

The header also carries the core `ImageBasedLightEXT` value type and its routes, which are core
PBR capabilities independent of the removed engine layer. The former `engine_layer.h`, PBR material
value, render-pipeline settings and engine resource routes, and later the indirect-draw argument
structures of the retired modern GPU layer, have been removed from the alpha-stage ABI.
