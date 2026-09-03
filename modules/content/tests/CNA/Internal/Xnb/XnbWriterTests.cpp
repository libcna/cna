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
#include <any>
#include <filesystem>
#include <map>
#include <fstream>
#include <memory>
#include <optional>
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
#include "CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/EffectMaterialContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbCompressionWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "CNA/Internal/Xnb/XnbDecompression.hpp"
#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Content/ReflectiveTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace CNA::Internal::Xnb;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    /** @brief A self-cleaning temporary directory for tests that need a real file. */
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnb_writer_file_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

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
            RegisterDecimalDateTimeXnbReaders();
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

// -- LZ4 output (XNAP-80) ----------------------------------------------------------------------

TEST(XnbLz4BlockTest, EveryBlockThisEncoderProducesDecodesBackToItsInput)
{
    // Round-tripped through CNA's own LZ4 *decoder*, which was written from the same published
    // block format but shares no code with the encoder. The cases below are chosen to walk the
    // format's own boundaries rather than to look plausible: an empty payload, one shorter than a
    // legal match can exist in, one exactly at the twelve-byte search margin, a highly repetitive
    // payload that exercises long matches and match-length extension bytes, and an incompressible
    // one that exercises literal-length extension bytes.
    std::vector<std::vector<std::uint8_t>> payloads;
    payloads.push_back({});
    payloads.push_back({1u});
    payloads.push_back(std::vector<std::uint8_t>(12u, 0x5Au));
    payloads.push_back(std::vector<std::uint8_t>(13u, 0x5Au));
    payloads.push_back(std::vector<std::uint8_t>(4096u, 0xA5u));

    std::vector<std::uint8_t> repetitive;
    for (int index = 0; index < 2000; ++index)
    {
        const char* word = "the quick brown fox ";
        repetitive.insert(repetitive.end(), word, word + 20);
    }
    payloads.push_back(repetitive);

    std::vector<std::uint8_t> noisy(8192u);
    std::uint32_t state = 0xDEADBEEFu;
    for (std::uint8_t& byte : noisy)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    payloads.push_back(noisy);

    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        const std::vector<std::uint8_t>& payload = payloads[index];
        const std::vector<std::uint8_t> block = CompressXnbLz4Block(payload);
        ASSERT_FALSE(block.empty()) << "payload " << index;
        const std::vector<std::uint8_t> restored = DecompressXnbLz4Payload(
            block.data(), static_cast<std::int32_t>(block.size()),
            static_cast<std::int32_t>(payload.size()), "block");
        EXPECT_EQ(restored, payload) << "payload " << index;
    }

    // The repetitive payload has to actually get smaller, or the encoder is emitting literals and
    // calling it compression.
    EXPECT_LT(CompressXnbLz4Block(repetitive).size(), repetitive.size() / 4u);
    EXPECT_EQ(CompressXnbLz4Block(repetitive), CompressXnbLz4Block(repetitive));
}

TEST_F(XnbWriterTest, AnLz4CompressedFileRoundTripsThroughTheReader)
{
    ScratchDirectory scratch("lz4");
    XnbFileOptions options;
    // LZ4 is an extended-ecosystem format, so an XNA 4.0 target platform refuses it outright and
    // this file has to name one of the others.
    options.platform = XnbTargetPlatform::DesktopGL;
    options.compression = XnbOutputCompression::Lz4;

    XnbTextureData texture;
    texture.kind = XnbTextureKind::Texture2D;
    texture.surfaceFormat = Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;
    texture.width = 32u;
    texture.height = 32u;
    texture.mipCount = 1u;
    texture.levels = {std::vector<std::uint8_t>(32u * 32u * 4u, 0x7Fu)};

    const std::vector<std::uint8_t> compressed =
        WriteXnbAsset(XnbTexture2DContent{texture}, options, "tile");
    ASSERT_GE(compressed.size(), 14u);
    EXPECT_EQ(compressed[5] & 0x40u, 0x40u) << "the LZ4 flag bit must be set";

    XnbFileOptions plain = options;
    plain.compression = XnbOutputCompression::None;
    const std::vector<std::uint8_t> uncompressed =
        WriteXnbAsset(XnbTexture2DContent{texture}, plain, "tile");
    EXPECT_LT(compressed.size(), uncompressed.size() / 4u)
        << "a flat texture must actually get much smaller";

    const std::filesystem::path path = scratch.Path() / "tile.xnb";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(compressed.data()),
                     static_cast<std::streamsize>(compressed.size()));
    }
    // The whole point: CNA's reader takes the compressed file apart without being told anything
    // beyond the flag byte, and gets the same pixels back.
    const XnbCanonicalAsset asset = DecodeXnbCanonicalAsset(path);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.Texture2DReader");
    EXPECT_EQ(std::get<XnbTextureData>(asset.value).levels, texture.levels);
}

