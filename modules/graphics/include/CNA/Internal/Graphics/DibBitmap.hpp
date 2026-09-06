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
}
