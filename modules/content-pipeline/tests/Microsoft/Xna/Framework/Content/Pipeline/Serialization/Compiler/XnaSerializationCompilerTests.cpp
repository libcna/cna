// SPDX-License-Identifier: MS-PL
//
// XNAPP-060..063: the Serialization.Compiler façade --
// ContentWriter over the one XnbWriter, ContentTypeWriter<T> as a user's base class,
// ContentCompiler resolving writers by type -- proven by writing a user type and reading the
// bytes back through CNA's independent XNB reader.

#include "CNA/TestSupport/TestScratch.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include <algorithm>
#include <barrier>
#include <functional>
#include <stdexcept>
#include "System/ArgumentException.hpp"
#include "System/Collections/Generic/OrderedDictionary.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include "System/Environment.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include <optional>

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
#include "System/InvalidOperationException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"

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

    // A regression must fail within a deadline if Initialize holds the lookup mutex again.
    // The thread is joined on normal return and on exceptions; no blocked thread is detached.
    template<typename Action>
    void WithCompilerCallbackDeadline(Action action)
    {
        std::mutex mutex;
        std::condition_variable_any condition;
        std::jthread deadline([&](std::stop_token stop) {
            std::unique_lock lock(mutex);
            condition.wait_for(lock, stop, std::chrono::seconds(30), [] { return false; });
            if (!stop.stop_requested())
            {
                std::fputs("ContentCompiler Initialize deadlocked beyond the regression deadline\n", stderr);
                std::abort();
            }
        });
        action();
    }

    class InitializingWaypointWriter : public Compiler::ContentTypeWriter<Waypoint>
    {
    public:
        inline static int totalCalls = 0;
        int calls = 0;
        std::shared_ptr<Compiler::ContentTypeWriterBase> nested;
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TestGame.WaypointReader, TestGame"; }
    protected:
        void Initialize(Compiler::ContentCompiler& compiler) override
        {
            ++calls;
            ++totalCalls;
            nested = compiler.GetTypeWriter(System::Type::From<std::string>());
            // Both public read paths must be callable from user initialization.
            EXPECT_FALSE(compiler.KnownTypeNames().empty());
        }
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Waypoint>& value) override
        {
            output.Write(value->position);
            output.Write(value->label);
        }
    };

    class FailingWaypointInitializer final : public InitializingWaypointWriter
    {
        void Initialize(Compiler::ContentCompiler& compiler) override
        {
            InitializingWaypointWriter::Initialize(compiler);
            throw System::InvalidOperationException("initializer failure");
        }
    };

    class FreezingWaypointInitializer final : public InitializingWaypointWriter
    {
        void Initialize(Compiler::ContentCompiler& compiler) override
        {
            InitializingWaypointWriter::Initialize(compiler);
            (void)compiler.TypeWriterRegistry(Xna::TargetPlatform::Windows);
        }
    };

    class DuplicateWaypointInitializer final : public InitializingWaypointWriter
    {
        void Initialize(Compiler::ContentCompiler& compiler) override
        {
            InitializingWaypointWriter::Initialize(compiler);
            (void)compiler.AddTypeWriter<WaypointWriter>();
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



namespace
{
    // A wire-frame scanner deliberately independent of ContentReader and XnbHeader.
    // XNA Refresh WriteHeader IL_000c-0076: reader count, names/versions, shared count.
    struct Tq063Frame
    {
        std::vector<std::pair<std::string, std::int32_t>> readers;
        std::uint32_t shared = 0;
        std::vector<std::uint8_t> payload;
    };

    Tq063Frame Tq063Scan(const std::vector<std::uint8_t>& bytes)
    {
        std::size_t at = 10;
        const auto take = [&]() -> std::uint8_t { return bytes.at(at++); };
        const auto seven = [&]() {
            std::uint32_t value = 0;
            for (int shift = 0; shift < 35; shift += 7)
            {
                const auto b = take();
                value |= std::uint32_t(b & 127) << shift;
                if ((b & 128) == 0) { return value; }
            }
            throw std::runtime_error("invalid wire-frame varint");
        };
        Tq063Frame frame;
        const auto count = seven();
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto size = seven();
            std::string name;
            for (std::uint32_t n = 0; n < size; ++n) { name += static_cast<char>(take()); }
            std::uint32_t version = 0;
            for (int shift = 0; shift < 32; shift += 8) { version |= std::uint32_t(take()) << shift; }
            frame.readers.emplace_back(name, static_cast<std::int32_t>(version));
        }
        frame.shared = seven();
        frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
        return frame;
    }

    class OverloadWriter final : public Compiler::ContentTypeWriter<Route>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TQ.Overload"; }
        std::shared_ptr<Compiler::ContentTypeWriterBase> integer, text, waypoint;
    protected:
        void Initialize(Compiler::ContentCompiler& compiler) override
        {
            integer = compiler.GetTypeWriter(System::Type::From<std::int32_t>());
            text = compiler.GetTypeWriter(System::Type::From<std::string>());
            waypoint = compiler.GetTypeWriter(System::Type::From<Waypoint>());
        }
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Route>& route) override
        {
            output.WriteObject<std::int32_t>(0x12345678, *integer);
            output.WriteObject<std::string>("q", *text);
            output.WriteObject<Waypoint>(route->start, *waypoint);
            output.WriteRawObject<Waypoint>(route->start);
            output.WriteRawObject<Waypoint>(route->start, *waypoint);
            output.Write(SharpRuntime::bytecs{0xee});
        }
    };

    TEST(XnaContentWriter, Tq063HintedObjectsAndBothRawOverloadsFollowRefreshDispatch)
    {
        // Refresh WriteObject(T,writer) IL_000e-0026; WriteRawObject(T) IL_0013-0029.
        // Root=1; initialization dependencies Int32=2, String=3, Waypoint=4;
        // dynamic NamedWaypoint first appears at 5. Value hint has no index, String does.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<WaypointWriter>();
        compiler.AddTypeWriter<NamedWaypointWriter>();
        compiler.AddTypeWriter<OverloadWriter>();
        auto route = std::make_shared<Route>();
        auto named = std::make_shared<NamedWaypoint>();
        named->position = Vector3{1, 2, 3}; named->label = "z"; named->priority = 9;
        route->start = named;
        auto options = Options(); options.compressContent = false;
        const auto frame = Tq063Scan(compiler.Compile<Route>(route, options).bytes);
        const std::vector<std::pair<std::string, std::int32_t>> manifest{
            {"TQ.Overload",0}, {"Microsoft.Xna.Framework.Content.Int32Reader",0},
            {"Microsoft.Xna.Framework.Content.StringReader",0},
            {"TestGame.WaypointReader, TestGame",3},
            {"TestGame.NamedWaypointReader, TestGame",0}};
        EXPECT_EQ(frame.readers, manifest);
        EXPECT_EQ(frame.shared, 0u);
        const std::vector<std::uint8_t> position{0,0,128,63, 0,0,0,64, 0,0,64,64, 1,'z'};
        std::vector<std::uint8_t> expected{1, 0x78,0x56,0x34,0x12, 3,1,'q', 5};
        expected.insert(expected.end(), position.begin(), position.end());
        expected.insert(expected.end(), {9,0,0,0});
        expected.insert(expected.end(), position.begin(), position.end());
        expected.insert(expected.end(), position.begin(), position.end());
        expected.push_back(0xee);
        EXPECT_EQ(frame.payload, expected);
    }

    TEST(XnaContentWriter, Tq063NullAndWrongWriterRefusalsDoNotEmitPayload)
    {
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<WaypointWriter>();
        auto options = Options(); options.compressContent = false;
        CNA::Internal::Xnb::XnbWriter bytes(compiler.TypeWriterRegistry(Xna::TargetPlatform::Windows), {}, "null");
        Compiler::ContentWriter output(compiler, bytes, Xna::TargetPlatform::Windows,
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach, {}, "null");
        auto hint = compiler.GetTypeWriter(System::Type::From<Waypoint>());
        // Refresh WriteRawObject(T[,writer]) IL_0000-0012 rejects null before invoking a writer.
        EXPECT_THROW(output.WriteRawObject<Waypoint>(nullptr), System::ArgumentNullException);
        EXPECT_THROW(output.WriteRawObject<Waypoint>(nullptr, *hint), System::ArgumentNullException);
        // Refresh ContentTypeWriter<T>.CastType IL_000e-0048 refuses an incompatible
        // value-type worker. A reference-type hint would instead be ignored.
        auto integer = compiler.GetTypeWriter(System::Type::From<std::int32_t>());
        EXPECT_THROW(output.WriteObject<float>(1.0f, *integer), System::ArgumentException);
        EXPECT_THROW(output.WriteObject<Waypoint>(nullptr, *integer), System::ArgumentNullException);
        EXPECT_THROW(output.WriteRawObject<Waypoint>(std::make_shared<Waypoint>(), *integer), System::ArgumentException);
        EXPECT_THROW(output.WriteRawObject<Route>(std::make_shared<Route>(), *hint), System::ArgumentException);
        output.WriteObject<Waypoint>(nullptr);
        output.WriteObject<Waypoint>(nullptr, *hint);
        output.WriteSharedResource<Waypoint>(nullptr);
        const auto frame = Tq063Scan(bytes.Finish());
        EXPECT_TRUE(frame.readers.empty());
        EXPECT_EQ(frame.shared, 0u);
        EXPECT_EQ(frame.payload, (std::vector<std::uint8_t>{0,0,0}));
    }
}

