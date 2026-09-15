// SPDX-License-Identifier: MS-PL
#pragma once

#include "WaylandProtocols.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace CNA::Platform::Wayland {

    /**
     * @brief One `wl_shm` buffer: an anonymous shared-memory file, mapped, in a pool of its own,
     * with the compositor's `release` tracked (plans/plan_wayland.md D-25).
     *
     * A client may not write into a buffer the compositor still reads -- between the commit that
     * attached it and its `wl_buffer.release` -- so IsBusy says which buffers may be drawn into.
     * The file is created with `memfd_create` (sealed against shrinking, so the compositor can
     * never be made to fault on a truncated mapping), falling back to an unlinked `shm_open` name
     * where memfd is not available.
     */
    class WaylandShmBuffer
    {
    public:
        /**
         * @brief Creates a buffer.
         * @param shm The `wl_shm`.
         * @param width Width in pixels.
         * @param height Height in pixels.
         * @param format `WL_SHM_FORMAT_XRGB8888` or `WL_SHM_FORMAT_ARGB8888`.
         * @return The buffer, or null when shared memory could not be had.
         */
        [[nodiscard]] static std::unique_ptr<WaylandShmBuffer> Create(wl_shm* shm, int width, int height,
                                                                      std::uint32_t format);

        /** @brief Destroys the `wl_buffer` and its pool, and unmaps the memory. */
        ~WaylandShmBuffer();

        WaylandShmBuffer(const WaylandShmBuffer&) = delete;
        WaylandShmBuffer& operator=(const WaylandShmBuffer&) = delete;

        /** @brief Gets the buffer. @return The `wl_buffer`. */
        [[nodiscard]] wl_buffer* GetBuffer() const { return buffer_; }
        /** @brief Gets the mapped pixels. @return The first row. */
        [[nodiscard]] std::uint8_t* GetPixels() const { return static_cast<std::uint8_t*>(data_); }
        /** @brief Gets the row stride. @return Bytes per row. */
        [[nodiscard]] int GetStride() const { return stride_; }
        /** @brief Gets the width. @return Pixels. */
        [[nodiscard]] int GetWidth() const { return width_; }
        /** @brief Gets the height. @return Pixels. */
        [[nodiscard]] int GetHeight() const { return height_; }
        /** @brief Gets whether the compositor may still be reading it. @return True between attach and release. */
        [[nodiscard]] bool IsBusy() const { return busy_; }
        /** @brief Records that the buffer was attached and committed; busy until released. */
        void MarkAttached() { busy_ = true; }

    private:
        WaylandShmBuffer() = default;

        static const wl_buffer_listener kListener;

        wl_buffer* buffer_ = nullptr;
        void* data_ = nullptr;
        std::size_t size_ = 0;
        int stride_ = 0;
        int width_ = 0;
        int height_ = 0;
        bool busy_ = false;
    };

    /**
     * @brief Creates an anonymous shared-memory file of a given size.
     * @param size The size in bytes.
     * @return The descriptor (close-on-exec), or -1.
     */
    [[nodiscard]] int CreateAnonymousFile(std::size_t size);

} // namespace CNA::Platform::Wayland
