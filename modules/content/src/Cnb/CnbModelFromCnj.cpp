// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjMorphSidecarEXT.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CNA::Content::Cnb
{
    namespace
    {
        namespace Json = CNA::Internal;
        namespace fs = std::filesystem;

        /// Reads a whole file, refusing quietly-truncated reads rather than returning short data.
        std::vector<std::uint8_t> ReadBinary(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw ContentLoadException("cnj-to-cnb: cannot open binary file '" + path + "'.");
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            const std::string bytes = ss.str();
            return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
        }

        std::string ReadText(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw ContentLoadException("cnj-to-cnb: cannot open '" + path + "'.");
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }

        /// A cursor over a raw sidecar's little-endian bytes. Every read is bounds-checked; the
        /// legacy sidecar formats have no length fields of their own, so this is the only thing
        /// standing between a truncated file and an out-of-bounds read.
        class SidecarReader
        {
        public:
            SidecarReader(std::vector<std::uint8_t> bytes, std::string what)
                : bytes_(std::move(bytes)), what_(std::move(what))
            {
            }

            [[nodiscard]] std::size_t Remaining() const { return bytes_.size() - position_; }

            std::int32_t I32() { return static_cast<std::int32_t>(Raw<std::uint32_t>()); }
            float F32() { const auto bits = Raw<std::uint32_t>(); float v; std::memcpy(&v, &bits, 4); return v; }
            double F64() { const auto bits = Raw<std::uint64_t>(); double v; std::memcpy(&v, &bits, 8); return v; }

            std::array<float, 16> Matrix()
            {
                std::array<float, 16> matrix{};
                for (float& value : matrix) { value = F32(); }
                return matrix;
            }

            [[noreturn]] void Fail(const std::string& detail) const
            {
                throw ContentLoadException("cnj-to-cnb: " + what_ + ": " + detail);
            }

            void RequireExhausted() const
            {
                if (Remaining() != 0u)
                {
                    Fail("has " + std::to_string(Remaining()) + " unexpected trailing byte(s).");
                }
            }

        private:
            template <typename T>
            T Raw()
            {
                if (position_ + sizeof(T) > bytes_.size())
                {
                    Fail("is truncated: needed " + std::to_string(sizeof(T)) +
                         " byte(s) at offset " + std::to_string(position_) + ", but only " +
                         std::to_string(Remaining()) + " remain.");
                }
                T value = 0;
                for (std::size_t i = 0; i < sizeof(T); ++i)
                {
                    value |= static_cast<T>(bytes_[position_ + i]) << (8u * i);
                }
                position_ += sizeof(T);
                return value;
            }

            std::vector<std::uint8_t> bytes_;
            std::string what_;
            std::size_t position_ = 0;
        };

        std::string ResolveSidecar(const std::string& root, const std::string& value,
                                   const char* field, const std::string& cnjPath)
        {
            const Internal::ContainedPathResult resolved =
                Internal::ResolveContainedPath(root, value);
            if (!resolved.ok)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: '" + cnjPath + "' field '" + field + "' ('" + value +
                    "') must be a relative path contained within the content root.");
            }
            return resolved.resolvedPath;
        }

        /// Turns a `.cnj` asset reference into the logical name ContentManager would resolve it by,
        /// with the same containment rule the runtime reader applies, and normalised to the
        /// forward-slash form the `.cnb` external-reference table requires.
        std::string ToLogicalName(const std::string& root, const std::string& value,
                                  const char* field, const std::string& cnjPath)
        {
            const std::string resolved = ResolveSidecar(root, value, field, cnjPath);
            const fs::path rootPath =
                (root.empty() ? fs::path(".") : fs::path(root)).lexically_normal();
            std::error_code ec;
            fs::path relative = fs::path(resolved).lexically_normal().lexically_relative(rootPath);
            if (relative.empty())
            {
                throw ContentLoadException(
                    "cnj-to-cnb: '" + cnjPath + "' field '" + field + "' ('" + value +
                    "') does not resolve to a name inside the content root.");
            }
            (void)ec;
            return relative.generic_string();
        }

        const Json::JsonValue* Member(const Json::JsonValue& object, const char* key)
        {
            return object.IsObject() ? object.FindMember(key) : nullptr;
        }

        std::string StringField(const Json::JsonValue& object, const char* key,
                                const std::string& cnjPath, const std::string& fallback = {})
        {
            const Json::JsonValue* value = Member(object, key);
            if (value == nullptr) { return fallback; }
            if (!value->IsString())
            {
                throw ContentLoadException("cnj-to-cnb: '" + cnjPath + "' field '" + key +
                                            "' must be a string.");
            }
            return value->stringValue;
        }

        double NumberField(const Json::JsonValue& object, const char* key,
                           const std::string& cnjPath, double fallback)
        {
            const Json::JsonValue* value = Member(object, key);
            if (value == nullptr) { return fallback; }
            if (!value->IsNumber())
            {
                throw ContentLoadException("cnj-to-cnb: '" + cnjPath + "' field '" + key +
                                            "' must be a number.");
            }
            return value->numberValue;
        }

        bool BoolField(const Json::JsonValue& object, const char* key, const std::string& cnjPath,
                       bool fallback)
        {
            const Json::JsonValue* value = Member(object, key);
            if (value == nullptr) { return fallback; }
            if (value->type != Json::JsonType::Boolean)
            {
                throw ContentLoadException("cnj-to-cnb: '" + cnjPath + "' field '" + key +
                                            "' must be a boolean.");
            }
            return value->boolValue;
        }

        std::vector<float> FloatArrayField(const Json::JsonValue& object, const char* key,
                                            const std::string& cnjPath)
        {
            std::vector<float> out;
            const Json::JsonValue* value = Member(object, key);
            if (value == nullptr) { return out; }
            if (value->type != Json::JsonType::Array)
            {
                throw ContentLoadException("cnj-to-cnb: '" + cnjPath + "' field '" + key +
                                            "' must be an array.");
            }
            out.reserve(value->arrayValue.size());
            for (const Json::JsonValue& element : value->arrayValue)
            {
                if (!element.IsNumber())
                {
                    throw ContentLoadException("cnj-to-cnb: '" + cnjPath + "' field '" + key +
                                                "' must contain only numbers.");
                }
                out.push_back(static_cast<float>(element.numberValue));
            }
            return out;
        }

        void RequireExactLength(const std::vector<float>& values, std::size_t expected,
                                 const char* key, const std::string& cnjPath)
        {
            if (values.size() != expected)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: '" + cnjPath + "' field '" + key + "' must contain exactly " +
                    std::to_string(expected) + " numbers, not " + std::to_string(values.size()) +
                    ".");
            }
        }

        AnimationClipEXT ReadClipBin(const std::string& path)
        {
            SidecarReader reader(ReadBinary(path), "clip sidecar '" + path + "'");
            AnimationClipEXT clip;
            const double duration = reader.F64();
            if (!std::isfinite(duration))
            {
                reader.Fail("declares a non-finite duration.");
            }
            clip.Duration = System::TimeSpan::FromSeconds(duration);
            const std::int32_t trackCount = reader.I32();
            if (trackCount < 0) { reader.Fail("declares a negative track count."); }
            clip.Tracks.reserve(static_cast<std::size_t>(std::min(trackCount, 4096)));
            for (std::int32_t t = 0; t < trackCount; ++t)
            {
                BoneTrackEXT track;
                track.BoneIndex = reader.I32();
                const std::int32_t keyCount = reader.I32();
                if (keyCount < 0) { reader.Fail("declares a negative key count."); }
                track.Keys.reserve(static_cast<std::size_t>(std::min(keyCount, 65536)));
                for (std::int32_t k = 0; k < keyCount; ++k)
                {
                    KeyframeEXT key;
                    const double time = reader.F64();
                    if (!std::isfinite(time)) { reader.Fail("has a non-finite keyframe time."); }
                    key.Time = System::TimeSpan::FromSeconds(time);
                    const float tx = reader.F32();
                    const float ty = reader.F32();
                    const float tz = reader.F32();
                    key.Translation = Microsoft::Xna::Framework::Vector3(tx, ty, tz);
                    const float qx = reader.F32();
                    const float qy = reader.F32();
                    const float qz = reader.F32();
                    const float qw = reader.F32();
                    key.Rotation = Microsoft::Xna::Framework::Quaternion(qx, qy, qz, qw);
                    const float sx = reader.F32();
                    const float sy = reader.F32();
                    const float sz = reader.F32();
                    key.Scale = Microsoft::Xna::Framework::Vector3(sx, sy, sz);
                    track.Keys.push_back(key);
                }
                clip.Tracks.push_back(std::move(track));
            }
            reader.RequireExhausted();
            return clip;
        }

        AnimationClipEXT ReadClipCnj(const std::string& path)
        {
            const std::string json = ReadText(path);
            const Internal::CnjEnvelope envelope = Internal::ParseCnjEnvelope(json);
            Internal::ValidateCnjEnvelope(envelope, "AnimationClip", path);

            const Json::JsonValue root = Json::ParseJson(json);
            if (const Json::JsonValue* clipFile = root.FindMember("clipFile");
                clipFile != nullptr && clipFile->IsString())
            {
                // A .cnj AnimationClip may itself defer to a raw sidecar; the compiler follows the
                // one indirection the runtime reader follows, and no further.
                const fs::path parent = fs::path(path).parent_path();
                return ReadClipBin((parent / clipFile->stringValue).string());
            }

            AnimationClipEXT clip;
            clip.Duration = System::TimeSpan::FromSeconds(
                NumberField(root, "duration", path, 0.0));
            if (StringField(root, "targetSpace", path) == "SceneNode")
            {
                clip.TargetSpace = ClipTargetSpaceEXT::SceneNode;
            }
            const Json::JsonValue* tracks = root.FindMember("tracks");
            if (tracks == nullptr || tracks->type != Json::JsonType::Array)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: AnimationClip '" + path + "' has no 'tracks' array.");
            }
            for (const Json::JsonValue& trackValue : tracks->arrayValue)
            {
                BoneTrackEXT track;
                track.BoneIndex = static_cast<int>(NumberField(trackValue, "boneIndex", path, -1.0));
                const Json::JsonValue* keys = Member(trackValue, "keys");
                if (keys == nullptr || keys->type != Json::JsonType::Array)
                {
                    throw ContentLoadException(
                        "cnj-to-cnb: AnimationClip '" + path + "' has a track with no 'keys'.");
                }
                for (const Json::JsonValue& keyValue : keys->arrayValue)
                {
                    KeyframeEXT key;
                    key.Time = System::TimeSpan::FromSeconds(
                        NumberField(keyValue, "time", path, 0.0));
                    const std::vector<float> translation =
                        FloatArrayField(keyValue, "translation", path);
                    if (!translation.empty())
                    {
                        RequireExactLength(translation, 3u, "translation", path);
                        key.Translation = Microsoft::Xna::Framework::Vector3(
                            translation[0], translation[1], translation[2]);
                    }
                    const std::vector<float> rotation = FloatArrayField(keyValue, "rotation", path);
                    if (!rotation.empty())
                    {
                        RequireExactLength(rotation, 4u, "rotation", path);
                        key.Rotation = Microsoft::Xna::Framework::Quaternion(
                            rotation[0], rotation[1], rotation[2], rotation[3]);
                    }
                    const std::vector<float> scale = FloatArrayField(keyValue, "scale", path);
                    if (!scale.empty())
                    {
                        RequireExactLength(scale, 3u, "scale", path);
                        key.Scale =
                            Microsoft::Xna::Framework::Vector3(scale[0], scale[1], scale[2]);
                    }
                    track.Keys.push_back(key);
                }
                clip.Tracks.push_back(std::move(track));
            }
            return clip;
        }

        CnbMorphData ReadMorphSidecar(const std::string& path, std::uint32_t partVertexCount,
                                       bool recomputeFlatNormals,
                                       const std::vector<float>& defaultWeights)
        {
            SidecarReader reader(ReadBinary(path), "morph sidecar '" + path + "'");
            CnbMorphData morph;
            morph.vertexCount = partVertexCount;
            morph.recomputeFlatNormals = recomputeFlatNormals;

            const std::int32_t targetCount = reader.I32();
            if (targetCount < 0 || targetCount > 100000)
            {
                reader.Fail("declares an invalid morph target count of " +
                            std::to_string(targetCount) + ".");
            }
            morph.targets.resize(static_cast<std::size_t>(targetCount));

            std::vector<std::int32_t> declaredVertexCounts;
            declaredVertexCounts.reserve(static_cast<std::size_t>(targetCount));
            for (std::int32_t t = 0; t < targetCount; ++t)
            {
                const std::int32_t vertexCount = reader.I32();
                if (vertexCount < 0 ||
                    (vertexCount != 0 &&
                     static_cast<std::uint32_t>(vertexCount) != partVertexCount))
                {
                    reader.Fail("target " + std::to_string(t) + " declares " +
                                std::to_string(vertexCount) + " vertices; expected 0 or " +
                                std::to_string(partVertexCount) + ".");
                }
                declaredVertexCounts.push_back(vertexCount);

                CnbMorphTarget& target = morph.targets[static_cast<std::size_t>(t)];
                target.positionDeltas.resize(static_cast<std::size_t>(vertexCount) * 3u);
                for (float& value : target.positionDeltas) { value = reader.F32(); }

                const std::int32_t hasNormals = reader.I32();
                if (hasNormals != 0 && hasNormals != 1)
                {
                    reader.Fail("target " + std::to_string(t) +
                                " has an invalid normal-delta flag.");
                }
                if (hasNormals != 0)
                {
                    target.normalDeltas.resize(static_cast<std::size_t>(vertexCount) * 3u);
                    for (float& value : target.normalDeltas) { value = reader.F32(); }
                }
            }

            if (reader.Remaining() > 0u)
            {
                const std::int32_t magic = reader.I32();
                if (magic != Internal::CnjMorphTangentTrailerMagicEXT)
                {
                    reader.Fail("has an unknown trailing block.");
                }
                const std::int32_t version = reader.I32();
                if (version != Internal::CnjMorphTangentTrailerVersionEXT)
                {
                    reader.Fail("has a tangent trailer with unsupported version " +
                                std::to_string(version) + ".");
                }
                const std::int32_t trailerTargets = reader.I32();
                if (trailerTargets != targetCount)
                {
                    reader.Fail("has a tangent trailer declaring " +
                                std::to_string(trailerTargets) + " targets; expected " +
                                std::to_string(targetCount) + ".");
                }
                for (std::int32_t t = 0; t < targetCount; ++t)
                {
                    const std::int32_t hasTangents = reader.I32();
                    if (hasTangents != 0 && hasTangents != 1)
                    {
                        reader.Fail("target " + std::to_string(t) +
                                    " has an invalid tangent-delta flag.");
                    }
                    if (hasTangents == 0) { continue; }
                    const std::int32_t vertexCount =
                        declaredVertexCounts[static_cast<std::size_t>(t)];
                    if (vertexCount == 0)
                    {
                        reader.Fail("target " + std::to_string(t) +
                                    " has tangent deltas but no vertices.");
                    }
                    CnbMorphTarget& target = morph.targets[static_cast<std::size_t>(t)];
                    target.tangentDeltas.resize(static_cast<std::size_t>(vertexCount) * 3u);
                    for (float& value : target.tangentDeltas) { value = reader.F32(); }
                }
            }
            reader.RequireExhausted();

            morph.weights = defaultWeights.empty()
                                ? std::vector<float>(static_cast<std::size_t>(targetCount), 0.0f)
                                : defaultWeights;
            return morph;
        }
    }

    CnbModelFromCnjResult BuildCnbModelFromCnj(const std::string& cnjPath,
                                                const std::string& contentRoot)
    {
        namespace Gltf = CNA::Internal::GltfImport;

        const std::string json = ReadText(cnjPath);
        const Internal::CnjEnvelope envelope = Internal::ParseCnjEnvelope(json);
        Internal::ValidateCnjEnvelope(envelope, "Model", cnjPath, /*maxVersion=*/2);
        if (envelope.hasSourceFile)
        {
            throw ContentLoadException(
                "cnj-to-cnb: Model '" + cnjPath +
                "' has a 'sourceFile' field, which a Model .cnj does not support.");
        }

        const Json::JsonValue root = Json::ParseJson(json);
        CnbModelFromCnjResult result;
        std::set<std::string> externalReferences;

        // plans/plan_cnb.md CNBF-084: material variants are outside the v1 Model schema. Refusing by
        // name beats compiling an asset into something quietly less capable than its source.
        if (root.FindMember("materialVariantNames") != nullptr)
        {
            throw ContentLoadException(
                "cnj-to-cnb: Model '" + cnjPath + "' uses glTF material variants "
                "('materialVariantNames'), which the CNB v1 Model schema does not express. See "
                "plans/plan_cnb.md decision D9.");
        }

        // --- bones ---------------------------------------------------------------------------
        if (const Json::JsonValue* bones = root.FindMember("bones"); bones != nullptr)
        {
            if (bones->type != Json::JsonType::Array)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Model '" + cnjPath + "' field 'bones' must be an array.");
            }
            for (std::size_t b = 0; b < bones->arrayValue.size(); ++b)
            {
                const Json::JsonValue& entry = bones->arrayValue[b];
                CnbModelBone bone;
                bone.name = StringField(entry, "name", cnjPath);
                // Entry 0 defaults to the root; every later entry defaults to a child of it,
                // matching the runtime reader's own defaults for a hand-written document.
                bone.parent = static_cast<std::int32_t>(
                    NumberField(entry, "parent", cnjPath, b == 0 ? -1.0 : 0.0));
                const std::vector<float> transform = FloatArrayField(entry, "transform", cnjPath);
                if (!transform.empty())
                {
                    RequireExactLength(transform, 16u, "transform", cnjPath);
                    std::copy(transform.begin(), transform.end(), bone.transform.begin());
                }
                if (bone.name.empty())
                {
                    bone.name = b == 0 ? "Root" : ("Node" + std::to_string(b));
                }
                result.model.bones.push_back(std::move(bone));
            }
        }
        if (result.model.bones.empty())
        {
            // The runtime reader synthesises a single "Root" bone for a document with no "bones"
            // array at all; the compiled form carries that bone explicitly so both paths agree.
            result.model.bones.push_back(CnbModelBone{"Root", -1, {}});
        }
        result.model.hasBoneHierarchy = result.model.bones.size() > 1u;
        // cnjVersion 2 is what gltf_to_cnj writes, and it is the version whose materials were
        // authored under glTF's lighting conventions. A version-1 or hand-written document keeps
        // XNA's own defaults, which the runtime reader is explicit about.
        result.model.appliesGltfLightingPolicy = envelope.cnjVersion >= 2;

        // --- lights --------------------------------------------------------------------------
        if (const Json::JsonValue* lights = root.FindMember("lights"); lights != nullptr)
        {
            if (lights->type != Json::JsonType::Array)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Model '" + cnjPath + "' field 'lights' must be an array.");
            }
            for (const Json::JsonValue& entry : lights->arrayValue)
            {
                CnbModelLight light;
                const std::vector<float> direction = FloatArrayField(entry, "direction", cnjPath);
                const std::vector<float> color = FloatArrayField(entry, "diffuseColor", cnjPath);
                RequireExactLength(direction, 3u, "direction", cnjPath);
                RequireExactLength(color, 3u, "diffuseColor", cnjPath);
                std::copy(direction.begin(), direction.end(), light.direction.begin());
                std::copy(color.begin(), color.end(), light.diffuseColor.begin());
                result.model.lights.push_back(light);
            }
        }

        // --- skeleton ------------------------------------------------------------------------
        const std::string skeletonFile = StringField(root, "skeleton", cnjPath);
        if (!skeletonFile.empty())
        {
            const std::string path =
                ResolveSidecar(contentRoot, skeletonFile, "skeleton", cnjPath);
            SidecarReader reader(ReadBinary(path), "skeleton sidecar '" + path + "'");
            const std::int32_t boneCount = reader.I32();
            if (boneCount < 0 || boneCount > 100000)
            {
                reader.Fail("declares an invalid bone count of " + std::to_string(boneCount) + ".");
            }
            CnbModelSkeleton skeleton;
            skeleton.hierarchy.resize(static_cast<std::size_t>(boneCount));
            for (std::int32_t& parent : skeleton.hierarchy) { parent = reader.I32(); }
            skeleton.bindPose.resize(static_cast<std::size_t>(boneCount));
            for (auto& matrix : skeleton.bindPose) { matrix = reader.Matrix(); }
            skeleton.inverseBindPose.resize(static_cast<std::size_t>(boneCount));
            for (auto& matrix : skeleton.inverseBindPose) { matrix = reader.Matrix(); }
            if (reader.Remaining() >= static_cast<std::size_t>(boneCount) * 64u)
            {
                skeleton.rootPrefix.resize(static_cast<std::size_t>(boneCount));
                for (auto& matrix : skeleton.rootPrefix) { matrix = reader.Matrix(); }
            }
            reader.RequireExhausted();
            result.model.skeleton = std::move(skeleton);
            result.absorbedFiles.push_back(skeletonFile);
        }

        // --- animations ----------------------------------------------------------------------
        if (const Json::JsonValue* animations = root.FindMember("animations");
            animations != nullptr)
        {
            if (animations->type != Json::JsonType::Array)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Model '" + cnjPath + "' field 'animations' must be an array.");
            }
            for (const Json::JsonValue& entry : animations->arrayValue)
            {
                const std::string name = StringField(entry, "name", cnjPath);
                const std::string clipFile = StringField(entry, "clip", cnjPath);
                if (name.empty() || clipFile.empty()) { continue; }

                const std::string path = ResolveSidecar(contentRoot, clipFile, "clip", cnjPath);
                CnbModelAnimation animation;
                animation.name = name;
                animation.clip = fs::path(path).extension() == ".cnj" ? ReadClipCnj(path)
                                                                       : ReadClipBin(path);
                // An unskinned model's rigid clips are dropped by the runtime reader when they
                // target a joint palette; the compiled form keeps the same rule so the two paths
                // produce the same clip set.
                if (!result.model.skeleton.has_value() &&
                    animation.clip.TargetSpace != ClipTargetSpaceEXT::SceneNode)
                {
                    continue;
                }
                result.model.animations.push_back(std::move(animation));
                result.absorbedFiles.push_back(clipFile);
            }
        }

        // --- meshes --------------------------------------------------------------------------
        const Json::JsonValue* meshes = root.FindMember("meshes");
        if (meshes == nullptr || meshes->type != Json::JsonType::Array)
        {
            throw ContentLoadException(
                "cnj-to-cnb: Model '" + cnjPath + "' is missing its 'meshes' array.");
        }

        struct PendingMesh
        {
            std::string name;
            int group = -1;
            std::int32_t parentBone = 0;
            std::vector<std::uint32_t> partIndices;
        };
        std::vector<PendingMesh> pending;

        for (const Json::JsonValue& entry : meshes->arrayValue)
        {
            if (!entry.IsObject())
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Model '" + cnjPath + "' has a non-object entry in 'meshes'.");
            }
            if (Member(entry, "variantOf") != nullptr || Member(entry, "materialVariant") != nullptr)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Model '" + cnjPath + "' has a mesh entry using glTF material "
                    "variants, which the CNB v1 Model schema does not express. See "
                    "plans/plan_cnb.md decision D9.");
            }

            const std::string vertFile = StringField(entry, "vertices", cnjPath);
            const std::string idxFile = StringField(entry, "indices", cnjPath);
            if (vertFile.empty() || idxFile.empty())
            {
                // The runtime reader skips such an entry outright; matching it keeps the two
                // loaders' part lists identical.
                continue;
            }

            CnbModelPart part;
            part.name = StringField(entry, "name", cnjPath);
            const double strideValue = NumberField(entry, "vertexStride", cnjPath, 16.0);
            if (strideValue <= 0.0 || strideValue > 4096.0)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: Model '" + cnjPath + "' mesh '" + part.name +
                    "' has an unusable 'vertexStride'.");
            }
            part.vertexStride = static_cast<std::uint32_t>(strideValue);

            const std::string vertPath = ResolveSidecar(contentRoot, vertFile, "vertices", cnjPath);
            const std::string idxPath = ResolveSidecar(contentRoot, idxFile, "indices", cnjPath);
            part.vertexBytes = ReadBinary(vertPath);
            part.indexBytes = ReadBinary(idxPath);
            result.absorbedFiles.push_back(vertFile);
            result.absorbedFiles.push_back(idxFile);

            // The runtime reader derives the counts by truncating division, so a sidecar whose
            // length is not a whole number of elements silently loads as a shorter mesh. A
            // build-time tool is exactly where that must be loud instead.
            if (part.vertexBytes.size() % part.vertexStride != 0u)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: '" + vertPath + "' holds " +
                    std::to_string(part.vertexBytes.size()) +
                    " byte(s), which is not a whole number of " +
                    std::to_string(part.vertexStride) + "-byte vertices.");
            }
            part.vertexCount =
                static_cast<std::uint32_t>(part.vertexBytes.size() / part.vertexStride);
            // Mirrors the runtime reader, which mirrors XNA's stock ModelProcessor: 32-bit
            // indices once a mesh exceeds 65535 vertices.
            part.indexElementSize = part.vertexCount > 65535u ? 4u : 2u;
            if (part.indexBytes.size() % part.indexElementSize != 0u)
            {
                throw ContentLoadException(
                    "cnj-to-cnb: '" + idxPath + "' holds " +
                    std::to_string(part.indexBytes.size()) +
                    " byte(s), which is not a whole number of " +
                    std::to_string(part.indexElementSize) + "-byte indices.");
            }
            part.indexCount =
                static_cast<std::uint32_t>(part.indexBytes.size() / part.indexElementSize);

            const Gltf::PrimitiveTopology topology = Gltf::PrimitiveTopologyFromName(
                StringField(entry, "primitiveTopology", cnjPath));
            part.primitiveTopology = static_cast<std::uint32_t>(topology);
            part.primitiveCount = static_cast<std::uint32_t>(std::max(
                Gltf::PrimitiveCountForTopology(topology,
                                                 static_cast<std::size_t>(part.indexCount)),
                0));

            const std::string effect = StringField(entry, "effect", cnjPath);
            if (effect.empty() || effect == "BasicEffect") { part.effectKind = CnbEffectKind::BasicEffect; }
            else if (effect == "SkinnedEffect") { part.effectKind = CnbEffectKind::SkinnedEffect; }
            else if (effect == "DualTextureEffect") { part.effectKind = CnbEffectKind::DualTextureEffect; }
            else if (effect == "PbrEffect") { part.effectKind = CnbEffectKind::PbrEffect; }
            else if (effect == "SkinnedPbrEffect") { part.effectKind = CnbEffectKind::SkinnedPbrEffect; }
            else
            {
                part.effectKind = CnbEffectKind::External;
                part.externalEffect = ToLogicalName(contentRoot, effect, "effect", cnjPath);
                externalReferences.insert(part.externalEffect);
            }

            part.vertexColorEnabled = BoolField(entry, "vertexColorEnabled", cnjPath, false);
            part.unlit = BoolField(entry, "unlit", cnjPath, false);

            const auto textureField = [&](const char* key, std::string& out)
            {
                const std::string value = StringField(entry, key, cnjPath);
                if (value.empty()) { return; }
                out = ToLogicalName(contentRoot, value, key, cnjPath);
                externalReferences.insert(out);
            };
            textureField("texture", part.material.baseColorTexture);
            textureField("texture2", part.material.texture2);
            textureField("normalMap", part.material.normalMap);
            textureField("metallicRoughnessMap", part.material.metallicRoughnessMap);
            textureField("emissiveMap", part.material.emissiveMap);
            textureField("occlusionMap", part.material.occlusionMap);
            textureField("specularMap", part.material.specularMap);
            textureField("specularColorMap", part.material.specularColorMap);

            part.material.metallicFactor =
                static_cast<float>(NumberField(entry, "metallicFactor", cnjPath, 1.0));
            part.material.roughnessFactor =
                static_cast<float>(NumberField(entry, "roughnessFactor", cnjPath, 1.0));
            part.material.ior = static_cast<float>(NumberField(entry, "ior", cnjPath, 1.5));
            part.material.specularFactor =
                static_cast<float>(NumberField(entry, "specularFactor", cnjPath, 1.0));
            part.material.normalScale =
                static_cast<float>(NumberField(entry, "normalScale", cnjPath, 1.0));
            part.material.occlusionStrength =
                static_cast<float>(NumberField(entry, "occlusionStrength", cnjPath, 1.0));
            part.material.alphaCutoff =
                static_cast<float>(NumberField(entry, "alphaCutoff", cnjPath, 0.5));
            part.material.doubleSided = BoolField(entry, "doubleSided", cnjPath, false);
            part.material.alphaMode = static_cast<std::uint32_t>(
                Gltf::AlphaModeEXTFromName(StringField(entry, "alphaMode", cnjPath)));

            if (const std::vector<float> specularColor =
                    FloatArrayField(entry, "specularColorFactor", cnjPath);
                !specularColor.empty())
            {
                RequireExactLength(specularColor, 3u, "specularColorFactor", cnjPath);
                std::copy(specularColor.begin(), specularColor.end(),
                          part.material.specularColorFactor.begin());
            }
            if (const std::vector<float> emissive = FloatArrayField(entry, "emissiveFactor", cnjPath);
                !emissive.empty())
            {
                RequireExactLength(emissive, 3u, "emissiveFactor", cnjPath);
                std::copy(emissive.begin(), emissive.end(), part.material.emissiveFactor.begin());
            }
            // The runtime reader keeps white when 'diffuseColor' is absent -- a generic zero
            // default would silently turn every such material black.
            if (const std::vector<float> diffuse = FloatArrayField(entry, "diffuseColor", cnjPath);
                !diffuse.empty())
            {
                RequireExactLength(diffuse, 3u, "diffuseColor", cnjPath);
                part.material.baseColorFactor[0] = diffuse[0];
                part.material.baseColorFactor[1] = diffuse[1];
                part.material.baseColorFactor[2] = diffuse[2];
            }
            part.material.baseColorFactor[3] =
                static_cast<float>(NumberField(entry, "alpha", cnjPath, 1.0));

            if (const std::vector<float> sets =
                    FloatArrayField(entry, "textureCoordinateSets", cnjPath);
                !sets.empty())
            {
                RequireExactLength(sets, 5u, "textureCoordinateSets", cnjPath);
                for (std::size_t slot = 0; slot < 5u; ++slot)
                {
                    if (sets[slot] != 0.0f && sets[slot] != 1.0f)
                    {
                        throw ContentLoadException(
                            "cnj-to-cnb: '" + cnjPath +
                            "' field 'textureCoordinateSets' entries must be 0 or 1.");
                    }
                    part.material.textureCoordinateSets[slot] =
                        static_cast<std::uint8_t>(sets[slot]);
                }
            }
            if (const std::vector<float> sets =
                    FloatArrayField(entry, "specularTextureCoordinateSets", cnjPath);
                !sets.empty())
            {
                RequireExactLength(sets, 2u, "specularTextureCoordinateSets", cnjPath);
                for (std::size_t i = 0; i < 2u; ++i)
                {
                    if (sets[i] != 0.0f && sets[i] != 1.0f)
                    {
                        throw ContentLoadException(
                            "cnj-to-cnb: '" + cnjPath +
                            "' field 'specularTextureCoordinateSets' entries must be 0 or 1.");
                    }
                    part.material.textureCoordinateSets[5u + i] =
                        static_cast<std::uint8_t>(sets[i]);
                }
            }

            const auto applyTransforms = [&](const std::vector<float>& values, std::size_t firstSlot,
                                             std::size_t slotCount, const char* key)
            {
                if (values.empty()) { return; }
                RequireExactLength(values, slotCount * 5u, key, cnjPath);
                for (std::size_t i = 0; i < slotCount; ++i)
                {
                    CnbTextureTransform& transform =
                        part.material.textureTransforms[firstSlot + i];
                    transform.offsetX = values[i * 5u + 0u];
                    transform.offsetY = values[i * 5u + 1u];
                    transform.scaleX = values[i * 5u + 2u];
                    transform.scaleY = values[i * 5u + 3u];
                    transform.rotation = values[i * 5u + 4u];
                }
            };
            applyTransforms(FloatArrayField(entry, "textureTransforms", cnjPath), 0u, 5u,
                            "textureTransforms");
            applyTransforms(FloatArrayField(entry, "specularTextureTransforms", cnjPath), 5u, 2u,
                            "specularTextureTransforms");

            for (std::size_t slot = 0; slot < CnbTextureSlotCount; ++slot)
            {
                const std::string prefix = "sampler" + std::to_string(slot);
                const auto filter = static_cast<int>(
                    NumberField(entry, (prefix + "Filter").c_str(), cnjPath,
                                static_cast<double>(
                                    Microsoft::Xna::Framework::Graphics::TextureFilter::Linear)));
                const auto addressU = static_cast<int>(NumberField(
                    entry, (prefix + "AddressU").c_str(), cnjPath,
                    static_cast<double>(
                        Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap)));
                const auto addressV = static_cast<int>(NumberField(
                    entry, (prefix + "AddressV").c_str(), cnjPath,
                    static_cast<double>(
                        Microsoft::Xna::Framework::Graphics::TextureAddressMode::Wrap)));

                using Filter = Microsoft::Xna::Framework::Graphics::TextureFilter;
                using Address = Microsoft::Xna::Framework::Graphics::TextureAddressMode;
                if (filter < static_cast<int>(Filter::Linear) ||
                    filter > static_cast<int>(Filter::MinPointMagLinearMipPoint) ||
                    addressU < static_cast<int>(Address::Wrap) ||
                    addressU > static_cast<int>(Address::Mirror) ||
                    addressV < static_cast<int>(Address::Wrap) ||
                    addressV > static_cast<int>(Address::Mirror))
                {
                    throw ContentLoadException(
                        "cnj-to-cnb: Model '" + cnjPath + "' mesh '" + part.name +
                        "' has an invalid serialized sampler state.");
                }

                CnbSamplerState& sampler = part.material.samplers[slot];
                sampler.filter = static_cast<std::uint32_t>(filter);
                sampler.addressU = static_cast<std::uint32_t>(addressU);
                sampler.addressV = static_cast<std::uint32_t>(addressV);
                // The .cnj form only stores non-default values, so the runtime reader
                // reconstructs 'declared' exactly this way. Matching it is what keeps the two
                // paths' sampler state identical.
                sampler.declared = filter != static_cast<int>(Filter::Linear) ||
                                   addressU != static_cast<int>(Address::Wrap) ||
                                   addressV != static_cast<int>(Address::Wrap);
            }

            const std::string morphFile = StringField(entry, "morphTargets", cnjPath);
            if (!morphFile.empty())
            {
                const std::string morphPath =
                    ResolveSidecar(contentRoot, morphFile, "morphTargets", cnjPath);
                CnbMorphData morph = ReadMorphSidecar(
                    morphPath, part.vertexCount,
                    BoolField(entry, "morphFlatNormals", cnjPath, false),
                    FloatArrayField(entry, "morphWeights", cnjPath));

                if (const Json::JsonValue* track = Member(entry, "morphWeightTrack");
                    track != nullptr && track->IsObject())
                {
                    morph.weightTrackStepInterpolation =
                        BoolField(*track, "stepInterpolation", cnjPath, false);
                    morph.weightTrackCubicSpline = BoolField(*track, "cubicSpline", cnjPath, false);
                    if (const Json::JsonValue* keys = Member(*track, "keys");
                        keys != nullptr && keys->type == Json::JsonType::Array)
                    {
                        for (const Json::JsonValue& keyValue : keys->arrayValue)
                        {
                            CnbMorphWeightKey key;
                            key.timeSeconds = NumberField(keyValue, "time", cnjPath, 0.0);
                            key.weights = FloatArrayField(keyValue, "weights", cnjPath);
                            key.inTangent = FloatArrayField(keyValue, "inTangent", cnjPath);
                            key.outTangent = FloatArrayField(keyValue, "outTangent", cnjPath);
                            morph.weightTrackKeys.push_back(std::move(key));
                        }
                    }
                }

                part.morph = std::move(morph);
                result.absorbedFiles.push_back(morphFile);
            }

            const auto partIndex = static_cast<std::uint32_t>(result.model.parts.size());
            result.model.parts.push_back(std::move(part));

            // GLTF-139's grouping rule, verbatim: consecutive entries sharing a "partOfMesh"
            // value become one ModelMesh, and an entry without the field gets its own.
            const int partOfMesh = static_cast<int>(NumberField(entry, "partOfMesh", cnjPath, -1.0));
            if (partOfMesh >= 0 && !pending.empty() && pending.back().group == partOfMesh)
            {
                pending.back().partIndices.push_back(partIndex);
            }
            else
            {
                PendingMesh mesh;
                const std::string& partName = result.model.parts.back().name;
                mesh.name = partName.empty() ? "mesh" : partName;
                mesh.group = partOfMesh;
                mesh.parentBone =
                    static_cast<std::int32_t>(NumberField(entry, "parentBone", cnjPath, 0.0));
                mesh.partIndices.push_back(partIndex);
                pending.push_back(std::move(mesh));
            }
        }

        for (PendingMesh& mesh : pending)
        {
            if (result.model.hasBoneHierarchy)
            {
                if (mesh.parentBone < 0 ||
                    static_cast<std::size_t>(mesh.parentBone) >= result.model.bones.size())
                {
                    throw ContentLoadException(
                        "cnj-to-cnb: Model '" + cnjPath + "' mesh '" + mesh.name +
                        "' names an out-of-range parentBone index (" +
                        std::to_string(mesh.parentBone) + ").");
                }
            }
            else
            {
                // Without a hierarchy the runtime reader gives every mesh its own bone, a child of
                // the root named after the mesh. The compiled form carries those bones explicitly
                // so the decoded model needs no such synthesis step.
                const auto boneIndex = static_cast<std::int32_t>(result.model.bones.size());
                result.model.bones.push_back(CnbModelBone{mesh.name, 0, {}});
                mesh.parentBone = boneIndex;
            }

            CnbModelMesh out;
            out.name = mesh.name;
            out.parentBone = mesh.parentBone;
            out.partIndices = std::move(mesh.partIndices);
            result.model.meshes.push_back(std::move(out));
        }

        result.externalReferences.assign(externalReferences.begin(), externalReferences.end());
        return result;
    }
}
