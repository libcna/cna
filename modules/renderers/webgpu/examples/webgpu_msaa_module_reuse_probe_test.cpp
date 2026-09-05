// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-197 step (a): re-measure the WEBGPU-58 shader-module reuse finding
// against the current wgpu-native pin.
//
// WHAT WEBGPU-58 RECORDED, and why it matters again now. `ClearAllPipelineCaches()` tears down every
// shader module, bind-group layout and pipeline layout -- not merely the `WGPURenderPipeline`
// objects -- whenever the renderer-global sample count changes, because that task measured that a
// module/layout set already used for one pipeline, then reused UNCHANGED for a pipeline with a
// different `WGPUMultisampleState.count`, silently renders wrong on this pin: pipeline creation
// succeeds, the draw succeeds, no validation error is raised, and only the pixels are wrong.
//
// `WEBGPU-197` made that hazard reachable. Pipelines are now built at the BOUND PASS's sample count
// rather than the renderer-global one, from the same long-lived modules -- which is exactly the
// reuse above. It is harmless while every pass in a frame shares one count, and `WEBGPU-165` is the
// task that would end that. So the finding has to be re-measured before `165`, not assumed in
// either direction: if it still holds, the module and layout SETS must be keyed by sample count too,
// and if it does not, `ClearAllPipelineCaches()`'s full teardown can be narrowed.
//
// THE EXPERIMENT. One module set is used for a 1-sample pipeline and then for a 4-sample pipeline;
// a second, freshly created module set is used for a 4-sample pipeline alone. The two 4-sample
// frames are compared to each other -- same sample count, same shader source, same layout shape,
// differing only in whether the module set had been used at another count first.
//
//   A1  set A, sampleCount 1   -> makes set A "already used"
//   A4  set A, sampleCount 4   -> the reuse case
//   B4  set B (fresh), count 4 -> the control
//
// A4 == B4 means reuse is safe here; A4 != B4 means the finding stands. The triangle has a DIAGONAL
// edge on purpose: a resolve that never happened, or happened wrongly, shows up as a binary edge
// where the control has intermediate coverage values, which a flat-edged triangle could not reveal.
//
// Non-vacuity is asserted first and separately: each of the three frames must actually contain a
// triangle. Two frames that both rendered nothing compare equal, and that comparison would prove
// nothing at all.

