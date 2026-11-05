# Downstream migrations

What other repositories must change because of a CNA change. CNA does not edit these repositories itself
(CLAUDE.md: "If old game code breaks after the API is corrected, fix the game code separately").
Remove an entry when its migration has landed. Retired code (`cna-cemetery`) and vendored CNA copies
(`openeggbert/mobile-eggbertlite/cna`) are not migrated.

## Net (TQ-039, `60e236c92`)

| Repository | What |
|---|---|
| cna-gamer-services-server | Rerun `service_cna_session_migration`, `service_cna_session_host_crash` and `service_cna_session_add_gamer` against a rebuilt `cna_service_session_client_harness`. The harness now expects `HostChanged {"", "Bob"}`. The server currently has no build tree. |

## Net host bits, local leaves and packet capacities (TQ-043)

`NetworkGamer.IsHost` is now the gamer's own bit (only the host gamer reports it), and `NetworkSession.IsHost` is the
session's own flag. A host change takes effect when it arrives, and `Update` raises `HostChanged` after the old host's
`GamerLeft`. Removing a local gamer raises `GamerLeft`; only the machine's last local gamer ends the session.
`PacketWriter(n)`/`PacketReader(n)` allocate `n` bytes, and `ReceiveData(PacketReader)` on an empty queue refuses a reader
that never had a buffer (`ArgumentOutOfRangeException("offset")` / `CNA_RESULT_INVALID_ARGUMENT`).

| Repository | Call sites |
|---|---|
| cna-samples | The ported XNA network samples now show XNA's single host marker; rerun the split-screen host case. No code change. |

## Content pipeline processor version 6 and writer identity 3 (TQ-038 `8401ac7e1`, TQ-050, TQ-063)

The canonical texture route now follows XNA's step order and resize kernel (TQ-038, version 5) and keeps 16-bit PNGs
as Rgba64 (TQ-050, version 6), so its output bytes differ. Rebuild any committed or cached canonical-route output. Incremental
builds rebuild it on their own, because the processor's identity carries the version
(`TextureProcessor::Identity`).

TQ-063 changes the writer side the same way: XNA-façade writer dispatch, raw-object and initialization manifests now
follow Refresh, so CNA-written `.xnb` bytes can differ. The façade writer cache identity moves to `3;<digest>` and the
canonical XNB codec identity to 2, so incremental builds rebuild on their own; rebuild committed CNA-written `.xnb`
files that are compared byte for byte. Custom `ContentTypeWriter`s must pass `WriteExternalReference` the compiled
`.xnb` name (any letter case), a real (non-null) root object and defined `TargetPlatform`/`GraphicsProfile` values;
native C++ writer plugins must be rebuilt (the type-writer bridge gained a protected CNAEXT `IsNullValue`). The
in-repository callers checked (cna-demos `content.custom.xml_xnb_pipeline`, CNA's own `xna-custom-pipeline` example)
already pass `.xnb` names.
