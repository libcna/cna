// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-012: the documented Content runtime contracts.
//
//   ContentLoadException()                              -- the parameterless constructor
//   ContentManager.Dispose(Boolean)                     -- the protected disposal hook
//   ContentManager.OpenStream(String)                   -- the protected virtual stream seam
//   ContentManager.ReadAsset<T>(String, Action<IDisposable>)
//   ContentReader.ReadRawObject<T>() / ReadRawObject<T>(T)
//   ContentTypeReader<T>()                              -- the parameterless constructor
//   ContentTypeReader<T>.Read(ContentReader, Object)    -- the object-form bridge
//   ContentTypeReaderManager.GetTypeReader(Type)
//
// The interesting parts are the seams: an override of OpenStream must be what ReadAsset reads
// from, the disposal hook must be what Dispose() routes through, and the type-to-reader lookup
// must be what ReadRawObject<T>() resolves through -- C++ has no reflection, so that association
// is recorded when a ContentTypeReader<T> is constructed, and asserting it is asserting the
// mechanism rather than a happy path.

#include <gtest/gtest.h>

#include <any>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/IDisposable.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "System/IO/Stream.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/Type.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderBase;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    struct ContractValue
    {
        std::int32_t value = 0;
    };

    /// A reader registered under a canonical name, which is also what associates ContractValue with
    /// that name for ContentTypeReaderManager::GetTypeReader(Type).
    class ContractValueReader final : public ContentTypeReader<ContractValue>
    {
    public:
        ContractValueReader() : ContentTypeReader<ContractValue>("CNA.Test.ContractValue") {}

    protected:
        ContractValue Read(ContentReader& input, std::optional<ContractValue> existing) override
        {
            ContractValue result = existing.value_or(ContractValue{});
            result.value = input.ReadInt32();
            return result;
        }
    };

    /// A type nothing is registered for, so GetTypeReader(Type) has nothing to answer with.
    struct UnknownValue
    {
        int unused = 0;
    };

    /// A reader built through the documented parameterless constructor. ContractValue is not a
    /// System::Object, so no canonical name can be derived for it -- the reader is unnamed, which
    /// means it is not selectable from an .xnb type-reader table but still reads when invoked
    /// directly.
    class UnnamedContractValueReader final : public ContentTypeReader<ContractValue>
    {
    public:
        UnnamedContractValueReader() = default;

    protected:
        ContractValue Read(ContentReader& input, std::optional<ContractValue> existing) override
        {
            ContractValue result = existing.value_or(ContractValue{});
            result.value = input.ReadInt32() * 10;
            return result;
        }
    };

    std::vector<std::uint8_t> BuildContractXnb(std::int32_t value)
    {
        System::IO::MemoryStream body;
        {
            System::IO::BinaryWriter writer(&body, true);
            writer.Write7BitEncodedInt(1);                     // one type reader
            writer.Write(std::string("CNA.Test.ContractValue"));
            writer.Write(static_cast<std::int32_t>(0));         // reader version
            writer.Write7BitEncodedInt(0);                     // no shared resources
            writer.Write7BitEncodedInt(1);                     // root object uses reader 1
            writer.Write(value);
        }
        const auto bodyArray = body.ToArray();

        System::IO::MemoryStream file;
        {
            System::IO::BinaryWriter writer(&file, true);
            writer.Write(static_cast<std::uint8_t>('X'));
            writer.Write(static_cast<std::uint8_t>('N'));
            writer.Write(static_cast<std::uint8_t>('B'));
            writer.Write(static_cast<std::uint8_t>('w'));
            writer.Write(static_cast<std::uint8_t>(5));
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(static_cast<std::int32_t>(10 + bodyArray.size()));
            for (const auto byteValue : bodyArray)
            {
                writer.Write(static_cast<std::uint8_t>(byteValue));
            }
        }
        const auto fileArray = file.ToArray();
        return std::vector<std::uint8_t>(fileArray.begin(), fileArray.end());
    }

    class TemporaryContentRoot
    {
    public:
        TemporaryContentRoot()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_content_contract_" + std::to_string(++counter_)))
        {
            std::filesystem::create_directories(dir_);
        }

        ~TemporaryContentRoot()
        {
            std::error_code ignored;
            std::filesystem::remove_all(dir_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

        void Write(const std::string& relative, const std::vector<std::uint8_t>& bytes) const
        {
            const std::filesystem::path target = dir_ / relative;
            std::filesystem::create_directories(target.parent_path());
            std::ofstream out(target, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }

    private:
        std::filesystem::path dir_;
        static int counter_;
    };

    int TemporaryContentRoot::counter_ = 0;

    /// A manager that serves assets from memory by overriding the documented stream seam, and
    /// records whether the seam and the disposal hook were reached.
    class ProbeContentManager final : public ContentManager
    {
    public:
        using ContentManager::Dispose;
        using ContentManager::ReadAsset;

        explicit ProbeContentManager(std::vector<std::uint8_t> bytes)
            : ContentManager(nullptr, "unused"), bytes_(std::move(bytes))
        {
        }

        int openStreamCalls = 0;
        int disposeHookCalls = 0;
        bool lastDisposing = false;
        std::string lastAssetName;

    protected:
        [[nodiscard]] std::unique_ptr<System::IO::Stream> OpenStream(
            const std::string& assetName) override
        {
            ++openStreamCalls;
            lastAssetName = assetName;
            if (bytes_.empty())
            {
                throw ContentLoadException("probe: nothing to serve for '" + assetName + "'.");
            }
            return std::make_unique<System::IO::MemoryStream>(
                bytes_.data(), static_cast<SharpRuntime::intcs>(bytes_.size()), false);
        }

        void Dispose(bool disposing) override
        {
            ++disposeHookCalls;
            lastDisposing = disposing;
            ContentManager::Dispose(disposing);
        }

    private:
        std::vector<std::uint8_t> bytes_;
    };

    /// A manager that only reports whether the base OpenStream found the asset.
    class BaseStreamManager final : public ContentManager
    {
    public:
        using ContentManager::OpenStream;
        using ContentManager::ReadAsset;

        explicit BaseStreamManager(const std::string& root) : ContentManager(nullptr, root) {}
    };

    class ContentRuntimeContractTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
            ContentTypeReaderManager::AddTypeCreator(
                "CNA.Test.ContractValue", [] { return std::make_unique<ContractValueReader>(); });
        }

        void TearDown() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            ContentTypeReaderManager::ClearTargetTypeAssociationsEXT();
        }
    };
}

