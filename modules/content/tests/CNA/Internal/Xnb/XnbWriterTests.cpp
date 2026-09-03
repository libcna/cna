// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-10/11/12/14/15/16/17/20/21/22/40/41/45: the `.xnb` serializer
// core and its built-in primitive, framework and collection writers.
//
// The strongest test in this file is GoldenXna40ListOfStringsIsByteIdentical: CNA writes the same
// List<string> the genuine Microsoft XNA 4.0 Content Pipeline wrote into the committed fixture and
// the two files must be byte for byte the same. Everything else here is round-tripped through
// CNA's own independent reader, which shares no code with the writer.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "CNA/Internal/Xnb/XnbByteWriter.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbReaderIdentity.hpp"
#include "CNA/Internal/Xnb/XnbTypeName.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"
#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/CurveContentTypeReader.hpp"
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace CNA::Internal::Xnb;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    /** @brief Reads a committed fixture, failing the test when it is missing. */
    std::vector<std::uint8_t> ReadFixture(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        EXPECT_TRUE(stream.good()) << "missing fixture: " << path.string();
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /**
     * @brief Loads a complete in-memory `.xnb` file through CNA's own reader.
     *
     * Deliberately drives `ContentReader` rather than reusing any writer helper, so a round-trip
     * failure cannot be masked by shared code.
     */
    class LoadedXnb
    {
    public:
        explicit LoadedXnb(std::vector<std::uint8_t> file) : file_(std::move(file))
        {
            headerStream_ = std::make_unique<System::IO::MemoryStream>(
                file_.data(), static_cast<std::int32_t>(file_.size()));
            System::IO::BinaryReader headerReader(headerStream_.get(), true);
            header_ = ParseXnbHeader(headerReader, "written");
            bodyStream_ = std::make_unique<System::IO::MemoryStream>(
                file_.data() + 10, static_cast<std::int32_t>(file_.size() - 10u));
            reader_ = std::make_unique<ContentReader>(
                nullptr, bodyStream_.get(), "written", header_.version, header_.platform);
        }

        [[nodiscard]] const XnbHeader& Header() const { return header_; }

        [[nodiscard]] ContentReader& Reader() const { return *reader_; }

        template<typename T>
        [[nodiscard]] T ReadAsset()
        {
            return reader_->ReadAsset<T>();
        }

    private:
        std::vector<std::uint8_t> file_;
        std::unique_ptr<System::IO::MemoryStream> headerStream_;
        std::unique_ptr<System::IO::MemoryStream> bodyStream_;
        std::unique_ptr<ContentReader> reader_;
        XnbHeader header_{};
    };

    class XnbWriterTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            RegisterPrimitiveXnbReaders();
            RegisterMathXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

// -- XnbByteWriter (XNAP-10) -------------------------------------------------------------------

TEST(XnbByteWriterTest, IntegersAreLittleEndianRegardlessOfHostOrder)
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

TEST(XnbByteWriterTest, FloatingPointUsesIeee754BitPatterns)
{
    XnbByteWriter writer;
    writer.WriteSingle(1.0f);
    writer.WriteDouble(-2.0);
    const std::vector<std::uint8_t> expected{
        0x00, 0x00, 0x80, 0x3F,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0};
    EXPECT_EQ(writer.Take(), expected);
}

TEST(XnbByteWriterTest, SevenBitEncodedIntegersMatchTheReadersOwnDecoding)
{
    for (const std::int32_t value : {0, 1, 127, 128, 300, 16383, 16384, 1 << 28})
    {
        XnbByteWriter writer;
        writer.Write7BitEncodedInt(value);
        const std::vector<std::uint8_t> bytes = writer.Take();
        System::IO::MemoryStream stream(bytes.data(), static_cast<std::int32_t>(bytes.size()));
        System::IO::BinaryReader reader(&stream, true);
        EXPECT_EQ(reader.Read7BitEncodedInt(), value) << "value " << value;
    }
}

TEST(XnbByteWriterTest, NegativeSevenBitEncodedIntegersUseTheFiveByteDotNetForm)
{
    XnbByteWriter writer;
    writer.Write7BitEncodedInt(-1);
    const std::vector<std::uint8_t> bytes = writer.Take();
    ASSERT_EQ(bytes.size(), 5u);
    System::IO::MemoryStream stream(bytes.data(), 5);
    System::IO::BinaryReader reader(&stream, true);
    EXPECT_EQ(reader.Read7BitEncodedInt(), -1);
}

TEST(XnbByteWriterTest, StringsCarryASevenBitEncodedUtf8ByteLength)
{
    XnbByteWriter writer;
    writer.WriteString("abc");
    const std::vector<std::uint8_t> expected{0x03, 'a', 'b', 'c'};
    EXPECT_EQ(writer.Take(), expected);
}

TEST(XnbByteWriterTest, MalformedUtf8IsRefusedRatherThanWritten)
{
    XnbByteWriter writer;
    EXPECT_THROW(writer.WriteString(std::string("\xFF\xFE", 2)), XnbWriteException);
}

TEST(XnbByteWriterTest, CharsAreUtf8EncodedAndSurrogatesAreRefused)
{
    XnbByteWriter writer;
    writer.WriteChar(u'A');
    writer.WriteChar(u'é');
    writer.WriteChar(u'€');
    const std::vector<std::uint8_t> expected{'A', 0xC3, 0xA9, 0xE2, 0x82, 0xAC};
    EXPECT_EQ(writer.Take(), expected);

    XnbByteWriter surrogate;
    EXPECT_THROW(surrogate.WriteChar(static_cast<SharpRuntime::charcs>(0xD800)),
                 XnbWriteException);
}

TEST(XnbByteWriterTest, StringLengthLimitIsEnforced)
{
    XnbWriteLimits limits;
    limits.maxStringBytes = 4;
    XnbByteWriter writer(limits);
    EXPECT_THROW(writer.WriteString("abcde"), XnbWriteException);
}

// -- reader identities (XNAP-17) ---------------------------------------------------------------

TEST(XnbReaderIdentityTest, Xna40StyleQualifiesOnlyNonCoreReadersAndEveryGenericArgument)
{
    XnbReaderIdentity listOfStrings;
    listOfStrings.readerBaseName = "Microsoft.Xna.Framework.Content.ListReader`1";
    listOfStrings.targetBaseName = "System.Collections.Generic.List`1";
    listOfStrings.targetAssembly = XnbAssembly::Mscorlib;
    listOfStrings.genericArguments = {XnbBuiltInReaderIdentity<std::string>()};

    EXPECT_EQ(FormatXnbReaderName(listOfStrings, XnbReaderNameStyle::Xna40),
              "Microsoft.Xna.Framework.Content.ListReader`1[[System.String, mscorlib, "
              "Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089]]");
    EXPECT_EQ(FormatXnbReaderName(listOfStrings, XnbReaderNameStyle::Portable),
              "Microsoft.Xna.Framework.Content.ListReader`1[[System.String]]");
}

TEST(XnbReaderIdentityTest, EveryEmittedNameNormalizesBackToItsCanonicalRegistryKey)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    for (const std::string& canonical : registry.RegisteredReaderNames())
    {
        EXPECT_EQ(NormalizeXnbTypeReaderName(canonical), canonical);
    }
}

