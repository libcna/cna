// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-090..092, 098: the BitmapContent family, the mipmap
// chains, the TextureContent types and VectorConverter against what the genuine XNA 4.0 pipeline
// measured (tests/reference/xna40/graphics/graphics-content-oracle.json). Layouts, conversions,
// sizes, messages and tables are compared exactly; resampled pixels are compared within a
// tolerance, because XNA resamples through D3DX's undocumented kernel and CNA uses a box filter,
// and DXT blocks are compared by decoding XNA's blocks with CNA's decoder.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/DxtBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChain.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/PixelBitmapContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureReferenceDictionary.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VectorConverter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace PackedVector = Microsoft::Xna::Framework::Graphics::PackedVector;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Graphics::BitmapContent;
using Graphics::Dxt1BitmapContent;
using Graphics::Dxt3BitmapContent;
using Graphics::Dxt5BitmapContent;
using Graphics::MipmapChain;
using Graphics::PixelBitmapContent;
using Graphics::Texture2DContent;
using Graphics::Texture3DContent;
using Graphics::TextureContent;
using Graphics::TextureCubeContent;
using Graphics::VectorConverter;

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
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty(); dir = dir.parent_path())
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

    std::string Expected(const std::string& name)
    {
        const auto found = Oracle().find(name);
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : found->second;
    }

    /** @brief Drops the .NET Framework "\nParameter name: x" tail an exception message carries. */
    std::string MessageOnly(const std::string& result)
    {
        const std::size_t cut = result.find("\nParameter name:");
        return cut == std::string::npos ? result : result.substr(0, cut);
    }

    // --------------------------------------------------------------------------------------------
    // The oracle's formatting, reproduced
    // --------------------------------------------------------------------------------------------
    std::string SurfaceName(SurfaceFormat format)
    {
        static const std::map<SurfaceFormat, std::string> names = {
            {SurfaceFormat::Color, "Color"}, {SurfaceFormat::Bgr565, "Bgr565"}, {SurfaceFormat::Bgra5551, "Bgra5551"},
            {SurfaceFormat::Bgra4444, "Bgra4444"}, {SurfaceFormat::Dxt1, "Dxt1"}, {SurfaceFormat::Dxt3, "Dxt3"},
            {SurfaceFormat::Dxt5, "Dxt5"}, {SurfaceFormat::NormalizedByte2, "NormalizedByte2"},
            {SurfaceFormat::NormalizedByte4, "NormalizedByte4"}, {SurfaceFormat::Rgba1010102, "Rgba1010102"},
            {SurfaceFormat::Rg32, "Rg32"}, {SurfaceFormat::Rgba64, "Rgba64"}, {SurfaceFormat::Alpha8, "Alpha8"},
            {SurfaceFormat::Single, "Single"}, {SurfaceFormat::Vector2, "Vector2"}, {SurfaceFormat::Vector4, "Vector4"},
            {SurfaceFormat::HalfSingle, "HalfSingle"}, {SurfaceFormat::HalfVector2, "HalfVector2"},
            {SurfaceFormat::HalfVector4, "HalfVector4"}, {SurfaceFormat::HdrBlendable, "HdrBlendable"}};
        const auto found = names.find(format);
        return found == names.end() ? "?" : found->second;
    }

    std::string VertexName(VertexElementFormat format)
    {
        static const std::map<VertexElementFormat, std::string> names = {
            {VertexElementFormat::Single, "Single"}, {VertexElementFormat::Vector2, "Vector2"},
            {VertexElementFormat::Vector3, "Vector3"}, {VertexElementFormat::Vector4, "Vector4"},
            {VertexElementFormat::Color, "Color"}, {VertexElementFormat::Byte4, "Byte4"},
            {VertexElementFormat::Short2, "Short2"}, {VertexElementFormat::Short4, "Short4"},
            {VertexElementFormat::NormalizedShort2, "NormalizedShort2"}, {VertexElementFormat::NormalizedShort4, "NormalizedShort4"},
            {VertexElementFormat::HalfVector2, "HalfVector2"}, {VertexElementFormat::HalfVector4, "HalfVector4"}};
        const auto found = names.find(format);
        return found == names.end() ? "?" : found->second;
    }

    std::string Hex(const std::vector<std::uint8_t>& bytes)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string out;
        for (std::uint8_t b : bytes)
        {
            out += digits[b >> 4];
            out += digits[b & 15];
        }
        return out;
    }

    std::string Hex8(std::uint32_t value)
    {
        char buffer[9];
        std::snprintf(buffer, sizeof buffer, "%08X", value);
        return buffer;
    }

    /** @brief `GetType().Name` as the C# oracle printed it: the generic's stub name for pixel bitmaps. */
    std::string ClrName(const BitmapContent& bitmap)
    {
        const std::string display = bitmap.TypeDisplayName();
        return display.rfind("PixelBitmapContent<", 0) == 0 ? "PixelBitmapContent`1" : display;
    }

    std::string Describe(const BitmapContent& bitmap)
    {
        SurfaceFormat format{};
        const bool has = bitmap.TryGetFormat(format);
        return ClrName(bitmap) + " " + std::to_string(bitmap.getWidthProperty()) + "x" + std::to_string(bitmap.getHeightProperty()) +
               " format=" + (has ? SurfaceName(format) : "none") + " bytes=" + std::to_string(bitmap.GetPixelData().size()) +
               " ToString=\"" + bitmap.ToString() + "\"";
    }

    std::string DescribeChain(const MipmapChain& chain)
    {
        std::string out = "count=" + std::to_string(chain.getCountProperty());
        for (SharpRuntime::intcs i = 0; i < chain.getCountProperty(); ++i)
        {
            const BitmapContent& level = *chain[i];
            SurfaceFormat format{};
            out += " [" + ClrName(level) + " " + std::to_string(level.getWidthProperty()) + "x" +
                   std::to_string(level.getHeightProperty()) + (level.TryGetFormat(format) ? " " + SurfaceName(format) : "") + "]";
        }
        return out;
    }

    std::string Pixels(const PixelBitmapContent<Color>& bitmap)
    {
        std::string out;
        for (SharpRuntime::intcs y = 0; y < bitmap.getHeightProperty(); ++y)
        {
            for (SharpRuntime::intcs x = 0; x < bitmap.getWidthProperty(); ++x)
            {
                if (!out.empty())
                {
                    out += ' ';
                }
                out += Hex8(bitmap.GetPixel(x, y).getPackedValueProperty());
            }
        }
        return out;
    }

    std::shared_ptr<PixelBitmapContent<Color>> Gradient(int width, int height)
    {
        auto bitmap = std::make_shared<PixelBitmapContent<Color>>(width, height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                bitmap->SetPixel(x, y, Color(static_cast<std::int32_t>(static_cast<std::uint8_t>(x * 255 / std::max(1, width - 1))),
                                            static_cast<std::int32_t>(static_cast<std::uint8_t>(y * 255 / std::max(1, height - 1))),
                                            static_cast<std::int32_t>(static_cast<std::uint8_t>(37 + 11 * x + 7 * y)),
                                            static_cast<std::int32_t>(static_cast<std::uint8_t>(255 - 40 * y))));
            }
        }
        return bitmap;
    }

    std::shared_ptr<PixelBitmapContent<Color>> FourPixels()
    {
        auto bitmap = std::make_shared<PixelBitmapContent<Color>>(2, 2);
        bitmap->SetPixel(0, 0, Color(0, 0, 0, 0));
        bitmap->SetPixel(1, 0, Color(255, 0, 0, 255));
        bitmap->SetPixel(0, 1, Color(0, 255, 0, 128));
        bitmap->SetPixel(1, 1, Color(10, 20, 30, 40));
        return bitmap;
    }

    std::vector<std::uint32_t> ParsePixels(const std::string& text)
    {
        std::vector<std::uint32_t> out;
        std::istringstream in(text);
        std::string token;
        while (in >> token)
        {
            out.push_back(static_cast<std::uint32_t>(std::stoul(token, nullptr, 16)));
        }
        return out;
    }

    std::vector<std::uint8_t> ParseHex(const std::string& text)
    {
        std::vector<std::uint8_t> out;
        for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        {
            out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
        }
        return out;
    }

    std::string Field(const std::string& result, const std::string& key)
    {
        const std::size_t start = result.find(key + "=");
        if (start == std::string::npos)
        {
            return std::string();
        }
        const std::size_t valueStart = start + key.size() + 1;
        std::size_t end = result.find(" ", valueStart);
        // pixel lists are space separated: take everything up to the next "key=" or the end
        std::size_t nextKey = std::string::npos;
        for (std::size_t p = valueStart; p < result.size(); ++p)
        {
            if (result[p] == '=' )
            {
                const std::size_t wordStart = result.rfind(' ', p);
                if (wordStart != std::string::npos && wordStart >= valueStart)
                {
                    nextKey = wordStart;
                    break;
                }
            }
        }
        end = nextKey;
        return result.substr(valueStart, end == std::string::npos ? std::string::npos : end - valueStart);
    }

    /** @brief Largest per-channel difference between two pixel lists (255 when the lengths differ). */
    int MaxChannelDelta(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
    {
        if (a.size() != b.size())
        {
            return 255;
        }
        int worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                const int x = static_cast<int>((a[i] >> shift) & 0xFF);
                const int y = static_cast<int>((b[i] >> shift) & 0xFF);
                worst = std::max(worst, std::abs(x - y));
            }
        }
        return worst;
    }

    template<typename T>
    std::string ConvertFromColor()
    {
        auto target = std::make_shared<PixelBitmapContent<T>>(2, 2);
        BitmapContent::Copy(FourPixels(), target);
        auto back = std::make_shared<PixelBitmapContent<Color>>(2, 2);
        BitmapContent::Copy(target, back);
        return "data=" + Hex(target->GetPixelData()) + " back=" + Pixels(*back);
    }

    template<typename T>
    std::string SurfaceAndVertex()
    {
        SurfaceFormat surface{};
        VertexElementFormat vertex{};
        const bool hasSurface = VectorConverter::TryGetSurfaceFormat(System::Type::From<T>(), surface);
        const bool hasVertex = VectorConverter::TryGetVertexElementFormat(System::Type::From<T>(), vertex);
        return "surface=" + (hasSurface ? SurfaceName(surface) : "none") + " vertex=" + (hasVertex ? VertexName(vertex) : "none");
    }

    std::vector<float> ParseFloats(const std::string& text)
    {
        std::vector<float> out;
        std::string token;
        std::istringstream in(text);
        while (std::getline(in, token, ','))
        {
            out.push_back(std::stof(token));
        }
        return out;
    }

    template<typename T>
    void CheckPixelType(const std::string& name)
    {
        EXPECT_EQ(Describe(PixelBitmapContent<T>(3, 2)), Expected("pixel_type/" + name + "/describe")) << name;
        EXPECT_EQ(ConvertFromColor<T>(), Expected("pixel_type/" + name + "/convert_from_color")) << name;
        EXPECT_EQ(SurfaceAndVertex<T>(), Expected("pixel_type/" + name + "/surface_format")) << name;
        // The converter round trip: the `back=` floats are compared numerically, since the packed
        // value's C# ToString varies by type.
        const std::function<T(Vector4)> toT = VectorConverter::GetConverter<Vector4, T>();
        const std::function<Vector4(T)> fromT = VectorConverter::GetConverter<T, Vector4>();
        const Vector4 back = fromT(toT(Vector4(0.25f, 0.5f, 0.75f, 1.0f)));
        const std::vector<float> expected = ParseFloats(Field(Expected("pixel_type/" + name + "/converter_vector4"), "back"));
        ASSERT_EQ(expected.size(), 4u) << name;
        EXPECT_NEAR(back.X, expected[0], 1e-6f) << name;
        EXPECT_NEAR(back.Y, expected[1], 1e-6f) << name;
        EXPECT_NEAR(back.Z, expected[2], 1e-6f) << name;
        EXPECT_NEAR(back.W, expected[3], 1e-6f) << name;
    }

    template<typename E>
    std::string Throws(const std::function<void()>& action, const std::string& typeName)
    {
        try
        {
            action();
            return "accepted";
        }
        catch (const E& error)
        {
            return "throws " + typeName + ": " + std::string(error.what());
        }
        catch (const std::exception& error)
        {
            return std::string("throws <other> ") + typeid(error).name() + ": " + error.what();
        }
    }

    std::string ValidateResult(const std::function<void()>& action)
    {
        try
        {
            action();
            return "accepted";
        }
        catch (const InvalidContentException& error)
        {
            return "throws InvalidContentException: " + error.getMessageProperty();
        }
        catch (const System::NotSupportedException& error)
        {
            return "throws NotSupportedException: " + std::string(error.what());
        }
    }
}

