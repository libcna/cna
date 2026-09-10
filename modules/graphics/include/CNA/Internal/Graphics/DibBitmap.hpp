// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace CNA::Internal::Graphics
{
    /**
     * @brief Tells whether the bytes are a device-independent bitmap without its file header.
     *
     * A `.dib` is the body of a `.bmp` and nothing else: the fourteen-byte `BM` file header that
     * every bitmap *file* begins with is absent, because the format is what Windows puts on the
     * clipboard and into a resource, where the enclosing container already says how long the
     * payload is. The check is deliberately narrow -- at least a whole `BITMAPINFOHEADER`, not
     * already a `.bmp`, and a leading dword that is one of the three header sizes Windows
     * defines -- so it cannot claim a file some other decoder would have read.
     *
     * @param bytes The file's bytes.
     * @return true when the bytes look like a headerless DIB.
     */
    [[nodiscard]] bool IsDeviceIndependentBitmap(std::span<const std::uint8_t> bytes) noexcept;

    /**
     * @brief The same bitmap with the fourteen-byte file header a `.bmp` carries.
     *
     * The pixel-data offset is computed from the DIB's own declared header size, so a v4
     * (`BITMAPV4HEADER`) or v5 body reaches its pixels as correctly as a v3 one does.
     *
     * @param body A DIB body, as IsDeviceIndependentBitmap() accepts.
     * @return The complete bitmap file.
     * @throws std::invalid_argument when @p body is not a DIB body.
     */
    [[nodiscard]] std::vector<std::uint8_t> WithBitmapFileHeader(std::span<const std::uint8_t> body);

    /**
     * @brief The same bitmap with any filler between its header and its pixels removed.
     *
     * A `.bmp` says where its pixels start in the file header's `bfOffBits`, and a bitmap of more
     * than eight bits per pixel is allowed to put bytes in between: a colour table written for
     * 256-colour displays is the usual one, and SAMPLE-141's `riemerstexture.bmp` carries 864
     * bytes of exactly that. **The vendored `stb_image` decodes such a file from the wrong place**
     * -- measured directly, outside CNA, by `spikes/bmp-offset-spike`: the same image with four
     * bytes of filler decodes 38,984 of its 262,144 bytes differently from the same image with
     * none, and with 864 it is 55,886, which is byte for byte what CNA produced for
     * `riemerstexture.bmp` against a reference the genuine XNA pipeline built. A bitmap of eight
     * bits or fewer is left alone: its table is its palette, the decoder reads it correctly, and
     * removing it would destroy the image (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-221`).
     *
     * Anything that is not such a bitmap -- another format, a bitmap whose pixels already follow
     * its header, one whose fields do not agree -- is answered empty, and the caller then hands
     * the decoder the bytes it was given.
     *
     * @param bytes A complete bitmap file.
     * @return The bitmap with the filler removed, or empty when there is nothing to remove.
     */
    [[nodiscard]] std::vector<std::uint8_t> WithoutBitmapPixelGap(
        std::span<const std::uint8_t> bytes);
}