TEST(XnbReaderIdentityTest, Xna40NamesNormalizeToTheSameKeyTheRuntimeRegistryUses)
{
    const XnbReaderIdentity texture = XnbTexture2DReaderIdentity();
    EXPECT_EQ(NormalizeXnbTypeReaderName(FormatXnbReaderName(texture, XnbReaderNameStyle::Xna40)),
              XnbCanonicalReaderName(texture));
    EXPECT_EQ(XnbCanonicalReaderName(texture),
              "Microsoft.Xna.Framework.Content.Texture2DReader");
}

// -- registry (XNAP-15) ------------------------------------------------------------------------

TEST(XnbTypeWriterRegistryTest, ASecondWriterForOneTypeIsRefused)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInPrimitiveXnbWriters(registry);
    EXPECT_THROW(RegisterBuiltInPrimitiveXnbWriters(registry), XnbWriteException);
}

TEST(XnbTypeWriterRegistryTest, AFrozenRegistryRefusesFurtherRegistration)
{
    XnbTypeWriterRegistry registry;
    registry.Freeze();
    EXPECT_TRUE(registry.IsFrozen());
    EXPECT_THROW(RegisterBuiltInPrimitiveXnbWriters(registry), XnbWriteException);
}

TEST(XnbTypeWriterRegistryTest, AnUnregisteredTypeNamesTheProblemInsteadOfCrashing)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInPrimitiveXnbWriters(registry);
    registry.Freeze();
    try
    {
        (void)WriteXnbAsset(Microsoft::Xna::Framework::Vector3{1.0f, 2.0f, 3.0f}, {}, "probe",
                            registry);
        FAIL() << "an unregistered root type must be refused";
    }
    catch (const XnbWriteException& error)
    {
        EXPECT_NE(std::string(error.what()).find("no registered XNB type writer"),
                  std::string::npos)
            << error.what();
    }
}

