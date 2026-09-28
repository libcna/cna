# CNA Gamer Services deployment

The new CNA service has its own accounts/protocol/assets and is not Xbox LIVE compatible. The implementation is in progress: see [living plan](../plans/plan_gamer_services_server.md). Symbol coverage is not behavioral completeness.

Configure outside ported gameplay code. Precedence is complete `CNA::GamerServices::setConfigurationOverride` (deployment/test hosts only), environment fields, title manifest, user settings. User settings default to `$XDG_CONFIG_HOME/cna/gamer-services.json` or `$HOME/.config/cna/gamer-services.json`. Title manifest defaults to `cna-title.json` in launch CWD or explicit `CNA_GAMER_SERVICES_MANIFEST`. No credential fields are accepted. Endpoint is full `https://host:port/cna/v1`; `gameId` must be administrator-provisioned and stable. Optional `caBundle` configures trust, never bypasses TLS checks.

Environment: `CNA_GAMER_SERVICES_ENDPOINT`, `CNA_GAME_ID`, `CNA_GAMER_SERVICES_CA_BUNDLE`, `CNA_GAMER_SERVICES_INSECURE_LOOPBACK=1` (explicit development only). Insecure HTTP is restricted to numeric `127.0.0.1` or `[::1]`, proxies and redirects disabled. No insecure Internet/LAN default. No endpoint means no service accounts, never fabricated Stub Gamers after backend migration. Credentials are user data, never title manifest values.

```json
{"endpoint":"https://service.example/cna/v1","gameId":"my-title"}
```

Server canonical protocol and administration commands live in sibling `cna-gamer-services-server/README.md` and `protocol/v1.md`. Desktop client depends on libcurl with TLS support and nlohmann/json. Browser/other secure platform transport integration remains unverified.

Measured service features include four local authenticated players, standard Guide sign-in, profiles/
lookup, achievements (metadata/awards), mutual friends and rich presence. Standard ShowFriends,
ShowGamerCard and ShowFriendRequest use CNA system overlays. Picture/assets, refresh/reconnect,
leaderboards, PlayerMatch/Ranked/invites/relay and standard Avatar migration remain unfinished.
Rich presence is sent during Dispatcher.Update; friend online state currently reflects authenticated
activity within 90 seconds, so idle heartbeat must still be added. Do not treat this as a production
service release or a claim of measured Xbox event/validation parity across every method.
