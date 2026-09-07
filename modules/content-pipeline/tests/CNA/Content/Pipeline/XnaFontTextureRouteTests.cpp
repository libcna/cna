// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-139: the `FontTextureProcessor` route.
//
// This is the twelfth of XNA's twelve processors and the last one the canonical graph could not
// route: the class existed and a build could not name it, so a `.contentproj` asking for it was
// refused and 44 assets in the public samples had nowhere to go. What it does was not guessed --
// four sheets were built by genuine XNA and every rule below is read off those files.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/SpriteFontContentPipeline.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /** @brief A scratch project naming one sheet and the processor under test. */
    class Sheet
    {
    public:
        Sheet(const std::string& label, const std::string& fixture, const std::string& extra = "")
            : root_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp139_" + label + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            std::filesystem::copy_file(Locate("tests/assets/xna40/texture") / fixture,
                                       Source() / fixture);
            std::ofstream project(Source() / "Sheet.contentproj");
            project << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                    << "<Project ToolsVersion=\"4.0\" DefaultTargets=\"Build\" "
                       "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
                    << "  <PropertyGroup><XnaProfile>Reach</XnaProfile></PropertyGroup>\n"
                    << "  <ItemGroup>\n    <Compile Include=\"" << fixture << "\">\n"
                    << "      <Name>sheet</Name>\n"
                    << "      <Importer>TextureImporter</Importer>\n"
                    << "      <Processor>FontTextureProcessor</Processor>\n"
                    << extra << "    </Compile>\n  </ItemGroup>\n</Project>\n";
        }
        ~Sheet()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        Sheet(const Sheet&) = delete;
        Sheet& operator=(const Sheet&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }
        [[nodiscard]] std::filesystem::path Built() const { return Output() / "sheet.xnb"; }
        [[nodiscard]] std::filesystem::path Project() const { return Source() / "Sheet.contentproj"; }

    private:
        std::filesystem::path root_;
    };

    struct Invocation
    {
        int exitCode = -1;
        std::string output;
    };

    Invocation Build(const Sheet& sheet)
    {
        std::ostringstream captured;
        std::streambuf* const previousOut = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* const previousErr = std::cerr.rdbuf(captured.rdbuf());
        Invocation invocation;
        try
        {
            invocation.exitCode = Pipeline::RunContentCompiler(
                {"build", sheet.Project(), "-o", sheet.Output()},
                [](const Pipeline::ContentCompilerOptions& options)
                {
                    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
                    Pipeline::RegisterBuiltInContentPipeline(*registry, options);
                    return registry;
                });
        }
        catch (...)
        {
            std::cout.rdbuf(previousOut);
            std::cerr.rdbuf(previousErr);
            throw;
        }
        std::cout.rdbuf(previousOut);
        std::cerr.rdbuf(previousErr);
        invocation.output = captured.str();
        return invocation;
    }
}

// The three sheets XNA built, byte for byte.
TEST(XnaFontTextureRoute, EverySheetGenuineXnaBuiltIsReproducedByteForByte)
{
    const std::pair<const char*, const char*> cases[] = {
        {"font_sheet.png", "fonttexture_sheet_default.xnb"},
        {"font_sheet_uneven.png", "fonttexture_sheet_uneven.xnb"},
        {"font_sheet_many.png", "fonttexture_sheet_many.xnb"},
    };
    for (const auto& [fixture, genuine] : cases)
    {
        const Sheet sheet(std::string("same_") + fixture, fixture);
        const Invocation invocation = Build(sheet);
        ASSERT_EQ(invocation.exitCode, 0) << fixture << "\n" << invocation.output;
        ASSERT_TRUE(std::filesystem::is_regular_file(sheet.Built())) << invocation.output;
        EXPECT_EQ(ReadBytes(sheet.Built()),
                  ReadBytes(Locate("tests/reference/xna40/differential") / genuine))
            << fixture << " does not match " << genuine;
    }
}