TEST(XnaGraphicsBitmap, OracleIsPresent)
{
    ASSERT_GE(Oracle().size(), 250u) << CorpusFile();
}

TEST(XnaGraphicsBitmap, EveryPixelTypeMatchesXna)
{
    CheckPixelType<PackedVector::Alpha8>("Alpha8");
    CheckPixelType<PackedVector::Bgr565>("Bgr565");
    CheckPixelType<PackedVector::Bgra4444>("Bgra4444");
    CheckPixelType<PackedVector::Bgra5551>("Bgra5551");
    CheckPixelType<PackedVector::Byte4>("Byte4");
    CheckPixelType<Color>("Color");
    CheckPixelType<PackedVector::HalfSingle>("HalfSingle");
    CheckPixelType<PackedVector::HalfVector2>("HalfVector2");
    CheckPixelType<PackedVector::HalfVector4>("HalfVector4");
    CheckPixelType<PackedVector::NormalizedByte2>("NormalizedByte2");
    CheckPixelType<PackedVector::NormalizedByte4>("NormalizedByte4");
    CheckPixelType<PackedVector::NormalizedShort2>("NormalizedShort2");
    CheckPixelType<PackedVector::NormalizedShort4>("NormalizedShort4");
    CheckPixelType<PackedVector::Rg32>("Rg32");
    CheckPixelType<PackedVector::Rgba1010102>("Rgba1010102");
    CheckPixelType<PackedVector::Rgba64>("Rgba64");
    CheckPixelType<PackedVector::Short2>("Short2");
    CheckPixelType<PackedVector::Short4>("Short4");
    CheckPixelType<float>("Single");
    CheckPixelType<Vector2>("Vector2");
    CheckPixelType<Vector3>("Vector3");
    CheckPixelType<Vector4>("Vector4");
}

