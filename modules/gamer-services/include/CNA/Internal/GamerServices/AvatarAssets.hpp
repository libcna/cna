// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/EmbeddedFile.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Internal::GamerServices::Avatars {
/** @brief Bones in the XNA avatar skeleton. */
inline constexpr int BoneCount = 71;
/** @brief Parent slot of every bone (-1 for the root), exactly the XNA table. @return Parents. */
const std::array<int,BoneCount>& parentBones();
/** @brief Joint name a CNA avatar asset uses for a slot ("SlotNN" where XNA names none). @param slot Slot. @return Name. */
std::string_view boneName(int slot);
/** @brief Slot for a joint name. @param name Name. @return Slot or -1. */
int boneIndex(std::string_view name);

/** @brief Every file of the base catalog compiled into the library (generated source). @return Files by name. */
std::span<const EmbeddedFile> embeddedCatalogFiles();

/** @brief One file listed by a catalog manifest. */
struct CatalogAsset {
    /** @brief File name. */
    std::string name;
    /** @brief Lower-case hex SHA-256 of the contents. */
    std::string sha256;
    /** @brief Size in bytes. */
    std::size_t size=0;
};

/** @brief A wardrobe item and its per-body-type asset. */
struct CatalogItem {
    /** @brief Stable id a description stores. */
    std::uint16_t id=0;
    /** @brief Wardrobe slot. */
    AvatarItemSlot slot=AvatarItemSlot::Hair;
    /** @brief Item name. */
    std::string name;
    /** @brief Asset per body type (female, male). */
    std::array<std::string,2> assets;
};

/** @brief Atlas tile indices of every expression state. */
struct FaceLayout {
    /** @brief Tile edge in pixels. */
    int tileSize=0;
    /** @brief Tiles per atlas row. */
    int columns=0;
    /** @brief Eye tiles [AvatarEye][left/right][white layer, iris layer]. */
    std::array<std::array<std::array<int,2>,2>,14> eyes{};
    /** @brief Eyebrow tiles [AvatarEyebrow][left/right]. */
    std::array<std::array<int,2>,5> eyebrows{};
    /** @brief Mouth tiles [AvatarMouth]. */
    std::array<int,14> mouths{};
};

/** @brief A parsed, validated avatar catalog manifest (catalog.json). */
struct CatalogManifest {
    /** @brief Catalog version descriptions refer to. */
    std::uint16_t version=0;
    /** @brief Body asset per body type. */
    std::array<std::string,2> bodies;
    /** @brief Authored feet-to-head height per body type. */
    std::array<std::uint16_t,2> authoredHeightMillimeters{};
    /** @brief Wardrobe items. */
    std::vector<CatalogItem> items;
    /** @brief Face feature atlas asset. */
    std::string faceAsset;
    /** @brief Atlas layout. */
    FaceLayout face;
    /** @brief Preset animation asset. */
    std::string animationsAsset;
    /** @brief Every listed file by name. */
    std::map<std::string,CatalogAsset,std::less<>> assets;
    /** @brief Finds an item. @param id Item id. @return Item or null. */
    const CatalogItem* item(std::uint16_t id) const;
};

/** @brief Parses and validates a manifest; every name, size and hash is checked. @param json Text.
 * @return Manifest. @throws std::runtime_error when malformed. */
CatalogManifest parseManifest(std::string_view json);
/** @brief The manifest of the embedded base catalog. @return Manifest. */
const CatalogManifest& embeddedManifest();
/** @brief Lower-case hex SHA-256. @param bytes Input. @return Digest. */
std::string sha256Hex(std::span<const std::uint8_t> bytes);

/** @brief Bytes of one asset, either static embedded data or an owned copy. */
struct AssetBytes {
    /** @brief Keeps downloaded or cached contents alive. */
    std::shared_ptr<const std::vector<std::uint8_t>> owned;
    /** @brief The contents. */
    std::span<const std::uint8_t> view;
};
/** @brief Resolves a manifest asset to verified bytes (embedded catalog only; the service cache
 * extends this). @param manifest Manifest listing it. @param name Asset name. @return Bytes, or
 * empty when unavailable or when the contents do not match the manifest hash. */
