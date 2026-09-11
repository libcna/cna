// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-094, 098: MaterialContent, the six stock materials,
// EffectContent and CompiledEffectContent against what the genuine XNA 4.0 pipeline does with the
// same inputs (tests/reference/xna40/graphics/graphics-content-oracle.json, cases material/*,
// effectcontent/* and compiledeffect/*).
//
// What the measurements establish: a material has no fields, only views over the two dictionaries
// it inherits and owns, so setting a property to null removes its entry and reading a property
// whose stored value has another type answers null rather than refusing. The texture properties
// live in Textures and everything else in OpaqueData -- except an effect material's two
// references, which are opaque data too.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MaterialContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;
namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Pipeline::ExternalReference;
using Pipeline::Processors::CompiledEffectContent;
using Graphics::AlphaTestMaterialContent;
using Graphics::BasicMaterialContent;
using Graphics::DualTextureMaterialContent;
using Graphics::EffectContent;
using Graphics::EffectMaterialContent;
using Graphics::EnvironmentMapMaterialContent;
using Graphics::MaterialContent;
using Graphics::SkinnedMaterialContent;
using Graphics::TextureContent;
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

    /**
     * @brief The measured text, with the two hosts' differences removed: the .NET Framework's
     *        "\nParameter name: key" tail, and the absolute path the runtime resolved a relative
     *        external reference to (its Wine drive letter is not a property of XNA).
     */
    std::string Normalize(const std::string& result)
    {
        std::string text = result;
        const std::size_t parameter = text.find("Parameter name:");
        if (parameter != std::string::npos)
        {
            std::size_t cut = parameter;
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
        // Reduce an absolute path to its last segment: the runtime resolved every relative
        // external reference against its own working directory, and that drive letter is a
        // property of the Wine host, not of XNA.
        static const std::regex windowsPath("[A-Za-z]:\\\\[^ <\"\\n]*");
        static const std::regex unixPath("/[^ <\"\\n]+");
        const auto lastSegment = [](const std::smatch& match)
        {
            const std::string path = match.str();
            const std::size_t slash = path.find_last_of("/\\\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        };
        std::string reduced;
        auto begin = std::sregex_iterator(text.begin(), text.end(), windowsPath);
        std::size_t copied = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it)
        {
            reduced += text.substr(copied, static_cast<std::size_t>(it->position()) - copied);
            reduced += lastSegment(*it);
            copied = static_cast<std::size_t>(it->position() + it->length());
        }
        reduced += text.substr(copied);
        text = reduced;
        reduced.clear();
        copied = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), unixPath); it != std::sregex_iterator(); ++it)
        {
            reduced += text.substr(copied, static_cast<std::size_t>(it->position()) - copied);
            reduced += lastSegment(*it);
            copied = static_cast<std::size_t>(it->position() + it->length());
        }
        reduced += text.substr(copied);
        return reduced;
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
        return found == Oracle().end() ? std::string("<missing case ") + name + ">" : Normalize(found->second);
    }

    // --------------------------------------------------------------------------------------------
    // The oracle's formatting, reproduced
    // --------------------------------------------------------------------------------------------
    std::string Number(float value)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << value;
        return text.str();
    }

    std::string VectorText(const Vector3& value)
    {
        return "{X:" + Number(value.X) + " Y:" + Number(value.Y) + " Z:" + Number(value.Z) + "}";
    }

    std::string OpaqueText(const Pipeline::ContentObject& value)
    {
        // The oracle prints value:TypeName for every opaque entry, using the .NET simple type name.
        if (Pipeline::Holds<float>(value))
        {
            return Number(Pipeline::Unbox<float>(value)) + ":Single";
        }
        if (Pipeline::Holds<Vector3>(value))
        {
            return VectorText(Pipeline::Unbox<Vector3>(value)) + ":Vector3";
        }
        if (Pipeline::Holds<bool>(value))
        {
            return std::string(Pipeline::Unbox<bool>(value) ? "True" : "False") + ":Boolean";
        }
        if (Pipeline::Holds<std::int32_t>(value))
        {
            return std::to_string(Pipeline::Unbox<std::int32_t>(value)) + ":Int32";
        }
        if (Pipeline::Holds<CompareFunction>(value))
        {
            static const std::map<CompareFunction, std::string> names = {
                {CompareFunction::Always, "Always"},   {CompareFunction::Never, "Never"},
                {CompareFunction::Less, "Less"},       {CompareFunction::LessEqual, "LessEqual"},
                {CompareFunction::Equal, "Equal"},     {CompareFunction::GreaterEqual, "GreaterEqual"},
                {CompareFunction::Greater, "Greater"}, {CompareFunction::NotEqual, "NotEqual"}};
            return names.at(Pipeline::Unbox<CompareFunction>(value)) + ":CompareFunction";
        }
        if (Pipeline::Holds<std::string>(value))
        {
            return Pipeline::Unbox<std::string>(value) + ":String";
        }
        // The two external references an effect material stores print as their .NET type name.
        return value.StableType() + ":ExternalReference`1";
    }

    std::string DescribeMaterial(const MaterialContent& material, const std::string& typeName)
    {
        std::string text = typeName + " opaque={";
        bool first = true;
        for (const std::string& key : material.getOpaqueDataProperty().getKeysProperty())
        {
            if (!first)
            {
                text += ' ';
            }
            first = false;
            Pipeline::ContentObject stored;
            material.getOpaqueDataProperty().TryGetValue(key, stored);
            text += key + "=" + OpaqueText(stored);
        }
        text += "} textures={";
        first = true;
        for (const std::string& key : material.getTexturesProperty().getKeysProperty())
        {
            if (!first)
            {
                text += ' ';
            }
            first = false;
            std::shared_ptr<ExternalReference<TextureContent>> reference;
            material.getTexturesProperty().TryGetValue(key, reference);
            text += key + "=" + (reference == nullptr ? "null" : reference->getFilenameProperty());
        }
        text += "}";
        return Normalize(text);
    }

    std::string Result(const std::function<std::string()>& body)
    {
        try
        {
            return Normalize(body());
        }
        catch (const System::ArgumentNullException& error)
        {
            return Normalize("throws ArgumentNullException: " + error.getMessageProperty());
        }
        catch (const System::Exception& error)
        {
            return Normalize("throws Exception: " + error.getMessageProperty());
        }
    }

    std::shared_ptr<ExternalReference<TextureContent>> TextureRef(const std::string& filename)
    {
        return std::make_shared<ExternalReference<TextureContent>>(filename);
    }

    template<typename T>
    std::string Serialize(const std::shared_ptr<T>& value)
    {
        System::Xml::XmlWriterSettings settings;
        settings.Indent = true;
        settings.NewLineChars = "\r\n";
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
        IntermediateSerializer::Serialize<std::shared_ptr<T>>(*writer, value, std::string());
        std::string xml = writer->ToString();
        const std::size_t cut = xml.find("?>");
        if (cut != std::string::npos)
        {
            xml = xml.substr(cut + 2);
        }
        while (!xml.empty() && (xml.front() == '\r' || xml.front() == '\n'))
        {
            xml.erase(xml.begin());
        }
        return Normalize(xml);
    }

    template<typename T>
    std::shared_ptr<T> Deserialize(const std::string& xml)
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        return IntermediateSerializer::Deserialize<std::shared_ptr<T>>(*reader, std::string());
    }
}

