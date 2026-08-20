// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/DateTime.hpp"
#include "System/IO/Stream.hpp"
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Represents a single achievement and its earned state.
     */
    class Achievement
    {
    public:
        /**
         * @brief Gets the description of the achievement.
         *
         * @return The description string.
         */
        [[nodiscard]] const std::string& getDescriptionProperty() const;

        /**
         * @brief Gets whether the achievement is displayed before it is earned.
         *
         * @return true if shown before earned.
         */
        [[nodiscard]] bool getDisplayBeforeEarnedProperty() const;

        /**
         * @brief Gets the date and time when the achievement was earned.
         *
         * @return The earned date/time.
         */
        [[nodiscard]] System::DateTime getEarnedDateTimeProperty() const;

        /**
         * @brief Gets whether the achievement was earned in an online session.
         *
         * @return true if earned online.
         */
        [[nodiscard]] bool getEarnedOnlineProperty() const;

        /**
         * @brief Gets the GamerScore value awarded by this achievement.
         *
         * @return The GamerScore.
         */
        [[nodiscard]] int getGamerScoreProperty() const;

        /**
         * @brief Gets the description of how to earn the achievement.
         *
         * @return The how-to-earn string.
         */
        [[nodiscard]] const std::string& getHowToEarnProperty() const;

        /**
         * @brief Gets whether the achievement has been earned.
         *
         * @return true if earned.
         */
        [[nodiscard]] bool getIsEarnedProperty() const;

        /**
         * @brief Gets the unique key that identifies the achievement.
         *
         * @return The key string.
         */
        [[nodiscard]] const std::string& getKeyProperty() const;

        /**
         * @brief Gets the display name of the achievement.
         *
         * @return The name string.
         */
        [[nodiscard]] const std::string& getNameProperty() const;

        /**
         * @brief Returns a stream containing the achievement picture.
         *
         * Task 4.6 (plans/plan_net.md Phase 4): deliberately left throwing, matching FNA's own
         * unimplemented stub - real Xbox 360 achievement artwork was streamed from Xbox LIVE at
         * request time and has no local equivalent, the same genuine-platform-unavailability
         * reasoning already applied elsewhere in this codebase (e.g. Guide's Xbox-Live-only
         * methods). Not a local-persistence gap Phase 4's disk-store work is meant to close.
         *
         * @return Never returns; always throws in this implementation.
         * @throws System::NotImplementedException always.
         */
        System::IO::Stream* GetPicture();

        /**
         * @brief Determines whether this achievement is structurally equal to another.
         *
         * FNA's Achievement has no custom equality (falls back to reference identity); since
         * achievements are stored by value here, structural comparison of every field is the
         * closest achievable equivalent (same reasoning as LeaderboardEntry::operator==).
         * Required by AchievementCollection's IndexOf/Contains/Remove (Task 8.2).
         *
         * @param other The achievement to compare against.
         * @return true if every field is equal.
         */
        CNAEXT [[nodiscard]] bool operator==(const Achievement& other) const;

        /**
         * @brief Determines whether this achievement differs from another.
         *
         * @param other The achievement to compare against.
         * @return true if any field differs.
         */
        CNAEXT [[nodiscard]] bool operator!=(const Achievement& other) const;

        /** @brief Creates an Achievement for CNA internal use. */
        CNAEXT static Achievement CreateInternal(
            const std::string& key,
            const std::string& name,
            const std::string& description,
            bool showBeforeEarned,
            bool earned,
            System::DateTime earnedDateTime
        );

    private:
        Achievement(
            const std::string& key,
            const std::string& name,
            const std::string& description,
            bool showBeforeEarned,
            bool earned,
            System::DateTime earnedDateTime
        );

        std::string description_;
        bool displayBeforeEarned_;
        System::DateTime earnedDateTime_;
        bool earnedOnline_;
        int gamerScore_;
        std::string howToEarn_;
        bool isEarned_;
        std::string key_;
        std::string name_;
    };
}
