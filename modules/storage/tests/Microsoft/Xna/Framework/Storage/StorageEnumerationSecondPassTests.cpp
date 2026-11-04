// SPDX-License-Identifier: MS-PL
#include "StorageSecondPassFixture.hpp"
#include "System/IO/DirectoryNotFoundException.hpp"
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace Tq042
{
TEST_F(StorageSecondPassTest, SubdirectoryPatternsReturnContainerRelativeNames)
{
    // XNA GetFileNames/GetDirectoryNames(string) IL_0017-0023 delegates the
    // pattern to Directory; IL_0063-007d strips only the container root.
    // FNA StorageContainer.cs:293-303,328-338 uses the same Directory overload.
    { std::ofstream seed(root / "child" / "nested.bin"); seed << "nested"; }
    ASSERT_TRUE(fs::create_directory(root / "child" / "level"));
    for (const char* pattern : {"child/*.bin", "child\\*.bin"})
        EXPECT_EQ(container->GetFileNames(pattern), (std::vector<std::string>{"child/nested.bin"}));
    for (const char* pattern : {"child/l*", "child\\l*"})
        EXPECT_EQ(container->GetDirectoryNames(pattern), (std::vector<std::string>{"child/level"}));
    EXPECT_EQ(container->GetFileNames(), (std::vector<std::string>{"seed.bin"}));
    EXPECT_EQ(container->GetDirectoryNames(), (std::vector<std::string>{"child"}));
    EXPECT_TRUE(container->GetFileNames("child/no-match*").empty());
}

TEST_F(StorageSecondPassTest, MissingPatternDirectoryThrowsInsteadOfReturningNoMatches)
{
    // Same Directory calls at IL_001e: the search directory must exist.
    Exact<System::IO::DirectoryNotFoundException>([&] { (void)container->GetFileNames("missing/*"); });
    Exact<System::IO::DirectoryNotFoundException>([&] { (void)container->GetDirectoryNames("missing/*"); });
    EXPECT_FALSE(fs::exists(root / "missing"));
    SeedUnchanged();
}
}
#endif
