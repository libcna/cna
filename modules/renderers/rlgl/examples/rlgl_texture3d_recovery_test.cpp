// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-062: focused volume capability, binding, shadow, and recovery proof.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"
#include "common/PixelTestGame.hpp"

#include <array>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    namespace Rlgl = CNA::Internal::Renderers::Rlgl;

    [[nodiscard]] bool SameColor(const Color& left, const Color& right)
    {
        return left.getRProperty() == right.getRProperty() &&
            left.getGProperty() == right.getGProperty() &&
            left.getBProperty() == right.getBProperty() &&
            left.getAProperty() == right.getAProperty();
    }

    class Texture3DRecoveryTest final : public CNA::Examples::PixelTestGame
    {
    public:
        Texture3DRecoveryTest()
            : graphics_(std::make_unique<GraphicsDeviceManager>(this))
        {
            graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            graphics_->setPreferredBackBufferWidthProperty(32);
            graphics_->setPreferredBackBufferHeightProperty(32);
            graphics_->setPreferredPresentationModeProperty(
                PresentationMode::NativeBackBuffer);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
        }

    protected:
        void RunTest() override
        {
            auto& device = getGraphicsDeviceProperty();
            auto& renderer = static_cast<Rlgl::RlglRenderer&>(device.GetRenderer());

            Check(device.SupportsCapability(CNA::GraphicsCapability::Texture3D) &&
                    renderer.SupportsTexture3DSamplingEXT() &&
                    renderer.GetMaxVolumeExtentForProfileEXT(
                        static_cast<int>(GraphicsProfile::HiDef)) >= 4 &&
                    renderer.ClassifyTexture3DFormatEXT(
                        static_cast<int>(SurfaceFormat::Color)) ==
                        CNA::Internal::Renderers::RendererFormatVerdict::Supported,
                "RLGL publishes the implemented Color volume capability and live extent");

            bool nonColorRefused = false;
            try
            {
                Texture3D unsupported(
                    device, 2, 2, 2, false, SurfaceFormat::Rgba64);
            }
            catch (const System::NotSupportedException&)
            {
                nonColorRefused = true;
            }
            Check(nonColorRefused,
                "Texture3D refuses formats without a complete RLGL transfer path");

            Texture3D volume(device, 4, 4, 4, true, SurfaceFormat::Color);
            std::vector<Color> levelZero(64, Color(17, 31, 47, 255));
            const std::array<Color, 8> levelOne{{
                Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                Color(0, 0, 255, 255), Color(255, 255, 0, 255),
                Color(255, 0, 255, 255), Color(0, 255, 255, 255),
                Color(91, 71, 51, 255), Color(29, 11, 193, 255)}};
            volume.SetData(levelZero.data(), static_cast<int>(levelZero.size()));
            volume.SetData(
                1, 0, 0, 2, 2, 0, 2,
                levelOne.data(), 0, static_cast<int>(levelOne.size()));

            const auto before =
                Rlgl::GetTexture3DResourceSnapshotForTesting(volume.GetRenderer());
            volume.GetRenderer().BindGL(3);
            Check(before.texture != 0 && before.width == 4 && before.height == 4 &&
                    before.depth == 4 && before.levelCount == 3 &&
                    before.recoveryRegistered &&
                    before.definedLevels == std::vector<bool>({true, true, false}) &&
                    before.recoveryLevels.size() == 3 &&
                    before.recoveryLevels[0].size() == 256 &&
                    before.recoveryLevels[1].size() == 32 &&
                    Rlgl::Bridge::GetBoundTexture3DForTesting(3) == before.texture,
                "volume allocation, mip shadows, and GL_TEXTURE_3D binding are exact");

            renderer.DebugSimulateContextLoss();
            Check(Rlgl::GetTexture3DResourceSnapshotForTesting(
                      volume.GetRenderer()).texture == 0,
                "context loss invalidates the old volume identity without GL calls");

            renderer.DebugRestoreContext();
            const auto after =
                Rlgl::GetTexture3DResourceSnapshotForTesting(volume.GetRenderer());
            std::array<Color, 8> restored{};
            volume.GetData(
                1, 0, 0, 2, 2, 0, 2,
                restored.data(), 0, static_cast<int>(restored.size()));
            bool bytesRestored = true;
            for (std::size_t index = 0; index < restored.size(); ++index)
                bytesRestored = bytesRestored && SameColor(restored[index], levelOne[index]);
            Check(after.texture != 0 && after.recoveryLevels == before.recoveryLevels &&
                    after.definedLevels == before.definedLevels && bytesRestored,
                "context recreation restores every defined volume mip exactly");

            device.SetContextRecoveryEnabled(false);
            Texture3D unregistered(device, 2, 2, 2, false, SurfaceFormat::Color);
            const std::array<Color, 8> black{{
                Color::Black, Color::Black, Color::Black, Color::Black,
                Color::Black, Color::Black, Color::Black, Color::Black}};
            unregistered.SetData(black.data(), static_cast<int>(black.size()));
            const auto disabled =
                Rlgl::GetTexture3DResourceSnapshotForTesting(unregistered.GetRenderer());
            Check(!disabled.recoveryRegistered && disabled.definedLevels.empty() &&
                    disabled.recoveryLevels.empty(),
                "recovery-disabled volumes retain no CPU shadow");
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };
}

int main()
{
    return CNA::Examples::RunPixelTest<Texture3DRecoveryTest>();
}