std::optional<AssetBytes> resolveAsset(const CatalogManifest& manifest,std::string_view name);

/** @brief Vertex of an avatar mesh in bind pose. */
struct AvatarVertex {
    /** @brief Model-space position. */
    Microsoft::Xna::Framework::Vector3 position;
    /** @brief Unit normal. */
    Microsoft::Xna::Framework::Vector3 normal;
    /** @brief Texture coordinate (0,0 for untextured parts). */
    Microsoft::Xna::Framework::Vector2 uv;
    /** @brief Four bone slots. */
    std::array<std::uint8_t,4> joints{};
    /** @brief Four weights summing to 1. */
    Microsoft::Xna::Framework::Vector4 weights;
};

/** @brief One material-uniform part of an avatar asset. */
struct AvatarPrimitive {
    /** @brief Vertices. */
    std::vector<AvatarVertex> vertices;
    /** @brief Triangle list, counter-clockwise front faces (glTF). */
    std::vector<std::uint16_t> indices;
    /** @brief Description color that tints it ("skin", "hair", ...), or "none". */
    std::string tint;
    /** @brief Base color factor. */
    Microsoft::Xna::Framework::Vector3 color{1.0f,1.0f,1.0f};
    /** @brief Face feature decal ("eyeLeft", ...), empty for ordinary parts. */
    std::string feature;
    /** @brief Decal layer (0 white/lines, 1 iris). */
    int layer=0;
    /** @brief PNG of the base color texture, if any. */
    std::vector<std::uint8_t> texturePng;
};

/** @brief One animation curve on a joint (glTF cubic spline). */
struct AvatarTrack {
    /** @brief Bone slot. */
    int bone=0;
    /** @brief Translation (root) rather than rotation. */
    bool translation=false;
    /** @brief Key times in seconds. */
    std::vector<float> times;
    /** @brief In-tangent, value, out-tangent per key (xyz or xyzw). */
    std::vector<Microsoft::Xna::Framework::Vector4> values;
};

/** @brief Step key of the facial expression. */
struct AvatarExpressionKey {
    /** @brief Seconds. */
    float time=0.0f;
    /** @brief AvatarMouth, left/right AvatarEye, left/right AvatarEyebrow. */
    int mouth=0, leftEye=0, rightEye=0, leftEyebrow=0, rightEyebrow=0;
};

/** @brief One preset clip. */
struct AvatarClip {
    /** @brief Preset name. */
    std::string name;
    /** @brief Length in seconds. */
    float duration=0.0f;
    /** @brief Authored to loop seamlessly. */
    bool loop=false;
    /** @brief Curves. */
    std::vector<AvatarTrack> tracks;
    /** @brief Expression keys, by time. */
    std::vector<AvatarExpressionKey> expressions;
};

/** @brief Contents of one avatar GLB. */
struct AvatarGlb {
    /** @brief Bind local translation of every slot. */
    std::array<Microsoft::Xna::Framework::Vector3,BoneCount> bindTranslations{};
    /** @brief Mesh parts. */
    std::vector<AvatarPrimitive> primitives;
    /** @brief Animations. */
    std::vector<AvatarClip> clips;
};

/** @brief Parses a CNA avatar GLB, validating the 71-slot rig, every accessor and every bound.
 * @param bytes GLB. @return Contents. @throws std::runtime_error when malformed. */
AvatarGlb parseAvatarGlb(std::span<const std::uint8_t> bytes);

