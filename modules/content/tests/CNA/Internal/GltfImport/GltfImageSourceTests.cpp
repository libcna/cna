// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-193 / GLTF-194 / GLTF-195 / GLTF-196: where a texture's pixels come from.
//
// §3.9.2 gives an image exactly three sources -- a `bufferView`, a `data:` URI, or a relative file
// URI -- and every one of them has to arrive as the same bytes. A file, a `.glb` and a
// self-contained `.gltf` are routinely three exports of one asset, so a reader that handles them
// differently makes the same model look different depending on how it was saved, which is
// attributed to the exporter rather than to the importer.
//
// The known-pixel PNG below is two texels wide, red then green. One texel would make a channel
// swap invisible; two distinct texels make both a channel swap and a row/column mix-up visible in
// the decoded result.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using namespace CNA::Internal::GltfImport;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;

namespace
{
    /// A 2x1 RGBA PNG: texel 0 opaque red, texel 1 opaque green.
    const char* kRedGreenPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAADklEQVR4nGP4z8DwHwQBEPgD/U6VwW8AAAAASUVORK5CYII=";
    constexpr std::size_t kRedGreenPngBytes = 71;

    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_image_source_"
                      + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    bool ParseFile(Parsed& out, const std::filesystem::path& path)
    {
        cgltf_options options{};
        const std::string pathString = path.string();
        if (cgltf_parse_file(&options, pathString.c_str(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, pathString.c_str()) == cgltf_result_success;
    }

    /// A document with one image, described by `imageJson`, and optionally a buffer holding the PNG
    /// so a `bufferView`-backed image has something to point at.
    std::string ImageDocument(const std::string& imageJson, bool withPngBuffer)
    {
        if (!withPngBuffer)
        {
            return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "images": [ )GLTF") + imageJson + R"GLTF( ]
})GLTF";
        }
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "images": [ )GLTF") + imageJson + R"GLTF( ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(kRedGreenPngBytes) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + kRedGreenPngBase64 +
               R"GLTF(" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": )GLTF" +
               std::to_string(kRedGreenPngBytes) + R"GLTF( } ]
})GLTF";
    }

    /// The PNG's own eight-byte signature. Asserting it is what distinguishes "some bytes arrived"
    /// from "the image arrived": a length check alone passes for any wrongly-offset slice of the
    /// same buffer.
    void ExpectCompletePng(const std::vector<std::uint8_t>& bytes)
    {
        ASSERT_GE(bytes.size(), 12u);
        const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        for (std::size_t i = 0; i < 8; ++i)
        {
            EXPECT_EQ(static_cast<int>(signature[i]), static_cast<int>(bytes[i]))
                << "byte " << i << " -- these are not the PNG's own bytes";
        }
        // IEND is the last four bytes of the last chunk, so its presence proves the tail arrived
        // too rather than only the header.
        EXPECT_EQ('D', static_cast<char>(bytes[bytes.size() - 5]));
    }

    void ExpectIsThePng(const std::vector<std::uint8_t>& bytes)
    {
        ASSERT_EQ(kRedGreenPngBytes, bytes.size());
        ExpectCompletePng(bytes);
    }
}

// --- GLTF-193: a bufferView-backed image --------------------------------------------------------

TEST(GltfImageSource, ABufferViewBackedImageYieldsExactlyTheViewsBytes)
{
    // The `.glb` shape: the pixels live in the binary chunk and the image names a `bufferView`.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ImageDocument(
        R"({ "bufferView": 0, "mimeType": "image/png" })", true)));
    ASSERT_EQ(1u, static_cast<std::size_t>(parsed.data->images_count));

    const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], ".");
    ASSERT_TRUE(image.has_value()) << "a bufferView-backed image extracted to nothing";
    ExpectIsThePng(image->bytes);
    EXPECT_EQ("png", image->extension) << "the extension comes from the declared mimeType";
}

// --- GLTF-195: a data: URI image ----------------------------------------------------------------

