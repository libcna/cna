// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides properties for managing the names of vertex channels: a base name and a
     *        usage index, encoded as one string such as `TextureCoordinate0`.
     */
    class VertexChannelNames final
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexChannelNames";

        /** @brief The class is static; it has no instances. */
        VertexChannelNames() = delete;

        /**
         * @brief Gets the name of a binormal channel with the given usage index.
         *
         * @param usageIndex The usage index.
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Binormal(SharpRuntime::intcs usageIndex);

        /**
         * @brief Gets the name of a colour channel with the given usage index.
         *
         * @param usageIndex The usage index.
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Color(SharpRuntime::intcs usageIndex);

        /**
         * @brief Gets the base name of an encoded channel name.
         *
         * @param encodedName The encoded name.
         * @return The base name, without the trailing usage index.
         * @throws System::ArgumentNullException when the name is empty.
         */
        [[nodiscard]] static std::string DecodeBaseName(const std::string& encodedName);

        /**
         * @brief Gets the usage index of an encoded channel name.
         *
         * @param encodedName The encoded name.
         * @return The usage index; zero when the name carries no digits.
         * @throws System::ArgumentNullException when the name is empty.
         */
        [[nodiscard]] static SharpRuntime::intcs DecodeUsageIndex(const std::string& encodedName);

        /**
         * @brief Combines a vertex element usage with a usage index into a channel name.
         *
         * @param usage The vertex element usage.
         * @param usageIndex The usage index.
         * @return The encoded name.
         * @throws System::ArgumentOutOfRangeException when the usage index is negative.
         */
        [[nodiscard]] static std::string EncodeName(Microsoft::Xna::Framework::Graphics::VertexElementUsage usage,
                                                    SharpRuntime::intcs usageIndex);

        /**
         * @brief Combines a base name with a usage index into a channel name.
         *
         * @param baseName The base name.
         * @param usageIndex The usage index.
         * @return The encoded name.
         * @throws System::ArgumentNullException when the base name is empty.
         * @throws System::ArgumentOutOfRangeException when the usage index is negative.
         */
        [[nodiscard]] static std::string EncodeName(const std::string& baseName, SharpRuntime::intcs usageIndex);

        /**
         * @brief Gets the name of the primary normal channel.
         *
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Normal();

        /**
         * @brief Gets the name of a normal channel with the given usage index.
         *
         * @param usageIndex The usage index.
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Normal(SharpRuntime::intcs usageIndex);

        /**
         * @brief Gets the name of a tangent channel with the given usage index.
         *
         * @param usageIndex The usage index.
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Tangent(SharpRuntime::intcs usageIndex);

        /**
         * @brief Gets the name of a texture coordinate channel with the given usage index.
         *
         * @param usageIndex The usage index.
         * @return The encoded name.
         */
        [[nodiscard]] static std::string TextureCoordinate(SharpRuntime::intcs usageIndex);

        /**
         * @brief Tries to read a vertex element usage out of an encoded channel name.
         *
         * @param encodedName The encoded name.
         * @param usage Receives the usage; left at `Position` when the name does not name one.
         * @return true when the base name is a vertex element usage.
         * @throws System::ArgumentNullException when the name is empty.
         */
        [[nodiscard]] static bool TryDecodeUsage(const std::string& encodedName,
                                                 Microsoft::Xna::Framework::Graphics::VertexElementUsage& usage);

        /**
         * @brief Gets the name of the primary blend weights channel.
         *
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Weights();

        /**
         * @brief Gets the name of a blend weights channel with the given usage index.
         *
         * @param usageIndex The usage index.
         * @return The encoded name.
         */
        [[nodiscard]] static std::string Weights(SharpRuntime::intcs usageIndex);
    };
}