/** @brief The 31 preset clips and the canonical bind translations they are authored against. */
struct AvatarClipLibrary {
    /** @brief Clips indexed by AvatarAnimationPreset. */
    std::vector<AvatarClip> clips;
    /** @brief Canonical bind translations. */
    std::array<Microsoft::Xna::Framework::Vector3,BoneCount> bindTranslations{};
};
/** @brief Loads the embedded preset clips once. @return Library. @throws std::runtime_error when unavailable. */
const AvatarClipLibrary& clipLibrary();

/** @brief Decoded RGBA image, premultiplied alpha. */
struct AvatarImage {
    /** @brief Width. */
    int width=0;
    /** @brief Height. */
    int height=0;
    /** @brief Pixels, rows top to bottom. */
    std::vector<std::uint8_t> rgba;
};

/** @brief Face feature decals, indexed by AvatarModelPart::feature. */
enum class AvatarFeature : int { None=-1, EyeLeft, EyeRight, EyebrowLeft, EyebrowRight, Mouth };

/** @brief A drawable part of an assembled avatar. */
struct AvatarModelPart {
    /** @brief Bind-pose vertices, already scaled to the avatar. */
    std::vector<AvatarVertex> vertices;
    /** @brief Triangles with XNA's clockwise front faces. */
    std::vector<std::uint16_t> indices;
    /** @brief Final diffuse color (base color times the description tint). */
    Microsoft::Xna::Framework::Vector3 color{1.0f,1.0f,1.0f};
    /** @brief Index into AvatarModel::images, or -1 for none. */
    int image=-1;
    /** @brief Decal this part is, drawn with the expression's atlas tile. */
    AvatarFeature feature=AvatarFeature::None;
    /** @brief Decal layer (eyes: 0 white/lines, 1 iris). */
    int layer=0;
};

/** @brief An avatar assembled from a description, ready for upload. */
struct AvatarModel {
    /** @brief Feet-to-head height in meters. */
    float height=0.0f;
    /** @brief Bind local translation of every bone. */
    std::array<Microsoft::Xna::Framework::Vector3,BoneCount> bindTranslations{};
    /** @brief Bind model-space position of every bone. */
    std::array<Microsoft::Xna::Framework::Vector3,BoneCount> bindPositions{};
    /** @brief Opaque parts first, then decals. */
    std::vector<AvatarModelPart> parts;
    /** @brief Part textures. */
    std::vector<AvatarImage> images;
    /** @brief Every expression tile of the face atlas. */
    std::shared_ptr<const std::vector<AvatarImage>> faceTiles;
    /** @brief Atlas tile indices. */
    FaceLayout face;
    /** @brief Items the catalog could not resolve and that were replaced by a default. */
    std::vector<std::uint16_t> substitutedItems;
};

/** @brief Assembles an avatar from the catalog. @param descriptor Description.
 * @return Model. @throws std::runtime_error when the body cannot be resolved. */
std::shared_ptr<const AvatarModel> buildAvatarModel(const AvatarDescriptor& descriptor);

/** @brief Progress of a background avatar load. */
struct AvatarLoad {
    /** @brief Guards the fields below. */
    std::mutex lock;
    /** @brief The load finished (successfully or not). */
    bool done=false;
    /** @brief The model on success. */
    std::shared_ptr<const AvatarModel> model;
    /** @brief Failure reason. */
    std::string error;
};
/** @brief Starts assembling an avatar on the shared avatar loader thread (a cached model completes
 * at once). @param descriptor Description. @return Progress. */
std::shared_ptr<AvatarLoad> loadAvatarAsync(const AvatarDescriptor& descriptor);

/** @brief Samples a clip. @param clip Clip. @param seconds Time. @param rotations Out: local rotation per slot
 * (identity where the clip has no curve). @param rootTranslation Out: root offset. @return Expression key in effect. */
AvatarExpressionKey sampleClip(const AvatarClip& clip,double seconds,
    std::array<Microsoft::Xna::Framework::Quaternion,BoneCount>& rotations,Microsoft::Xna::Framework::Vector3& rootTranslation);
}
