// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/NullReferenceException.hpp"
using Microsoft::Xna::Framework::Content::ContentManager;
TEST(ContentManagerRootDisposedTq045Test, GetterSurvivesButBothSettersFollowNullCacheFailure)
{
    // Refresh Dispose(bool) IL_0013-0021 nulls loadedAssets. Root getter IL_0000-0006
    // still returns the string; setter IL_000e-0014 dereferences the null dictionary.
    ContentManager manager;
    manager.setRootDirectoryProperty(std::string("retained"));
    manager.Dispose();
    EXPECT_EQ(manager.getRootDirectoryProperty(), "retained");
    const std::string lvalue = "different";
    EXPECT_THROW(manager.setRootDirectoryProperty(lvalue), System::NullReferenceException);
    std::string rvalue = "other";
    EXPECT_THROW(manager.setRootDirectoryProperty(std::move(rvalue)), System::NullReferenceException);
    EXPECT_EQ(rvalue, "other");
    EXPECT_EQ(manager.getRootDirectoryProperty(), "retained");
}
