# CNA-local ENet fixes

## 2026-10-22 — unsequenced receive window

`protocol.c`, `enet_protocol_handle_send_unsequenced`: both bit masks use `1u`
instead of signed `1`. Bit 31 otherwise has undefined behavior in C; UBSan
reproduced this through the actual receive handler. Wire format, duplicate
suppression and window advancement are unchanged.

Regression: `modules/net/tests/enet_unsequenced_window_test.c`, registered as
`CnaENetUnsequencedTests` under native Linux GCC/Clang with UBSan. It exercises
the real parser/queue/receive implementation without sockets, including all
bits, duplicates, rollover and stale packets. This note records the museum
repair, not a complete inventory of older vendor/build adaptations.

## 2026-10-24 — fragment bitmaps

`protocol.c`, `enet_protocol_handle_send_fragment` and
`enet_protocol_handle_send_unreliable_fragment`: the received-fragment bitmap masks
use `1u` instead of signed `1`, the same undefined bit-31 shift as the unsequenced
window, found reading the file after the museum's report of that one. Reassembly,
duplicate suppression and the wire format are unchanged.

Regression: `modules/net/tests/enet_fragment_bitmap_test.c`, registered as
`CnaENetFragmentTests` with UBSan. It reassembles a 33-fragment packet through each
real handler, so fragment 31 sets bit 31 of the first word and fragment 32 bit 0 of
the next, and repeats fragment 31 to check the duplicate is not counted.