// MEASURED 2026-09-05 on wgpu-native v29.0.1.1 (AMD 780M / RADV, Xvfb :131), stable over repeated
// runs: A1 496 lit / 0 partial, A4 528 / 32, B4 528 / 32, **A4 vs B4 max channel difference 0**.
// The finding does NOT reproduce: a module set reused across sample counts renders identically to a
// fresh one here. That is why this file asserts equality rather than merely printing it -- if the
// hazard ever returns, on a new pin or a different adapter, this goes red instead of `WEBGPU-165`
// quietly producing wrong pixels.
//
// One thing worth recording about HOW to measure it, because the first version of this probe got a
// confident wrong answer. Mapping the readback buffer is not enough on this pin: the map callback
// can fire while the copy that fills it is still queued, and the readback then returns an all-zero
// frame -- "pipeline created fine, draw succeeded, no validation error, only the pixels are wrong",
// which is precisely the signature `WEBGPU-58` recorded. Waiting on
// `wgpuQueueOnSubmittedWorkDone` first is what made the answer trustworthy. That is a plausible
// explanation for the original finding, not a demonstrated one: the original probe is not in the
// tree and this cannot show what it did.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 32;
    constexpr std::uint32_t kBytesPerRow = 256;   ///< 32 * 4 rounded up to WebGPU's 256 alignment.
    constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    /// A lower-left half-screen triangle in flat red. The hypotenuse is the diagonal whose coverage
    /// a 4-sample resolve turns into intermediate values.
    constexpr const char* kWgsl = R"WGSL(
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0));
    return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f {
    return vec4f(1.0, 0.0, 0.0, 1.0);
}
)WGSL";

    [[nodiscard]] WGPUStringView Sv(const char* s)
    {
        WGPUStringView view{};
        view.data = s;
        view.length = s != nullptr ? std::strlen(s) : 0;
        return view;
    }

    /// One shader module plus the layouts built from it -- the unit WEBGPU-58 says must not be
    /// reused across sample counts.
    struct ModuleSet
    {
        WGPUShaderModule module = nullptr;
        WGPUBindGroupLayout bindGroupLayout = nullptr;
        WGPUPipelineLayout pipelineLayout = nullptr;

        void Release()
        {
            if (pipelineLayout != nullptr) wgpuPipelineLayoutRelease(pipelineLayout);
            if (bindGroupLayout != nullptr) wgpuBindGroupLayoutRelease(bindGroupLayout);
            if (module != nullptr) wgpuShaderModuleRelease(module);
            pipelineLayout = nullptr;
            bindGroupLayout = nullptr;
            module = nullptr;
        }
    };

    [[nodiscard]] ModuleSet MakeModuleSet(WGPUDevice device)
    {
        ModuleSet set;
        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = Sv(kWgsl);
        WGPUShaderModuleDescriptor moduleDescriptor{};
        moduleDescriptor.nextInChain = &wgsl.chain;
        moduleDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe");
        set.module = wgpuDeviceCreateShaderModule(device, &moduleDescriptor);

        WGPUBindGroupLayoutDescriptor bglDescriptor{};
        bglDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe BGL");
        bglDescriptor.entryCount = 0;
        set.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDescriptor);

        WGPUPipelineLayoutDescriptor plDescriptor{};
        plDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe PL");
        plDescriptor.bindGroupLayoutCount = 1;
        plDescriptor.bindGroupLayouts = &set.bindGroupLayout;
        set.pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDescriptor);
        return set;
    }

    struct MapState { bool done = false; WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error; };

    void OnMap(WGPUMapAsyncStatus status, WGPUStringView, void* user1, void*)
    {
        auto* state = static_cast<MapState*>(user1);
        state->status = status;
        state->done = true;
    }

    void OnWorkDone(WGPUQueueWorkDoneStatus, WGPUStringView, void* user1, void*)
    {
        static_cast<MapState*>(user1)->done = true;
    }
}

