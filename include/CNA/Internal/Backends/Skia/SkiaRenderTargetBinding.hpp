#pragma once

#include "CNA/Internal/Backends/Skia/SkiaOwnership.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Internal::Backends::Skia
{
    class SkiaRenderTargetBackend;
    class SkiaSurface;

    /**
     * Backend-owned record of the current raster destination.
     *
     * Render targets retain this only weakly.  That makes a target destructor safe after the
     * graphics backend has already gone away, while still allowing a target that dies while bound
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
                    + " was attempted after graphics backend destruction.");
            }
            ownership->AssertOwnerThread(operation);
        }

        void SetBackbuffer(SkiaSurface* backbuffer)
        {
            AssertOwnerAccess("backbuffer selection");
            if (!backbuffer)
                throw std::runtime_error("Skia active-surface violation: backbuffer must not be null.");
            backbuffer_ = backbuffer;
            if (activeTarget_ == nullptr)
                activeSurface_ = backbuffer_;
        }

        void Bind(SkiaRenderTargetBackend* target, SkiaSurface* surface)
        {
            AssertOwnerAccess("render-target binding");
            if (!target || !surface || surface == backbuffer_)
            {
                throw std::runtime_error(
                    "Skia active-surface violation: target and its non-backbuffer surface must be valid.");
            }
            activeTarget_ = target;
            activeSurface_ = surface;
        }

        void UnbindToBackbuffer()
        {
            AssertOwnerAccess("render-target unbinding");
            if (!backbuffer_)
                throw std::runtime_error("Skia active-surface violation: no backbuffer is registered.");
            activeTarget_ = nullptr;
            activeSurface_ = backbuffer_;
        }

        void Detach(const SkiaRenderTargetBackend* target) noexcept
        {
            if (activeTarget_ == target)
            {
                // Resource destructors cannot throw. The weak ownership token makes this safe
                // after backend destruction; while the binding is live, restore its stable
                // backend-owned surface directly before the target's surface is released.
                activeTarget_ = nullptr;
                activeSurface_ = backbuffer_;
            }
        }

        void AssertConsistent(const SkiaSurface* expectedBackbuffer) const
        {
            AssertOwnerAccess("active-surface access");
            if (!backbuffer_ || backbuffer_ != expectedBackbuffer || !activeSurface_
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
        [[nodiscard]] SkiaRenderTargetBackend* ActiveTarget() const
        {
            AssertOwnerAccess("active-target query");
            return activeTarget_;
        }

    private:
        SkiaSurface* backbuffer_ = nullptr;
        SkiaSurface* activeSurface_ = nullptr;
        SkiaRenderTargetBackend* activeTarget_ = nullptr;
        std::weak_ptr<SkiaOwnership> ownership_;
        bool requiresOwnership_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
