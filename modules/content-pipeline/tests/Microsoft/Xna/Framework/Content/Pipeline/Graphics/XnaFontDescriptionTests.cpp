// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-093, 098: FontDescription and FontDescriptionStyle
// against what the genuine XNA 4.0 pipeline does with the same inputs
// (tests/reference/xna40/graphics/graphics-content-oracle.json, cases font/*).
//
// The measurements settle several things a reading of the API could not: every constructor leaves
// UseKerning false, an empty font name and a size that is not greater than zero are refused with
// their exact texts while a NaN size and an undefined style are accepted, and the .spritefont
// document is this type in intermediate XML -- FontName, Size, Spacing, UseKerning, Style,
// DefaultCharacter, CharacterRegions, of which Spacing, UseKerning and DefaultCharacter may be
// left out and the rest may not.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/FontDescription.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Graphics::FontDescription;
using Graphics::FontDescriptionStyle;
using Intermediate::IntermediateSerializer;

namespace
{
    // --------------------------------------------------------------------------------------------
    // The oracle corpus
    // --------------------------------------------------------------------------------------------
    std::filesystem::path CorpusFile()
    {
        const std::filesystem::path relative = "tests/reference/xna40/graphics/graphics-content-oracle.json";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    std::string Unescape(const std::string& text)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char next = text[++i];
                out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
            }
            else
            {
                out += text[i];
            }
        }
        return out;
    }

    const std::map<std::string, std::string>& Oracle()
    {
        static const std::map<std::string, std::string> cases = []
        {
            std::map<std::string, std::string> map;
            std::ifstream in(CorpusFile());
            std::string line;
            const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"result\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
            while (std::getline(in, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, pattern))
                {
                    map[match[1]] = Unescape(match[2]);
                }
            }
            return map;
        }();
        return cases;
    }

    /**
     * @brief The measured result, with the two runtimes' different parameter-name decorations
     *        removed: the .NET Framework appends "\nParameter name: value" and .NET Core, which
     *        sharp-runtime follows, appends " (Parameter 'value')".
     */
    std::string Normalize(const std::string& result)
    {
        std::string text = result;
        const std::size_t framework = text.find("Parameter name:");
        if (framework != std::string::npos)
        {
            std::size_t cut = framework;
            while (cut > 0 && (text[cut - 1] == '\n' || text[cut - 1] == '\r'))
            {
                --cut;
            }
            text = text.substr(0, cut);
        }
        const std::size_t core = text.find(" (Parameter '");
        if (core != std::string::npos)
        {
            const std::size_t end = text.find(')', core);
            text = text.substr(0, core) + (end == std::string::npos ? "" : text.substr(end + 1));
        }
        return text;
    }

    std::string Expected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : Normalize(found->second);
    }

    // --------------------------------------------------------------------------------------------
    // The oracle's formatting, reproduced
    // --------------------------------------------------------------------------------------------
    std::string CodePoint(SharpRuntime::charcs value)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string hex(4, '0');
        auto code = static_cast<std::uint16_t>(value);
        for (int i = 3; i >= 0; --i)
        {
            hex[static_cast<std::size_t>(i)] = digits[code & 0xFU];
            code = static_cast<std::uint16_t>(code >> 4);
        }
        return "U+" + hex;
    }

    std::string Characters(const FontDescription& font)
    {
        std::string text;
        for (const SharpRuntime::charcs character : font.getCharactersProperty())
        {
            if (!text.empty())
            {
                text += ' ';
            }
            text += CodePoint(character);
        }
        return text;
    }

    std::string StyleName(FontDescriptionStyle style)
    {
        switch (style)
        {
        case FontDescriptionStyle::Regular:
            return "Regular";
        case FontDescriptionStyle::Bold:
            return "Bold";
        case FontDescriptionStyle::Italic:
            return "Italic";
        default:
            break;
        }
        // An undefined value prints as its number, as .NET's Enum.ToString does.
        return std::to_string(static_cast<SharpRuntime::intcs>(style));
    }

    std::string Number(SharpRuntime::Single value)
    {
        if (std::isnan(value))
        {
            return "NaN";
        }
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << value;
        return text.str();
    }

    std::string DescribeFont(const FontDescription& font)
    {
        return "name=\"" + font.getFontNameProperty() + "\" size=" + Number(font.getSizeProperty()) +
               " spacing=" + Number(font.getSpacingProperty()) + " style=" + StyleName(font.getStyleProperty()) +
               " kerning=" + (font.getUseKerningProperty() ? "True" : "False") + " default=" +
               (font.getDefaultCharacterProperty().has_value() ? CodePoint(*font.getDefaultCharacterProperty())
                                                               : "null") +
               " chars=[" + Characters(font) + "]";
    }

    /** @brief Runs a measurement, reporting a refusal the way the oracle's driver reports it. */
    std::string Result(const std::function<std::string()>& body)
    {
        try
        {
            return body();
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            return Normalize("throws ArgumentOutOfRangeException: " + error.getMessageProperty());
        }
        catch (const InvalidContentException& error)
        {
            return Normalize("throws InvalidContentException: " + error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return Normalize("throws Exception: " + error.getMessageProperty());
        }
    }

    std::string Font(const std::function<FontDescription()>& make)
    {
        return Result([&] { return DescribeFont(make()); });
    }

    // --------------------------------------------------------------------------------------------
    // Intermediate XML, the .spritefont format
    // --------------------------------------------------------------------------------------------
    std::string Serialize(const std::shared_ptr<FontDescription>& font)
    {
        System::Xml::XmlWriterSettings settings;
        settings.Indent = true;
        // The measured documents were written by .NET's XmlWriter on Windows.
        settings.NewLineChars = "\r\n";
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
        IntermediateSerializer::Serialize<std::shared_ptr<FontDescription>>(*writer, font, std::string());
        std::string xml = writer->ToString();
        // The oracle drops the XML declaration, whose encoding names the writer's own buffer.
        const std::size_t cut = xml.find("?>");
        if (cut != std::string::npos)
        {
            xml = xml.substr(cut + 2);
        }
        while (!xml.empty() && (xml.front() == '\r' || xml.front() == '\n'))
        {
            xml.erase(xml.begin());
        }
        return xml;
    }

    std::shared_ptr<FontDescription> Deserialize(const std::string& xml)
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        return IntermediateSerializer::Deserialize<std::shared_ptr<FontDescription>>(*reader, std::string());
    }

    std::string SpriteFont(const std::string& body)
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\n"
               "  <Asset Type=\"Graphics:FontDescription\">\n" +
               body +
               "  </Asset>\n"
               "</XnaContent>\n";
    }
}