/// WEBGPU-197 step (a): does reusing a module set across sample counts still corrupt the frame?
class WebGpuMsaaModuleReuseProbeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int checkCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// Renders the probe triangle through @p set at @p sampleCount and returns the resolved frame.
    std::vector<std::uint8_t> RunTriangle(WebGPURenderer& renderer, const ModuleSet& set,
                                          std::uint32_t sampleCount)
    {
        WGPUDevice device = renderer.Device();
        WGPUQueue queue = renderer.Queue();

        WGPUTextureDescriptor colorDescriptor{};
        colorDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe Color");
        colorDescriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        colorDescriptor.dimension = WGPUTextureDimension_2D;
        colorDescriptor.size = WGPUExtent3D{kSize, kSize, 1};
        colorDescriptor.format = kFormat;
        colorDescriptor.mipLevelCount = 1;
        colorDescriptor.sampleCount = 1;
        WGPUTexture resolved = wgpuDeviceCreateTexture(device, &colorDescriptor);
        WGPUTextureView resolvedView = wgpuTextureCreateView(resolved, nullptr);

        WGPUTexture msaa = nullptr;
        WGPUTextureView msaaView = nullptr;
        if (sampleCount > 1)
        {
            WGPUTextureDescriptor msaaDescriptor = colorDescriptor;
            msaaDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe MSAA Color");
            msaaDescriptor.usage = WGPUTextureUsage_RenderAttachment;
            msaaDescriptor.sampleCount = sampleCount;
            msaa = wgpuDeviceCreateTexture(device, &msaaDescriptor);
            msaaView = wgpuTextureCreateView(msaa, nullptr);
        }

        WGPUColorTargetState target{};
        target.format = kFormat;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = set.module;
        fragment.entryPoint = Sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipelineDescriptor{};
        pipelineDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe Pipeline");
        pipelineDescriptor.layout = set.pipelineLayout;
        pipelineDescriptor.vertex.module = set.module;
        pipelineDescriptor.vertex.entryPoint = Sv("vs_main");
        pipelineDescriptor.vertex.bufferCount = 0;
        pipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDescriptor.primitive.frontFace = WGPUFrontFace_CCW;
        pipelineDescriptor.primitive.cullMode = WGPUCullMode_None;
        pipelineDescriptor.multisample.count = sampleCount;
        pipelineDescriptor.multisample.mask = 0xFFFFFFFFu;
        pipelineDescriptor.multisample.alphaToCoverageEnabled = false;
        pipelineDescriptor.fragment = &fragment;
        WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDescriptor);

        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDescriptor);

        WGPURenderPassColorAttachment attachment{};
        attachment.view = sampleCount > 1 ? msaaView : resolvedView;
        attachment.resolveTarget = sampleCount > 1 ? resolvedView : nullptr;
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};
        attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        WGPURenderPassDescriptor passDescriptor{};
        passDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe Pass");
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUBufferDescriptor bufferDescriptor{};
        bufferDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe Readback");
        bufferDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        bufferDescriptor.size = static_cast<std::uint64_t>(kBytesPerRow) * kSize;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &bufferDescriptor);

        WGPUTexelCopyTextureInfo source{};
        source.texture = resolved;
        source.mipLevel = 0;
        source.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readback;
        destination.layout.bytesPerRow = kBytesPerRow;
        destination.layout.rowsPerImage = kSize;
        const WGPUExtent3D extent{kSize, kSize, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);

        WGPUCommandBufferDescriptor commandsDescriptor{};
        commandsDescriptor.label = Sv("CNA WebGPU MSAA Reuse Probe Commands");
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &commandsDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue, 1, &commands);
        wgpuCommandBufferRelease(commands);

        // Wait for the SUBMITTED work first. Mapping alone is not enough on this pin: the map
        // callback can fire while the copy that fills the buffer is still queued, and the readback
        // then reports an all-zero frame -- indistinguishable from a draw that rendered nothing,
        // which is exactly the distinction this probe exists to make.
        MapState submitState;
        WGPUQueueWorkDoneCallbackInfo workInfo{};
        workInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        workInfo.callback = OnWorkDone;
        workInfo.userdata1 = &submitState;
        wgpuQueueOnSubmittedWorkDone(queue, workInfo);
        for (int spin = 0; spin < 200000 && !submitState.done; ++spin)
            wgpuInstanceProcessEvents(renderer.Instance());

        MapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback = OnMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bufferDescriptor.size, callbackInfo);
        for (int spin = 0; spin < 200000 && !mapState.done; ++spin)
            wgpuInstanceProcessEvents(renderer.Instance());
        if (!mapState.done || mapState.status != WGPUMapAsyncStatus_Success)
        {
            std::printf("[INFO] readback at sampleCount %u did not map (done=%d status=%d)\n",
                        sampleCount, mapState.done ? 1 : 0, static_cast<int>(mapState.status));
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSize) * kSize * 4u, 0u);
        if (mapState.done && mapState.status == WGPUMapAsyncStatus_Success)
        {
            const auto* mapped = static_cast<const std::uint8_t*>(
                wgpuBufferGetConstMappedRange(readback, 0, bufferDescriptor.size));
            if (mapped != nullptr)
            {
                for (int y = 0; y < kSize; ++y)
                    std::memcpy(pixels.data() + static_cast<std::size_t>(y) * kSize * 4u,
                                mapped + static_cast<std::size_t>(y) * kBytesPerRow,
                                static_cast<std::size_t>(kSize) * 4u);
            }
            wgpuBufferUnmap(readback);
        }

        wgpuBufferRelease(readback);
        wgpuRenderPipelineRelease(pipeline);
        if (msaaView != nullptr) wgpuTextureViewRelease(msaaView);
        if (msaa != nullptr) wgpuTextureRelease(msaa);
        wgpuTextureViewRelease(resolvedView);
        wgpuTextureRelease(resolved);
        return pixels;
    }

    /// How many texels carry any red at all -- the "did a triangle render" measure.
    [[nodiscard]] static int CountRed(const std::vector<std::uint8_t>& p)
    {
        int n = 0;
        for (std::size_t i = 0; i + 3 < p.size(); i += 4)
            if (p[i] > 8) ++n;
        return n;
    }

    /// How many texels are neither fully red nor fully background -- the resolve's own signature.
    [[nodiscard]] static int CountPartial(const std::vector<std::uint8_t>& p)
    {
        int n = 0;
        for (std::size_t i = 0; i + 3 < p.size(); i += 4)
            if (p[i] > 8 && p[i] < 247) ++n;
        return n;
    }

    [[nodiscard]] static int MaxDiff(const std::vector<std::uint8_t>& a,
                                     const std::vector<std::uint8_t>& b)
    {
        int worst = 0;
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        {
            const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            if (d > worst) worst = d;
        }
        return worst;
    }

