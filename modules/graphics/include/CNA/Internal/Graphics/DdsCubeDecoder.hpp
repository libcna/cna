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
        /**
         * @brief Width of face level 0, in texels. Equal to the height: cube faces are square.
         *
         * At most 16384, so that a level's RGBA length (`width * height * 4`) is representable.
         */
        int width = 0;

        /**
         * @brief Number of mip levels present for every face; at least 1.
         *
         * Never more than the chain @ref width physically allows, so at most 15.
         */
        int mipCount = 0;

        /** @brief `faces[face][mip]` holds that level's RGBA8 bytes, `w * h * 4` of them. */
        std::array<std::vector<std::vector<std::uint8_t>>, 6> faces;
    };

    /**
     * @brief Default ceiling on the **total** RGBA8 bytes a decoded cube map may occupy
     *        (plans/plan_cnb.md `CNBF-122`).
     *
     * The dimension ceiling alone does not bound this. A cube map is six faces and a face is a
     * whole mip chain, so at the 16384-texel maximum the decoded result is
     * `6 * 4 * sum(16384^2 / 4^k)` = about **8.6 GiB** of retained `std::vector` -- from a DDS file
     * of roughly 1.4 GiB, or from a truncated one that gets that far before the read bound catches
     * it. Every one of those allocations is individually representable, which is exactly why the
     * per-level ceiling cannot see the problem.
     *
     * 2 GiB is the largest complete cube this admits: a fully mipped 8192-texel cube needs
     * 2 147 483 640 bytes, eight short of this value, and every 4096-texel cube is a quarter of
     * that. So nothing a GPU CNA targets would accept as a cube map is refused, and the 16384 case
     * -- which no consumer of this decoder could upload anyway -- is.
     *
     * A caller that genuinely wants more passes its own budget; a caller that wants less (a test,
     * or a build machine with a known memory ceiling) does the same.
     */
    inline constexpr std::uint64_t DefaultDdsCubeDecodedByteBudget = 2ull * 1024ull * 1024ull * 1024ull;

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
     * **Every header number is bounded before it is used** (plans/plan_cnb.md `CNBF-116`). The
     * dimension ceiling is 16384 texels, which is what `DecodedDdsCube::width` and the RGBA output
     * length can represent rather than a preference; the mip count may not exceed the chain the
     * face's own size allows; and a cube map must declare all six `DDSCAPS2_CUBEMAP_*` face bits,
     * because six faces are read whether or not they were declared. Each of those is a refusal,
     * not a repair: a clamped header would produce a plausible wrong image.
     *
     * **The decoded output is budgeted as well as the input** (plans/plan_cnb.md `CNBF-122`). The
     * aggregate RGBA8 length of all six faces and every mip level is computed in `std::uint64_t`
     * from the validated header alone -- before a single output vector is allocated and before any
     * block is decompressed -- and refused if it exceeds @p maxDecodedBytes. Without it a
     * well-formed 16384-texel cube map header asks for roughly 8.6 GiB of retained memory, which
     * on most machines is an OOM kill rather than an exception.
     *
     * @param data             The complete DDS file bytes.
     * @param size             Length of @p data.
     * @param diagnosticPrefix Text placed at the front of every message, so a caller's
     *                         diagnostics name the caller. Pass the API the user actually called.
     * @param maxDecodedBytes  Ceiling on the total decoded RGBA8 bytes; see
     *                         DefaultDdsCubeDecodedByteBudget.
     * @return The decoded cube.
     * @throws System::NotSupportedException if the bytes are not a DDS image, the header is
     *         malformed, the pixel format is outside the supported scope, or `caps2` carries a bit
     *         outside the cube-map set.
     * @throws System::FormatException if the image is not a cube map, does not declare all six
     *         faces, its faces are not square, a dimension is zero or above what the decoded
     *         result can represent, the mip count exceeds the face's physical chain, the decoded
     *         result would exceed @p maxDecodedBytes, or the file is truncated part-way through a
     *         face or mip level.
     */
    [[nodiscard]] DecodedDdsCube DecodeDdsCube(
        const std::uint8_t* data, std::size_t size, const std::string& diagnosticPrefix,
        std::uint64_t maxDecodedBytes = DefaultDdsCubeDecodedByteBudget);
}
