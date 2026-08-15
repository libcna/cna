// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-200 / GLTF-350: a texture whose pixels are in a format CNA has no decoder for
// must be reported, never silently absent.
//
// `KHR_texture_basisu` (KTX2/Basis) and `EXT_texture_webp` both attach their own image to a
// texture, and both are specified so a file MAY additionally keep a plain PNG/JPEG `source` as a
// fallback for readers without the codec. That gives three distinct outcomes, and all three are
// asserted here: the fallback is used when present, the loss is named when it is not, and a file
// that lists either extension as *required* is refused outright by GLTF-023 long before any of
// this runs.
//
// These are scratch-directory documents rather than corpus assets because what varies between the
// cases is the shape of the `texture` object -- which `source` it has and which it does not -- and
// none of them needs a single byte of real pixel data to make its point.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using namespace CNA::Internal::GltfImport;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_unsupported_texture_"
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

    /// A single triangle with a base-color texture, where @p textureJson is the whole `textures[0]`
    /// object and @p imagesJson the whole `images` array -- the two things every case here varies.
    std::string Document(const std::string& extensionsUsed, const std::string& textureJson,
                         const std::string& imagesJson)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ )GLTF") + extensionsUsed + R"GLTF( ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "material": 0
  } ] } ],
  "materials": [ { "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 }
  } } ],
  "textures": [ )GLTF" + textureJson + R"GLTF( ],
  "images": )GLTF" + imagesJson + R"GLTF(,
  "samplers": [ { } ],
  "buffers": [ { "byteLength": 96, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";
    }

    struct Loaded
    {
        cgltf_data* data = nullptr;
        ~Loaded() { if (data != nullptr) { cgltf_free(data); } }
        Loaded() = default;
        Loaded(const Loaded&) = delete;
        Loaded& operator=(const Loaded&) = delete;
    };

    bool Parse(Loaded& out, const ScratchDir& dir, const std::string& json)
    {
        const std::filesystem::path gltfPath = dir.path() / "fixture.gltf";
        std::ofstream(gltfPath, std::ios::binary) << json;
        cgltf_options options{};
        if (cgltf_parse_file(&options, gltfPath.string().c_str(), &out.data) !=
            cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, gltfPath.string().c_str()) ==
               cgltf_result_success;
    }

    MeshOut Extract(const std::string& json)
    {
        const ScratchDir dir;
        Loaded loaded;
        EXPECT_TRUE(Parse(loaded, dir, json)) << "the fixture document did not parse";
        if (loaded.data == nullptr || loaded.data->meshes_count == 0) { return MeshOut{}; }
        return ExtractMesh(loaded.data, loaded.data->meshes[0].primitives[0], "primitive0", nullptr,
                           1.0f);
    }

    const char* kPngImage = R"([ { "uri": "base.png", "mimeType": "image/png" } ])";
}

// --- KHR_texture_basisu -----------------------------------------------------------------------

TEST(GltfUnsupportedTexture, BasisuWithNoFallbackSourceIsReportedAndTheMapIsDropped)
{
    // The silent case this task exists for: the texture's only source is the KTX2 image, so
    // `texture->image` is null and every downstream check used to read "no texture on this slot".
    const MeshOut out = Extract(Document(
        R"("KHR_texture_basisu")",
        R"({ "sampler": 0, "extensions": { "KHR_texture_basisu": { "source": 0 } } })",
        R"([ { "uri": "base.ktx2", "mimeType": "image/ktx2" } ])"));

    EXPECT_EQ(nullptr, out.material.baseColorImage);
    ASSERT_EQ(1u, out.unsupportedTextureSourcesEXT.size())
        << "an unreadable base-colour texture was dropped without a word";
    EXPECT_NE(out.unsupportedTextureSourcesEXT[0].find("base color"), std::string::npos)
        << out.unsupportedTextureSourcesEXT[0];
    EXPECT_NE(out.unsupportedTextureSourcesEXT[0].find("KHR_texture_basisu"), std::string::npos)
        << out.unsupportedTextureSourcesEXT[0];
}

TEST(GltfUnsupportedTexture, BasisuWithAPlainFallbackSourceUsesTheFallbackAndReportsNothing)
{
    // The extension is specified so a file may keep a PNG/JPEG `source` for readers without the
    // codec. Using it is not a compromise -- it is what the extension tells such a reader to do --
    // so nothing has been lost and nothing should be reported.
    const MeshOut out = Extract(Document(
        R"("KHR_texture_basisu")",
        R"({ "sampler": 0, "source": 1, "extensions": { "KHR_texture_basisu": { "source": 0 } } })",
        R"([ { "uri": "base.ktx2", "mimeType": "image/ktx2" },
             { "uri": "base.png", "mimeType": "image/png" } ])"));

    ASSERT_NE(nullptr, out.material.baseColorImage) << "the authored PNG fallback was not used";
    EXPECT_TRUE(out.unsupportedTextureSourcesEXT.empty())
        << "nothing was lost, so nothing should have been reported: "
        << (out.unsupportedTextureSourcesEXT.empty() ? "" : out.unsupportedTextureSourcesEXT[0]);
}

// --- EXT_texture_webp -------------------------------------------------------------------------

TEST(GltfUnsupportedTexture, WebpWithNoFallbackSourceIsReportedByName)
{
    const MeshOut out = Extract(Document(
        R"("EXT_texture_webp")",
        R"({ "sampler": 0, "extensions": { "EXT_texture_webp": { "source": 0 } } })",
        R"([ { "uri": "base.webp", "mimeType": "image/webp" } ])"));

    EXPECT_EQ(nullptr, out.material.baseColorImage);
    ASSERT_EQ(1u, out.unsupportedTextureSourcesEXT.size());
    EXPECT_NE(out.unsupportedTextureSourcesEXT[0].find("EXT_texture_webp"), std::string::npos)
        << "the report has to name the extension, or it says nothing a user can act on: "
        << out.unsupportedTextureSourcesEXT[0];
}

// --- An undecodable mime type on a plain source ------------------------------------------------

TEST(GltfUnsupportedTexture, APlainSourceWithAnUndecodableMimeTypeIsReportedAtImportNotAtDecode)
{
    // The same loss wearing different clothes: the image is present and readable as bytes, and
    // stb_image will refuse it. Caught here, where the map's name is still known, rather than at a
    // decode failure that cannot say which map it was.
    const MeshOut out = Extract(Document(
        R"("EXT_texture_webp")",
        R"({ "sampler": 0, "source": 0 })",
        R"([ { "uri": "base.webp", "mimeType": "image/webp" } ])"));

    EXPECT_EQ(nullptr, out.material.baseColorImage);
    ASSERT_EQ(1u, out.unsupportedTextureSourcesEXT.size());
    EXPECT_NE(out.unsupportedTextureSourcesEXT[0].find("image/webp"), std::string::npos)
        << out.unsupportedTextureSourcesEXT[0];
}

// --- Every map slot, not just base colour ------------------------------------------------------

TEST(GltfUnsupportedTexture, EveryMaterialMapSlotReportsItsOwnLossByName)
{
    // A report that only covered base colour would leave the four other slots exactly as silent as
    // they were. Each entry names its own map so a user reading the log knows which one vanished.
    const ScratchDir dir;
    Loaded loaded;
    ASSERT_TRUE(Parse(loaded, dir, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_texture_basisu" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "material": 0
  } ] } ],
  "materials": [ {
      "pbrMetallicRoughness": {
          "baseColorTexture": { "index": 0 },
          "metallicRoughnessTexture": { "index": 0 }
      },
      "normalTexture": { "index": 0 },
      "occlusionTexture": { "index": 0 },
      "emissiveTexture": { "index": 0 }
  } ],
  "textures": [ { "sampler": 0, "extensions": { "KHR_texture_basisu": { "source": 0 } } } ],
  "images": [ { "uri": "base.ktx2", "mimeType": "image/ktx2" } ],
  "samplers": [ { } ],
  "buffers": [ { "byteLength": 96, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF")));

    const MeshOut out = ExtractMesh(loaded.data, loaded.data->meshes[0].primitives[0], "primitive0",
                                    nullptr, 1.0f);

    const std::string joined = [&] {
        std::string all;
        for (const std::string& entry : out.unsupportedTextureSourcesEXT) { all += entry + " | "; }
        return all;
    }();
    for (const char* map : {"base color", "metallic-roughness", "normal", "emissive", "occlusion"})
    {
        EXPECT_NE(joined.find(map), std::string::npos)
            << "no report entry for the " << map << " map: " << joined;
    }
}

// --- The required-extension case is somebody else's problem, and stays that way ----------------

TEST(GltfUnsupportedTexture, NeitherExtensionIsClaimedAsSupported)
{
    // If either were ever listed as supported, a file *requiring* it would load with its textures
    // quietly missing instead of being refused -- which is a worse outcome than today's, not a
    // better one. This is the guard on that.
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_texture_basisu"));
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("EXT_texture_webp"));
}

