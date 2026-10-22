// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include <gtest/gtest.h>

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::Viewport;

namespace
{
    template <class T> void CheckInternalMembers()
    {
        EXPECT_FALSE(requires { &T::BuildAssetPath; });
        EXPECT_FALSE(requires { &T::ResolveExistingAssetPath; });
        EXPECT_FALSE(requires { &T::TryReadAssetBytes; });
        EXPECT_FALSE(requires { &T::NormalizeKey; });
        EXPECT_FALSE(requires { &T::TryOpenCompiledStream; });
        EXPECT_FALSE(requires { &T::ReadStreamToEnd; });
        EXPECT_FALSE(requires { &T::ReadXnbHeader; });
        EXPECT_FALSE(requires { &T::RecordDisposableObject; });
        EXPECT_FALSE(requires(T& manager) { manager.RecordLoadedAsset(1); });
        EXPECT_FALSE(requires { &T::LoadUntypedXnbReference; });
        EXPECT_FALSE(requires { &T::LoadXnbAssetUntyped; });
    }
} // namespace

TEST(ContentManagerVisibilityTest, LoadingHelpersAreNotPublic)
{
    CheckInternalMembers<ContentManager>();
}

TEST(ContentManagerVisibilityTest, RootDirectoryRemainsPublic)
{
    constexpr bool publicProperty = requires(ContentManager& manager, const std::string& root) {
        manager.getRootDirectoryProperty();
        manager.setRootDirectoryProperty(root);
        manager.setRootDirectoryProperty(std::string{});
    };
    EXPECT_TRUE(publicProperty);
}

TEST(ContentManagerVisibilityTest, OtherPropertyMacroConsumerRemainsPublic)
{
    constexpr bool publicProperties = requires(Viewport& viewport) {
        viewport.getWidthProperty();
        viewport.setWidthProperty(64);
        viewport.getHeightProperty();
        viewport.setHeightProperty(64);
        viewport.getXProperty();
        viewport.setXProperty(0);
        viewport.getYProperty();
        viewport.setYProperty(0);
        viewport.getMinDepthProperty();
        viewport.setMinDepthProperty(0.0f);
        viewport.getMaxDepthProperty();
        viewport.setMaxDepthProperty(1.0f);
    };
    EXPECT_TRUE(publicProperties);
}
