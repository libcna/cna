// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4_modern_graphics.md GL4-0032: a long run over the MODERN OpenGL4 resources.
//
// Modelled on webgpu_modern_stress_test (WMG-0027), with this renderer's own oracle. OpenGL4 issues
// work immediately, so its lifetime risk is not WebGPU's replayed frame; it is a GL name used after
// the object behind it was deleted, a binding a compute pass left behind for the next draw, a
// barrier missing between a write and its reader, or a name never deleted at all. The first three
// surface as GL errors through the synchronous KHR_debug callback -- the "[OpenGL4 GL Error]" line
// the renderer logs, counted here through the Logger sink -- or as wrong readback values; the last
// as growth.
//
// The work per cycle, every resource created, used, read back and destroyed inside it:
//
//   * a compute dispatch into a storage buffer (seeded by the cycle), and a readback of it;
//   * a second dispatch reading that buffer and writing a storage TEXTURE;
//   * a render pass, then compute, then another render pass (the ordering ADR 0001 governs);
//   * an indirect draw whose arguments were uploaded to an argument buffer;
//   * a base-instance draw from a two-record instance stream;
//   * a GPU timer around the cycle;
//   * a render-target resize every 64th cycle.
//
// Check A -- the run completed.
// Check B -- zero "[OpenGL4 GL Error]" lines across the whole run.
// Check C -- RSS did not grow beyond the allowance once the caches are warm.
// Check D -- the open file descriptor count did not grow.
// Check E -- the thread count did not grow, not counting Mesa's shader-compiler pool, which the
//            driver widens on its own under GPU contention (named, and printed when it grows).
// Check F -- the work really happened: the last cycle's readback holds what its dispatch wrote.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = not OPENGL4, or no compute.
//
//   ./cna_test_opengl4_modern_stress [--cycles N]   (default 3000)

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
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/IndirectDrawArguments.hpp"
#include "CNA/Logger.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kElements = 64;

    int passCount = 0;
    int totalCount = 0;
    int cycles = 3000;
    std::size_t glErrors = 0;

    void check(const bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

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

    /// The kernel names of this process's threads (`/proc/self/task/<tid>/comm`), one per thread.
    std::vector<std::string> threadNames()
    {
        std::vector<std::string> names;
        DIR* dir = ::opendir("/proc/self/task");
        if (dir == nullptr) return names;
        while (const dirent* entry = ::readdir(dir))
        {
            if (entry->d_name[0] == '.') continue;
            const std::string path = std::string("/proc/self/task/") + entry->d_name + "/comm";
            std::FILE* f = std::fopen(path.c_str(), "r");
            if (f == nullptr) continue;
            char name[64] = {};
            if (std::fgets(name, sizeof(name), f) != nullptr)
            {
                std::string text(name);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                names.push_back(text);
            }
            std::fclose(f);
        }
        ::closedir(dir);
        std::sort(names.begin(), names.end());
        return names;
    }

    /// Mesa radeonsi's shader-compiler queues ("<process>:sh<N>", "<process>:shlo<N>"). Mesa
    /// starts them lazily and adds workers when the queue backs up -- which happens when other
    /// processes compete for the GPU, as a parallel ctest run does -- up to a fixed ceiling. A
    /// bounded pool of the driver's, not something a cycle of this test could leak.
    bool isDriverShaderCompilerThread(const std::string& name)
    {
        const std::size_t colon = name.rfind(':');
        if (colon == std::string::npos) return false;
        std::string tail = name.substr(colon + 1);
        if (tail.rfind("shlo", 0) == 0) tail = tail.substr(4);
        else if (tail.rfind("sh", 0) == 0) tail = tail.substr(2);
        else return false;
        return !tail.empty() &&
               std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    std::size_t countExcludingDriverPool(const std::vector<std::string>& names)
    {
        return static_cast<std::size_t>(std::count_if(
            names.begin(), names.end(),
            [](const std::string& name) { return !isDriverShaderCompilerThread(name); }));
    }

    // Writes element i as (i + seed), so a readback proves this cycle's dispatch ran rather than a
    // previous one's result still sitting in a reused buffer.
    const char* const kFill = R"GLSL(#version 430 core
layout(local_size_x = 16) in;
layout(std430, binding = 0) buffer Data { uint data[]; };
uniform float seed;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < uint(data.length())) data[i] = i + uint(seed);
}
)GLSL";

    // Reads the buffer the first dispatch wrote and paints a storage texture from it, so the two
    // dispatches are ordered against each other rather than merely both submitted.
    const char* const kPaint = R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) readonly buffer Data { uint data[]; };