TEST(GltfUnsupportedTexture, RequiringEitherExtensionIsRefusedOutrightRatherThanReported)
{
    // GLTF-023's rule, exercised through this task's two extensions specifically: `extensionsUsed`
    // is a report, `extensionsRequired` is a rejection, and the reporting added here must not have
    // quietly downgraded the second into the first.
    for (const char* extension : {"KHR_texture_basisu", "EXT_texture_webp"})
    {
        SCOPED_TRACE(extension);
        const ScratchDir dir;
        Loaded loaded;
        ASSERT_TRUE(Parse(loaded, dir, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "extensionsUsed": [ ")GLTF") + extension + R"GLTF(" ],
  "extensionsRequired": [ ")GLTF" + std::string(extension) + R"GLTF(" ]
})GLTF"));

        std::vector<std::string> warnings;
        EXPECT_THROW(ValidateGltfEXT(loaded.data, "fixture.gltf", warnings), std::runtime_error);
    }
}

// --- plan_gltf.md GLTF-184/GLTF-336: independent per-map transform state ------------------------

namespace
{
    /// A material whose base colour and normal map each carry their own texture transform.
    std::string TransformDocument(const std::string& baseColorTransform,
                                  const std::string& normalTransform)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_texture_transform" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "material": 0
  } ] } ],
  "materials": [ {
      "pbrMetallicRoughness": {
          "baseColorTexture": { "index": 0)GLTF") + baseColorTransform + R"GLTF( }
      },
      "normalTexture": { "index": 0)GLTF" + normalTransform + R"GLTF( }
  } ],
  "textures": [ { "sampler": 0, "source": 0 } ],
  "images": [ { "uri": "base.png", "mimeType": "image/png" } ],
  "samplers": [ { } ],
  "buffers": [ { "byteLength": 96, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";
    }

    const char* kScaledTwice = R"(, "extensions": { "KHR_texture_transform": { "scale": [2, 2] } })";
    const char* kScaledFour  = R"(, "extensions": { "KHR_texture_transform": { "scale": [4, 4] } })";
}

