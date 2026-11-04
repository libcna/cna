// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/ObjectDisposedException.hpp"
using Microsoft::Xna::Framework::Content::ContentManager;
TEST(ContentManagerDisposedNameTq045Test, BaseManagerReportsItsQualifiedName)
{
    // Refresh Load/ReadAsset/Unload IL_0008-0013 passes Object.ToString(),
    // whose base ContentManager result is its fully qualified CLR type name.
    ContentManager manager;
    manager.Dispose();
    for (bool unload : {false, true}) {
        try {
            if (unload) manager.Unload();
            else (void)manager.Load<int>("");
            FAIL() << "disposed operation succeeded";
        } catch (const System::ObjectDisposedException& error) {
            EXPECT_EQ(error.getObjectNameProperty(), "Microsoft.Xna.Framework.Content.ContentManager");
        }
    }
}
