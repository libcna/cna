// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-002/003/004/005: the .xnb container writer -- primitive
// encodings, the header, the type-writer table, shared resources, determinism and the refusals
// that keep the writer from producing a file CNA's own reader would reject.

#include <algorithm>
#include <any>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbByteWriter.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace CNA::Content::Xnb;
using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    /** @brief Holds the streams a decoded container needs alive while it is being read. */
    class DecodedXnb
    {
    public:
        explicit DecodedXnb(const std::vector<std::uint8_t>& file)
            : file_(file)
        {
            System::IO::MemoryStream headerStream(file_.data(),
                                                  static_cast<std::int32_t>(file_.size()));
            System::IO::BinaryReader headerReader(&headerStream, true);
            header_ = CNA::Internal::Xnb::ParseXnbHeader(headerReader, "test");
            bodyStream_ = std::make_unique<System::IO::MemoryStream>(
                file_.data() + 10, static_cast<std::int32_t>(file_.size() - 10u));
            reader_ = std::make_unique<ContentReader>(nullptr, bodyStream_.get(), "test",
                                                      header_.version, header_.platform);
        }

        [[nodiscard]] const CNA::Internal::Xnb::XnbHeader& Header() const { return header_; }
        [[nodiscard]] ContentReader& Reader() { return *reader_; }

    private:
        std::vector<std::uint8_t> file_;
        CNA::Internal::Xnb::XnbHeader header_{};
        std::unique_ptr<System::IO::MemoryStream> bodyStream_;
        std::unique_ptr<ContentReader> reader_;
    };

    class XnbWriterTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            RegisterBuiltInXnbTypeWriters(registry_);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        template <typename T>
        [[nodiscard]] std::vector<std::uint8_t> WriteRoot(const T& value)
        {
            return WriteXnbFile(registry_, options_, value);
        }

        template <typename T>
        [[nodiscard]] T RoundTrip(const T& value)
        {
            DecodedXnb decoded(WriteRoot(value));
            return decoded.Reader().ReadAsset<T>();
        }

        XnbTypeWriterRegistry registry_;
        XnbFileOptions options_{};
    };

    /** @brief Reads the exact bytes Write7BitEncodedInt() produced for @p value. */
    [[nodiscard]] std::vector<std::uint8_t> Encode7Bit(const std::int32_t value)
    {
        XnbByteWriter writer;
        writer.Write7BitEncodedInt(value);
        return writer.Take();
    }
}

// -- XnbByteWriter primitives --

TEST(XnbByteWriterTest, IntegersAreLittleEndianRegardlessOfHost)
{
    XnbByteWriter writer;
    writer.WriteUInt16(0x1234u);
    writer.WriteUInt32(0x89ABCDEFu);
    writer.WriteUInt64(0x0102030405060708ull);
    const std::vector<std::uint8_t> expected{
        0x34, 0x12,
        0xEF, 0xCD, 0xAB, 0x89,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    EXPECT_EQ(writer.Take(), expected);
}

TEST(XnbByteWriterTest, SevenBitEncodingMatchesTheDocumentedBoundaries)
{
    EXPECT_EQ(Encode7Bit(0), (std::vector<std::uint8_t>{0x00}));
    EXPECT_EQ(Encode7Bit(1), (std::vector<std::uint8_t>{0x01}));
    EXPECT_EQ(Encode7Bit(0x7F), (std::vector<std::uint8_t>{0x7F}));
    EXPECT_EQ(Encode7Bit(0x80), (std::vector<std::uint8_t>{0x80, 0x01}));
    EXPECT_EQ(Encode7Bit(0x3FFF), (std::vector<std::uint8_t>{0xFF, 0x7F}));
    EXPECT_EQ(Encode7Bit(0x4000), (std::vector<std::uint8_t>{0x80, 0x80, 0x01}));
    EXPECT_EQ(Encode7Bit(0x1FFFFF), (std::vector<std::uint8_t>{0xFF, 0xFF, 0x7F}));
    EXPECT_EQ(Encode7Bit(0x200000), (std::vector<std::uint8_t>{0x80, 0x80, 0x80, 0x01}));
    EXPECT_EQ(Encode7Bit(0x0FFFFFFF), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0x7F}));
    EXPECT_EQ(Encode7Bit(0x7FFFFFFF), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0xFF, 0x07}));
    // A negative value is encoded from its unsigned bit pattern and always occupies five bytes,
    // exactly as .NET's own writer does.
    EXPECT_EQ(Encode7Bit(-1), (std::vector<std::uint8_t>{0xFF, 0xFF, 0xFF, 0xFF, 0x0F}));
}