namespace
{
    template<typename T>
    class Tq063ActionWriterFor final : public Compiler::ContentTypeWriter<T>
    {
    public:
        std::function<void(Compiler::ContentWriter&)> action;
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TQ.Action"; }
    protected:
        void Write(Compiler::ContentWriter& output,const std::shared_ptr<T>&) override { action(output); }
    };
    using Tq063ActionWriter = Tq063ActionWriterFor<Waypoint>;
    TEST(XnaContentWriter, Tq063ExplicitRawAndValueHintsDoNotInternUnusedReaders)
    {
        // Refresh WriteRawObject(T,writer) IL_0021-0029 and WriteObject(T,valueWriter)
        // IL_0016-001e invoke directly. A lookup during Write is not an Initialize dependency.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<WaypointWriter>();
        auto writer=compiler.AddTypeWriter<Tq063ActionWriterFor<Route>>();
        writer->action=[&](Compiler::ContentWriter& output) {
            auto point=std::make_shared<Waypoint>(); point->label="q";
            auto raw=compiler.GetTypeWriter(System::Type::From<Waypoint>());
            auto integer=compiler.GetTypeWriter(System::Type::From<std::int32_t>());
            output.WriteRawObject<Waypoint>(point,*raw);
            output.WriteObject<std::int32_t>(7,*integer);
        };
        auto options=Options(); options.compressContent=false;
        const auto frame=Tq063Scan(compiler.Compile<Route>(std::make_shared<Route>(),options).bytes);
        EXPECT_EQ(frame.readers,(std::vector<std::pair<std::string,std::int32_t>>{{"TQ.Action",0}}));
        EXPECT_EQ(frame.payload,(std::vector<std::uint8_t>{1,0,0,0,0,0,0,0,0,0,0,0,0,1,'q',7,0,0,0}));
    }

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



namespace
{
    TEST(XnaContentWriter, Tq063ExternalReferencesRequireXnbAndStripEverySuffixCase)
    {
        // Refresh WriteExternalReference<T> IL_0000-0029 (empty), IL_002a-0079
        // (.xnb, OrdinalIgnoreCase=5). Same directory avoids host path spelling differences.
        for (const std::string extension : {".xnb", ".XNB", ".XnB"})
        {
            Compiler::ContentCompiler compiler;
            CNA::Internal::Xnb::XnbWriter bytes(compiler.TypeWriterRegistry(Xna::TargetPlatform::Windows), {}, "ref");
            Compiler::ContentWriter output(compiler, bytes, Xna::TargetPlatform::Windows,
                Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach, "/out", "ref");
            output.WriteExternalReference(Xna::ExternalReference<Waypoint>{});
            output.WriteExternalReference(Xna::ExternalReference<Waypoint>("/out/map" + extension));
            // No reader manifest entries; shared count zero; empty string then UTF-8 "map".
            const auto file = bytes.Finish();
            const std::vector<std::uint8_t> expected{
                'X','N','B','w',5,0, 17,0,0,0, 0,0, 0,3,'m','a','p'};
            EXPECT_EQ(file, expected) << extension;
        }
        for (const std::string name : {"/out/map", "/out/map.png", "/out/map.xnb.bak"})
        {
            Compiler::ContentCompiler compiler;
            CNA::Internal::Xnb::XnbWriter bytes(compiler.TypeWriterRegistry(Xna::TargetPlatform::Windows), {}, "ref");
            Compiler::ContentWriter output(compiler, bytes, Xna::TargetPlatform::Windows,
                Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach, "/out", "ref");
            EXPECT_THROW(output.WriteExternalReference(Xna::ExternalReference<Waypoint>(name)),
                         System::ArgumentException) << name;
            EXPECT_EQ(bytes.Finish(), (std::vector<std::uint8_t>{'X','N','B','w',5,0,12,0,0,0,0,0}));
        }
    }
}
    TEST(XnaContentCompiler, InitializeCanResolveNestedWritersExactlyOnce)
    {
        WithCompilerCallbackDeadline([] {
            InitializingWaypointWriter::totalCalls = 0;
            Compiler::ContentCompiler compiler;
            const auto nested = compiler.GetTypeWriter(System::Type::From<std::string>());
            const auto before = compiler.KnownTypeNames().size();
            const auto writer = compiler.AddTypeWriter<InitializingWaypointWriter>();
            EXPECT_EQ(writer->nested, nested);
            EXPECT_EQ(writer->calls, 1);
            EXPECT_EQ(compiler.GetTypeWriter(System::Type::From<Waypoint>()), writer);
            EXPECT_EQ(compiler.KnownTypeNames().size(), before + 1);
            EXPECT_THROW(compiler.AddTypeWriter<InitializingWaypointWriter>(), Xna::PipelineException);
            EXPECT_EQ(InitializingWaypointWriter::totalCalls, 1);

            Compiler::ContentCompiler second;
            const auto other = second.AddTypeWriter<InitializingWaypointWriter>();
            EXPECT_NE(other, writer);
            EXPECT_EQ(other->calls, 1);
            EXPECT_EQ(InitializingWaypointWriter::totalCalls, 2);

            Compiler::ContentCompiler frozen;
            (void)frozen.TypeWriterRegistry(Xna::TargetPlatform::Windows);
            EXPECT_THROW(frozen.AddTypeWriter<InitializingWaypointWriter>(), Xna::PipelineException);
            EXPECT_EQ(InitializingWaypointWriter::totalCalls, 2);
        });
    }



namespace
{
    TEST(XnaContentCompiler, Tq063BuiltInWriterExistingObjectMetadataMatchesRefresh)
    {
        // Refresh ListWriter<T>, DictionaryWriter<K,V>, CurveWriter property IL_0000-0001.
        Compiler::ContentCompiler compiler;
        EXPECT_TRUE(compiler.GetTypeWriter(System::Type::From<std::vector<std::string>>())
            ->getCanDeserializeIntoExistingObjectProperty());
        using Map = System::Collections::Generic::OrderedDictionary<std::string, std::int32_t>;
        EXPECT_TRUE(compiler.GetTypeWriter(System::Type::From<Map>())
            ->getCanDeserializeIntoExistingObjectProperty());
        EXPECT_TRUE(compiler.GetTypeWriter(System::Type::From<Microsoft::Xna::Framework::Curve>())
            ->getCanDeserializeIntoExistingObjectProperty());
        EXPECT_FALSE(compiler.GetTypeWriter(System::Type::From<std::int32_t>())
            ->getCanDeserializeIntoExistingObjectProperty());
        EXPECT_FALSE(compiler.GetTypeWriter(System::Type::From<std::string>())
            ->getCanDeserializeIntoExistingObjectProperty());
    }
}
    TEST(XnaContentCompiler, FailedInitializeDoesNotPublishAWriter)
    {
        WithCompilerCallbackDeadline([] {
            Compiler::ContentCompiler compiler;
            const auto before = compiler.KnownTypeNames();
            EXPECT_THROW(compiler.AddTypeWriter<FailingWaypointInitializer>(), System::InvalidOperationException);
            EXPECT_EQ(compiler.KnownTypeNames(), before);
            EXPECT_THROW((void)compiler.GetTypeWriter(System::Type::From<Waypoint>()), Xna::PipelineException);
            const auto writer = compiler.AddTypeWriter<InitializingWaypointWriter>();
            EXPECT_EQ(compiler.GetTypeWriter(System::Type::From<Waypoint>()), writer);
        });
    }



namespace
{
    TEST(XnaContentCompiler, Tq063UnusedInitializeLookupRetainsItsReaderAndVersion)
    {
        // Refresh ContentWriter.GetTypeWriter IL_002c-0085: root, then Initialize dependencies.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<InitializingWaypointWriter>();
        auto value = std::make_shared<Waypoint>(); value->label = "q";
        auto options = Options(); options.compressContent = false;
        const auto frame = Tq063Scan(compiler.Compile<Waypoint>(value, options).bytes);
        const std::vector<std::pair<std::string,std::int32_t>> expected{
            {"TestGame.WaypointReader, TestGame",0},
            {"Microsoft.Xna.Framework.Content.StringReader",0}};
        EXPECT_EQ(frame.readers, expected);
        EXPECT_EQ(frame.payload, (std::vector<std::uint8_t>{1, 0,0,0,0, 0,0,0,0, 0,0,0,0, 1,'q'}));
    }
}
    TEST(XnaContentCompiler, RegistryFrozenDuringInitializeRejectsPublication)
    {
        WithCompilerCallbackDeadline([] {
            Compiler::ContentCompiler compiler;
            const auto before = compiler.KnownTypeNames();
            EXPECT_THROW(compiler.AddTypeWriter<FreezingWaypointInitializer>(), Xna::PipelineException);
            EXPECT_EQ(compiler.KnownTypeNames(), before);
            EXPECT_THROW((void)compiler.GetTypeWriter(System::Type::From<Waypoint>()), Xna::PipelineException);
        });
    }


namespace
{
    TEST(XnaContentWriter, Tq063RecursiveObjectGraphIsRejectedBeforeInvokingWriterTwice)
    {
        // Refresh WriteObject IL_002f-0094 rejects a cyclic dispatched object with
        // InvalidOperationException before invoking its type writer a second time.
        Compiler::ContentCompiler compiler;
        auto writer=compiler.AddTypeWriter<Tq063ActionWriter>();
        auto value=std::make_shared<Waypoint>();
        int calls=0;
        writer->action=[&](Compiler::ContentWriter& output) { ++calls; output.WriteObject<Waypoint>(value); };
        auto options=Options(); options.compressContent=false;
        options.container.limits.maxObjectNestingDepth=8;
        EXPECT_THROW((void)compiler.Compile<Waypoint>(value,options),System::InvalidOperationException);
        EXPECT_EQ(calls,1);
        writer->action=[](Compiler::ContentWriter& output) { output.Write(SharpRuntime::intcs{7}); };
        EXPECT_EQ(Tq063Scan(compiler.Compile<Waypoint>(value,options).bytes).payload,
                  (std::vector<std::uint8_t>{1,7,0,0,0}));
    }
}

