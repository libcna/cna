// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VectorConverter.hpp"

#include <array>
#include <optional>

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;

    namespace
    {
        struct Entry
        {
            System::Type type;
            std::string_view dotNetName;
            std::optional<SurfaceFormat> surface;
            std::optional<VertexElementFormat> vertex;
        };

        template<typename T>
        Entry Make()
        {
            return Entry{System::Type::From<T>(), detail::PixelTraits<T>::DotNetName, detail::PixelTraits<T>::Surface,
                         detail::PixelTraits<T>::Vertex};
        }

        const std::array<Entry, 22>& Table()
        {
            using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
            static const std::array<Entry, 22> table = {
                Make<Alpha8>(),     Make<Bgr565>(),          Make<Bgra4444>(),        Make<Bgra5551>(),
                Make<Byte4>(),      Make<Color>(),           Make<HalfSingle>(),      Make<HalfVector2>(),
                Make<HalfVector4>(), Make<NormalizedByte2>(), Make<NormalizedByte4>(), Make<NormalizedShort2>(),
                Make<NormalizedShort4>(), Make<Rg32>(),      Make<Rgba1010102>(),     Make<Rgba64>(),
                Make<Short2>(),     Make<Short4>(),          Make<float>(),           Make<Vector2>(),
                Make<Vector3>(),    Make<Vector4>()};
            return table;
        }

        const Entry* Find(System::Type type)
        {
            for (const Entry& entry : Table())
            {
                if (entry.type == type)
                {
                    return &entry;
                }
            }
            return nullptr;
        }
    }

    bool VectorConverter::TryGetSurfaceFormat(System::Type vectorType, SurfaceFormat& surfaceFormat)
    {
        const Entry* entry = Find(vectorType);
        if (entry == nullptr || !entry->surface.has_value())
        {
            return false;
        }
        surfaceFormat = *entry->surface;
        return true;
    }

    bool VectorConverter::TryGetVectorType(SurfaceFormat surfaceFormat, System::Type& vectorType)
    {
        for (const Entry& entry : Table())
        {
            if (entry.surface == surfaceFormat)
            {
                vectorType = entry.type;
                return true;
            }
        }
        return false;
    }

    bool VectorConverter::TryGetVectorType(VertexElementFormat vertexElementFormat, System::Type& vectorType)
    {
        for (const Entry& entry : Table())
        {
            if (entry.vertex == vertexElementFormat)
            {
                vectorType = entry.type;
                return true;
            }
        }
        return false;
    }

    bool VectorConverter::TryGetVertexElementFormat(System::Type vectorType, VertexElementFormat& vertexElementFormat)
    {
        const Entry* entry = Find(vectorType);
        if (entry == nullptr || !entry->vertex.has_value())
        {
            return false;
        }
        vertexElementFormat = *entry->vertex;
        return true;
    }

    std::string VectorConverter::VectorTypeName(System::Type vectorType)
    {
        const Entry* entry = Find(vectorType);
        return entry == nullptr ? std::string() : std::string(entry->dotNetName);
    }
}
