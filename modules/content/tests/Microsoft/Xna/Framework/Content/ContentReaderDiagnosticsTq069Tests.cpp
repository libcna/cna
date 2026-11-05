// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace Microsoft::Xna::Framework::Content;
namespace
{
    template<class F> void ExpectLoadMessage(F read, const std::string& expected)
    {
        try { read(); FAIL() << "expected ContentLoadException"; }
        catch (const ContentLoadException& error) { EXPECT_EQ(std::string(error.what()), expected); }
    }

    class IntReader final : public ContentTypeReader<int32_t>
    {
    public:
        IntReader() : ContentTypeReader<int32_t>("System.Int32") {}
        static inline int reads = 0;
    protected:
        int32_t Read(ContentReader& input, std::optional<int32_t>) override
        { ++reads; return input.ReadInt32(); }
    };
    class UnnamedIntReader final : public ContentTypeReader<int32_t>
    {
    public:
        UnnamedIntReader() = default;
    protected:
        int32_t Read(ContentReader& input, std::optional<int32_t>) override
        { ++IntReader::reads; return input.ReadInt32(); }
    };
    class ErasedNullReader final : public ContentTypeReaderBase
    {
    public:
        ErasedNullReader() : ContentTypeReaderBase("System.Object") {}
        std::any ReadUntyped(ContentReader&, std::any) override { return {}; }
    };
    struct Item final : System::IDisposable { void Dispose() override {} };
    using Ref = std::shared_ptr<Item>;
    class ReplacingReader final : public ContentTypeReader<Ref>
    {
    public:
        ReplacingReader() : ContentTypeReader<Ref>("Tq069.Item") {}
    protected:
        Ref Read(ContentReader& input, std::optional<Ref>) override
        { (void)input.ReadByte(); return std::make_shared<Item>(); }
    };

