// SPDX-License-Identifier: MS-PL
// ISSUE-003 supplementary contract/lifetime witnesses; keep the original six cases unchanged.
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/ObjectDisposedException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

class SlotWritingEffect final : public BasicEffect
{
public:
    SlotWritingEffect(GraphicsDevice& device, Texture2D& decoy)
        : BasicEffect(device), device_(device), decoy_(decoy) {}
    std::vector<Texture*> observedBeforeApply;
protected:
    void OnApply() override
    {
        observedBeforeApply.push_back(device_.getTexturesProperty()[0]);
        BasicEffect::OnApply();
        device_.getTexturesProperty()(0, &decoy_);
    }
private:
    GraphicsDevice& device_;
    Texture2D& decoy_;
};

class SpriteTextureBindingLifetimeTest : public testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters{}};
    Texture2D source{device, 1, 1};
    Texture2D decoy{device, 1, 1};
    SpriteBatch batch{device};
    void SetUp() override
    {
        CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, SdlGpu, Fna3d, WebGPU, DirectX9);
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
        const Color white = Color::White;
        source.SetData(&white, 1);
        decoy.SetData(&white, 1);
    }
    void Draw(const Texture2D& texture)
    {
        batch.Draw(texture, Rectangle(0, 0, 1, 1), Color::White);
    }
};

TEST_F(SpriteTextureBindingLifetimeTest, StockEffectPassesObserveAndRestoreSpriteBinding)
{
    SlotWritingEffect effect(device, decoy);
    EffectTechnique* technique = effect.getCurrentTechniqueProperty();
    ASSERT_NE(technique, nullptr);
    ASSERT_EQ(technique->getPassesProperty().getCountProperty(), 1);
    technique->getPassesProperty().Add(
        EffectPass(&effect, "Additional", technique->getIdInternal(), 1));
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    Draw(source);
    EXPECT_TRUE(effect.observedBeforeApply.empty());
    batch.End();
    ASSERT_EQ(effect.observedBeforeApply.size(), 2u);
    EXPECT_EQ(effect.observedBeforeApply[0], nullptr);
    EXPECT_EQ(effect.observedBeforeApply[1], &source);
    EXPECT_EQ(device.getTexturesProperty()[0], &source);
}

TEST_F(SpriteTextureBindingLifetimeTest, ExplicitDisposalBeforeFlushRefusesTheLiveDisposedWrapper)
{
    batch.Begin();
    Draw(source);
    source.Dispose();
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
}

TEST_F(SpriteTextureBindingLifetimeTest, MoveConstructionKeepsQueuedPublicIdentityLive)
{
    batch.Begin();
    Draw(source);
    Texture2D moved(std::move(source));
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &moved);
}

TEST_F(SpriteTextureBindingLifetimeTest, MoveAssignmentKeepsQueuedPublicIdentityLive)
{
    batch.Begin();
    Draw(source);
    decoy = std::move(source);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &decoy);
}

TEST_F(SpriteTextureBindingLifetimeTest, DestroyedWrapperStillAllowsTheRetainedRendererToFlush)
{
    // CNA's C++ lifetime extension: a queued temporary may die before End. It must
    // still flush safely; there is no surviving public wrapper to expose in the slot.
    batch.Begin();
    auto temporary = std::make_unique<Texture2D>(source);
    Draw(*temporary);
    temporary.reset();
    EXPECT_NO_THROW(batch.End());
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
}

TEST_F(SpriteTextureBindingLifetimeTest, DestroyingAnImmediateTextureClearsItsPublicSlot)
{
    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    auto temporary = std::make_unique<Texture2D>(source);
    Draw(*temporary);
    EXPECT_EQ(device.getTexturesProperty()[0], temporary.get());
    temporary.reset();
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    EXPECT_NO_THROW(batch.End());
}

TEST_F(SpriteTextureBindingLifetimeTest, SharedRendererCopiesRetainTheSubmittedWrapperIdentity)
{
    Texture2D copy(source);
    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    Draw(source);
    EXPECT_EQ(device.getTexturesProperty()[0], &source);
    Draw(copy);
    EXPECT_EQ(device.getTexturesProperty()[0], &copy);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &copy);
}
} // namespace