TEST(GltfUnsupportedTexture, DifferentBaseAndNormalTransformsAreBothCarried)
{
    const MeshOut out = Extract(TransformDocument(kScaledTwice, kScaledFour));
    const auto& base = out.material.textureTransformsEXT[
        static_cast<std::size_t>(TextureSlotEXT::BaseColor)];
    const auto& normal = out.material.textureTransformsEXT[
        static_cast<std::size_t>(TextureSlotEXT::Normal)];
    EXPECT_FLOAT_EQ(base.Scale.X, 2.0f);
    EXPECT_FLOAT_EQ(base.Scale.Y, 2.0f);
    EXPECT_FLOAT_EQ(normal.Scale.X, 4.0f);
    EXPECT_FLOAT_EQ(normal.Scale.Y, 4.0f);
    EXPECT_NE(base, normal);
}

TEST(GltfUnsupportedTexture, MapsMayCarryEqualTransformsIndependently)
{
    const MeshOut out = Extract(TransformDocument(kScaledTwice, kScaledTwice));
    EXPECT_EQ(out.material.textureTransformsEXT[
                  static_cast<std::size_t>(TextureSlotEXT::BaseColor)],
              out.material.textureTransformsEXT[
                  static_cast<std::size_t>(TextureSlotEXT::Normal)]);
}

TEST(GltfUnsupportedTexture, ATransformOnOnlyTheNormalMapLeavesBaseColourAtIdentity)
{
    const MeshOut out = Extract(TransformDocument("", kScaledFour));
    const Microsoft::Xna::Framework::Graphics::TextureTransformEXT identity;
    EXPECT_EQ(identity, out.material.textureTransformsEXT[
                            static_cast<std::size_t>(TextureSlotEXT::BaseColor)]);
    EXPECT_FLOAT_EQ(4.0f, out.material.textureTransformsEXT[
                              static_cast<std::size_t>(TextureSlotEXT::Normal)].Scale.X);
}

TEST(GltfUnsupportedTexture, ATransformlessMaterialKeepsAllFiveIdentityDefaults)
{
    const MeshOut out = Extract(TransformDocument("", ""));
    const Microsoft::Xna::Framework::Graphics::TextureTransformEXT identity;
    for (const auto& transform : out.material.textureTransformsEXT)
        EXPECT_EQ(identity, transform);
}

// --- plan_gltf.md GLTF-339: KHR_materials_transmission -----------------------------------------
//
// The extension was read by nobody, so a glass material imported fully opaque -- the
// ChronographWatch defect, where the crystal hides the dial it exists to reveal. A real
// transmission pass samples the framebuffer behind the surface and blurs it by roughness, which
// needs a second pass and a scene-colour target no CNA stock effect has. What CNA does instead is
// an alpha-blend approximation, and these tests pin both halves of that decision: that it happens
// at all, and that it is never silent.

