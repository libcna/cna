#pragma once

#include <string_view>

namespace CNA::Internal::Renderers::Skia
{
    /**
     * SKIA-144/145/146: the fixed `cnaSampleCubeEXT` SkSL preamble docs/skia-cube-volume-sampling-
     * contract.md specifies -- six named face children (`cnaCubeFace0`=+X .. `cnaCubeFace5`=-Z,
     * matching `ITextureCubeRenderer`'s existing face order), the `cnaCubeFaceSizeEXT` pixel-space
     * scale uniform, and the D3D dominant-axis face/UV table with a deterministic corner tie-break
     * (X before Y before Z, positive before negative). Empirically confirmed byte-for-byte correct
     * against real rendered pixels by `Skia_CubeSampling_Spike` (SKIA-145) with no correction
     * needed. SKIA-149 prepends this exact text to an author's compiled fragment source; every
     * spike/test that exercises cube sampling must use this single shared constant rather than a
     * per-file copy, so a future formula change cannot silently diverge between what is tested and
     * what SKIA-149 ships.
     *
     * Callers append their own `main(float2 sourcePixel)` (or further helper functions) after this
     * text and compile the concatenation with `SkRuntimeEffect::MakeForShader`.
     */
    inline constexpr std::string_view kCnaSampleCubePreambleEXT = R"(
        uniform shader cnaCubeFace0;
        uniform shader cnaCubeFace1;
        uniform shader cnaCubeFace2;
        uniform shader cnaCubeFace3;
        uniform shader cnaCubeFace4;
        uniform shader cnaCubeFace5;
        uniform float cnaCubeFaceSizeEXT;

        half4 cnaSampleCubeEXT(float3 dir) {
            float ax = abs(dir.x);
            float ay = abs(dir.y);
            float az = abs(dir.z);
            float ma;
            float2 uv;
            int face;
            if (ax >= ay && ax >= az) {
                if (dir.x >= 0.0) { ma = dir.x; uv = float2(-dir.z, -dir.y); face = 0; }
                else { ma = -dir.x; uv = float2(dir.z, -dir.y); face = 1; }
            } else if (ay >= ax && ay >= az) {
                if (dir.y >= 0.0) { ma = dir.y; uv = float2(dir.x, dir.z); face = 2; }
                else { ma = -dir.y; uv = float2(dir.x, -dir.z); face = 3; }
            } else {
                if (dir.z >= 0.0) { ma = dir.z; uv = float2(dir.x, -dir.y); face = 4; }
                else { ma = -dir.z; uv = float2(-dir.x, -dir.y); face = 5; }
            }
            float2 st = clamp(0.5 * (uv / ma + 1.0), 0.0, 1.0);
            float2 px = st * cnaCubeFaceSizeEXT;
            if (face == 0) return cnaCubeFace0.eval(px);
            if (face == 1) return cnaCubeFace1.eval(px);
            if (face == 2) return cnaCubeFace2.eval(px);
            if (face == 3) return cnaCubeFace3.eval(px);
            if (face == 4) return cnaCubeFace4.eval(px);
            return cnaCubeFace5.eval(px);
        }
    )";
} // namespace CNA::Internal::Renderers::Skia
