#pragma once

#include "CNA/Internal/Renderers/Skia/SkiaOwnership.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRasterTarget.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::Skia
{
    /**
     * Renderer-owned record of the current raster destination.
     *
     * Render targets retain this only weakly.  That makes a target destructor safe after the
     * graphics renderer has already gone away, while still allowing a target that dies while bound
     * to detach the raw active-surface reference before its SkSurface is released.
     */
    class SkiaRenderTargetBinding final
    {
    public:
        explicit SkiaRenderTargetBinding(std::shared_ptr<SkiaOwnership> ownership = {})
            : ownership_(ownership), requiresOwnership_(ownership != nullptr) {}

        void AssertOwnerAccess(std::string_view operation) const
        {
            if (!requiresOwnership_)
                return;
            const std::shared_ptr<SkiaOwnership> ownership = ownership_.lock();
            if (!ownership)
            {
                throw std::runtime_error(
                    "Skia ownership violation: " + std::string(operation)
                    + " was attempted after graphics renderer destruction.");
            }
            ownership->AssertOwnerThread(operation);
        }

        void SetBackbuffer(SkiaSurface* backbuffer)
        {
            AssertOwnerAccess("backbuffer selection");
            if (!backbuffer)
                throw std::runtime_error("Skia active-surface violation: backbuffer must not be null.");
            backbuffer_ = backbuffer;
            backbufferIdentity_ = backbuffer->Identity();
            if (activeTarget_ == nullptr)
            {
                activeSurface_ = backbuffer_;
                activeSurfaceIdentity_ = backbufferIdentity_;
            }
        }

        void Bind(SkiaRasterTarget* target, SkiaSurface* surface)
        {
            AssertOwnerAccess("render-target binding");
            if (!target || !surface || surface == backbuffer_)
            {
                throw std::runtime_error(
                    "Skia active-surface violation: target and its non-backbuffer surface must be valid.");
            }
            activeTarget_ = target;
            activeSurface_ = surface;
            activeSurfaceIdentity_ = surface->Identity();
        }

        void UnbindToBackbuffer()
        {
            AssertOwnerAccess("render-target unbinding");
            if (!backbuffer_)
                throw std::runtime_error("Skia active-surface violation: no backbuffer is registered.");
            activeTarget_ = nullptr;
            activeSurface_ = backbuffer_;
            activeSurfaceIdentity_ = backbufferIdentity_;
        }

        void Detach(const SkiaRasterTarget* target) noexcept
        {
            if (activeTarget_ == target)
            {
                // Resource destructors cannot throw. The weak ownership token makes this safe
                // after renderer destruction; while the binding is live, restore its stable
                // renderer-owned surface directly before the target's surface is released.
                activeTarget_ = nullptr;
                activeSurface_ = backbuffer_;
                activeSurfaceIdentity_ = backbufferIdentity_;
            }
        }

        void AssertConsistent(const SkiaSurface* expectedBackbuffer) const
        {
            AssertOwnerAccess("active-surface access");
            if (!backbuffer_ || backbuffer_ != expectedBackbuffer || !activeSurface_
                || backbufferIdentity_ == 0 || activeSurfaceIdentity_ == 0
                || backbuffer_->Identity() != backbufferIdentity_
                || activeSurface_->Identity() != activeSurfaceIdentity_
                || (activeTarget_ == nullptr && activeSurface_ != backbuffer_)
                || (activeTarget_ != nullptr && activeSurface_ == backbuffer_))
            {
                throw std::runtime_error("Skia active-surface ownership is inconsistent.");
            }
        }

        [[nodiscard]] SkiaSurface* ActiveSurface() const
        {
            AssertOwnerAccess("active-surface query");
            return activeSurface_;
        }
        [[nodiscard]] SkiaSurface*& ActiveSurfaceRef()
        {
            AssertOwnerAccess("active-surface reference");
            return activeSurface_;
        }
        [[nodiscard]] SkiaRasterTarget* ActiveTarget() const
        {
            AssertOwnerAccess("active-target query");
            return activeTarget_;
        }

    private:
        SkiaSurface* backbuffer_ = nullptr;
        SkiaSurface* activeSurface_ = nullptr;
        SkiaRasterTarget* activeTarget_ = nullptr;
        std::uint64_t backbufferIdentity_ = 0;
        std::uint64_t activeSurfaceIdentity_ = 0;
        std::weak_ptr<SkiaOwnership> ownership_;
        bool requiresOwnership_ = false;
    };
} // namespace CNA::Internal::Renderers::Skia
