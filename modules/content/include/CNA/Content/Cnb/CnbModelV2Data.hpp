// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Content::Cnb
{
    /** @brief Stable Model-schema-2 vertex element formats. */
    enum class CnbModelV2VertexFormat : std::uint32_t
    {
        /** @brief One 32-bit float. */
        Single = 0u,
        /** @brief Two 32-bit floats. */
        Vector2 = 1u,
        /** @brief Three 32-bit floats. */
        Vector3 = 2u,
        /** @brief Four 32-bit floats. */
        Vector4 = 3u,
        /** @brief Four normalized packed BGRA bytes. */
        Color = 4u,
        /** @brief Four unsigned bytes. */
        Byte4 = 5u,
        /** @brief Two signed 16-bit integers. */
        Short2 = 6u,
        /** @brief Four signed 16-bit integers. */
        Short4 = 7u,
        /** @brief Two normalized signed 16-bit integers. */
        NormalizedShort2 = 8u,
        /** @brief Four normalized signed 16-bit integers. */
        NormalizedShort4 = 9u,
        /** @brief Two IEEE-754 binary16 values. */
        HalfVector2 = 10u,
        /** @brief Four IEEE-754 binary16 values. */
        HalfVector4 = 11u,
    };

    /** @brief Stable Model-schema-2 vertex element usages. */
    enum class CnbModelV2VertexUsage : std::uint32_t
    {
        /** @brief Position data. */
        Position = 0u,
        /** @brief Colour data. */
        Color = 1u,
        /** @brief Texture-coordinate or user data. */
        TextureCoordinate = 2u,
        /** @brief Normal data. */
        Normal = 3u,
        /** @brief Binormal data. */
        Binormal = 4u,
        /** @brief Tangent data. */
        Tangent = 5u,
        /** @brief Skinning indices. */
        BlendIndices = 6u,
        /** @brief Skinning weights. */
        BlendWeight = 7u,
        /** @brief Depth data. */
        Depth = 8u,
        /** @brief Fog data. */
        Fog = 9u,
        /** @brief Point-size data. */
        PointSize = 10u,
        /** @brief Displacement sample data. */
        Sample = 11u,
        /** @brief Tessellation-factor data. */
        TessellateFactor = 12u,
    };

    /** @brief One exact element in a schema-2 vertex declaration. */
    struct CnbModelV2VertexElement
    {
        /** @brief Byte offset from the start of one vertex. */
        std::uint32_t offset = 0u;
        /** @brief Stable element format. */
        CnbModelV2VertexFormat format = CnbModelV2VertexFormat::Single;
        /** @brief Stable element usage. */
        CnbModelV2VertexUsage usage = CnbModelV2VertexUsage::Position;
        /** @brief Usage channel index. */
        std::uint32_t usageIndex = 0u;

        /**
         * @brief Compares every declaration element field.
         *
         * @param other Element to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2VertexElement& other) const = default;
    };

    /** @brief One exact schema-2 vertex declaration. */
    struct CnbModelV2VertexDeclaration
    {
        /** @brief Bytes between consecutive vertices. */
        std::uint32_t vertexStride = 0u;
        /** @brief Elements in authored order. */
        std::vector<CnbModelV2VertexElement> elements;

        /**
         * @brief Compares the stride and complete element sequence.
         *
         * @param other Declaration to compare.
         * @return True when the declarations are equal.
         */
        bool operator==(const CnbModelV2VertexDeclaration& other) const = default;
    };

    /** @brief One document-local shared vertex-buffer resource. */
    struct CnbModelV2VertexBuffer
    {
        /** @brief Index into CnbModelV2Data::vertexDeclarations. */
        std::uint32_t declaration = 0u;
        /** @brief Number of complete vertices. */
        std::uint32_t vertexCount = 0u;
        /** @brief Exact interleaved bytes. */
        std::vector<std::uint8_t> bytes;

        /**
         * @brief Compares declaration identity, count, and bytes.
         *
         * @param other Vertex-buffer resource to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2VertexBuffer& other) const = default;
    };

    /** @brief One document-local shared index-buffer resource. */
    struct CnbModelV2IndexBuffer
    {
        /** @brief Bytes per index, either two or four. */
        std::uint32_t indexElementSize = 2u;
        /** @brief Number of indices. */
        std::uint32_t indexCount = 0u;
        /** @brief Exact little-endian index bytes. */
        std::vector<std::uint8_t> bytes;

        /**
         * @brief Compares width, count, and bytes.
         *
         * @param other Index-buffer resource to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2IndexBuffer& other) const = default;
    };

    /** @brief Stock effect discriminants supported by Model schema 2. */
    enum class CnbModelV2EffectKind : std::uint32_t
    {
        /** @brief BasicEffect. */
        BasicEffect = 0u,
        /** @brief SkinnedEffect. */
        SkinnedEffect = 1u,
        /** @brief DualTextureEffect. */
        DualTextureEffect = 2u,
        /** @brief AlphaTestEffect. */
        AlphaTestEffect = 3u,
        /** @brief EnvironmentMapEffect. */
        EnvironmentMapEffect = 4u,
    };

    /** @brief One complete document-local stock-effect resource. */
    struct CnbModelV2Effect
    {
        /** @brief Concrete stock-effect kind. */
        CnbModelV2EffectKind kind = CnbModelV2EffectKind::BasicEffect;
        /** @brief Primary Texture2D logical name, or empty. */
        std::string primaryTexture;
        /** @brief Secondary Texture2D logical name, or empty. */
        std::string secondaryTexture;
        /** @brief Environment TextureCube logical name, or empty. */
        std::string cubeTexture;
        /** @brief Diffuse RGB value. */
        std::array<float, 3> diffuse{{0.0f, 0.0f, 0.0f}};
        /** @brief Emissive RGB value. */
        std::array<float, 3> emissive{{0.0f, 0.0f, 0.0f}};
        /** @brief Basic/Skinned specular or EnvironmentMapSpecular RGB value. */
        std::array<float, 3> specular{{0.0f, 0.0f, 0.0f}};
        /** @brief Basic/Skinned specular exponent. */
        float specularPower = 0.0f;
        /** @brief Effect opacity. */
        float alpha = 0.0f;
        /** @brief Environment-map amount. */
        float environmentMapAmount = 0.0f;
        /** @brief Environment-map Fresnel factor. */
        float fresnelFactor = 0.0f;
        /** @brief SkinnedEffect weights per vertex. */
        std::uint32_t weightsPerVertex = 0u;
        /** @brief AlphaTestEffect comparison ID. */
        std::uint32_t alphaFunction = 0u;
        /** @brief AlphaTestEffect serialized reference-alpha bits. */
        std::uint32_t referenceAlpha = 0u;
        /** @brief Basic/Dual/AlphaTest vertex-colour enablement. */
        bool vertexColorEnabled = false;

        /**
         * @brief Compares every serialized stock-effect field.
         *
         * @param other Effect resource to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2Effect& other) const = default;
    };

    /** @brief One Model schema-2 bone. */
    struct CnbModelV2Bone
    {
        /** @brief Bone name. */
        std::string name;
        /** @brief Parent index, or -1. */
        std::int32_t parent = -1;
        /** @brief Bone-local transform in M11..M44 order. */
        std::array<float, 16> transform{{1.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, 1.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f, 0.0f, 1.0f}};

        /**
         * @brief Compares the complete bone record.
         *
         * @param other Bone to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2Bone& other) const = default;
    };

    /** @brief One Model schema-2 mesh part. */
    struct CnbModelV2Part
    {
        /** @brief Base vertex selected from the shared vertex buffer. */
        std::uint32_t vertexOffset = 0u;
        /** @brief Number of vertices declared to the draw. */
        std::uint32_t numVertices = 0u;
        /** @brief First selected index. */
        std::uint32_t startIndex = 0u;
        /** @brief Number of TriangleList primitives. */
        std::uint32_t primitiveCount = 0u;
        /** @brief Shared vertex-buffer resource index. */
        std::uint32_t vertexBuffer = 0u;
        /** @brief Shared index-buffer resource index. */
        std::uint32_t indexBuffer = 0u;
        /** @brief Shared stock-effect resource index. */
        std::uint32_t effect = 0u;

        /**
         * @brief Compares the complete draw/resource record.
         *
         * @param other Part to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2Part& other) const = default;
    };

    /** @brief One Model schema-2 mesh with an exact authored bound. */
    struct CnbModelV2Mesh
    {
        /** @brief Mesh name. */
        std::string name;
        /** @brief Parent bone index. */
        std::int32_t parentBone = -1;
        /** @brief Bounding-sphere center XYZ and radius. */
        std::array<float, 4> boundingSphere{{0.0f, 0.0f, 0.0f, 0.0f}};
        /** @brief Contiguous parts owned by this mesh. */
        std::vector<CnbModelV2Part> parts;

        /**
         * @brief Compares the complete mesh and part sequence.
         *
         * @param other Mesh to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2Mesh& other) const = default;
    };

    /** @brief Complete CPU-only Model schema-2 document. */
    struct CnbModelV2Data
    {
        /** @brief Bones in runtime collection order. */
        std::vector<CnbModelV2Bone> bones;
        /** @brief Explicit Model.Root bone index. */
        std::uint32_t rootBone = 0u;
        /** @brief Meshes in runtime collection order. */
        std::vector<CnbModelV2Mesh> meshes;
        /** @brief Interned exact vertex declarations. */
        std::vector<CnbModelV2VertexDeclaration> vertexDeclarations;
        /** @brief Shared vertex-buffer resources. */
        std::vector<CnbModelV2VertexBuffer> vertexBuffers;
        /** @brief Shared index-buffer resources. */
        std::vector<CnbModelV2IndexBuffer> indexBuffers;
        /** @brief Shared stock-effect resources. */
        std::vector<CnbModelV2Effect> effects;

        /**
         * @brief Compares every graph and resource field.
         *
         * @param other Model document to compare.
         * @return True when every field is equal.
         */
        bool operator==(const CnbModelV2Data& other) const = default;
    };
}