TEST_F(XnbWriterTest, LzxOutputFailsWithAPlanReferenceRatherThanSilentlyWritingRawBytes)
{
    // LZX is the one Microsoft XNA 4.0 itself produced, and CNA has only the decoder for it.
    // Setting the flag and writing raw bytes under it would produce a file that no LZX decoder
    // can read, so the refusal names the task instead.
    XnbFileOptions options;
    options.compression = XnbOutputCompression::Lzx;
    try
    {
        (void)WriteXnbAsset(std::int32_t{1}, options, "one");
        FAIL() << "LZX output is not implemented and must not silently succeed";
    }
    catch (const XnbWriteException& error)
    {
        EXPECT_NE(std::string(error.what()).find("XNAP-81"), std::string::npos) << error.what();
    }
}

// -- EffectMaterial and its polymorphic parameter table (XNAP-29/XNAP-2B) ---------------------

TEST_F(XnbWriterTest, AnEffectParameterTableRoundTripsEveryValueTypeItCanHold)
{
    using namespace Microsoft::Xna::Framework;

    // The table is Dictionary<String, Object>: every value carries its own dispatch index, so a
    // reader that has never seen this file still knows what each entry is. Reading it back as
    // std::any and checking the concrete type of each entry is what proves the index is right --
    // a wrong index would decode as some other type rather than fail.
    RegisterEffectMaterialXnbReaders();

    XnbEffectParameterTable table;
    table.values.emplace("Alpha", 0.75f);
    table.values.emplace("Enabled", true);
    table.values.emplace("Passes", std::int32_t{3});
    table.values.emplace("Offset", Vector2{1.0f, 2.0f});
    table.values.emplace("Diffuse", Vector3{0.25f, 0.5f, 0.75f});
    table.values.emplace("Tint", Vector4{1.0f, 2.0f, 3.0f, 4.0f});
    table.values.emplace("World", Matrix::CreateTranslation(Vector3{5.0f, 6.0f, 7.0f}));
    table.values.emplace("Spin", Quaternion{0.1f, 0.2f, 0.3f, 0.4f});

    const std::map<std::string, std::any> read =
        LoadedXnb(WriteXnbAsset(table)).ReadAsset<std::map<std::string, std::any>>();
    ASSERT_EQ(read.size(), table.values.size());

    EXPECT_FLOAT_EQ(std::any_cast<float>(read.at("Alpha")), 0.75f);
    EXPECT_TRUE(std::any_cast<bool>(read.at("Enabled")));
    EXPECT_EQ(std::any_cast<std::int32_t>(read.at("Passes")), 3);
    EXPECT_FLOAT_EQ(std::any_cast<Vector2>(read.at("Offset")).Y, 2.0f);
    EXPECT_FLOAT_EQ(std::any_cast<Vector3>(read.at("Diffuse")).Z, 0.75f);
    EXPECT_FLOAT_EQ(std::any_cast<Vector4>(read.at("Tint")).W, 4.0f);
    EXPECT_FLOAT_EQ(std::any_cast<Matrix>(read.at("World")).M43, 7.0f);
    EXPECT_FLOAT_EQ(std::any_cast<Quaternion>(read.at("Spin")).X, 0.1f);
}

TEST_F(XnbWriterTest, AnEffectMaterialInternsBothItsOwnReaderAndItsParameterReaders)
{
    using namespace Microsoft::Xna::Framework;

    XnbEffectMaterialData material;
    material.effectReference = "Effects/Water";
    material.parameters.values.emplace("Alpha", 0.5f);
    material.parameters.values.emplace("NormalMap",
                                       XnbExternalAssetReference{"Textures/WaterNormal"});

    // The material's own effect reference is inline and carries no dispatch index; the parameter
    // table and every value in it do. A reader that resolves external references needs a
    // ContentManager and a device, so this test asserts on the type-reader table rather than
    // loading the graph -- which is precisely the part a wrong identity would break.
    const std::vector<std::uint8_t> written = WriteXnbAsset(material, {}, "water");
    const std::string text(written.begin(), written.end());
    for (const char* expected : {"EffectMaterialReader",
                                 "DictionaryReader`2[[System.String",
                                 "System.Object",
                                 "ExternalReferenceReader",
                                 "SingleReader"})
    {
        EXPECT_NE(text.find(expected), std::string::npos) << expected << " is not in the table";
    }
    EXPECT_NE(text.find("Effects/Water"), std::string::npos);
    EXPECT_NE(text.find("Textures/WaterNormal"), std::string::npos);
}

TEST_F(XnbWriterTest, AnEffectMaterialWithNoEffectReferenceIsRefused)
{
    XnbEffectMaterialData material;
    material.parameters.values.emplace("Alpha", 0.5f);
    EXPECT_THROW((void)WriteXnbAsset(material, {}, "broken"), XnbWriteException);
}

