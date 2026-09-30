// SPDX-License-Identifier: MS-PL
#pragma once
#include <string>

namespace CNA::Internal::GamerServices {
/** @brief Whether a local profile has muted a gamer (the Guide gamer card's Mute): neither hears the
 * other in a network session. Kept for the life of the process, per local gamertag.
 * @param localGamertag Local profile. @param gamertag Other gamer. @return Muted. */
bool voiceMuted(const std::string& localGamertag,const std::string& gamertag);
/** @brief Mutes or unmutes a gamer for a local profile. @param localGamertag Local profile.
 * @param gamertag Other gamer. @param muted Wanted state. */
void setVoiceMuted(const std::string& localGamertag,const std::string& gamertag,bool muted);
/** @brief Forgets every mute; deterministic tests only. */
void resetVoiceMutesForTesting();
}