TEST(XnaMaterialContent, OracleIsPresent)
{
    ASSERT_GE(Oracle().size(), 300u) << CorpusFile();
}

TEST(XnaMaterialContent, EmptyMaterialsMatchXna)
{
    EXPECT_EQ(DescribeMaterial(MaterialContent(), "MaterialContent"), Expected("material/base_defaults"));
    EXPECT_EQ(DescribeMaterial(BasicMaterialContent(), "BasicMaterialContent"), Expected("material/basic_defaults"));
}

TEST(XnaMaterialContent, BasicMaterialPropertiesMatchXna)
{
    BasicMaterialContent material;
    material.setAlphaProperty(0.5f);
    material.setDiffuseColorProperty(Vector3(1, 0, 0));
    material.setEmissiveColorProperty(Vector3(0, 1, 0));
    material.setSpecularColorProperty(Vector3(0, 0, 1));
    material.setSpecularPowerProperty(16.0f);
    material.setVertexColorEnabledProperty(true);
    material.setTextureProperty(TextureRef("cat.tga"));
    const std::string read = " read=" + Number(*material.getAlphaProperty()) + "," +
                             VectorText(*material.getDiffuseColorProperty()) + "," +
                             Number(*material.getSpecularPowerProperty()) + "," +
                             (*material.getVertexColorEnabledProperty() ? "True" : "False") + "," +
                             material.getTextureProperty()->getFilenameProperty();
    EXPECT_EQ(Normalize(DescribeMaterial(material, "BasicMaterialContent") + read),
              Expected("material/basic_properties"));
}

