// SPDX-License-Identifier: MS-PL

#include "WaylandShm.hpp"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace CNA::Platform::Wayland {

    int CreateAnonymousFile(const std::size_t size)
    {
        int descriptor = -1;
#if defined(MFD_CLOEXEC)
        descriptor = memfd_create("cna-wayland-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (descriptor >= 0)
        {
            // The compositor maps this file too: sealed against shrinking, a truncate by anyone
            // cannot make its mapping fault.
            (void) fcntl(descriptor, F_ADD_SEALS, F_SEAL_SHRINK);
        }
#endif
        if (descriptor < 0)
        {
            // A name nobody else will pick, unlinked at once: the object lives only as long as its
            // descriptors do.
            static std::atomic<unsigned> counter{0};
            for (int attempt = 0; attempt < 100 && descriptor < 0; ++attempt)
            {
                const std::string name = "/cna-wayland-" + std::to_string(::getpid()) + "-" +
                                         std::to_string(counter.fetch_add(1));
                descriptor = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
                if (descriptor >= 0)
                {
                    shm_unlink(name.c_str());
                }
                else if (errno != EEXIST)
                {
                    break;
                }
            }
        }
        if (descriptor < 0)
        {
            return -1;
        }
        int result = 0;
        do
        {
            result = posix_fallocate(descriptor, 0, static_cast<off_t>(size));
        } while (result == EINTR);
        // posix_fallocate is refused by some filesystems (EINVAL, EOPNOTSUPP); ftruncate still
        // gives the file its size, without the guarantee that the pages exist.
        if (result != 0 && ftruncate(descriptor, static_cast<off_t>(size)) != 0)
        {
            ::close(descriptor);
            return -1;
        }
        return descriptor;
    }

    const wl_buffer_listener WaylandShmBuffer::kListener = {
        .release = [](void* data, wl_buffer*) { static_cast<WaylandShmBuffer*>(data)->busy_ = false; },
    };

    std::unique_ptr<WaylandShmBuffer> WaylandShmBuffer::Create(wl_shm* shm, const int width, const int height,
                                                               const std::uint32_t format)
    {
        if (shm == nullptr || width <= 0 || height <= 0 || width > INT_MAX / 4)
        {
            return nullptr;
        }
        const int stride = width * 4;
        if (height > INT_MAX / stride)
        {
            return nullptr;
        }
        const std::size_t size = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
        const int descriptor = CreateAnonymousFile(size);
        if (descriptor < 0)
        {
            return nullptr;
        }
        void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        if (data == MAP_FAILED)
        {
            ::close(descriptor);
            return nullptr;
        }
        wl_shm_pool* pool = wl_shm_create_pool(shm, descriptor, static_cast<std::int32_t>(size));
        // The pool holds its own reference to the file; ours is not needed after this.
        ::close(descriptor);
        std::unique_ptr<WaylandShmBuffer> buffer(new WaylandShmBuffer());
        buffer->buffer_ = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
        // A buffer keeps its pool's memory alive; the pool object itself is no longer needed.
        wl_shm_pool_destroy(pool);
        buffer->data_ = data;
        buffer->size_ = size;
        buffer->stride_ = stride;
        buffer->width_ = width;
        buffer->height_ = height;
        wl_buffer_add_listener(buffer->buffer_, &kListener, buffer.get());
        return buffer;
    }

    WaylandShmBuffer::~WaylandShmBuffer()
    {
        if (buffer_ != nullptr)
        {
            wl_buffer_destroy(buffer_);
            buffer_ = nullptr;
        }
        if (data_ != nullptr)
        {
            munmap(data_, size_);
            data_ = nullptr;
        }
    }

} // namespace CNA::Platform::Wayland
