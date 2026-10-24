// SPDX-License-Identifier: MS-PL
// ISSUE-003: supplemental compiled-effect lifetime/retry witnesses; capability-gated.
#include <gtest/gtest.h>
#include <fstream>
#include <memory>
#include <utility>
#include "System/ObjectDisposedException.hpp"
#include "System/InvalidOperationException.hpp"
#include <iterator>
#include <string>
#include <vector>
#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
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

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

class LifetimeObservedCompiledEffect final : public Effect
{
public:
    LifetimeObservedCompiledEffect(GraphicsDevice& device,
                           const std::vector<SharpRuntime::bytecs>& bytes)
        : Effect(device, bytes), device_(device) {}
    std::vector<Texture*> observedBeforeApply;
    bool failApply = false;
protected:
    void OnApply() override
    {
        observedBeforeApply.push_back(device_.getTexturesProperty()[0]);
        if (failApply)
            throw System::InvalidOperationException("Compiled binding witness Apply failure");
        Effect::OnApply();
    }
private:
    GraphicsDevice& device_;
};

class SpriteCompiledBindingLifetimeTest : public testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(800, 480)};
    Texture2D source{device, 1, 1};
    Texture2D decoy{device, 1, 1};
    SpriteBatch batch{device};
    void SetUp() override
    {
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
        if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
            GTEST_SKIP() << "This configured renderer does not expose compiled effects";
        const Color white = Color::White;
        source.SetData(&white, 1);
        decoy.SetData(&white, 1);
    }
    std::vector<SharpRuntime::bytecs> Bytecode()
    {
        std::ifstream file(CNA::TestSupport::CompiledEffectDirectory() /
                           "CnaConformanceEffect.fxb", std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};

TEST_F(SpriteCompiledBindingLifetimeTest, DisposedBeforeFlushRetainsBeginPairForRetry)
{
    const auto bytes = Bytecode();
    ASSERT_FALSE(bytes.empty());
    LifetimeObservedCompiledEffect effect(device, bytes);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    batch.Draw(source, Rectangle(0, 0, 1, 1), Color::White);
    source.Dispose();
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
    ASSERT_EQ(effect.observedBeforeApply.size(), 1u);
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
    EXPECT_EQ(effect.observedBeforeApply.size(), 2u);
}

TEST_F(SpriteCompiledBindingLifetimeTest, SharedRendererWrappersRemainDistinctTextureRuns)
{
    // CNA's C++ copy extension: independent public wrappers can share a renderer.
    // Keeping their bindings distinct is not a claim that XNA has value-copy textures.
    const auto bytes = Bytecode();
    ASSERT_FALSE(bytes.empty());
    LifetimeObservedCompiledEffect effect(device, bytes);
    ASSERT_EQ(effect.getCurrentTechniqueProperty()->getPassesProperty().getCountProperty(), 2);
    Texture2D copy(source);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    batch.Draw(source, Rectangle(0, 0, 1, 1), Color::White);
    batch.Draw(copy, Rectangle(1, 0, 1, 1), Color::White);
    batch.End();
    ASSERT_EQ(effect.observedBeforeApply.size(), 4u);
    EXPECT_EQ(effect.observedBeforeApply[0], nullptr);
    EXPECT_EQ(effect.observedBeforeApply[1], &source);
    EXPECT_EQ(effect.observedBeforeApply[2], &source);
    EXPECT_EQ(effect.observedBeforeApply[3], &copy);
    EXPECT_EQ(device.getTexturesProperty()[0], &copy);
}

TEST_F(SpriteCompiledBindingLifetimeTest, DestroyedWrapperAllowsRetainedCompiledRendererToFlush)
{
    const auto bytes = Bytecode();
    ASSERT_FALSE(bytes.empty());
    LifetimeObservedCompiledEffect effect(device, bytes);
    auto temporary = std::make_unique<Texture2D>(source);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    batch.Draw(*temporary, Rectangle(0, 0, 1, 1), Color::White);
    temporary.reset();
    EXPECT_NO_THROW(batch.End());
    ASSERT_EQ(effect.observedBeforeApply.size(), 2u);
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
}

TEST_F(SpriteCompiledBindingLifetimeTest, MovedWrapperIsPublishedAfterCompiledPasses)
{
    const auto bytes = Bytecode();
    ASSERT_FALSE(bytes.empty());
    LifetimeObservedCompiledEffect effect(device, bytes);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    batch.Draw(source, Rectangle(0, 0, 1, 1), Color::White);
    Texture2D moved(std::move(source));
    batch.End();
    ASSERT_EQ(effect.observedBeforeApply.size(), 2u);
    EXPECT_EQ(effect.observedBeforeApply[0], nullptr);
    EXPECT_EQ(effect.observedBeforeApply[1], &moved);
    EXPECT_EQ(device.getTexturesProperty()[0], &moved);
}

TEST_F(SpriteCompiledBindingLifetimeTest, FailedImmediateDrawIsNotReplayedByEnd)
{
    const auto bytes = Bytecode();
    ASSERT_FALSE(bytes.empty());
    LifetimeObservedCompiledEffect effect(device, bytes);
    effect.failApply = true;
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    EXPECT_THROW(batch.Draw(source, Rectangle(0, 0, 1, 1), Color::White),
                 System::InvalidOperationException);
    ASSERT_EQ(effect.observedBeforeApply.size(), 1u);
    effect.failApply = false;
    EXPECT_NO_THROW(batch.End());
    EXPECT_EQ(effect.observedBeforeApply.size(), 1u);
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    EXPECT_NO_THROW(batch.Draw(source, Rectangle(0, 0, 1, 1), Color::White));
    EXPECT_NO_THROW(batch.End());
    EXPECT_EQ(effect.observedBeforeApply.size(), 3u);
    EXPECT_EQ(device.getTexturesProperty()[0], &source);
}
} // namespace
