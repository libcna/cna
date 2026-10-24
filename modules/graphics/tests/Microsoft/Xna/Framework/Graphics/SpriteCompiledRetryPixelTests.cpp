// SPDX-License-Identifier: MS-PL
// ISSUE-003: a failed deferred Apply must not duplicate the retained renderer run on retry.
#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class RetryPixelEffect final : public Effect
{
public:
    RetryPixelEffect(GraphicsDevice& device, const std::vector<SharpRuntime::bytecs>& bytes)
        : Effect(device, bytes) {}
    bool failApply = false;
    int applyCount = 0;
protected:
    void OnApply() override
    {
        ++applyCount;
        if (failApply)
            throw System::InvalidOperationException("Retry pixel witness Apply failure");
        Effect::OnApply();
    }
};

TEST(SpriteCompiledRetryPixelTest, DeferredApplyFailureRetriesWithoutDuplicatingTheRun)
{
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters::CreateWindowedEXT(800, 480)};
    RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "This configured renderer does not expose compiled effects";
    std::ifstream file(CNA::TestSupport::CompiledEffectDirectory() /
                       "CnaConformanceEffect.fxb", std::ios::binary);
    const std::vector<SharpRuntime::bytecs> bytes{
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    ASSERT_FALSE(bytes.empty());
    Texture2D source(device, 1, 1);
    const Color white = Color::White;
    source.SetData(&white, 1);

    const auto render = [&](bool injectFailure)
    {
        RenderTarget2D target(device, 8, 8);
        RetryPixelEffect effect(device, bytes);
        SpriteBatch batch(device);
        EXPECT_EQ(effect.getCurrentTechniqueProperty()->getPassesProperty().getCountProperty(), 2);
        device.getTexturesProperty()(0, nullptr);
        device.SetRenderTarget(&target);
        device.Clear(Color::Transparent);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    nullptr, nullptr, nullptr, &effect);
        // The fixture's vertex shader has an identity Transform: this quad covers clip space.
        // Its second pass blends with SrcAlpha, so duplicate retained quads change the pixels.
        batch.Draw(source, Rectangle(-1, -1, 2, 2), Color::White);
        effect.failApply = injectFailure;
        if (injectFailure)
        {
            EXPECT_THROW(batch.End(), System::InvalidOperationException);
            EXPECT_EQ(effect.applyCount, 1);
            effect.failApply = false;
        }
        EXPECT_NO_THROW(batch.End());
        EXPECT_EQ(effect.applyCount, injectFailure ? 3 : 2);
        device.SetRenderTarget(nullptr);
        std::vector<Color> pixels(64);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    };

    const auto control = render(false);
    ASSERT_TRUE(std::any_of(control.begin(), control.end(),
                           [](const Color& pixel) { return pixel != Color::Transparent; }));
    const auto retried = render(true);
    EXPECT_EQ(retried, control);
}
} // namespace