    TEST(XnaContentCompiler, DuplicateRegisteredDuringInitializeIsNotOverwritten)
    {
        WithCompilerCallbackDeadline([] {
            Compiler::ContentCompiler compiler;
            const auto before = compiler.KnownTypeNames().size();
            EXPECT_THROW(compiler.AddTypeWriter<DuplicateWaypointInitializer>(), Xna::PipelineException);
            EXPECT_EQ(compiler.KnownTypeNames().size(), before + 1);
            EXPECT_NE(std::dynamic_pointer_cast<WaypointWriter>(
                compiler.GetTypeWriter(System::Type::From<Waypoint>())), nullptr);
        });
    }


namespace
{
    TEST(XnaContentWriter, Tq063MathOverloadsPreserveAllComponentsAndDoNotNormalize)
    {
        using namespace Microsoft::Xna::Framework;
        // Refresh ContentWriter.Write overloads: literal IEEE754 words for 1,2,3,4.
        Compiler::ContentCompiler compiler;
        auto writer=compiler.AddTypeWriter<Tq063ActionWriter>();
        writer->action=[](Compiler::ContentWriter& output) {
            output.Write(Vector2{1,2}); output.Write(Vector3{1,2,3});
            output.Write(Vector4{1,2,3,4}); output.Write(Quaternion{1,2,3,4});
            output.Write(Matrix{1,2,3,4,1,2,3,4,1,2,3,4,1,2,3,4});
        };
        auto options=Options(); options.compressContent=false;
        const auto frame=Tq063Scan(compiler.Compile<Waypoint>(std::make_shared<Waypoint>(),options).bytes);
        std::vector<std::uint8_t> expected{1};
        const std::vector<std::uint8_t> xy{0,0,128,63,0,0,0,64};
        const std::vector<std::uint8_t> xyz{0,0,128,63,0,0,0,64,0,0,64,64};
        const std::vector<std::uint8_t> xyzw{0,0,128,63,0,0,0,64,0,0,64,64,0,0,128,64};
        expected.insert(expected.end(),xy.begin(),xy.end());
        expected.insert(expected.end(),xyz.begin(),xyz.end());
        for (int copy=0;copy<6;++copy) { expected.insert(expected.end(),xyzw.begin(),xyzw.end()); }
        EXPECT_EQ(frame.payload,expected);
        EXPECT_EQ(frame.readers,(std::vector<std::pair<std::string,std::int32_t>>{{"TQ.Action",0}}));
    }

