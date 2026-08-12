// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-193 / GLTF-194 / GLTF-195 / GLTF-196: where a texture's pixels come from.
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

using namespace CNA::Internal::GltfImport;

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
    void ExpectIsThePng(const std::vector<std::uint8_t>& bytes)
    {
        ASSERT_EQ(kRedGreenPngBytes, bytes.size());
        const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        for (std::size_t i = 0; i < 8; ++i)
        {
            EXPECT_EQ(static_cast<int>(signature[i]), static_cast<int>(bytes[i]))
                << "byte " << i << " -- these are not the PNG's own bytes";
        }
        // IEND is the last four bytes of the last chunk, so its presence proves the tail arrived
        // too rather than only the header.
        EXPECT_EQ('D', static_cast<char>(bytes[kRedGreenPngBytes - 5]));
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

TEST(GltfImageSource, TheMimeTypeDecidesTheExtensionAndOverridesTheFileName)
{
    // The extension is what selects a decoder downstream, and §3.9.2 makes `mimeType` authoritative
    // when present -- a `.png`-named file declared `image/jpeg` is a JPEG. Trusting the name
    // instead hands the wrong decoder bytes it will refuse, and the texture disappears with the
    // failure attributed to the image rather than to the naming.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ImageDocument(
        R"({ "bufferView": 0, "mimeType": "image/jpeg" })", true)));
    const std::optional<ExtractedImage> image = ExtractImage(&parsed.data->images[0], ".");
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ("jpg", image->extension);
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