layout(rgba8, binding = 1) writeonly uniform image2D image;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, imageSize(image)))) return;
    float v = float(data[uint(p.x) % uint(data.length())] % 255u) / 255.0;
    imageStore(image, p, vec4(v, 0.25, 0.5, 1.0));
}
)GLSL";

    const char* const kInstancedVertex = R"GLSL(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColour;
flat out vec4 vColour;
void main() { gl_Position = vec4(aPosition, 1.0); vColour = aColour; }
)GLSL";

    const char* const kInstancedFragment = R"GLSL(#version 410 core
flat in vec4 vColour;
out vec4 FragColor;
void main() { FragColor = vColour; }
)GLSL";
}

class OpenGL4ModernStressTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    bool skipped_ = false;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        // Its shaders are desktop GLSL 4.30 and its oracle is this renderer's debug callback, so it
        // measures OpenGL4 and nothing else a multi-renderer tree may select at runtime.
        if (dev.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        {
            std::printf("SKIP: this run selected %s, not OPENGL4\n",
                        std::string(dev.GetGraphicsRendererName()).c_str());
            skipped_ = true;
            Exit();
            return;
        }
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

        // The base-instance draw's inputs: a quad, its indices, two per-instance colours.
        const std::array<std::array<float, 3>, 4> corners{{
            {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
        VertexBuffer quad(dev,
                          VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                               VertexElementUsage::Position, 0)}),
                          4, BufferUsage::None);
        quad.SetDataRaw(corners.data(), 4, 12);
        const std::array<std::uint32_t, 2> colours{Color::Red.getPackedValueProperty(),
                                                   Color::Lime.getPackedValueProperty()};
        VertexBuffer instances(dev,
                               VertexDeclaration(4, {VertexElement(0, VertexElementFormat::Color,
                                                                   VertexElementUsage::Color, 0)}),
                               2, BufferUsage::None);
        instances.SetDataRaw(colours.data(), 2, 4);
        const std::array<std::uint16_t, 6> order{0, 1, 2, 0, 2, 3};
        IndexBuffer indices(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        indices.SetData(order.data(), 6);
        ShaderEffect instanced(dev, kInstancedVertex, kInstancedFragment);

        // Warm every lazily-created program, sampler and cache before the first sample, so their
        // one-time growth is not read as a leak.
        constexpr int kWarmUp = 10;
        std::vector<std::uint32_t> readback(kElements, 0);
        std::size_t errorsBefore = 0;
        std::size_t rssBefore = 0;
        std::size_t fdBefore = 0;
        std::size_t threadsBefore = 0;
        std::vector<std::string> namesBefore;

        std::string failure;
        int completed = 0;
        int targetSize = kSize;

        for (int cycle = 0; cycle < cycles + kWarmUp; ++cycle)
        {
            if (cycle == kWarmUp)
            {
                errorsBefore = glErrors;
                rssBefore = residentKiB();
                fdBefore = openDescriptors();
                threadsBefore = threadCount();
                namesBefore = threadNames();
                std::printf("    warm: RSS %zu KiB, %zu fds, %zu threads, %zu GL errors\n",
                            rssBefore, fdBefore, threadsBefore, errorsBefore);
            }
            try
            {
                if ((cycle % 64) == 0) targetSize = (targetSize == kSize) ? kSize / 2 : kSize;
                RenderTarget2D target(dev, targetSize, targetSize, false, SurfaceFormat::Color,
                                      DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

                CNA::Graphics::GpuTimer timer(dev);
                if (timer.isSupported()) timer.begin();

                CNA::Graphics::StorageBuffer data(
                    dev, CNA::Graphics::StorageBufferDescriptor(
                             kElements * sizeof(std::uint32_t),
                             CNA::Graphics::StorageBufferUsage::Storage,
                             CNA::Graphics::StorageBufferCpuAccess::Read |
                                 CNA::Graphics::StorageBufferCpuAccess::Write));

                CNA::Graphics::ComputeShader fill(dev, kFill);
                fill.bindStorageBuffer(0, data);
                fill.setUniform("seed", static_cast<float>(cycle));
                fill.dispatch(kElements / 16, 1, 1);

                // A render pass, then compute, then another render pass.
                dev.SetRenderTarget(&target);
                dev.Clear(Color(0, 0, 0, 255));
                dev.SetVertexBuffer(&vb);
                fx.Apply();
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                dev.SetVertexBuffer(nullptr);
                dev.SetRenderTarget(nullptr);

                {
                    CNA::Graphics::StorageTexture2D image(
                        dev, CNA::Graphics::StorageTexture2DDescriptor(
                                 16, 16, 1, SurfaceFormat::Color,
                                 CNA::Graphics::StorageTexture2DUsage::StorageWrite |
                                     CNA::Graphics::StorageTexture2DUsage::Sampled));
                    CNA::Graphics::ComputeShader paint(dev, kPaint);
                    paint.bindStorageBuffer(0, data);
                    paint.bindStorageTexture(1, image, CNA::GraphicsImageAccess::WriteOnly);
                    paint.dispatch(2, 2, 1);
                }

                // An indirect draw whose counts live in a GPU buffer.
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

                // A base-instance draw of the second instance.
                dev.SetRenderTarget(&target);
                instanced.Apply();
                dev.SetVertexBuffers({VertexBufferBinding(&quad, 0, 0),
                                      VertexBufferBinding(&instances, 0, 1)});
                dev.setIndicesProperty(&indices);
                dev.DrawInstancedPrimitivesBaseInstanceEXT(PrimitiveType::TriangleList, 0, 0, 4, 0,
                                                           2, 1, 1);
                dev.setIndicesProperty(nullptr);
                dev.SetVertexBuffer(nullptr);
                dev.SetRenderTarget(nullptr);

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
        }

        const std::size_t errorsAfter = glErrors;
        const std::size_t rssAfter = residentKiB();
        const std::size_t fdAfter = openDescriptors();
        const std::size_t threadsAfter = threadCount();
        const std::vector<std::string> namesAfter = threadNames();
        std::vector<std::string> gained;
        std::set_difference(namesAfter.begin(), namesAfter.end(), namesBefore.begin(),
                            namesBefore.end(), std::back_inserter(gained));
        for (const std::string& name : gained)
            std::printf("    thread gained since warm-up: '%s'\n", name.c_str());

        std::printf("    ran %d of %d cycles%s%s\n", completed, cycles + kWarmUp,
                    failure.empty() ? "" : ", stopped by: ", failure.c_str());
        std::printf("    RSS %zu -> %zu KiB (%+lld), fds %zu -> %zu, threads %zu -> %zu\n",
                    rssBefore, rssAfter,
                    static_cast<long long>(rssAfter) - static_cast<long long>(rssBefore), fdBefore,
                    fdAfter, threadsBefore, threadsAfter);
        std::printf("    GL errors %zu -> %zu\n", errorsBefore, errorsAfter);

        check(failure.empty() && completed == cycles + kWarmUp,
              "Check A: the whole run completed");
        check(errorsAfter == 0, "Check B: the GL debug callback reported no error across the run");

        // 64 MiB over three thousand cycles, as the WebGPU run allows: an allocator is free not to
        // return pages to the kernel, and one leaked 64x64 target per cycle would still exceed it.
        const long long rssDelta =
            static_cast<long long>(rssAfter) - static_cast<long long>(rssBefore);
        check(rssDelta < 64 * 1024, "Check C: RSS did not grow beyond the allowance");
        check(fdAfter <= fdBefore, "Check D: no file descriptor was leaked");
        const std::size_t ownBefore = countExcludingDriverPool(namesBefore);
        const std::size_t ownAfter = countExcludingDriverPool(namesAfter);
        std::printf("    threads outside the driver's shader-compiler pool: %zu -> %zu\n", ownBefore,
                    ownAfter);
        check(ownAfter <= ownBefore,
              "Check E: no thread was leaked (Mesa's lazily scaled shader-compiler pool excluded)");

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

public:
    OpenGL4ModernStressTest()
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

    // The renderer's GL debug callback logs every GL error as "[OpenGL4 GL Error]"; counting them
    // here makes zero of them a check rather than a line someone has to notice. Every other line
    // still reaches the console.
    CNA::Logger::SetSink([](CNA::LogLevel, CNA::LogCategory, std::string_view line) {
        if (line.find("[OpenGL4 GL Error]") != std::string_view::npos) ++glErrors;
        std::fwrite(line.data(), 1, line.size(), stderr);
        std::fputc('\n', stderr);
    });

    OpenGL4ModernStressTest game;
    game.Run();
    CNA::Logger::ResetSink();

    if (game.wasSkipped()) return 77;
    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}

#endif