TEST(XnaMaterialContent, ClearingAPropertyRemovesItsEntry)
{
    BasicMaterialContent cleared;
    cleared.setAlphaProperty(0.5f);
    cleared.setAlphaProperty(std::nullopt);
    EXPECT_EQ(DescribeMaterial(cleared, "BasicMaterialContent") + " read=" +
                  (cleared.getAlphaProperty().has_value() ? "set" : "null"),
              Expected("material/basic_property_cleared"));

    BasicMaterialContent untextured;
    untextured.setTextureProperty(TextureRef("cat.tga"));
    untextured.setTextureProperty(nullptr);
    EXPECT_EQ(DescribeMaterial(untextured, "BasicMaterialContent") + " read=" +
                  (untextured.getTextureProperty() == nullptr ? "null" : "set"),
              Expected("material/basic_texture_cleared"));
}

TEST(XnaMaterialContent, PropertiesAreViewsOverTheDictionaries)
{
    BasicMaterialContent stored;
    stored.getOpaqueDataProperty().SetValue<float>(std::string(BasicMaterialContent::AlphaKey), 0.25f);
    EXPECT_EQ("alpha=" + Number(*stored.getAlphaProperty()) + " " +
                  DescribeMaterial(stored, "BasicMaterialContent"),
              Expected("material/opaque_data_is_the_store"));

    BasicMaterialContent textured;
    textured.getTexturesProperty().Add("Texture", TextureRef("cat.tga"));
    EXPECT_EQ(Normalize("texture=" + textured.getTextureProperty()->getFilenameProperty() + " " +
                        DescribeMaterial(textured, "BasicMaterialContent")),
              Expected("material/textures_direct"));
}

TEST(XnaMaterialContent, AWrongTypeReadsAsNull)
{
    BasicMaterialContent value;
    value.getOpaqueDataProperty().SetValue<std::string>(std::string(BasicMaterialContent::AlphaKey), "not a float");
    EXPECT_EQ("alpha=" + std::string(value.getAlphaProperty().has_value() ? "set" : ""),
              Expected("material/value_property_wrong_type"));

    BasicMaterialContent reference;
    reference.getOpaqueDataProperty().SetValue<std::int32_t>(std::string(BasicMaterialContent::TextureKey), 42);
    EXPECT_EQ("texture=" + std::string(reference.getTextureProperty() == nullptr ? "null" : "set"),
              Expected("material/reference_property_wrong_type"));
}

TEST(XnaMaterialContent, MissingPropertiesReadAsNull)
{
    const BasicMaterialContent material;
    EXPECT_EQ("alpha=", Expected("material/value_property_missing"));
    EXPECT_EQ("value=null", Expected("material/reference_property_missing"));
    EXPECT_EQ("texture=null", Expected("material/get_texture_missing"));
    // The three above are what a material with nothing set answers, which is what this asserts.
    EXPECT_FALSE(material.getAlphaProperty().has_value());
    EXPECT_EQ(material.getTextureProperty(), nullptr);
}

TEST(XnaMaterialContent, AnEmptyKeyIsRefused)
{
    EXPECT_EQ(Result([]
                     {
                         BasicMaterialContent material;
                         material.getOpaqueDataProperty();
                         material.setAlphaProperty(1.0f);
                         BasicMaterialContent().getTexturesProperty();
                         // The property setters always pass a key; the refusal is reachable through
                         // the protected surface, which a derived material uses.
                         struct Probe : BasicMaterialContent
                         {
                             using BasicMaterialContent::SetProperty;
                             using BasicMaterialContent::SetTexture;
                         };
                         Probe probe;
                         probe.SetProperty(std::string(), std::optional<float>(1.0f));
                         return DescribeMaterial(probe, "ProbeMaterial");
                     }),
              Expected("material/set_property_null_name"));

    EXPECT_EQ(Result([]
                     {
                         struct Probe : BasicMaterialContent
                         {
                             using BasicMaterialContent::SetTexture;
                         };
                         Probe probe;
                         probe.SetTexture(std::string(), TextureRef("cat.tga"));
                         return DescribeMaterial(probe, "ProbeMaterial");
                     }),
              Expected("material/set_texture_null_name"));
}

