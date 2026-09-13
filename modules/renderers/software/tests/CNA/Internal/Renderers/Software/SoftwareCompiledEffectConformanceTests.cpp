// SPDX-License-Identifier: MS-PL

#if defined(CNA_RENDERER_SOFTWARE) && defined(CNA_SOFTWARE_COMPILED_EFFECTS)

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

namespace
{
    using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using Microsoft::Xna::Framework::Graphics::PresentationParameters;

    template<typename Callback>
    void RunOnFreshSoftwareDevice(const char* name, Callback&& callback)
    {
        SCOPED_TRACE(name);
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::HiDef, PresentationParameters());
        ASSERT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects));
        callback(device);
    }
}

TEST(SoftwareCompiledEffectConformanceTest, RunsEverySharedCompiledEffectContract)
{
    using namespace CNA::TestSupport;

    RunOnFreshSoftwareDevice("reflection/state/parameter/lifecycle", [](GraphicsDevice& device)
    {
        RunCompiledEffectContract(device);
    });
    RunOnFreshSoftwareDevice("ordinary/indexed draw", [](GraphicsDevice& device)
    {
        RunCompiledEffectDrawContract(device);
    });
    RunOnFreshSoftwareDevice("loop", [](GraphicsDevice& device)
    {
        RunCompiledEffectLoopContract(device);
    });
    RunOnFreshSoftwareDevice("signed LOG", [](GraphicsDevice& device)
    {
        RunCompiledEffectSignedLogContract(device);
    });
    RunOnFreshSoftwareDevice("NRM write mask", [](GraphicsDevice& device)
    {
        RunCompiledEffectNrmWriteMaskContract(device);
    });
    for (const auto probe : {SyntheticCompositeWriteMaskProbe::VertexDst,
                             SyntheticCompositeWriteMaskProbe::VertexCrs})
    {
        RunOnFreshSoftwareDevice("composite write mask", [probe](GraphicsDevice& device)
        {
            RunCompiledEffectCompositeWriteMaskContract(device, probe);
        });
    }
    RunOnFreshSoftwareDevice("Shader Model 1.1 inputs", [](GraphicsDevice& device)
    {
        RunCompiledEffectShaderModel11InputContract(device);
        RunCompiledEffectShaderModel11ExtendedInputContract(device);
        RunCompiledEffectLegacyExppContract(device);
    });
    RunOnFreshSoftwareDevice("relative/dependent coordinates", [](GraphicsDevice& device)
    {
        RunCompiledEffectRelativeInputTextureCoordinateContract(device);
        RunCompiledEffectDependentTemporaryTextureCoordinateContract(device);
        RunCompiledEffectDependentTemporaryTextureCoordinate3DContract(device);
    });
    RunOnFreshSoftwareDevice("subroutines/derivatives/raster inputs", [](GraphicsDevice& device)
    {
        RunCompiledEffectSubroutineContract(device);
        RunCompiledEffectDerivativeContract(device);
        RunCompiledEffectRasterInputContract(device);
    });
    RunOnFreshSoftwareDevice("predication", [](GraphicsDevice& device)
    {
        RunCompiledEffectPredicationContract(device);
        RunCompiledEffectPredicatedTexkillContract(device);
    });
    RunOnFreshSoftwareDevice("Shader Model 1.4", [](GraphicsDevice& device)
    {
        RunCompiledEffectShaderModel14TexcrdDwContract(device);
        RunCompiledEffectShaderModel14TexldDzContract(device);
        RunCompiledEffectShaderModel14TexldDwContract(device);
        RunCompiledEffectShaderModel14TextureLoadContract(device);
        RunCompiledEffectShaderModel14PhaseContract(device);
    });
    RunOnFreshSoftwareDevice("legacy texture families", [](GraphicsDevice& device)
    {
        RunCompiledEffectLegacyTextureMatrixContract(device);
        RunCompiledEffectLegacyTextureMatrix2Contract(device);
        RunCompiledEffectLegacyTextureMatrix3SampleContract(device);
        RunCompiledEffectLegacyTextureMatrix3SpecularContract(device);
        RunCompiledEffectLegacyDepthOutputContract(device);
        RunCompiledEffectLegacyTextureRemapContract(device);
        RunCompiledEffectLegacyDependentTextureContract(device);
        RunCompiledEffectLegacyBumpEnvironmentContract(device);
    });
    RunOnFreshSoftwareDevice("multi-stream/instancing", [](GraphicsDevice& device)
    {
        RunCompiledEffectMultiStreamDrawContract(device);
        RunCompiledEffectInstancingDrawContract(device);
    });
    RunOnFreshSoftwareDevice("SpriteBatch", [](GraphicsDevice& device)
    {
        RunCompiledEffectSpriteBatchContract(device);
        RunCompiledEffectSpriteBatchMultiPassContract(device);
        RunCompiledEffectSpriteBatchTextureSlotContract(device);
        RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
    });
    RunOnFreshSoftwareDevice("samplers", [](GraphicsDevice& device)
    {
        RunCompiledEffectSamplerPixelContract(device);
        RunCompiledEffectSamplerResultSwizzleContract(device);
        RunCompiledEffectVertexSamplerContract(device);
        RunCompiledEffectVertexSamplerResultSwizzleContract(device);
        RunCompiledEffectVertexSamplerDimensionsContract(device);
        RunCompiledEffectCubeAndVolumeSamplerContract(device);
    });
    RunOnFreshSoftwareDevice("targets/state isolation", [](GraphicsDevice& device)
    {
        RunCompiledEffectPassSelectionContract(device);
        RunCompiledEffectRenderTargetSourceContract(device);
        RunCompiledEffectStockDrawIsolationContract(device);
    });
    RunOnFreshSoftwareDevice("orientation/switching/stress", [](GraphicsDevice& device)
    {
        RunCompiledEffectOrientationContract(device);
        RunCompiledEffectSwitchingContract(device);
        RunCompiledEffectManyDrawsContract(device);
        RunCompiledEffectTruncationContract(device);
    });
}

#endif
