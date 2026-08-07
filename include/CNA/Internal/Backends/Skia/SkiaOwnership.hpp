#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace CNA::Internal::Backends::Skia
{
    /**
     * Stable owner-thread identity shared with backend-local resource adapters.
     *
     * Skia's CPU raster canvas and SDL presenter are both single-owner objects. A weak reference
     * to this token additionally lets a SpriteBatch reject use after graphics-backend destruction
     * before touching any of its raw pointers into backend state.
     */
    class SkiaOwnership final
    {
    public:
        SkiaOwnership() : ownerThread_(std::this_thread::get_id()) {}

        void AssertOwnerThread(std::string_view operation) const
        {
            if (std::this_thread::get_id() != ownerThread_)
            {
                throw std::runtime_error(
                    "Skia ownership violation: " + std::string(operation)
                    + " must run on the graphics backend owner thread.");
            }
        }

    private:
        std::thread::id ownerThread_;
    };
} // namespace CNA::Internal::Backends::Skia