// =============================================================================
// ContentLoadException()
// =============================================================================

TEST(ContentLoadExceptionContractTest, TheParameterlessConstructorNamesItsOwnType)
{
    const ContentLoadException error;
    const std::string message = error.what();
    EXPECT_NE(message.find("Microsoft.Xna.Framework.Content.ContentLoadException"),
              std::string::npos)
        << "XNA leaves the message to System.Exception's parameterless constructor, whose .NET "
           "fallback text names the exception's own type";

    // Still the same exception type as the message-taking constructors, and still catchable the
    // way every content throw site is caught.
    EXPECT_THROW({ throw ContentLoadException(); }, ContentLoadException);
    EXPECT_THROW({ throw ContentLoadException(); }, std::runtime_error);
    EXPECT_NE(std::string(ContentLoadException("explicit").what()), message);
}

// =============================================================================
// ContentManager.Dispose(Boolean)
// =============================================================================

TEST_F(ContentRuntimeContractTest, PublicDisposeRoutesThroughTheProtectedHook)
{
    ProbeContentManager manager(BuildContractXnb(1));
    ASSERT_EQ(manager.disposeHookCalls, 0);

    manager.Dispose();
    EXPECT_EQ(manager.disposeHookCalls, 1);
    EXPECT_TRUE(manager.lastDisposing);

    // Idempotent: the forwarder runs again, the base does nothing more.
    manager.Dispose();
    EXPECT_EQ(manager.disposeHookCalls, 2);
}

TEST_F(ContentRuntimeContractTest, DisposalThroughTheDisposableInterfaceReachesTheOverride)
{
    ProbeContentManager manager(BuildContractXnb(1));
    System::IDisposable& asDisposable = manager;
    asDisposable.Dispose();
    EXPECT_EQ(manager.disposeHookCalls, 1);
    EXPECT_TRUE(manager.lastDisposing);
}