    TEST(XnaContentWriter, Tq063InvalidByteRangesRefuseBeforeWritingAnyBytes)
    {
        // CNA pointer/count contract: null pointer or negative offset/count refuses.
        Compiler::ContentCompiler compiler;
        auto writer=compiler.AddTypeWriter<Tq063ActionWriter>();
        writer->action=[](Compiler::ContentWriter& output) {
            const SharpRuntime::bytecs input[]{0xaa,0xbb,0xcc};
            EXPECT_THROW(output.Write(nullptr,0,1),Xna::PipelineException);
            EXPECT_THROW(output.Write(input,-1,1),Xna::PipelineException);
            EXPECT_THROW(output.Write(input,0,-1),Xna::PipelineException);
            output.Write(input,1,0); output.Write(input,1,1);
        };
        auto options=Options(); options.compressContent=false;
        const auto frame=Tq063Scan(compiler.Compile<Waypoint>(std::make_shared<Waypoint>(),options).bytes);
        EXPECT_EQ(frame.payload,(std::vector<std::uint8_t>{1,0xbb}));
    }

}

    TEST(XnaContentWriter, EveryBuiltInTypeTheCompilerIntendsToKnowIsActuallyKnown)
    {
        // RegisterBuiltIns() adds each built-in by looking its canonical `.xnb` writer up by C++
        // type and returning silently when nothing answers, so a built-in can stop being
        // registered without any line of code being removed. That is exactly what happened to
        // `Dictionary<string,int>`, whose canonical writer is over `std::unordered_map` while the
        // pipeline holds the type as `std::map`: the line was there, the type was not
        // (XNAPP-260). Spelling the whole set out is what keeps a
        // silent drop from being silent.
        const Compiler::ContentCompiler compiler;
        std::vector<std::string> names = compiler.KnownTypeNames();

        std::vector<std::string> expected = {
            "System.Boolean", "System.SByte", "System.Byte", "System.Int16", "System.UInt16",
            "System.Int32", "System.UInt32", "System.Int64", "System.UInt64", "System.Single",
            "System.Double", "System.Char", "System.String", "System.TimeSpan", "System.DateTime",
#if SHARP_RUNTIME_HAS_NATIVE_INT128
            "System.Decimal",
#endif
            "Microsoft.Xna.Framework.Vector2", "Microsoft.Xna.Framework.Vector3",
            "Microsoft.Xna.Framework.Vector4", "Microsoft.Xna.Framework.Matrix",
            "Microsoft.Xna.Framework.Quaternion", "Microsoft.Xna.Framework.Color",
            "Microsoft.Xna.Framework.Point", "Microsoft.Xna.Framework.Rectangle",
            "Microsoft.Xna.Framework.Plane", "Microsoft.Xna.Framework.BoundingBox",
            "Microsoft.Xna.Framework.BoundingSphere", "Microsoft.Xna.Framework.BoundingFrustum",
            "Microsoft.Xna.Framework.Ray", "Microsoft.Xna.Framework.Curve",
            "System.Collections.Generic.List`1[[System.String]]",
            "System.Collections.Generic.List`1[[System.Int32]]",
            "System.Collections.Generic.List`1[[System.Char]]",
            "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Rectangle]]",
            "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]",
            "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Matrix]]",
            "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
            "System.Collections.Generic.Dictionary`2[[System.String],[System.String]]",
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture3DContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureCubeContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SoundEffectContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SongContent",
            "Microsoft.Xna.Framework.Content.Pipeline.VideoContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SpriteFontContent",
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.CompiledEffectContent",
        };
        std::sort(names.begin(), names.end());
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(names, expected);
    }


namespace
{
    struct Tq063Unregistered
    {
        static constexpr std::string_view XnaTypeName="TQ.Unregistered";
    };
    TEST(XnaContentCompiler, Tq063UnsupportedAndEmptyRootsRefuseAndPermitValidRecovery)
    {
        // CNA CompileObject's documented missing-registration/empty-box contract.
        Compiler::ContentCompiler compiler;
        EXPECT_THROW((void)compiler.CompileObject(Xna::ContentObject{},Options()),Xna::PipelineException);
        EXPECT_THROW((void)compiler.GetTypeWriter(System::Type::From<Tq063Unregistered>()),Xna::PipelineException);
        EXPECT_THROW((void)compiler.Compile<Tq063Unregistered>(Tq063Unregistered{},Options()),Xna::PipelineException);
        auto options=Options(); options.compressContent=false;
        const auto frame=Tq063Scan(compiler.Compile<std::int32_t>(7,options).bytes);
        EXPECT_EQ(frame.payload,(std::vector<std::uint8_t>{1,7,0,0,0}));
    }

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


namespace
{
    class Tq063QueueNode final : public Xna::ContentItem
    {
    public:
        static constexpr std::string_view XnaTypeName="TQ.QueueNode";
        std::int32_t value=0;
        std::shared_ptr<Tq063QueueNode> next;
        const std::string& GetTypeName() const override
        { static const std::string name{XnaTypeName}; return name; }
    };
    class Tq063QueueWriter final : public Compiler::ContentTypeWriter<Tq063QueueNode>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TQ.Queue"; }
        std::int32_t getTypeVersionProperty() const override { return 7; }
    protected:
        void Write(Compiler::ContentWriter& output,const std::shared_ptr<Tq063QueueNode>& value) override
        {
            output.Write(value->value);
            output.WriteSharedResource<Tq063QueueNode>(value->next);
            output.WriteSharedResource<Tq063QueueNode>(value->next);
            output.WriteSharedResource<Tq063QueueNode>(nullptr);
        }
    };
    TEST(XnaContentWriter, Tq063SharedQueueDiscoversResourcesDuringFinishInFifoOrder)
    {
        // Refresh WriteSharedResource IL_0025-0058 assigns one-based ids once;
        // WriteSharedResources IL_0000-0023 drains resources discovered during the drain.
        Compiler::ContentCompiler compiler; compiler.AddTypeWriter<Tq063QueueWriter>();
        auto root=std::make_shared<Tq063QueueNode>();
        auto first=std::make_shared<Tq063QueueNode>(); first->value=1;
        auto second=std::make_shared<Tq063QueueNode>(); second->value=2;
        root->next=first; first->next=second; second->next=first;
        auto options=Options(); options.compressContent=false;
        const auto bytes=compiler.Compile<Tq063QueueNode>(root,options).bytes;
        first->next.reset(); second->next.reset(); // release the authored shared cycle
        const auto frame=Tq063Scan(bytes);
        EXPECT_EQ(frame.shared,2u);
        EXPECT_EQ(frame.readers,(std::vector<std::pair<std::string,std::int32_t>>{{"TQ.Queue",7}}));
        EXPECT_EQ(frame.payload,(std::vector<std::uint8_t>{
            1,0,0,0,0,1,1,0, 1,1,0,0,0,2,2,0, 1,2,0,0,0,1,1,0}));
    }

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



namespace
{
    TEST(XnaContentCompiler, Tq063CompiledEffectRefusesPhoneButWindowsWritesAnEmptyArray)
    {
        // Refresh EffectWriter.Write IL_0000-0013 refuses Phone before inspecting bytes;
        // IL_0014-002b writes a zero byte count for an empty array on Windows.
        using Effect=Microsoft::Xna::Framework::Content::Pipeline::Processors::CompiledEffectContent;
        Compiler::ContentCompiler compiler;
        auto options=Options(); options.compressContent=false;
        options.targetPlatform=Xna::TargetPlatform::WindowsPhone;
        for (const auto& code : {std::vector<SharpRuntime::bytecs>{},std::vector<SharpRuntime::bytecs>{1,2,3}})
        {
            EXPECT_THROW((void)compiler.Compile<Effect>(std::make_shared<Effect>(code),options),Xna::InvalidContentException);
        }
        options.targetPlatform=Xna::TargetPlatform::Windows;
        const auto frame=Tq063Scan(compiler.Compile<Effect>(std::make_shared<Effect>(),options).bytes);
        EXPECT_EQ(frame.payload,(std::vector<std::uint8_t>{1,0,0,0,0}));
        EXPECT_EQ(frame.readers,(std::vector<std::pair<std::string,std::int32_t>>{{
            "Microsoft.Xna.Framework.Content.EffectReader, Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553",0}}));
        // The canonical API retains its stronger empty-payload safety policy.
        CNA::Internal::Xnb::XnbFileOptions canonical;
        canonical.platform=CNA::Internal::Xnb::XnbTargetPlatform::WindowsPhone;
        EXPECT_THROW((void)CNA::Internal::Xnb::WriteXnbAsset(
            CNA::Internal::Xnb::XnbCompiledEffectContent{{1,2,3}},canonical),CNA::Internal::Xnb::XnbWriteException);
    }
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
            std::ifstream stream(CNA::Internal::PathFromUtf8(filename));
            std::string line;
            std::getline(stream, line);
            route->setNameProperty(line);
            return route;
        }
    };



namespace
{
    TEST(XnaContentCompiler, Tq063NullRootsAndInvalidTargetsRefuseBeforeCallbacks)
    {
        // Refresh ContentCompiler.Compile IL_000e-0060: null root, invalid platform/profile.
        Compiler::ContentCompiler compiler;
        auto writer=compiler.AddTypeWriter<Tq063ActionWriter>();
        int calls=0;
        writer->action=[&](Compiler::ContentWriter& output) { ++calls; output.Write(SharpRuntime::intcs{7}); };
        auto options=Options(); options.compressContent=false;
        EXPECT_THROW((void)compiler.Compile<Waypoint>(nullptr,options),System::ArgumentNullException);
        EXPECT_THROW((void)compiler.CompileObject(Xna::Box<Waypoint>(nullptr),options),System::ArgumentNullException);
        auto root=std::make_shared<Waypoint>();
        options.targetPlatform=static_cast<Xna::TargetPlatform>(-1);
        EXPECT_THROW((void)compiler.Compile<Waypoint>(root,options),System::ArgumentOutOfRangeException);
        options.targetPlatform=Xna::TargetPlatform::Windows;
        options.targetProfile=static_cast<Microsoft::Xna::Framework::Graphics::GraphicsProfile>(99);
        EXPECT_THROW((void)compiler.Compile<Waypoint>(root,options),System::ArgumentOutOfRangeException);
        EXPECT_EQ(calls,0);
        options.targetProfile=Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach;
        EXPECT_EQ(Tq063Scan(compiler.Compile<Waypoint>(root,options).bytes).payload,
                  (std::vector<std::uint8_t>{1,7,0,0,0}));
        EXPECT_EQ(calls,1);
    }
}
    TEST(XnaXnbOutput, AnXnaShapedRouteBuildsToXnbThroughTheCanonicalPipeline)
    {
        ReaderRegistration readers;
        const std::filesystem::path root = CNA::TestSupport::FixtureRoot() / ("cna-xnapp-compiler-" + std::to_string(System::Environment::getProcessIdProperty()));
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

namespace
{
    class LiteralLayoutWriter final : public Compiler::ContentTypeWriter<Waypoint>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TQ.Reader"; }
        std::int32_t getTypeVersionProperty() const override { return 3; }
    protected:
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Waypoint>&) override
        {
            output.Write(SharpRuntime::bytecs{0xab});
            output.Write(std::int8_t{-2});
            output.Write(true);
            output.Write(SharpRuntime::shortcs{-0x1234});
            output.Write(SharpRuntime::ushortcs{0xfedc});
            output.Write(SharpRuntime::intcs{-2});
            output.Write(std::uint32_t{0x89abcdef});
            output.Write(SharpRuntime::longcs{-2});
            output.Write(std::uint64_t{0x0123456789abcdef});
            output.Write(1.0f);
            output.Write(-2.0);
            output.Write(SharpRuntime::charcs{u'Ω'});
            output.Write(std::string(128, 'x'));
            for (SharpRuntime::intcs n : {0, 127, 128, 16383, 16384, -1})
                output.Write7BitEncodedInt(n);
            output.Write(Color(0x11, 0x22, 0x33, 0x44));
            const SharpRuntime::bytecs slice[]{0xaa,0xbb,0xcc,0xdd,0xee};
            output.Write(slice, 1, 3);
        }
    };

