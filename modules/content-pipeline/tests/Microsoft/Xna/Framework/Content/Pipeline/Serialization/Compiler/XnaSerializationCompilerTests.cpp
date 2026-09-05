// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-060..063: the Serialization.Compiler façade --
// ContentWriter over the one XnbWriter, ContentTypeWriter<T> as a user's base class,
// ContentCompiler resolving writers by type -- proven by writing a user type and reading the
// bytes back through CNA's independent XNB reader.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnaPipelineBridge.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/PipelineException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentCompiler.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentTypeWriter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentTypeWriterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Compiler/ContentWriter.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

namespace
{
    namespace Xna = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Compiler = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Compiler;
    namespace Canon = CNA::Content::Pipeline;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;

    /// A user's intermediate/runtime type.
    class Waypoint : public Xna::ContentItem
    {
    public:
        static constexpr std::string_view XnaTypeName = "TestGame.Waypoint";
        Vector3 position;
        std::string label;
        const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    /// A derived type, to prove dispatch on the dynamic type.
    class NamedWaypoint final : public Waypoint
    {
    public:
        static constexpr std::string_view XnaTypeName = "TestGame.NamedWaypoint";
        std::int32_t priority = 0;
        const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    /// A route: a list of waypoints, a shared start waypoint referenced twice, a colour and an
    /// external reference.
    class Route final : public Xna::ContentItem
    {
    public:
        static constexpr std::string_view XnaTypeName = "TestGame.Route";
        std::vector<std::shared_ptr<Waypoint>> waypoints;
        std::shared_ptr<Waypoint> start;
        Color tint{1, 2, 3, 4};
        Xna::ExternalReference<Waypoint> map;
        const std::string& GetTypeName() const override
        {
            static const std::string name{XnaTypeName};
            return name;
        }
    };

    class WaypointWriter final : public Compiler::ContentTypeWriter<Waypoint>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TestGame.WaypointReader, TestGame"; }
        std::int32_t getTypeVersionProperty() const override { return 3; }

    protected:
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Waypoint>& value) override
        {
            output.Write(value->position);
            output.Write(value->label);
        }
    };

    class NamedWaypointWriter final : public Compiler::ContentTypeWriter<NamedWaypoint>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TestGame.NamedWaypointReader, TestGame"; }

