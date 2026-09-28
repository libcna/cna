// SPDX-License-Identifier: MS-PL
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices {
/** @brief An offline profile kept on this machine: never an account, never sent to a service. */
struct LocalProfile {
    /** @brief Profile name, used as the gamertag. */
    std::string gamertag;
    /** @brief Whether the profile signs in when gamer services start. */
    bool autoSignIn = false;
    /** @brief The profile's 1021-byte CNA avatar description, or empty for none. */
    std::vector<unsigned char> avatar;
};

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
 * @brief Gets the avatar of a stored profile.
 * @param gamertag Profile name, ignoring case.
 * @return The 1021-byte description, or empty when the profile or its avatar is missing.
 */
std::vector<unsigned char> localProfileAvatar(const std::string& gamertag);
}
