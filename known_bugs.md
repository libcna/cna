# CNA known bugs

This file no longer keeps a list. The one authoritative list of current known bugs and limitations
is [`NEXT.md`](NEXT.md) §5; the measurements behind some of its entries are in
[`misc/known_bugs.md`](misc/known_bugs.md).

The two entries this file last held were verified fixed on 2026-10-10: FNA3D resources outliving
their device (`d5c3460db`) and several `SpriteBatch` `Begin`/`End` pairs per frame on Vulkan
(`7dd71cd88`). Earlier entries are in Git history.
