// SPDX-License-Identifier: MS-PL
#ifndef CNA_PROBE_PRODUCER
#error The producer lost its private build support.
#endif

// Deliberately not constant-foldable: the installed archive needs the UBSan
// runtime even though the consumer does not request sanitizer instrumentation.
int probe_add(int left, int right) { return left + right; }
void probe_throw() { throw 42; }
