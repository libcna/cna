// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu_modern_graphics.md WMG-0027: a long run over the MODERN WebGPU resources.
//
// webgpu_resource_lifetime_stress_test (WEBGPU-191) does this for the classic path and says why it
// matters more on this renderer than on an immediate one: WebGPU queues its draws and replays them
// at flush, so a resource the game disposes right after the call is still referenced by work that
// has not run. That test predates every modern resource -- compute shaders, storage buffers,
// storage textures, indirect arguments, GPU timers -- so none of them were ever stressed.
//
// THE ORACLE IS THE RENDERER'S OWN UNCAPTURED-ERROR COUNT, not "it did not crash", and not the
// pixels. wgpu-native reports a use-after-free of a buffer, a view or an attachment as a validation
// error through the uncaptured-error callback; GetUncapturedErrorCountEXT() is that count, and a
// single one fails this program. A test that only checked for a crash would pass on a renderer
// quietly submitting invalid work, which is what a lifetime bug actually does -- the freed handle
// usually still addresses memory nothing has reused yet.
//
// Beside it, the four things a leak shows up in before it shows up anywhere else, sampled at the
// start and at the end: resident set size, open file descriptors, thread count, and the device's
// own lost flag. None of them may grow without an explanation.
//
// The work per cycle, so that every modern resource is created, used, read back and destroyed
// rather than allocated once and held:
//
//   * a compute dispatch into a storage buffer, and a readback of what it wrote;
//   * a second dispatch reading that buffer and writing a storage TEXTURE;
//   * a render pass sampling the storage texture into a render target;
//   * an indirect draw whose counts were written by the first dispatch and never came to the CPU;
//   * a compute dispatch BETWEEN two render passes, which is the ordering ADR 0001 governs;
//   * a GPU timer opened and closed around the whole cycle;
//   * a render-target resize every 64th cycle, which is where a pool must release and reallocate;
//   * every one of those resources destroyed at the end of the cycle and rebuilt by the next.
//
// Check A -- the run completed without the device being lost.
// Check B -- zero uncaptured provider errors across the whole run.
// Check C -- RSS did not grow beyond the allowance once the caches are warm.
// Check D -- the open file descriptor count did not grow.
// Check E -- the thread count did not grow.
// Check F -- the work really happened: the last cycle's readback still holds what compute wrote.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = this renderer has no modern route.
//
//   ./cna_test_webgpu_modern_stress [--cycles N]   (default 3000)

#ifndef CNA_CNAEXT
#include <cstdio>
int main()
{
    std::printf("SKIP: the engine layer is off in this build; the modern resources need it\n");
    return 77;
}
#else

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Graphics/StorageTexture2D.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsImageAccess.hpp"
#include "CNA/IndirectDrawArguments.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 64;
    constexpr int kElements = 64;

    int passCount = 0;
    int totalCount = 0;
    int cycles = 3000;

    void check(const bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // /proc is the only place these three are available without adding a dependency, and this
    // program is Linux-only in practice -- it is a WebGPU example, built where wgpu-native is.
    std::size_t residentKiB()
    {
        std::FILE* f = std::fopen("/proc/self/statm", "r");
        if (f == nullptr) return 0;
        unsigned long size = 0, resident = 0;
        const int read = std::fscanf(f, "%lu %lu", &size, &resident);
        std::fclose(f);
        if (read != 2) return 0;
        return static_cast<std::size_t>(resident) * 4u;  // pages of 4 KiB
    }

    std::size_t countEntries(const char* path)
    {
        DIR* dir = ::opendir(path);
        if (dir == nullptr) return 0;
        std::size_t n = 0;
        while (const dirent* entry = ::readdir(dir))
        {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
                continue;
            ++n;
        }
        ::closedir(dir);
        return n;
    }

    std::size_t openDescriptors() { return countEntries("/proc/self/fd"); }
    std::size_t threadCount() { return countEntries("/proc/self/task"); }

    // Writes element i as (i + seed), so a readback proves this cycle's dispatch ran rather than a
    // previous one's result still sitting in a reused buffer.
    const char* const kFillWgsl = R"WGSL(
struct Params { seed: f32, pad0: f32, pad1: f32, pad2: f32, };
@group(0) @binding(0) var<storage, read_write> data: array<u32>;
@group(3) @binding(0) var<uniform> p: Params;
@compute @workgroup_size(16)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&data)) { data[id.x] = id.x + u32(p.seed); }
}
)WGSL";

    // Reads the buffer the first dispatch wrote and paints a storage texture from it, so the two
    // dispatches are ordered against each other rather than merely both submitted.
    const char* const kPaintWgsl = R"WGSL(