TEST(XnaGraphicsBitmap, ColorLayoutAndAccessorsMatchXna)
{
    PixelBitmapContent<Color> bitmap(2, 1);
    bitmap.SetPixel(0, 0, Color(1, 2, 3, 4));
    bitmap.SetPixel(1, 0, Color(250, 251, 252, 253));
    EXPECT_EQ(Hex(bitmap.GetPixelData()), Expected("color/pixel_data_layout"));

    PixelBitmapContent<Color> restored(2, 1);
    restored.SetPixelData({1, 2, 3, 4, 250, 251, 252, 253});
    EXPECT_EQ(Pixels(restored) + " row0=" + std::to_string(restored.GetRow(0).size()), Expected("color/set_pixel_data_roundtrip"));

    EXPECT_EQ(Throws<System::ArgumentException>([&] { restored.SetPixelData(std::vector<std::uint8_t>(7)); }, "ArgumentException"),
              Expected("color/set_pixel_data_wrong_length"));
    EXPECT_THROW((void)restored.GetPixel(2, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(restored.SetPixel(0, 1, Color::Red), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)restored.GetRow(1), System::ArgumentOutOfRangeException);
}

// color/get_row_is_live and color/get_pixel_data_is_snapshot: GetRow hands out the bitmap's own
// row (writing through it changes the pixel), while GetPixelData hands out a copy.
TEST(XnaGraphicsBitmap, GetRowAliasesTheBitmapAndGetPixelDataDoesNot)
{
    PixelBitmapContent<Color> bitmap(2, 1);
    bitmap.SetPixel(0, 0, Color::Red);
    std::span<Color> row = bitmap.GetRow(0);
    ASSERT_EQ(row.size(), 2u);
    row[0] = Color::Lime;
    EXPECT_EQ("row=" + std::to_string(row.size()) + " pixel=" + bitmap.GetPixel(0, 0).ToString(),
              Expected("color/get_row_is_live"));

    PixelBitmapContent<Color> snapshot(1, 1);
    snapshot.SetPixel(0, 0, Color::Red);
    std::vector<std::uint8_t> data = snapshot.GetPixelData();
    ASSERT_FALSE(data.empty());
    data[0] = 0x7F;
    EXPECT_EQ("pixel=" + snapshot.GetPixel(0, 0).ToString(), Expected("color/get_pixel_data_is_snapshot"));
    EXPECT_THROW(PixelBitmapContent<Color>(0, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(PixelBitmapContent<Color>(-1, 4), System::ArgumentOutOfRangeException);

    auto four = FourPixels();
    four->ReplaceColor(Color(255, 0, 0, 255), Color(1, 2, 3, 4));
    EXPECT_EQ(Pixels(*four), Expected("color/replace_color"));
    EXPECT_EQ(Pixels(PixelBitmapContent<Color>(2, 1)), Expected("color/default_pixels"));

    PixelBitmapContent<Vector4> vectors(1, 1);
    vectors.SetPixel(0, 0, Vector4(1, 0.5f, -2, 1e-3f));
    EXPECT_EQ(Hex(vectors.GetPixelData()), Expected("vector4/pixel_data_layout"));
}

TEST(XnaGraphicsBitmap, RegionsAndValidationMatchXna)
{
    auto target = std::make_shared<PixelBitmapContent<Color>>(4, 4);
    BitmapContent::Copy(Gradient(4, 4), Rectangle(1, 1, 2, 2), target, Rectangle(0, 0, 2, 2));
    EXPECT_EQ(Pixels(*target), Expected("region/copy_subrect"));

    EXPECT_THROW(BitmapContent::Copy(Gradient(4, 4), Rectangle(2, 2, 4, 4), std::make_shared<PixelBitmapContent<Color>>(4, 4), Rectangle(0, 0, 4, 4)),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(BitmapContent::Copy(Gradient(4, 4), Rectangle(0, 0, 4, 4), std::make_shared<PixelBitmapContent<Color>>(4, 4), Rectangle(1, 0, 4, 4)),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(BitmapContent::Copy(Gradient(4, 4), Rectangle(0, 0, -1, 4), std::make_shared<PixelBitmapContent<Color>>(4, 4), Rectangle(0, 0, 4, 4)),
                 System::ArgumentOutOfRangeException);
    EXPECT_NO_THROW(BitmapContent::Copy(Gradient(4, 4), Rectangle(0, 0, 0, 0), std::make_shared<PixelBitmapContent<Color>>(4, 4), Rectangle(0, 0, 0, 0)));
    EXPECT_THROW(BitmapContent::Copy(nullptr, std::make_shared<PixelBitmapContent<Color>>(1, 1)), System::ArgumentNullException);
    EXPECT_THROW(BitmapContent::Copy(std::make_shared<PixelBitmapContent<Color>>(1, 1), nullptr), System::ArgumentNullException);

    auto same = FourPixels();
    BitmapContent::Copy(same, same);
    EXPECT_EQ(Pixels(*same), Expected("copy/same_instance"));
    auto overlapping = Gradient(4, 4);
    BitmapContent::Copy(overlapping, Rectangle(0, 0, 2, 2), overlapping, Rectangle(1, 1, 2, 2));
    EXPECT_EQ(Pixels(*overlapping), Expected("copy/same_instance_overlapping_regions"));
}

TEST(XnaGraphicsBitmap, ResamplingStaysWithinToleranceOfXna)
{
    // XNA resamples through D3DX; the kernel is not documented, so CNA's bilinear enlargement and
    // box reduction are compared within a channel tolerance and the largest deviation is reported.
    struct Case { const char* name; std::function<std::string()> run; int tolerance; };
    const std::vector<Case> cases = {
        {"resize/4x4_to_2x2", [] { auto t = std::make_shared<PixelBitmapContent<Color>>(2, 2); BitmapContent::Copy(Gradient(4, 4), t); return Pixels(*t); }, 8},
        {"resize/2x2_to_4x4", [] { auto t = std::make_shared<PixelBitmapContent<Color>>(4, 4); BitmapContent::Copy(FourPixels(), t); return Pixels(*t); }, 3},
        {"resize/3x3_to_2x2", [] { auto t = std::make_shared<PixelBitmapContent<Color>>(2, 2); BitmapContent::Copy(Gradient(3, 3), t); return Pixels(*t); }, 12},
        {"region/copy_subrect_resized", [] { auto t = std::make_shared<PixelBitmapContent<Color>>(4, 4); BitmapContent::Copy(Gradient(4, 4), Rectangle(0, 0, 2, 2), t, Rectangle(0, 0, 4, 4)); return Pixels(*t); }, 3},
    };
    for (const Case& c : cases)
    {
        const int delta = MaxChannelDelta(ParsePixels(c.run()), ParsePixels(Expected(c.name)));
        EXPECT_LE(delta, c.tolerance) << c.name << ": CNA " << c.run() << " XNA " << Expected(c.name);
        std::printf("  %s: largest channel deviation from D3DX %d\n", c.name, delta);
    }
}

TEST(XnaGraphicsBitmap, DxtSizesMessagesAndDecodingMatchXna)
{
    struct Kind { const char* name; std::function<std::shared_ptr<Graphics::DxtBitmapContent>(int, int)> make; };
    const std::vector<Kind> kinds = {
        {"Dxt1", [](int w, int h) { return std::make_shared<Dxt1BitmapContent>(w, h); }},
        {"Dxt3", [](int w, int h) { return std::make_shared<Dxt3BitmapContent>(w, h); }},
        {"Dxt5", [](int w, int h) { return std::make_shared<Dxt5BitmapContent>(w, h); }},
    };
    for (const Kind& kind : kinds)
    {
        const std::string prefix = std::string("dxt/") + kind.name + "/";
        EXPECT_EQ(Describe(*kind.make(8, 4)), Expected(prefix + "describe_8x4"));
        EXPECT_EQ(Describe(*kind.make(5, 3)), Expected(prefix + "describe_5x3"));
        EXPECT_EQ(Describe(*kind.make(1, 1)), Expected(prefix + "describe_1x1"));
        EXPECT_EQ(Throws<System::ArgumentException>([&] { kind.make(8, 4)->SetPixelData(std::vector<std::uint8_t>(7)); }, "ArgumentException"),
                  Expected(prefix + "set_wrong_length"));
        // Decoder parity: XNA's blocks through CNA's decoder must give XNA's pixels (565 expansion
        // rounding aside).
        for (const char* encoded : {"encode_gradient_8x4", "encode_solid_8x4", "encode_transparent_8x4"})
        {
            const std::string expected = Expected(prefix + encoded);
            auto bitmap = kind.make(8, 4);
            bitmap->SetPixelData(ParseHex(Field(expected, "data")));
            auto back = std::make_shared<PixelBitmapContent<Color>>(8, 4);
            BitmapContent::Copy(bitmap, back);
            const int delta = MaxChannelDelta(ParsePixels(Pixels(*back)), ParsePixels(Field(expected, "back")));
            EXPECT_LE(delta, 3) << prefix << encoded << " decode: CNA " << Pixels(*back) << " XNA " << Field(expected, "back");
        }
        // Encoder sanity: CNA's own blocks decode close to the source (the encoders differ by design,
        // so the bytes are not compared).
        auto encoded = kind.make(8, 4);
        auto solid = std::make_shared<PixelBitmapContent<Color>>(8, 4);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) solid->SetPixel(x, y, Color(200, 100, 50, 255));
        BitmapContent::Copy(solid, encoded);
        EXPECT_EQ(encoded->GetPixelData().size(), ParseHex(Field(Expected(prefix + "encode_solid_8x4"), "data")).size());
        auto decoded = std::make_shared<PixelBitmapContent<Color>>(8, 4);
        BitmapContent::Copy(encoded, decoded);
        EXPECT_LE(MaxChannelDelta(ParsePixels(Pixels(*decoded)), ParsePixels(Pixels(*solid))), 8) << prefix << "solid round trip";
        // A 5x3 bitmap still has two blocks and copies in.
        auto odd = kind.make(5, 3);
        BitmapContent::Copy(Gradient(5, 3), odd);
        EXPECT_EQ(odd->GetPixelData().size(), ParseHex(Field(Expected(prefix + "copy_from_5x3"), "data")).size());
    }
}

TEST(XnaGraphicsBitmap, MipmapChainsMatchXna)
{
    const std::shared_ptr<BitmapContent> bitmap = Gradient(2, 2);
    MipmapChain implicit = bitmap;
    EXPECT_EQ("count=" + std::to_string(implicit.getCountProperty()) + " ToString=\"" + implicit.ToString() + "\"", Expected("mipmapchain/implicit"));
    EXPECT_EQ(MipmapChain().getCountProperty(), 0);
    EXPECT_THROW(MipmapChain().Add(nullptr), System::ArgumentNullException);
    EXPECT_THROW(MipmapChain(Gradient(2, 2)).setItem(0, nullptr), System::ArgumentNullException);
    EXPECT_THROW(MipmapChain(std::shared_ptr<BitmapContent>()), System::ArgumentNullException);

    Texture2DContent texture;
    EXPECT_EQ("faces=" + std::to_string(texture.getFacesProperty().getCountProperty()) + " mipmaps=" +
                  std::to_string(texture.getMipmapsProperty().getCountProperty()) + " same=True",
              Expected("mipmapchaincollection/texture2d_default"));
    EXPECT_EQ(std::as_const(texture.getFacesProperty())[0].get(), &texture.getMipmapsProperty());
    const std::string fixedMessage = Expected("mipmapchaincollection/texture2d_add_face");
    EXPECT_EQ(Throws<System::NotSupportedException>([&] { texture.getFacesProperty().Add(std::make_shared<MipmapChain>()); }, "NotSupportedException"), fixedMessage);
    EXPECT_EQ(Throws<System::NotSupportedException>([&] { texture.getFacesProperty().RemoveAt(0); }, "NotSupportedException"), fixedMessage);
    EXPECT_EQ(Throws<System::NotSupportedException>([&] { texture.getFacesProperty().Clear(); }, "NotSupportedException"), fixedMessage);
    EXPECT_THROW(texture.getFacesProperty().setItem(0, nullptr), System::ArgumentNullException);
    texture.setMipmapsProperty(std::make_shared<MipmapChain>(Gradient(2, 2)));
    EXPECT_EQ(texture.getMipmapsProperty().getCountProperty(), 1);
    EXPECT_EQ(TextureCubeContent().getFacesProperty().getCountProperty(), 6);
    EXPECT_THROW(TextureCubeContent().getFacesProperty().Add(std::make_shared<MipmapChain>()), System::NotSupportedException);
    Texture3DContent volume;
    EXPECT_EQ(volume.getFacesProperty().getCountProperty(), 0);
    volume.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(2, 2)));
    EXPECT_EQ(volume.getFacesProperty().getCountProperty(), 1);
    EXPECT_EQ(Texture2DContent().ToString(), Expected("texture/tostring"));
}

TEST(XnaGraphicsBitmap, MipmapGenerationAndConversionMatchXna)
{
    const auto shape = [](TextureContent& texture, const MipmapChain& chain) { (void)texture; return DescribeChain(chain); };
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(5, 3));
        t.GenerateMipmaps(false);
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/generate_mipmaps_5x3"));
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(8, 2));
        t.GenerateMipmaps(false);
        const std::string expected = Expected("texture/generate_mipmaps_8x2");
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), expected.substr(0, expected.find(" level1=")));
        for (int level = 1; level <= 3; ++level)
        {
            const auto& bitmap = dynamic_cast<const PixelBitmapContent<Color>&>(*std::as_const(t.getMipmapsProperty())[level]);
            const int delta = MaxChannelDelta(ParsePixels(Pixels(bitmap)), ParsePixels(Field(expected, "level" + std::to_string(level))));
            EXPECT_LE(delta, 24) << "8x2 mip level " << level << ": CNA " << Pixels(bitmap);
            std::printf("  generate_mipmaps_8x2 level %d: largest channel deviation from D3DX %d\n", level, delta);
        }
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(4, 4));
        t.getMipmapsProperty().Add(Gradient(2, 2));
        t.GenerateMipmaps(false);
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/generate_mipmaps_keeps_existing"));
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(4, 4));
        t.getMipmapsProperty().Add(FourPixels());
        t.GenerateMipmaps(true);
        const std::string expected = Expected("texture/generate_mipmaps_overwrite");
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), expected.substr(0, expected.find(" level1=")));
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(1, 1));
        t.GenerateMipmaps(false);
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/generate_mipmaps_1x1"));
        Texture2DContent empty;
        empty.GenerateMipmaps(false);
        EXPECT_EQ(shape(empty, empty.getMipmapsProperty()), Expected("texture/generate_mipmaps_empty"));
    }
    {
        Texture2DContent t;
        auto v = std::make_shared<PixelBitmapContent<Vector4>>(4, 2);
        BitmapContent::Copy(Gradient(4, 2), v);
        t.getMipmapsProperty().Add(v);
        t.GenerateMipmaps(false);
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/generate_mipmaps_vector4"));
    }
    {
        Texture2DContent t;
        auto dxt = std::make_shared<Dxt1BitmapContent>(8, 8);
        BitmapContent::Copy(Gradient(8, 8), dxt);
        t.getMipmapsProperty().Add(dxt);
        t.GenerateMipmaps(false);
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/generate_mipmaps_dxt"));
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(4, 4));
        t.GenerateMipmaps(false);
        t.ConvertBitmapType<PixelBitmapContent<Vector4>>();
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/convert_to_vector4"));
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(5, 3));
        t.ConvertBitmapType<Dxt1BitmapContent>();
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/convert_to_dxt1_5x3"));
    }
    {
        Texture2DContent t;
        t.getMipmapsProperty().Add(Gradient(8, 8));
        t.GenerateMipmaps(false);
        t.ConvertBitmapType<Dxt5BitmapContent>();
        EXPECT_EQ(shape(t, t.getMipmapsProperty()), Expected("texture/convert_to_dxt5_8x8_mips"));
    }
    {
        Texture2DContent t;
        auto bitmap = Gradient(2, 2);
        t.getMipmapsProperty().Add(bitmap);
        EXPECT_THROW(t.ConvertBitmapType(System::Type()), System::ArgumentNullException);
        try
        {
            t.ConvertBitmapType(System::Type::From<std::string>());
            FAIL() << "a non-bitmap type must be refused";
        }
        catch (const System::ArgumentException& error)
        {
            const std::string message = error.what();
            EXPECT_EQ(message.rfind("ConvertBitmapType cannot convert to ", 0), 0u) << message;
            EXPECT_NE(message.find("The target type must be derived from BitmapContent."), std::string::npos) << message;
        }
        t.ConvertBitmapType<PixelBitmapContent<Color>>();
        EXPECT_EQ(std::as_const(t.getMipmapsProperty())[0], bitmap) << "same type: the bitmap is kept";
    }
}