TEST_F(XnbWriterTest, AnExternalReferenceThatEscapesTheContentRootIsRefusedAsAnObject)
{
    // The inline form already refuses these; the boxed form has to refuse them for the same
    // reason, or the check is only as strong as the field the reference happens to sit in.
    EXPECT_THROW((void)WriteXnbAsset(XnbExternalAssetReference{"../outside"}, {}, "escape"),
                 XnbWriteException);
    EXPECT_THROW((void)WriteXnbAsset(XnbExternalAssetReference{"/absolute"}, {}, "absolute"),
                 XnbWriteException);
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

TEST_F(XnbWriterTest, GoldenMonoGameTexture2DIsByteIdentical)
{
    // plans/plan_xnapipeline.md XNAP-42. The golden test above proves the container and the
    // collection protocol against Microsoft's own output; this one proves an *asset* writer's
    // field layout against a second, independently produced file -- MonoGame's smallest real
    // Texture2D fixture, a single opaque white pixel, from its own test corpus.
    XnbTextureData texture;
    texture.kind = XnbTextureKind::Texture2D;
    texture.surfaceFormat = Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;
    texture.width = 1u;
    texture.height = 1u;
    texture.depth = 1u;
    texture.faceCount = 1u;
    texture.mipCount = 1u;
    texture.levels = {{0xFFu, 0xFFu, 0xFFu, 0xFFu}};

    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Windows;
    options.version = XnbContainerVersion::Xna40;
    options.graphicsProfile = XnbGraphicsProfile::Reach;
    options.readerNameStyle = XnbReaderNameStyle::Xna40;

    const std::vector<std::uint8_t> written =
        WriteXnbAsset(XnbTexture2DContent{texture}, options, "white-1");
    const std::vector<std::uint8_t> expected = ReadFixture(
        "tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb");
    ASSERT_EQ(written.size(), expected.size());
    EXPECT_EQ(written, expected);
}

TEST_F(XnbWriterTest, GoldenMonoGameSoundEffectIsByteIdentical)
{
    // plans/plan_xnapipeline.md XNAP-42, third golden. A SoundEffect exercises a different part of
    // the format from a texture: a length-prefixed WAVEFORMATEX block whose fields are written
    // individually, then the samples, then the loop region and duration. The sample bytes are
    // taken from the fixture itself -- the claim under test is the *framing*, not that CNA can
    // regenerate somebody else's sine wave.
    const std::vector<std::uint8_t> expected = ReadFixture(
        "tests/assets/xnb/monogame/windows/uncompressed/audio/tone_mono_44khz_16bit.xnb");
    ASSERT_GT(expected.size(), 44100u);

    XnbSoundEffectData sound;
    sound.formatTag = 1u;
    sound.channels = 1u;
    sound.sampleRate = 44100u;
    sound.averageBytesPerSecond = 88200u;
    sound.blockAlign = 2u;
    sound.bitsPerSample = 16u;
    sound.samples.assign(expected.end() - 44100 - 12, expected.end() - 12);
    sound.loopStart = 0;
    sound.loopLength = 22050;
    sound.storedDurationMs = 500u;

    XnbFileOptions options;
    options.platform = XnbTargetPlatform::DesktopGL;
    options.version = XnbContainerVersion::Xna40;
    options.graphicsProfile = XnbGraphicsProfile::Reach;
    options.readerNameStyle = XnbReaderNameStyle::Xna40;

    const std::vector<std::uint8_t> written = WriteXnbAsset(sound, options, "tone");
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

TEST_F(XnbWriterTest, EveryRemainingFrameworkValueTypeRoundTripsThroughTheReader)
{
    // The types above cover the shapes the asset writers themselves use. These are the rest of
    // the registered value-type writers, each with its own reader on the other side, so that
    // "registered" and "verified" mean the same thing for all of them.
    using namespace Microsoft::Xna::Framework;

    const Vector2 vector2{1.5f, -2.5f};
    const Vector2 readVector2 = LoadedXnb(WriteXnbAsset(vector2)).ReadAsset<Vector2>();
    EXPECT_FLOAT_EQ(readVector2.X, vector2.X);
    EXPECT_FLOAT_EQ(readVector2.Y, vector2.Y);

    const Vector4 vector4{1.0f, 2.0f, 3.0f, 4.0f};
    const Vector4 readVector4 = LoadedXnb(WriteXnbAsset(vector4)).ReadAsset<Vector4>();
    EXPECT_FLOAT_EQ(readVector4.W, 4.0f);
    EXPECT_FLOAT_EQ(readVector4.Z, 3.0f);

    const Quaternion quaternion{0.1f, 0.2f, 0.3f, 0.4f};
    const Quaternion readQuaternion =
        LoadedXnb(WriteXnbAsset(quaternion)).ReadAsset<Quaternion>();
    EXPECT_FLOAT_EQ(readQuaternion.X, 0.1f);
    EXPECT_FLOAT_EQ(readQuaternion.W, 0.4f);

    const Point point{11, -22};
    const Point readPoint = LoadedXnb(WriteXnbAsset(point)).ReadAsset<Point>();
    EXPECT_EQ(readPoint.X, 11);
    EXPECT_EQ(readPoint.Y, -22);

    const Plane plane{Vector3{0.0f, 1.0f, 0.0f}, -5.0f};
    const Plane readPlane = LoadedXnb(WriteXnbAsset(plane)).ReadAsset<Plane>();
    EXPECT_FLOAT_EQ(readPlane.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(readPlane.D, -5.0f);

    const BoundingBox box{Vector3{-1.0f, -2.0f, -3.0f}, Vector3{4.0f, 5.0f, 6.0f}};
    const BoundingBox readBox = LoadedXnb(WriteXnbAsset(box)).ReadAsset<BoundingBox>();
    EXPECT_FLOAT_EQ(readBox.Min.X, -1.0f);
    EXPECT_FLOAT_EQ(readBox.Max.Z, 6.0f);

    const BoundingSphere sphere{Vector3{1.0f, 2.0f, 3.0f}, 7.5f};
    const BoundingSphere readSphere =
        LoadedXnb(WriteXnbAsset(sphere)).ReadAsset<BoundingSphere>();
    EXPECT_FLOAT_EQ(readSphere.Center.Y, 2.0f);
    EXPECT_FLOAT_EQ(readSphere.Radius, 7.5f);

    const Ray ray{Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}};
    const Ray readRay = LoadedXnb(WriteXnbAsset(ray)).ReadAsset<Ray>();
    EXPECT_FLOAT_EQ(readRay.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(readRay.Direction.Z, 1.0f);
}

TEST_F(XnbWriterTest, ABoundingFrustumRoundTripsAsItsMatrix)
{
    // BoundingFrustum is the one .NET *class* in the framework value-type group, so it is
    // serialized by reference: a nested element carries its own dispatch index. The payload is
    // the source Matrix alone -- the six planes and eight corners are recomputed by the
    // constructor on the reading side, which is what makes that the whole of the stored state.
    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    const Matrix view = Matrix::CreateLookAt(Vector3{0.0f, 0.0f, 5.0f}, Vector3::Zero,
                                             Vector3::Up);
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(1.0f, 1.25f, 1.0f, 100.0f);
    const BoundingFrustum frustum{view * projection};

    const BoundingFrustum read =
        LoadedXnb(WriteXnbAsset(frustum)).ReadAsset<BoundingFrustum>();

    const Matrix expected = frustum.getMatrixProperty();
    const Matrix actual = read.getMatrixProperty();
    EXPECT_FLOAT_EQ(actual.M11, expected.M11);
    EXPECT_FLOAT_EQ(actual.M22, expected.M22);
    EXPECT_FLOAT_EQ(actual.M33, expected.M33);
    EXPECT_FLOAT_EQ(actual.M34, expected.M34);
    EXPECT_FLOAT_EQ(actual.M43, expected.M43);

    // The derived state has to come back with it, otherwise "round-trips" would only mean the
    // matrix survived and not the object.
    EXPECT_FLOAT_EQ(read.getNearProperty().D, frustum.getNearProperty().D);
    EXPECT_FLOAT_EQ(read.getFarProperty().D, frustum.getFarProperty().D);
}

#if SHARP_RUNTIME_HAS_NATIVE_INT128
TEST_F(XnbWriterTest, ADecimalRoundTripsAllFourWordsIncludingScaleAndSign)
{
    // Four Int32 words in the order BinaryWriter.Write(decimal) emits them: lo, mid, hi, flags.
    // Scale and sign live in flags, so a value that exercises neither would pass while the word
    // was dropped; -12345.678 has both, and a large hi word proves the third is not truncated.
    const System::Decimal negativeWithScale(12345678, 0, 0, true, 3);
    const System::Decimal read =
        LoadedXnb(WriteXnbAsset(negativeWithScale)).ReadAsset<System::Decimal>();

    SharpRuntime::intcs lo = 0;
    SharpRuntime::intcs mid = 0;
    SharpRuntime::intcs hi = 0;
    SharpRuntime::intcs flags = 0;
    System::Decimal::GetBits(read, lo, mid, hi, flags);
    EXPECT_EQ(lo, 12345678);
    EXPECT_EQ(mid, 0);
    EXPECT_EQ(hi, 0);
    EXPECT_EQ((static_cast<std::uint32_t>(flags) >> 16) & 0xFFu, 3u);
    EXPECT_NE(static_cast<std::uint32_t>(flags) & 0x80000000u, 0u);
    EXPECT_TRUE(read == negativeWithScale);

    const System::Decimal wide(1, 2, 3, false, 0);
    const System::Decimal readWide = LoadedXnb(WriteXnbAsset(wide)).ReadAsset<System::Decimal>();
    System::Decimal::GetBits(readWide, lo, mid, hi, flags);
    EXPECT_EQ(lo, 1);
    EXPECT_EQ(mid, 2);
    EXPECT_EQ(hi, 3);
    EXPECT_EQ(flags, 0);

    EXPECT_EQ(LoadedXnb(WriteXnbAsset(System::Decimal(0)))
                  .ReadAsset<System::Decimal>()
                  .ToString(),
              System::Decimal(0).ToString());
}
#endif

TEST_F(XnbWriterTest, TimeSpanAndDateTimeRoundTripThroughTheReader)
{
    // Both are stored as a single Int64 of 100-nanosecond ticks. DateTime's top two bits carry
    // .NET's DateTimeKind, which System::DateTime does not model, so the writer emits Unspecified
    // and the reader must read back exactly the tick count that went in.
    const System::TimeSpan span = System::TimeSpan::FromSeconds(1.5);
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(span)).ReadAsset<System::TimeSpan>().getTicksProperty(), span.getTicksProperty());

    const System::DateTime moment(630822816000000000LL);
    EXPECT_EQ(LoadedXnb(WriteXnbAsset(moment)).ReadAsset<System::DateTime>().getTicksProperty(),
              moment.getTicksProperty());
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

// -- untrusted-input hardening (XNAP-85) -------------------------------------------------------

TEST_F(XnbWriterTest, ANonPositiveWriteLimitIsRefusedRatherThanSilentlyDisablingTheCeiling)
{
    // Every limit is signed and every check that consults one widens it to std::size_t, so a
    // negative limit does not mean "small" -- it means "no limit at all". That is the opposite of
    // what a limit is for, and it has to fail loudly at the one place it enters the system.
    for (const auto& [name, mutate] :
         std::vector<std::pair<const char*, void (*)(XnbWriteLimits&)>>{
             {"maxFileSize", [](XnbWriteLimits& limits) { limits.maxFileSize = -1; }},
             {"maxPayloadSize", [](XnbWriteLimits& limits) { limits.maxPayloadSize = 0; }},
             {"maxStringBytes", [](XnbWriteLimits& limits) { limits.maxStringBytes = -5; }},
             {"maxTypeWriterCount", [](XnbWriteLimits& limits) { limits.maxTypeWriterCount = 0; }},
             {"maxSharedResourceCount",
              [](XnbWriteLimits& limits) { limits.maxSharedResourceCount = -1; }},
             {"maxCollectionElementCount",
              [](XnbWriteLimits& limits) { limits.maxCollectionElementCount = 0; }},
             {"maxObjectNestingDepth",
              [](XnbWriteLimits& limits) { limits.maxObjectNestingDepth = -3; }}})
    {
        XnbFileOptions options;
        mutate(options.limits);
        try
        {
            (void)WriteXnbAsset(std::int32_t{1}, options, "limits");
            FAIL() << name << " was accepted as a limit";
        }
        catch (const XnbWriteException& error)
        {
            EXPECT_NE(std::string(error.what()).find(name), std::string::npos) << error.what();
        }
    }

    // A file ceiling below its own header describes something that cannot exist.
    XnbFileOptions tiny;
    tiny.limits.maxFileSize = 8;
    EXPECT_THROW((void)WriteXnbAsset(std::int32_t{1}, tiny, "tiny"), XnbWriteException);
}

TEST_F(XnbWriterTest, TheAssemblyBufferIsCappedByTheFileCeilingRatherThanThePayloadCeiling)
{
    // The default payload ceiling is four times the file ceiling, so without this cap the writer
    // would allocate up to 256 MB assembling something the file ceiling was always going to
    // refuse. Failing while writing rather than after assembling is the whole point.
    XnbFileOptions options;
    options.limits.maxFileSize = 4096;
    options.limits.maxPayloadSize = 64 * 1024 * 1024;

    std::vector<std::string> many;
    for (int index = 0; index < 4096; ++index) { many.push_back("entry-" + std::to_string(index)); }
    try
    {
        (void)WriteXnbAsset(many, options, "oversized");
        FAIL() << "an asset larger than the file ceiling must be refused";
    }
    catch (const XnbWriteException& error)
    {
        EXPECT_NE(std::string(error.what()).find("payload"), std::string::npos) << error.what();
    }
}

TEST_F(XnbWriterTest, AnExternalReferenceCarryingAControlCharacterIsRefused)
{
    // A NUL in a path survives this format's length-prefixed strings intact and then truncates in
    // whatever consumes the path as a C string on the other side.
    EXPECT_THROW((void)WriteXnbAsset(XnbExternalAssetReference{std::string("Textures/a\0b", 12)},
                                     {}, "nul"),
                 XnbWriteException);
    EXPECT_THROW((void)WriteXnbAsset(XnbExternalAssetReference{"Textures/a\nb"}, {}, "newline"),
                 XnbWriteException);
}

// -- the custom-writer extension point (XNAP-92) -----------------------------------------------

namespace
{
    /** @brief A game-defined asset type the built-in registry knows nothing about. */
    struct WaypointList
    {
        std::vector<Microsoft::Xna::Framework::Vector3> points;
    };

    /** @brief A custom XNB type writer for it, registered the way a game would register one. */
    class WaypointListXnbWriter final : public XnbTypeWriter<WaypointList>
    {
    public:
        [[nodiscard]] XnbReaderIdentity ReaderIdentity() const override
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = "ExampleGame.Content.WaypointListReader";
            identity.readerAssembly = XnbAssembly::None;
            identity.targetBaseName = "ExampleGame.WaypointList";
            identity.targetAssembly = XnbAssembly::None;
            identity.evidence = XnbNameEvidence::DerivedRule;
            return identity;
        }

        [[nodiscard]] bool IsSerializedByReference() const noexcept override { return true; }

    protected:
        void Write(XnbWriter& output, const WaypointList& value) const override
        {
            output.RequireCollectionCount(value.points.size(), "WaypointListWriter");
            output.WriteInt32(static_cast<std::int32_t>(value.points.size()));
            for (const Microsoft::Xna::Framework::Vector3& point : value.points)
            {
                output.WriteVector3(point);
            }
        }
    };

    /** @brief The reader half, which is what makes the written file mean anything. */
    class WaypointListXnbReader final
        : public Microsoft::Xna::Framework::Content::ContentTypeReader<WaypointList>
    {
    public:
        WaypointListXnbReader()
            : Microsoft::Xna::Framework::Content::ContentTypeReader<WaypointList>(
                  "ExampleGame.WaypointList")
        {
        }

    protected:
        WaypointList Read(ContentReader& input,
                          std::optional<WaypointList> existingInstance) override
        {
            static_cast<void>(existingInstance);
            WaypointList result;
            const std::int32_t count = input.ReadInt32();
            input.CheckCollectionElementCount(count, getTargetTypeNameProperty());
            for (std::int32_t index = 0; index < count; ++index)
            {
                result.points.push_back(input.ReadVector3());
            }
            return result;
        }
    };
}

// -- Enum writer (XNAP-98) ---------------------------------------------------------------------

namespace
{
    /** @brief A game's own enum, in no XNA assembly -- the case with no qualifier at all. */
    enum class QuestState : std::int32_t
    {
        Unstarted = 0,
        Active = 1,
        Failed = -7,
    };
}

TEST_F(XnbWriterTest, AnEnumIsWrittenAsAnInt32AndReadBackByTheEnumReaderTheTableNames)
{
    using Microsoft::Xna::Framework::Content::EnumTypeReader;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    RegisterXnbEnumWriter<SurfaceFormat>(registry,
                                         "Microsoft.Xna.Framework.Graphics.SurfaceFormat",
                                         XnbAssembly::FrameworkGraphics);

    const std::string readerName = EnumTypeReader<SurfaceFormat>::CanonicalReaderName(
        "Microsoft.Xna.Framework.Graphics.SurfaceFormat");
    ContentTypeReaderManager::AddTypeCreator(readerName, [readerName] {
        return std::make_unique<EnumTypeReader<SurfaceFormat>>(
            "Microsoft.Xna.Framework.Graphics.SurfaceFormat");
    });

    const std::vector<std::uint8_t> written =
        WriteXnbAsset(SurfaceFormat::Dxt5, {}, "format", registry);

    // Four payload bytes: XNA's EnumReader<T> stores the underlying Int32 and nothing else.
    const std::string text(written.begin(), written.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.EnumReader`1"
                        "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat, "
                        "Microsoft.Xna.Framework.Graphics, Version=4.0.0.0"),
              std::string::npos)
        << "the enum reader has to reach the type table with its argument assembly-qualified";

    EXPECT_EQ(LoadedXnb(written).ReadAsset<SurfaceFormat>(), SurfaceFormat::Dxt5);
}

TEST_F(XnbWriterTest, AGameEnumRoundTripsIncludingANegativeValue)
{
    using Microsoft::Xna::Framework::Content::EnumTypeReader;

    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    RegisterXnbEnumWriter<QuestState>(registry, "ExampleGame.QuestState", XnbAssembly::None);

    ContentTypeReaderManager::AddTypeCreator(
        EnumTypeReader<QuestState>::CanonicalReaderName("ExampleGame.QuestState"),
        [] { return std::make_unique<EnumTypeReader<QuestState>>("ExampleGame.QuestState"); });

    // A negative value proves the underlying type is written signed rather than widened through
    // an unsigned cast, which a `Failed = -7` would survive only by accident.
    for (const QuestState value : {QuestState::Unstarted, QuestState::Active, QuestState::Failed})
    {
        const std::vector<std::uint8_t> written = WriteXnbAsset(value, {}, "quest", registry);
        EXPECT_EQ(LoadedXnb(written).ReadAsset<QuestState>(), value);
    }

    // With no assembly, nothing is qualified -- the same spelling both name styles produce.
    const std::vector<std::uint8_t> written =
        WriteXnbAsset(QuestState::Active, {}, "quest", registry);
    const std::string text(written.begin(), written.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.EnumReader`1[[ExampleGame.QuestState]]"),
              std::string::npos);
}

TEST(XnbEnumReaderIdentityTest, TheEnumIsTheReadersArgumentButIsNotItselfGeneric)
{
    const XnbReaderIdentity identity = XnbEnumReaderIdentity(
        "Microsoft.Xna.Framework.Graphics.SurfaceFormat", XnbAssembly::FrameworkGraphics);

    // The target type is the plain enum. Appending the reader's argument list to it a second time
    // would produce `SurfaceFormat[[SurfaceFormat]]`, which is what targetSharesGenericArguments
    // exists to prevent -- and what a `List<SurfaceFormat>` would otherwise write into its table.
    EXPECT_EQ(XnbTargetTypeName(identity), "Microsoft.Xna.Framework.Graphics.SurfaceFormat");
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.EnumReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(FormatXnbReaderName(identity, XnbReaderNameStyle::Portable),
              XnbCanonicalReaderName(identity));

    XnbReaderIdentity list;
    list.readerBaseName = "Microsoft.Xna.Framework.Content.ListReader`1";
    list.targetBaseName = "System.Collections.Generic.List`1";
    list.targetAssembly = XnbAssembly::Mscorlib;
    list.genericArguments = {identity};
    EXPECT_EQ(XnbCanonicalReaderName(list),
              "Microsoft.Xna.Framework.Content.ListReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
}

TEST(XnbEnumReaderIdentityTest, AnArrayNamesItsElementOnceRatherThanTwice)
{
    // The same defect the enum surfaced, in the array writer: `Int32[]` already spells its element
    // type inside targetBaseName, so appending the reader's argument list again would give
    // `System.Int32[][[System.Int32]]` wherever an array appears as a nested generic argument.
    XnbTypeWriterRegistry registry;
    const XnbArrayTypeWriter<std::int32_t> writer(XnbBuiltInReaderIdentity<std::int32_t>());
    EXPECT_EQ(XnbTargetTypeName(writer.ReaderIdentity()), "System.Int32[]");
    EXPECT_EQ(XnbCanonicalReaderName(writer.ReaderIdentity()),
              "Microsoft.Xna.Framework.Content.ArrayReader`1[[System.Int32]]");
}

TEST_F(XnbWriterTest, ListOfMatrixAndArrayOfVector3RoundTripThroughTheBuiltInRegistry)
{
    using namespace Microsoft::Xna::Framework;

    // plans/plan_xnapipeline.md XNAP-9D. Both of these instantiations already had readers in
    // CNA's runtime registry -- a real XNA `Model` names ArrayReader<Vector3> in its own type
    // table -- and neither had a writer, so the writer side was narrower than the reader side for
    // no reason. `XnbArray<T>` is how a registry keyed by C++ type tells `Vector3[]` apart from
    // `List<Vector3>`: both are std::vector<Vector3>.
    const std::vector<Matrix> matrices{
        Matrix::CreateTranslation(1.0f, 2.0f, 3.0f), Matrix::getIdentityProperty()};
    const std::vector<Matrix> readMatrices =
        LoadedXnb(WriteXnbAsset(matrices, {}, "transforms")).ReadAsset<std::vector<Matrix>>();
    ASSERT_EQ(readMatrices.size(), 2u);
    EXPECT_FLOAT_EQ(readMatrices[0].M41, 1.0f);
    EXPECT_FLOAT_EQ(readMatrices[0].M43, 3.0f);
    EXPECT_EQ(readMatrices[1], Matrix::getIdentityProperty());

    const XnbArray<Vector3> points{{Vector3{1.0f, 2.0f, 3.0f}, Vector3{-4.0f, 5.0f, 6.0f}}};
    const std::vector<std::uint8_t> file = WriteXnbAsset(points, {}, "points");
    const std::string text(file.begin(), file.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.ArrayReader`1"), std::string::npos);
    EXPECT_EQ(text.find("Microsoft.Xna.Framework.Content.ListReader`1"), std::string::npos);
    const std::vector<Vector3> readPoints = LoadedXnb(file).ReadAsset<std::vector<Vector3>>();
    ASSERT_EQ(readPoints.size(), 2u);
    EXPECT_FLOAT_EQ(readPoints[0].Z, 3.0f);
    EXPECT_FLOAT_EQ(readPoints[1].X, -4.0f);
}

TEST_F(XnbWriterTest, ANullableInstantiationRoundTripsThroughTheDocumentedExtensionPath)
{
    using namespace Microsoft::Xna::Framework;

    // XNAP-22/XNAP-9D: no `Nullable<T>` instantiation is registered by default, because no
    // built-in CNA reader resolves one -- a writer with no reader produces a file CNA itself
    // cannot load. Registering one is the documented extension point, and this is that path run
    // end to end rather than described: both halves registered by the consumer, both states of
    // the flag, and a real file between them.
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    registry.Register(std::make_shared<const XnbNullableTypeWriter<Vector3>>(
        XnbBuiltInReaderIdentity<Vector3>()));

    ContentTypeReaderManager::AddTypeCreator(
        "Microsoft.Xna.Framework.Content.NullableReader`1[[Microsoft.Xna.Framework.Vector3]]",
        []
        {
            return std::make_unique<NullableReader<Vector3>>(
                "System.Nullable`1[[Microsoft.Xna.Framework.Vector3]]",
                "Microsoft.Xna.Framework.Content.Vector3Reader");
        });

    const std::optional<Vector3> present = Vector3{7.0f, 8.0f, 9.0f};
    const std::optional<Vector3> read =
        LoadedXnb(WriteXnbAsset(present, {}, "spawn", registry))
            .ReadAsset<std::optional<Vector3>>();
    ASSERT_TRUE(read.has_value());
    EXPECT_FLOAT_EQ(read->Y, 8.0f);

    const std::optional<Vector3> absent;
    EXPECT_FALSE(LoadedXnb(WriteXnbAsset(absent, {}, "spawn", registry))
                     .ReadAsset<std::optional<Vector3>>()
                     .has_value());
}

TEST_F(XnbWriterTest, AnArrayTypedGenericArgumentSurvivesTheWholeRoundTrip)
{
    using namespace Microsoft::Xna::Framework;

    // XNAP-9C: `List<Int32[]>` is the shape that produced `System.Int32[][[System.Int32]]` before
    // targetSharesGenericArguments, and whose *reader* name -- which legitimately contains
    // `System.Int32[]` as a generic argument -- CNA's own type-name parser could not parse. Both
    // ends are exercised here at once: the writer spells the name, and the reader resolves it.
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    const XnbReaderIdentity arrayOfInt32 =
        XnbArrayTypeWriter<std::int32_t>(XnbBuiltInReaderIdentity<std::int32_t>()).ReaderIdentity();
    registry.Register(std::make_shared<const XnbArrayTypeWriter<std::int32_t>>(
        XnbBuiltInReaderIdentity<std::int32_t>()));
    registry.Register(
        std::make_shared<const XnbListTypeWriter<XnbArray<std::int32_t>>>(arrayOfInt32));

    ContentTypeReaderManager::AddTypeCreator(
        "Microsoft.Xna.Framework.Content.ArrayReader`1[[System.Int32]]",
        []
        {
            return std::make_unique<ArrayReader<std::int32_t>>(
                "System.Int32[]", "Microsoft.Xna.Framework.Content.Int32Reader");
        });
    ContentTypeReaderManager::AddTypeCreator(
        "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32[]]]",
        []
        {
            return std::make_unique<ListReader<std::vector<std::int32_t>>>(
                "System.Collections.Generic.List`1[[System.Int32[]]]",
                "Microsoft.Xna.Framework.Content.ArrayReader`1[[System.Int32]]");
        });

    const std::vector<XnbArray<std::int32_t>> rows{{{1, 2, 3}}, {{}}, {{-9}}};
    const std::vector<std::uint8_t> file = WriteXnbAsset(rows, {}, "rows", registry);
    const std::string text(file.begin(), file.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32[], mscorlib"),
              std::string::npos);
    EXPECT_EQ(text.find("System.Int32[][["), std::string::npos);

    const std::vector<std::vector<std::int32_t>> read =
        LoadedXnb(file).ReadAsset<std::vector<std::vector<std::int32_t>>>();
    ASSERT_EQ(read.size(), 3u);
    EXPECT_EQ(read[0], (std::vector<std::int32_t>{1, 2, 3}));
    EXPECT_TRUE(read[1].empty());
    EXPECT_EQ(read[2], (std::vector<std::int32_t>{-9}));
}

TEST_F(XnbWriterTest, AGameCanRegisterItsOwnTypeWriterAndReaderAndRoundTripThroughThem)
{
    // This is the documented extension point, exercised rather than described: a type the
    // built-in registry has never heard of, its own writer, its own reader, and a real file
    // between them. Nothing in CNA's own registry is touched.
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    registry.Register(std::make_shared<const WaypointListXnbWriter>());

    ContentTypeReaderManager::AddTypeCreator(
        "ExampleGame.Content.WaypointListReader",
        [] { return std::make_unique<WaypointListXnbReader>(); });

    WaypointList source;
    source.points = {{1.0f, 2.0f, 3.0f}, {-4.0f, 5.5f, 6.25f}};

    const std::vector<std::uint8_t> written = WriteXnbAsset(source, {}, "waypoints", registry);
    const std::string text(written.begin(), written.end());
    EXPECT_NE(text.find("ExampleGame.Content.WaypointListReader"), std::string::npos)
        << "the custom reader name has to reach the type table";

    const WaypointList read = LoadedXnb(written).ReadAsset<WaypointList>();
    ASSERT_EQ(read.points.size(), source.points.size());
    EXPECT_FLOAT_EQ(read.points[0].X, 1.0f);
    EXPECT_FLOAT_EQ(read.points[1].Z, 6.25f);

    // A custom writer gets the same ceilings and the same refusals the built-ins get, because it
    // writes through the same XnbWriter rather than around it.
    WaypointList huge;
    XnbFileOptions options;
    options.limits.maxCollectionElementCount = 1;
    huge.points = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    EXPECT_THROW((void)WriteXnbAsset(huge, options, "huge", registry), XnbWriteException);
}