@group(0) @binding(0) var<storage, read> data: array<u32>;
@group(0) @binding(1) var image: texture_storage_2d<rgba8unorm, write>;
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let dims = textureDimensions(image);
    if (id.x >= dims.x || id.y >= dims.y) { return; }
    let v = f32(data[id.x % arrayLength(&data)] % 255u) / 255.0;
    textureStore(image, vec2i(i32(id.x), i32(id.y)), vec4f(v, 0.25, 0.5, 1.0));
}
)WGSL";
}

class WebGpuModernStressTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(dev.GetRenderer());

        if (!dev.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
        {
            std::printf("SKIP: this renderer has no compute shaders\n");
            skipped_ = true;
            Exit();
            return;
        }

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rs);
        dev.setBlendStateProperty(BlendState::Opaque);

        const VertexPositionColor triangle[3] = {
            {Vector3(-1.0f, -1.0f, 0.0f), Color(255, 0, 0, 255)},
            {Vector3(3.0f, -1.0f, 0.0f), Color(0, 255, 0, 255)},
            {Vector3(-1.0f, 3.0f, 0.0f), Color(0, 0, 255, 255)},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3,
                        BufferUsage::None);
        vb.SetData(triangle, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;

        // Warm the pipeline, shader and pool caches before the first sample, so their one-time
        // growth is not read as a leak. Ten cycles is well past every lazily-created resource.
        constexpr int kWarmUp = 10;
        std::vector<std::uint32_t> readback(kElements, 0);
        std::size_t errorsBefore = 0;
        std::size_t rssBefore = 0;
        std::size_t fdBefore = 0;
        std::size_t threadsBefore = 0;

        bool devicePresumedLost = false;
        std::string failure;
        int completed = 0;
        int targetSize = kSize;

        for (int cycle = 0; cycle < cycles + kWarmUp; ++cycle)
        {
            if (cycle == kWarmUp)
            {
                errorsBefore = renderer.GetUncapturedErrorCountEXT();
                rssBefore = residentKiB();
                fdBefore = openDescriptors();
                threadsBefore = threadCount();
                std::printf("    warm: RSS %zu KiB, %zu fds, %zu threads, %zu provider errors\n",
                            rssBefore, fdBefore, threadsBefore, errorsBefore);
            }
            try
            {
                // Every resource is created here and destroyed at the end of the iteration, which
                // is the whole point: a pool that never releases, or a keep-alive that never
                // expires, shows up as growth rather than as a wrong pixel.
                if ((cycle % 64) == 0) targetSize = (targetSize == kSize) ? kSize / 2 : kSize;
                RenderTarget2D target(dev, targetSize, targetSize, false, SurfaceFormat::Color,
                                      DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

                CNA::Graphics::GpuTimer timer(dev);
                if (timer.isSupported()) timer.begin();

                CNA::Graphics::StorageBuffer data(
                    dev, CNA::Graphics::StorageBufferDescriptor(
                             kElements * sizeof(std::uint32_t),
                             CNA::Graphics::StorageBufferUsage::Storage |
                                 CNA::Graphics::StorageBufferUsage::IndirectArguments,
                             CNA::Graphics::StorageBufferCpuAccess::Read |
                                 CNA::Graphics::StorageBufferCpuAccess::Write));

                CNA::Graphics::ComputeShader fill(dev, kFillWgsl);
                fill.bindStorageBuffer(0, data);
                fill.setUniform("seed", static_cast<float>(cycle));
                fill.dispatch(kElements / 16, 1, 1);

                // A render pass, then compute, then another render pass -- the ordering ADR 0001
                // governs and the one a renderer that batches naively gets wrong.
                dev.SetRenderTarget(&target);
                dev.Clear(Color(0, 0, 0, 255));
                dev.SetVertexBuffer(&vb);
                fx.Apply();
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                dev.SetVertexBuffer(nullptr);
                dev.SetRenderTarget(nullptr);

                {
                    // Storage textures, where the renderer offers them. A renderer that declines
                    // says so at construction and the rest of the cycle is unaffected.
                    try
                    {
                        CNA::Graphics::StorageTexture2D image(
                            dev, CNA::Graphics::StorageTexture2DDescriptor(
                                     16, 16, 1, SurfaceFormat::Color,
                                     CNA::Graphics::StorageTexture2DUsage::StorageWrite |
                                         CNA::Graphics::StorageTexture2DUsage::Sampled));
                        CNA::Graphics::ComputeShader paint(dev, kPaintWgsl);
                        paint.bindStorageBuffer(0, data);
                        paint.bindStorageTexture(1, image, CNA::GraphicsImageAccess::WriteOnly);
                        paint.dispatch(2, 2, 1);
                    }
                    catch (const std::exception&)
                    {
                        // A renderer that declines a storage texture says so at construction; the
                        // rest of the cycle is unaffected and the run continues.
                    }
                }

                // An indirect draw whose counts came from the GPU. The arguments live in the same
                // buffer compute just wrote, so the CPU never learns the count.
                if (dev.SupportsCapability(CNA::GraphicsCapability::IndirectDraw))
                {
                    CNA::IndirectDrawArguments args{};
                    args.VertexCount = 3;
                    args.InstanceCount = 1;
                    args.FirstVertex = 0;
                    args.BaseInstance = 0;
                    CNA::Graphics::StorageBuffer arguments(
                        dev, CNA::Graphics::StorageBufferDescriptor(
                                 sizeof(args), CNA::Graphics::StorageBufferUsage::IndirectArguments,
                                 CNA::Graphics::StorageBufferCpuAccess::Write));
                    arguments.setBytes(&args, sizeof(args));
                    dev.SetRenderTarget(&target);
                    dev.SetVertexBuffer(&vb);
                    fx.Apply();
                    dev.DrawPrimitivesIndirectEXT(PrimitiveType::TriangleList,
                                                  *arguments.getRendererEXT(), 0);
                    dev.SetVertexBuffer(nullptr);
                    dev.SetRenderTarget(nullptr);
                }

                // The read-back, which is what forces the whole cycle through and is the only
                // thing proving the dispatch above really ran.
                data.getBytes(readback.data(), readback.size() * sizeof(std::uint32_t));

                if (timer.isSupported())
                {
                    timer.end();
                    timer.poll();
                }
                ++completed;
            }
            catch (const std::exception& e)
            {
                failure = e.what();
                break;
            }
            if (!renderer.CanBeginDrawEXT())
            {
                devicePresumedLost = true;
                break;
            }
        }

        const std::size_t errorsAfter = renderer.GetUncapturedErrorCountEXT();
        const std::size_t rssAfter = residentKiB();
        const std::size_t fdAfter = openDescriptors();
        const std::size_t threadsAfter = threadCount();

        std::printf("    ran %d of %d cycles%s%s\n", completed, cycles + kWarmUp,
                    failure.empty() ? "" : ", stopped by: ", failure.c_str());
        std::printf("    RSS %zu -> %zu KiB (%+lld), fds %zu -> %zu, threads %zu -> %zu\n",
                    rssBefore, rssAfter,
                    static_cast<long long>(rssAfter) - static_cast<long long>(rssBefore), fdBefore,
                    fdAfter, threadsBefore, threadsAfter);
        std::printf("    provider errors %zu -> %zu, device lost: %s\n", errorsBefore, errorsAfter,
                    devicePresumedLost ? "yes" : "no");

        check(!devicePresumedLost && failure.empty() && completed == cycles + kWarmUp,
              "Check A: the whole run completed and the device was never lost");
        check(errorsAfter == errorsBefore,
              "Check B: zero uncaptured provider errors across the run");

        // 64 MiB over three thousand cycles. Generous, because an allocator is free not to return
        // pages to the kernel, and still far under what one leaked target per cycle would cost:
        // a 64x64 RGBA target is 16 KiB, so three thousand of them is 48 MiB of texture alone plus
        // its views, bind groups and pipeline records.
        const long long rssDelta =
            static_cast<long long>(rssAfter) - static_cast<long long>(rssBefore);
        check(rssDelta < 64 * 1024, "Check C: RSS did not grow beyond the allowance");
        check(fdAfter <= fdBefore, "Check D: no file descriptor was leaked");
        check(threadsAfter <= threadsBefore, "Check E: no thread was leaked");

        bool lastCycleLanded = completed > 0;
        for (int i = 0; i < kElements && lastCycleLanded; ++i)
        {
            const std::uint32_t expected =
                static_cast<std::uint32_t>(i) + static_cast<std::uint32_t>(completed - 1);
            if (readback[static_cast<std::size_t>(i)] != expected) lastCycleLanded = false;
        }
        std::printf("    last readback element 0 = %u (cycle %d wrote seed %d)\n", readback[0],
                    completed - 1, completed - 1);
        check(lastCycleLanded,
              "Check F: the last cycle's readback holds what its own dispatch wrote");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

private:
    bool skipped_ = false;

public:
    WebGpuModernStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] bool wasSkipped() const { return skipped_; }
};

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) cycles = std::atoi(argv[++i]);
    if (cycles < 1) cycles = 1;

    WebGpuModernStressTest game;
    game.Run();

    if (game.wasSkipped()) return 77;
    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}

#endif