TEST(XnaGraphicsBitmap, ValidateMessagesMatchXna)
{
    const auto twoD = [](std::shared_ptr<BitmapContent> bitmap, std::optional<GraphicsProfile> profile)
    {
        return ValidateResult([&] { Texture2DContent t; if (bitmap) t.getMipmapsProperty().Add(bitmap); t.Validate(profile); });
    };
    EXPECT_EQ(twoD(nullptr, std::nullopt), Expected("validate/2d_empty_null_profile"));
    EXPECT_EQ(twoD(nullptr, GraphicsProfile::Reach), Expected("validate/2d_empty_reach"));
    EXPECT_EQ(twoD(Gradient(5, 3), GraphicsProfile::Reach), Expected("validate/2d_5x3_reach"));
    EXPECT_EQ(twoD(Gradient(5, 3), GraphicsProfile::HiDef), Expected("validate/2d_5x3_hidef"));
    EXPECT_EQ(twoD(Gradient(5, 3), std::nullopt), Expected("validate/2d_5x3_null"));
    EXPECT_EQ(twoD(Gradient(4, 4), GraphicsProfile::Reach), Expected("validate/2d_4x4_reach"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<Color>>(4096, 4), GraphicsProfile::Reach), Expected("validate/2d_4096x4_reach"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<Color>>(4096, 4), GraphicsProfile::HiDef), Expected("validate/2d_4096x4_hidef"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<Color>>(8192, 4), GraphicsProfile::HiDef), Expected("validate/2d_8192x4_hidef"));
    EXPECT_EQ(ValidateResult([] { Texture2DContent t; t.getMipmapsProperty().Add(Gradient(4, 4)); t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4))); t.Validate(std::nullopt); }),
              Expected("validate/2d_two_faces"));
    EXPECT_EQ(ValidateResult([] { Texture2DContent t; t.getMipmapsProperty().Add(Gradient(4, 4)); t.getMipmapsProperty().Add(Gradient(3, 3)); t.Validate(std::nullopt); }),
              Expected("validate/2d_bad_mip_chain"));
    EXPECT_EQ(ValidateResult([] { Texture2DContent t; t.getMipmapsProperty().Add(Gradient(4, 4)); t.getMipmapsProperty().Add(Gradient(2, 2)); t.Validate(std::nullopt); }),
              Expected("validate/2d_incomplete_mip_chain"));
    EXPECT_EQ(ValidateResult([] { Texture2DContent t; t.getMipmapsProperty().Add(Gradient(4, 4)); t.getMipmapsProperty().Add(std::make_shared<PixelBitmapContent<Vector4>>(2, 2)); t.getMipmapsProperty().Add(std::make_shared<PixelBitmapContent<Color>>(1, 1)); t.Validate(std::nullopt); }),
              Expected("validate/2d_mixed_types_in_chain"));
    EXPECT_EQ(twoD(std::make_shared<Dxt1BitmapContent>(5, 3), GraphicsProfile::Reach), Expected("validate/2d_dxt_5x3_reach"));
    EXPECT_EQ(twoD(std::make_shared<Dxt1BitmapContent>(5, 3), GraphicsProfile::HiDef), Expected("validate/2d_dxt_5x3_hidef"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<Vector4>>(4, 4), GraphicsProfile::Reach), Expected("validate/2d_vector4_reach"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<Vector4>>(4, 4), GraphicsProfile::HiDef), Expected("validate/2d_vector4_hidef"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<PackedVector::Short4>>(4, 4), std::nullopt), Expected("validate/2d_short4_null"));
    EXPECT_EQ(twoD(std::make_shared<PixelBitmapContent<Vector3>>(4, 4), std::nullopt), Expected("validate/2d_vector3_null"));

    const auto cube = [](int missingFrom, int oddFace, int width, int height, std::optional<GraphicsProfile> profile)
    {
        return ValidateResult([&]
        {
            TextureCubeContent t;
            for (int i = 0; i < missingFrom; ++i)
            {
                std::as_const(t.getFacesProperty())[i]->Add(i == oddFace ? Gradient(2, 2) : Gradient(width, height));
            }
            t.Validate(profile);
        });
    };
    EXPECT_EQ(cube(0, -1, 4, 4, std::nullopt), Expected("validate/cube_empty"));
    EXPECT_EQ(cube(6, -1, 4, 4, std::nullopt), Expected("validate/cube_six_4x4"));
    EXPECT_EQ(cube(6, -1, 4, 4, GraphicsProfile::Reach), Expected("validate/cube_six_4x4_reach"));
    EXPECT_EQ(cube(5, -1, 4, 4, std::nullopt), Expected("validate/cube_one_face_missing"));
    EXPECT_EQ(cube(6, -1, 4, 2, std::nullopt), Expected("validate/cube_non_square"));
    EXPECT_EQ(cube(6, 3, 4, 4, std::nullopt), Expected("validate/cube_different_sizes"));
    {
        TextureCubeContent t;
        for (int i = 0; i < 6; ++i) std::as_const(t.getFacesProperty())[i]->Add(Gradient(4, 4));
        t.GenerateMipmaps(false);
        EXPECT_EQ("face0=" + DescribeChain(*std::as_const(t.getFacesProperty())[0]) + " face5=" + DescribeChain(*std::as_const(t.getFacesProperty())[5]),
                  Expected("validate/cube_generate_mipmaps"));
    }

    EXPECT_EQ(ValidateResult([] { Texture3DContent t; t.Validate(std::nullopt); }), Expected("validate/3d_empty"));
    EXPECT_EQ(ValidateResult([] { Texture3DContent t; t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4))); t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4))); t.Validate(std::nullopt); }),
              Expected("validate/3d_depth2_4x4"));
    {
        Texture3DContent t;
        for (int i = 0; i < 4; ++i) t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4)));
        t.GenerateMipmaps(false);
        std::string result = "faces=" + std::to_string(t.getFacesProperty().getCountProperty());
        for (int i = 0; i < t.getFacesProperty().getCountProperty(); ++i)
            result += " face" + std::to_string(i) + "=" + DescribeChain(*std::as_const(t.getFacesProperty())[i]);
        EXPECT_EQ(result, Expected("validate/3d_depth3_generate_mipmaps"));
    }
    EXPECT_EQ(ValidateResult([] { Texture3DContent t; t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4))); t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(2, 2))); t.Validate(std::nullopt); }),
              Expected("validate/3d_depth2_different_sizes"));
    EXPECT_EQ(ValidateResult([] { Texture3DContent t; t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4))); t.Validate(GraphicsProfile::Reach); }),
              Expected("validate/3d_reach"));
    EXPECT_EQ(ValidateResult([] { Texture3DContent t; t.getFacesProperty().Add(std::make_shared<MipmapChain>(Gradient(4, 4))); t.Validate(GraphicsProfile::HiDef); }),
              Expected("validate/3d_hidef"));
}