    TEST(XnaContentWriter, PrimitiveWritesMatchAHandComputedXnbByteLayout)
    {
        // XNB v5: header, 7-bit reader count, length-prefixed reader name, LE version,
        // 7-bit shared count, root reader index, then BinaryWriter primitive bytes.
        // This expectation never uses ContentReader/XnbHeader to decode writer output.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<LiteralLayoutWriter>();
        Compiler::CompileOptions options;
        options.compressContent = false;
        const auto written = compiler.Compile<Waypoint>(std::make_shared<Waypoint>(), options);
        std::vector<std::uint8_t> body{
            0x01, 0x09, 'T','Q','.','R','e','a','d','e','r', 0x03,0x00,0x00,0x00,
            0x00, // no shared resources
            0x01, // root reader's one-based index
            0xab,0xfe,0x01, 0xcc,0xed, 0xdc,0xfe,
            0xfe,0xff,0xff,0xff, 0xef,0xcd,0xab,0x89,
            0xfe,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
            0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01,
            0x00,0x00,0x80,0x3f, // IEEE754 Single 1
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0, // Double -2
            0xce,0xa9, // UTF-8 U+03A9 (no length for a char)
            0x80,0x01 // 128-byte string length, 7 bits at a time
        };
        body.insert(body.end(), 128, 'x');
        const std::vector<std::uint8_t> tail{
            0x00,0x7f,0x80,0x01,0xff,0x7f,0x80,0x80,0x01,0xff,0xff,0xff,0xff,0x0f,
            0x11,0x22,0x33,0x44, 0xbb,0xcc,0xdd};
        body.insert(body.end(), tail.begin(), tail.end());
        // Header length = ten header bytes + the independently constructed body.
        const std::uint32_t size = static_cast<std::uint32_t>(10 + body.size());
        std::vector<std::uint8_t> expected{'X','N','B','w',0x05,0x00,
            static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8),
            static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 24)};
        expected.insert(expected.end(), body.begin(), body.end());
        EXPECT_EQ(written.bytes, expected);
        EXPECT_EQ(written.rootReaderName, "TQ.Reader");
    }
}

