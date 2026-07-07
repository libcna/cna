// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "System/IDisposable.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief A disposable, indexed collection of Achievement objects.
     */
    class AchievementCollection : public System::IDisposable
    {
    public:
        /**
         * @brief Returns the number of achievements in the collection.
         *
         * @return The element count.
         */
        [[nodiscard]] int getCountProperty() const;

        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets the achievement at the specified index.
         *
         * @param index Zero-based index.
         * @return Const reference to the Achievement.
         * @throws System::ArgumentOutOfRangeException if index is out of range.
         */
        [[nodiscard]] const Achievement& operator[](int index) const;

        /**
         * @brief Gets the achievement with the specified key.
         *
         * @param achievementKey The key to search for.
         * @return Const reference to the matching Achievement.
         * @throws System::IndexOutOfRangeException if no achievement with that key exists.
         */
        [[nodiscard]] const Achievement& operator[](const std::string& achievementKey) const;

        /**
         * @brief Returns a C++ iterator to the beginning (for range-for).
         *
         * @return Begin iterator.
         */
        NOXNA [[nodiscard]] auto begin() const { return collection_.begin(); }

        /**
         * @brief Returns a C++ iterator past the end (for range-for).
         *
         * @return End iterator.
         */
        NOXNA [[nodiscard]] auto end() const { return collection_.end(); }

        /**
         * @brief Releases the achievements held by this collection.
         */
        void Dispose() override;

        /** @brief Creates an AchievementCollection for CNA internal use. */
        NOXNA static AchievementCollection CreateInternal(std::vector<Achievement> achievements);

    private:
        explicit AchievementCollection(std::vector<Achievement> achievements);

        std::vector<Achievement> collection_;
        bool isDisposed_{false};
    };
}
