// SPDX-License-Identifier: MS-PL
// TQ-042: owner selected XNA 4.0 Refresh (4.0.30901) over original 4.0.20823.
#include <gtest/gtest.h>
#include <string>
#include <typeinfo>
#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "System/IO/FileNotFoundException.hpp"
TEST(TitleContainerRefreshTest, ArgumentFailuresFromOpenTranslateToFileNotFound)
{
    // Shipped Framework.dll TitleContainer.OpenStream IL_0086-008c includes
    // ArgumentException in the FileNotFound branch ending at IL_00a9-00ae.
    // Embedded NUL is refused before native open, so no file fixture is needed.
    const std::string path = std::string("tq042") + '\0' + "suffix";
    try
    {
        (void)Microsoft::Xna::Framework::TitleContainer::OpenStream(path);
        ADD_FAILURE() << "expected FileNotFoundException";
    }
    catch (const std::exception& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::IO::FileNotFoundException));
    }
}