    std::vector<uint8_t> Body(const char* name, bool indexed)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(1); writer.Write(std::string(name)); writer.Write(int32_t{0});
        writer.Write7BitEncodedInt(0);
        if (indexed) writer.Write7BitEncodedInt(1);
        writer.Write(int32_t{73}); writer.Write(uint8_t{0x5a}); writer.Flush();
        return stream.ToArray();
    }
    class ContentReaderDiagnosticsTq069Test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            IntReader::reads = 0;
            ContentTypeReaderManager::AddTypeCreator("Tq069.IntReader",
                [] { return std::make_unique<IntReader>(); });
            ContentTypeReaderManager::AddTypeCreator("Tq069.NullReader",
                [] { return std::make_unique<ErasedNullReader>(); });
        }
        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
    };

    TEST_F(ContentReaderDiagnosticsTq069Test, WrongExistingTypeRefusesBeforeReadingInAllApplicableRoutes)
    {
        // Refresh ContentTypeReader<T>.Read(object) IL_000d-003e: reader target FIRST,
        // existing object's runtime type SECOND in BadXnbWrongType; Read is not reached.
        // Unlike a raw overload inferred from T, these routes can select a different target.
        for (int mode : {0, 1, 2, 3, 4, 5})
        {
            SCOPED_TRACE(mode);
            const auto bytes = Body("Tq069.IntReader", mode == 0);
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            int recorded = 0;
            ContentReader input(nullptr, &stream, "models/ship", 5, 'w', [&](auto) { ++recorded; });
            input.InitializeTypeReaders();
            const auto start = stream.getPositionProperty();
            IntReader type;
            if (mode == 4) ContentTypeReaderManager::AssociateTargetType(
                System::Type::From<std::string>(), "Tq069.IntReader");
            ExpectLoadMessage([&] {
                switch (mode)
                {
                case 0: (void)input.ReadObject<std::string>(std::string("keep")); break;
                case 1: (void)input.ReadObject<std::string>(type, std::string("keep")); break;
                case 2: (void)input.ReadRawObject<std::string>(type, std::string("keep")); break;
                case 3: (void)type.ReadUntyped(input, std::any(std::string("keep"))); break;
                case 4: (void)input.ReadRawObject<std::string>(std::string("keep")); break;
                case 5:
                {
                    // Refresh bridge IL_0024-002e uses typeof(T), including parameterless readers.
                    UnnamedIntReader unnamed;
                    (void)input.ReadRawObject<std::string>(unnamed, std::string("keep"));
                    break;
                }
                }
            }, "Error loading \"models/ship\". File contains System.Int32 but trying to load as System.String.");
            EXPECT_EQ(IntReader::reads, 0);
            EXPECT_EQ(recorded, 0);
            EXPECT_EQ(stream.getPositionProperty(), start + (mode == 0 ? 1 : 0));
            EXPECT_EQ(input.ReadInt32(), 73); // payload untouched, not merely an EOF refusal
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, WrongReturnedTypeNamesTheRuntimeResultAndRequestedType)
    {
        // Refresh InvokeReader<T> IL_0030-006d: result checked AFTER Read, before ownership.
        for (int mode : {0, 1, 2, 3})
        {
            SCOPED_TRACE(mode);
            const auto bytes = Body("Tq069.IntReader", mode == 0);
            System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
            ContentReader input(nullptr, &stream, "models/ship", 5, 'w');
            input.InitializeTypeReaders();
            const auto start = stream.getPositionProperty();
            const int before = IntReader::reads;
            IntReader type;
            if (mode == 3) ContentTypeReaderManager::AssociateTargetType(
                System::Type::From<std::string>(), "Tq069.IntReader");
            ExpectLoadMessage([&] {
                if (mode == 0) (void)input.ReadObject<std::string>();
                if (mode == 1) (void)input.ReadObject<std::string>(type);
                if (mode == 2) (void)input.ReadRawObject<std::string>(type);
                if (mode == 3) (void)input.ReadRawObject<std::string>();
            }, "Error loading \"models/ship\". File contains System.Int32 but trying to load as System.String.");
            EXPECT_EQ(IntReader::reads, before + 1);
            EXPECT_EQ(stream.getPositionProperty(), start + 4 + (mode == 0 ? 1 : 0));
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, ErasedNullResultBecomesDefaultForReferenceAndValueRequests)
    {
        // Refresh InvokeReader<T> IL_0039-003a / 0077-007d: null from non-generic Read -> default(T).
        // Covers a non-generic reader; a typed reader's any holding shared_ptr{} is different.
        for (bool indexed : {false, true})
        {
            const auto bytes = Body("Tq069.NullReader", indexed);
            for (bool valueType : {false, true})
            {
                SCOPED_TRACE(indexed);
                SCOPED_TRACE(valueType);
                System::IO::MemoryStream stream(bytes.data(), static_cast<int32_t>(bytes.size()));
                int recorded = 0;
                ContentReader input(nullptr, &stream, "null-result", 5, 'w', [&](auto) { ++recorded; });
                input.InitializeTypeReaders();
                const auto start = stream.getPositionProperty();
                ErasedNullReader type;
                if (valueType)
                    EXPECT_EQ(indexed ? input.ReadObject<int32_t>() : input.ReadRawObject<int32_t>(type), 0);
                else
                    EXPECT_EQ(indexed ? input.ReadObject<Ref>() : input.ReadRawObject<Ref>(type), nullptr);
                EXPECT_EQ(recorded, 0);
                EXPECT_EQ(stream.getPositionProperty(), start + (indexed ? 1 : 0));
                EXPECT_EQ(input.ReadInt32(), 73);
            }
        }
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, NullMoveOnlyCarrierRefusesWithoutDereferencingANullBox)
    {
        struct MoveOnly
        {
            explicit MoveOnly(int) {}
            MoveOnly(const MoveOnly&) = delete;
            MoveOnly(MoveOnly&&) = default;
        };
        class NullBoxReader final : public ContentTypeReaderBase
        {
            bool boxed_;
        public:
            explicit NullBoxReader(bool boxed) : ContentTypeReaderBase("Tq069.MoveOnly"), boxed_(boxed) {}
            std::any ReadUntyped(ContentReader&, std::any) override
            { return boxed_ ? std::any(std::shared_ptr<MoveOnly>{}) : std::any{}; }
        };
        // C++ carrier adaptation: there is no default/native null MoveOnly object to return.
        // Refresh null->default(T) must not make CNA dereference a null shared box.
        for (bool boxed : {false, true})
        {
            const uint8_t bytes[]{0x5a};
            System::IO::MemoryStream stream(bytes, sizeof(bytes));
            ContentReader input(nullptr, &stream, "move-only", 5, 'w');
            NullBoxReader type(boxed);
            ExpectLoadMessage([&] { (void)input.ReadRawObject<MoveOnly>(type); },
                "'move-only' has a null object with no native value representation.");
            EXPECT_EQ(stream.getPositionProperty(), 0);
            EXPECT_EQ(input.ReadByte(), 0x5a);
        }
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, WrongRuntimeResultDoesNotUseTheReadersDeclaredTargetName)
    {
        class LyingReader final : public ContentTypeReaderBase
        {
        public:
            LyingReader() : ContentTypeReaderBase("Tq069.DeclaredTarget") {}
            std::any ReadUntyped(ContentReader& input, std::any) override
            { (void)input.ReadByte(); return std::string("result"); }
        } type;
        const uint8_t bytes[]{0x73, 0x5a};
        System::IO::MemoryStream stream(bytes, sizeof(bytes));
        ContentReader input(nullptr, &stream, "runtime-type", 5, 'w');
        // InvokeReader IL_0053-0059 uses result.GetType(), not reader.TargetType.
        ExpectLoadMessage([&] { (void)input.ReadRawObject<int32_t>(type); },
            "Error loading \"runtime-type\". File contains System.String but trying to load as System.Int32.");
        EXPECT_EQ(input.ReadByte(), 0x5a);
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, DiagnosticsUseAssociatedNamesWithoutInvokingFactories)
    {
        struct Requested { int value = 0; };
        int factories = 0;
        ContentTypeReaderManager::AssociateTargetType(System::Type::From<Requested>(), "Tq069.Requested");
        ContentTypeReaderManager::AddTypeCreator("Tq069.Requested", [&] {
            ++factories; return std::make_unique<IntReader>();
        });
        const uint8_t bytes[]{73, 0, 0, 0, 0x5a};
        System::IO::MemoryStream stream(bytes, sizeof(bytes));
        ContentReader input(nullptr, &stream, "associated", 5, 'w');
        IntReader type;
        // Refresh InvokeReader IL_0053-0066 formats Type values; formatting is not reader lookup.
        // The native canonical association names Requested, without running game factories.
        ExpectLoadMessage([&] { (void)input.ReadRawObject<Requested>(type); },
            "Error loading \"associated\". File contains System.Int32 but trying to load as Tq069.Requested.");
        EXPECT_EQ(factories, 0);
        EXPECT_EQ(input.ReadByte(), 0x5a);
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, UnregisteredRawTypeHasAssetAndTargetInBothOverloadMessages)
    {
        ContentTypeReaderManager::ClearTypeCreators();
        const uint8_t bytes[]{0x5a};
        System::IO::MemoryStream stream(bytes, sizeof(bytes));
        ContentReader input(nullptr, &stream, "missing-reader", 5, 'w');
        // Refresh GetTypeReader(Type,ContentReader) IL_0033-004a + TypeReaderNotRegistered.
        for (bool existing : {false, true})
            ExpectLoadMessage([&] {
                if (existing) (void)input.ReadRawObject<int32_t>(int32_t{9});
                else (void)input.ReadRawObject<int32_t>();
            }, "Error loading \"missing-reader\". Cannot find ContentTypeReader for System.Int32.");
        EXPECT_EQ(stream.getPositionProperty(), 0);
        EXPECT_EQ(input.ReadByte(), 0x5a);
    }

    TEST_F(ContentReaderDiagnosticsTq069Test, ReplacementErrorUsesReaderConstructedNewInstanceResource)
    {
        const uint8_t bytes[]{0x73, 0x5a};
        System::IO::MemoryStream stream(bytes, sizeof(bytes));
        int recorded = 0;
        ContentReader input(nullptr, &stream, "replacement", 5, 'w', [&](auto) { ++recorded; });
        ReplacingReader type;
        const auto existing = std::make_shared<Item>();
        // Refresh InvokeReader<T> IL_0090-00b8. Native reader classes have RTTI names;
        // std::type_info::name supplies the native replacement for managed Type.ToString.
        try { (void)input.ReadRawObject<Ref>(type, existing); FAIL() << "replacement accepted"; }
        catch (const System::InvalidOperationException& error)
        {
            EXPECT_EQ(std::string(error.what()), std::string("ContentTypeReader ") + typeid(ReplacingReader).name() +
                " returned a new object instance from its Read method. This should have loaded data into the existingInstance parameter.");
        }
        EXPECT_EQ(recorded, 0);
        EXPECT_EQ(input.ReadByte(), 0x5a);
    }
}
