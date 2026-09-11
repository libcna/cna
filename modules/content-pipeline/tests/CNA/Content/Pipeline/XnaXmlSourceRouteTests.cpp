// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-260: the `.xml` source route, end to end.
//
// `.xml` is the one XNA source extension whose meaning is not fixed by the file: the document says
// what type it is, and the build has to resolve that name, carry the value through a processor and
// hand it to a writer that knows the type. These are the matrix legs for it -- the processor, the
// source-to-XNB build, the per-target build, the deliberate absence of a source-to-CNB one, and
// determinism -- measured against the files genuine XNA produced from the same documents.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace Canon = CNA::Content::Pipeline;

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

    /** @brief One committed document, the type it declares, and the genuine XNA file to match. */
    struct Document
    {
        /** @brief Fixture file name under `tests/assets/xna40/source`. */
        const char* source;

        /** @brief The .NET type the document's `Asset Type` resolves to. */
        const char* type;

        /** @brief The genuine XNA output under `tests/reference/xna40/differential`. */
        const char* genuine;
    };

    /** @brief Every committed `.xml` document, primitive, framework and generic. */
    std::vector<Document> Documents()
    {
        return {
            {"xml_int.xml", "System.Int32", "xml_primitive_int.xnb"},
            {"xml_vector3.xml", "Microsoft.Xna.Framework.Vector3", "xml_framework_vector3.xnb"},
            {"xml_curve.xml", "Microsoft.Xna.Framework.Curve", "xml_framework_curve.xnb"},
            {"xml_dictionary.xml",
             "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
             "xml_generic_dictionary.xnb"},
            // The same type, listed in an order that is not its keys' order. XNA writes a
            // dictionary in the order enumerating it gives, which for an add-only .NET
            // `Dictionary` is the order the document listed -- the genuine file here carries
            // `zulu, mike, alpha, tango` (plans/plan_xna_sample_xnb_sweep.md XNASWEEP-119).
            {"xml_dictionary_order.xml",
             "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
             "xml_generic_dictionary_order.xnb"},
            {"xml_rectangles.xml",
             "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Rectangle]]",
             "xml_generic_rectangle_list.xnb"},
            {"probe.xml", "System.Collections.Generic.List`1[[System.String]]",
             "xml_intermediate_passthrough.xnb"},
        };
    }

    /** @brief A source root holding one copied document, removed when the test ends. */
    class OneDocument
    {
    public:
        OneDocument(const std::string& label, const std::string& fileName)
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp260_xml_" + label))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            std::filesystem::copy_file(Locate("tests/assets/xna40/source") / fileName,
                                       Source() / fileName,
                                       std::filesystem::copy_options::overwrite_existing);
            name_ = std::filesystem::path(fileName).stem().string();
        }
        ~OneDocument()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        OneDocument(const OneDocument&) = delete;
        OneDocument& operator=(const OneDocument&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }
        [[nodiscard]] std::filesystem::path Built() const { return Output() / (name_ + ".xnb"); }

        /** @brief The build manifest's text, for the names the route actually ran under. */
        [[nodiscard]] std::string Manifest() const
        {
            std::ifstream stream(Output() / Canon::ContentBuildManifestFileName);
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

    private:
        std::filesystem::path root_;
        std::string name_;
    };

    /** @brief Builds the one document into @p format; answers the coordinator's status. */
    int Build(const OneDocument& document, const std::string& format,
              const std::vector<std::string>& extra = {})
    {
        std::vector<std::filesystem::path> arguments{
            "build", document.Source(), "-o", document.Output(), "--format", format, "--quiet"};
        for (const std::string& argument : extra) { arguments.emplace_back(argument); }
        return Canon::RunContentCompiler(arguments, [](const Canon::ContentCompilerOptions& options)
                                         {
                                             auto registry =
                                                 std::make_shared<Canon::ContentPipelineRegistry>();
                                             Canon::RegisterBuiltInContentPipeline(*registry, options);
                                             return registry;
                                         });
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
}

// The route: XNA's own importer, XNA's own pass-through for the type the document declares, and
// the writer the content compiler has for that type. The manifest is what the coordinator recorded
// rather than what the graph could have chosen, so it is the honest place to read the route from.
TEST(XnaXmlSourceRoute, EveryDocumentBuildsUnderTheImporterPassThroughAndWriterItDeclares)
{
    for (const Document& document : Documents())
    {
        OneDocument built("route_" + std::string(document.source), document.source);
        ASSERT_EQ(Build(built, "xnb"), 0) << document.source;
        ASSERT_TRUE(std::filesystem::is_regular_file(built.Built())) << document.source;

        const std::string manifest = built.Manifest();
        EXPECT_NE(manifest.find("\"name\":\"XmlImporter\""), std::string::npos) << document.source;
        EXPECT_NE(manifest.find(std::string("PassThroughProcessor(") + document.type + ")"),
                  std::string::npos)
            << document.source << "\n" << manifest;
        EXPECT_NE(manifest.find(std::string("CNA.XnaObjectXnbWriter[") + document.type + "]"),
                  std::string::npos)
            << document.source;
        // And the primary source is a recorded dependency, so editing the document rebuilds it.
        EXPECT_NE(manifest.find(document.source), std::string::npos) << document.source;
    }
}