TEST(XnbByteWriterTest, SevenBitEncodingRoundTripsThroughTheRuntimeReader)
{
    const std::vector<std::int32_t> values{0,          1,          127,        128,
                                           16383,      16384,      2097151,    2097152,
                                           268435455,  2147483647, -1,
                                           std::numeric_limits<std::int32_t>::min()};
    for (const std::int32_t value : values)
    {
        std::vector<std::uint8_t> bytes = Encode7Bit(value);
        System::IO::MemoryStream stream(bytes.data(), static_cast<std::int32_t>(bytes.size()));
        System::IO::BinaryReader reader(&stream, true);
        EXPECT_EQ(reader.Read7BitEncodedInt(), value) << value;
    }
}

TEST(XnbByteWriterTest, StringsCarryASevenBitUtf8ByteCountAndNoTerminator)
{
    XnbByteWriter writer;
    writer.WriteString("");
    writer.WriteString("abc");
    writer.WriteString("\xC5\xBEluv");  // "žluv": a two-byte code point followed by three ASCII
    const std::vector<std::uint8_t> bytes = writer.Take();
    const std::vector<std::uint8_t> expected{
        0x00,
        0x03, 'a', 'b', 'c',
        0x05, 0xC5, 0xBE, 'l', 'u', 'v'};
    EXPECT_EQ(bytes, expected);
}

TEST(XnbByteWriterTest, CharsAreUtf8EncodedAndUnpairedSurrogatesAreRefused)
{
    XnbByteWriter writer;
    writer.WriteChar(u'A');
    writer.WriteChar(u'ž');   // two UTF-8 bytes
    writer.WriteChar(u'中');   // three UTF-8 bytes
    EXPECT_EQ(writer.Take(),
              (std::vector<std::uint8_t>{'A', 0xC5, 0xBE, 0xE4, 0xB8, 0xAD}));

    XnbByteWriter surrogate;
    EXPECT_THROW(surrogate.WriteChar(static_cast<char16_t>(0xD800)), XnbWriteException);
}

TEST(XnbByteWriterTest, OversizedStringsAndPayloadsAreRefusedBeforeAllocation)
{
    XnbWriteLimits limits;
    limits.maxStringBytes = 8;
    limits.maxPayloadBytes = 4;
    XnbByteWriter writer(limits);
    EXPECT_THROW(writer.WriteString(std::string(9u, 'x')), XnbWriteException);
    const std::vector<std::uint8_t> payload(5u, 0u);
    EXPECT_THROW(writer.WriteBytes(payload), XnbWriteException);
}

TEST(XnbByteWriterTest, PatchingOutsideTheBufferIsRefused)
{
    XnbByteWriter writer;
    writer.WriteUInt32(0u);
    EXPECT_NO_THROW(writer.PatchUInt32(0u, 0x11223344u));
    EXPECT_EQ(writer.View()[0], 0x44);
    EXPECT_THROW(writer.PatchUInt32(1u, 0u), XnbWriteException);
}

// -- Container header --

TEST_F(XnbWriterTest, TheHeaderCarriesTheMagicPlatformVersionFlagsAndExactTotalSize)
{
    const std::vector<std::uint8_t> file = WriteRoot<std::int32_t>(42);

    ASSERT_GE(file.size(), 10u);
    EXPECT_EQ(file[0], 'X');
    EXPECT_EQ(file[1], 'N');
    EXPECT_EQ(file[2], 'B');
    EXPECT_EQ(file[3], 'w');
    EXPECT_EQ(file[4], 5);
    EXPECT_EQ(file[5], 0);   // Reach, uncompressed

    const std::uint32_t declared = static_cast<std::uint32_t>(file[6]) |
                                   (static_cast<std::uint32_t>(file[7]) << 8u) |
                                   (static_cast<std::uint32_t>(file[8]) << 16u) |
                                   (static_cast<std::uint32_t>(file[9]) << 24u);
    EXPECT_EQ(declared, file.size());
}

