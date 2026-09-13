// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Operations on the intermediate mesh types: normals, tangent frames, skeletons,
     *        merging, ordering and whole-scene transforms.
     */
    class MeshHelper final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshHelper";

        /** @brief The class is static; it has no instances. */
        MeshHelper() = delete;

        /**
         * @brief Computes a normal for every vertex, averaged over the faces meeting at its
         *        position.
         *
         * @param mesh The mesh whose geometry is read and whose normal channels are written.
         * @param overwriteExistingNormals true to replace a normal channel a batch already has;
         *        false to leave that batch's normals alone.
         * @throws System::ArgumentNullException when the mesh is null.
         */
        static void CalculateNormals(const std::shared_ptr<MeshContent>& mesh, bool overwriteExistingNormals);

        /**
         * @brief Computes a tangent and a binormal for every vertex from its texture coordinates.
         *
         * @param mesh The mesh to work on.
         * @param textureCoordinateChannelName The channel the frame is derived from.
         * @param tangentChannelName The channel to write tangents to, or empty to write none.
         * @param binormalChannelName The channel to write binormals to, or empty to write none.
         * @throws System::ArgumentNullException when the mesh is null.
         * @throws InvalidContentException when a batch has no such texture coordinate channel.
         */
        static void CalculateTangentFrames(const std::shared_ptr<MeshContent>& mesh,
                                           const std::string& textureCoordinateChannelName,
                                           const std::string& tangentChannelName,
                                           const std::string& binormalChannelName);

        /**
         * @brief Finds the root bone of the skeleton a node belongs to.
         *
         * @param node Any node of the scene.
         * @return The topmost bone, or null when the scene holds none.
         * @throws System::ArgumentNullException when the node is null.
         */
        [[nodiscard]] static std::shared_ptr<BoneContent> FindSkeleton(const std::shared_ptr<NodeContent>& node);

        /**
         * @brief Flattens a skeleton into a list, parents before their children.
         *
         * @param skeleton The root bone.
         * @return The bones in depth-first order, the root first.
         * @throws System::ArgumentNullException when the skeleton is null.
         */
        [[nodiscard]] static std::vector<std::shared_ptr<BoneContent>> FlattenSkeleton(
            const std::shared_ptr<BoneContent>& skeleton);

        /**
         * @brief Merges positions no further apart than the given tolerance into one.
         *
         * @param mesh The mesh whose positions are merged.
         * @param tolerance The greatest distance two positions may be apart and still merge.
         * @throws System::ArgumentNullException when the mesh is null.
         */
        static void MergeDuplicatePositions(const std::shared_ptr<MeshContent>& mesh,
                                            SharpRuntime::Single tolerance);

        /**
         * @brief Merges the vertices of one batch that name the same position and carry the same
         *        channel data.
         *
         * @param geometry The batch to merge.
         * @throws System::ArgumentNullException when the batch is null.
         */
        static void MergeDuplicateVertices(const std::shared_ptr<GeometryContent>& geometry);

        /**
         * @brief Merges the duplicate vertices of every batch of a mesh.
         *
         * @param mesh The mesh to merge.
         * @throws System::ArgumentNullException when the mesh is null.
         */
        static void MergeDuplicateVertices(const std::shared_ptr<MeshContent>& mesh);

        /**
         * @brief Reorders the triangles and vertices of every batch for the vertex cache.
         *
         * @param mesh The mesh to reorder.
         * @throws System::ArgumentNullException when the mesh is null.
         */
        static void OptimizeForCache(const std::shared_ptr<MeshContent>& mesh);

        /**
         * @brief Reverses the winding order of every triangle of every batch.
         *
         * @param mesh The mesh to reverse.
         * @throws System::ArgumentNullException when the mesh is null.
         */
        static void SwapWindingOrder(const std::shared_ptr<MeshContent>& mesh);

        /**
         * @brief Transforms a scene: its geometry moves, and every node's transform is
         *        re-expressed in the new frame.
         *
         * @param scene The node to transform, with its descendants.
         * @param transform The transform to apply.
         * @throws System::ArgumentNullException when the scene is null.
         */
        static void TransformScene(const std::shared_ptr<NodeContent>& scene, const Matrix& transform);
    };
}