// -- container (XNAP-12/14/16) -----------------------------------------------------------------

TEST_F(XnbWriterTest, TheHeaderCarriesThePlatformVersionProfileAndExactTotalLength)
{
    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Windows;
    options.graphicsProfile = XnbGraphicsProfile::HiDef;
    const std::vector<std::uint8_t> file =
        WriteXnbAsset(std::int32_t{7}, options, "seven");

    ASSERT_GE(file.size(), 10u);
    EXPECT_EQ(file[0], 'X');
    EXPECT_EQ(file[1], 'N');
    EXPECT_EQ(file[2], 'B');
    EXPECT_EQ(file[3], 'w');
    EXPECT_EQ(file[4], 5u);
    EXPECT_EQ(file[5], 0x01u);
    const std::uint32_t declared = static_cast<std::uint32_t>(file[6]) |
                                   (static_cast<std::uint32_t>(file[7]) << 8) |
                                   (static_cast<std::uint32_t>(file[8]) << 16) |
                                   (static_cast<std::uint32_t>(file[9]) << 24);
    EXPECT_EQ(declared, file.size());
}

TEST_F(XnbWriterTest, ReachIsTheDefaultProfileBecauseItLoadsUnderBothProfiles)
{
    const std::vector<std::uint8_t> file = WriteXnbAsset(std::int32_t{1}, {}, "one");
    EXPECT_EQ(file[5], 0x00u);
}

TEST_F(XnbWriterTest, ExtendedEcosystemPlatformsAreWrittenButNotAsXna40Targets)
{
    EXPECT_TRUE(IsXna40TargetPlatform(XnbTargetPlatform::Windows));
    EXPECT_TRUE(IsXna40TargetPlatform(XnbTargetPlatform::WindowsPhone));
    EXPECT_TRUE(IsXna40TargetPlatform(XnbTargetPlatform::Xbox360));
    EXPECT_FALSE(IsXna40TargetPlatform(XnbTargetPlatform::DesktopGL));
    EXPECT_FALSE(IsXna40TargetPlatform(XnbTargetPlatform::Android));

    XnbFileOptions options;
    options.platform = XnbTargetPlatform::DesktopGL;
    const std::vector<std::uint8_t> file = WriteXnbAsset(std::int32_t{1}, options, "one");
    EXPECT_EQ(file[3], 'd');
}

TEST_F(XnbWriterTest, Lz4CompressionIsRefusedForAnXna40TargetPlatform)
{
    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Windows;
    options.compression = XnbOutputCompression::Lz4;
    EXPECT_THROW(ValidateXnbFileOptions(options), XnbWriteException);
}