TEST_F(XnbWriterTest, TheHiDefProfileAndVersionFourAreEncodedInTheHeader)
{
    options_.profile = XnbGraphicsProfile::HiDef;
    options_.version = 4;
    const std::vector<std::uint8_t> file = WriteRoot<std::int32_t>(7);
    EXPECT_EQ(file[4], 4);
    EXPECT_EQ(file[5] & 0x01u, 0x01u);
    EXPECT_EQ(file[5] & 0x80u, 0x00u);
}

TEST_F(XnbWriterTest, EveryWritablePlatformProducesAHeaderTheReaderAccepts)
{
    for (const XnbTargetPlatform platform : {
             XnbTargetPlatform::Windows, XnbTargetPlatform::WindowsPhone,
             XnbTargetPlatform::DesktopGL, XnbTargetPlatform::MacOSX, XnbTargetPlatform::Linux,
             XnbTargetPlatform::iOS, XnbTargetPlatform::Android})
    {
        options_.platform = platform;
        DecodedXnb decoded(WriteRoot<std::int32_t>(3));
        EXPECT_EQ(decoded.Header().platform, XnbPlatformByte(platform));
        EXPECT_EQ(decoded.Header().compression, CNA::Internal::Xnb::XnbCompression::None);
    }
}

TEST_F(XnbWriterTest, AnUnwritableContainerDescriptionIsRefusedBeforeAnyByteIsProduced)
{
    options_.version = 3;
    EXPECT_THROW(WriteRoot<std::int32_t>(1), XnbWriteException);

    options_ = XnbFileOptions{};
    options_.compression = static_cast<XnbWriteCompression>(99);
    EXPECT_THROW(WriteRoot<std::int32_t>(1), XnbWriteException);

    options_ = XnbFileOptions{};
    options_.platform = static_cast<XnbTargetPlatform>('x');
    EXPECT_THROW(WriteRoot<std::int32_t>(1), XnbWriteException);
}

TEST(XnbTargetPlatformTest, StablePlatformSpellingsRoundTrip)
{
    for (const char* name : {"windows", "windowsphone", "desktopgl", "macosx", "linux", "ios",
                             "android"})
    {
        EXPECT_STREQ(XnbTargetPlatformName(ParseXnbTargetPlatform(name)), name);
    }
    EXPECT_THROW((void)ParseXnbTargetPlatform("xbox360"), XnbWriteException);
    EXPECT_THROW((void)ParseXnbTargetPlatform("Windows"), XnbWriteException);
}

// -- Type-writer table --

TEST_F(XnbWriterTest, TheTypeTableRecordsFirstUseOrderAndDeduplicates)
{
    RegisterXnbListWriter(registry_, XnbTypeKey<std::string>::Name());
    XnbWriter writer(registry_, options_);

    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<std::string>::Name();
    list.elements = {std::any(std::string("a")), std::any(std::string("b"))};
    const std::vector<std::uint8_t> file =
        writer.WriteAsset(XnbListTypeName(XnbTypeKey<std::string>::Name()), std::any(list));
    ASSERT_FALSE(file.empty());

    // The list reader is entry 1 because it is dispatched to first; the string reader is entry 2
    // and appears once, not once per element.
    ASSERT_EQ(writer.TypeReaderNames().size(), 2u);
    EXPECT_EQ(writer.TypeReaderNames()[0],
              XnbListReaderName(XnbTypeKey<std::string>::Name()));
    EXPECT_EQ(writer.TypeReaderNames()[1], "Microsoft.Xna.Framework.Content.StringReader");
}

TEST_F(XnbWriterTest, AnUnregisteredTypeFailsWithItsOwnNameInTheMessage)
{
    XnbWriter writer(registry_, options_);
    try
    {
        (void)writer.WriteAsset("MyGame.Content.LevelData", std::any(std::int32_t{1}));
        FAIL() << "expected a write failure";
    }
    catch (const XnbWriteException& error)
    {
        EXPECT_NE(std::string(error.what()).find("MyGame.Content.LevelData"), std::string::npos);
    }
}

TEST_F(XnbWriterTest, RegisteringTheSameTargetTypeTwiceIsRefused)
{
    EXPECT_THROW(RegisterBuiltInXnbTypeWriters(registry_), XnbWriteException);
}

