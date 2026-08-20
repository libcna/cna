#pragma once

// plans/plan_dx9.md Phase D9-8 (D9-82): the actual SetVertexShaderConstantF/SetPixelShaderConstantF
// upload step, register-table-driven via D9-72's D3D9ShaderRegisters.hpp -- looks a named HLSL
// constant (e.g. "WorldViewProj") up in a specific compiled shader's own real register table and
// uploads the matching float data to that exact register.
//
// A NON-empty table missing the requested name is a real transcription bug (D3D9ShaderRegisters.hpp
// enumerates every constant a given entry point actually reads) and throws. An EMPTY table (nullptr/
// count 0 -- "no named constants", D3D9ShaderRegisters.hpp's own convention for e.g.
// BasicEffect_PSBasicNoFog) is a legitimate, silent no-op: some shader variants genuinely have
// nothing to upload for a given stage.

#include "CNA/Internal/Renderers/DirectX9/shaders/D3D9ShaderRegisters.hpp"

#include <d3d9.h>

namespace CNA::Internal::Renderers::DirectX9
{
    /// Uploads `data` (`registerCount * 4` floats) to the real vertex-shader constant register
    /// slot named `name` within `table` (`count` entries). No-op if `table`/`count` is empty.
    /// Throws `std::runtime_error` if `table` is non-empty but does not contain `name`.
    void UploadVertexShaderConstantEXT(IDirect3DDevice9* device,
                                       const Shaders::D3D9ShaderConstantSlot* table, int count,
                                       const char* name, const float* data);

    /// Pixel-shader counterpart of `UploadVertexShaderConstantEXT` -- `SetPixelShaderConstantF`.
    void UploadPixelShaderConstantEXT(IDirect3DDevice9* device,
                                      const Shaders::D3D9ShaderConstantSlot* table, int count,
                                      const char* name, const float* data);

    /// D9-82b: "soft" counterpart of `UploadVertexShaderConstantEXT` -- returns `true` if
    /// uploaded, `false` (never throws) if `name` is not present in `table` (empty OR simply not
    /// declared by this specific compiled variant). Lets a generic effect-dispatch path attempt
    /// every constant an effect COULD have and let each variant's own real (D9-72) register table
    /// decide which ones actually apply, instead of hand-tracking a separate "which constants does
    /// variant X need" list that could drift out of sync with the compiler-verified ground truth.
    bool TryUploadVertexShaderConstantEXT(IDirect3DDevice9* device,
                                          const Shaders::D3D9ShaderConstantSlot* table, int count,
                                          const char* name, const float* data);

    /// Pixel-shader counterpart of `TryUploadVertexShaderConstantEXT`.
    bool TryUploadPixelShaderConstantEXT(IDirect3DDevice9* device,
                                         const Shaders::D3D9ShaderConstantSlot* table, int count,
                                         const char* name, const float* data);
}
