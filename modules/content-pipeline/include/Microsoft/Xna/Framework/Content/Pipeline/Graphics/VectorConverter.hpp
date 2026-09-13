// SPDX-License-Identifier: MS-PL
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/detail/PixelTraits.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "System/Type.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides methods for converting between the 22 vector and packed-vector types the
     *        content pipeline handles, and for mapping them to GPU formats
     *        (`tests/reference/xna40/graphics/graphics-content-oracle.json`, `vectorconverter/*`).
     */
    class VectorConverter final
    {
    public:
        VectorConverter() = delete;

        /**
         * @brief Gets a method for converting between two vector types.
         *
         * @tparam TInput Type being converted.
         * @tparam TOutput Type being converted to.
         * @return A callable converting one value through `Vector4`.
         */
        template<typename TInput, typename TOutput>
        [[nodiscard]] static std::function<TOutput(TInput)> GetConverter()
        {
            static_assert(detail::ValidPixelType<TInput> && detail::ValidPixelType<TOutput>,
                          "VectorConverter::GetConverter: supported types are Single, Vector2, Vector3, Vector4, and value "
                          "types that implement IPackedVector.");
            return [](TInput input) -> TOutput
            { return detail::PixelTraits<TOutput>::FromVector4(detail::PixelTraits<TInput>::ToVector4(input)); };
        }

        /**
         * @brief Gets the corresponding SurfaceFormat for the specified vector type.
         *
         * @param vectorType The vector type.
         * @param surfaceFormat Receives the format.
         * @return True when the type has a surface format.
         */
        [[nodiscard]] static bool TryGetSurfaceFormat(System::Type vectorType,
                                                      Microsoft::Xna::Framework::Graphics::SurfaceFormat& surfaceFormat);

        /**
         * @brief Gets the corresponding vector type for the specified SurfaceFormat.
         *
         * @param surfaceFormat The surface format.
         * @param vectorType Receives the vector type.
         * @return True when the format has a vector type.
         */
        [[nodiscard]] static bool TryGetVectorType(Microsoft::Xna::Framework::Graphics::SurfaceFormat surfaceFormat,
                                                   System::Type& vectorType);

        /**
         * @brief Gets the corresponding vector type for the specified VertexElementFormat.
         *
         * @param vertexElementFormat The vertex element format.
         * @param vectorType Receives the vector type.
         * @return True when the format has a vector type.
         */
        [[nodiscard]] static bool TryGetVectorType(Microsoft::Xna::Framework::Graphics::VertexElementFormat vertexElementFormat,
                                                   System::Type& vectorType);

        /**
         * @brief Gets the corresponding VertexElementFormat for the specified vector type.
         *
         * @param vectorType The vector type.
         * @param vertexElementFormat Receives the format.
         * @return True when the type has a vertex element format.
         */
        [[nodiscard]] static bool TryGetVertexElementFormat(System::Type vectorType,
                                                            Microsoft::Xna::Framework::Graphics::VertexElementFormat& vertexElementFormat);

        /**
         * @brief The .NET full name of a vector type, for messages and `Type` attributes.
         *
         * @param vectorType The vector type.
         * @return The name, or empty for a type this converter does not know.
         */
        CNAEXT [[nodiscard]] static std::string VectorTypeName(System::Type vectorType);
    };
}
