#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /**
     * SKIA-144/147: the padded grid-atlas layout docs/skia-cube-volume-sampling-contract.md
     * specifies for packing a `Texture3D`/`RenderTargetCube`-style linear voxel buffer's depth
     * slices into one 2D image. `cols`/`rows` favor a roughly square atlas (minimizing the larger
     * of the two axes for a given depth, so the existing 16384-axis ceiling stays reachable for
     * realistic volumes). Every tile carries a 1-texel replicated border on every edge
     * (`tileWidth = width + 2`, `tileHeight = height + 2`) so a bilinear sample anywhere inside a
     * tile's own `(width, height)` interior can never read a neighboring tile's texels -- the
     * exact "atlas padding cannot bleed between slices" requirement.
     */
    struct VolumeAtlasLayoutEXT final
    {
        int cols = 0;
        int rows = 0;
        int tileWidth = 0;
        int tileHeight = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
    };

    /** Computes the layout for a `depth`-slice volume level without allocating or copying. */
    [[nodiscard]] VolumeAtlasLayoutEXT ComputeVolumeAtlasLayoutEXT(
        int width, int height, int depth) noexcept;

    /**
     * Packs `depth` contiguous RGBA8 slices (`width * height * 4` bytes each, slice-major,
     * top-row-first within each slice -- exactly `ITexture3DBackend`'s existing voxel layout) into
     * one padded grid atlas image per `ComputeVolumeAtlasLayoutEXT`'s layout. Returns an empty
     * vector if `width`/`height`/`depth` are non-positive or `voxels` is too short.
     */
    [[nodiscard]] std::vector<std::uint8_t> PackVolumeAtlasEXT(
        int width, int height, int depth, const std::uint8_t* voxels, std::size_t voxelsByteCount);

    /**
     * SKIA-144/147/148: the fixed `cnaSampleVolumeEXT` SkSL preamble. Its signature has stayed
     * fixed since SKIA-147 (Point-only sampling); SKIA-148 replaces the body with the documented
     * half-texel-centered trilinear blend and independent per-axis Clamp/Wrap/Mirror address
     * modes, so nothing that already calls `cnaSampleVolumeEXT` needs to change with it.
     *
     * `cnaApplyAddressEXT`'s three modes (0=Clamp, 1=Wrap, 2=Mirror) apply identically to `u`, `v`,
     * and the raw (pre-slice-index) `w` coordinate. The w-axis blend uses `sampleF = w * depth -
     * 0.5` so `w=0`/`w=1` land exactly on the first/last slice's own centre; critically, `s1` and
     * the blend weight `wf` are derived from the *unclamped* `floor(sampleF)`, with clamping to
     * `[0, depth - 1]` applied only to the two final slice indices -- clamping `s0` before deriving
     * `s1 = s0 + 1` (as this project's own earlier design-doc wording literally said) double-counts
     * the boundary clamp and incorrectly blends slice 0 with slice 1 at `w=0` instead of returning
     * slice 0 alone; this preamble is the corrected version, and
     * docs/skia-cube-volume-sampling-contract.md's wording is corrected to match.
     *
     * Declares `cnaVolumeAtlas0` (the packed atlas image), `cnaVolumeAtlasMeta0` = `(cols, rows,
     * depth, 0)`, `cnaVolumeAtlasMeta1` = `(tileWidth, tileHeight, 0, 0)`, and
     * `cnaVolumeAddressModesEXT` = `(addressU, addressV, addressW)` -- all reserved, written
     * automatically by `SetTexture(1, Texture3D)` once SKIA-149 wires the public path, not
     * settable by an effect author (matching the existing `cnaTint` reserved-uniform precedent).
     */
    inline constexpr std::string_view kCnaSampleVolumePreambleEXT = R"(
        uniform shader cnaVolumeAtlas0;
        uniform float4 cnaVolumeAtlasMeta0;
        uniform float4 cnaVolumeAtlasMeta1;
        uniform float3 cnaVolumeAddressModesEXT;

        float cnaApplyAddressEXT(float x, float mode) {
            if (mode < 0.5) return clamp(x, 0.0, 1.0);
            if (mode < 1.5) return fract(x);
            return 1.0 - abs(fract(x * 0.5) * 2.0 - 1.0);
        }

        half4 cnaSampleVolumeAtEXT(float u, float v, float sliceIndex) {
            float cols = cnaVolumeAtlasMeta0.x;
            float tileW = cnaVolumeAtlasMeta1.x;
            float tileH = cnaVolumeAtlasMeta1.y;
            float innerW = tileW - 2.0;
            float innerH = tileH - 2.0;
            float col = sliceIndex - cols * floor(sliceIndex / cols);
            float row = floor(sliceIndex / cols);
            float atlasX = col * tileW + 1.0 + u * innerW;
            float atlasY = row * tileH + 1.0 + v * innerH;
            return cnaVolumeAtlas0.eval(float2(atlasX, atlasY));
        }

        half4 cnaSampleVolumeEXT(float3 uvw) {
            float depth = cnaVolumeAtlasMeta0.z;
            float u = cnaApplyAddressEXT(uvw.x, cnaVolumeAddressModesEXT.x);
            float v = cnaApplyAddressEXT(uvw.y, cnaVolumeAddressModesEXT.y);
            float w = cnaApplyAddressEXT(uvw.z, cnaVolumeAddressModesEXT.z);

            float sampleF = w * depth - 0.5;
            float flooredS0 = floor(sampleF);
            float s0 = clamp(flooredS0, 0.0, depth - 1.0);
            float s1 = clamp(flooredS0 + 1.0, 0.0, depth - 1.0);
            float wf = clamp(sampleF - flooredS0, 0.0, 1.0);

            half4 c0 = cnaSampleVolumeAtEXT(u, v, s0);
            half4 c1 = cnaSampleVolumeAtEXT(u, v, s1);
            return mix(c0, c1, wf);
        }
    )";
} // namespace CNA::Internal::Backends::Skia
