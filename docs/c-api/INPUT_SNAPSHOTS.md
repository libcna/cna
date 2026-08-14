# CNA C API Input Snapshots

## Keyboard capture

`cna_keyboard_get_state` performs one canonical `Keyboard::GetState()` call and copies the result
into a caller-owned `CNA_KeyboardState`. The required game handle establishes the active runtime
and creation-thread boundary; the returned value is not a borrowed native object and remains usable
after the callback or game ends.

Capture is point-in-time, not frame-cached. Two calls during one update or draw callback perform two
separate native captures and may observe different state if the platform input bridge changes
between them. Capture must run on the game's creation/graphics thread and returns
`CNA_RESULT_THREAD` otherwise.

## Snapshot layout and queries

The version 1 snapshot contains four 64-bit words covering the same 256 numeric key slots as FNA's
keyboard bitfield. Key `N` occupies bit `N % 64` of word `N / 64`. `input.h` names every one of the
160 canonical `Keys` identities; a build/CTest audit compares the complete C list and every numeric
value directly with CNA's native `Keys.hpp`.

`is_key_down`, `is_key_up`, pressed-count and ascending count/copy functions inspect only the POD
snapshot. They do not call CNA, SDL or a platform backend, so a copied snapshot may be queried from
any thread. Key slots 0 through 255 are valid even where the sparse XNA enum has no named value;
unnamed slots from a native capture remain clear. Values above 255 are invalid arguments. An
undersized pressed-key destination receives the required count and no partial array.

## Initial scope boundary

The initial input slice deliberately exposes no live `KeyboardState` pointer, per-key native call,
event subscription or callback. Player-index keyboard capture, keyboard naming extensions, mouse,
game-pad and touch snapshots remain outside this slice and require their own layouts and platform
semantics before implementation.
