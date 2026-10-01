# Operation cross-reference

Literal call-site inventory, not a runtime coverage claim. Fake-only occurrences are explicitly marked. Shared transport validation/timeout rules are described in the architecture document.

| Operation | CNA literal occurrences | Server dispatch/handler |
|---|---|---|
| `achievements.award` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:274` | `server/src/Service.cpp` via `Service.cpp:151` |
| `achievements.list` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:261` | `server/src/Service.cpp` via `Service.cpp:151` |
| `assets.read` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:541`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:712` | `server/src/Service.cpp` via `Service.cpp:151` |
| `auth.login` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:181`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:640` | `server/src/Service.cpp` via `Service.cpp:151` |
| `auth.logout` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:195`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:200`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:217`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:823` | `server/src/Service.cpp` via `Service.cpp:151` |
| `auth.ping` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:243`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:767` | `server/src/Service.cpp` via `Service.cpp:151` |
| `auth.refresh` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:640`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:701`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:813` | `server/src/Authentication.cpp` via `Service.cpp:151` |
| `avatars.catalog` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:528` | `server/src/Avatars.cpp` via `Service.cpp:151` |
| `avatars.catalogPack` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:561`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:713` | `server/src/Avatars.cpp` via `Service.cpp:151` |
| `avatars.get` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:489` | `server/src/Avatars.cpp` via `Service.cpp:151` |
| `avatars.set` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:520` | `server/src/Avatars.cpp` via `Service.cpp:151` |
| `friends.accept` | Dynamic operation concatenation: `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:302` | `server/src/Service.cpp` via `Service.cpp:151` |
| `friends.add` | Dynamic operation concatenation: `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:302` | `server/src/Service.cpp` via `Service.cpp:151` |
| `friends.list` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:280` | `server/src/Service.cpp` via `Service.cpp:151` |
| `friends.remove` | Dynamic operation concatenation: `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:302` | `server/src/Service.cpp` via `Service.cpp:151` |
| `gamer.lookup` | No native caller found; native lookup uses `profile.get` | `server/src/Service.cpp` via `Service.cpp:151` |
| `hello` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:749` | `server/src/Service.cpp` via `Service.cpp:151` |
| `invites.accept` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:241` | `server/src/Invitations.cpp` via `Service.cpp:151` |
| `invites.dismiss` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:244` | `server/src/Invitations.cpp` via `Service.cpp:151` |
| `invites.get` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:239` | `server/src/Invitations.cpp` via `Service.cpp:151` |
| `invites.joinFriend` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:710`<br>`modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:225` | `server/src/Invitations.cpp` via `Service.cpp:151` |
| `invites.list` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:230` | `server/src/Invitations.cpp` via `Service.cpp:151` |
| `invites.send` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:220` | `server/src/Invitations.cpp` via `Service.cpp:151` |
| `leaderboards.definition` | Server route exists; no native caller found | `server/src/Service.cpp` via `Service.cpp:151` |
| `leaderboards.game.abort` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:457`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:705` | `server/src/Leaderboards.cpp` via `Service.cpp:151` |
| `leaderboards.game.begin` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:452` | `server/src/Leaderboards.cpp` via `Service.cpp:151` |
| `leaderboards.game.commit` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:471` | `server/src/Leaderboards.cpp` via `Service.cpp:151` |
| `leaderboards.list` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:384`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:707` | `server/src/Service.cpp` via `Service.cpp:151` |
| `leaderboards.read` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:409` | `server/src/Leaderboards.cpp` via `Service.cpp:151` |
| `messages.delete` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:341` | `server/src/Service.cpp` via `Service.cpp:151` |
| `messages.list` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:326` | `server/src/Service.cpp` via `Service.cpp:151` |
| `messages.read` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:341` | `server/src/Service.cpp` via `Service.cpp:151` |
| `messages.send` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:317` | `server/src/Service.cpp` via `Service.cpp:151` |
| `parties.accept` | Dynamic operation concatenation: `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:379` | `server/src/Parties.cpp` via `Service.cpp:151` |
| `parties.decline` | Dynamic operation concatenation: `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:379` | `server/src/Parties.cpp` via `Service.cpp:151` |
| `parties.get` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:376` | `server/src/Parties.cpp` via `Service.cpp:151` |
| `parties.invite` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:378` | `server/src/Parties.cpp` via `Service.cpp:151` |
| `parties.leave` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:380` | `server/src/Parties.cpp` via `Service.cpp:151` |
| `presence.set` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:305`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:714` | `server/src/Service.cpp` via `Service.cpp:151` |
| `presence.status` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:314` | `server/src/Service.cpp` via `Service.cpp:151` |
| `privacy.block` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:323` | `server/src/Privacy.cpp` via `Service.cpp:151` |
| `privacy.list` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:320`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:853` | `server/src/Privacy.cpp` via `Service.cpp:151` |
| `privacy.unblock` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:323` | `server/src/Privacy.cpp` via `Service.cpp:151` |
| `profile.gameDefaults` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:866` | `server/src/Service.cpp` via `Service.cpp:151` |
| `profile.get` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:259` | `server/src/Service.cpp` via `Service.cpp:151` |
| `profile.setGameDefaults` | Server route exists; no native caller found | `server/src/Service.cpp` via `Service.cpp:151` |
| `profile.setGamerZone` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:309` | `server/src/Service.cpp` via `Service.cpp:151` |
| `reviews.submit` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:345`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:704` | `server/src/Service.cpp` via `Service.cpp:151` |
| `sessions.addMembers` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:194` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.create` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:140` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.find` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:151` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.get` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:171` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.join` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:168` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.joinInvited` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:718`<br>`modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:168` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.leave` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:201` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.relayTicket` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:640`<br>`modules/gamer-services/src/Internal/GamerServicesBackend.cpp:716`<br>`modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:206` | `server/src/RelayAuthorization.cpp` via `Service.cpp:151` |
| `sessions.remove` | `modules/gamer-services/src/Internal/GamerServicesBackend.cpp:717`<br>`modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:184` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.touch` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:172` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
| `sessions.update` | `modules/gamer-services/src/Internal/ServiceSessionDirectoryClient.cpp:176` | `server/src/SessionDirectory.cpp` via `Service.cpp:151` |
