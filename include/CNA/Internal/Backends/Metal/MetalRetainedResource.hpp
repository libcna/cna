// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Backends::Metal
{
    /**
     * @brief Owns one manually retained Metal resource through caller-supplied MRR callbacks.
     *
     * @tparam Resource Nullable Objective-C-compatible resource handle type.
     */
    template<typename Resource>
    class MetalRetainedResource final
    {
    public:
        /** @brief Callback that retains and returns a resource handle. */
        using RetainFunction = Resource (*)(Resource);
        /** @brief Callback that releases a resource handle. */
        using ReleaseFunction = void (*)(Resource);

        /**
         * @brief Creates an empty owner using non-null retain and release callbacks.
         *
         * @param retainFunction Callback invoked before adopting a non-null resource.
         * @param releaseFunction Callback invoked exactly once for each adopted resource.
         */
        MetalRetainedResource(RetainFunction retainFunction,
                              ReleaseFunction releaseFunction) noexcept
            : retainFunction_(retainFunction), releaseFunction_(releaseFunction)
        {
        }

        /** @brief Releases the currently owned resource, if any. */
        ~MetalRetainedResource()
        {
            Reset();
        }

        /**
         * @brief Retained Metal resources cannot be copied.
         *
         * @param other Owner that would otherwise be copied.
         */
        MetalRetainedResource(const MetalRetainedResource& other) = delete;

        /**
         * @brief Retained Metal resources cannot be copy-assigned.
         *
         * @param other Owner that would otherwise be assigned.
         * @return This owner; the operation is deleted and therefore never returns.
         */
        MetalRetainedResource& operator=(const MetalRetainedResource& other) = delete;

        /**
         * @brief Retains a replacement before releasing the currently owned resource.
         *
         * Passing the already-owned handle is a no-op. Passing a null handle releases the current
         * resource and leaves the owner empty.
         *
         * @param replacement Borrowed resource handle to retain, or null to clear the owner.
         */
        void Reset(Resource replacement = Resource{})
        {
            if (replacement == resource_)
                return;

            const Resource retained = replacement ? retainFunction_(replacement) : Resource{};
            const Resource previous = resource_;
            resource_ = retained;
            if (previous)
                releaseFunction_(previous);
        }

        /**
         * @brief Returns the retained resource without transferring ownership.
         *
         * @return The retained resource handle, or null when empty.
         */
        [[nodiscard]] Resource Get() const noexcept
        {
            return resource_;
        }

        /**
         * @brief Reports whether this owner currently retains a resource.
         *
         * @return True when a non-null resource is owned.
         */
        [[nodiscard]] bool HasValue() const noexcept
        {
            return resource_ != Resource{};
        }

    private:
        RetainFunction retainFunction_;
        ReleaseFunction releaseFunction_;
        Resource resource_{};
    };
}