TEST(XnaMaterialContent, StockMaterialsStoreWhereXnaStores)
{
    AlphaTestMaterialContent alphaTest;
    alphaTest.setAlphaProperty(1.0f);
    alphaTest.setAlphaFunctionProperty(CompareFunction::GreaterEqual);
    alphaTest.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
    alphaTest.setReferenceAlphaProperty(128);
    alphaTest.setVertexColorEnabledProperty(false);
    alphaTest.setTextureProperty(TextureRef("cat.tga"));
    EXPECT_EQ(DescribeMaterial(alphaTest, "AlphaTestMaterialContent") + " function=GreaterEqual reference=" +
                  std::to_string(*alphaTest.getReferenceAlphaProperty()),
              Expected("material/alphatest_properties"));

    DualTextureMaterialContent dual;
    dual.setAlphaProperty(1.0f);
    dual.setDiffuseColorProperty(Vector3(1, 1, 1));
    dual.setVertexColorEnabledProperty(true);
    dual.setTextureProperty(TextureRef("one.tga"));
    dual.setTexture2Property(TextureRef("two.tga"));
    EXPECT_EQ(DescribeMaterial(dual, "DualTextureMaterialContent"), Expected("material/dualtexture_properties"));

    EnvironmentMapMaterialContent environment;
    environment.setAlphaProperty(1.0f);
    environment.setDiffuseColorProperty(Vector3(1, 1, 1));
    environment.setEmissiveColorProperty(Vector3(0, 0, 0));
    environment.setEnvironmentMapAmountProperty(0.5f);
    environment.setEnvironmentMapSpecularProperty(Vector3(0.25f, 0.25f, 0.25f));
    environment.setFresnelFactorProperty(0.75f);
    environment.setTextureProperty(TextureRef("one.tga"));
    environment.setEnvironmentMapProperty(TextureRef("cube.dds"));
    EXPECT_EQ(DescribeMaterial(environment, "EnvironmentMapMaterialContent"),
              Expected("material/environmentmap_properties"));

    SkinnedMaterialContent skinned;
    skinned.setAlphaProperty(1.0f);
    skinned.setDiffuseColorProperty(Vector3(1, 1, 1));
    skinned.setEmissiveColorProperty(Vector3(0, 0, 0));
    skinned.setSpecularColorProperty(Vector3(1, 1, 1));
    skinned.setSpecularPowerProperty(8.0f);
    skinned.setWeightsPerVertexProperty(2);
    skinned.setTextureProperty(TextureRef("one.tga"));
    EXPECT_EQ(DescribeMaterial(skinned, "SkinnedMaterialContent"), Expected("material/skinned_properties"));
}

TEST(XnaMaterialContent, AnEffectMaterialStoresItsReferencesAsOpaqueData)
{
    EffectMaterialContent material;
    material.setEffectProperty(std::make_shared<ExternalReference<EffectContent>>("shader.fx"));
    material.setCompiledEffectProperty(std::make_shared<ExternalReference<CompiledEffectContent>>("shader.xnb"));
    EXPECT_EQ(material.getOpaqueDataProperty().getCountProperty(), 2);
    EXPECT_EQ(material.getTexturesProperty().getCountProperty(), 0);
    EXPECT_EQ(std::filesystem::path(material.getEffectProperty()->getFilenameProperty()).filename().string(),
              "shader.fx");
    EXPECT_EQ(std::filesystem::path(material.getCompiledEffectProperty()->getFilenameProperty()).filename().string(),
              "shader.xnb");
}

TEST(XnaMaterialContent, ToStringIsTheFullTypeName)
{
    EXPECT_EQ(BasicMaterialContent().ToString() + "|" + MaterialContent().ToString() + "|" +
                  EffectMaterialContent().ToString(),
              Expected("material/tostring"));
}

