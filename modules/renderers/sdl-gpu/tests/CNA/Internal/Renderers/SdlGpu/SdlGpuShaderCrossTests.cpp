// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-92: exact stock-shader route selection.

#if defined(CNA_RENDERER_SDL_GPU)

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include <gtest/gtest.h>

namespace
{
    using CNA::Internal::Renderers::SdlGpu::SdlGpuShaderCreationRouteEXT;
    using CNA::Internal::Renderers::SdlGpu::SelectSdlGpuShaderCreationRouteEXT;
}

TEST(SdlGpuShaderCrossTest, NativeSpirvRemainsThePreferredFastPath)
{
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_SPIRV,
            static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)),
        SdlGpuShaderCreationRouteEXT::DirectSpirv);
}

TEST(SdlGpuShaderCrossTest, MetalAndD3dUseCrossCompilationWhenFormatsIntersect)
{
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_MSL,
            static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)),
        SdlGpuShaderCreationRouteEXT::ShaderCross);
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_DXBC,
            static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC)),
        SdlGpuShaderCreationRouteEXT::ShaderCross);
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_DXIL,
            static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL)),
        SdlGpuShaderCreationRouteEXT::ShaderCross);
}

TEST(SdlGpuShaderCrossTest, DisjointNativeAndCompilerFormatsAreRejected)
{
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_DXIL,
            static_cast<SDL_GPUShaderFormat>(
                SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL)),
        SdlGpuShaderCreationRouteEXT::Unsupported);
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_MSL, static_cast<SDL_GPUShaderFormat>(0)),
        SdlGpuShaderCreationRouteEXT::Unsupported);
}

TEST(SdlGpuShaderCrossTest, TestOverrideExercisesCompilerOnlyWhenItCanServeTheDevice)
{
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_SPIRV, SDL_GPU_SHADERFORMAT_SPIRV, true),
        SdlGpuShaderCreationRouteEXT::ShaderCross);
    EXPECT_EQ(
        SelectSdlGpuShaderCreationRouteEXT(
            SDL_GPU_SHADERFORMAT_SPIRV, static_cast<SDL_GPUShaderFormat>(0), true),
        SdlGpuShaderCreationRouteEXT::DirectSpirv);
}

#endif  // CNA_RENDERER_SDL_GPU
