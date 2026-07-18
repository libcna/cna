# AUDIT_CROSS_CUTTING_FINDINGS.md

**Status: SKELETON — populated incrementally during Pass 2 as patterns spanning multiple files emerge, finalized
in Pass 5.**

Each entry references the per-file audit reports that provide evidence rather than restating their detail.
Organize by category as entries accumulate.

## Known pre-existing issue to actively cross-check (from `known_bugs.md`, consulted as secondary context per D-3)

- "Multiple SpriteBatch Begin/End in one frame discards all but the last" — check whether this is still reproducible
  against current `SpriteBatch` source, which backend(s) it affects, and whether it's backend-specific or a shared
  `Microsoft::Xna::Framework::Graphics::SpriteBatch` logic bug. Link the corresponding per-file finding here once
  the `xna-graphics` / `tests-xna-graphics` shards are audited.

## Architecture

_(pending)_

## Duplicated backend logic

_(pending)_

## Recurring memory/resource risk patterns

_(pending)_

## Recurring performance risk patterns

_(pending)_

## Systematic FNA parity gaps

_(pending — see also Pass 3 in AUDIT_PROGRESS.md)_

## Recurring testing gaps

_(pending)_

## Build-system inconsistencies

_(pending)_