namespace
{
    class SharedLayoutWriter final : public Compiler::ContentTypeWriter<Waypoint>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override { return "TQ.Shared"; }
    protected:
        void Write(Compiler::ContentWriter& output, const std::shared_ptr<Waypoint>& value) override
        {
            output.Write(SharpRuntime::intcs{42});
            output.WriteSharedResource<Waypoint>(value);
            output.WriteSharedResource<Waypoint>(value);
        }
    };

    TEST(XnaContentWriter, SharedResourcesUseOneBasedDeduplicatedReferencesAndATrailingObject)
    {
        // XNB layout: one reader, one shared object, root (reader 1, int 42, refs 1/1),
        // then shared object (reader 1, int 42, refs 1/1). The two references and cycle
        // name the same instance, so neither a second resource nor a second reader is emitted.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<SharedLayoutWriter>();
        Compiler::CompileOptions options;
        options.compressContent = false;
        const auto value = std::make_shared<Waypoint>();
        const auto written = compiler.Compile<Waypoint>(value, options);
        const std::vector<std::uint8_t> expected{
            'X','N','B','w', 0x05,0x00, 0x28,0x00,0x00,0x00, // 40 bytes total
            0x01,0x09, 'T','Q','.','S','h','a','r','e','d', 0x00,0x00,0x00,0x00,
            0x01, // shared resource count
            0x01, 0x2a,0x00,0x00,0x00, 0x01,0x01, // root
            0x01, 0x2a,0x00,0x00,0x00, 0x01,0x01  // trailing shared object
        };
        EXPECT_EQ(written.bytes, expected);
    }



