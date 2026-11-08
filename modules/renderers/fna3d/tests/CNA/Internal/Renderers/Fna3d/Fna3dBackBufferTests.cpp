// SPDX-License-Identifier: MS-PL
// GPU execution belongs to the private runner, on both FNA3D drivers.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include <gtest/gtest.h>
#include <utility>
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    void CheckSamples(GraphicsDevice& device, int portable)
    {
        // Refresh QueryMultiSampleCount IL_0019-0044 chooses supported candidates
        // downward. The renderer may select less on a device lacking 2x or 4x.
        // Native reporting is independent of CNA's public parameter write-back.
        const int native = device.GetRenderer().GetMultiSampleCount();
        EXPECT_TRUE(native == 0 || native == 2 || native == 4);
        EXPECT_LE(native, portable);
        EXPECT_EQ(device.getPresentationParametersProperty().getMultiSampleCountProperty(), native);
    }
}

TEST(Fna3dBackBufferTest, PresentationParametersNormalizeBeforeInitialNativeCreationAndReset)
{
    const std::pair<int, int> cases[] = {{0, 0}, {1, 0}, {3, 2}, {5, 4}, {7, 4}, {1024, 4}};
    for (const auto& [requested, portable] : cases)
    {
        SCOPED_TRACE(requested);
        auto parameters = PresentationParameters::CreateWindowedEXT(64, 64);
        parameters.setMultiSampleCountProperty(requested);
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
        if (dynamic_cast<CNA::Internal::Renderers::Fna3d::Fna3dRenderer*>(&device.GetRenderer()) == nullptr)
            GTEST_SKIP() << "Select FNA3D for this renderer contract";
        CheckSamples(device, portable);
        // The caller's requested parameters remain a request, not the negotiated result.
        EXPECT_EQ(parameters.getMultiSampleCountProperty(), requested);
        // This CNA store-only API cannot change native allocation. Its public count must
        // still report what exists, even if the new request would select another mode.
        auto unapplied = device.getPresentationParametersProperty().Clone();
        unapplied.setMultiSampleCountProperty(portable == 0 ? 4 : 0);
        device.SetPresentationParameters(unapplied);
        CheckSamples(device, portable);
        parameters.setMultiSampleCountProperty(0);
        device.Reset(parameters);
        CheckSamples(device, 0);
        parameters.setMultiSampleCountProperty(requested);
        device.Reset(parameters);
        CheckSamples(device, portable);
    }
}

TEST(Fna3dBackBufferTest, ManagerPreparedParametersNormalizeOnCreationAndApplyChanges)
{
    Game game;
    GraphicsDeviceManager manager(&game);
    auto& device = game.getGraphicsDeviceProperty();
    if (dynamic_cast<CNA::Internal::Renderers::Fna3d::Fna3dRenderer*>(&device.GetRenderer()) == nullptr)
        GTEST_SKIP() << "Select FNA3D for this renderer contract";
    int request = 3;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args)
    {
        args.getGraphicsDeviceInformationEXT().getPresentationParametersProperty()
            .setMultiSampleCountProperty(request);
    };
    manager.CreateDevice();
    CheckSamples(device, 2);
    for (const auto& [requested, portable] : {std::pair{5, 4}, {7, 4}, {1, 0}, {3, 2}, {0, 0}})
    {
        SCOPED_TRACE(requested);
        request = requested;
        // Mark a manager preference dirty; preparation then supplies the raw count.
        manager.setPreferMultiSamplingProperty(!manager.getPreferMultiSamplingProperty());
        manager.ApplyChanges();
        CheckSamples(device, portable);
    }
}
#endif
