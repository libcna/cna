// SPDX-License-Identifier: MS-PL
// The system overlay runs after title Draw. Its SpriteBatch must not change the next title frame.
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "../../../../../src/Internal/Guide/GuideUi.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace Service = CNA::Internal::GamerServices;
namespace Ui = CNA::Internal::GamerServices::GuideUi;
namespace Graphics = Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::PlayerIndex;

namespace {
class GuideRenderStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        Ui::closeAll();
        Service::setGuideNotificationClockForTesting([this] { return now_; });
        drain();
        device_ = std::make_unique<Graphics::GraphicsDevice>();
        blend_ = Graphics::BlendState(Graphics::BlendState::Opaque);
        depth_ = Graphics::DepthStencilState(Graphics::DepthStencilState::Default);
        rasterizer_ = Graphics::RasterizerState(Graphics::RasterizerState::CullClockwise);
        sampler_ = Graphics::SamplerState(Graphics::SamplerState::PointWrap);
        device_->setBlendStateProperty(blend_);
        device_->setDepthStencilStateProperty(depth_);
        device_->setRasterizerStateProperty(rasterizer_);
        device_->getSamplerStatesProperty()[0] = sampler_;
    }
    void TearDown() override {
        Ui::closeAll();
        drain();
        Ui::releaseDeviceResources();
        device_.reset();
        Service::setGuideNotificationClockForTesting({});
    }
    void drain() {
        for (int step = 0; step < 32 && !Service::guideNotifications().empty(); ++step)
            now_ += std::chrono::seconds(5);
    }
    void expectTitleState() {
        EXPECT_EQ(Graphics::Blend::One, device_->getBlendStateProperty().getColorSourceBlendProperty());
        EXPECT_EQ(Graphics::Blend::Zero, device_->getBlendStateProperty().getColorDestinationBlendProperty());
        EXPECT_TRUE(device_->getDepthStencilStateProperty().getDepthBufferEnableProperty());
        EXPECT_TRUE(device_->getDepthStencilStateProperty().getDepthBufferWriteEnableProperty());
        EXPECT_EQ(Graphics::CullMode::CullClockwiseFace, device_->getRasterizerStateProperty().getCullModeProperty());
        const auto& sampler = device_->getSamplerStatesProperty()[0];
        EXPECT_EQ(Graphics::TextureFilter::Point, sampler.getFilterProperty());
        EXPECT_EQ(Graphics::TextureAddressMode::Wrap, sampler.getAddressUProperty());
        EXPECT_EQ(Graphics::TextureAddressMode::Wrap, sampler.getAddressVProperty());
    }
    void expectOriginalStateReferences() {
        // Assignment retains XNA reference semantics; a copy construction would silently detach them.
        blend_.Dispose(); depth_.Dispose(); rasterizer_.Dispose(); sampler_.Dispose();
        EXPECT_TRUE(device_->getBlendStateProperty().getIsDisposedProperty());
        EXPECT_TRUE(device_->getDepthStencilStateProperty().getIsDisposedProperty());
        EXPECT_TRUE(device_->getRasterizerStateProperty().getIsDisposedProperty());
        EXPECT_TRUE(device_->getSamplerStatesProperty()[0].getIsDisposedProperty());
        device_->setBlendStateProperty(Graphics::BlendState::Opaque);
        device_->setDepthStencilStateProperty(Graphics::DepthStencilState::Default);
        device_->setRasterizerStateProperty(Graphics::RasterizerState::CullCounterClockwise);
        device_->getSamplerStatesProperty()[0] = Graphics::SamplerState::LinearWrap;
    }
    std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now();
    std::unique_ptr<Graphics::GraphicsDevice> device_;
    Graphics::BlendState blend_;
    Graphics::DepthStencilState depth_;
    Graphics::RasterizerState rasterizer_;
    Graphics::SamplerState sampler_;
};
}

TEST_F(GuideRenderStateTest, NotificationRestoresTitleStatesAndReferences) {
    Service::postGuideNotification("Sample94 signed in");
    Ui::draw(*device_);
    expectTitleState();
    drain();
    Ui::draw(*device_); // Expiring the toast must not leave its state behind either.
    expectTitleState();
    expectOriginalStateReferences();
}

TEST_F(GuideRenderStateTest, GuideScreenRestoresTitleStatesAndReferences) {
    Ui::open(Ui::homeScreen(PlayerIndex::One), PlayerIndex::One);
    Ui::draw(*device_);
    expectTitleState();
    Ui::closeAll();
    Ui::draw(*device_);
    expectTitleState();
    expectOriginalStateReferences();
}