namespace
{
    TEST(XnaContentCompiler, Tq063PhoneIgnoresCompressionRequestForBothProfiles)
    {
        // Refresh ContentCompiler.ShouldCompressContent IL_0000-0005 returns false for Phone=2.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<LiteralLayoutWriter>();
        for (const auto profile : {Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach,
                                   Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef})
        {
            Compiler::CompileOptions options;
            options.targetPlatform = Xna::TargetPlatform::WindowsPhone;
            options.targetProfile = profile;
            const auto root = std::make_shared<Waypoint>();
            options.compressContent = false;
            const auto plain = compiler.Compile<Waypoint>(root, options).bytes;
            options.compressContent = true;
            const auto requested = compiler.Compile<Waypoint>(root, options).bytes;
            ASSERT_GE(requested.size(), 10u);
            EXPECT_EQ(requested[3], 'm');
            EXPECT_EQ(requested[4], 5);
            EXPECT_EQ(requested[5], profile == Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach ? 0 : 1);
            EXPECT_EQ(requested, plain);
        }
    }
}
    TEST(XnaContentWriter, RequestedCompressionSetsTheXnbLzxFlagAndUncompressedBodySize)
    {
        // XNB v5 specifies LZX as bit 7 (0x80) and a four-byte LE uncompressed-body
        // size immediately after the ten-byte header. No decompressor from CNA is the oracle.
        Compiler::ContentCompiler compiler;
        compiler.AddTypeWriter<LiteralLayoutWriter>();
        Compiler::CompileOptions options;
        const auto value = std::make_shared<Waypoint>();
        options.compressContent = false;
        const auto plain = compiler.Compile<Waypoint>(value, options);
        options.compressContent = true;
        const auto compressed = compiler.Compile<Waypoint>(value, options);
        ASSERT_GE(plain.bytes.size(), 10u);
        ASSERT_GE(compressed.bytes.size(), 14u);
        EXPECT_EQ(plain.bytes[5], 0x00);
        EXPECT_EQ(compressed.bytes[5], 0x80);
        const std::uint32_t declaredBodySize = compressed.bytes[10] |
            (std::uint32_t{compressed.bytes[11]} << 8) |
            (std::uint32_t{compressed.bytes[12]} << 16) |
            (std::uint32_t{compressed.bytes[13]} << 24);
        EXPECT_EQ(declaredBodySize, plain.bytes.size() - 10);
        const std::uint32_t declaredFileSize = compressed.bytes[6] |
            (std::uint32_t{compressed.bytes[7]} << 8) |
            (std::uint32_t{compressed.bytes[8]} << 16) |
            (std::uint32_t{compressed.bytes[9]} << 24);
        EXPECT_EQ(declaredFileSize, compressed.bytes.size());
    }
}


namespace
{
    std::barrier<>* tq063InitializerBarrier = nullptr;

