// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Graphics
{
    /**
     * @brief A DDS cube map decoded to plain RGBA8 bytes on the CPU
     *        (plans/plan_cnb.md `CNBF-113`).
     *
     * Faces are indexed in the on-disk DDS order, which is also `CubeMapFace`'s order:
     * `+X, -X, +Y, -Y, +Z, -Z`. Within a face the levels are ordered mip 0 first, each half the
     * previous one's dimensions and never below 1. That is face-major then mip-minor, which is
     * exactly the order `CnbTextureRepresentation::levels` uses, so a consumer can move the data
     * across without reindexing it.
     */
    struct DecodedDdsCube
    {
        /** @brief Width of face level 0, in texels. Equal to the height: cube faces are square. */
        int width = 0;

        /** @brief Number of mip levels present for every face; at least 1. */
        int mipCount = 0;

        /** @brief `faces[face][mip]` holds that level's RGBA8 bytes, `w * h * 4` of them. */
        std::array<std::vector<std::vector<std::uint8_t>>, 6> faces;
    };

    /**
     * @brief Decodes a DXT1/DXT3/DXT5 cube-map DDS image into RGBA8, entirely on the CPU
     *        (plans/plan_cnb.md `CNBF-113`).
     *
     * **Needs no `GraphicsDevice`, no renderer, no window and no GPU readback.** That is the whole
     * reason it exists as a separate component: this logic used to sit inside
     * `TextureCube::DDSFromStreamEXT`, where it was reachable only by code that could create a GPU
     * texture — so a headless content compiler could not use it and CNB had no way to produce a
     * cube map. The parsing and decompression are unchanged; only their location is.
     *
     * The supported scope is deliberately **not** widened by the move: DXT1, DXT3 and DXT5
     * cube maps with square faces, and nothing else. DX10 headers, uncompressed and HDR DDS
     * variants are refused exactly as before.
     *
     * @param data             The complete DDS file bytes.
     * @param size             Length of @p data.
     * @param diagnosticPrefix Text placed at the front of every message, so a caller's
     *                         diagnostics name the caller. Pass the API the user actually called.
     * @return The decoded cube.
     * @throws System::NotSupportedException if the bytes are not a DDS image, the header is
     *         malformed, or the pixel format is outside the supported scope.
     * @throws System::FormatException if the image is not a cube map, its faces are not square, or
     *         the file is truncated part-way through a face or mip level.
     */
    [[nodiscard]] DecodedDdsCube DecodeDdsCube(const std::uint8_t* data, std::size_t size,
                                                const std::string& diagnosticPrefix);
}