TEST(XnaMaterialContent, SerializesAsXnaSerializes)
{
    auto material = std::make_shared<BasicMaterialContent>();
    material->setAlphaProperty(0.5f);
    material->setDiffuseColorProperty(Vector3(1, 0, 0));
    material->setVertexColorEnabledProperty(true);
    material->setTextureProperty(TextureRef("cat.tga"));
    EXPECT_EQ(Serialize(material), Expected("material/serialize_basic"));

    EXPECT_EQ(Serialize(std::make_shared<BasicMaterialContent>()), Expected("material/serialize_empty_basic"));
    EXPECT_EQ(Serialize(std::make_shared<MaterialContent>()), Expected("material/serialize_base"));

    auto named = std::make_shared<BasicMaterialContent>();
    named->setNameProperty("TheName");
    named->setAlphaProperty(1.0f);
    EXPECT_EQ(Serialize(named), Expected("material/serialize_with_name"));
}

TEST(XnaMaterialContent, DeserializesWhatXnaWrites)
{
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\" "
        "xmlns:Framework=\"Microsoft.Xna.Framework\">\r\n"
        "  <Asset Type=\"Graphics:BasicMaterialContent\">\r\n"
        "    <OpaqueData>\r\n"
        "      <Data Key=\"Alpha\" Type=\"float\">0.5</Data>\r\n"
        "      <Data Key=\"DiffuseColor\" Type=\"Framework:Vector3\">1 0 0</Data>\r\n"
        "    </OpaqueData>\r\n"
        "  </Asset>\r\n"
        "</XnaContent>\r\n";
    const std::shared_ptr<BasicMaterialContent> material = Deserialize<BasicMaterialContent>(xml);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(DescribeMaterial(*material, "BasicMaterialContent") + " alpha=" +
                  Number(*material->getAlphaProperty()) + " diffuse=" +
                  VectorText(*material->getDiffuseColorProperty()),
              Expected("material/deserialize_basic"));
}

TEST(XnaEffectContent, MatchesXna)
{
    const EffectContent empty;
    EXPECT_EQ("code=" + std::string(empty.getEffectCodeProperty().has_value() ? "set" : "null") + " name=\"" +
                  empty.getNameProperty() + "\" opaquedata=" +
                  std::to_string(empty.getOpaqueDataProperty().getCountProperty()),
              Expected("effectcontent/defaults"));

    EffectContent code;
    code.setEffectCodeProperty("technique T { }");
    EXPECT_EQ("code=" + *code.getEffectCodeProperty(), Expected("effectcontent/set_code"));

    code.setEffectCodeProperty(std::nullopt);
    EXPECT_EQ("code=" + std::string(code.getEffectCodeProperty().has_value() ? "set" : "null"),
              Expected("effectcontent/set_code_null"));

    auto serialized = std::make_shared<EffectContent>();
    serialized->setEffectCodeProperty("technique T { }");
    EXPECT_EQ(Serialize(serialized), Expected("effectcontent/serialize"));
    EXPECT_EQ(Serialize(std::make_shared<EffectContent>()), Expected("effectcontent/serialize_null_code"));

    auto annotated = std::make_shared<EffectContent>();
    annotated->setEffectCodeProperty("technique T { }");
    annotated->getOpaqueDataProperty().SetValue<std::string>("Note", "hello");
    annotated->setNameProperty("TheName");
    EXPECT_EQ(Serialize(annotated), Expected("effectcontent/serialize_with_opaquedata"));
}

TEST(XnaCompiledEffectContent, MatchesXna)
{
    const CompiledEffectContent compiled(std::vector<std::uint8_t>{1, 2, 3});
    std::string hex;
    static const char* digits = "0123456789ABCDEF";
    for (const std::uint8_t byte : compiled.GetEffectCode())
    {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    EXPECT_EQ("code=" + hex + " name=\"" + compiled.getNameProperty() + "\"", Expected("compiledeffect/roundtrip"));

    const CompiledEffectContent empty{std::vector<std::uint8_t>{}};
    EXPECT_EQ("code= length=" + std::to_string(empty.GetEffectCode().size()), Expected("compiledeffect/empty"));

    EXPECT_EQ(Serialize(std::make_shared<CompiledEffectContent>(std::vector<std::uint8_t>{1, 2, 3})),
              Expected("compiledeffect/serialize"));
}
