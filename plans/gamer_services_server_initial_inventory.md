# Initial implementation candidates (GS-001)

Baseline b2fd47a45. Every candidate needs individual Xbox-target review; this is not a defect count. Re-run rg after migrations; retain this baseline as evidence.

This is the historical GS-001 baseline, not an open list: `gamer_services_server_final_register.md` records what every candidate became.

* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:43` — `* Reuses Microsoft::Xna::Framework::Storage::StorageDevice::GetStorageRootEXT() (the`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:49` — `std::string GetGamerServicesStoreRootEXT();`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:71` — `std::string MakeLeaderboardFileKeyEXT(const std::string& leaderboardKeyName, int gameMode);`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:83` — `std::vector<PersistedAchievement> LoadEarnedAchievementsEXT(const std::string& gamertag);`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:93` — `void SaveEarnedAchievementEXT(const std::string& gamertag, const std::string& key, long long earnedTicks);`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:105` — `std::vector<PersistedLeaderboardEntry> LoadLeaderboardEntriesEXT(const std::string& leaderboardFileKey);`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:116` — `void SaveLeaderboardEntryEXT(`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:130` — `void LoadLeaderboardEntryColumnsEXT(`
* `modules/gamer-services/include/CNA/Internal/GamerServices/LocalGamerServicesStore.hpp:141` — `void ResetStoreForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp:26` — `* @return true if shown before earned.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp:40` — `* @return true if earned online.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp:61` — `* @return true if earned.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp:89` — `* @throws System::NotImplementedException always.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp:102` — `* @return true if every field is equal.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Achievement.hpp:110` — `* @return true if any field differs.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp:28` — `* @return true if disposed.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp:118` — `* @return true if a matching achievement was found and removed; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp:135` — `* @return true if found; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp:94` — `* @note CNAEXT — CNA extension. Defaults to AvatarAnimationPresetToClipNameEXT(preset)`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp:99` — `CNAEXT void SetRealClipNameEXT(const std::string& clipName);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp:107` — `CNAEXT [[nodiscard]] const std::string& GetRealClipNameEXT() const;`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarAnimationPresetNamesEXT.hpp:23` — `CNAEXT [[nodiscard]] std::string AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset preset);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp:92` — `// generate_materials.py's MATERIAL_COLORS, is what PartTintEXT()/DrawRealEXT()`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarBodyTypeNamesEXT.hpp:31` — `CNAEXT [[nodiscard]] std::string AvatarBodyTypeToContentNameEXT(AvatarBodyType bodyType);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp:39` — `* @return true if the description is exactly 1021 bytes and its first byte is nonzero.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:155` — `* CNAEXT — CNA extension. The faithful Draw() overloads above remain permanent no-ops`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:156` — `* regardless of this call; only DrawRealEXT() renders real geometry, and only after`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:165` — `CNAEXT void EnableRealRenderingEXT(Graphics::GraphicsDevice& device,`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:172` — `* @return true if real rendering is enabled.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:174` — `CNAEXT [[nodiscard]] bool IsRealRenderingEnabledEXT() const;`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:182` — `CNAEXT void SetAppearanceEXT(const AvatarAppearanceEXT& appearance);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:199` — `CNAEXT void DrawRealEXT(const std::string& animationClipName,`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp:226` — `CNAEXT [[nodiscard]] Microsoft::Xna::Framework::Color PartTintEXT(const std::string& partName) const;`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/AvatarRendererState.hpp:15` — `/** @brief The avatar is unavailable. */`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp:29` — `* @return true if disposed.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:18` — `* @return true if a friend request was received from this gamer.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:25` — `* @return true if a friend request was sent to this gamer.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:32` — `* @return true if this gamer has voice.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:39` — `* @return true if the invite was accepted.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:46` — `* @return true if an invite was received from this gamer.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:53` — `* @return true if the invite was rejected.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:60` — `* @return true if an invite was sent to this gamer.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:67` — `* @return true if away.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:74` — `* @return true if busy.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:81` — `* @return true if joinable.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:88` — `* @return true if online.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp:95` — `* @return true if playing.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:49` — `* @return true if auto-aim is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:56` — `* @return true if auto-center is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:63` — `* @return true if move-with-right-thumbstick is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:70` — `* @return true if Y-axis inversion is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:77` — `* @return true if manual transmission is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:91` — `* @return true if accelerate-with-buttons is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp:98` — `* @return true if brake-with-buttons is on.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Gamer.hpp:52` — `* @return true if disposed.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp:110` — `return false;`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp:114` — `return true;`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp:224` — `* @return true if found; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp:53` — `CNAEXT void SetPresenceModeStringEXT(const std::string& mode);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp:24` — `* @return true if online sessions are permitted.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp:31` — `* @return true if premium content access is permitted.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp:45` — `* @return true if content purchases are permitted.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp:52` — `* @return true if content trading is permitted.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp:70` — `* @return true if disposed.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp:24` — `* @return true if Initialize() has been called.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp:58` — `* @return true if GamerServices is initialized; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:39` — `* @return true if the screen saver is enabled.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:53` — `* @return true if running in trial mode.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:71` — `* PC no-op stub) now that those two overlays are genuinely real, not stubs.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:73` — `* @return true if a message box or keyboard input overlay is currently pending.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:103` — `* @return true if trial mode is being simulated.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:121` — `* title/description are stored and rendered by RenderPendingKeyboardInputEXT() below - a`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:151` — `* in RenderPendingKeyboardInputEXT()'s own rendering (not the returned text itself, which`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:184` — `*         check WasKeyboardInputCanceledEXT() to distinguish "canceled" from "confirmed`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:203` — `CNAEXT [[nodiscard]] static const std::string& GetPendingKeyboardInputTitleForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:212` — `CNAEXT [[nodiscard]] static const std::string& GetPendingKeyboardInputDescriptionForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:226` — `CNAEXT [[nodiscard]] static std::string GetPendingKeyboardInputDisplayTextForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:236` — `* @return true if the operation was canceled rather than confirmed.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:239` — `CNAEXT [[nodiscard]] static bool WasKeyboardInputCanceledEXT(System::IAsyncResult* result);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:245` — `* @return true if a keyboard input request is currently awaiting Enter/cancel.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:267` — `CNAEXT static void RenderPendingKeyboardInputEXT(`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:282` — `CNAEXT static void SimulateKeyboardInputCancelEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:293` — `CNAEXT static void ResetPendingKeyboardInputForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:300` — `* own Draw() loop calls RenderPendingMessageBoxEXT() and the user selects a button (or`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:301` — `* a test/headless caller calls SimulateMessageBoxClickEXT()).`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:372` — `* @return true if a message box is currently awaiting a button selection.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:396` — `CNAEXT static void RenderPendingMessageBoxEXT(`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:413` — `CNAEXT static void SimulateMessageBoxClickEXT(int buttonIndex);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:423` — `CNAEXT static void ResetPendingMessageBoxForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:432` — `CNAEXT [[nodiscard]] static int GetPendingMessageBoxFocusButtonForTestingEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/Guide.hpp:554` — `CNAEXT static void ShowAchievementsEXT(Microsoft::Xna::Framework::PlayerIndex player);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp:33` — `* @return true if this is the current session.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp:53` — `* the local store when SetOnRatingChangedHookEXT() has installed a hook. A no-op for`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp:69` — `CNAEXT void SetOnRatingChangedHookEXT(std::function<void()> hook);`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp:90` — `* @return true if gamer, rating, and ranking are all equal.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp:98` — `* @return true if any of gamer, rating, or ranking differ.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp:26` — `* @return true if disposed.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp:33` — `* @return true if PageDown() would reveal additional entries.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp:40` — `* @return true if PageUp() would reveal additional entries.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp:134` — `* match, since FNA's own LeaderboardReader is identically all-NotSupportedException); a`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp:311` — `void ResliceEntriesEXT();`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:52` — `* @return true if the key was found; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:61` — `* @return true if the key was found; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:213` — `* NotSupportedException(ProFeatureNotSupported) from every member, including this one and`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:249` — `* @throws System::NotSupportedException if both values are of the same type and that type`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:263` — `* @return true if the key was found and removed; otherwise false.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:276` — `* @throws System::NotSupportedException under the same condition as Contains().`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp:328` — `* @throws System::NotImplementedException always.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp:42` — `* @return true if a guest profile.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp:49` — `* @return true if signed in to Live.`
* `modules/gamer-services/include/Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp:113` — `* @return true if the microphone is a headset.`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:28` — `// (StorageDevice::GetStorageRootEXT(), platform user-data-directory backed) rather than inventing a`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:33` — `Microsoft::Xna::Framework::Storage::StorageDevice::GetStorageRootEXT())`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:91` — `std::string GetGamerServicesStoreRootEXT()`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:120` — `std::string MakeLeaderboardFileKeyEXT(const std::string& leaderboardKeyName, int gameMode)`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:125` — `std::vector<PersistedAchievement> LoadEarnedAchievementsEXT(const std::string& gamertag)`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:155` — `void SaveEarnedAchievementEXT(const std::string& gamertag, const std::string& key, long long earnedTicks)`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:157` — `std::vector<PersistedAchievement> current = LoadEarnedAchievementsEXT(gamertag);`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:312` — `std::vector<PersistedLeaderboardEntry> LoadLeaderboardEntriesEXT(const std::string& leaderboardFileKey)`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:342` — `void SaveLeaderboardEntryEXT(`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:389` — `void LoadLeaderboardEntryColumnsEXT(`
* `modules/gamer-services/src/Internal/LocalGamerServicesStore.cpp:421` — `void ResetStoreForTestingEXT()`
* `modules/gamer-services/src/Xna/Achievement.cpp:3` — `#include "System/NotImplementedException.hpp"`
* `modules/gamer-services/src/Xna/Achievement.cpp:51` — `throw System::NotImplementedException();`
* `modules/gamer-services/src/Xna/AchievementCollection.cpp:97` — `return false;`
* `modules/gamer-services/src/Xna/AchievementCollection.cpp:100` — `return true;`
* `modules/gamer-services/src/Xna/AchievementCollection.cpp:127` — `return true;`
* `modules/gamer-services/src/Xna/AvatarAnimation.cpp:16` — `, realClipName_(AvatarAnimationPresetToClipNameEXT(animationPreset))`
* `modules/gamer-services/src/Xna/AvatarAnimation.cpp:80` — `void AvatarAnimation::SetRealClipNameEXT(const std::string& clipName)`
* `modules/gamer-services/src/Xna/AvatarAnimation.cpp:85` — `const std::string& AvatarAnimation::GetRealClipNameEXT() const`
* `modules/gamer-services/src/Xna/AvatarAnimationPresetNamesEXT.cpp:7` — `std::string AvatarAnimationPresetToClipNameEXT(AvatarAnimationPreset preset)`
* `modules/gamer-services/src/Xna/AvatarBodyTypeNamesEXT.cpp:7` — `std::string AvatarBodyTypeToContentNameEXT(AvatarBodyType bodyType)`
* `modules/gamer-services/src/Xna/AvatarDescription.cpp:27` — `[[nodiscard]] bool getIsCompletedProperty() const override { return true; }`
* `modules/gamer-services/src/Xna/AvatarDescription.cpp:28` — `[[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return true; }`
* `modules/gamer-services/src/Xna/AvatarDescription.cpp:60` — `return false;`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:127` — `// Genuinely a no-op once validated, matching the real implementation.`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:130` — `void AvatarRenderer::EnableRealRenderingEXT(Graphics::GraphicsDevice& device,`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:154` — `bool AvatarRenderer::IsRealRenderingEnabledEXT() const`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:159` — `void AvatarRenderer::SetAppearanceEXT(const AvatarAppearanceEXT& appearance)`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:169` — `Microsoft::Xna::Framework::Color AvatarRenderer::PartTintEXT(const std::string& partName) const`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:178` — `void AvatarRenderer::DrawRealEXT(const std::string& animationClipName,`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:185` — `if (!IsRealRenderingEnabledEXT())`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:192` — `realModel_->ComputeBoneTransformsEXT(animationClipName, position, loop, boneTransforms);`
* `modules/gamer-services/src/Xna/AvatarRenderer.cpp:222` — `realEffect_->setDiffuseColorProperty(PartTintEXT(part.Name).ToVector3());`
* `modules/gamer-services/src/Xna/Gamer.cpp:5` — `#include "System/NotSupportedException.hpp"`
* `modules/gamer-services/src/Xna/Gamer.cpp:84` — `throw System::NotSupportedException();`
* `modules/gamer-services/src/Xna/Gamer.cpp:92` — `throw System::NotSupportedException();`
* `modules/gamer-services/src/Xna/Gamer.cpp:97` — `throw System::NotSupportedException();`
* `modules/gamer-services/src/Xna/Gamer.cpp:102` — `throw System::NotSupportedException();`
* `modules/gamer-services/src/Xna/Gamer.cpp:110` — `throw System::NotSupportedException();`
* `modules/gamer-services/src/Xna/Gamer.cpp:115` — `throw System::NotSupportedException();`
* `modules/gamer-services/src/Xna/Gamer.cpp:126` — `bool Gamer::GamerAction::getCompletedSynchronouslyProperty() const  { return false; }`
* `modules/gamer-services/src/Xna/GamerPresence.cpp:94` — `SetPresenceModeStringEXT(presence_);`
* `modules/gamer-services/src/Xna/GamerPresence.cpp:110` — `SetPresenceModeStringEXT(presence_);`
* `modules/gamer-services/src/Xna/GamerPresence.cpp:114` — `void GamerPresence::SetPresenceModeStringEXT(const std::string& /*mode*/)`
* `modules/gamer-services/src/Xna/GamerProfile.cpp:43` — `return nullptr;`
* `modules/gamer-services/src/Xna/GamerServicesDispatcher.cpp:33` — `// first (a harmless no-op the first time this runs, since getSignedInGamersProperty()`
* `modules/gamer-services/src/Xna/GamerServicesDispatcher.cpp:43` — `"Stub Gamer", isInitialized_`
* `modules/gamer-services/src/Xna/GamerServicesDispatcher.cpp:47` — `"Stub Gamer (1)", isInitialized_, true, Microsoft::Xna::Framework::PlayerIndex::Two`
* `modules/gamer-services/src/Xna/GamerServicesDispatcher.cpp:50` — `"Stub Gamer (2)", isInitialized_, true, Microsoft::Xna::Framework::PlayerIndex::Three`
* `modules/gamer-services/src/Xna/GamerServicesDispatcher.cpp:53` — `"Stub Gamer (3)", isInitialized_, true, Microsoft::Xna::Framework::PlayerIndex::Four`
* `modules/gamer-services/src/Xna/Guide.cpp:23` — `#include "System/NotSupportedException.hpp"`
* `modules/gamer-services/src/Xna/Guide.cpp:45` — `[[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return false; }`
* `modules/gamer-services/src/Xna/Guide.cpp:290` — `Input::Touch::TouchPanel::setInputSuppressedEXT(`
* `modules/gamer-services/src/Xna/Guide.cpp:486` — `const std::string& Guide::GetPendingKeyboardInputTitleForTestingEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:495` — `const std::string& Guide::GetPendingKeyboardInputDescriptionForTestingEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:504` — `std::string Guide::GetPendingKeyboardInputDisplayTextForTestingEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:513` — `bool Guide::WasKeyboardInputCanceledEXT(System::IAsyncResult* result)`
* `modules/gamer-services/src/Xna/Guide.cpp:528` — `void Guide::RenderPendingKeyboardInputEXT(`
* `modules/gamer-services/src/Xna/Guide.cpp:617` — `void Guide::SimulateKeyboardInputCancelEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:626` — `void Guide::ResetPendingKeyboardInputForTestingEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:646` — `// a permanent NotSupportedException stub, "FIXME: Surely they don't want us doing this");`
* `modules/gamer-services/src/Xna/Guide.cpp:701` — `void Guide::RenderPendingMessageBoxEXT(`
* `modules/gamer-services/src/Xna/Guide.cpp:814` — `void Guide::SimulateMessageBoxClickEXT(int buttonIndex)`
* `modules/gamer-services/src/Xna/Guide.cpp:827` — `void Guide::ResetPendingMessageBoxForTestingEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:834` — `int Guide::GetPendingMessageBoxFocusButtonForTestingEXT()`
* `modules/gamer-services/src/Xna/Guide.cpp:904` — `void Guide::ShowAchievementsEXT(Microsoft::Xna::Framework::PlayerIndex /*player*/)`
* `modules/gamer-services/src/Xna/LeaderboardEntry.cpp:34` — `void LeaderboardEntry::SetOnRatingChangedHookEXT(std::function<void()> hook)`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:19` — `// FNA.NetStub's own LeaderboardReader is identically all-NotSupportedException, so sort`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:30` — `std::vector<LeaderboardEntry> LoadFullLocalLeaderboardEXT(const LeaderboardIdentity& identity)`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:32` — `const std::string fileKey = CNA::Internal::GamerServices::MakeLeaderboardFileKeyEXT(`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:37` — `CNA::Internal::GamerServices::LoadLeaderboardEntriesEXT(fileKey);`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:61` — `CNA::Internal::GamerServices::LoadLeaderboardEntryColumnsEXT(`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:91` — `return 0;`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:110` — `[[nodiscard]] bool getIsCompletedProperty() const override { return true; }`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:111` — `[[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return true; }`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:126` — `System::IAsyncResult* CompleteReadEXT(LeaderboardReader reader, System::AsyncCallback callback, std::any asyncState)`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:137` — `System::IAsyncResult* CompletePageEXT(System::AsyncCallback callback, std::any asyncState)`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:186` — `void LeaderboardReader::ResliceEntriesEXT()`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:244` — `return CompletePageEXT(std::move(callback), std::move(asyncState));`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:248` — `// ResliceEntriesEXT()'s real [pageStart, pageStart + pageSize) window - deliberately NOT`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:253` — `// exists for PageDown/PageUp's own behavior at all (both are NotSupportedException stubs`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:263` — `ResliceEntriesEXT();`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:282` — `return CompletePageEXT(std::move(callback), std::move(asyncState));`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:292` — `ResliceEntriesEXT();`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:336` — `std::vector<LeaderboardEntry> entries = LoadFullLocalLeaderboardEXT(leaderboardId);`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:341` — `// pageStart == 0 case (see ResliceEntriesEXT()'s own doc comment) - a real caller-supplied`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:343` — `reader.ResliceEntriesEXT();`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:344` — `return CompleteReadEXT(std::move(reader), std::move(callback), std::move(asyncState));`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:354` — `std::vector<LeaderboardEntry> entries = LoadFullLocalLeaderboardEXT(leaderboardId);`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:359` — `reader.ResliceEntriesEXT();`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:360` — `return CompleteReadEXT(std::move(reader), std::move(callback), std::move(asyncState));`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:371` — `std::vector<LeaderboardEntry> allEntries = LoadFullLocalLeaderboardEXT(leaderboardId);`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:389` — `reader.ResliceEntriesEXT();`
* `modules/gamer-services/src/Xna/LeaderboardReader.cpp:390` — `return CompleteReadEXT(std::move(reader), std::move(callback), std::move(asyncState));`
* `modules/gamer-services/src/Xna/LeaderboardWriter.cpp:15` — `const std::string fileKey = CNA::Internal::GamerServices::MakeLeaderboardFileKeyEXT(`
* `modules/gamer-services/src/Xna/LeaderboardWriter.cpp:29` — `for (const auto& record : CNA::Internal::GamerServices::LoadLeaderboardEntriesEXT(fileKey))`
* `modules/gamer-services/src/Xna/LeaderboardWriter.cpp:42` — `CNA::Internal::GamerServices::LoadLeaderboardEntryColumnsEXT(`
* `modules/gamer-services/src/Xna/LeaderboardWriter.cpp:54` — `result->SetOnRatingChangedHookEXT([result, fileKey, owner]() {`
* `modules/gamer-services/src/Xna/LeaderboardWriter.cpp:58` — `CNA::Internal::GamerServices::SaveLeaderboardEntryEXT(fileKey, record, &result->getColumnsProperty());`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:4` — `#include "System/NotImplementedException.hpp"`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:5` — `#include "System/NotSupportedException.hpp"`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:51` — `return false;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:53` — `return true;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:154` — `return false;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:158` — `return true;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:171` — `return false;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:189` — `throw System::NotSupportedException(`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:229` — `return false;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:232` — `return true;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:264` — `return true;`
* `modules/gamer-services/src/Xna/PropertyDictionary.cpp:269` — `throw System::NotImplementedException();`
* `modules/gamer-services/src/Xna/SignedInGamer.cpp:57` — `return false;`
* `modules/gamer-services/src/Xna/SignedInGamer.cpp:77` — `CNA::Internal::GamerServices::SaveEarnedAchievementEXT(`
* `modules/gamer-services/src/Xna/SignedInGamer.cpp:88` — `// FNA: the overlap check on statStoreAction is intentionally a no-op — the`
* `modules/gamer-services/src/Xna/SignedInGamer.cpp:163` — `for (const auto& record : CNA::Internal::GamerServices::LoadEarnedAchievementsEXT(getGamertagProperty()))`
* `modules/gamer-services/src/Xna/SignedInGamerCollection.cpp:20` — `return nullptr;`
* `modules/net/include/CNA/Internal/Net/ENetBackend.hpp:59` — `* @return true if sessionType should be backed by a real ENet host.`
* `modules/net/include/CNA/Internal/Net/ENetBackend.hpp:104` — `* Safe to call even if no transport state is registered for session (no-op).`
* `modules/net/include/CNA/Internal/Net/ENetDiscoveryService.hpp:35` — `* method below is a no-op there (RegisterHost/UnregisterHost/Poll do nothing; FindSessions`
* `modules/net/include/CNA/Internal/Net/ENetDiscoveryService.hpp:36` — `* always returns empty). This is a permanent platform constraint, not a TODO.`
* `modules/net/include/CNA/Internal/Net/ENetDiscoveryService.hpp:64` — `* registered (in which case this is a no-op).`
* `modules/net/include/CNA/Internal/Net/ENetHostHandle.hpp:77` — `* @return true if valid.`
* `modules/net/include/Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp:69` — `* @return true if the comparable fields are all equal.`
* `modules/net/include/Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp:77` — `* @return true if any comparable field differs.`
* `modules/net/include/Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp:29` — `* @return true if disposed.`
* `modules/net/include/Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp:29` — `* @return true if a packet is available.`
* `modules/net/include/Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp:43` — `* Overrides NetworkGamer::getIsLocalProperty() to return true, matching FNA's`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:23` — `* @return true if the gamer has left the session.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:44` — `* @return true if voice is available.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:71` — `* @return true if the gamer is a guest.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:78` — `* @return true if the gamer is the session host.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:107` — `* @return true if this gamer is a LocalNetworkGamer.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:114` — `* @return true if muted by the local user.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:121` — `* @return true if the gamer occupies a private slot.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:128` — `* @return true if the gamer is ready.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:142` — `* @return true if the gamer is talking.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:188` — `* @param gamertag The gamer's gamertag. Defaults to "Stub Gamer", matching FNA's stub`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:192` — `CNAEXT static NetworkGamer CreateInternal(NetworkSession* session, const std::string& gamertag = "Stub Gamer");`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:199` — `* @param gamertag The gamer's gamertag. Defaults to "Stub Gamer", matching FNA's stub.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkGamer.hpp:201` — `explicit NetworkGamer(NetworkSession* session, const std::string& gamertag = "Stub Gamer");`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkMachine.hpp:26` — `* Always throws NotImplementedException, matching FNA's stub.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp:126` — `* @return true if disposed.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp:178` — `* @return true if host migration is allowed.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp:195` — `* @return true if join-in-progress is allowed.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp:230` — `* @return true if all local gamers are ready.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp:237` — `* @return true if a local gamer is host.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSession.hpp:408` — `* Task 12.1: idempotent - a second and every subsequent call is a safe no-op. All of`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp:49` — `* source carries a "TODO: Expand list to index size?" comment).`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp:85` — `* (its reference source carries a "TODO: Expand list to index size?" comment).`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp:136` — `* @return true if a value was removed; otherwise false.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp:144` — `* @return true if found; otherwise false.`
* `modules/net/include/Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp:197` — `* @return true if there is a next element; otherwise false.`
* `modules/net/include/Microsoft/Xna/Framework/Net/QualityOfService.hpp:38` — `* @return true if available.`
* `modules/net/include/Microsoft/Xna/Framework/Net/QualityOfService.hpp:54` — `* acknowledged upstream stub, its own source carries a "TODO: Everything below" comment),`
* `modules/net/include/Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp:26` — `* @return true if the gamer is leaving.`
* `modules/net/src/Internal/ENetBackend.cpp:86` — `return false;`
* `modules/net/src/Internal/ENetBackend.cpp:90` — `return true;`
* `modules/net/src/Internal/ENetBackend.cpp:233` — `// always reports "Stub Gamer" (an unchanged, preserved FNA stub behavior — see`
* `modules/net/src/Internal/ENetBackend.cpp:297` — `return false;`
* `modules/net/src/Internal/ENetBackend.cpp:303` — `return false;`
* `modules/net/src/Internal/ENetBackend.cpp:306` — `return true;`
* `modules/net/src/Internal/ENetBackend.cpp:776` — `return false;`
* `modules/net/src/Internal/ENetBackend.cpp:803` — `return false; // handshake never completed far enough to even learn who the host was`
* `modules/net/src/Internal/ENetBackend.cpp:816` — `return false; // nothing left to migrate to (shouldn't happen - own locals survive)`
* `modules/net/src/Internal/ENetBackend.cpp:888` — `return true;`
* `modules/net/src/Internal/ENetBackend.cpp:928` — `// wrapper's own non-Emscripten body once StartHosting's redundant no-op is`
* `modules/net/src/Internal/ENetBackend.cpp:933` — `return true;`
* `modules/net/src/Internal/ENetBackend.cpp:938` — `return false;`
* `modules/net/src/Internal/ENetBackend.cpp:1180` — `// rollback and no way to retry (StartHosting is a no-op once Sessions() already contains`
* `modules/net/src/Internal/ENetBackend.cpp:1269` — `return 0;`
* `modules/net/src/Internal/ENetBackend.cpp:1289` — `return 0;`
* `modules/net/src/Internal/ENetBackend.cpp:1304` — `return {};`
* `modules/net/src/Internal/ENetDiscoveryService.cpp:279` — `return false;`
* `modules/net/src/Internal/ENetDiscoveryService.cpp:290` — `return false;`
* `modules/net/src/Internal/ENetDiscoveryService.cpp:295` — `return true;`
* `modules/net/src/Internal/ENetDiscoveryService.cpp:328` — `return {};`
* `modules/net/src/Internal/ENetDiscoveryService.cpp:371` — `// not a TODO (see NEXT.md and this class's own header doc comment).`
* `modules/net/src/Internal/ENetDiscoveryService.cpp:379` — `return {};`
* `modules/net/src/Internal/NetPacketCodec.cpp:340` — `return 0;`
* `modules/net/src/Xna/LocalNetworkGamer.cpp:39` — `bool LocalNetworkGamer::getIsLocalProperty() const { return true; }`
* `modules/net/src/Xna/LocalNetworkGamer.cpp:59` — `return 0;`
* `modules/net/src/Xna/LocalNetworkGamer.cpp:105` — `return 0;`
* `modules/net/src/Xna/NetworkGamer.cpp:27` — `bool NetworkGamer::getIsLocalProperty() const             { return false; }`
* `modules/net/src/Xna/NetworkMachine.cpp:4` — `#include "System/NotImplementedException.hpp"`
* `modules/net/src/Xna/NetworkMachine.cpp:25` — `throw System::NotImplementedException();`
* `modules/net/src/Xna/NetworkSession.cpp:80` — `// Update() is a permanently empty no-op in both FNA and CNA, so once a`
* `modules/net/src/Xna/NetworkSession.cpp:109` — `bool NetworkSession::NetworkSessionAction::getCompletedSynchronouslyProperty() const { return false; }`
* `modules/net/src/Xna/NetworkSession.cpp:297` — `if (!gamer->getIsReadyProperty()) return false;`
* `modules/net/src/Xna/NetworkSession.cpp:299` — `return true;`
* `modules/net/src/Xna/NetworkSession.cpp:306` — `if (gamer->getIsHostProperty()) return true;`
* `modules/net/src/Xna/NetworkSession.cpp:308` — `return false;`
* `modules/net/src/Xna/NetworkSession.cpp:409` — `// pre-Phase-5 behavior byte-for-byte: PacketSend stays a complete no-op for them`
* `modules/net/src/Xna/NetworkSession.cpp:500` — `return nullptr;`
* `modules/net/src/Xna/NetworkSessionProperties.cpp:23` — `// index (the reference source itself has a "TODO: Expand list to index size?" comment).`
* `modules/net/src/Xna/NetworkSessionProperties.cpp:43` — `// "TODO: Expand list to index size?" behavior), replaces in place otherwise.`
* `modules/net/src/Xna/NetworkSessionProperties.cpp:76` — `return true;`
* `modules/net/src/Xna/NetworkSessionProperties.cpp:89` — `return false;`
* `modules/net/src/Xna/NetworkSessionProperties.cpp:92` — `return true;`