TEST_F(XnbWriterTest, AFrozenRegistryRefusesFurtherRegistration)
{
    registry_.Freeze();
    EXPECT_TRUE(registry_.IsFrozen());
    EXPECT_THROW(RegisterXnbEnumWriter(registry_, "MyGame.Content.Mode"), XnbWriteException);
}

// -- Object dispatch, null and shared resources --

TEST_F(XnbWriterTest, AnEmptyAnyIsWrittenAsTheNullReference)
{
    // A bare null cannot be a root asset, so the encoding is exercised through a list element.
    RegisterXnbListWriter(registry_, XnbTypeKey<std::string>::Name());
    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<std::string>::Name();
    list.elements = {std::any{}, std::any(std::string("present"))};
    const std::vector<std::uint8_t> file = WriteXnbFile(
        registry_, options_, XnbListTypeName(XnbTypeKey<std::string>::Name()), std::any(list));

    DecodedXnb decoded(file);
    decoded.Reader().InitializeTypeReaders();
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 1);   // the root list
    EXPECT_EQ(decoded.Reader().ReadUInt32(), 2u);
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 0);   // the null element
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 2);   // the string reader
    EXPECT_EQ(decoded.Reader().ReadString(), "present");
}

TEST_F(XnbWriterTest, AValueTypeRefusesToBeWrittenAsNull)
{
    RegisterXnbListWriter(registry_, XnbTypeKey<std::int32_t>::Name());
    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<std::int32_t>::Name();
    list.elements = {std::any{}};
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbListTypeName(XnbTypeKey<std::int32_t>::Name()),
                                    std::any(list)),
                 XnbWriteException);
}

TEST_F(XnbWriterTest, SharedResourcesAreSerializedAfterTheRootAndReferencedByIndex)
{
    RegisterXnbListWriter(registry_, XnbTypeKey<std::string>::Name());
    XnbWriter writer(registry_, options_);

    const std::int32_t first =
        writer.RegisterSharedResource("one", XnbTypeKey<std::string>::Name(),
                                      std::any(std::string("shared-one")));
    const std::int32_t second =
        writer.RegisterSharedResource("two", XnbTypeKey<std::string>::Name(),
                                      std::any(std::string("shared-two")));
    const std::int32_t firstAgain =
        writer.RegisterSharedResource("one", XnbTypeKey<std::string>::Name(),
                                      std::any(std::string("shared-one")));

    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
    EXPECT_EQ(firstAgain, 1) << "the same key must map to the same resource";

    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<std::string>::Name();
    list.elements = {std::any(std::string("root"))};
    const std::vector<std::uint8_t> file =
        writer.WriteAsset(XnbListTypeName(XnbTypeKey<std::string>::Name()), std::any(list));

    DecodedXnb decoded(file);
    decoded.Reader().InitializeTypeReaders();
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 1);       // the root list's type identifier
    EXPECT_EQ(decoded.Reader().ReadUInt32(), 1u);              // one element
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 2);       // the string reader's identifier
    EXPECT_EQ(decoded.Reader().ReadString(), "root");
    // Both shared resources follow, in registration order, each in polymorphic form.
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 2);
    EXPECT_EQ(decoded.Reader().ReadString(), "shared-one");
    EXPECT_EQ(decoded.Reader().Read7BitEncodedInt(), 2);
    EXPECT_EQ(decoded.Reader().ReadString(), "shared-two");
}

TEST_F(XnbWriterTest, ReusingASharedResourceKeyForAnotherTypeIsRefused)
{
    XnbWriter writer(registry_, options_);
    (void)writer.RegisterSharedResource("k", XnbTypeKey<std::string>::Name(),
                                        std::any(std::string("v")));
    EXPECT_THROW(writer.RegisterSharedResource("k", XnbTypeKey<std::int32_t>::Name(),
                                               std::any(std::int32_t{1})),
                 XnbWriteException);
    EXPECT_THROW(writer.RegisterSharedResource("", XnbTypeKey<std::string>::Name(),
                                               std::any(std::string("v"))),
                 XnbWriteException);
}

TEST_F(XnbWriterTest, AnUnregisteredSharedResourceIdentifierIsRefused)
{
    XnbWriter writer(registry_, options_);
    EXPECT_NO_THROW(writer.WriteSharedResourceReference(0));   // the format's null reference
    EXPECT_THROW(writer.WriteSharedResourceReference(1), XnbWriteException);
    EXPECT_THROW(writer.WriteSharedResourceReference(-1), XnbWriteException);
}

