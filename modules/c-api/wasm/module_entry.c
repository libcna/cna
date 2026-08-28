/* SPDX-License-Identifier: MS-PL */

/*
 * plans/plan_cabi.md CABI-14: the wasm module's translation unit.
 *
 * Emscripten links a *program*, so the module needs one even though nothing here is called:
 * --no-entry says there is no main, and every route the module exposes comes from the static
 * library through -sEXPORTED_FUNCTIONS. This file exists only to give that link a root.
 *
 * It deliberately declares nothing of its own. The export list is generated from the public
 * headers, so a route added here would be invisible to it -- and the version query a browser
 * consumer needs first is already public as cna_get_abi_version().
 */

#include <CNA/C/abi.h>
