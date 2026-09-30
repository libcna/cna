// SPDX-License-Identifier: MS-PL
#pragma once
#include <string>
#include <vector>

namespace CNA::Internal::GamerServices {
/** @brief Whether a local profile has muted a gamer (the Guide gamer card's Mute), or blocked them:
 * neither hears the other in a network session. Mutes last for the life of the process, blocks as
 * long as the account's block list holds them. @param localGamertag Local profile.
 * @param gamertag Other gamer. @return Muted. */
bool voiceMuted(const std::string& localGamertag,const std::string& gamertag);
/** @brief Mutes or unmutes a gamer for a local profile. @param localGamertag Local profile.
 * @param gamertag Other gamer. @param muted Wanted state. */
void setVoiceMuted(const std::string& localGamertag,const std::string& gamertag,bool muted);
/** @brief Whether a local profile's account has blocked a gamer, as this process knows it.
 * @param localGamertag Local profile. @param gamertag Other gamer. @return Blocked. */
bool playerBlocked(const std::string& localGamertag,const std::string& gamertag);
/** @brief Records one block or unblock of a local profile's account. @param localGamertag Local
 * profile. @param gamertag Other gamer. @param blocked Wanted state. */
void setPlayerBlocked(const std::string& localGamertag,const std::string& gamertag,bool blocked);
/** @brief Replaces a local profile's block list with the service's. @param localGamertag Local
 * profile. @param gamertags Blocked gamers. */
void setBlockedPlayers(const std::string& localGamertag,const std::vector<std::string>& gamertags);
/** @brief Forgets every mute and block; deterministic tests only. */
void resetVoiceMutesForTesting();
}