TEST(GltfImageSource, ADataUriImageDecodesToTheSameBytesAsTheBufferViewForm)
{
    // The self-contained `.gltf` shape. Asserted against the bufferView form rather than against a
    // literal, because the point is that the two are the SAME asset: a reader that handled one
    // path differently would make the same model look different depending on how it was exported.
    Parsed viaBufferView;
    ASSERT_TRUE(Parse(viaBufferView, ImageDocument(
        R"({ "bufferView": 0, "mimeType": "image/png" })", true)));
    Parsed viaDataUri;
    ASSERT_TRUE(Parse(viaDataUri, ImageDocument(
        std::string(R"({ "uri": "data:image/png;base64,)") + kRedGreenPngBase64 + R"(" })", false)));

    const std::optional<ExtractedImage> fromView = ExtractImage(&viaBufferView.data->images[0], ".");
    const std::optional<ExtractedImage> fromUri = ExtractImage(&viaDataUri.data->images[0], ".");
    ASSERT_TRUE(fromView.has_value());
    ASSERT_TRUE(fromUri.has_value()) << "a data: URI image extracted to nothing";
    ExpectIsThePng(fromUri->bytes);
    EXPECT_EQ(fromView->bytes, fromUri->bytes)
        << "the two source forms of one image produced different bytes";
}

TEST(GltfImageSource, CommittedExternalDataUriAndGlbImagesYieldTheSamePngBytes)
{
    const LoadedFixture external("gltf-external-image");
    const LoadedFixture inlineData("gltf-data-uri-image");
    ASSERT_TRUE(external.Ok()) << external.Error();
    ASSERT_TRUE(inlineData.Ok()) << inlineData.Error();
    ASSERT_EQ(1u, static_cast<std::size_t>(external.Data().images_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(inlineData.Data().images_count));
    ASSERT_NE(nullptr, external.Data().images[0].uri);
    ASSERT_NE(nullptr, inlineData.Data().images[0].uri);
    EXPECT_STREQ("gltf-external-image.texture.png", external.Data().images[0].uri);
    EXPECT_EQ(0u, std::string(inlineData.Data().images[0].uri).find("data:image/png;base64,"));
    const auto& externalImages = Path(external.Expected(), "container.gltf.images");
    const auto& inlineImages = Path(inlineData.Expected(), "container.gltf.images");
    ASSERT_EQ(CNA::Internal::JsonType::Array, externalImages.type);
    ASSERT_EQ(CNA::Internal::JsonType::Array, inlineImages.type);
    ASSERT_EQ(1u, externalImages.arrayValue.size());
    ASSERT_EQ(1u, inlineImages.arrayValue.size());
    EXPECT_EQ("external", StringOr(externalImages.arrayValue[0], "source", ""));
    EXPECT_EQ("data-uri", StringOr(inlineImages.arrayValue[0], "source", ""));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        CorpusDirectory() / "gltf-external-image.texture.png"));

    const std::optional<ExtractedImage> fromExternal =
        ExtractImage(&external.Data().images[0], external.AssetPath().parent_path());
    const std::optional<ExtractedImage> fromData =
        ExtractImage(&inlineData.Data().images[0], inlineData.AssetPath().parent_path());
    ASSERT_TRUE(fromExternal.has_value());
    ASSERT_TRUE(fromData.has_value());
    ExpectCompletePng(fromExternal->bytes);
    EXPECT_EQ(fromExternal->bytes, fromData->bytes);

    Parsed externalGlb;
    Parsed dataGlb;
    ASSERT_TRUE(ParseFile(externalGlb, CorpusDirectory() / "gltf-external-image.glb"));
    ASSERT_TRUE(ParseFile(dataGlb, CorpusDirectory() / "gltf-data-uri-image.glb"));
    ASSERT_EQ(1u, static_cast<std::size_t>(externalGlb.data->images_count));
    ASSERT_EQ(1u, static_cast<std::size_t>(dataGlb.data->images_count));
    ASSERT_NE(nullptr, externalGlb.data->images[0].uri);
    ASSERT_NE(nullptr, dataGlb.data->images[0].uri);
    EXPECT_EQ(0u, std::string(externalGlb.data->images[0].uri).find("data:image/png;base64,"));
    EXPECT_EQ(0u, std::string(dataGlb.data->images[0].uri).find("data:image/png;base64,"));
    const std::optional<ExtractedImage> fromExternalGlb = ExtractImage(
        &externalGlb.data->images[0], CorpusDirectory());
    const std::optional<ExtractedImage> fromDataGlb = ExtractImage(
        &dataGlb.data->images[0], CorpusDirectory());
    ASSERT_TRUE(fromExternalGlb.has_value());
    ASSERT_TRUE(fromDataGlb.has_value());
    EXPECT_EQ(fromExternal->bytes, fromExternalGlb->bytes);
    EXPECT_EQ(fromExternal->bytes, fromDataGlb->bytes);
}