namespace
{
    /// A material with the given transmission block, over the same triangle as everything else here.
    std::string TransmissionDocument(const std::string& transmissionJson,
                                     const std::string& baseColorFactor = "")
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_materials_transmission" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "material": 0
  } ] } ],
  "materials": [ {
      "pbrMetallicRoughness": { )GLTF") + baseColorFactor + R"GLTF( })GLTF" +
               transmissionJson + R"GLTF(
  } ],
  "buffers": [ { "byteLength": 96, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";
    }
}

TEST(GltfUnsupportedTexture, FullTransmissionBecomesAFullyBlendedSurfaceRatherThanAnOpaqueOne)
{
    // transmissionFactor 1 is clear glass. Opaque is the one answer that cannot be right, and it is
    // what CNA produced.
    const MeshOut out = Extract(TransmissionDocument(
        R"(, "extensions": { "KHR_materials_transmission": { "transmissionFactor": 1.0 } })"));

    EXPECT_TRUE(out.transmissionApproximatedEXT);
    EXPECT_NEAR(1.0f, out.transmissionFactorEXT, 1e-6f);
    EXPECT_EQ(Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend,
              out.material.alphaMode)
        << "a transmissive material left on OPAQUE draws as glass that hides what is behind it";
    EXPECT_NEAR(0.0f, out.material.baseColorFactor.W, 1e-6f);
}

TEST(GltfUnsupportedTexture, PartialTransmissionMultipliesIntoTheMaterialsOwnAlpha)
{
    // A material that is both partly transparent and partly transmissive should end up more
    // transparent than either alone -- so the factor multiplies rather than replaces. 0.5 alpha
    // with 0.5 transmission gives 0.25.
    const MeshOut out = Extract(TransmissionDocument(
        R"(, "extensions": { "KHR_materials_transmission": { "transmissionFactor": 0.5 } })",
        R"("baseColorFactor": [1, 1, 1, 0.5])"));

    EXPECT_TRUE(out.transmissionApproximatedEXT);
    EXPECT_NEAR(0.25f, out.material.baseColorFactor.W, 1e-6f)
        << "the transmission replaced the material's own alpha instead of compounding with it";
}

TEST(GltfUnsupportedTexture, ATransmissionFactorOfZeroLeavesTheMaterialCompletelyAlone)
{
    // The control. A material may declare the extension neutrally, and treating that as "approximate
    // something" would force every such material to BLEND -- turning a correctness fix into a
    // regression for the assets that need nothing.
    const MeshOut out = Extract(TransmissionDocument(
        R"(, "extensions": { "KHR_materials_transmission": { "transmissionFactor": 0.0 } })"));

    EXPECT_FALSE(out.transmissionApproximatedEXT);
    EXPECT_EQ(Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque,
              out.material.alphaMode);
    EXPECT_NEAR(1.0f, out.material.baseColorFactor.W, 1e-6f);
}

TEST(GltfUnsupportedTexture, ATransmissionTextureIsFlaggedSeparatelyFromTheFactor)
{
    // A per-texel transmission map has nowhere to go in an alphaMode/baseColorFactor
    // approximation, so a surface that varies its transmission is flattened to one value. That is
    // materially worse than the uniform case and is reported apart from it.
    const MeshOut out = Extract(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_materials_transmission" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
      "material": 0
  } ] } ],
  "materials": [ {
      "pbrMetallicRoughness": { },
      "extensions": { "KHR_materials_transmission": {
          "transmissionFactor": 0.8,
          "transmissionTexture": { "index": 0 }
      } }
  } ],
  "textures": [ { "sampler": 0, "source": 0 } ],
  "images": [ { "uri": "t.png", "mimeType": "image/png" } ],
  "samplers": [ { } ],
  "buffers": [ { "byteLength": 96, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF");

    EXPECT_TRUE(out.transmissionApproximatedEXT);
    EXPECT_NEAR(0.8f, out.transmissionFactorEXT, 1e-6f);
    EXPECT_TRUE(out.transmissionHasTextureEXT)
        << "a per-texel transmission map was flattened to one factor with nothing recording it";
}

TEST(GltfUnsupportedTexture, TransmissionIsNotClaimedAsAnImplementedExtension)
{
    // An approximation is not an implementation. Claiming it would let a file that REQUIRES
    // transmission load with its glass drawn as tinted alpha, which is exactly the silent wrongness
    // GLTF-023 refuses such files to prevent.
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_transmission"));
}