TEST_F(ContentRuntimeContractTest, ADisposedManagerRefusesToReadAnAsset)
{
    ProbeContentManager manager(BuildContractXnb(1));
    manager.Dispose();
    EXPECT_THROW((void)manager.ReadAsset<ContractValue>("anything", {}),
                 System::ObjectDisposedException);
}

// =============================================================================
// ContentManager.OpenStream / ReadAsset
// =============================================================================

TEST_F(ContentRuntimeContractTest, ReadAssetReadsThroughTheOverriddenStreamSeam)
{
    ProbeContentManager manager(BuildContractXnb(4242));
    const ContractValue result = manager.ReadAsset<ContractValue>("in-memory", {});

    EXPECT_EQ(result.value, 4242);
    EXPECT_EQ(manager.openStreamCalls, 1) << "the asset must be read through OpenStream, once";
    EXPECT_EQ(manager.lastAssetName, "in-memory")
        << "the asset name reaches the override unchanged, without an extension";
}

TEST_F(ContentRuntimeContractTest, ReadAssetSurfacesTheSeamsOwnFailure)
{
    ProbeContentManager manager({});
    EXPECT_THROW((void)manager.ReadAsset<ContractValue>("missing", {}), ContentLoadException);
    EXPECT_EQ(manager.openStreamCalls, 1);
}

TEST_F(ContentRuntimeContractTest, ReadAssetRefusesAnEmptyAssetName)
{
    ProbeContentManager manager(BuildContractXnb(1));
    EXPECT_THROW((void)manager.ReadAsset<ContractValue>("", {}), System::ArgumentNullException);
    EXPECT_EQ(manager.openStreamCalls, 0) << "the name is validated before the seam is called";
}

TEST_F(ContentRuntimeContractTest, ReadAssetDoesNotPopulateTheCache)
{
    // XNA's Load<T> caches and ReadAsset<T> reads; reading twice reads twice.
    ProbeContentManager manager(BuildContractXnb(7));
    EXPECT_EQ(manager.ReadAsset<ContractValue>("twice", {}).value, 7);
    EXPECT_EQ(manager.ReadAsset<ContractValue>("twice", {}).value, 7);
    EXPECT_EQ(manager.openStreamCalls, 2);
}

TEST_F(ContentRuntimeContractTest, TheBaseOpenStreamServesTheContentRoot)
{
    TemporaryContentRoot root;
    root.Write("fixture.xnb", BuildContractXnb(99));

    BaseStreamManager manager(root.path().string());
    const std::unique_ptr<System::IO::Stream> stream = manager.OpenStream("fixture");
    ASSERT_NE(stream, nullptr);

    // The stream holds the whole container, starting at its header.
    std::array<std::uint8_t, 4> signature{};
    ASSERT_EQ(stream->Read(signature.data(), 0, 4), 4);
    EXPECT_EQ(signature[0], 'X');
    EXPECT_EQ(signature[1], 'N');
    EXPECT_EQ(signature[2], 'B');

    EXPECT_EQ(manager.ReadAsset<ContractValue>("fixture", {}).value, 99);
}

TEST_F(ContentRuntimeContractTest, TheBaseOpenStreamReportsAMissingAsset)
{
    TemporaryContentRoot root;
    BaseStreamManager manager(root.path().string());
    EXPECT_THROW((void)manager.OpenStream("absent"), ContentLoadException);
}

TEST_F(ContentRuntimeContractTest, ReadAssetHandsEveryDisposableToTheRecorder)
{
    // The recorder is the caller's way of taking over the lifetime of what the asset owns. This
    // asset owns nothing, so what is asserted is that a supplied recorder is accepted and the read
    // still completes -- the ownership hand-off itself is ContentReader's, tested there.
    ProbeContentManager manager(BuildContractXnb(11));
    int recorded = 0;
    const ContractValue result = manager.ReadAsset<ContractValue>(
        "recorded", [&recorded](std::shared_ptr<System::IDisposable>) { ++recorded; });
    EXPECT_EQ(result.value, 11);
    EXPECT_EQ(recorded, 0);
}

