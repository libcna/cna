// SPDX-License-Identifier: MS-PL
// SKIA-85/SKIA-86: direct proof of bounded six-surface RenderTargetCube emulation.

#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetBinding.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetCubeRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>

using namespace CNA::Internal::Renderers::Skia;

namespace
{
    int failures = 0;

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures;
    }

    template<typename Callable>
    bool Throws(Callable&& callable)
    {
        try
        {
            callable();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
}

int main()
{
    auto counters = std::make_shared<SkiaResourceCounters>();
    auto binding = std::make_shared<SkiaRenderTargetBinding>();
    SkiaSurface backbuffer(4, 4);
    binding->SetBackbuffer(&backbuffer);

    {
        SkiaRenderTargetCubeRenderer target(3, true, true, binding, counters);
        Check(target.GetSize() == 3 && target.GetMultiSampleCount() == 0
                  && !target.HasRealDepthBuffer(true),
              "cube reports exact face size and truthful raster depth/MSAA boundaries");
        Check(target.LevelCountEXT() == 2 && target.StorageBytesEXT() == 480
                  && target.PreservesContentsEXT(),
              "3x3 mipmapped cube bounds six surfaces plus exact RGBA shadows at both levels");
        const SkiaResourceStats live = counters->GetStats();
        Check(live.renderTargetCubes == 1 && live.cubeTargetStorageBytes == 480,
              "cube accounting reports the exact live surface-plus-shadow byte budget");

        target.BindAsRenderTargetFace(2);
        SkiaSurface* face2 = binding->ActiveSurface();
        const std::uint64_t face2Identity = face2->Identity();
        target.BeforeWriteEXT();
        face2->Clear(1.0f, 0.0f, 0.0f, 1.0f);
        target.BindAsRenderTargetFace(3); // Finalizes face 2 before selecting face 3.
        SkiaSurface* face3 = binding->ActiveSurface();
        Check(face3 != face2 && face3->Identity() != face2Identity,
              "selecting another face binds a distinct stable raster surface");

        std::array<std::uint8_t, 4> mipPixel{};
        Check(target.GetData(2, 1, 0, 0, 1, 1, mipPixel.data(), 4)
                  && mipPixel == std::array<std::uint8_t, 4>{255, 0, 0, 255},
              "switching faces regenerates the authored face's real mip chain");
        mipPixel.fill(0xCD);
        Check(target.GetData(3, 1, 0, 0, 1, 1, mipPixel.data(), 4)
                  && std::all_of(mipPixel.begin(), mipPixel.end(),
                                 [](std::uint8_t value) { return value == 0; }),
              "an untouched face and its mip levels remain independently zero initialized");

        target.BeforeWriteEXT();
        face3->Clear(0.0f, 1.0f, 0.0f, 1.0f);
        Check(target.GetData(3, 0, 0, 0, 1, 1, mipPixel.data(), 4)
                  && mipPixel == std::array<std::uint8_t, 4>{0, 255, 0, 255}
                  && binding->ActiveSurface() == face3,
              "readback synchronizes a dirty currently bound face without changing selection");

        target.BindAsRenderTargetFace(2);
        Check(binding->ActiveSurface()->Identity() == face2Identity,
              "rebinding a face returns to the same owned surface");
        target.UnbindAsRenderTarget();
        Check(binding->ActiveTarget() == nullptr && binding->ActiveSurface() == &backbuffer,
              "cube unbind finalizes writes and restores the registered backbuffer");

        const std::array<std::uint8_t, 4> translucent{17, 101, 203, 7};
        std::array<std::uint8_t, 4> translucentRead{};
        Check(target.SetData(4, 0, 1, 1, 1, 1, translucent.data(), 4)
                  && target.GetData(4, 0, 1, 1, 1, 1, translucentRead.data(), 4)
                  && translucentRead == translucent,
              "canonical shadow preserves arbitrary translucent SetData bytes exactly");

        std::array<std::uint8_t, 4> untouched{0xA5, 0xA5, 0xA5, 0xA5};
        Check(!target.GetData(6, 0, 0, 0, 1, 1, untouched.data(), 4)
                  && std::all_of(untouched.begin(), untouched.end(),
                                 [](std::uint8_t value) { return value == 0xA5; }),
              "invalid face readback rejects without touching destination memory");
    }
    Check(counters->GetStats().renderTargetCubes == 0
              && counters->GetStats().cubeTargetStorageBytes == 0,
          "cube destruction releases every surface byte and debug counter");

    {
        auto target = std::make_unique<SkiaRenderTargetCubeRenderer>(2, false, false,
                                                                    binding, counters);
        target->BindAsRenderTargetFace(5);
        target.reset();
        Check(binding->ActiveTarget() == nullptr && binding->ActiveSurface() == &backbuffer,
              "destroying a selected cube face detaches before its surface is released");
    }

    auto lateTarget = std::make_unique<SkiaRenderTargetCubeRenderer>(1, false, false,
                                                                    binding, counters);
    binding.reset();
    lateTarget.reset();
    Check(counters->GetStats().renderTargetCubes == 0,
          "cube destruction remains safe after its graphics binding has expired");

    Check(Throws([] { SkiaRenderTargetCubeRenderer invalid(0, false, false, {}); }),
          "zero-sized cube rejects before allocating surfaces");
    Check(Throws([] { SkiaRenderTargetCubeRenderer oversized(4096, false, false, {}); }),
          "cube exceeding 256 MiB rejects before allocating any face");
    Check(Throws([] { SkiaRenderTargetCubeRenderer oversized(16385, false, false, {}); }),
          "cube axis exceeding 16384 rejects independently from the byte budget");

    std::printf("=== %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
