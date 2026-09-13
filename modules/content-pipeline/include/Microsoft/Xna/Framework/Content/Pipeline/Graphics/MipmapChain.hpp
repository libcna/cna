// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/BitmapContent.hpp"
#include "System/Collections/ObjectModel/Collection.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides methods for maintaining a mipmap chain: level 0 first, each further level
     *        half the size of the one before. Items are serialized as `<Mipmap>` elements
     *        (`[ContentSerializerCollectionItemName("Mipmap")]`).
     */
    class MipmapChain : public System::Collections::ObjectModel::Collection<std::shared_ptr<BitmapContent>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MipmapChain";

        /** @brief The item element name the intermediate format uses for this collection. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Mipmap";

        /** @brief Initializes a new instance of MipmapChain. */
        MipmapChain();

        /**
         * @brief Initializes a new instance of MipmapChain with the specified mipmap. This is
         *        also the implicit conversion XNA declares from BitmapContent to MipmapChain.
         *
         * @param bitmap Bitmap used for the mipmap.
         * @throws System::ArgumentNullException for a null bitmap.
         */
        MipmapChain(std::shared_ptr<BitmapContent> bitmap); // NOLINT(google-explicit-constructor)

        /** @brief Returns the type's full name, as XNA's `ToString` does. */
        CNAEXT [[nodiscard]] std::string ToString() const;

    protected:
        /**
         * @brief Inserts a bitmap into the chain.
         *
         * @param index Position of the item.
         * @param item The bitmap.
         * @throws System::ArgumentNullException for a null item.
         */
        void InsertItem(SharpRuntime::intcs index, const std::shared_ptr<BitmapContent>& item) override;

        /**
         * @brief Replaces a bitmap in the chain.
         *
         * @param index Position of the item.
         * @param item The bitmap.
         * @throws System::ArgumentNullException for a null item.
         */
        void SetItem(SharpRuntime::intcs index, const std::shared_ptr<BitmapContent>& item) override;
    };
}