TEST(XnaGraphicsBitmap, VectorConverterTablesMatchXna)
{
    for (const auto& [format, name] : std::map<SurfaceFormat, std::string>{
             {SurfaceFormat::Color, "Color"}, {SurfaceFormat::Bgr565, "Bgr565"}, {SurfaceFormat::Bgra5551, "Bgra5551"},
             {SurfaceFormat::Bgra4444, "Bgra4444"}, {SurfaceFormat::Dxt1, "Dxt1"}, {SurfaceFormat::Dxt3, "Dxt3"},
             {SurfaceFormat::Dxt5, "Dxt5"}, {SurfaceFormat::NormalizedByte2, "NormalizedByte2"},
             {SurfaceFormat::NormalizedByte4, "NormalizedByte4"}, {SurfaceFormat::Rgba1010102, "Rgba1010102"},
             {SurfaceFormat::Rg32, "Rg32"}, {SurfaceFormat::Rgba64, "Rgba64"}, {SurfaceFormat::Alpha8, "Alpha8"},
             {SurfaceFormat::Single, "Single"}, {SurfaceFormat::Vector2, "Vector2"}, {SurfaceFormat::Vector4, "Vector4"},
             {SurfaceFormat::HalfSingle, "HalfSingle"}, {SurfaceFormat::HalfVector2, "HalfVector2"},
             {SurfaceFormat::HalfVector4, "HalfVector4"}, {SurfaceFormat::HdrBlendable, "HdrBlendable"}})
    {
        System::Type type;
        const std::string actual = VectorConverter::TryGetVectorType(format, type) ? VectorConverter::VectorTypeName(type) : "none";
        EXPECT_EQ(actual, Expected("vectorconverter/surface/" + name)) << name;
    }
    for (const auto& [format, name] : std::map<VertexElementFormat, std::string>{
             {VertexElementFormat::Single, "Single"}, {VertexElementFormat::Vector2, "Vector2"},
             {VertexElementFormat::Vector3, "Vector3"}, {VertexElementFormat::Vector4, "Vector4"},
             {VertexElementFormat::Color, "Color"}, {VertexElementFormat::Byte4, "Byte4"},
             {VertexElementFormat::Short2, "Short2"}, {VertexElementFormat::Short4, "Short4"},
             {VertexElementFormat::NormalizedShort2, "NormalizedShort2"}, {VertexElementFormat::NormalizedShort4, "NormalizedShort4"},
             {VertexElementFormat::HalfVector2, "HalfVector2"}, {VertexElementFormat::HalfVector4, "HalfVector4"}})
    {
        System::Type type;
        const std::string actual = VectorConverter::TryGetVectorType(format, type) ? VectorConverter::VectorTypeName(type) : "none";
        EXPECT_EQ(actual, Expected("vectorconverter/vertex/" + name)) << name;
    }
    EXPECT_EQ((VectorConverter::GetConverter<Color, Color>()(Color(1, 2, 3, 4))), Color(1, 2, 3, 4));
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%04X", VectorConverter::GetConverter<Color, PackedVector::Bgr565>()(Color(10, 20, 30, 40)).getPackedValueProperty());
    EXPECT_EQ(std::string(buffer), Expected("vectorconverter/converter_color_to_bgr565"));
    EXPECT_NEAR((VectorConverter::GetConverter<Color, float>()(Color(10, 20, 30, 40))), std::stof(Expected("vectorconverter/converter_color_to_single")), 1e-7f);
    EXPECT_EQ((VectorConverter::GetConverter<float, Color>()(0.5f)), Color(128, 0, 0, 255));
    EXPECT_EQ((VectorConverter::GetConverter<PackedVector::Alpha8, Color>()(PackedVector::Alpha8(0.5f))), Color(0, 0, 0, 128));
    std::snprintf(buffer, sizeof buffer, "%02X", VectorConverter::GetConverter<Color, PackedVector::Alpha8>()(Color(10, 20, 30, 40)).getPackedValueProperty());
    EXPECT_EQ(std::string(buffer), Expected("vectorconverter/converter_color_to_alpha8"));
    EXPECT_EQ((VectorConverter::GetConverter<Vector2, Color>()(Vector2(0.25f, 0.5f))), Color(64, 128, 0, 255));
    const Vector2 v2 = VectorConverter::GetConverter<Color, Vector2>()(Color(10, 20, 30, 40));
    const std::vector<float> expected2 = ParseFloats(Expected("vectorconverter/converter_color_to_vector2"));
    EXPECT_NEAR(v2.X, expected2[0], 1e-7f);
    EXPECT_NEAR(v2.Y, expected2[1], 1e-7f);
    PackedVector::Byte4 byte4;
    byte4.PackFromVector4(Vector4(10, 20, 30, 40));
    EXPECT_EQ((VectorConverter::GetConverter<PackedVector::Byte4, Color>()(byte4)), Color(255, 255, 255, 255));
    EXPECT_EQ((VectorConverter::GetConverter<Vector4, Color>()(Vector4(2, -1, 0.5f, 1))), Color(255, 0, 128, 255));
    Graphics::TextureReferenceDictionary references;
    EXPECT_EQ("count=" + std::to_string(references.getCountProperty()) + " ToString=\"" + references.ToString() + "\"", Expected("texturereferencedictionary/default"));
}
