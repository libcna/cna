// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ModelImporters.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <vector>

#include "CNA/Content/Pipeline/DirectXFileReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/AnimationContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/TextureImporter.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    namespace
    {
        namespace Canon = CNA::Content::Pipeline;
        using Graphics::AnimationChannel;
        using Graphics::AnimationContent;
        using Graphics::AnimationKeyframe;
        using Graphics::BasicMaterialContent;
        using Graphics::BoneContent;
        using Graphics::BoneWeight;
        using Graphics::BoneWeightCollection;
        using Graphics::GeometryContent;
        using Graphics::MeshContent;
        using Graphics::NodeContent;
        using Graphics::VertexChannelNames;

        /** @brief The DirectX default when a file declares no `AnimTicksPerSecond`. */
        constexpr double DefaultTicksPerSecond = 4800.0;

        /** @brief XNA's sentence for a `.x` it could not read, with the D3DX code appended. */
        [[nodiscard]] std::string Unreadable(const char* code)
        {
            return std::string("Could not read the X file. The file is corrupt or invalid. Error code: ") +
                   code + ".";
        }

        /** @brief One float out of an object's flat number list, with a bounds check. */
        [[nodiscard]] float At(const Canon::DirectXFileObject& object, const std::size_t index)
        {
            if (index >= object.numbers.size())
            {
                // A well-formed file whose object is short of the data its template declares is
                // the E_FAIL case rather than a parse error: the tokens read, the object did not.
                throw InvalidContentException(Unreadable("E_FAIL"));
            }
            return static_cast<float>(object.numbers[index]);
        }

        /** @brief The same, as a count that must fit an index. */
        [[nodiscard]] std::size_t Count(const Canon::DirectXFileObject& object, const std::size_t index)
        {
            const double value = At(object, index);
            if (value < 0.0 || value > 100000000.0)
            {
                throw InvalidContentException(Unreadable("E_FAIL"));
            }
            return static_cast<std::size_t>(value);
        }

        /** @brief The left-handed source vector as the right-handed pipeline holds it. */
        [[nodiscard]] Vector3 Convert(const Vector3 value)
        {
            return Vector3(value.X, value.Y, -value.Z);
        }

        /**
         * @brief The left-handed matrix as the right-handed pipeline holds it.
         *
         * The basis change `S M S` with `S = diag(1, 1, -1, 1)`: the third row and the third
         * column are negated, which leaves `M33` alone because it is negated twice (measured,
         * `x/transform_z.x`).
         */
        [[nodiscard]] Matrix Convert(const Matrix& m)
        {
            Matrix out = m;
            out.M13 = -m.M13;
            out.M23 = -m.M23;
            out.M31 = -m.M31;
            out.M32 = -m.M32;
            out.M43 = -m.M43;
            return out;
        }

        [[nodiscard]] const Canon::DirectXFileObject* Find(const Canon::DirectXFileObject& parent,
                                                           const std::string& type)
        {
            for (const Canon::DirectXFileObject& child : parent.children)
            {
                if (child.type == type)
                {
                    return &child;
                }
            }
            return nullptr;
        }

        /** @brief A matrix out of a `FrameTransformMatrix`'s sixteen numbers, row by row. */
        [[nodiscard]] Matrix ReadMatrix(const Canon::DirectXFileObject& object, const std::size_t at)
        {
            Matrix m;
            m.M11 = At(object, at + 0); m.M12 = At(object, at + 1);
            m.M13 = At(object, at + 2); m.M14 = At(object, at + 3);
            m.M21 = At(object, at + 4); m.M22 = At(object, at + 5);
            m.M23 = At(object, at + 6); m.M24 = At(object, at + 7);
            m.M31 = At(object, at + 8); m.M32 = At(object, at + 9);
            m.M33 = At(object, at + 10); m.M34 = At(object, at + 11);
            m.M41 = At(object, at + 12); m.M42 = At(object, at + 13);
            m.M43 = At(object, at + 14); m.M44 = At(object, at + 15);
            return m;
        }

        /** @brief Everything one `.x` file's importer needs to carry between its stages. */
        struct Importing
        {
            /** @brief Frames by name, for a SkinWeights or an Animation to reach. */
            std::map<std::string, std::shared_ptr<NodeContent>> framesByName;
            /** @brief Frames named by a SkinWeights, which is what makes a frame a bone. */
            std::set<std::string> boneNames;
            /** @brief Whether any mesh declared a skeleton. */
            bool hasSkeleton = false;
            /** @brief The file's own tick rate. */
            double ticksPerSecond = DefaultTicksPerSecond;
            /** @brief Where the source lives, for an external texture reference. */
            std::filesystem::path directory;
        };

        /** @brief One vertex's worth of bone weights, gathered before the channel is built. */
        using WeightsPerVertex = std::vector<std::vector<BoneWeight>>;

        void ReadMaterial(const Canon::DirectXFileObject& object, BasicMaterialContent& material,
                          const Importing& importing)
        {
            // Face colour (RGBA), power, specular (RGB), emissive (RGB): the template's own order.
            // The order matters: OpaqueData keeps what it was given in the order it was given,
            // and the genuine importer's order is diffuse, specular, emissive, alpha, power.
            material.setDiffuseColorProperty(Vector3(At(object, 0), At(object, 1), At(object, 2)));
            material.setSpecularColorProperty(Vector3(At(object, 5), At(object, 6), At(object, 7)));
            material.setEmissiveColorProperty(Vector3(At(object, 8), At(object, 9), At(object, 10)));
            material.setAlphaProperty(At(object, 3));
            material.setSpecularPowerProperty(At(object, 4));
            const Canon::DirectXFileObject* texture = Find(object, "TextureFilename");
            if (texture != nullptr && !texture->strings.empty())
            {
                // The reference is the file beside the source, which is where a `.x` names it.
                material.setTextureProperty(std::make_shared<ExternalReference<Graphics::TextureContent>>(
                    (importing.directory / texture->strings.front()).string()));
            }
        }

        /** @brief Turns one `Mesh` object into a MeshContent, with every channel XNA fills. */
        [[nodiscard]] std::shared_ptr<MeshContent> ReadMesh(const Canon::DirectXFileObject& object,
                                                            Importing& importing)
        {
            auto mesh = std::make_shared<MeshContent>();
            mesh->setNameProperty(object.name);

            std::size_t at = 0u;
            const std::size_t vertexCount = Count(object, at++);
            std::vector<Vector3> positions;
            positions.reserve(vertexCount);
            for (std::size_t i = 0; i < vertexCount; ++i)
            {
                positions.push_back(Convert(Vector3(At(object, at), At(object, at + 1), At(object, at + 2))));
                at += 3u;
            }
            for (const Vector3& position : positions)
            {
                mesh->getPositionsProperty().Add(position);
            }
            const std::size_t faceCount = Count(object, at++);
            std::vector<std::vector<std::size_t>> faces;
            faces.reserve(faceCount);
            for (std::size_t i = 0; i < faceCount; ++i)
            {
                const std::size_t corners = Count(object, at++);
                if (corners < 3u)
                {
                    throw InvalidContentException(Unreadable("E_FAIL"));
                }
                std::vector<std::size_t> face;
                face.reserve(corners);
                for (std::size_t corner = 0; corner < corners; ++corner)
                {
                    const std::size_t index = Count(object, at++);
                    if (index >= vertexCount)
                    {
                        // A face naming a vertex the mesh does not have: the genuine reader
                        // answers E_FAIL for exactly this (measured, x/index_out_of_range.x).
                        throw InvalidContentException(Unreadable("E_FAIL"));
                    }
                    face.push_back(index);
                }
                faces.push_back(std::move(face));
            }

            // The optional channels, each indexed by the mesh's own vertices.
            std::vector<Vector3> normals;
            if (const Canon::DirectXFileObject* object2 = Find(object, "MeshNormals"); object2 != nullptr)
            {
                std::size_t normalAt = 0u;
                const std::size_t count = Count(*object2, normalAt++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    normals.push_back(Convert(
                        Vector3(At(*object2, normalAt), At(*object2, normalAt + 1), At(*object2, normalAt + 2))));
                    normalAt += 3u;
                }
            }
            std::vector<Vector2> textureCoordinates;
            if (const Canon::DirectXFileObject* object2 = Find(object, "MeshTextureCoords");
                object2 != nullptr)
            {
                std::size_t uvAt = 0u;
                const std::size_t count = Count(*object2, uvAt++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    textureCoordinates.emplace_back(At(*object2, uvAt), At(*object2, uvAt + 1));
                    uvAt += 2u;
                }
            }
            std::vector<Vector4> colors;
            if (const Canon::DirectXFileObject* object2 = Find(object, "MeshVertexColors");
                object2 != nullptr)
            {
                std::size_t colorAt = 0u;
                const std::size_t count = Count(*object2, colorAt++);
                colors.assign(vertexCount, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t index = Count(*object2, colorAt++);
                    // A colour reaches the pipeline through eight bits per channel, which is what
                    // makes 0.5 come back as 0.501961 (measured, x/quad_textured.x).
                    const auto quantize = [](const float value)
                    {
                        const float clamped = std::min(1.0f, std::max(0.0f, value));
                        return static_cast<float>(static_cast<int>(clamped * 255.0f + 0.5f)) / 255.0f;
                    };
                    const Vector4 color(quantize(At(*object2, colorAt)), quantize(At(*object2, colorAt + 1)),
                                        quantize(At(*object2, colorAt + 2)), quantize(At(*object2, colorAt + 3)));
                    colorAt += 4u;
                    if (index < colors.size())
                    {
                        colors[index] = color;
                    }
                }
            }

            // Skinning: every SkinWeights names a frame and the vertices it moves.
            // SkinWeights are read only where the mesh declares a skeleton: without an
            // XSkinMeshHeader the genuine importer answers no Weights channel at all and leaves
            // the frames plain nodes (measured, x/two_bones_animated.x against
            // x/skinned_two_animations.x, which differ in nothing else).
            WeightsPerVertex weights;
            const bool skeleton = Find(object, "XSkinMeshHeader") != nullptr;
            if (skeleton)
            {
                importing.hasSkeleton = true;
            }
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (!skeleton)
                {
                    break;
                }
                if (child.type != "SkinWeights" || child.strings.empty())
                {
                    continue;
                }
                if (weights.empty())
                {
                    weights.assign(vertexCount, {});
                }
                const std::string bone = child.strings.front();
                importing.boneNames.insert(bone);
                std::size_t weightAt = 0u;
                const std::size_t count = Count(child, weightAt++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const std::size_t vertex = Count(child, weightAt + i);
                    const float weight = At(child, weightAt + count + i);
                    // A weight of zero is not carried: the genuine importer drops it rather than
                    // answering a bone that moves the vertex not at all.
                    if (vertex < weights.size() && weight != 0.0f)
                    {
                        weights[vertex].emplace_back(bone, weight);
                    }
                }
            }

            // One batch per material, or one batch for the whole mesh where the file names none.
            std::vector<std::size_t> materialPerFace(faces.size(), 0u);
            std::vector<std::shared_ptr<BasicMaterialContent>> materials;
            if (const Canon::DirectXFileObject* list = Find(object, "MeshMaterialList"); list != nullptr)
            {
                std::size_t listAt = 0u;
                const std::size_t materialCount = Count(*list, listAt++);
                const std::size_t indexCount = Count(*list, listAt++);
                for (std::size_t i = 0; i < indexCount && i < materialPerFace.size(); ++i)
                {
                    materialPerFace[i] = Count(*list, listAt + i);
                }
                for (const Canon::DirectXFileObject& child : list->children)
                {
                    if (child.type != "Material")
                    {
                        continue;
                    }
                    auto material = std::make_shared<BasicMaterialContent>();
                    ReadMaterial(child, *material, importing);
                    materials.push_back(std::move(material));
                }
                if (materials.size() != materialCount && !materials.empty())
                {
                    // The list's own count disagreeing with the materials it holds is a file that
                    // parsed but does not describe itself.
                    throw InvalidContentException(Unreadable("E_FAIL"));
                }
            }
            const std::size_t batches = materials.empty() ? 1u : materials.size();

            for (std::size_t batch = 0; batch < batches; ++batch)
            {
                // A batch's vertices are the mesh positions its own faces name, in first-use
                // order, which is what makes its indices local and its position indices shared.
                std::vector<std::size_t> used;
                std::map<std::size_t, SharpRuntime::intcs> local;
                std::vector<SharpRuntime::intcs> indices;
                for (std::size_t face = 0; face < faces.size(); ++face)
                {
                    if (!materials.empty() && materialPerFace[face] != batch)
                    {
                        continue;
                    }
                    // A polygon becomes a triangle fan, which is how every reader of this format
                    // turns an n-gon into triangles.
                    std::vector<SharpRuntime::intcs> corners;
                    for (const std::size_t vertex : faces[face])
                    {
                        const auto found = local.find(vertex);
                        if (found == local.end())
                        {
                            const auto assigned = static_cast<SharpRuntime::intcs>(used.size());
                            local.emplace(vertex, assigned);
                            used.push_back(vertex);
                            corners.push_back(assigned);
                        }
                        else
                        {
                            corners.push_back(found->second);
                        }
                    }
                    for (std::size_t corner = 2; corner < corners.size(); ++corner)
                    {
                        indices.push_back(corners[0]);
                        indices.push_back(corners[corner - 1u]);
                        indices.push_back(corners[corner]);
                    }
                }
                if (used.empty())
                {
                    continue;
                }
                auto geometry = std::make_shared<GeometryContent>();
                mesh->getGeometryProperty().Add(geometry);
                std::vector<SharpRuntime::intcs> positionIndices;
                positionIndices.reserve(used.size());
                for (const std::size_t vertex : used)
                {
                    positionIndices.push_back(static_cast<SharpRuntime::intcs>(vertex));
                }
                geometry->getVerticesProperty().AddRange(positionIndices);
                geometry->getIndicesProperty().AddRange(indices);
                if (batch < materials.size())
                {
                    geometry->setMaterialProperty(materials[batch]);
                }

                // The channel order the genuine importer answers: weights, normals, colours, then
                // texture coordinates.
                if (!weights.empty())
                {
                    std::vector<BoneWeightCollection> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        BoneWeightCollection collection;
                        for (const BoneWeight& weight : weights[vertex])
                        {
                            collection.Add(weight);
                        }
                        channel.push_back(std::move(collection));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<BoneWeightCollection>(
                        VertexChannelNames::Weights(0), channel);
                }
                // A file's own normals take their place among the channels the file declares; a
                // channel the importer *generates* because the file has none is appended after
                // them. Measured both ways: `quad_textured.x` declares MeshNormals and answers
                // Normal, Color, TextureCoordinate, while `two_textures.x` declares none and
                // answers TextureCoordinate, Normal (tests/reference/xna40/model, x/*). The order
                // is not cosmetic -- it is the order of the elements in the vertex buffer
                // (plans/plan_xnapipeline_parity.md XNAPP-266).
                const auto addNormals = [&]
                {
                    std::vector<Vector3> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        // A file with no MeshNormals still answers a normal channel: the genuine
                        // importer generates one (measured, x/bare_mesh.x).
                        channel.push_back(vertex < normals.size() ? normals[vertex]
                                                                  : Vector3(0.0f, 0.0f, -1.0f));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<Vector3>(
                        VertexChannelNames::Normal(), channel);
                };
                if (!normals.empty()) { addNormals(); }
                if (!colors.empty())
                {
                    std::vector<Vector4> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        channel.push_back(vertex < colors.size() ? colors[vertex]
                                                                 : Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<Vector4>(
                        VertexChannelNames::Color(0), channel);
                }
                if (!textureCoordinates.empty())
                {
                    std::vector<Vector2> channel;
                    channel.reserve(used.size());
                    for (const std::size_t vertex : used)
                    {
                        channel.push_back(vertex < textureCoordinates.size() ? textureCoordinates[vertex]
                                                                            : Vector2(0.0f, 0.0f));
                    }
                    geometry->getVerticesProperty().getChannelsProperty().Add<Vector2>(
                        VertexChannelNames::TextureCoordinate(0), channel);
                }
                if (normals.empty()) { addNormals(); }
            }
            return mesh;
        }

        void ReadFrame(const Canon::DirectXFileObject& object, const std::shared_ptr<NodeContent>& node,
                       Importing& importing)
        {
            node->setNameProperty(object.name);
            importing.framesByName[object.name] = node;
            // A frame's meshes come before its child frames, whatever order the file wrote them
            // in (measured: a file declaring Frame Bone0 then Mesh Skin answers Skin first).
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.type == "FrameTransformMatrix")
                {
                    node->setTransformProperty(Convert(ReadMatrix(child, 0u)));
                }
            }
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.type == "Mesh")
                {
                    node->getChildrenProperty().Add(ReadMesh(child, importing));
                }
            }
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.type == "Frame")
                {
                    auto sub = std::make_shared<NodeContent>();
                    ReadFrame(child, sub, importing);
                    node->getChildrenProperty().Add(sub);
                }
            }
        }

        /** @brief Replaces a frame with a BoneContent carrying the same state, in place. */
        [[nodiscard]] std::shared_ptr<NodeContent> AsBone(const std::shared_ptr<NodeContent>& node)
        {
            auto bone = std::make_shared<BoneContent>();
            bone->setNameProperty(node->getNameProperty());
            bone->setTransformProperty(node->getTransformProperty());
            bone->setIdentityProperty(node->getIdentityProperty());
            while (node->getChildrenProperty().getCountProperty() > 0)
            {
                const std::shared_ptr<NodeContent> child =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<NodeContent>>&>(node->getChildrenProperty())[0];
                node->getChildrenProperty().RemoveAt(0);
                bone->getChildrenProperty().Add(child);
            }
            return bone;
        }

        /** @brief Turns every frame a SkinWeights named, and its ancestors' subtree, into bones. */
        void PromoteBones(const std::shared_ptr<NodeContent>& node, Importing& importing)
        {
            auto& children = node->getChildrenProperty();
            for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
            {
                const std::shared_ptr<NodeContent> child =
                    static_cast<const System::Collections::ObjectModel::Collection<
                        std::shared_ptr<NodeContent>>&>(children)[i];
                const bool isBone = importing.boneNames.count(child->getNameProperty()) != 0;
                if (isBone && std::dynamic_pointer_cast<MeshContent>(child) == nullptr &&
                    std::dynamic_pointer_cast<BoneContent>(child) == nullptr)
                {
                    const std::shared_ptr<NodeContent> bone = AsBone(child);
                    children.RemoveAt(i);
                    children.Insert(i, bone);
                    importing.framesByName[bone->getNameProperty()] = bone;
                    PromoteBones(bone, importing);
                    continue;
                }
                PromoteBones(child, importing);
            }
        }

        /** @brief The topmost bone of the skeleton, or null when the file declared none. */
        [[nodiscard]] std::shared_ptr<NodeContent> FindSkeletonRoot(const std::shared_ptr<NodeContent>& node)
        {
            if (std::dynamic_pointer_cast<BoneContent>(node) != nullptr)
            {
                return node;
            }
            const auto& children = static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<NodeContent>>&>(node->getChildrenProperty());
            for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
            {
                if (const std::shared_ptr<NodeContent> found = FindSkeletonRoot(children[i]); found != nullptr)
                {
                    return found;
                }
            }
            return nullptr;
        }

        /** @brief One `Animation` object: the target it names and the keys it holds. */
        struct ReadAnimation
        {
            std::string target;
            std::map<double, Matrix> keys;
            double lastTick = 0.0;
        };

        /**
         * @brief Merges an `Animation`'s separate key lists into one matrix track.
         *
         * A `.x` animation stores rotation, scale and position as three key lists with times of
         * their own; XNA answers a single channel of matrices at the union of those times
         * (measured, x/skinned_animated.x, whose two lists of three and two keys become one
         * channel of three). Each component is held at the last key at or before the time, which
         * is what makes the merged track agree with each list where they share a time.
         */
        [[nodiscard]] ReadAnimation ReadOneAnimation(const Canon::DirectXFileObject& object)
        {
            ReadAnimation animation;
            if (!object.references.empty())
            {
                animation.target = object.references.front();
            }
            std::map<double, Quaternion> rotations;
            std::map<double, Vector3> scales;
            std::map<double, Vector3> positions;
            std::map<double, Matrix> matrices;
            std::set<double> times;
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.type == "Frame" && animation.target.empty())
                {
                    animation.target = child.name;
                    continue;
                }
                if (child.type != "AnimationKey")
                {
                    continue;
                }
                std::size_t at = 0u;
                const std::size_t kind = Count(child, at++);
                const std::size_t count = Count(child, at++);
                for (std::size_t i = 0; i < count; ++i)
                {
                    const double time = At(child, at++);
                    const std::size_t values = Count(child, at++);
                    times.insert(time);
                    animation.lastTick = std::max(animation.lastTick, time);
                    if (kind == 0u && values >= 4u)
                    {
                        // A `.x` rotation key is w, x, y, z, and the rotation it names is the
                        // inverse of the quaternion those spell.
                        rotations[time] = Quaternion(-At(child, at + 1), -At(child, at + 2),
                                                     -At(child, at + 3), At(child, at));
                    }
                    else if (kind == 1u && values >= 3u)
                    {
                        scales[time] = Vector3(At(child, at), At(child, at + 1), At(child, at + 2));
                    }
                    else if (kind == 2u && values >= 3u)
                    {
                        positions[time] = Vector3(At(child, at), At(child, at + 1), At(child, at + 2));
                    }
                    else if (kind == 4u && values >= 16u)
                    {
                        matrices[time] = ReadMatrix(child, at);
                    }
                    at += values;
                }
            }
            // Each component is *interpolated* at the union time, not held: a rotation list with
            // keys at 0 and 20 and a position list with keys at 0, 10 and 20 answer a rotation
            // half way through at the merged time 10 (measured, x/skinned_animated.x, whose
            // middle key carries a 45-degree rotation the rotation list never states).
            const auto sample = [](const auto& track, const double time, const auto fallback,
                                   const auto& blend)
            {
                if (track.empty())
                {
                    return fallback;
                }
                auto after = track.lower_bound(time);
                if (after != track.end() && after->first == time)
                {
                    return after->second;
                }
                if (after == track.begin())
                {
                    return track.begin()->second;
                }
                if (after == track.end())
                {
                    return std::prev(track.end())->second;
                }
                auto before = std::prev(after);
                const double span = after->first - before->first;
                const float amount = span > 0.0 ? static_cast<float>((time - before->first) / span) : 0.0f;
                return blend(before->second, after->second, amount);
            };
            const auto lerp3 = [](const Vector3& from, const Vector3& to, const float amount)
            { return Vector3::Lerp(from, to, amount); };
            const auto slerp = [](const Quaternion& from, const Quaternion& to, const float amount)
            { return Quaternion::Slerp(from, to, amount); };
            const auto lerpMatrix = [](const Matrix& from, const Matrix& to, const float amount)
            { return amount < 0.5f ? from : to; };

            for (const double time : times)
            {
                if (!matrices.empty())
                {
                    animation.keys[time] =
                        Convert(sample(matrices, time, Matrix::getIdentityProperty(), lerpMatrix));
                    continue;
                }
                const Quaternion rotation = sample(rotations, time, Quaternion::Identity, slerp);
                const Vector3 scale = sample(scales, time, Vector3(1.0f, 1.0f, 1.0f), lerp3);
                const Vector3 position = sample(positions, time, Vector3(0.0f, 0.0f, 0.0f), lerp3);
                Matrix transform = Matrix::CreateScale(scale) *
                                   Matrix::CreateFromQuaternion(rotation) *
                                   Matrix::CreateTranslation(position);
                animation.keys[time] = Convert(transform);
            }
            return animation;
        }

        void ReadAnimationSet(const Canon::DirectXFileObject& object,
                              const std::shared_ptr<NodeContent>& root, Importing& importing)
        {
            std::vector<ReadAnimation> animations;
            double lastTick = 0.0;
            for (const Canon::DirectXFileObject& child : object.children)
            {
                if (child.type != "Animation")
                {
                    continue;
                }
                ReadAnimation one = ReadOneAnimation(child);
                lastTick = std::max(lastTick, one.lastTick);
                animations.push_back(std::move(one));
            }
            if (animations.empty())
            {
                return;
            }
            const double rate = importing.ticksPerSecond > 0.0 ? importing.ticksPerSecond
                                                               : DefaultTicksPerSecond;
            const auto toTicks = [rate](const double tick)
            {
                return static_cast<SharpRuntime::longcs>(tick / rate * 10000000.0);
            };
            // The duration is the last key's time truncated to whole milliseconds, while the keys
            // keep their full precision (measured: 20 ticks at the default rate is 41666 ticks of
            // key time and a duration of 40000).
            const System::TimeSpan duration(toTicks(lastTick) / 10000 * 10000);

            // Where the file declares a skeleton, every animation in the set lands on the
            // skeleton's root bone as one AnimationContent with a channel per target; where it
            // does not, each animation lands on the node it names (measured,
            // x/skinned_two_animations.x against x/two_bones_animated.x).
            const std::shared_ptr<NodeContent> skeleton =
                importing.hasSkeleton ? FindSkeletonRoot(root) : nullptr;
            const auto channelFor = [&](const ReadAnimation& one) -> std::shared_ptr<AnimationChannel>
            {
                auto channel = std::make_shared<AnimationChannel>();
                for (const auto& [time, transform] : one.keys)
                {
                    channel->Add(std::make_shared<AnimationKeyframe>(System::TimeSpan(toTicks(time)),
                                                                     transform));
                }
                return channel;
            };
            if (skeleton != nullptr)
            {
                auto content = std::make_shared<AnimationContent>();
                content->setNameProperty(object.name);
                content->setDurationProperty(duration);
                for (const ReadAnimation& one : animations)
                {
                    content->getChannelsProperty().Add(one.target, channelFor(one));
                }
                skeleton->getAnimationsProperty().Add(object.name, content);
                return;
            }
            for (const ReadAnimation& one : animations)
            {
                const auto found = importing.framesByName.find(one.target);
                if (found == importing.framesByName.end())
                {
                    continue;
                }
                auto content = std::make_shared<AnimationContent>();
                content->setNameProperty(object.name);
                content->setDurationProperty(duration);
                content->getChannelsProperty().Add(one.target, channelFor(one));
                found->second->getAnimationsProperty().Add(object.name, content);
            }
        }
    }

    XImporter::~XImporter() { Dispose(false); }

    void XImporter::Dispose() { Dispose(true); }

    void XImporter::Dispose(const bool disposing)
    {
        (void)disposing;
        // Nothing native is held open: the file is read whole and closed inside Import. The
        // pattern is here because XNA declares it, and calling it twice is accepted.
        disposed_ = true;
    }

    std::shared_ptr<Graphics::NodeContent> XImporter::Import(const std::string& filename,
                                                             ContentImporterContext& context)
    {
        (void)context;
        std::error_code error;
        if (!std::filesystem::exists(filename, error) || error)
        {
            throw System::IO::FileNotFoundException("Could not locate model file \"" + filename + "\".");
        }
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream file(filename, std::ios::binary);
            const std::vector<char> read((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
            bytes.assign(read.begin(), read.end());
        }
        Canon::DirectXFile parsed;
        try
        {
            parsed = Canon::ReadDirectXFile(bytes);
        }
        catch (const Canon::DirectXFileException& failure)
        {
            throw InvalidContentException(Unreadable(failure.CodeName()));
        }

        Importing importing;
        importing.directory = std::filesystem::path(filename).parent_path();
        for (const Canon::DirectXFileObject& object : parsed.objects)
        {
            if (object.type == "AnimTicksPerSecond" && !object.numbers.empty())
            {
                importing.ticksPerSecond = object.numbers.front();
            }
        }

        // A file whose single top-level object is a Frame answers that frame as the root; any
        // other shape answers an unnamed root holding the objects (measured, x/quad_textured.x
        // against x/bare_mesh.x).
        std::size_t topLevelFrames = 0u;
        std::size_t topLevelData = 0u;
        for (const Canon::DirectXFileObject& object : parsed.objects)
        {
            if (object.type == "Frame") { ++topLevelFrames; }
            if (object.type == "Frame" || object.type == "Mesh") { ++topLevelData; }
        }
        auto root = std::make_shared<NodeContent>();
        if (topLevelFrames == 1u && topLevelData == 1u)
        {
            for (const Canon::DirectXFileObject& object : parsed.objects)
            {
                if (object.type == "Frame")
                {
                    ReadFrame(object, root, importing);
                }
            }
        }
        else
        {
            for (const Canon::DirectXFileObject& object : parsed.objects)
            {
                if (object.type == "Frame")
                {
                    auto child = std::make_shared<NodeContent>();
                    ReadFrame(object, child, importing);
                    root->getChildrenProperty().Add(child);
                }
                else if (object.type == "Mesh")
                {
                    root->getChildrenProperty().Add(ReadMesh(object, importing));
                }
            }
        }
        PromoteBones(root, importing);
        if (importing.hasSkeleton && std::dynamic_pointer_cast<BoneContent>(root) == nullptr &&
            importing.boneNames.count(root->getNameProperty()) != 0)
        {
            // A skeleton whose root is the file's own root frame stays a NodeContent, because a
            // root cannot be replaced in place; nothing measured shows XNA doing otherwise.
        }
        for (const Canon::DirectXFileObject& object : parsed.objects)
        {
            if (object.type == "AnimationSet")
            {
                ReadAnimationSet(object, root, importing);
            }
        }
        return root;
    }

    ContentImporterAttribute XImporter::Attribute()
    {
        ContentImporterAttribute attribute(".x");
        attribute.setDefaultProcessorProperty("ModelProcessor");
        attribute.setDisplayNameProperty("X File - XNA Framework");
        attribute.setCacheImportedDataProperty(true);
        return attribute;
    }

    const std::string& XImporter::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