TEST(GltfImageSource, ADataUriWithNoCommaIsRefusedRatherThanDecodedAsGarbage)
{
    // A `data:` URI's payload begins after the first comma. Without one there is no payload at all,
    // and decoding the media type as base64 produces plausible-looking bytes that are not an image
    // -- which surfaces much later as a decode failure with nothing pointing back at the URI.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ImageDocument(R"({ "uri": "data:image/png;base64" })", false)));
    EXPECT_THROW((void)ExtractImage(&parsed.data->images[0], "."), std::runtime_error);
}

// --- GLTF-194: an external file URI -------------------------------------------------------------

TEST(GltfImageSource, AnExternalUriImageIsReadFromTheAssetDirectory)
{
    // The third shape, and the only one that touches the filesystem. Resolution is relative to the
    // `.gltf`'s own directory, which is also where GLTF-032/GLTF-198's containment check applies.
    const ScratchDir dir;
    std::vector<std::uint8_t> png;
    {
        Parsed source;
        ASSERT_TRUE(Parse(source, ImageDocument(
            R"({ "bufferView": 0, "mimeType": "image/png" })", true)));
        const std::optional<ExtractedImage> image = ExtractImage(&source.data->images[0], ".");
        ASSERT_TRUE(image.has_value());
        png = image->bytes;
    }
    {
        std::ofstream out(dir.path() / "tex.png", std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()),
                  static_cast<std::streamsize>(png.size()));
    }

    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ImageDocument(R"({ "uri": "tex.png" })", false)));
    const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], dir.path());
    ASSERT_TRUE(image.has_value()) << "an external image file was not read";
    ExpectIsThePng(image->bytes);
    EXPECT_EQ("png", image->extension)
        << "with no mimeType declared the extension comes from the file name";
}

TEST(GltfImageSource, AMissingExternalImageFileErrorsNamingThePath)
{
    // "Cannot open" has to name the path, because the single most common cause is an asset shipped
    // without its textures and the fix is entirely about which file is missing from where.
    const ScratchDir dir;
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ImageDocument(R"({ "uri": "absent.png" })", false)));

    std::string message;
    try
    {
        (void)ExtractImage(&parsed.data->images[0], dir.path());
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }
    ASSERT_FALSE(message.empty()) << "a missing image file was accepted silently";
    EXPECT_NE(std::string::npos, message.find("absent.png")) << message;
}

// --- GLTF-196: the extension a decoder will be handed -------------------------------------------

TEST(GltfImageSource, TheBytesDecideTheExtensionAndTheDeclaredTypeIsOnlyAHint)
{
    // plans/plan_gltf.md GLTF-199. The extension travels with the bytes -- the offline path writes a
    // file named with it and a `.cnj` then references that file -- so it has to describe what the
    // bytes actually are. Both `mimeType` and a URI's media type are author-supplied strings, and
    // an exporter that writes `image/png` above JPEG bytes is not hypothetical: it is what a
    // "convert to PNG" step that failed silently produces.
    //
    // So the rule is: sniff the signature, and fall back to the declared type only when the bytes
    // say nothing. Both directions are asserted, because a sniffer that ignored the declaration
    // entirely would pass the first case alone.
    {
        // PNG bytes declared as JPEG. The bytes win.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(
            R"({ "bufferView": 0, "mimeType": "image/jpeg" })", true)));
        const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], ".");
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ("png", image->extension)
            << "the declared type overrode the actual bytes; the written file would not match its "
               "own name";
    }
    {
        // JPEG bytes in a `data:` URI declaring `image/png`. Again the bytes win, through the
        // other source branch -- the two used to guess independently.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(
            R"({ "uri": "data:image/png;base64,/9j/4AAQSkZJRgABAQAAAQABAAA=" })", false)));
        const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], ".");
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ("jpg", image->extension);
    }
    {
        // Bytes whose signature matches nothing: the declaration is all there is, so it is used.
        // Refusing here would reject every format CNA has no signature for rather than every
        // format it cannot decode, which are different sets.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(
            R"({ "uri": "data:image/jpeg;base64,AAECAwQFBgcICQoLDA0ODw==" })", false)));
        const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], ".");
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ("jpg", image->extension) << "the declared type is the fallback, not nothing";
    }
}