public:
    WebGpuMsaaModuleReuseProbeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());

        // Does this adapter take 4x at all? WEBGPU-58's own probe found exactly {1, 4} here, and a
        // device that refused 4 would make every comparison below vacuous.
        const int applied = renderer.ApplyMultiSampleCount(4);
        std::printf("[INFO] ApplyMultiSampleCount(4) -> %d\n", applied);
        if (applied < 2)
        {
            std::printf("[INFO] this adapter offers no 4x MSAA; the reuse question cannot be "
                        "measured here and nothing is claimed\n");
            result_ = 0;
            Exit();
            return;
        }

        ModuleSet setA = MakeModuleSet(renderer.Device());
        const std::vector<std::uint8_t> a1 = RunTriangle(renderer, setA, 1);
        const std::vector<std::uint8_t> a4 = RunTriangle(renderer, setA, 4);
        ModuleSet setB = MakeModuleSet(renderer.Device());
        const std::vector<std::uint8_t> b4 = RunTriangle(renderer, setB, 4);

        std::printf("[INFO] A1 (set A, 1x): red=%d partial=%d\n", CountRed(a1), CountPartial(a1));
        std::printf("[INFO] A4 (set A REUSED, 4x): red=%d partial=%d\n",
                    CountRed(a4), CountPartial(a4));
        std::printf("[INFO] B4 (fresh set B, 4x): red=%d partial=%d\n",
                    CountRed(b4), CountPartial(b4));

        // Non-vacuity first: two empty frames compare equal and prove nothing.
        check(CountRed(a1) > 100, "A1: the 1-sample draw rendered a real triangle");
        check(CountRed(b4) > 100, "B4: the fresh-module 4-sample draw rendered a real triangle");
        check(CountPartial(b4) > 0,
              "B4: the 4-sample frame really was resolved -- its diagonal carries partial coverage, "
              "which a 1-sample frame cannot have");
        check(CountPartial(a1) == 0,
              "A1: the 1-sample frame has no partial coverage, so 'partial' is a real discriminator");

        const int worst = MaxDiff(a4, b4);
        std::printf("[INFO] A4 vs B4 max channel difference = %d\n", worst);
        // THE MEASUREMENT. Same sample count, same WGSL, same layout shape -- the only difference is
        // whether the module set had already been used at another count.
        check(worst == 0,
              "the module set reused across sample counts renders IDENTICALLY to a fresh one "
              "(max channel difference " + std::to_string(worst) + ")");

        setB.Release();
        setA.Release();

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    WebGpuMsaaModuleReuseProbeTest test;
    test.Run();
    return test.Result();
}
