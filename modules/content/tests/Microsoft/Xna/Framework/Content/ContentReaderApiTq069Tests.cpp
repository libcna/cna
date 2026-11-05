// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"


using namespace Microsoft::Xna::Framework::Content;
namespace
{
    struct Item { int value = -1; };
    using Ref = std::shared_ptr<Item>;
    class ItemReader final : public ContentTypeReader<Ref>
    {
    public:
        ItemReader() : ContentTypeReader<Ref>("Tq069.ApiItem") {}
        static inline int reads = 0;
    protected:
        Ref Read(ContentReader& input, std::optional<Ref> existing) override
        {
            ++reads;
            auto result = existing.value_or(nullptr);
            if (!result) result = std::make_shared<Item>();
            result->value = input.ReadByte(); return result;
        }
    };
    class DecoyReader final : public ContentTypeReader<std::string>
    {
    public:
        DecoyReader() : ContentTypeReader<std::string>("System.String") {}
        int calls = 0;
    protected:
        std::string Read(ContentReader&, std::optional<std::string>) override
        { ++calls; return "decoy"; }
    };
    class ContentReaderApiTq069Test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            ItemReader::reads = 0;
        }
        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
    };

    TEST_F(ContentReaderApiTq069Test, ExplicitReferenceReaderIsADispatchHintEvenWithAnotherTargetType)
    {
        // Refresh ReadObjectInternal(typeReader,object) IL_000e-0026: reference hint is ignored;
        // stream's selected reader receives existing. ReadObjectInternal(object) IL_000a-0013:
        // zero index is default(T) even with non-null existing. FNA's no-existing overload differs.
        ContentTypeReaderManager::AddTypeCreator("Tq069.ApiItemReader",
            [] { return std::make_unique<ItemReader>(); });
        for (int mode : {0, 1, 2}) for (uint8_t index : {uint8_t{0}, uint8_t{1}})
        {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(static_cast<int>(index));
            System::IO::MemoryStream body;
            System::IO::BinaryWriter writer(&body, true);
            writer.Write7BitEncodedInt(1); writer.Write(std::string("Tq069.ApiItemReader"));
            writer.Write(int32_t{0}); writer.Write7BitEncodedInt(0); writer.Write(index);
            if (index) writer.Write(uint8_t{73});
            writer.Write(uint8_t{0x5a}); writer.Flush();
            const auto bytes = body.ToArray();
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            ContentReader input(nullptr, &stream, "hint", 5, 'w');
            input.InitializeTypeReaders();
            DecoyReader hint;
            const auto existing = std::make_shared<Item>();
            const int before = ItemReader::reads;
            Ref result;
            if (mode == 0) result = input.ReadObject<Ref>(hint);
            if (mode == 1) result = input.ReadObject<Ref>(hint, existing);
            if (mode == 2) result = input.ReadObject<Ref>(hint, Ref{});
            EXPECT_EQ(hint.calls, 0);
            EXPECT_EQ(ItemReader::reads, before + (index ? 1 : 0));
            if (index)
            {
                ASSERT_NE(result, nullptr);
                EXPECT_EQ(result->value, 73);
                EXPECT_EQ(result == existing, mode == 1);
            }
            else EXPECT_EQ(result, nullptr);
            EXPECT_EQ(existing->value, index && mode == 1 ? 73 : -1);
            EXPECT_EQ(stream.getPositionProperty(), static_cast<int64_t>(bytes.size() - 1));
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderApiTq069Test, EmptyExternalReferenceReturnsBeforeRequiringAnOwnerInBothRoutes)
    {
        // Refresh ReadExternalReference<T> IL_0007-0018 returns default before manager access.
        // The existing texture fixture constructs a GraphicsDevice; this witness is device-free.
        for (bool erased : {false, true})
        {
            const uint8_t bytes[]{0, 0x5a}; // 7-bit zero UTF-8 length, then next field
            System::IO::MemoryStream stream(bytes, sizeof(bytes));
            ContentReader input(nullptr, &stream, "models/ship", 5, 'w');
            if (erased) EXPECT_FALSE(input.ReadExternalReference().has_value());
            else EXPECT_FALSE(input.ReadExternalReference<int32_t>().has_value());
            EXPECT_EQ(stream.getPositionProperty(), 1);
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderApiTq069Test, TruncatedTypedPrimitivesPropagateEndOfStream)
    {
        // Refresh ReadMatrix IL_0003-00cb, Quaternion/Vector4 0003-002f,
        // Vector2 0003-0015, Vector3 0003-0022, Color 000b-0010: each field invokes BinaryReader, with no catch/defaulting.
        // ReadSingle IL_0001-000b reinterprets ReadUInt32. BinaryReader's documented short-read
        // contract is EndOfStreamException; these are raw API calls, outside ReadAsset's wrapper.
        const int widths[]{4, 8, 12, 16, 16, 64}; // Color,V2,V3,V4,Quaternion,Matrix
        for (int kind = 0; kind < 6; ++kind) for (int count : {0, widths[kind] - 1})
        {
            SCOPED_TRACE(kind);
            SCOPED_TRACE(count);
            const std::vector<uint8_t> bytes(static_cast<std::size_t>(count), 0);
            System::IO::MemoryStream stream(bytes.data(), count);
            ContentReader input(nullptr, &stream, "short-primitive", 5, 'w');
            EXPECT_THROW({
                switch (kind)
                {
                case 0: (void)input.ReadColor(); break;
                case 1: (void)input.ReadVector2(); break;
                case 2: (void)input.ReadVector3(); break;
                case 3: (void)input.ReadVector4(); break;
                case 4: (void)input.ReadQuaternion(); break;
                case 5: (void)input.ReadMatrix(); break;
                }
            }, System::IO::EndOfStreamException);
            EXPECT_EQ(stream.getPositionProperty(), count);
        }
    }

    TEST_F(ContentReaderApiTq069Test, BaseDefaultsAndInitializeKeepTheDeclaredTargetAndDoNotRead)
    {
        class Probe final : public ContentTypeReaderBase
        {
        public:
            explicit Probe(std::string name) : ContentTypeReaderBase(std::move(name)) {}
            int reads = 0;
            std::any ReadUntyped(ContentReader& input, std::any) override
            { ++reads; return input.ReadByte(); }
        };
        // Refresh base .ctor IL_0006-0008; TargetType IL_0001-0006; version/capability 0000-0001;
        // Initialize IL_0000 is ret. Empty native name represents a null/unknown target.
        for (const std::string name : {std::string{}, std::string("Tq069.Declared")})
        {
            Probe type(name);
            ContentTypeReaderManager manager;
            const uint8_t bytes[]{0x73, 0x5a};
            System::IO::MemoryStream stream(bytes, sizeof(bytes));
            ContentReader input(nullptr, &stream, "base", 5, 'w');
            for (int pass : {0, 1})
            {
                if (pass) type.Initialize(manager);
                EXPECT_EQ(type.getTargetTypeNameProperty(), name);
                EXPECT_EQ(type.getTypeVersionProperty(), 0);
                EXPECT_FALSE(type.getCanDeserializeIntoExistingObjectProperty());
                EXPECT_EQ(type.reads, 0);
                EXPECT_EQ(stream.getPositionProperty(), 0);
            }
            EXPECT_EQ(std::any_cast<uint8_t>(type.ReadUntyped(input, {})), 0x73);
            EXPECT_EQ(type.reads, 1);
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderApiTq069Test, ManifestCreatesEveryReaderBeforeInitializingAndReadingInOrder)
    {
        std::vector<std::string> trace;
        int constructed = 0;
        class Probe final : public ContentTypeReader<int32_t>
        {
            std::vector<std::string>& trace_;
            int& constructed_;
            std::string label_;
            bool initialized_ = false;
        public:
            Probe(std::vector<std::string>& trace, int& constructed, std::string label)
                : ContentTypeReader<int32_t>("Tq069.Init" + label), trace_(trace),
                  constructed_(constructed), label_(std::move(label))
            { ++constructed_; trace_.push_back("create " + label_); }
            void Initialize(ContentTypeReaderManager& manager) override
            {
                ContentTypeReaderBase::Initialize(manager);
                initialized_ = true;
                trace_.push_back("initialize " + label_ + " of " + std::to_string(constructed_));
            }
        protected:
            int32_t Read(ContentReader& input, std::optional<int32_t>) override
            {
                trace_.push_back(initialized_ ? "read initialized " + label_ : "read UNINITIALIZED");
                return input.ReadByte();
            }
        };
        // Refresh ReadTypeManifest IL_0020-006b constructs/versions the whole table first;
        // IL_0084-009d initializes newly created readers, then ReadAsset reads the root.
        for (const std::string label : {"A", "B"})
            ContentTypeReaderManager::AddTypeCreator("Tq069.InitReader" + label,
                [&, label] { return std::make_unique<Probe>(trace, constructed, label); });
        System::IO::MemoryStream body;
        System::IO::BinaryWriter writer(&body, true);
        writer.Write7BitEncodedInt(2);
        for (const std::string label : {"A", "B"})
        { writer.Write("Tq069.InitReader" + label); writer.Write(int32_t{0}); }
        writer.Write7BitEncodedInt(0); writer.Write7BitEncodedInt(1); writer.Write(uint8_t{73});
        writer.Write(uint8_t{0x5a}); writer.Flush();
        const auto bytes = body.ToArray();
        System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
        ContentReader input(nullptr, &stream, "initialize", 5, 'w');
        EXPECT_EQ(input.ReadAsset<int32_t>(), 73);
        EXPECT_EQ(trace, (std::vector<std::string>{"create A", "create B",
            "initialize A of 2", "initialize B of 2", "read initialized A"}));
        EXPECT_EQ(input.ReadByte(), 0x5a);
    }
}
