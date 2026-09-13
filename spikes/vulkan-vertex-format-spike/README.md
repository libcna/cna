# Vulkan vertex-format probe

`plans/plan_vulkan.md` `VULKAN-151`.

## The question

XNA lets a content processor spell `BLENDINDICES` as `Byte4` **or** as `Vector4`
(`plans/plan_fx.md` FX-127; CustomModelAnimation's own `SkinnedModelProcessor` writes `Vector4`).
CNA's Vulkan skinned shaders declared `uvec4 aBoneIndices`, and a Vulkan shader input cannot take
both an integer and a float attribute — so only the integer spelling bound, and the float one was
refused by name.

One shader can serve both if the input becomes `vec4` and the `Byte4` element is bound as
`VK_FORMAT_R8G8B8A8_USCALED`: integer components converted to float **without** normalisation.
That is exactly what `glVertexAttribPointer(..., GL_UNSIGNED_BYTE, GL_FALSE, ...)` does, which is
why EasyGL accepts both spellings without a second shader.

The catch: `_USCALED` formats are **not** in Vulkan's mandatory vertex-buffer list
(`VkFormatProperties::bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT`), so a conforming
driver may refuse them. This probe asks, so the design decision is measured rather than assumed.

## What it does

Creates an instance, enumerates physical devices, and prints
`vkGetPhysicalDeviceFormatProperties` for six formats. No surface, no window, no logical device —
which is also why it reaches **RADV** here rather than only llvmpipe (only presentation needs
DRI3; see `spikes/vulkan-depth-bias-spike/README.md` for the same finding).

## Build and run

```bash
ccache g++ -std=c++17 -O1 vertex_format_probe.cpp -lvulkan -o vertex_format_probe
./vertex_format_probe
```

## What it proved (2026-09-05)

```
=== AMD Radeon 780M (RADV PHOENIX) ===
  R8G8B8A8_UINT        (Byte4 today, feeds uvec4)          vertexBuffer=YES
  R8G8B8A8_USCALED     (Byte4 -> vec4, unnormalised)       vertexBuffer=YES
  R8G8B8A8_UNORM       (Color)                             vertexBuffer=YES
  R32G32B32A32_SFLOAT  (Vector4)                           vertexBuffer=YES
  R16G16_USCALED       (Short2 -> vec2)                    vertexBuffer=YES
  R16G16B16A16_USCALED (Short4 -> vec4)                    vertexBuffer=YES
=== llvmpipe (LLVM 19.1.7, 256 bits) ===
  ... identical, all YES ...
```

Both drivers available on this machine can bind `_USCALED` vertex attributes, so `VULKAN-151` took
the one-shader route. The renderer still **queries** the format on the device it selects
(`uscaledVertexFormatSupported_`) and refuses by name where it is absent, naming the `Vector4`
spelling as the way round — the probe justifies the design, it does not license skipping the check.