TEST_F(XnbWriterTest, CompressedOutputFailsWithAPlanReferenceRatherThanSilentlyWritingRawBytes)
{
    XnbFileOptions options;
    options.compression = XnbOutputCompression::Lzx;
    try
    {
        (void)WriteXnbAsset(std::int32_t{1}, options, "one");
        FAIL() << "compressed output is not implemented and must not silently succeed";
    }
    catch (const XnbWriteException& error)
    {
        EXPECT_NE(std::string(error.what()).find("XNAP-81"), std::string::npos) << error.what();
    }
}

// -- golden byte equality against genuine Microsoft XNA 4.0 output (XNAP-41) --------------------

TEST_F(XnbWriterTest, GoldenXna40ListOfStringsIsByteIdentical)
{
    // tests/assets/xnb/xna40/windows/uncompressed/ContentManifestListStrings.xnb was produced by
    // the official XNA 4.0 BuildContent task (see its provenance manifest). Reproducing it byte
    // for byte proves CNA's container header, type-reader table spelling, 7-bit encoding, object
    // dispatch protocol and string encoding all match Microsoft's own Content Pipeline for this
    // asset -- the single strongest interoperability signal available without an XNA runtime.
    const std::vector<std::string> items{
        "Characters\\Bear",   "Characters\\Cardinal",     "Characters\\Dog",
        "Characters\\Duck",   "clock",                    "flashlight",
        "heart",              "heart_grey",               "Font",
        "Content\\Characters\\Duck.png",
        "Content\\CopiedFile1.txt", "Content\\CopiedFile2.txt",
        "Content\\CopiedFile3.txt", "Content\\CopiedFile4.txt"};

    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Windows;
    options.version = XnbContainerVersion::Xna40;
    options.graphicsProfile = XnbGraphicsProfile::HiDef;
    options.readerNameStyle = XnbReaderNameStyle::Xna40;

    const std::vector<std::uint8_t> written = WriteXnbAsset(items, options, "manifest");
    const std::vector<std::uint8_t> expected = ReadFixture(
        "tests/assets/xnb/xna40/windows/uncompressed/ContentManifestListStrings.xnb");

    ASSERT_EQ(written.size(), expected.size());
    EXPECT_EQ(written, expected);
}

// -- round-trips through CNA's own reader (XNAP-40) --------------------------------------------

TEST_F(XnbWriterTest, PrimitiveRootsRoundTripThroughTheReader)
{
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(std::int32_t{-42})).ReadAsset<std::int32_t>(), -42);
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(std::uint8_t{200})).ReadAsset<std::uint8_t>(), 200);
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(true)).ReadAsset<bool>(), true);
    EXPECT_FLOAT_EQ(LoadedXnb(WriteXnbAsset(1.5f)).ReadAsset<float>(), 1.5f);
    EXPECT_DOUBLE_EQ(LoadedXnb(WriteXnbAsset(2.25)).ReadAsset<double>(), 2.25);
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(std::int64_t{-9000000000LL})).ReadAsset<std::int64_t>(),
              -9000000000LL);
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(std::string("hello"))).ReadAsset<std::string>(), "hello");
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(u'é')).ReadAsset<SharpRuntime::charcs>(), u'é');
}

