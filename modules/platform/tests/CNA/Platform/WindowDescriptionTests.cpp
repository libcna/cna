// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/WindowDescription.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

using namespace CNA::Platform;

TEST(WindowDescriptionTests, DefaultsProduceAVisibleResizableWindowedWindow)
{
    // The common case should need only a title and a size; anything else defaulting to an
    // invisible or fixed window would make every caller restate the obvious.
    const WindowDescription description;
    EXPECT_TRUE(description.visible);
    EXPECT_TRUE(description.resizable);
    EXPECT_TRUE(description.centered);
    EXPECT_FALSE(description.borderless);
    EXPECT_EQ(description.fullscreenMode, WindowFullscreenMode::Windowed);
    EXPECT_EQ(description.renderIntent, WindowRenderIntent::None);
    EXPECT_EQ(description.openGlFramebuffer.depthBits, 0);
    EXPECT_EQ(description.openGlFramebuffer.stencilBits, 0);
    EXPECT_FALSE(description.openGlFramebuffer.doubleBuffered);
    EXPECT_EQ(description.openGlFramebuffer.samples, 0);
    EXPECT_GT(description.width, 0);
    EXPECT_GT(description.height, 0);
}

TEST(WindowDescriptionTests, SizeConstraintsDefaultToUnconstrained)
{
    // Zero means "no constraint" rather than "size zero"; a caller that sets neither must not
    // accidentally pin the window to nothing.
    const WindowDescription description;
    EXPECT_EQ(description.minimumWidth, 0);
    EXPECT_EQ(description.minimumHeight, 0);
    EXPECT_EQ(description.maximumWidth, 0);
    EXPECT_EQ(description.maximumHeight, 0);
}

TEST(WindowDescriptionTests, HighDpiIsOptInNotDefault)
{
    // Changing this silently would change the drawable size of every existing game.
    const WindowDescription description;
    EXPECT_FALSE(description.highDpi);
}

TEST(WindowDescriptionTests, RenderIntentNamesAreDistinctAndStable)
{
    const WindowRenderIntent intents[] = {WindowRenderIntent::None, WindowRenderIntent::OpenGl,
                                          WindowRenderIntent::Vulkan, WindowRenderIntent::Metal};
    std::set<std::string> names;
    for (const WindowRenderIntent intent : intents)
    {
        EXPECT_FALSE(ToString(intent).empty());
        EXPECT_EQ(&ToString(intent), &ToString(intent));
        names.insert(ToString(intent));
    }
    EXPECT_EQ(names.size(), 4u);
}

TEST(WindowDescriptionTests, FullscreenModeNamesAreDistinctAndStable)
{
    const WindowFullscreenMode modes[] = {WindowFullscreenMode::Windowed,
                                          WindowFullscreenMode::BorderlessFullscreen,
                                          WindowFullscreenMode::ExclusiveFullscreen};
    std::set<std::string> names;
    for (const WindowFullscreenMode mode : modes)
    {
        EXPECT_FALSE(ToString(mode).empty());
        EXPECT_EQ(&ToString(mode), &ToString(mode));
        names.insert(ToString(mode));
    }
    EXPECT_EQ(names.size(), 3u);
}

TEST(WindowDescriptionTests, BorderlessFullscreenIsDistinctFromExclusive)
{
    // XNA's fullscreen is exclusive; borderless is a separate, capability-gated mode. Collapsing
    // them would silently change display-mode behaviour.
    EXPECT_NE(ToString(WindowFullscreenMode::BorderlessFullscreen),
              ToString(WindowFullscreenMode::ExclusiveFullscreen));
}

} // namespace
