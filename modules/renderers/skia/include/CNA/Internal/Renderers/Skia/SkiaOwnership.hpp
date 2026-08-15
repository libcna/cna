#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace CNA::Internal::Renderers::Skia
{
    /**
     * Stable owner-thread identity shared with renderer-local resource adapters.
     *
     * Skia's CPU raster canvas and platform presenter are both single-owner objects. A weak reference
     * to this token additionally lets a SpriteBatch reject use after graphics-renderer destruction
     * before touching any of its raw pointers into renderer state.
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
                    + " must run on the graphics renderer owner thread.");
            }
        }

    private:
        std::thread::id ownerThread_;
    };
} // namespace CNA::Internal::Renderers::Skia