TEST_F(XnbWriterTest, FrameworkValueTypesRoundTripThroughTheReader)
{
    using namespace Microsoft::Xna::Framework;

    const Vector3 vector{1.0f, -2.0f, 3.5f};
    const Vector3 readVector = LoadedXnb(WriteXnbAsset(vector)).ReadAsset<Vector3>();
    EXPECT_FLOAT_EQ(readVector.X, vector.X);
    EXPECT_FLOAT_EQ(readVector.Y, vector.Y);
    EXPECT_FLOAT_EQ(readVector.Z, vector.Z);

    const Rectangle rectangle{3, 4, 5, 6};
    const Rectangle readRectangle = LoadedXnb(WriteXnbAsset(rectangle)).ReadAsset<Rectangle>();
    EXPECT_EQ(readRectangle, rectangle);

    const Matrix matrix = Matrix::CreateTranslation(Vector3{7.0f, 8.0f, 9.0f});
    const Matrix readMatrix = LoadedXnb(WriteXnbAsset(matrix)).ReadAsset<Matrix>();
    EXPECT_FLOAT_EQ(readMatrix.M41, 7.0f);
    EXPECT_FLOAT_EQ(readMatrix.M42, 8.0f);
    EXPECT_FLOAT_EQ(readMatrix.M43, 9.0f);
    EXPECT_FLOAT_EQ(readMatrix.M11, 1.0f);

    const Color color(10, 20, 30, 40);
    const Color readColor = LoadedXnb(WriteXnbAsset(color)).ReadAsset<Color>();
    EXPECT_EQ(readColor, color);
}

TEST_F(XnbWriterTest, ACurveRoundTripsWithEveryKeyField)
{
    using Microsoft::Xna::Framework::Curve;
    using Microsoft::Xna::Framework::CurveContinuity;
    using Microsoft::Xna::Framework::CurveKey;
    using Microsoft::Xna::Framework::CurveLoopType;

    Curve curve;
    curve.setPreLoopProperty(CurveLoopType::Cycle);
    curve.setPostLoopProperty(CurveLoopType::Oscillate);
    curve.getKeysProperty().Add(CurveKey(0.0f, 1.0f, 2.0f, 3.0f, CurveContinuity::Smooth));
    curve.getKeysProperty().Add(CurveKey(1.0f, -1.0f, 0.5f, 0.25f, CurveContinuity::Step));

    CNA::Internal::Xnb::RegisterCurveXnbReader();
    const Curve read = LoadedXnb(WriteXnbAsset(curve, {}, "curve")).ReadAsset<Curve>();

    EXPECT_EQ(read.getPreLoopProperty(), CurveLoopType::Cycle);
    EXPECT_EQ(read.getPostLoopProperty(), CurveLoopType::Oscillate);
    ASSERT_EQ(read.getKeysProperty().getCountProperty(), 2);
    EXPECT_FLOAT_EQ(read.getKeysProperty()[1].getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(read.getKeysProperty()[1].getValueProperty(), -1.0f);
    EXPECT_FLOAT_EQ(read.getKeysProperty()[1].getTangentInProperty(), 0.5f);
    EXPECT_FLOAT_EQ(read.getKeysProperty()[1].getTangentOutProperty(), 0.25f);
    EXPECT_EQ(read.getKeysProperty()[1].getContinuityProperty(), CurveContinuity::Step);
}

TEST_F(XnbWriterTest, CollectionsRoundTripIncludingAnEmptyOne)
{
    const std::vector<std::string> strings{"one", "two", "three"};
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(strings)).ReadAsset<std::vector<std::string>>(), strings);

    const std::vector<std::int32_t> integers{1, 2, 3, 4, 5};
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(integers)).ReadAsset<std::vector<std::int32_t>>(),
              integers);

    const std::vector<std::string> empty;
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(empty)).ReadAsset<std::vector<std::string>>(), empty);
}

TEST_F(XnbWriterTest, ADictionaryRoundTripsAndIsWrittenInDeterministicKeyOrder)
{
    const std::unordered_map<std::string, std::int32_t> map{
        {"gamma", 3}, {"alpha", 1}, {"beta", 2}};

    const std::vector<std::uint8_t> first = WriteXnbAsset(map, {}, "map");
    const std::vector<std::uint8_t> second = WriteXnbAsset(map, {}, "map");
    EXPECT_EQ(first, second);

    const auto read =
        LoadedXnb(first).ReadAsset<std::unordered_map<std::string, std::int32_t>>();
    EXPECT_EQ(read, map);
}

