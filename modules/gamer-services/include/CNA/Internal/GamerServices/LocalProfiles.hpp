// SPDX-License-Identifier: MS-PL
#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Internal::GamerServices {
/** @brief A profile's preferred game settings (XNA GameDefaults). Every field starts at the value
 * XNA reports for a profile that never set it: the enumerations' first members, no colors, false. */
struct LocalGameDefaults {
    /** @brief GameDifficulty ordinal: Easy 0, Normal 1, Hard 2. */
    int gameDifficulty = 0;
    /** @brief ControllerSensitivity ordinal: Low 0, Medium 1, High 2. */
    int controllerSensitivity = 0;
    /** @brief Preferred player color as red, green and blue, or none. */
    std::optional<std::array<unsigned char, 3>> primaryColor;
    /** @brief Second preferred player color as red, green and blue, or none. */
    std::optional<std::array<unsigned char, 3>> secondaryColor;
    /** @brief Boolean preferences, as their XNA properties name them. */
    bool autoAim = false, autoCenter = false, moveWithRightThumbStick = false, invertYAxis = false,
        manualTransmission = false, accelerateWithButtons = false, brakeWithButtons = false;
    /** @brief RacingCameraAngle ordinal: Back 0, Front 1, Inside 2. */
    int racingCameraAngle = 0;
};

/** @brief An offline profile kept on this machine: never an account, never sent to a service. */
struct LocalProfile {
    /** @brief Profile name, used as the gamertag. */
    std::string gamertag;
    /** @brief Whether the profile signs in when gamer services start. */
    bool autoSignIn = false;
    /** @brief The profile's 1021-byte CNA avatar description, or empty for none. */
    std::vector<unsigned char> avatar;
    /** @brief Preferred game settings from the entry's `gameDefaults` object; a missing or invalid
     * field keeps its default. */
    LocalGameDefaults gameDefaults;
    /** @brief The entry's `gameDefaults` object exactly as stored (JSON), or empty, so rewriting the
     * store never loses what a player wrote. */
    std::string gameDefaultsJson;
};

/**
 * @brief Reads game defaults in the profile store's `gameDefaults` form (also what a CNA service
 * keeps for an account).
 * @param json A JSON object; a missing, invalid or malformed field keeps its default.
 * @return The defaults.
 */
LocalGameDefaults parseGameDefaultsJson(std::string_view json);

/** @brief Most profiles a store keeps. */
inline constexpr std::size_t MaxLocalProfiles = 32;

/**
 * @brief Checks a local profile name: 1 to 15 ASCII letters, digits and single inner spaces,
 * starting with a letter.
 * @param gamertag Candidate name.
 * @return Whether the name is accepted.
 */
bool isValidLocalGamertag(const std::string& gamertag);

/**
 * @brief Gets the profile store file: `CNA_GAMER_SERVICES_PROFILES_DIR`, else the user data
 * directory.
 * @return Store path, or empty when no location is available (profiles then last one run).
 */
std::filesystem::path localProfilesPath();

/**
 * @brief Reads the stored profiles. A missing, oversized or malformed store reads as empty, and
 * malformed entries are skipped.
 * @return Stored profiles in store order.
 */
std::vector<LocalProfile> loadLocalProfiles();

/**
 * @brief Finds a stored profile by name, ignoring case, or creates one with a random avatar and
 * stores it. A store that exists but cannot be read is never overwritten; the new profile then
 * lasts only this run.
 * @param gamertag Profile name.
 * @return The stored spelling and data of the profile.
 * @throws System::ArgumentException if the name is not a valid local profile name.
 */
LocalProfile openLocalProfile(const std::string& gamertag);

/**
 * @brief Gets the profiles to sign in when gamer services start: the comma-separated names in
 * `CNA_GAMER_SERVICES_AUTO_SIGN_IN` (created when missing), else the stored profiles marked
 * `autoSignIn`, at most four.
 * @return Profiles in player order.
 * @throws CnaService::Error INVALID_CONFIGURATION for an invalid, duplicated or fifth name.
 */
std::vector<LocalProfile> autoSignInLocalProfiles();

/**
 * @brief Finds a stored profile by name, ignoring case, without creating one.
 * @param gamertag Profile name.
 * @return The stored profile, or empty when there is none.
 */
std::optional<LocalProfile> findLocalProfile(const std::string& gamertag);

/**
 * @brief Gets the avatar of a stored profile.
 * @param gamertag Profile name, ignoring case.
 * @return The 1021-byte description, or empty when the profile or its avatar is missing.
 */
std::vector<unsigned char> localProfileAvatar(const std::string& gamertag);

/**
 * @brief Replaces the avatar of a stored profile (the CNA avatar editor's save).
 * @param gamertag Profile name, ignoring case.
 * @param description A valid 1021-byte CNA description.
 * @return True when the store now holds it; false for an invalid description, an unknown
 * profile or a store that cannot be written.
 */
bool setLocalProfileAvatar(const std::string& gamertag,const std::vector<unsigned char>& description);
}
