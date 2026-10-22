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