TEST_F(XnbWriterTest, ValueTypedListElementsCarryNoDispatchIndexButStillInternTheirReader)
{
    // Default.xnb's own type-reader table proves the shape: a List<Rectangle> is immediately
    // followed by RectangleReader even though no Rectangle element ever emits a dispatch index.
    const std::vector<Microsoft::Xna::Framework::Rectangle> rectangles{{0, 0, 1, 1}, {2, 3, 4, 5}};
    const std::vector<std::uint8_t> file = WriteXnbAsset(rectangles, {}, "rects");

    System::IO::MemoryStream stream(file.data() + 10,
                                    static_cast<std::int32_t>(file.size() - 10u));
    System::IO::BinaryReader reader(&stream, true);
    ASSERT_EQ(reader.Read7BitEncodedInt(), 2);
    const std::string listName = reader.ReadString();
    EXPECT_EQ(reader.ReadInt32(), 0);
    const std::string elementName = reader.ReadString();
    EXPECT_EQ(reader.ReadInt32(), 0);
    EXPECT_EQ(NormalizeXnbTypeReaderName(listName),
              "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Rectangle]]");
    EXPECT_EQ(NormalizeXnbTypeReaderName(elementName),
              "Microsoft.Xna.Framework.Content.RectangleReader");
}

// -- determinism (XNAP-44) ---------------------------------------------------------------------

TEST_F(XnbWriterTest, IdenticalInputsProduceIdenticalBytes)
{
    const std::vector<std::string> items{"a", "b", "c"};
    EXPECT_EQ(WriteXnbAsset(items, {}, "items"), WriteXnbAsset(items, {}, "items"));
    // The diagnostic asset name is never serialized, so it cannot influence the bytes.
    EXPECT_EQ(WriteXnbAsset(items, {}, "items"), WriteXnbAsset(items, {}, "a-different-name"));
}

// -- limits and refusals (XNAP-45) -------------------------------------------------------------

TEST_F(XnbWriterTest, ACollectionAboveTheConfiguredLimitIsRefused)
{
    XnbFileOptions options;
    options.limits.maxCollectionElementCount = 2;
    const std::vector<std::int32_t> values{1, 2, 3};
    EXPECT_THROW((void)WriteXnbAsset(values, options, "values"), XnbWriteException);
}

TEST_F(XnbWriterTest, AFileAboveTheConfiguredSizeLimitIsRefused)
{
    XnbFileOptions options;
    options.limits.maxFileSize = 16;
    const std::vector<std::string> values(100, "padding");
    EXPECT_THROW((void)WriteXnbAsset(values, options, "values"), XnbWriteException);
}

TEST_F(XnbWriterTest, AnEscapingExternalReferenceIsRefused)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    registry.Freeze();
    XnbWriter writer(registry, {}, "effect");
    EXPECT_THROW(writer.WriteExternalReference("../../outside"), XnbWriteException);
    EXPECT_THROW(writer.WriteExternalReference("/absolute"), XnbWriteException);
    EXPECT_NO_THROW(writer.WriteExternalReference("Textures/wood"));
    EXPECT_NO_THROW(writer.WriteExternalReference(""));
}

TEST_F(XnbWriterTest, AnUnissuedSharedResourceReferenceIsRefused)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    registry.Freeze();
    XnbWriter writer(registry, {}, "model");
    EXPECT_THROW(writer.WriteSharedResourceReference(1), XnbWriteException);
    EXPECT_NO_THROW(writer.WriteSharedResourceReference(0));
}

TEST_F(XnbWriterTest, FinishingTwiceIsRefusedRatherThanProducingASecondTruncatedFile)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    registry.Freeze();
    XnbWriter writer(registry, {}, "twice");
    writer.WriteObject(std::int32_t{1});
    EXPECT_NO_THROW((void)writer.Finish());
    EXPECT_THROW((void)writer.Finish(), XnbWriteException);
}
