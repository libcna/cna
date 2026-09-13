// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MipmapChain.hpp"
#include "System/Collections/ObjectModel/Collection.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides methods for maintaining a list of mipmap chains: one per face. A 2D texture
     *        has exactly one, a cube texture exactly six, and those collections cannot be resized
     *        (`Cannot resize MipmapChainCollection. This type of texture has a fixed number of
     *        faces.`); a 3D texture's grows with its depth.
     */
    class MipmapChainCollection : public System::Collections::ObjectModel::Collection<std::shared_ptr<MipmapChain>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChainCollection";

        /**
         * @brief Creates the collection a texture type needs -- XNA's internal constructor.
         *
         * @param initialSize Number of empty chains to start with.
         * @param fixedSize True when the texture type has a fixed number of faces.
         */
        CNAEXT MipmapChainCollection(SharpRuntime::intcs initialSize, bool fixedSize);

        /** @brief Tells whether the collection refuses to change its face count. */
        CNAEXT [[nodiscard]] bool IsFixedSize() const noexcept;

    protected:
        /**
         * @brief Removes all chains.
         *
         * @throws System::NotSupportedException for a fixed-size collection.
         */
        void ClearItems() override;

        /**
         * @brief Inserts a chain.
         *
         * @param index Position of the item.
         * @param item The chain.
         * @throws System::NotSupportedException for a fixed-size collection.
         * @throws System::ArgumentNullException for a null item.
         */
        void InsertItem(SharpRuntime::intcs index, const std::shared_ptr<MipmapChain>& item) override;

        /**
         * @brief Removes a chain.
         *
         * @param index Position of the item.
         * @throws System::NotSupportedException for a fixed-size collection.
         */
        void RemoveItem(SharpRuntime::intcs index) override;

        /**
         * @brief Replaces a chain.
         *
         * @param index Position of the item.
         * @param item The chain.
         * @throws System::ArgumentNullException for a null item.
         */
        void SetItem(SharpRuntime::intcs index, const std::shared_ptr<MipmapChain>& item) override;

    private:
        bool fixedSize_;
    };
}
