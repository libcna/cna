// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class PropertyDictionary;
}

namespace CNA::Internal::GamerServices
{
    /**
     * @brief A single locally-persisted "this achievement is earned" record.
     *
     * Task 4.2 (plan_net.md): local, no-catalog persistence - only the fact-of-earning is real
     * local data (name/description/gamerScore have no local source of truth; see the achievement
     * catalog note in plan_net.md Phase 4 for why).
     */
    struct PersistedAchievement
    {
        /** @brief The achievement's unique key, matching Achievement::getKeyProperty(). */
        std::string Key;
        /** @brief The moment it was earned, as System::DateTime ticks (exact round-trip, no string parsing). */
        long long EarnedTicks = 0;
    };

    /**
     * @brief A single locally-persisted leaderboard row.
     */
    struct PersistedLeaderboardEntry
    {
        /** @brief The gamertag this row belongs to. */
        std::string Gamertag;
        /** @brief The entry's rating (LeaderboardEntry::getRatingProperty()). */
        long long Rating = 0;
    };

    /**
     * @brief Returns the local root directory this store persists into, creating it if needed.
     *
     * Reuses Microsoft::Xna::Framework::Storage::StorageDevice::GetStorageRootEXT() (the
     * project's existing platform-owned user-data-directory convention) rather than
     * inventing a new one, appending a "GamerServices" subdirectory.
     *
     * @return Absolute path to the GamerServices local store root.
     */
    std::string GetGamerServicesStoreRootEXT();

    /**
     * @brief Replaces filesystem-unsafe characters in a gamertag/leaderboard-key so it can be
     * used directly as a file name component.
     *
     * @param raw The raw string (a gamertag or a LeaderboardIdentity's key/game-mode encoding).
     * @return A sanitized string safe to use as a single path component.
     */
    std::string SanitizeStoreFileNameComponent(const std::string& raw);

    /**
     * @brief Derives the single, canonical file-name key for a leaderboard from its identity's
     * key string and game mode - the one authoritative implementation LeaderboardWriter and
     * LeaderboardReader both call, so they can never disagree on which file a given
     * LeaderboardIdentity maps to.
     *
     * @param leaderboardKeyName The LeaderboardIdentity's key, already converted to its string
     *        form by the caller (e.g. via a name lookup for the LeaderboardKey enum).
     * @param gameMode The LeaderboardIdentity's game mode.
     * @return A sanitized, unique file-name key (without extension/directory).
     */
    std::string MakeLeaderboardFileKeyEXT(const std::string& leaderboardKeyName, int gameMode);

    // --- Achievements: one JSON file per gamertag ---

    /**
     * @brief Loads every locally-earned achievement record for the given gamertag.
     *
     * @param gamertag The gamertag to load records for.
     * @return The persisted records, or empty if the gamertag has no store file yet, or if the
     *         store file is missing/corrupt (never throws for a missing/corrupt file - starts
     *         empty, matching plan_net.md Task 4.7's requirement).
     */
    std::vector<PersistedAchievement> LoadEarnedAchievementsEXT(const std::string& gamertag);

    /**
     * @brief Records (or updates) one earned-achievement entry for the given gamertag and
     * immediately persists it to disk.
     *
     * @param gamertag The gamertag earning the achievement.
     * @param key The achievement's unique key.
     * @param earnedTicks The moment it was earned, as System::DateTime ticks.
     */
    void SaveEarnedAchievementEXT(const std::string& gamertag, const std::string& key, long long earnedTicks);

    // --- Leaderboards: one JSON file per (leaderboard key, game mode) ---

    /**
     * @brief Loads every locally-known entry for the given leaderboard.
     *
     * @param leaderboardFileKey A pre-sanitized, unique file-name key identifying the leaderboard
     *        (LeaderboardIdentity's key + game mode, already encoded by the caller).
     * @return The persisted entries (unordered - callers sort as needed), or empty if the
     *         leaderboard has no store file yet, or if the store file is missing/corrupt.
     */
    std::vector<PersistedLeaderboardEntry> LoadLeaderboardEntriesEXT(const std::string& leaderboardFileKey);

    /**
     * @brief Records (or updates) one gamer's entry on the given leaderboard and immediately
     * persists it to disk.
     *
     * @param leaderboardFileKey A pre-sanitized, unique file-name key identifying the leaderboard.
     * @param entry The entry to upsert, keyed by Gamertag.
     * @param columns The entry's additional named columns (persisted alongside Rating); may be
     *        null to persist no columns.
     */
    void SaveLeaderboardEntryEXT(
        const std::string& leaderboardFileKey,
        const PersistedLeaderboardEntry& entry,
        const Microsoft::Xna::Framework::GamerServices::PropertyDictionary* columns
    );

    /**
     * @brief Loads the persisted column values for one gamer's entry on the given leaderboard
     * back into a PropertyDictionary.
     *
     * @param leaderboardFileKey A pre-sanitized, unique file-name key identifying the leaderboard.
     * @param gamertag The gamertag whose columns to load.
     * @param outColumns The dictionary to populate (existing contents are not cleared first).
     */
    void LoadLeaderboardEntryColumnsEXT(
        const std::string& leaderboardFileKey,
        const std::string& gamertag,
        Microsoft::Xna::Framework::GamerServices::PropertyDictionary& outColumns
    );

    /**
     * @brief Test-only: deletes this process's entire local GamerServices store, so tests that
     * exercise real disk persistence do not leak state into (or read stale state left by) other
     * tests/runs.
     */
    void ResetStoreForTestingEXT();
}