// The bytes, against the file genuine XNA produced from the same document.
TEST(XnaXmlSourceRoute, EveryDocumentBuildsToTheBytesGenuineXnaWrote)
{
    const std::filesystem::path reference = Locate("tests/reference/xna40/differential");
    for (const Document& document : Documents())
    {
        const std::filesystem::path genuine = reference / document.genuine;
        ASSERT_TRUE(std::filesystem::is_regular_file(genuine)) << genuine.string();

        OneDocument built("bytes_" + std::string(document.source), document.source);
        ASSERT_EQ(Build(built, "xnb"), 0) << document.source;
        EXPECT_EQ(ReadBytes(built.Built()), ReadBytes(genuine))
            << document.source << " does not match " << document.genuine;
    }
}

// The three targets XNA had. The Xbox 360 build is compared byte for byte with the genuine one,
// which is what caught the Compact Framework's own `mscorlib` identity in the type-reader table.
TEST(XnaXmlSourceRoute, EveryTargetPlatformAnswersWhatXnaAnswers)
{
    {
        OneDocument windows("target_windows", "probe.xml");
        ASSERT_EQ(Build(windows, "xnb", {"--xnb-platform", "windows"}), 0);
        const std::vector<std::uint8_t> bytes = ReadBytes(windows.Built());
        ASSERT_GE(bytes.size(), 4u);
        EXPECT_EQ(bytes[3], static_cast<std::uint8_t>('w'));
        EXPECT_EQ(bytes, ReadBytes(Locate("tests/reference/xna40/differential") /
                                   "xml_intermediate_passthrough.xnb"));
    }
    {
        OneDocument phone("target_phone", "probe.xml");
        ASSERT_EQ(Build(phone, "xnb", {"--xnb-platform", "windowsphone"}), 0);
        const std::vector<std::uint8_t> bytes = ReadBytes(phone.Built());
        ASSERT_GE(bytes.size(), 4u);
        EXPECT_EQ(bytes[3], static_cast<std::uint8_t>('m'));
        // Windows Phone runs the Compact Framework too, under its own public key.
        const std::string text(bytes.begin(), bytes.end());
        EXPECT_NE(text.find("mscorlib, Version=3.7.0.0, Culture=neutral, "
                            "PublicKeyToken=969db8053d3322ac"),
                  std::string::npos);
    }
    {
        OneDocument xbox("target_xbox", "probe.xml");
        ASSERT_EQ(Build(xbox, "xnb",
                        {"--xnb-platform", "xbox360", "--xnb-profile", "hidef",
                         "--xnb-allow-unverified-xbox"}),
                  0);
        EXPECT_EQ(ReadBytes(xbox.Built()),
                  ReadBytes(Locate("tests/reference/xna40/differential") /
                            "xbox_xml_passthrough.xnb"));
    }
}

// The route is XNB-only, and the refusal says so rather than reading as an omission.
TEST(XnaXmlSourceRoute, TheCnbRefusalNamesTheTypeAndTheReason)
{
    OneDocument document("cnb", "xml_int.xml");
    EXPECT_NE(Build(document, "cnb"), 0);
    EXPECT_FALSE(std::filesystem::exists(document.Output() / "xml_int.cnb"));

    // The reason is enumerable rather than something a reader has to find in a plan.
    auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
    Canon::RegisterBuiltInContentPipeline(*registry, Canon::ContentCompilerOptions{});
    try
    {
        (void)registry->ResolveWriter("System.Int32", {}, Canon::ContentOutputFormat::Cnb);
        ADD_FAILURE() << "a `.cnb` writer for an XNA object graph must not exist";
    }
    catch (const std::logic_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("System.Int32"), std::string::npos) << message;
        EXPECT_NE(message.find("--format xnb"), std::string::npos) << message;
    }
    // And the XNB half of the same route does exist.
    EXPECT_NO_THROW((void)registry->ResolveWriter("System.Int32", {},
                                                  Canon::ContentOutputFormat::Xnb));
}

// The same document twice writes the same bytes.
TEST(XnaXmlSourceRoute, BuildingTheSameDocumentTwiceWritesTheSameBytes)
{
    for (const Document& document : Documents())
    {
        const auto bytesOf = [&document](const std::string& label)
        {
            OneDocument built(label + document.source, document.source);
            EXPECT_EQ(Build(built, "xnb"), 0) << document.source;
            return ReadBytes(built.Built());
        };
        EXPECT_EQ(bytesOf("det1_"), bytesOf("det2_")) << document.source;
    }
}