TEST(XnaFontDescription, OracleIsPresent)
{
    ASSERT_GE(Oracle().size(), 250u) << CorpusFile();
}

TEST(XnaFontDescription, ConstructorsMatchXna)
{
    EXPECT_EQ(Font([] { return FontDescription("Arial", 14.0f, 2.0f); }), Expected("font/ctor3_defaults"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", 14.0f, 2.0f, FontDescriptionStyle::Bold); }),
              Expected("font/ctor4_style"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", 14.0f, 2.0f, FontDescriptionStyle::Italic, false); }),
              Expected("font/ctor5_kerning"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", 14.0f, 2.0f, FontDescriptionStyle::Italic, true); }),
              Expected("font/ctor5_kerning_true"));
    EXPECT_EQ(Font([] { return FontDescription("   ", 14.0f, 2.0f); }), Expected("font/ctor_whitespace_name"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", 14.0f, -3.0f); }), Expected("font/ctor_negative_spacing"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", std::numeric_limits<float>::quiet_NaN(), 2.0f); }),
              Expected("font/ctor_nan_size"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", 14.0f, 2.0f, static_cast<FontDescriptionStyle>(99)); }),
              Expected("font/ctor_undefined_style"));
}

TEST(XnaFontDescription, ConstructorRefusalsMatchXna)
{
    // C++ has no null std::string, so the empty name reproduces both of XNA's null and empty cases.
    EXPECT_EQ(Font([] { return FontDescription("", 14.0f, 2.0f); }), Expected("font/ctor_empty_name"));
    EXPECT_EQ(Font([] { return FontDescription("", 14.0f, 2.0f); }), Expected("font/ctor_null_name"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", -1.0f, 2.0f); }), Expected("font/ctor_negative_size"));
    EXPECT_EQ(Font([] { return FontDescription("Arial", 0.0f, 2.0f); }), Expected("font/ctor_zero_size"));
}

TEST(XnaFontDescription, SettersMatchXna)
{
    const auto mutate = [](const std::function<void(FontDescription&)>& change)
    {
        return Result([&]
                      {
                          FontDescription font("Arial", 14.0f, 2.0f);
                          change(font);
                          return DescribeFont(font);
                      });
    };
    EXPECT_EQ(mutate([](FontDescription& f) { f.setFontNameProperty(""); }), Expected("font/set_font_name_null"));
    EXPECT_EQ(mutate([](FontDescription& f) { f.setFontNameProperty(""); }), Expected("font/set_font_name_empty"));
    EXPECT_EQ(mutate([](FontDescription& f) { f.setSizeProperty(-2.0f); }), Expected("font/set_size_negative"));
    EXPECT_EQ(mutate([](FontDescription& f) { f.setSpacingProperty(-2.0f); }), Expected("font/set_spacing_negative"));
    EXPECT_EQ(mutate([](FontDescription& f) { f.setStyleProperty(static_cast<FontDescriptionStyle>(99)); }),
              Expected("font/set_style_undefined"));
    EXPECT_EQ(mutate([](FontDescription& f) { f.setDefaultCharacterProperty(u'?'); }),
              Expected("font/set_default_character"));
    EXPECT_EQ(mutate([](FontDescription& f) { f.setUseKerningProperty(true); }), Expected("font/set_use_kerning"));
}

TEST(XnaFontDescription, CharacterCollectionMatchesXna)
{
    FontDescription duplicates("Arial", 14.0f, 2.0f);
    duplicates.getCharactersProperty().insert(u'a');
    duplicates.getCharactersProperty().insert(u'b');
    duplicates.getCharactersProperty().insert(u'a');
    EXPECT_EQ("count=" + std::to_string(duplicates.getCharactersProperty().size()) + " contains_a=" +
                  (duplicates.getCharactersProperty().count(u'a') != 0 ? "True" : "False") + " chars=" +
                  Characters(duplicates),
              Expected("font/characters_add_duplicate"));

    FontDescription edited("Arial", 14.0f, 2.0f);
    edited.getCharactersProperty().insert(u'a');
    edited.getCharactersProperty().insert(u'b');
    const bool removed = edited.getCharactersProperty().erase(u'a') != 0;
    const bool missing = edited.getCharactersProperty().erase(u'z') != 0;
    edited.getCharactersProperty().clear();
    EXPECT_EQ(std::string("removed=") + (removed ? "True" : "False") + " missing=" + (missing ? "True" : "False") +
                  " count=" + std::to_string(edited.getCharactersProperty().size()) + " readonly=False",
              Expected("font/characters_remove_and_clear"));
}

TEST(XnaFontDescription, ContentItemMembersMatchXna)
{
    const FontDescription font("Arial", 14.0f, 2.0f);
    EXPECT_EQ("name=\"" + font.getNameProperty() + "\" identity=" +
                  (font.getIdentityProperty().getSourceFilenameProperty().empty() ? "null" : "set") +
                  " opaquedata=" + std::to_string(font.getOpaqueDataProperty().getCountProperty()),
              Expected("font/contentitem_members"));
}

TEST(XnaFontDescription, SerializesAsTheSpriteFontFormat)
{
    auto font = std::make_shared<FontDescription>("Segoe UI Mono", 14.0f, 1.5f, FontDescriptionStyle::Bold, false);
    font->setDefaultCharacterProperty(u'?');
    font->getCharactersProperty().insert(u'A');
    font->getCharactersProperty().insert(u'B');
    EXPECT_EQ(Serialize(font), Expected("font/serialize"));

    EXPECT_EQ(Serialize(std::make_shared<FontDescription>("Arial", 12.0f, 0.0f)), Expected("font/serialize_minimal"));
}

TEST(XnaFontDescription, DeserializesTheSpriteFontFormat)
{
    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(SpriteFont("    <FontName>Segoe UI Mono</FontName>\n"
                                                              "    <Size>14</Size>\n"
                                                              "    <Spacing>0</Spacing>\n"
                                                              "    <UseKerning>true</UseKerning>\n"
                                                              "    <Style>Regular</Style>\n"
                                                              "    <CharacterRegions>\n"
                                                              "      <CharacterRegion>\n"
                                                              "        <Start>&#32;</Start>\n"
                                                              "        <End>&#38;</End>\n"
                                                              "      </CharacterRegion>\n"
                                                              "    </CharacterRegions>\n")));
              }),
              Expected("font/deserialize_spritefont"));

    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(SpriteFont("    <FontName>Arial</FontName>\n"
                                                              "    <Size>10</Size>\n"
                                                              "    <Style>Regular</Style>\n"
                                                              "    <CharacterRegions />\n")));
              }),
              Expected("font/deserialize_spritefont_empty_regions"));

    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(
                      SpriteFont("    <FontName>Arial</FontName>\n"
                                 "    <Size>10</Size>\n"
                                 "    <Style>Regular</Style>\n"
                                 "    <CharacterRegions>\n"
                                 "      <CharacterRegion><Start>a</Start><End>c</End></CharacterRegion>\n"
                                 "      <CharacterRegion><Start>c</Start><End>e</End></CharacterRegion>\n"
                                 "    </CharacterRegions>\n")));
              }),
              Expected("font/deserialize_spritefont_two_regions"));
}