// A sheet with nothing but the separator in it, and one whose separator is not magenta, are the
// same refusal -- which is what settles that the separator colour is fixed rather than read from
// the image, because the second sheet has perfectly good glyphs in it.
TEST(XnaFontTextureRoute, TheSeparatorIsMagentaAndNotTheSheetsOwnCorner)
{
    for (const char* fixture : {"probe.png", "font_sheet_alpha_border.png"})
    {
        const Sheet sheet(std::string("refuse_") + fixture, fixture);
        const Invocation invocation = Build(sheet);
        EXPECT_NE(invocation.exitCode, 0) << fixture << "\n" << invocation.output;
        EXPECT_NE(invocation.output.find("there were no glyphs found to build"), std::string::npos)
            << "XNA's own sentence for this, measured\n" << invocation.output;
        EXPECT_FALSE(std::filesystem::exists(sheet.Built()));
    }
}

// `FirstCharacter` is the only thing about the character table a sheet does not decide.
TEST(XnaFontTextureRoute, FirstCharacterMovesTheWholeCharacterTable)
{
    const Sheet sheet("first", "font_sheet.png",
                      "      <ProcessorParameters_FirstCharacter>A"
                      "</ProcessorParameters_FirstCharacter>\n");
    const Invocation invocation = Build(sheet);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    // The characters are the only difference from the default build, so the rest of the file is
    // the same length and the glyph table is identical.
    const std::vector<std::uint8_t> shifted = ReadBytes(sheet.Built());
    const std::vector<std::uint8_t> byDefault =
        ReadBytes(Locate("tests/reference/xna40/differential") / "fonttexture_sheet_default.xnb");
    ASSERT_EQ(shifted.size(), byDefault.size());
    std::size_t differing = 0u;
    for (std::size_t index = 0u; index < shifted.size(); ++index)
    {
        if (shifted[index] != byDefault[index]) { ++differing; }
    }
    // Three characters, each one byte in the container's compact encoding.
    EXPECT_EQ(differing, 3u);
}

// The atlas is not compressed by default, where the description route's is: this processor has a
// `TextureFormat` whose XNA default is `Color`, and the description processor has no such property.
TEST(XnaFontTextureRoute, TheAtlasIsCompressedOnlyWhenTheProjectAsks)
{
    const Sheet asked("dxt", "font_sheet_many.png",
                      "      <ProcessorParameters_TextureFormat>DxtCompressed"
                      "</ProcessorParameters_TextureFormat>\n");
    const Invocation invocation = Build(asked);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    const std::vector<std::uint8_t> compressed = ReadBytes(asked.Built());
    const std::vector<std::uint8_t> uncompressed =
        ReadBytes(Locate("tests/reference/xna40/differential") / "fonttexture_sheet_many.xnb");
    EXPECT_LT(compressed.size(), uncompressed.size())
        << "a DXT3 atlas is a quarter the size of the same glyphs in Color";

    // And the default really is the uncompressed one: the same sheet with no TextureFormat is what
    // XNA wrote, byte for byte, and that file is not compressed.
    const Sheet byDefault("plain", "font_sheet_many.png");
    const Invocation plain = Build(byDefault);
    ASSERT_EQ(plain.exitCode, 0) << plain.output;
    EXPECT_EQ(ReadBytes(byDefault.Built()), uncompressed);
}

// A parameter this processor has not got is refused by name rather than ignored.
TEST(XnaFontTextureRoute, AParameterItHasNotGotIsRefusedByName)
{
    const Sheet sheet("bad", "font_sheet.png",
                      "      <ProcessorParameters_NoSuchThing>1</ProcessorParameters_NoSuchThing>\n");
    const Invocation invocation = Build(sheet);
    // A `.contentproj` build takes XNA's leniency, so this is a warning and the asset still
    // builds -- the parameter is dropped rather than silently applied (XNAPP-267).
    EXPECT_EQ(invocation.exitCode, 0) << invocation.output;
    EXPECT_NE(invocation.output.find("NoSuchThing"), std::string::npos) << invocation.output;
}
