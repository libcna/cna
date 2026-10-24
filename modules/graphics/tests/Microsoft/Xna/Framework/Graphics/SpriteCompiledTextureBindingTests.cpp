// SPDX-License-Identifier: MS-PL
// ISSUE-003: compiled-effect passes also require public binding after each Apply.
#include <gtest/gtest.h>
#include <fstream>
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

class ObservedCompiledEffect final : public Effect
{
public:
    ObservedCompiledEffect(GraphicsDevice& device,
                           const std::vector<SharpRuntime::bytecs>& bytes)
        : Effect(device, bytes), device_(device) {}
    std::vector<Texture*> observedBeforeApply;
protected:
    void OnApply() override
    {
        observedBeforeApply.push_back(device_.getTexturesProperty()[0]);
        Effect::OnApply();
    }
private:
    GraphicsDevice& device_;
};

class SpriteCompiledTextureBindingTest : public testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(800, 480)};
    Texture2D source{device, 1, 1};
    Texture2D decoy{device, 1, 1};
    SpriteBatch batch{device};
    void SetUp() override
    {
        CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, SdlGpu, Fna3d, WebGPU, DirectX9);
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
    void CheckPasses(SpriteSortMode mode)
    {
        const auto bytes = Bytecode();
        ASSERT_FALSE(bytes.empty());
        ObservedCompiledEffect effect(device, bytes);
        ASSERT_EQ(effect.getCurrentTechniqueProperty()->getPassesProperty().getCountProperty(), 2);
        EffectParameter* texture = effect.getParametersProperty()["FxTexture"];
        ASSERT_NE(texture, nullptr);
        texture->SetValue(&decoy);
        batch.Begin(mode, &BlendState::Opaque, nullptr, nullptr, nullptr, &effect);
        EXPECT_TRUE(effect.observedBeforeApply.empty());
        batch.Draw(source, Rectangle(0, 0, 1, 1), Color::White);
        if (mode == SpriteSortMode::Deferred)
        {
            EXPECT_TRUE(effect.observedBeforeApply.empty());
        }
        else
        {
            EXPECT_EQ(effect.observedBeforeApply.size(), 2u);
            EXPECT_EQ(device.getTexturesProperty()[0], &source);
        }
        batch.End();
        ASSERT_EQ(effect.observedBeforeApply.size(), 2u);
        EXPECT_EQ(effect.observedBeforeApply[0], nullptr);
        EXPECT_EQ(effect.observedBeforeApply[1], &source);
        EXPECT_EQ(device.getTexturesProperty()[0], &source);
    }
};

TEST_F(SpriteCompiledTextureBindingTest, DeferredPublishesAfterEachCompiledPass)
{
    CheckPasses(SpriteSortMode::Deferred);
}

TEST_F(SpriteCompiledTextureBindingTest, ImmediatePublishesAfterEachCompiledPass)
{
    CheckPasses(SpriteSortMode::Immediate);
}

TEST_F(SpriteCompiledTextureBindingTest, EmptyCompiledBatchPreservesExistingBinding)
{
    const auto bytes = Bytecode();
    ASSERT_FALSE(bytes.empty());
    ObservedCompiledEffect effect(device, bytes);
    device.getTexturesProperty()(0, &decoy);
    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
    batch.End();
    EXPECT_TRUE(effect.observedBeforeApply.empty());
    EXPECT_EQ(device.getTexturesProperty()[0], &decoy);
}
} // namespace