TEST(XnaFontDescription, RefusesTheDocumentsXnaRefuses)
{
    // Reading is positional and strict, so a missing required element is reported as the element
    // the reader expected next.
    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(SpriteFont("    <FontName>Arial</FontName>\n"
                                                              "    <Style>Regular</Style>\n")));
              }),
              Expected("font/deserialize_spritefont_no_size"));

    EXPECT_EQ(Result([] { return DescribeFont(*Deserialize(SpriteFont("    <Size>10</Size>\n"))); }),
              Expected("font/deserialize_spritefont_no_fontname"));

    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(SpriteFont("    <FontName>Arial</FontName>\n"
                                                              "    <Size>10</Size>\n"
                                                              "    <Style>Regular</Style>\n")));
              }),
              Expected("font/deserialize_spritefont_no_regions"));

    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(SpriteFont("    <FontName>Arial</FontName>\n"
                                                              "    <Size>10</Size>\n"
                                                              "    <Style>Italic</Style>\n"
                                                              "    <DefaultCharacter>*</DefaultCharacter>\n")));
              }),
              Expected("font/deserialize_spritefont_defaultchar"));

    EXPECT_EQ(Result([] {
                  return DescribeFont(*Deserialize(
                      SpriteFont("    <FontName>Arial</FontName>\n"
                                 "    <Size>10</Size>\n"
                                 "    <Style>Regular</Style>\n"
                                 "    <CharacterRegions>\n"
                                 "      <CharacterRegion><Start>e</Start><End>a</End></CharacterRegion>\n"
                                 "    </CharacterRegions>\n")));
              }),
              Expected("font/deserialize_spritefont_reversed_region"));
}

TEST(XnaFontDescription, RoundTripsASpriteFontDocument)
{
    EXPECT_EQ(Result([] {
                  return Serialize(Deserialize(
                      SpriteFont("    <FontName>Arial</FontName>\n"
                                 "    <Size>10</Size>\n"
                                 "    <Spacing>2</Spacing>\n"
                                 "    <UseKerning>true</UseKerning>\n"
                                 "    <Style>Bold</Style>\n"
                                 "    <CharacterRegions>\n"
                                 "      <CharacterRegion><Start>a</Start><End>c</End></CharacterRegion>\n"
                                 "      <CharacterRegion><Start>x</Start><End>x</End></CharacterRegion>\n"
                                 "    </CharacterRegions>\n")));
              }),
              Expected("font/deserialize_spritefont_roundtrip"));
}
