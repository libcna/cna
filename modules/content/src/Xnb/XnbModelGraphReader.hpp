// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"

namespace CNA::Internal::Xnb
{
    enum class XnbModelTagKind
    {
        Mesh,
        MeshPart,
        Model,
    };

    enum class XnbModelSharedKind
    {
        VertexBuffer,
        IndexBuffer,
        Effect,
    };

    inline std::int32_t ReadXnbModelBoneReference(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        const std::uint32_t boneCount)
    {
        const std::uint32_t boneId = boneCount < 255u ? input.ReadByte() : input.ReadUInt32();
        return boneId == 0u ? -1 : static_cast<std::int32_t>(boneId - 1u);
    }

    template<typename Sink>
    auto ReadXnbModelGraph(
        Microsoft::Xna::Framework::Content::ContentReader& input, Sink& sink)
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        const std::uint32_t boneCount = input.ReadUInt32();
        input.CheckCollectionElementCount(boneCount, "ModelReader bones");
        sink.BeginBones(boneCount);
        for (std::uint32_t bone = 0u; bone < boneCount; ++bone)
        {
            const std::string name = sink.ReadString(input);
            const auto transform = input.ReadMatrix();
            sink.AddBone(bone, name, transform);
        }
        for (std::uint32_t bone = 0u; bone < boneCount; ++bone)
        {
            const std::int32_t parent = ReadXnbModelBoneReference(input, boneCount);
            const std::uint32_t childCount = input.ReadUInt32();
            input.CheckCollectionElementCount(childCount, "ModelReader bone children");
            sink.BeginBoneLinks(bone, parent, childCount);
            for (std::uint32_t child = 0u; child < childCount; ++child)
            {
                sink.AddBoneChild(
                    bone, ReadXnbModelBoneReference(input, boneCount));
            }
            sink.EndBoneLinks(bone);
        }

        const std::int32_t meshCount = input.ReadInt32();
        input.CheckCollectionElementCount(meshCount, "ModelReader meshes");
        sink.BeginMeshes(static_cast<std::uint32_t>(meshCount));
        for (std::int32_t mesh = 0; mesh < meshCount; ++mesh)
        {
            const std::string name = sink.ReadString(input);
            const std::int32_t parent = ReadXnbModelBoneReference(input, boneCount);
            const auto boundingSphere = input.ReadBoundingSphere();
            sink.BeginMesh(
                static_cast<std::uint32_t>(mesh), name, parent, boundingSphere);
            sink.ReadTag(input, XnbModelTagKind::Mesh);

            const std::int32_t partCount = input.ReadInt32();
            input.CheckCollectionElementCount(partCount, "ModelReader mesh parts");
            sink.BeginMeshParts(static_cast<std::uint32_t>(partCount));
            for (std::int32_t part = 0; part < partCount; ++part)
            {
                const std::int32_t vertexOffset = input.ReadInt32();
                const std::int32_t vertexCount = input.ReadInt32();
                const std::int32_t startIndex = input.ReadInt32();
                const std::int32_t primitiveCount = input.ReadInt32();
                sink.BeginMeshPart(
                    static_cast<std::uint32_t>(part), vertexOffset, vertexCount,
                    startIndex, primitiveCount);
                sink.ReadTag(input, XnbModelTagKind::MeshPart);
                sink.ReadSharedReference(input, XnbModelSharedKind::VertexBuffer);
                sink.ReadSharedReference(input, XnbModelSharedKind::IndexBuffer);
                sink.ReadSharedReference(input, XnbModelSharedKind::Effect);
                sink.EndMeshPart();
            }
            sink.EndMesh();
        }

        const std::int32_t root = ReadXnbModelBoneReference(input, boneCount);
        sink.ReadTag(input, XnbModelTagKind::Model);
        return sink.Finish(root);
    }
}
