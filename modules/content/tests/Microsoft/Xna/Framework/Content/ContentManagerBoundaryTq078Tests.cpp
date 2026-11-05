// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/FileNotFoundException.hpp"
using namespace Microsoft::Xna::Framework::Content;
namespace {
class Tq078OpenManager final : public ContentManager {
public:
    using ContentManager::ContentManager;
    using ContentManager::OpenStream;
};
}
TEST(ContentManagerBoundaryTq078Test, RelativeRootsRefuseRootedAssetsWithArgumentCause)
{
    // Refresh OpenStream IL_001d-0039 chooses TitleContainer by ROOT;
    // TitleContainer.OpenStream IL_001b-002d rejects the rooted combined path.
    // ContentManager wraps that ArgumentException at OpenStream IL_006f-0090.
    const auto path = std::filesystem::absolute("tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb");
    ASSERT_TRUE(std::filesystem::is_regular_file(path));
    auto asset = path; asset.replace_extension();
    for (const char* root : {"", "Content"}) {
        Tq078OpenManager manager(CNA::TestSupport::EmptyServices(), root);
        const std::string name = CNA::Internal::PathToUtf8(asset);
        try { (void)manager.OpenStream(name); FAIL() << "rooted asset bypassed TitleContainer refusal"; }
        catch (const ContentLoadException& error) {
            EXPECT_EQ(error.getMessageProperty(), "Error loading \"" + name + "\". Cannot open file.");
            ASSERT_NE(error.getInnerExceptionProperty(), nullptr);
            try { std::rethrow_exception(error.getInnerExceptionProperty()); }
            catch (const System::ArgumentException& cause) {
                EXPECT_EQ(typeid(cause), typeid(System::ArgumentException));
            }
            catch (...) { FAIL() << "rooted-name cause is not ArgumentException"; }
        }
    }
    // Same physical fixture opens with an absolute root: literal XNB magic.
    Tq078OpenManager absolute(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(path.parent_path()));
    const auto stream = absolute.OpenStream("white-1");
    ASSERT_NE(stream, nullptr);
    std::array<std::uint8_t,3> bytes{};
    ASSERT_EQ(stream->Read(bytes.data(), 0, 3), 3);
    EXPECT_EQ(bytes, (std::array<std::uint8_t,3>{'X','N','B'}));
}

TEST(ContentManagerBoundaryTq078Test, BaseOpenStreamAppendsXnbEvenWhenNameAlreadyHasSuffix)
{
    // Refresh OpenStream IL_0006-0011 concatenates assetName + ".xnb"
    // unconditionally. This is physical opening, not only cache-key separation.
    const auto root = std::filesystem::absolute("tests/assets/xnb/monogame/windows/uncompressed");
    ASSERT_TRUE(std::filesystem::is_regular_file(root / "white-1.xnb"));
    ASSERT_FALSE(std::filesystem::exists(root / "white-1.xnb.xnb"));
    Tq078OpenManager manager(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root));
    try { (void)manager.OpenStream("white-1.xnb"); FAIL() << "suffix was stripped before opening"; }
    catch (const ContentLoadException& error) {
        EXPECT_EQ(error.getMessageProperty(), "Error loading \"white-1.xnb\". File not found.");
        ASSERT_NE(error.getInnerExceptionProperty(), nullptr);
        try { std::rethrow_exception(error.getInnerExceptionProperty()); }
        catch (const System::IO::FileNotFoundException& cause) {
            EXPECT_EQ(typeid(cause), typeid(System::IO::FileNotFoundException));
        }
        catch (...) { FAIL() << "double-suffix miss lost its file cause"; }
    }
}