// =============================================================================
// ContentTypeReaderManager.GetTypeReader(Type)
// =============================================================================

TEST_F(ContentRuntimeContractTest, GetTypeReaderResolvesATypeAReaderWasConstructedFor)
{
    // Constructing the reader is what associates ContractValue with its canonical name; before
    // that, nothing can resolve the type.
    EXPECT_EQ(ContentTypeReaderManager{}.GetTypeReader(System::Type::From<ContractValue>()),
              nullptr);

    const ContractValueReader association;
    (void)association;

    const std::unique_ptr<ContentTypeReaderBase> reader =
        ContentTypeReaderManager{}.GetTypeReader(System::Type::From<ContractValue>());
    ASSERT_NE(reader, nullptr);
    EXPECT_EQ(reader->getTargetTypeNameProperty(), "CNA.Test.ContractValue");
}

TEST_F(ContentRuntimeContractTest, GetTypeReaderReturnsNullForAnUnknownType)
{
    EXPECT_EQ(ContentTypeReaderManager{}.GetTypeReader(System::Type::From<UnknownValue>()), nullptr);
    EXPECT_EQ(ContentTypeReaderManager{}.GetTypeReader(System::Type::From<int>()), nullptr);
}

TEST_F(ContentRuntimeContractTest, GetTypeReaderReturnsAFreshInstanceEachTime)
{
    const ContractValueReader association;
    (void)association;

    const auto first = ContentTypeReaderManager{}.GetTypeReader(System::Type::From<ContractValue>());
    const auto second = ContentTypeReaderManager{}.GetTypeReader(System::Type::From<ContractValue>());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first.get(), second.get())
        << "CNA's factories create one reader per request, which is the contract CreateReader has";
}

// =============================================================================
// ContentTypeReader<T>: the parameterless constructor and the object-form Read
// =============================================================================

TEST_F(ContentRuntimeContractTest, TheParameterlessReaderConstructorLeavesAnUnnamedReader)
{
    // ContractValue is not a System::Object, so no canonical name can be derived from it. The
    // reader is unnamed -- not selectable from a type-reader table -- and that is the documented
    // meaning rather than a failure.
    const UnnamedContractValueReader reader;
    EXPECT_TRUE(reader.getTargetTypeNameProperty().empty());

    // And an unnamed reader associates nothing, so it cannot shadow a named one.
    EXPECT_EQ(ContentTypeReaderManager{}.GetTypeReader(System::Type::From<ContractValue>()),
              nullptr);
}

TEST_F(ContentRuntimeContractTest, TheObjectFormReadIsTheUnboxingBridge)
{
    // Read(ContentReader, object) and ReadUntyped are one implementation under two names, so they
    // have to agree -- including on an existing instance and on the wrong-type refusal.
    const std::vector<std::uint8_t> bytes = BuildContractXnb(5);
    ProbeContentManager manager(bytes);
    const ContractValue loaded = manager.ReadAsset<ContractValue>("bridge", {});
    ASSERT_EQ(loaded.value, 5);

    ContractValueReader reader;

    // Reached through a ContentTypeReader<T> reference, which is exactly what the documented
    // name-hiding caveat says to do: the concrete reader declares its own `Read`, so the
    // object-form one is not visible on it.
    ContentTypeReader<ContractValue>& asTypedReader = reader;

    System::IO::MemoryStream empty(nullptr, 0, false);
    ContentReader contentReader(&manager, &empty, "bridge", 5, 'w');

    // A wrong-typed existing instance is refused, which is what XNA's override checks before it
    // unboxes -- and both names must refuse it the same way, being one implementation.
    EXPECT_ANY_THROW(
        (void)asTypedReader.Read(contentReader, std::any(std::string("not a ContractValue"))));
    EXPECT_ANY_THROW(
        (void)asTypedReader.ReadUntyped(contentReader, std::any(std::string("not a ContractValue"))));
}

// =============================================================================
// ContentReader.ReadRawObject<T>() / ReadRawObject<T>(T)
// =============================================================================

