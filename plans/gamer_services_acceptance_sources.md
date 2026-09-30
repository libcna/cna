# Gamer Services standard-API acceptance sources

> **Historical (2026-09-28).** Every "remaining prerequisite" below was met: SAMPLE-075, SAMPLE-087,
> SAMPLE-096 and the achievements/leaderboards compatibility program were ported and accepted
> (last run 2026-09-30, `plan_gamer_services_xbox_fidelity.md` acceptance matrix). Current
> limitations: `docs/gamer-services-known-limitations.md`.

This is an evidence/porting queue, not a claim that these samples are unblocked. Source inspection
only, no sample-agent files changed and no Xbox Avatar model/assets inspected or extracted.

| Area | Existing legal source | Required unchanged game logic | Remaining prerequisite |
| --- | --- | --- | --- |
| identity/Guide | `/rv/tmp/samples/SAMPLE-075-NGSMSample_4_0/xna4-original/NetworkStateManagement/Networking/ProfileSignInScreen.cs:116` and SAMPLE-096 InvitesGame.cs:123 | GamerServicesComponent, Guide.ShowSignIn, signed-in player selection | Native acceptance adaptation and system-overlay automated input |
| invites/networking | `/rv/tmp/samples/SAMPLE-096-InvitesSample_4_0/xna4-original/Invites/InvitesGame.cs:69,203` | Subscribe InviteAccepted, dispose prior NetworkSession, JoinInvited(maxLocalGamers), subscribe GamerJoined/SessionEnded | Client directory/Guide acceptance/event integration and authenticated realtime relay |
| avatar | `/rv/tmp/samples/SAMPLE-087-AvatarShadows_4_0/xna4-original/AvatarShadows/AvatarShadows/Avatar.cs:50` | AvatarAnimation preset chosen from 30 values, AvatarDescription.CreateRandom, AvatarRenderer(description,false), standard Draw | Standard renderer/description/animation migration and original CNA assets |
| achievements/leaderboards | No matching GetAchievements/AwardAchievement/LeaderboardReader source found by rg across the currently extracted `/rv/tmp/samples` C# corpus | Minimal XNA-shaped achievement/board scenario using standard APIs, no service calls in game logic | Build a compatibility sample; existing native service harness is integration infrastructure, not an original sample port |

Invites sample's handler at line 203 confirms that acceptance must occur in Guide before the game
callback: it immediately disposes its old session and calls JoinInvited without another confirmation.
Subscription also enables Guide Invite/Join-In-Progress choices according to the accompanying
Invites.htm lines 892–896. Receiving an inbox item must not raise InviteAccepted. This source does
not establish Ranked invitation/arbitration rules or Internet transport security.

AvatarShadows creates renderer/animation entirely with the standard XNA API. Port its logic using
new original procedural CNA geometry/rig/animations; do not copy or extract the sample's proprietary
Avatar resources. Its public preset selection expects every enum 0..29 to produce a useful animation.
The Windows unavailable renderer is not an acceptable reference for that Xbox feature.

When prerequisites pass, record actual standard-API compilation/run, independent client processes,
server restarts and private-runner visual output for each port. Keep deployment endpoint/title outside
gameplay code. Necessary build/transpilation changes are acceptable; no avatar rendering EXT calls
or CNA matchmaking/account calls may replace the standard XNA game logic.