    protected:
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<NamedWaypoint>& value) override
        {
            output.Write(value->position);
            output.Write(value->label);
            output.Write(value->priority);
        }
    };

    class RouteWriter final : public Compiler::ContentTypeWriter<Route>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform platform) const override
        {
            return platform == Xna::TargetPlatform::Xbox360 ? "TestGame.RouteReader, TestGame.Xbox" : "TestGame.RouteReader, TestGame";
        }

    protected:
        void Initialize(Compiler::ContentCompiler& compiler) override { initialized = &compiler; }
        bool ShouldCompressContent(Xna::TargetPlatform, const Xna::ContentObject&) const override { return false; }
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Route>& value) override
        {
            output.Write(static_cast<std::int32_t>(value->waypoints.size()));
            for (const auto& waypoint : value->waypoints) { output.WriteObject<Waypoint>(waypoint); }
            output.WriteSharedResource<Waypoint>(value->start);
            output.WriteSharedResource<Waypoint>(value->start);
            output.Write(value->tint);
            output.WriteExternalReference(value->map);
            output.WriteRawObject<std::string>(value->getNameProperty());
            output.WriteObject<std::int32_t>(7);
            output.Write7BitEncodedInt(300);
            output.Write(true);
            output.Write(u'Z');
        }

    public:
        Compiler::ContentCompiler* initialized = nullptr;
    };

    std::shared_ptr<Route> MakeRoute()
    {
        auto route = std::make_shared<Route>();
        route->setNameProperty("patrol");
        auto a = std::make_shared<Waypoint>();
        a->position = Vector3(1, 2, 3);
        a->label = "a";
        auto b = std::make_shared<NamedWaypoint>();
        b->position = Vector3(4, 5, 6);
        b->label = "b";
        b->priority = 9;
        route->waypoints = {a, b};
        route->start = a;
        route->map = Xna::ExternalReference<Waypoint>("/out/Maps/level1.xnb");
        return route;
    }

    Compiler::CompileOptions Options()
    {
        Compiler::CompileOptions options;
        options.assetName = "Routes/patrol";
        options.outputDirectory = "/out";
        return options;
    }

    // ---- reader side: CNA's independent XNB reader, with matching readers registered ------------

    class WaypointReaderT final : public Microsoft::Xna::Framework::Content::ContentTypeReader<std::shared_ptr<Waypoint>>
    {
    public:
        WaypointReaderT() : ContentTypeReader("TestGame.Waypoint") {}
        // The writer declares TypeVersion 3; the runtime reader must agree, exactly as XNA's does.
        int getTypeVersionProperty() const override { return 3; }

    protected:
        std::shared_ptr<Waypoint> Read(Microsoft::Xna::Framework::Content::ContentReader& input,
                                       std::optional<std::shared_ptr<Waypoint>>) override
        {
            auto value = std::make_shared<Waypoint>();
            value->position = input.ReadVector3();
            value->label = input.ReadString();
            return value;
        }
    };

    class NamedWaypointReaderT final : public Microsoft::Xna::Framework::Content::ContentTypeReader<std::shared_ptr<Waypoint>>
    {
    public:
        NamedWaypointReaderT() : ContentTypeReader("TestGame.NamedWaypoint") {}

    protected:
        std::shared_ptr<Waypoint> Read(Microsoft::Xna::Framework::Content::ContentReader& input,
                                       std::optional<std::shared_ptr<Waypoint>>) override
        {
            auto value = std::make_shared<NamedWaypoint>();
            value->position = input.ReadVector3();
            value->label = input.ReadString();
            value->priority = input.ReadInt32();
            return value;
        }
    };

    struct ReadRoute
    {
        std::vector<std::shared_ptr<Waypoint>> waypoints;
        std::shared_ptr<Waypoint> start1;
        std::shared_ptr<Waypoint> start2;
        Color tint;
        std::string map;
        std::string name;
        std::int32_t seven = 0;
        std::int32_t encoded = 0;
        bool flag = false;
        char16_t letter = 0;
    };

    class RouteReaderT final : public Microsoft::Xna::Framework::Content::ContentTypeReader<std::shared_ptr<ReadRoute>>
    {
    public:
        RouteReaderT() : ContentTypeReader("TestGame.Route") {}

    protected:
        std::shared_ptr<ReadRoute> Read(Microsoft::Xna::Framework::Content::ContentReader& input,
                                        std::optional<std::shared_ptr<ReadRoute>>) override
        {
            auto value = std::make_shared<ReadRoute>();
            const std::int32_t count = input.ReadInt32();
            for (std::int32_t i = 0; i < count; ++i) { value->waypoints.push_back(input.ReadObject<std::shared_ptr<Waypoint>>()); }
            input.ReadSharedResource<std::shared_ptr<Waypoint>>([value](const std::shared_ptr<Waypoint>& w) { value->start1 = w; });
            input.ReadSharedResource<std::shared_ptr<Waypoint>>([value](const std::shared_ptr<Waypoint>& w) { value->start2 = w; });
            value->tint = input.ReadColor();
            value->map = input.ReadString(); // an external reference is a relative-path string on the wire
            value->name = input.ReadString(); // a raw String object is its payload alone
            value->seven = input.ReadObject<std::int32_t>();
            value->encoded = input.Read7BitEncodedInt();
            value->flag = input.ReadBoolean();
            value->letter = input.ReadChar();
            return value;
        }
    };

    struct ReaderRegistration
    {
        ReaderRegistration()
        {
            using Manager = Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
            if (!Manager::IsRegistered("Microsoft.Xna.Framework.Content.StringReader"))
            {
                CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            }
            Manager::AddTypeCreator("TestGame.WaypointReader", [] { return std::make_unique<WaypointReaderT>(); });
            Manager::AddTypeCreator("TestGame.NamedWaypointReader", [] { return std::make_unique<NamedWaypointReaderT>(); });
            Manager::AddTypeCreator("TestGame.RouteReader", [] { return std::make_unique<RouteReaderT>(); });
        }
        ~ReaderRegistration()
        {
            using Manager = Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
            Manager::RemoveTypeCreatorEXT("TestGame.WaypointReader");
            Manager::RemoveTypeCreatorEXT("TestGame.NamedWaypointReader");
            Manager::RemoveTypeCreatorEXT("TestGame.RouteReader");
        }
    };

    CNA::Internal::Xnb::XnbHeader Header(const std::vector<std::uint8_t>& bytes)
    {
        System::IO::MemoryStream stream(bytes.data(), static_cast<SharpRuntime::intcs>(bytes.size()), false);
        System::IO::BinaryReader reader(&stream, true);
        return CNA::Internal::Xnb::ParseXnbHeader(reader, "Routes/patrol.xnb");
    }

    std::shared_ptr<ReadRoute> ReadBack(const std::vector<std::uint8_t>& bytes)
    {
        Microsoft::Xna::Framework::Content::ContentManager manager;
        System::IO::MemoryStream stream(bytes.data(), static_cast<SharpRuntime::intcs>(bytes.size()), false);
        System::IO::BinaryReader headerReader(&stream, true);
        const CNA::Internal::Xnb::XnbHeader header = CNA::Internal::Xnb::ParseXnbHeader(headerReader, "Routes/patrol.xnb");
        Microsoft::Xna::Framework::Content::ContentReader reader(&manager, &stream, "Routes/patrol", header.version,
                                                                  header.platform);
        return reader.ReadAsset<std::shared_ptr<ReadRoute>>();
    }

    TEST(XnaContentCompiler, ResolvesBuiltInAndUserWritersByType)
    {
        Compiler::ContentCompiler compiler;
        auto routeWriter = compiler.AddTypeWriter<RouteWriter>(Compiler::ContentTypeWriterAttribute{});
        EXPECT_EQ(routeWriter->initialized, &compiler);
        compiler.AddTypeWriter<WaypointWriter>();
        compiler.AddTypeWriter<NamedWaypointWriter>();
        EXPECT_THROW(compiler.AddTypeWriter<WaypointWriter>(), Xna::PipelineException);

        auto writer = compiler.GetTypeWriter(System::Type::From<Route>());
        EXPECT_EQ(writer->GetRuntimeReader(Xna::TargetPlatform::Windows), "TestGame.RouteReader, TestGame");
        EXPECT_EQ(writer->GetRuntimeReader(Xna::TargetPlatform::Xbox360), "TestGame.RouteReader, TestGame.Xbox");
        EXPECT_EQ(writer->GetRuntimeType(Xna::TargetPlatform::Windows), "TestGame.Route");
        EXPECT_EQ(writer->getTargetTypeProperty(), System::Type::From<Route>());
        EXPECT_EQ(writer->getTypeVersionProperty(), 0);
        EXPECT_FALSE(writer->getCanDeserializeIntoExistingObjectProperty());
        EXPECT_EQ(compiler.GetTypeWriter(System::Type::From<Waypoint>())->getTypeVersionProperty(), 3);

        auto builtIn = compiler.GetTypeWriter(System::Type::From<std::int32_t>());
        EXPECT_EQ(builtIn->GetRuntimeReader(Xna::TargetPlatform::Windows), "Microsoft.Xna.Framework.Content.Int32Reader");
        EXPECT_EQ(builtIn->GetRuntimeType(Xna::TargetPlatform::Windows), "System.Int32");
        auto vector3 = compiler.GetTypeWriter(System::Type::From<Vector3>());
        EXPECT_EQ(vector3->GetRuntimeReader(Xna::TargetPlatform::Windows), "Microsoft.Xna.Framework.Content.Vector3Reader");
        EXPECT_THROW((void)compiler.GetTypeWriter(System::Type::From<ReadRoute>()), Xna::PipelineException);

        const std::vector<std::string> names = compiler.KnownTypeNames();
        EXPECT_NE(std::find(names.begin(), names.end(), "System.Int32"), names.end());
        EXPECT_NE(std::find(names.begin(), names.end(), "Microsoft.Xna.Framework.Curve"), names.end());
        EXPECT_NE(std::find(names.begin(), names.end(), "TestGame.Route"), names.end());
        EXPECT_NE(std::find(names.begin(), names.end(), "System.Collections.Generic.List`1[[System.String]]"), names.end());

        // Once compiled, the registries are frozen.
        (void)compiler.Compile<std::int32_t>(5, Options());
        EXPECT_THROW(compiler.AddTypeWriter<RouteWriter>(), Xna::PipelineException);
    }

    TEST(XnaContentWriter, UserWritersProduceAnXnbCnaReadsBackWithEveryValue)
    {
        ReaderRegistration readers;
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<RouteWriter>();
        compiler.AddTypeWriter<WaypointWriter>();
        compiler.AddTypeWriter<NamedWaypointWriter>();

        const CNA::Internal::Xnb::XnbAssetWriteResult written = compiler.Compile<Route>(MakeRoute(), Options());
        EXPECT_EQ(written.rootReaderName, "TestGame.RouteReader, TestGame");
        const CNA::Internal::Xnb::XnbHeader header = Header(written.bytes);
        EXPECT_EQ(header.platform, 'w');
        EXPECT_EQ(header.version, 5);
        EXPECT_EQ(static_cast<std::size_t>(header.totalLength), written.bytes.size());
        EXPECT_EQ(header.compression, CNA::Internal::Xnb::XnbCompression::None);

        const std::shared_ptr<ReadRoute> route = ReadBack(written.bytes);
        ASSERT_EQ(route->waypoints.size(), 2u);
        EXPECT_EQ(route->waypoints[0]->label, "a");
        EXPECT_EQ(route->waypoints[0]->position, Vector3(1, 2, 3));
        // The second waypoint was declared as Waypoint but written by its dynamic type's writer.
        auto named = std::dynamic_pointer_cast<NamedWaypoint>(route->waypoints[1]);
        ASSERT_NE(named, nullptr);
        EXPECT_EQ(named->priority, 9);
        // Both shared-resource references resolve to one object.
        ASSERT_NE(route->start1, nullptr);
        EXPECT_EQ(route->start1, route->start2);
        EXPECT_EQ(route->start1->label, "a");
        EXPECT_EQ(route->tint, Color(1, 2, 3, 4));
        // Relative to Routes/, without the extension.
        EXPECT_EQ(route->map, "../Maps/level1");
        EXPECT_EQ(route->name, "patrol");
        EXPECT_EQ(route->seven, 7);
        EXPECT_EQ(route->encoded, 300);
        EXPECT_TRUE(route->flag);
        EXPECT_EQ(route->letter, u'Z');
    }

    TEST(XnaContentWriter, NullReferencesAndCompressionFollowXna)
    {
        ReaderRegistration readers;
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<RouteWriter>();
        compiler.AddTypeWriter<WaypointWriter>();
        compiler.AddTypeWriter<NamedWaypointWriter>();
        auto route = MakeRoute();
        route->waypoints = {nullptr};
        route->start = nullptr;
        route->map = Xna::ExternalReference<Waypoint>();
        Compiler::CompileOptions options = Options();
        options.compressContent = true; // RouteWriter declines, so the file stays uncompressed
        const CNA::Internal::Xnb::XnbAssetWriteResult written = compiler.Compile<Route>(route, options);
        EXPECT_EQ(Header(written.bytes).compression, CNA::Internal::Xnb::XnbCompression::None);
        const std::shared_ptr<ReadRoute> read = ReadBack(written.bytes);
        ASSERT_EQ(read->waypoints.size(), 1u);
        EXPECT_EQ(read->waypoints[0], nullptr);
        EXPECT_EQ(read->start1, nullptr);
        EXPECT_TRUE(read->map.empty());

        // A reference whose dynamic type has no writer is refused rather than written with the
        // base type's writer: XNA never falls back to a base class (a type without a writer gets
        // the reflective writer, which is XNAPP-070's).
        Compiler::ContentCompiler other;
        other.AddTypeWriter<RouteWriter>();
        other.AddTypeWriter<WaypointWriter>();
        Compiler::CompileOptions plain = Options();
        EXPECT_THROW((void)other.Compile<Route>(MakeRoute(), plain), Xna::PipelineException)
            << "a list containing a NamedWaypoint with no writer";
    }

    TEST(XnaContentWriter, XnbTargetPlatformAndProfileReachTheHeaderAndTheReaderName)
    {
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<RouteWriter>();
        compiler.AddTypeWriter<WaypointWriter>();
        compiler.AddTypeWriter<NamedWaypointWriter>();
        Compiler::CompileOptions options = Options();
        options.targetPlatform = Xna::TargetPlatform::WindowsPhone;
        options.targetProfile = Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef;
        auto route = MakeRoute();
        route->waypoints.clear();
        route->start = nullptr;
        const CNA::Internal::Xnb::XnbAssetWriteResult written = compiler.Compile<Route>(route, options);
        EXPECT_EQ(Header(written.bytes).platform, 'm');
        EXPECT_EQ(written.bytes[5] & 0x01, 0x01); // the HiDef profile bit
        options.targetPlatform = Xna::TargetPlatform::Xbox360;
        options.container.allowUnverifiedXboxPayloads = true;
        const CNA::Internal::Xnb::XnbAssetWriteResult xbox = compiler.Compile<Route>(route, options);
        EXPECT_EQ(xbox.rootReaderName, "TestGame.RouteReader, TestGame.Xbox");
    }

    // ---- through the canonical pipeline ---------------------------------------------------------

    class RouteImporter final : public Xna::ContentImporter<Route>
    {
    public:
        std::shared_ptr<Route> Import(const std::string& filename, Xna::ContentImporterContext&) override
        {
            auto route = MakeRoute();
            std::ifstream stream(filename);
            std::string line;
            std::getline(stream, line);
            route->setNameProperty(line);
            return route;
        }
    };

    TEST(XnaXnbOutput, AnXnaShapedRouteBuildsToXnbThroughTheCanonicalPipeline)
    {
        ReaderRegistration readers;
        const std::filesystem::path root = std::filesystem::temp_directory_path() / ("cna-xnapp-compiler-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        { std::ofstream(root / "patrol.route") << "patrol-route\n"; }

        auto compiler = std::make_shared<Compiler::ContentCompiler>();
        compiler->AddTypeWriter<RouteWriter>();
        compiler->AddTypeWriter<WaypointWriter>();
        compiler->AddTypeWriter<NamedWaypointWriter>();
        auto registry = std::make_shared<Canon::ContentPipelineRegistry>();
        Xna::ContentImporterAttribute attribute(".route");
        attribute.setDefaultProcessorProperty("PassThrough");
        Canon::RegisterXnaImporter<RouteImporter>(*registry, "RouteImporter", attribute);
        // A pass-through processor for Route, declared XNA-style.
        class PassThrough final : public Xna::ContentProcessor<Route, Route>
        {
        public:
            std::shared_ptr<Route> Process(const std::shared_ptr<Route>& input, Xna::ContentProcessorContext&) override { return input; }
        };
        Canon::RegisterXnaProcessor<PassThrough>(*registry, "PassThrough", Xna::ContentProcessorAttribute{});
        Canon::RegisterXnaXnbOutput(*registry, compiler, CNA::Internal::Xnb::XnbFileOptions{});
        Canon::ContentPipeline pipeline(registry);

        Canon::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "patrol.route";
        request.logicalName = "Routes/patrol";
        request.outputFormat = Canon::ContentOutputFormat::Xnb;
        request.environment.outputDirectory = "/out";
        const Canon::ContentBuildResult result = pipeline.Build(request);
        EXPECT_EQ(result.writer.name, "CNA.XnaObjectXnbWriter[TestGame.Route]");
        EXPECT_EQ(result.output.assetTypeName, "TestGame.Route");
        EXPECT_EQ(result.output.rootReaderName, "TestGame.RouteReader, TestGame");
        const std::shared_ptr<ReadRoute> route = ReadBack(result.output.bytes);
        EXPECT_EQ(route->name, "patrol-route");
        EXPECT_EQ(route->waypoints.size(), 2u);
        EXPECT_EQ(route->map, "../Maps/level1");
        std::filesystem::remove_all(root);
    }
}