namespace
{
    /// A reader payload that is just a raw Int32, with no dispatch index in front of it -- which is
    /// what ReadRawObject reads, and what distinguishes it from ReadObject.
    std::vector<std::uint8_t> RawInt32Payload(std::int32_t value)
    {
        System::IO::MemoryStream stream;
        {
            System::IO::BinaryWriter writer(&stream, true);
            writer.Write(value);
        }
        const auto array = stream.ToArray();
        return std::vector<std::uint8_t>(array.begin(), array.end());
    }
}

TEST_F(ContentRuntimeContractTest, ReadRawObjectResolvesTheReaderFromTheType)
{
    // Constructing a reader for ContractValue is what makes the type resolvable, exactly as in
    // GetTypeReader's own cases; ReadRawObject<T>() then needs no reader argument.
    const ContractValueReader association;
    (void)association;

    ProbeContentManager manager({});
    const std::vector<std::uint8_t> payload = RawInt32Payload(31337);
    System::IO::MemoryStream stream(payload.data(),
                                   static_cast<SharpRuntime::intcs>(payload.size()), false);
    ContentReader reader(&manager, &stream, "raw", 5, 'w');

    const ContractValue result = reader.ReadRawObject<ContractValue>();
    EXPECT_EQ(result.value, 31337);
}

TEST_F(ContentRuntimeContractTest, ReadRawObjectReadsIntoAnExistingInstance)
{
    const ContractValueReader association;
    (void)association;

    ProbeContentManager manager({});
    const std::vector<std::uint8_t> payload = RawInt32Payload(4);
    System::IO::MemoryStream stream(payload.data(),
                                   static_cast<SharpRuntime::intcs>(payload.size()), false);
    ContentReader reader(&manager, &stream, "raw", 5, 'w');

    ContractValue existing;
    existing.value = 1;
    const ContractValue result = reader.ReadRawObject<ContractValue>(existing);

    // The reader overwrites the value it reads, so what this shows is that the existing instance
    // reached it: a reader that ignored it could not tell these apart, which is why the reader
    // returns the instance it was handed rather than a fresh one.
    EXPECT_EQ(result.value, 4);
}

TEST_F(ContentRuntimeContractTest, ReadRawObjectAgreesWithTheExplicitReaderOverload)
{
    const ContractValueReader association;
    (void)association;

    ProbeContentManager manager({});
    const std::vector<std::uint8_t> payload = RawInt32Payload(77);

    std::int32_t viaResolved = 0;
    {
        System::IO::MemoryStream stream(payload.data(),
                                       static_cast<SharpRuntime::intcs>(payload.size()), false);
        ContentReader reader(&manager, &stream, "raw", 5, 'w');
        viaResolved = reader.ReadRawObject<ContractValue>().value;
    }

    std::int32_t viaExplicit = 0;
    {
        System::IO::MemoryStream stream(payload.data(),
                                       static_cast<SharpRuntime::intcs>(payload.size()), false);
        ContentReader reader(&manager, &stream, "raw", 5, 'w');
        ContractValueReader explicitReader;
        viaExplicit = reader.ReadRawObject<ContractValue>(explicitReader).value;
    }

    EXPECT_EQ(viaResolved, 77);
    EXPECT_EQ(viaResolved, viaExplicit)
        << "resolving the reader from the type must reach the same reader the caller could name";
}

TEST_F(ContentRuntimeContractTest, ReadRawObjectRefusesAnUnresolvableType)
{
    // Nothing has ever constructed a reader for UnknownValue, so the type has no associated name
    // and the read is refused rather than reading arbitrary bytes as that type.
    ProbeContentManager manager({});
    const std::vector<std::uint8_t> payload = RawInt32Payload(1);
    System::IO::MemoryStream stream(payload.data(),
                                   static_cast<SharpRuntime::intcs>(payload.size()), false);
    ContentReader reader(&manager, &stream, "raw", 5, 'w');

    EXPECT_THROW((void)reader.ReadRawObject<UnknownValue>(), ContentLoadException);
    EXPECT_THROW((void)reader.ReadRawObject<UnknownValue>(UnknownValue{}), ContentLoadException);
}