TEST_F(XnbWriterTest, ExternalReferencesRejectAnExtensionOrABackslash)
{
    XnbWriter writer(registry_, options_);
    EXPECT_NO_THROW(writer.WriteExternalReference("textures/wall"));
    EXPECT_NO_THROW(writer.WriteExternalReference(""));
    EXPECT_THROW(writer.WriteExternalReference("textures/wall.xnb"), XnbWriteException);
    EXPECT_THROW(writer.WriteExternalReference("textures\\wall"), XnbWriteException);
}

TEST_F(XnbWriterTest, AWriterProducesExactlyOneFile)
{
    XnbWriter writer(registry_, options_);
    EXPECT_NO_THROW((void)writer.WriteAsset(XnbTypeKey<std::int32_t>::Name(),
                                            std::any(std::int32_t{1})));
    EXPECT_THROW((void)writer.WriteAsset(XnbTypeKey<std::int32_t>::Name(),
                                         std::any(std::int32_t{1})),
                 XnbWriteException);
}

// -- Limits --

TEST_F(XnbWriterTest, AnOversizedCollectionIsRefusedRatherThanWritten)
{
    XnbWriteLimits limits;
    limits.maxCollectionElementCount = 2;
    XnbWriter writer(registry_, options_, limits);
    EXPECT_NO_THROW(writer.WriteCollectionCount(2u, "test"));
    EXPECT_THROW(writer.WriteCollectionCount(3u, "test"), XnbWriteException);
}

TEST_F(XnbWriterTest, AnOverDeepGraphIsRefusedRatherThanOverflowingTheStack)
{
    // A list that contains itself as its own element type would recurse forever; the depth guard
    // turns that into a diagnosable refusal.
    RegisterXnbListWriter(registry_, XnbTypeKey<std::string>::Name());
    const std::string listType = XnbListTypeName(XnbTypeKey<std::string>::Name());
    RegisterXnbListWriter(registry_, listType);

    XnbWriteLimits limits;
    limits.maxObjectNestingDepth = 3;

    XnbBoxedList inner;
    inner.elementTypeName = XnbTypeKey<std::string>::Name();
    inner.elements = {std::any(std::string("x"))};

    XnbBoxedList outer;
    outer.elementTypeName = listType;
    outer.elements = {std::any(inner), std::any(inner)};

    XnbBoxedList outermost;
    outermost.elementTypeName = XnbListTypeName(listType);
    RegisterXnbListWriter(registry_, XnbListTypeName(listType));
    outermost.elements = {std::any(outer)};

    XnbWriter writer(registry_, options_, limits);
    EXPECT_THROW((void)writer.WriteAsset(XnbListTypeName(XnbListTypeName(listType)),
                                         std::any(outermost)),
                 XnbWriteException);
}

TEST_F(XnbWriterTest, AnOversizedTypeTableIsRefused)
{
    XnbWriteLimits limits;
    limits.maxTypeWriterCount = 1;
    RegisterXnbListWriter(registry_, XnbTypeKey<std::string>::Name());

    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<std::string>::Name();
    list.elements = {std::any(std::string("a"))};

    XnbWriter writer(registry_, options_, limits);
    EXPECT_THROW((void)writer.WriteAsset(XnbListTypeName(XnbTypeKey<std::string>::Name()),
                                         std::any(list)),
                 XnbWriteException);
}

// -- Determinism --

TEST_F(XnbWriterTest, TheSameGraphProducesByteIdenticalOutput)
{
    RegisterXnbListWriter(registry_, XnbTypeKey<Vector3>::Name());
    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<Vector3>::Name();
    list.elements = {std::any(Vector3(1.0f, 2.0f, 3.0f)), std::any(Vector3(4.0f, 5.0f, 6.0f))};

    const std::string type = XnbListTypeName(XnbTypeKey<Vector3>::Name());
    const std::vector<std::uint8_t> first =
        WriteXnbFile(registry_, options_, type, std::any(list));
    const std::vector<std::uint8_t> second =
        WriteXnbFile(registry_, options_, type, std::any(list));
    EXPECT_EQ(first, second);
}
