// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        using IntCollection = System::Collections::ObjectModel::Collection<SharpRuntime::intcs>;

        /** @brief The geometry batches of a mesh, read through the collection's index. */
        [[nodiscard]] std::vector<std::shared_ptr<GeometryContent>> BatchesOf(const MeshContent& mesh)
        {
            const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<GeometryContent>>&>(mesh.getGeometryProperty());
            std::vector<std::shared_ptr<GeometryContent>> result;
            for (SharpRuntime::intcs i = 0; i < batches.getCountProperty(); ++i)
            {
                result.push_back(batches[i]);
            }
            return result;
        }

        /** @brief The children of a node, read through the collection's index. */
        [[nodiscard]] std::vector<std::shared_ptr<NodeContent>> ChildrenOf(const NodeContent& node)
        {
            const auto& children = static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<NodeContent>>&>(node.getChildrenProperty());
            std::vector<std::shared_ptr<NodeContent>> result;
            for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
            {
                result.push_back(children[i]);
            }
            return result;
        }

        /** @brief The triangle indices of a batch, as a plain list. */
        [[nodiscard]] std::vector<SharpRuntime::intcs> IndicesOf(const GeometryContent& geometry)
        {
            const auto& indices = static_cast<const IntCollection&>(geometry.getIndicesProperty());
            std::vector<SharpRuntime::intcs> result;
            for (SharpRuntime::intcs i = 0; i < indices.getCountProperty(); ++i)
            {
                result.push_back(indices[i]);
            }
            return result;
        }

        /** @brief Replaces a batch's triangle indices. */
        void SetIndices(GeometryContent& geometry, const std::vector<SharpRuntime::intcs>& indices)
        {
            geometry.getIndicesProperty().Clear();
            for (const SharpRuntime::intcs index : indices)
            {
                geometry.getIndicesProperty().Add(index);
            }
        }

        /** @brief The position index of each vertex of a batch. */
        [[nodiscard]] std::vector<SharpRuntime::intcs> PositionIndicesOf(const GeometryContent& geometry)
        {
            const VertexChannel<SharpRuntime::intcs>& channel =
                geometry.getVerticesProperty().getPositionIndicesProperty();
            std::vector<SharpRuntime::intcs> result;
            for (SharpRuntime::intcs i = 0; i < channel.getCountProperty(); ++i)
            {
                result.push_back(channel.At(i));
            }
            return result;
        }

        /** @brief The mesh's positions, as a plain list. */
        [[nodiscard]] std::vector<Vector3> PositionsOf(const MeshContent& mesh)
        {
            const auto& positions =
                static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(
                    mesh.getPositionsProperty());
            std::vector<Vector3> result;
            for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
            {
                result.push_back(positions[i]);
            }
            return result;
        }

        /** @brief The channels of a batch, in order. */
        [[nodiscard]] std::vector<std::shared_ptr<VertexChannelBase>> ChannelsOf(const GeometryContent& geometry)
        {
            std::vector<std::shared_ptr<VertexChannelBase>> result;
            const VertexChannelCollection& channels = geometry.getVerticesProperty().getChannelsProperty();
            for (SharpRuntime::intcs i = 0; i < channels.getCountProperty(); ++i)
            {
                result.push_back(channels[i]);
            }
            return result;
        }
    }

    void MeshHelper::CalculateNormals(const std::shared_ptr<MeshContent>& mesh, bool overwriteExistingNormals)
    {
        if (mesh == nullptr)
        {
            throw System::ArgumentNullException("mesh");
        }
        const std::string normalName = VertexChannelNames::Normal();
        const std::vector<Vector3> positions = PositionsOf(*mesh);
        // A vertex normal is the sum of the faces meeting at its *position*, which is why two
        // vertices of a texture seam come out with the same normal (measured,
        // meshhelper/calculate_normals_shared_positions).
        std::vector<Vector3> sums(positions.size(), Vector3(0, 0, 0));
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            const std::vector<SharpRuntime::intcs> vertexPositions = PositionIndicesOf(*geometry);
            const std::vector<SharpRuntime::intcs> indices = IndicesOf(*geometry);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                std::array<SharpRuntime::intcs, 3> corner{};
                bool usable = true;
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const SharpRuntime::intcs vertex = indices[i + k];
                    if (vertex < 0 || static_cast<std::size_t>(vertex) >= vertexPositions.size())
                    {
                        usable = false;
                        break;
                    }
                    corner[k] = vertexPositions[static_cast<std::size_t>(vertex)];
                    if (corner[k] < 0 || static_cast<std::size_t>(corner[k]) >= positions.size())
                    {
                        usable = false;
                        break;
                    }
                }
                if (!usable)
                {
                    continue;
                }
                const Vector3& a = positions[static_cast<std::size_t>(corner[0])];
                const Vector3& b = positions[static_cast<std::size_t>(corner[1])];
                const Vector3& c = positions[static_cast<std::size_t>(corner[2])];
                // The face normal is the clockwise one: a triangle wound counter-clockwise in the
                // XY plane answers -Z (measured, meshhelper/calculate_normals).
                const Vector3 face = Vector3::Cross(Vector3::Subtract(c, a), Vector3::Subtract(b, a));
                for (const SharpRuntime::intcs position : corner)
                {
                    sums[static_cast<std::size_t>(position)] =
                        Vector3::Add(sums[static_cast<std::size_t>(position)], face);
                }
            }
        }
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            VertexChannelCollection& channels = geometry->getVerticesProperty().getChannelsProperty();
            if (channels.Contains(normalName))
            {
                if (!overwriteExistingNormals)
                {
                    continue;
                }
                // Overwriting removes the channel and adds it again, which is why it moves to the
                // end of the list (measured, meshhelper/calculate_normals_overwrite).
                (void)channels.Remove(normalName);
            }
            const std::vector<SharpRuntime::intcs> vertexPositions = PositionIndicesOf(*geometry);
            std::vector<Vector3> normals;
            normals.reserve(vertexPositions.size());
            for (const SharpRuntime::intcs position : vertexPositions)
            {
                Vector3 normal = position >= 0 && static_cast<std::size_t>(position) < sums.size()
                                     ? sums[static_cast<std::size_t>(position)]
                                     : Vector3(0, 0, 0);
                if (normal.LengthSquared() > 0.0f)
                {
                    normal.Normalize();
                }
                normals.push_back(normal);
            }
            (void)channels.Add<Vector3>(normalName, std::move(normals));
        }
    }

    void MeshHelper::CalculateTangentFrames(const std::shared_ptr<MeshContent>& mesh,
                                            const std::string& textureCoordinateChannelName,
                                            const std::string& tangentChannelName,
                                            const std::string& binormalChannelName)
    {
        if (mesh == nullptr)
        {
            throw System::ArgumentNullException("mesh");
        }
        const std::vector<Vector3> positions = PositionsOf(*mesh);
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            VertexChannelCollection& channels = geometry->getVerticesProperty().getChannelsProperty();
            if (!channels.Contains(textureCoordinateChannelName))
            {
                throw InvalidContentException("Required vertex channel \"" + textureCoordinateChannelName +
                                              "\" not found.");
            }
            const std::shared_ptr<VertexChannel<Vector2>> texCoords =
                channels.Get<Vector2>(textureCoordinateChannelName);
            const std::shared_ptr<VertexChannel<Vector3>> normals =
                channels.Contains(VertexChannelNames::Normal())
                    ? channels.Get<Vector3>(VertexChannelNames::Normal())
                    : nullptr;
            const std::vector<SharpRuntime::intcs> vertexPositions = PositionIndicesOf(*geometry);
            const std::vector<SharpRuntime::intcs> indices = IndicesOf(*geometry);
            const std::size_t count = vertexPositions.size();
            std::vector<Vector3> tangents(count, Vector3(0, 0, 0));
            std::vector<Vector3> faces(count, Vector3(0, 0, 0));
            const auto positionOf = [&](SharpRuntime::intcs vertex)
            {
                const SharpRuntime::intcs position = vertexPositions[static_cast<std::size_t>(vertex)];
                return position >= 0 && static_cast<std::size_t>(position) < positions.size()
                           ? positions[static_cast<std::size_t>(position)]
                           : Vector3(0, 0, 0);
            };
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const SharpRuntime::intcs a = indices[i];
                const SharpRuntime::intcs b = indices[i + 1];
                const SharpRuntime::intcs c = indices[i + 2];
                if (a < 0 || b < 0 || c < 0 || static_cast<std::size_t>(a) >= count ||
                    static_cast<std::size_t>(b) >= count || static_cast<std::size_t>(c) >= count)
                {
                    continue;
                }
                const Vector3 edge1 = Vector3::Subtract(positionOf(b), positionOf(a));
                const Vector3 edge2 = Vector3::Subtract(positionOf(c), positionOf(a));
                const Vector2 delta1 = Vector2::Subtract(texCoords->At(b), texCoords->At(a));
                const Vector2 delta2 = Vector2::Subtract(texCoords->At(c), texCoords->At(a));
                const Vector3 face = Vector3::Cross(edge2, edge1);
                for (const SharpRuntime::intcs vertex : {a, b, c})
                {
                    faces[static_cast<std::size_t>(vertex)] =
                        Vector3::Add(faces[static_cast<std::size_t>(vertex)], face);
                }
                const SharpRuntime::Single determinant = delta1.X * delta2.Y - delta2.X * delta1.Y;
                if (determinant == 0.0f)
                {
                    continue;
                }
                const Vector3 tangent = Vector3::Multiply(
                    Vector3::Subtract(Vector3::Multiply(edge1, delta2.Y), Vector3::Multiply(edge2, delta1.Y)),
                    1.0f / determinant);
                for (const SharpRuntime::intcs vertex : {a, b, c})
                {
                    tangents[static_cast<std::size_t>(vertex)] =
                        Vector3::Add(tangents[static_cast<std::size_t>(vertex)], tangent);
                }
            }
            std::vector<Vector3> tangentChannel(count);
            std::vector<Vector3> binormalChannel(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                Vector3 normal = normals != nullptr ? normals->At(static_cast<SharpRuntime::intcs>(i)) : faces[i];
                if (normal.LengthSquared() > 0.0f)
                {
                    normal.Normalize();
                }
                // The tangent is made perpendicular to the normal before it is stored, which keeps
                // the frame orthogonal where two triangles disagree.
                Vector3 tangent = Vector3::Subtract(
                    tangents[i], Vector3::Multiply(normal, Vector3::Dot(normal, tangents[i])));
                if (tangent.LengthSquared() > 0.0f)
                {
                    tangent.Normalize();
                }
                Vector3 binormal = Vector3::Cross(normal, tangent);
                if (binormal.LengthSquared() > 0.0f)
                {
                    binormal.Normalize();
                }
                tangentChannel[i] = tangent;
                binormalChannel[i] = binormal;
            }
            // A name left empty asks for that half of the frame not to be written at all
            // (measured, meshhelper/calculate_tangent_frames_refusals accepts two null names).
            if (!tangentChannelName.empty() && !channels.Contains(tangentChannelName))
            {
                (void)channels.Add<Vector3>(tangentChannelName, std::move(tangentChannel));
            }
            if (!binormalChannelName.empty() && !channels.Contains(binormalChannelName))
            {
                (void)channels.Add<Vector3>(binormalChannelName, std::move(binormalChannel));
            }
        }
    }

    std::shared_ptr<BoneContent> MeshHelper::FindSkeleton(const std::shared_ptr<NodeContent>& node)
    {
        if (node == nullptr)
        {
            throw System::ArgumentNullException("node");
        }
        // The search starts at the scene root, so any node of a scene answers that scene's own
        // skeleton (measured, meshhelper/skeleton answers Skeleton from a grandchild bone).
        NodeContent* root = node.get();
        while (root->getParentProperty() != nullptr)
        {
            root = root->getParentProperty();
        }
        std::shared_ptr<BoneContent> found;
        const std::function<bool(const std::shared_ptr<NodeContent>&)> walk =
            [&](const std::shared_ptr<NodeContent>& current)
        {
            if (const auto bone = std::dynamic_pointer_cast<BoneContent>(current))
            {
                found = bone;
                return true;
            }
            for (const std::shared_ptr<NodeContent>& child : ChildrenOf(*current))
            {
                if (walk(child))
                {
                    return true;
                }
            }
            return false;
        };
        if (const auto rootBone = std::dynamic_pointer_cast<BoneContent>(node); rootBone != nullptr &&
                                                                                root == node.get())
        {
            return rootBone;
        }
        for (const std::shared_ptr<NodeContent>& child : ChildrenOf(*root))
        {
            if (walk(child))
            {
                return found;
            }
        }
        return nullptr;
    }

    std::vector<std::shared_ptr<BoneContent>> MeshHelper::FlattenSkeleton(
        const std::shared_ptr<BoneContent>& skeleton)
    {
        if (skeleton == nullptr)
        {
            throw System::ArgumentNullException("skeleton");
        }
        // Depth first, a parent before its children (measured, meshhelper/skeleton answers
        // Skeleton,A,A1,B).
        std::vector<std::shared_ptr<BoneContent>> bones;
        const std::function<void(const std::shared_ptr<BoneContent>&)> walk =
            [&](const std::shared_ptr<BoneContent>& bone)
        {
            bones.push_back(bone);
            for (const std::shared_ptr<NodeContent>& child : ChildrenOf(*bone))
            {
                if (const auto childBone = std::dynamic_pointer_cast<BoneContent>(child))
                {
                    walk(childBone);
                }
            }
        };
        walk(skeleton);
        return bones;
    }

    void MeshHelper::MergeDuplicatePositions(const std::shared_ptr<MeshContent>& mesh,
                                             SharpRuntime::Single tolerance)
    {
        if (mesh == nullptr)
        {
            throw System::ArgumentNullException("mesh");
        }
        const std::vector<Vector3> positions = PositionsOf(*mesh);
        std::vector<Vector3> kept;
        std::vector<SharpRuntime::intcs> remap(positions.size(), 0);
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            SharpRuntime::intcs match = -1;
            for (std::size_t k = 0; k < kept.size(); ++k)
            {
                if (Vector3::Distance(kept[k], positions[i]) <= tolerance)
                {
                    match = static_cast<SharpRuntime::intcs>(k);
                    break;
                }
            }
            if (match < 0)
            {
                match = static_cast<SharpRuntime::intcs>(kept.size());
                kept.push_back(positions[i]);
            }
            remap[i] = match;
        }
        if (kept.size() == positions.size())
        {
            return;
        }
        mesh->getPositionsProperty().Clear();
        for (const Vector3& position : kept)
        {
            mesh->getPositionsProperty().Add(position);
        }
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            VertexChannel<SharpRuntime::intcs>& channel =
                geometry->getVerticesProperty().getPositionIndicesProperty();
            for (SharpRuntime::intcs i = 0; i < channel.getCountProperty(); ++i)
            {
                const SharpRuntime::intcs old = channel.At(i);
                if (old >= 0 && static_cast<std::size_t>(old) < remap.size())
                {
                    channel.SetAt(i, remap[static_cast<std::size_t>(old)]);
                }
            }
        }
    }

    void MeshHelper::MergeDuplicateVertices(const std::shared_ptr<GeometryContent>& geometry)
    {
        if (geometry == nullptr)
        {
            throw System::ArgumentNullException("geometry");
        }
        const std::vector<SharpRuntime::intcs> vertexPositions = PositionIndicesOf(*geometry);
        const std::vector<std::shared_ptr<VertexChannelBase>> channels = ChannelsOf(*geometry);
        // Two vertices merge when they name the same position and every channel entry compares
        // equal (measured, meshhelper/merge_duplicate_vertices_real keeps the two that differ in
        // one texture coordinate).
        std::vector<SharpRuntime::intcs> kept;
        std::vector<SharpRuntime::intcs> remap(vertexPositions.size(), 0);
        for (std::size_t i = 0; i < vertexPositions.size(); ++i)
        {
            SharpRuntime::intcs match = -1;
            for (std::size_t k = 0; k < kept.size(); ++k)
            {
                const SharpRuntime::intcs candidate = kept[k];
                if (vertexPositions[static_cast<std::size_t>(candidate)] != vertexPositions[i])
                {
                    continue;
                }
                bool same = true;
                for (const std::shared_ptr<VertexChannelBase>& channel : channels)
                {
                    if (!channel->EntriesEqual(candidate, static_cast<SharpRuntime::intcs>(i)))
                    {
                        same = false;
                        break;
                    }
                }
                if (same)
                {
                    match = static_cast<SharpRuntime::intcs>(k);
                    break;
                }
            }
            if (match < 0)
            {
                match = static_cast<SharpRuntime::intcs>(kept.size());
                kept.push_back(static_cast<SharpRuntime::intcs>(i));
            }
            remap[i] = match;
        }
        if (kept.size() == vertexPositions.size())
        {
            return;
        }
        std::vector<SharpRuntime::intcs> indices = IndicesOf(*geometry);
        for (SharpRuntime::intcs& index : indices)
        {
            if (index >= 0 && static_cast<std::size_t>(index) < remap.size())
            {
                index = remap[static_cast<std::size_t>(index)];
            }
        }
        // The reorder wants a whole permutation, so the dropped vertices follow the kept ones and
        // are then taken off the end.
        std::vector<SharpRuntime::intcs> order = kept;
        std::vector<bool> keptFlags(vertexPositions.size(), false);
        for (const SharpRuntime::intcs index : kept)
        {
            keptFlags[static_cast<std::size_t>(index)] = true;
        }
        for (std::size_t i = 0; i < vertexPositions.size(); ++i)
        {
            if (!keptFlags[i])
            {
                order.push_back(static_cast<SharpRuntime::intcs>(i));
            }
        }
        geometry->getVerticesProperty().ReorderVertices(order);
        for (SharpRuntime::intcs i = static_cast<SharpRuntime::intcs>(vertexPositions.size()) - 1;
             i >= static_cast<SharpRuntime::intcs>(kept.size()); --i)
        {
            geometry->getVerticesProperty().RemoveAt(i);
        }
        SetIndices(*geometry, indices);
    }

    void MeshHelper::MergeDuplicateVertices(const std::shared_ptr<MeshContent>& mesh)
    {
        if (mesh == nullptr)
        {
            throw System::ArgumentNullException("mesh");
        }
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            MergeDuplicateVertices(geometry);
        }
    }

    void MeshHelper::OptimizeForCache(const std::shared_ptr<MeshContent>& mesh)
    {
        if (mesh == nullptr)
        {
            throw System::ArgumentNullException("mesh");
        }
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            const std::vector<SharpRuntime::intcs> indices = IndicesOf(*geometry);
            const std::size_t vertexCount =
                static_cast<std::size_t>(geometry->getVerticesProperty().getVertexCountProperty());
            // XNA's optimization takes the triangles in reverse and renumbers the vertices in the
            // order the reversed list first reaches them; a shuffled mesh comes out as the exact
            // reverse of the order it went in (measured, meshhelper/optimize_for_cache_grid and
            // optimize_for_cache_shuffled).
            std::vector<SharpRuntime::intcs> reordered;
            reordered.reserve(indices.size());
            for (std::size_t triangle = indices.size() / 3; triangle > 0; --triangle)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    reordered.push_back(indices[(triangle - 1) * 3 + k]);
                }
            }
            std::vector<SharpRuntime::intcs> order;
            std::vector<SharpRuntime::intcs> remap(vertexCount, -1);
            for (const SharpRuntime::intcs vertex : reordered)
            {
                if (vertex < 0 || static_cast<std::size_t>(vertex) >= vertexCount)
                {
                    continue;
                }
                if (remap[static_cast<std::size_t>(vertex)] < 0)
                {
                    remap[static_cast<std::size_t>(vertex)] = static_cast<SharpRuntime::intcs>(order.size());
                    order.push_back(vertex);
                }
            }
            // A vertex no triangle reaches keeps its place at the end rather than being dropped.
            for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
            {
                if (remap[vertex] < 0)
                {
                    remap[vertex] = static_cast<SharpRuntime::intcs>(order.size());
                    order.push_back(static_cast<SharpRuntime::intcs>(vertex));
                }
            }
            for (SharpRuntime::intcs& vertex : reordered)
            {
                if (vertex >= 0 && static_cast<std::size_t>(vertex) < vertexCount)
                {
                    vertex = remap[static_cast<std::size_t>(vertex)];
                }
            }
            geometry->getVerticesProperty().ReorderVertices(order);
            SetIndices(*geometry, reordered);
        }
    }

    void MeshHelper::SwapWindingOrder(const std::shared_ptr<MeshContent>& mesh)
    {
        if (mesh == nullptr)
        {
            throw System::ArgumentNullException("mesh");
        }
        for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
        {
            std::vector<SharpRuntime::intcs> indices = IndicesOf(*geometry);
            // The whole triangle is reversed, not just its last two corners (measured,
            // meshhelper/swap_winding_order answers 2,1,0 for 0,1,2).
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                std::swap(indices[i], indices[i + 2]);
            }
            SetIndices(*geometry, indices);
        }
    }

    void MeshHelper::TransformScene(const std::shared_ptr<NodeContent>& scene, const Matrix& transform)
    {
        if (scene == nullptr)
        {
            throw System::ArgumentNullException("scene");
        }
        const Matrix inverse = Matrix::Invert(transform);
        const std::function<void(const std::shared_ptr<NodeContent>&)> walk =
            [&](const std::shared_ptr<NodeContent>& node)
        {
            // The node's transform is re-expressed in the new frame, so the scene as a whole comes
            // out transformed exactly once (measured, meshhelper/transform_scene: a translation
            // moves and the rotation stays out of the matrix).
            node->setTransformProperty(inverse * node->getTransformProperty() * transform);
            if (const auto mesh = std::dynamic_pointer_cast<MeshContent>(node))
            {
                PositionCollection& positions = mesh->getPositionsProperty();
                const auto& reader =
                    static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(positions);
                for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
                {
                    positions.setItem(i, Vector3::Transform(reader[i], transform));
                }
                for (const std::shared_ptr<GeometryContent>& geometry : BatchesOf(*mesh))
                {
                    for (const std::shared_ptr<VertexChannelBase>& channel : ChannelsOf(*geometry))
                    {
                        const std::string base = VertexChannelNames::DecodeBaseName(channel->getNameProperty());
                        if (base != "Normal" && base != "Tangent" && base != "Binormal")
                        {
                            continue;
                        }
                        auto typed = std::dynamic_pointer_cast<VertexChannel<Vector3>>(channel);
                        if (typed == nullptr)
                        {
                            continue;
                        }
                        for (SharpRuntime::intcs i = 0; i < typed->getCountProperty(); ++i)
                        {
                            Vector3 value = Vector3::TransformNormal(typed->At(i), transform);
                            value.Normalize();
                            typed->SetAt(i, value);
                        }
                    }
                }
            }
            for (const std::shared_ptr<NodeContent>& child : ChildrenOf(*node))
            {
                walk(child);
            }
        };
        walk(scene);
    }
}
