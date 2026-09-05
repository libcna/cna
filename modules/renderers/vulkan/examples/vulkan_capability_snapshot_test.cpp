// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-008: this renderer's capability reporting, captured as a checked-in
// snapshot, and the oracle VULKAN-470 re-verifies against.
//
// **Read through the `GraphicsDevice` seam, never the renderer's own switch.** Six of the nineteen
// `GraphicsCapability` members are answered ABOVE the renderer -- `GraphicsDevice::SupportsCapability`
// resolves them itself -- so a snapshot taken from `IGraphicsRenderer` would record answers no
// caller can ever observe, and miss the ones every caller gets.
//
// The snapshot lives in this file rather than in a data file beside it. That is deliberate: it
// cannot go missing, it cannot be found by the wrong path from a build directory, and a change to
// it shows up in the same diff as the change that caused it.
//
// **Device dependence, stated rather than assumed.** Some of these answers come from the physical
// device (`VULKAN-020`/`-021` read `MultipleRenderTargets` and `MultiSampleAntiAliasing` from
// `VkPhysicalDeviceLimits`, anisotropy from a device feature, the float-format rows from format
// properties). The recorded values are therefore keyed to the device that produced them. On a
// device this file has no section for, the test still checks that **every** entry answers and that
// the structural invariants hold, and prints the measured snapshot so the section can be added --
// it does not silently skip, and it does not manufacture a failure out of a different GPU.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    std::string Show(bool v) { return v ? "true" : "false"; }
    std::string Show(int v)  { return std::to_string(v); }

    std::string ShowSupport(CNA::RendererFeatureSupport s)
    {
        switch (s) {
            case CNA::RendererFeatureSupport::Unknown:     return "unknown";
            case CNA::RendererFeatureSupport::Unsupported: return "unsupported";
            case CNA::RendererFeatureSupport::Supported:   return "supported";
            case CNA::RendererFeatureSupport::Restricted:  return "restricted";
        }
        return "?";
    }

    std::string ShowLimit(CNA::RendererLimitValue v)
    {
        return v.known ? std::to_string(v.value) : "unknown";
    }

    std::string ShowDialect(CNA::Internal::Renderers::ShaderDialectEXT d)
    {
        using D = CNA::Internal::Renderers::ShaderDialectEXT;
        switch (d) {
            case D::Unknown:     return "Unknown";
            case D::GlslDesktop: return "GlslDesktop";
            case D::GlslEs:      return "GlslEs";
            case D::GlslVulkan:  return "GlslVulkan";
            case D::Hlsl:        return "Hlsl";
            case D::Msl:         return "Msl";
            case D::Wgsl:        return "Wgsl";
        }
        return "?";
    }

    struct Entry
    {
        const char* name;
        std::function<std::string(GraphicsDevice&)> read;
    };

    /// Where an answer comes from, which decides how hard the snapshot may hold it.
    enum class Origin
    {
        /// A decision this renderer or this build made. It cannot differ between two machines, so
        /// a difference is a change to CNA and the snapshot fails on it.
        Fixed,
        /// Read from the physical device -- `VkPhysicalDeviceLimits`, a device feature, or format
        /// properties. Recorded, reported, and NOT failed on: another GPU is not a regression.
        Device,
    };

    struct Recorded
    {
        const char* name;
        const char* value;
        Origin origin;
    };

    /// The checked-in snapshot. Measured 2026-09-05 on **llvmpipe (LLVM 19.1.7, 256 bits)** under
    /// Xvfb, which is the device this project's virtual display provides; see plan_vulkan.md §8.
    ///
    /// It is a C++ table rather than a data file on purpose: it cannot go missing, no build
    /// directory can resolve the wrong path to it, and a change to a recorded value lands in the
    /// same diff as the change that caused it.
    const Recorded kRecorded[] = {
        { "capability.AdditiveBlending", "true", Origin::Fixed },
        { "capability.AnisotropicFiltering", "true", Origin::Device },
        { "capability.CompiledEffects", "false", Origin::Fixed },
        { "capability.ComputeShaders", "false", Origin::Fixed },
        { "capability.CustomEffects", "true", Origin::Fixed },
        { "capability.DepthStencilBuffer", "true", Origin::Fixed },
        { "capability.FloatRenderTargets", "false", Origin::Device },
        { "capability.HalfFloatRenderTargets", "false", Origin::Device },
        { "capability.HalfFloatTextureLinearFiltering", "false", Origin::Device },
        { "capability.IndirectDraw", "false", Origin::Fixed },
        { "capability.Instancing", "true", Origin::Fixed },
        { "capability.MultiSampleAntiAliasing", "true", Origin::Device },
        { "capability.MultiStreamVertexInput", "false", Origin::Fixed },
        { "capability.MultipleRenderTargets", "true", Origin::Device },
        { "capability.OcclusionQuery", "true", Origin::Fixed },
        { "capability.StencilBuffer", "true", Origin::Fixed },
        { "capability.Texture3D", "true", Origin::Fixed },
        { "capability.ThreeD", "true", Origin::Fixed },
        { "capability.WireFrame", "true", Origin::Fixed },
        { "feature.AdditiveBlending", "supported", Origin::Fixed },
        { "feature.AnisotropicFiltering", "supported", Origin::Device },
        { "feature.CompiledXnaEffects", "unsupported", Origin::Fixed },
        { "feature.ComputeImageBinding", "unsupported", Origin::Fixed },
        { "feature.ComputeShaders", "unsupported", Origin::Fixed },
        { "feature.DepthStencilBuffer", "supported", Origin::Fixed },
        { "feature.Float16RenderTargets", "unsupported", Origin::Device },
        { "feature.Float16TextureLinearFiltering", "unsupported", Origin::Device },
        { "feature.Float32RenderTargets", "unsupported", Origin::Device },
        { "feature.GpuTimers", "unsupported", Origin::Fixed },
        { "feature.ImageBasedLighting", "unsupported", Origin::Fixed },
        { "feature.IndirectDrawing", "unsupported", Origin::Fixed },
        { "feature.InstancedDrawing", "supported", Origin::Fixed },
        { "feature.MultiSampleAntiAliasing", "supported", Origin::Device },
        { "feature.MultiStreamVertexInput", "unsupported", Origin::Fixed },
        { "feature.MultipleRenderTargets", "supported", Origin::Device },
        { "feature.OcclusionQueries", "supported", Origin::Fixed },
        { "feature.ShaderDialectGlslDesktop", "unsupported", Origin::Fixed },
        { "feature.ShaderDialectGlslEs", "unsupported", Origin::Fixed },
        { "feature.ShaderDialectGlslVulkan", "unsupported", Origin::Fixed },
        { "feature.ShaderDialectHlsl", "unsupported", Origin::Fixed },
        { "feature.ShaderDialectMsl", "unsupported", Origin::Fixed },
        { "feature.ShaderDialectWgsl", "unsupported", Origin::Fixed },
        { "feature.ShaderEffectSourceExecution", "unsupported", Origin::Fixed },
        { "feature.ShaderEffects", "supported", Origin::Fixed },
        { "feature.ShadowSampling", "unsupported", Origin::Fixed },
        { "feature.StencilBuffer", "supported", Origin::Fixed },
        { "feature.Texture3DStorage", "supported", Origin::Fixed },
        { "feature.ThreeDimensionalPipeline", "supported", Origin::Fixed },
        { "feature.WireFrameRasterization", "supported", Origin::Fixed },
        { "limit.MaxComputeWorkGroupCountX", "0", Origin::Fixed },
        { "limit.MaxComputeWorkGroupCountY", "0", Origin::Fixed },
        { "limit.MaxComputeWorkGroupCountZ", "0", Origin::Fixed },
        { "limit.MaxComputeWorkGroupInvocations", "0", Origin::Fixed },
        { "limit.MaxComputeWorkGroupSizeX", "0", Origin::Fixed },
        { "limit.MaxComputeWorkGroupSizeY", "0", Origin::Fixed },
        { "limit.MaxComputeWorkGroupSizeZ", "0", Origin::Fixed },
        { "limit.MaxTextureDimension", "16384", Origin::Device },
        { "limit.MaxVertexShaderStorageBlocks", "0", Origin::Fixed },
        { "limit.MaxVertexStreams", "1", Origin::Fixed },
        { "query.executesShaderEffectSource", "false", Origin::Fixed },
        { "query.maxComputeWorkGroupInvocations", "0", Origin::Fixed },
        { "query.maxTextureDimension", "16384", Origin::Device },
        { "query.rendererName", "VULKAN", Origin::Fixed },
        { "query.shaderDialect", "GlslVulkan", Origin::Fixed },
        { "query.supportsImageBasedLighting", "false", Origin::Fixed },
        { "query.supportsShadowSampling", "false", Origin::Fixed },
        { "renderTargetFormat.Alpha8", "false", Origin::Device },
        { "renderTargetFormat.Bc7EXT", "false", Origin::Device },
        { "renderTargetFormat.Bc7SrgbEXT", "false", Origin::Device },
        { "renderTargetFormat.Bgr565", "false", Origin::Device },
        { "renderTargetFormat.Bgra4444", "false", Origin::Device },
        { "renderTargetFormat.Bgra5551", "false", Origin::Device },
        { "renderTargetFormat.ByteEXT", "false", Origin::Device },
        { "renderTargetFormat.Color", "true", Origin::Device },
        { "renderTargetFormat.ColorBgraEXT", "false", Origin::Device },
        { "renderTargetFormat.ColorSrgbEXT", "false", Origin::Device },
        { "renderTargetFormat.Dxt1", "false", Origin::Device },
        { "renderTargetFormat.Dxt3", "false", Origin::Device },
        { "renderTargetFormat.Dxt5", "false", Origin::Device },
        { "renderTargetFormat.Dxt5SrgbEXT", "false", Origin::Device },
        { "renderTargetFormat.HalfSingle", "false", Origin::Device },
        { "renderTargetFormat.HalfVector2", "false", Origin::Device },
        { "renderTargetFormat.HalfVector4", "false", Origin::Device },
        { "renderTargetFormat.HdrBlendable", "false", Origin::Device },
        { "renderTargetFormat.NormalizedByte2", "false", Origin::Device },
        { "renderTargetFormat.NormalizedByte4", "false", Origin::Device },
        { "renderTargetFormat.Rg32", "false", Origin::Device },
        { "renderTargetFormat.Rgba1010102", "false", Origin::Device },
        { "renderTargetFormat.Rgba64", "false", Origin::Device },
        { "renderTargetFormat.Single", "false", Origin::Device },
        { "renderTargetFormat.UShortEXT", "false", Origin::Device },
        { "renderTargetFormat.Vector2", "false", Origin::Device },
        { "renderTargetFormat.Vector4", "false", Origin::Device },
    };
}

class VulkanCapabilitySnapshotTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        const std::vector<Entry> entries = {
        { "capability.ThreeD", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::ThreeD)); } },
        { "capability.DepthStencilBuffer", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::DepthStencilBuffer)); } },
        { "capability.MultiSampleAntiAliasing", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing)); } },
        { "capability.MultipleRenderTargets", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets)); } },
        { "capability.AnisotropicFiltering", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering)); } },
        { "capability.WireFrame", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::WireFrame)); } },
        { "capability.OcclusionQuery", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery)); } },
        { "capability.CustomEffects", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::CustomEffects)); } },
        { "capability.Texture3D", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::Texture3D)); } },
        { "capability.MultiStreamVertexInput", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput)); } },
        { "capability.Instancing", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::Instancing)); } },
        { "capability.StencilBuffer", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::StencilBuffer)); } },
        { "capability.AdditiveBlending", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending)); } },
        { "capability.CompiledEffects", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::CompiledEffects)); } },
        { "capability.FloatRenderTargets", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::FloatRenderTargets)); } },
        { "capability.HalfFloatRenderTargets", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets)); } },
        { "capability.HalfFloatTextureLinearFiltering", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::HalfFloatTextureLinearFiltering)); } },
        { "capability.ComputeShaders", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::ComputeShaders)); } },
        { "capability.IndirectDraw", [](GraphicsDevice& d) { return Show(d.SupportsCapability(CNA::GraphicsCapability::IndirectDraw)); } },
        { "feature.ThreeDimensionalPipeline", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ThreeDimensionalPipeline)); } },
        { "feature.DepthStencilBuffer", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::DepthStencilBuffer)); } },
        { "feature.MultiSampleAntiAliasing", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::MultiSampleAntiAliasing)); } },
        { "feature.MultipleRenderTargets", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::MultipleRenderTargets)); } },
        { "feature.AnisotropicFiltering", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::AnisotropicFiltering)); } },
        { "feature.WireFrameRasterization", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::WireFrameRasterization)); } },
        { "feature.OcclusionQueries", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::OcclusionQueries)); } },
        { "feature.ShaderEffects", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderEffects)); } },
        { "feature.ShaderEffectSourceExecution", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderEffectSourceExecution)); } },
        { "feature.Texture3DStorage", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::Texture3DStorage)); } },
        { "feature.MultiStreamVertexInput", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::MultiStreamVertexInput)); } },
        { "feature.InstancedDrawing", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::InstancedDrawing)); } },
        { "feature.StencilBuffer", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::StencilBuffer)); } },
        { "feature.AdditiveBlending", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::AdditiveBlending)); } },
        { "feature.CompiledXnaEffects", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::CompiledXnaEffects)); } },
        { "feature.Float32RenderTargets", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::Float32RenderTargets)); } },
        { "feature.Float16RenderTargets", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::Float16RenderTargets)); } },
        { "feature.Float16TextureLinearFiltering", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::Float16TextureLinearFiltering)); } },
        { "feature.ComputeShaders", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ComputeShaders)); } },
        { "feature.ComputeImageBinding", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ComputeImageBinding)); } },
        { "feature.IndirectDrawing", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::IndirectDrawing)); } },
        { "feature.ShadowSampling", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShadowSampling)); } },
        { "feature.ImageBasedLighting", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ImageBasedLighting)); } },
        { "feature.GpuTimers", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::GpuTimers)); } },
        { "feature.ShaderDialectGlslDesktop", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderDialectGlslDesktop)); } },
        { "feature.ShaderDialectGlslEs", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderDialectGlslEs)); } },
        { "feature.ShaderDialectGlslVulkan", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderDialectGlslVulkan)); } },
        { "feature.ShaderDialectHlsl", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderDialectHlsl)); } },
        { "feature.ShaderDialectMsl", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderDialectMsl)); } },
        { "feature.ShaderDialectWgsl", [](GraphicsDevice& d) { return ShowSupport(d.GetRendererFeatureSupportEXT(CNA::RendererFeature::ShaderDialectWgsl)); } },
        { "limit.MaxTextureDimension", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureDimension)); } },
        { "limit.MaxVertexStreams", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxVertexStreams)); } },
        { "limit.MaxComputeWorkGroupCountX", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupCountX)); } },
        { "limit.MaxComputeWorkGroupCountY", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupCountY)); } },
        { "limit.MaxComputeWorkGroupCountZ", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupCountZ)); } },
        { "limit.MaxComputeWorkGroupSizeX", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupSizeX)); } },
        { "limit.MaxComputeWorkGroupSizeY", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupSizeY)); } },
        { "limit.MaxComputeWorkGroupSizeZ", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupSizeZ)); } },
        { "limit.MaxComputeWorkGroupInvocations", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxComputeWorkGroupInvocations)); } },
        { "limit.MaxVertexShaderStorageBlocks", [](GraphicsDevice& d) { return ShowLimit(d.GetRendererLimitEXT(CNA::RendererLimit::MaxVertexShaderStorageBlocks)); } },
        { "renderTargetFormat.Color", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Color)); } },
        { "renderTargetFormat.Bgr565", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Bgr565)); } },
        { "renderTargetFormat.Bgra5551", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Bgra5551)); } },
        { "renderTargetFormat.Bgra4444", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Bgra4444)); } },
        { "renderTargetFormat.Dxt1", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt1)); } },
        { "renderTargetFormat.Dxt3", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt3)); } },
        { "renderTargetFormat.Dxt5", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt5)); } },
        { "renderTargetFormat.NormalizedByte2", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::NormalizedByte2)); } },
        { "renderTargetFormat.NormalizedByte4", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::NormalizedByte4)); } },
        { "renderTargetFormat.Rgba1010102", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rgba1010102)); } },
        { "renderTargetFormat.Rg32", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rg32)); } },
        { "renderTargetFormat.Rgba64", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Rgba64)); } },
        { "renderTargetFormat.Alpha8", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)); } },
        { "renderTargetFormat.Single", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single)); } },
        { "renderTargetFormat.Vector2", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector2)); } },
        { "renderTargetFormat.Vector4", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4)); } },
        { "renderTargetFormat.HalfSingle", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle)); } },
        { "renderTargetFormat.HalfVector2", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector2)); } },
        { "renderTargetFormat.HalfVector4", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector4)); } },
        { "renderTargetFormat.HdrBlendable", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable)); } },
        { "renderTargetFormat.ColorBgraEXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::ColorBgraEXT)); } },
        { "renderTargetFormat.ColorSrgbEXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::ColorSrgbEXT)); } },
        { "renderTargetFormat.Dxt5SrgbEXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt5SrgbEXT)); } },
        { "renderTargetFormat.Bc7EXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Bc7EXT)); } },
        { "renderTargetFormat.Bc7SrgbEXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Bc7SrgbEXT)); } },
        { "renderTargetFormat.ByteEXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::ByteEXT)); } },
        { "renderTargetFormat.UShortEXT", [](GraphicsDevice& d) { return Show(d.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::UShortEXT)); } },
            { "query.shaderDialect", [](GraphicsDevice& d) {
                  return ShowDialect(d.GetShaderDialectEXT()); } },
            { "query.executesShaderEffectSource", [](GraphicsDevice& d) {
                  return Show(d.ExecutesShaderEffectSourceEXT()); } },
            { "query.supportsShadowSampling", [](GraphicsDevice& d) {
                  return Show(d.SupportsShadowSamplingEXT()); } },
            { "query.supportsImageBasedLighting", [](GraphicsDevice& d) {
                  return Show(d.SupportsImageBasedLightingEXT()); } },
            { "query.maxTextureDimension", [](GraphicsDevice& d) {
                  return Show(d.GetMaxTextureDimension()); } },
            { "query.maxComputeWorkGroupInvocations", [](GraphicsDevice& d) {
                  return Show(d.GetMaxComputeWorkGroupInvocationsEXT()); } },
            { "query.rendererName", [](GraphicsDevice& d) {
                  return std::string(d.GetGraphicsRendererName()); } },
        };

        std::map<std::string, std::string> measured;
        for (const Entry& e : entries) {
            measured[e.name] = e.read(dev);
        }

        std::printf("--- BEGIN Vulkan capability snapshot ---\n");
        for (const auto& [name, value] : measured) {
            std::printf("%s = %s\n", name.c_str(), value.c_str());
        }
        std::printf("--- END Vulkan capability snapshot (%zu entries) ---\n", measured.size());
        std::fflush(stdout);

        check(measured.size() == entries.size(),
              "A every capability, feature, limit, format and query answered exactly once",
              std::to_string(measured.size()) + " distinct names from "
                  + std::to_string(entries.size()) + " entries");

        // ---- B: the recorded snapshot still describes this renderer ------------------------------
        int fixedChecked = 0;
        int fixedDrift = 0;
        int deviceChecked = 0;
        int deviceDiffers = 0;
        std::string firstFixedDrift;
        std::string deviceNotes;
        std::vector<std::string> missing;
        for (const Recorded& r : kRecorded) {
            const auto it = measured.find(r.name);
            if (it == measured.end()) { missing.emplace_back(r.name); continue; }
            if (r.origin == Origin::Fixed) {
                ++fixedChecked;
                if (it->second != r.value) {
                    ++fixedDrift;
                    if (firstFixedDrift.empty()) {
                        firstFixedDrift = std::string(r.name) + ": recorded " + r.value
                                        + ", measured " + it->second;
                    }
                }
            } else {
                ++deviceChecked;
                if (it->second != r.value) {
                    ++deviceDiffers;
                    deviceNotes += std::string("\n    ") + r.name + ": recorded " + r.value
                                 + ", this device " + it->second;
                }
            }
        }

        check(missing.empty(),
              "B every recorded entry is still present in the live snapshot",
              missing.empty() ? std::to_string(fixedChecked + deviceChecked) + " matched by name"
                              : std::to_string(missing.size()) + " gone, first: " + missing.front());
        check(static_cast<std::size_t>(fixedChecked + deviceChecked) == measured.size(),
              "B and nothing answers that the snapshot does not record",
              std::to_string(measured.size()) + " measured, "
                  + std::to_string(fixedChecked + deviceChecked) + " recorded");
        check(fixedDrift == 0,
              "B every renderer-fixed answer matches the snapshot",
              fixedDrift == 0 ? std::to_string(fixedChecked) + " fixed entries unchanged"
                              : std::to_string(fixedDrift) + " drifted; " + firstFixedDrift);
        // Device-dependent answers are REPORTED, never failed on. A different GPU is not a
        // regression, and a snapshot that goes red on the owner's desktop teaches nothing.
        std::printf("[INFO] %d device-dependent entries; %d differ from the recorded llvmpipe "
                    "values%s\n", deviceChecked, deviceDiffers,
                    deviceNotes.empty() ? "" : deviceNotes.c_str());

        // ---- C: invariants that must hold on ANY device -------------------------------------------
        // A capability and the profile feature that mirrors it cannot disagree: they are the same
        // question asked at two levels, and a caller may reasonably read either.
        const std::pair<const char*, const char*> kMirrors[] = {
            { "capability.ThreeD",                  "feature.ThreeDimensionalPipeline" },
            { "capability.DepthStencilBuffer",      "feature.DepthStencilBuffer" },
            { "capability.MultiSampleAntiAliasing", "feature.MultiSampleAntiAliasing" },
            { "capability.MultipleRenderTargets",   "feature.MultipleRenderTargets" },
            { "capability.AnisotropicFiltering",    "feature.AnisotropicFiltering" },
            { "capability.WireFrame",               "feature.WireFrameRasterization" },
            { "capability.OcclusionQuery",          "feature.OcclusionQueries" },
            { "capability.CustomEffects",           "feature.ShaderEffects" },
            { "capability.Texture3D",               "feature.Texture3DStorage" },
            { "capability.MultiStreamVertexInput",  "feature.MultiStreamVertexInput" },
            { "capability.Instancing",              "feature.InstancedDrawing" },
            { "capability.StencilBuffer",           "feature.StencilBuffer" },
            { "capability.AdditiveBlending",        "feature.AdditiveBlending" },
            { "capability.CompiledEffects",         "feature.CompiledXnaEffects" },
            { "capability.FloatRenderTargets",      "feature.Float32RenderTargets" },
            { "capability.HalfFloatRenderTargets",  "feature.Float16RenderTargets" },
            { "capability.HalfFloatTextureLinearFiltering",
              "feature.Float16TextureLinearFiltering" },
            { "capability.ComputeShaders",          "feature.ComputeShaders" },
            { "capability.IndirectDraw",            "feature.IndirectDrawing" },
        };
        int mirrorsChecked = 0;
        std::string mirrorMismatch;
        for (const auto& [capName, featName] : kMirrors) {
            const bool cap  = measured[capName] == "true";
            const bool feat = measured[featName] == "supported";
            ++mirrorsChecked;
            if (cap != feat && mirrorMismatch.empty()) {
                mirrorMismatch = std::string(capName) + "=" + measured[capName] + " but "
                               + featName + "=" + measured[featName];
            }
        }
        check(mirrorMismatch.empty(),
              "C no capability disagrees with the profile feature that mirrors it",
              mirrorMismatch.empty() ? std::to_string(mirrorsChecked) + " pairs agree"
                                     : mirrorMismatch);

        check((measured["capability.ComputeShaders"] == "true")
                  == (measured["limit.MaxComputeWorkGroupInvocations"] != "0"),
              "C compute limits are non-zero exactly when compute is reported",
              "capability=" + measured["capability.ComputeShaders"] + " invocations="
                  + measured["limit.MaxComputeWorkGroupInvocations"]);
        check((measured["capability.MultiStreamVertexInput"] == "true")
                  || measured["limit.MaxVertexStreams"] == "1",
              "C a renderer that refuses multi-stream input reports exactly one stream",
              "capability=" + measured["capability.MultiStreamVertexInput"] + " streams="
                  + measured["limit.MaxVertexStreams"]);
        check(measured["limit.MaxTextureDimension"] == measured["query.maxTextureDimension"],
              "C the limit table and the direct query give the same maximum texture dimension",
              measured["limit.MaxTextureDimension"] + " vs " + measured["query.maxTextureDimension"]);
        check(measured["renderTargetFormat.Color"] == "true",
              "C SurfaceFormat::Color is usable as a render target", measured["renderTargetFormat.Color"]);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanCapabilitySnapshotTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanCapabilitySnapshotTest g;
    g.Run();
    return g.getResult();
}
