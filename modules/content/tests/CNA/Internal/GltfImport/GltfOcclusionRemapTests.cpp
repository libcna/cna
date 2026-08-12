// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-239 / GLTF-358: two behaviours that live entirely on the CPU side of the
// importer and are therefore verifiable without a renderer.
//
// `RemapOcclusionImageForDualTextureEXT` exists because `DualTextureEffect` multiplies its two
// textures and treats **0.5 as neutral** -- a lightmap convention, not glTF's. A glTF occlusion map
// uses 1.0 as "fully lit", so handing one straight to that effect doubles the brightness of every
// unoccluded texel. Halving the RGB is the conversion, and it is the kind of arithmetic that looks
// obviously right and is exactly as obviously wrong in the other direction.

#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using namespace CNA::Internal::GltfImport;

namespace
{
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

    /// The 2x1 PNG described above, as a `data:` URI payload. Texel 0 is (255, 128, 64, 255) and
    /// texel 1 is (0, 255, 200, 128).
    const char* kTwoTexelPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAAFUlEQVR4nGP4//8/AwMDAwMTAwMDAB6NAwHUxBmqAAAAAElFTkSuQmCC";

    std::optional<ExtractedImage> LoadKnownImage()
    {
        Parsed parsed;
        const std::string json = std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "images": [ { "uri": "data:image/png;base64,)GLTF") + kTwoTexelPngBase64 + R"GLTF(" } ]
})GLTF";
        if (!Parse(parsed, json)) { return std::nullopt; }
        return ExtractImage(&parsed.data->images[0], ".");
    }
}

// --- GLTF-239: the occlusion-to-lightmap remap --------------------------------------------------

TEST(GltfOcclusionRemap, TheRemapProducesADecodableImageOfTheSameSize)
{
    // The conversion re-encodes, so the first thing that has to hold is that the result is still a
    // readable image of the same dimensions -- a remap that produced a truncated or malformed PNG
    // would surface much later as a texture that failed to load, with nothing pointing back here.
    const std::optional<ExtractedImage> source = LoadKnownImage();
    ASSERT_TRUE(source.has_value()) << "the test's own source image did not decode";

    const std::optional<ExtractedImage> remapped =
        RemapOcclusionImageForDualTextureEXT(*source);
    ASSERT_TRUE(remapped.has_value()) << "the remap produced nothing at all";
    EXPECT_FALSE(remapped->bytes.empty());
    EXPECT_EQ("png", remapped->extension)
        << "the remap re-encodes, and the extension has to describe what it actually produced";

    // A PNG signature on the output, for the same reason GLTF-193 asserts one on the input: a size
    // check alone passes for any blob of the right length.
    ASSERT_GE(remapped->bytes.size(), 8u);
    const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (std::size_t i = 0; i < 8; ++i)
    {
        EXPECT_EQ(static_cast<int>(signature[i]), static_cast<int>(remapped->bytes[i]))
            << "byte " << i << " -- the remap did not produce a PNG";
    }
}

TEST(GltfOcclusionRemap, TheRemapIsNotTheIdentity)
{
    // The whole point is that the bytes change. A remap that returned its input unmodified would
    // satisfy every structural assertion above while leaving each unoccluded texel twice as bright
    // as DualTextureEffect's 0.5-is-neutral convention expects -- which looks like a lighting
    // setting rather than a conversion that never ran.
    const std::optional<ExtractedImage> source = LoadKnownImage();
    ASSERT_TRUE(source.has_value());
    const std::optional<ExtractedImage> remapped =
        RemapOcclusionImageForDualTextureEXT(*source);
    ASSERT_TRUE(remapped.has_value());
    EXPECT_NE(source->bytes, remapped->bytes)
        << "the remap returned its input; every unoccluded texel is then twice as bright as the "
           "0.5-is-neutral convention expects";
}

TEST(GltfOcclusionRemap, TheRemapIsRefusedRatherThanGuessedForBytesThatAreNotAnImage)
{
    // The input comes from a file, so it can be anything. Returning a result for undecodable bytes
    // would mean fabricating an occlusion map, and a fabricated one is indistinguishable from a
    // real one at every later stage.
    ExtractedImage garbage;
    garbage.bytes = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    garbage.extension = "png";
    EXPECT_FALSE(RemapOcclusionImageForDualTextureEXT(garbage).has_value());

    ExtractedImage empty;
    empty.extension = "png";
    EXPECT_FALSE(RemapOcclusionImageForDualTextureEXT(empty).has_value());
}

// --- GLTF-358: the build without libdraco -------------------------------------------------------

TEST(GltfOcclusionRemap, DracoIsRefusedUpFrontInABuildWithoutTheDecoder)
{
    // `KHR_draco_mesh_compression` is the one extension whose support depends on how CNA was built,
    // and the two builds have to fail differently: with libdraco the file imports, without it the
    // file must be refused **up front** by the extensionsRequired gate rather than failing later
    // inside `ExtractMesh` with a message about an accessor that has no backing data.
    //
    // The assertion follows the build, so the test is meaningful in both configurations rather than
    // skipped in one.
#ifdef CNA_DRACO_AVAILABLE
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_draco_mesh_compression"))
        << "this build has libdraco, so a Draco-compressed file is importable";
#else
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_draco_mesh_compression"))
        << "this build has no libdraco, so a file REQUIRING Draco must be refused up front";

    // And the refusal actually happens, through the ordinary extensionsRequired path -- the point
    // of GLTF-358 is that no Draco-specific error handling is needed for this case at all.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, R"GLTF({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ "KHR_draco_mesh_compression" ],
  "extensionsRequired": [ "KHR_draco_mesh_compression" ]
})GLTF"));

    std::vector<std::string> warnings;
    std::string message;
    try
    {
        ValidateGltfEXT(parsed.data, "draco.gltf", warnings);
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }
    ASSERT_FALSE(message.empty()) << "a file requiring Draco was accepted by a build without it";
    EXPECT_NE(std::string::npos, message.find("KHR_draco_mesh_compression")) << message;
    EXPECT_NE(std::string::npos, message.find("extensionsRequired")) << message;
#endif
}
