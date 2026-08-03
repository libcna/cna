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
     * SKIA-144/147: the fixed `cnaSampleVolumeEXT` SkSL preamble. SKIA-147 implements Point
     * (nearest-slice, nearest-texel) sampling only -- `s = floor(clamp(uvw.z, 0, 1) * depth)`,
     * clamped to `[0, depth - 1]`, with `u`/`v` clamped (not yet Wrap/Mirror) before an ordinary
     * nearest 2D sample inside the selected tile's own interior. SKIA-148 replaces the slice
     * selection and per-axis clamp with the documented half-texel-centered trilinear blend and
     * independent Clamp/Wrap/Mirror address modes; this preamble's `cnaSampleVolumeEXT` signature
     * does not change, only its body, so nothing that already calls it needs to change with it.
     *
     * Declares `cnaVolumeAtlas0` (the packed atlas image), `cnaVolumeAtlasMeta0` = `(cols, rows,
     * depth, 0)` and `cnaVolumeAtlasMeta1` = `(tileWidth, tileHeight, 0, 0)` -- both reserved,
     * written automatically by `SetTexture(1, Texture3D)` once SKIA-149 wires the public path, not
     * settable by an effect author (matching the existing `cnaTint` reserved-uniform precedent).
     */
    inline constexpr std::string_view kCnaSampleVolumePreambleEXT = R"(
        uniform shader cnaVolumeAtlas0;
        uniform float4 cnaVolumeAtlasMeta0;
        uniform float4 cnaVolumeAtlasMeta1;

        half4 cnaSampleVolumeEXT(float3 uvw) {
            float cols = cnaVolumeAtlasMeta0.x;
            float depth = cnaVolumeAtlasMeta0.z;
            float tileW = cnaVolumeAtlasMeta1.x;
            float tileH = cnaVolumeAtlasMeta1.y;
            float innerW = tileW - 2.0;
            float innerH = tileH - 2.0;

            float u = clamp(uvw.x, 0.0, 1.0);
            float v = clamp(uvw.y, 0.0, 1.0);
            float s = clamp(floor(clamp(uvw.z, 0.0, 1.0) * depth), 0.0, depth - 1.0);

            float col = s - cols * floor(s / cols);
            float row = floor(s / cols);
            float atlasX = col * tileW + 1.0 + u * innerW;
            float atlasY = row * tileH + 1.0 + v * innerH;
            return cnaVolumeAtlas0.eval(float2(atlasX, atlasY));
        }
    )";
} // namespace CNA::Internal::Backends::Skia
