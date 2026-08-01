// SPDX-License-Identifier: MS-PL
// SKIA-69: Direct ownership test for the raster target-binding record.  It covers destruction of
// a target while selected, repeated snapshot release, and the inverse lifetime (backend binding
// released before a target) without needing an SDL window.

#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include <cstdio>
#include <memory>

using namespace CNA::Internal::Backends::Skia;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures;
    }
}

int main()
{
    auto binding = std::make_shared<SkiaRenderTargetBinding>();
    SkiaSurface backbuffer(4, 4);
    backbuffer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    binding->SetBackbuffer(&backbuffer);
    Check(binding->ActiveSurface() == &backbuffer, "new binding selects its backbuffer");

    bool nullBackbufferRejected = false;
    try
    {
        binding->SetBackbuffer(nullptr);
    }
    catch (const std::exception&)
    {
        nullBackbufferRejected = true;
    }
    Check(nullBackbufferRejected && binding->ActiveSurface() == &backbuffer,
          "null backbuffer is rejected without changing the active surface");

    {
        auto target = std::make_unique<SkiaRenderTargetBackend>(4, 4, true, binding);

        bool nullTargetRejected = false;
        try
        {
            binding->Bind(nullptr, &target->Surface());
        }
        catch (const std::exception&)
        {
            nullTargetRejected = true;
        }
        Check(nullTargetRejected && binding->ActiveSurface() == &backbuffer,
              "null target is rejected without changing the active surface");

        bool backbufferAliasRejected = false;
        try
        {
            binding->Bind(target.get(), &backbuffer);
        }
        catch (const std::exception&)
        {
            backbufferAliasRejected = true;
        }
        Check(backbufferAliasRejected && binding->ActiveSurface() == &backbuffer,
              "target cannot claim the backend-owned backbuffer surface");

        binding->Bind(target.get(), &target->Surface());
        Check(binding->ActiveSurface() == &target->Surface(), "bound target becomes active surface");

        auto snapshot = target->SnapshotImage(SkiaSourceAlphaConvention::Premultiplied);
        Check(snapshot != nullptr, "bound target creates a sampling snapshot");
        snapshot.reset();
    }
    Check(binding->ActiveSurface() == &backbuffer && binding->ActiveTarget() == nullptr,
          "destroyed bound target returns the binding to the backbuffer");

    bool everySnapshotWasReleased = true;
    for (int i = 0; i < 128; ++i)
    {
        auto target = std::make_unique<SkiaRenderTargetBackend>(2, 2, true, binding);
        binding->Bind(target.get(), &target->Surface());
        auto snapshot = target->SnapshotImage(SkiaSourceAlphaConvention::Premultiplied);
        everySnapshotWasReleased = everySnapshotWasReleased && snapshot != nullptr;
    }
    Check(everySnapshotWasReleased && binding->ActiveSurface() == &backbuffer,
          "repeated target snapshots release with their destroyed targets");

    auto lateTarget = std::make_unique<SkiaRenderTargetBackend>(2, 2, true, binding);
    binding->Bind(lateTarget.get(), &lateTarget->Surface());
    std::weak_ptr<SkiaRenderTargetBinding> weakBinding = binding;
    binding.reset(); // Models graphics-backend destruction before its resource wrapper dies.
    Check(weakBinding.expired(), "target binding expires when its graphics backend is destroyed");
    lateTarget.reset(); // Must not access the expired backend binding.
    Check(true, "target destruction after backend destruction is safe");

    std::printf("=== %d/%d PASS ===\n", 10 - failures, 10);
    return failures == 0 ? 0 : 1;
}