TEST(GltfImageSource, AnImageWithNeitherAUriNorABufferViewYieldsNothingRatherThanEmptyBytes)
{
    // The shape KHR_texture_basisu and EXT_texture_webp produce (GLTF-200): the pixels hang off an
    // extension and the plain image has no source at all. Returning empty BYTES rather than no
    // image would give a decoder a zero-length buffer and turn a missing texture into a decode
    // error, which points at the wrong thing.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ImageDocument(R"({ "name": "sourceless" })", false)));
    const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], ".");
    EXPECT_FALSE(image.has_value());
}

// --- GLTF-201: bytes that are not an image ---------------------------------------------------------

TEST(GltfImageSource, ZeroLengthAndCorruptImageSourcesAreDeterministicRatherThanUndefined)
{
    // An image's bytes come from a file or a URI, so they can be anything -- truncated by a failed
    // export, empty because a `data:` URI's payload was lost in a text pipeline, or simply not an
    // image. None of that may reach a decoder as an unbounded read, and none of it may crash: the
    // whole point of this row is that the outcome is the same every time and is either a named
    // failure or an empty-but-well-formed result the caller can test.
    //
    // ExtractImage does not decode -- it extracts -- so what it owes here is exactly that: bytes
    // it can vouch for, or nothing.
    {
        // A `data:` URI with no payload at all.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(
            R"({ "uri": "data:image/png;base64," })", false)));
        std::optional<ExtractedImage> image;
        ASSERT_NO_THROW(image = ExtractImage(&parsed.data->images[0], "."));
        if (image.has_value())
        {
            EXPECT_TRUE(image->bytes.empty())
                << "an empty payload produced bytes from somewhere";
            EXPECT_FALSE(image->extension.empty())
                << "an extension is still owed, so a caller can report what it thought it had";
        }
    }
    {
        // A payload that decodes as base64 but is not an image in any format.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(
            R"({ "uri": "data:image/png;base64,AAECAwQFBgcICQoLDA0ODw==" })", false)));
        std::optional<ExtractedImage> image;
        ASSERT_NO_THROW(image = ExtractImage(&parsed.data->images[0], "."));
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ(16u, image->bytes.size()) << "the bytes must arrive exactly as authored";
    }
    {
        // A `data:` URI with no comma at all -- malformed as a URI, before any base64 question.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(R"({ "uri": "data:image/png;base64" })", false)));
        EXPECT_THROW((void)ExtractImage(&parsed.data->images[0], "."), std::runtime_error);
    }
    {
        // An external file that does not exist. Named, not silent: a texture that vanishes with no
        // message is attributed to the material, the sampler, or the light -- anything but the
        // missing file.
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ImageDocument(R"({ "uri": "definitely-not-here.png" })", false)));
        std::string message;
        try
        {
            (void)ExtractImage(&parsed.data->images[0], ".");
        }
        catch (const std::exception& e)
        {
            message = e.what();
        }
        ASSERT_FALSE(message.empty()) << "a missing image file was accepted";
        EXPECT_NE(std::string::npos, message.find("definitely-not-here.png")) << message;
    }
}

// --- GLTF-205: the sampler a texture gets when it declares none ------------------------------------

TEST(GltfImageSource, ATextureWithNoSamplerGetsGltfsOwnDefaultsRatherThanTheDevicesCurrentState)
{
    // §5.29: `sampler` is optional, and its absence means repeat wrapping with the client free to
    // choose filtering -- not "whatever the device happens to have bound". Before GLTF-202/203
    // every imported texture drew with the device's LinearWrap by coincidence; a default that is
    // *chosen* rather than inherited is what makes that no longer a coincidence, and what stops a
    // future default change from silently altering every sampler-less asset.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "images": [ { "uri": "data:image/png;base64,)GLTF") + kRedGreenPngBase64 + R"GLTF(" } ],
  "textures": [ { "source": 0 } ],
  "materials": [ { "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 } } } ]
})GLTF"));

    ASSERT_EQ(1u, parsed.data->textures_count);
    EXPECT_EQ(nullptr, parsed.data->textures[0].sampler) << "the fixture must declare no sampler";

    // `SamplerForTextureView` is file-private; the mapping it falls back to for an absent sampler
    // is glTF's own "0 means unspecified" for all four values, which is what this asserts.
    const SamplerOut sampler = MapGltfSamplerEXT(0, 0, 0, 0);
    EXPECT_FALSE(sampler.declared)
        << "a default sampler must not claim to have been authored";
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureFilter::Linear), static_cast<int>(sampler.filter));
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap), static_cast<int>(sampler.addressU));
    EXPECT_EQ(static_cast<int>(Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap), static_cast<int>(sampler.addressV));
}
