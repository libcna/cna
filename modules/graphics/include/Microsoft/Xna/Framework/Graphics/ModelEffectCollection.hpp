// SPDX-License-Identifier: MS-PL
#pragma once

#include <vector>

#include "CNA/CNAHelper.hpp"
#include "System/Collections/Generic/IEnumerator.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class Effect;

    /**
     * @brief Represents a collection of effects associated with a model mesh.
     */
    class ModelEffectCollection
    {
    public:
        /** @brief Iterates over the effects in a ModelEffectCollection. */
        struct Enumerator final : public System::Collections::Generic::IEnumerator<Effect*>,
                                  public System::IDisposable
        {
            /** @brief Constructs the default value of the enumerator struct. */
            Enumerator() = default;

            /**
             * @brief Gets the current effect.
             * @return The current Effect pointer.
             */
            [[nodiscard]] Effect* const& Current() const override;

            /**
             * @brief Advances to the next effect.
             * @return True if an element is available.
             */
            bool MoveNext() override;

            /** @brief Returns to the position before the first element. */
            void Reset() override;

            /** @brief Releases enumerator resources. */
            void Dispose() override;

            /**
             * @brief Gets the current element through the non-generic interface.
             * @return The current Effect pointer boxed as an object.
             */
            [[nodiscard]] std::any getCurrentProperty() const override;

        private:
            friend class ModelEffectCollection;
            explicit Enumerator(const ModelEffectCollection& collection);
            const ModelEffectCollection* collection_ = nullptr;
            int position_ = 0;
            Effect* current_ = nullptr;
            bool hasCurrent_ = false;
            std::size_t version_ = 0;
        };

        /**
         * @brief Creates an enumerator positioned before the first effect.
         * @return An independent value-type enumerator.
         */
        [[nodiscard]] Enumerator GetEnumerator() const;
        /**
         * @brief Retrieves an Effect by index.
         * @param index The zero-based index of the effect to retrieve.
         * @return Pointer to the Effect at the specified index.
         * @throws System::ArgumentOutOfRangeException if index is outside the collection.
         */
        [[nodiscard]] Effect* operator[](int index) const;

        /**
         * @brief Gets the number of effects in this collection.
         * @return The effect count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Returns true if the collection contains the given effect.
         * @param effect Pointer to the Effect to search for.
         * @return True if found; false otherwise.
         */
        [[nodiscard]] bool Contains(const Effect* effect) const;

        /**
         * @brief Adds an effect to this collection.
         * @param effect Pointer to the Effect to add.
         */
        CNAEXT void Add(Effect* effect);

        /**
         * @brief Removes an effect from this collection.
         * @param effect Pointer to the Effect to remove.
         */
        CNAEXT void Remove(const Effect* effect);

        using iterator = std::vector<Effect*>::iterator;
        using const_iterator = std::vector<Effect*>::const_iterator;

        /** @brief Returns an iterator to the beginning of the collection. */
        CNAEXT iterator begin();
        /** @brief Returns an iterator past the end of the collection. */
        CNAEXT iterator end();
        /** @brief Returns a const iterator to the beginning of the collection. */
        CNAEXT const_iterator begin() const;
        /** @brief Returns a const iterator past the end of the collection. */
        CNAEXT const_iterator end() const;

    private:
        std::vector<Effect*> effects_;
        std::size_t version_ = 0;
        friend class ModelMesh;
    };
}