    template<typename T>
    class Tq063ConcurrentWriter final : public Compiler::ContentTypeWriter<T>
    {
    public:
        std::string GetRuntimeReader(Xna::TargetPlatform) const override
        {
            if constexpr (std::is_same_v<T,Waypoint>) { return "TQ.ConcurrentPoint"; }
            else { return "TQ.ConcurrentNamed"; }
        }
    protected:
        void Initialize(Compiler::ContentCompiler& compiler) override
        {
            const auto lookup = [&] {
                if constexpr (std::is_same_v<T,Waypoint>)
                { (void)compiler.GetTypeWriter(System::Type::From<std::string>()); }
                else { (void)compiler.GetTypeWriter(System::Type::From<std::int32_t>()); }
            };
            lookup();
            tq063InitializerBarrier->arrive_and_wait();
            lookup();
        }
        void Write(Compiler::ContentWriter& output,const std::shared_ptr<T>&) override
        {
            output.Write(SharpRuntime::intcs{std::is_same_v<T,Waypoint> ? 1 : 2});
        }
    };

    TEST(XnaContentCompiler, Tq063ConcurrentInitializersKeepSeparateDependencyFrames)
    {
        // Refresh RecordDependency/GetTypeWriter records per-initialization lookup order;
        // ContentWriter.GetTypeWriter IL_002c-0085 emits each dependency once. CNA's
        // concurrent registration extension must keep overlapping initializations separate.
        WithCompilerCallbackDeadline([] {
            Compiler::ContentCompiler compiler;
            std::barrier barrier(2);
            tq063InitializerBarrier = &barrier;
            std::exception_ptr failures[2];
            std::jthread first([&] {
                try { compiler.AddTypeWriter<Tq063ConcurrentWriter<Waypoint>>(); }
                catch (...) { failures[0]=std::current_exception(); }
            });
            std::jthread second([&] {
                try { compiler.AddTypeWriter<Tq063ConcurrentWriter<NamedWaypoint>>(); }
                catch (...) { failures[1]=std::current_exception(); }
            });
            first.join(); second.join();
            tq063InitializerBarrier = nullptr;
            for (const auto& failure : failures) { if (failure) { std::rethrow_exception(failure); } }
            auto options=Options(); options.compressContent=false;
            const auto point=Tq063Scan(compiler.Compile<Waypoint>(std::make_shared<Waypoint>(),options).bytes);
            const auto named=Tq063Scan(compiler.Compile<NamedWaypoint>(std::make_shared<NamedWaypoint>(),options).bytes);
            EXPECT_EQ(point.readers,(std::vector<std::pair<std::string,std::int32_t>>{
                {"TQ.ConcurrentPoint",0},{"Microsoft.Xna.Framework.Content.StringReader",0}}));
            EXPECT_EQ(named.readers,(std::vector<std::pair<std::string,std::int32_t>>{
                {"TQ.ConcurrentNamed",0},{"Microsoft.Xna.Framework.Content.Int32Reader",0}}));
            EXPECT_EQ(point.payload,(std::vector<std::uint8_t>{1,1,0,0,0}));
            EXPECT_EQ(named.payload,(std::vector<std::uint8_t>{1,2,0,0,0}));
        });
    }

    TEST(XnaContentCompiler, Tq063ValueCarriersAreNotNullRoots)
    {
        // Refresh Compile IL_000e-001b rejects null object references, not false/zero/
        // empty value carriers. BinaryWriter payloads below are hand-derived zero bytes.
        Compiler::ContentCompiler compiler;
        auto options=Options(); options.compressContent=false;
        const auto check=[&]<typename T>(const Xna::Carrier<T>& value,const std::string& reader,std::size_t size) {
            const auto frame=Tq063Scan(compiler.Compile<T>(value,options).bytes);
            EXPECT_EQ(frame.readers,(std::vector<std::pair<std::string,std::int32_t>>{
                {"Microsoft.Xna.Framework.Content."+reader,0}}));
            std::vector<std::uint8_t> expected(size+1,0); expected[0]=1;
            EXPECT_EQ(frame.payload,expected) << reader;
            EXPECT_EQ(frame.shared,0u);
        };
        check.template operator()<bool>(false,"BooleanReader",1);
        check.template operator()<std::int32_t>(0,"Int32Reader",4);
        check.template operator()<std::string>("","StringReader",1);
        check.template operator()<System::DateTime>(System::DateTime(0),"DateTimeReader",8);
        check.template operator()<System::TimeSpan>(System::TimeSpan(0),"TimeSpanReader",8);
    }
}


namespace
{
    TEST(XnaContentWriter, Tq063ReferenceHintsAlsoDispatchValueCarriers)
    {
        // Refresh WriteObject<T>(T,writer) IL_000e-0026 branches on the writer's
        // TargetIsValueType, regardless of T. A reference hint is ignored for int too.
        Compiler::ContentCompiler compiler;
        auto hint=compiler.AddTypeWriter<RouteWriter>();
        auto writer=compiler.AddTypeWriter<Tq063ActionWriter>();
        writer->action=[hint](Compiler::ContentWriter& output) {
            output.WriteObject<std::int32_t>(7,*hint);
            output.WriteObject<System::DateTime>(System::DateTime(0),*hint);
            output.WriteRawObject<System::DateTime>(System::DateTime(0));
        };
        auto options=Options(); options.compressContent=false;
        const auto frame=Tq063Scan(compiler.Compile<Waypoint>(std::make_shared<Waypoint>(),options).bytes);
        EXPECT_EQ(frame.readers,(std::vector<std::pair<std::string,std::int32_t>>{
            {"TQ.Action",0},{"Microsoft.Xna.Framework.Content.Int32Reader",0},
            {"Microsoft.Xna.Framework.Content.DateTimeReader",0}}));
        // Kind Unspecified and ticks 0 give eight zero bytes, with index 3 only
        // on the dispatched field; the final raw DateTime has no index.
        EXPECT_EQ(frame.payload,(std::vector<std::uint8_t>{1,2,7,0,0,0,
            3,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0}));
    }
}
