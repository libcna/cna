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

#include <CNA/C/cna.h>

#include <stddef.h>

/* The browser regression writes these public structures through the wasm heap. Keep that measured
 * wasm32 layout pinned at compile time instead of letting JavaScript silently follow a native ABI
 * layout or a future accidental padding change. */
_Static_assert(sizeof(void*) == 4U, "The C API wasm artifact requires wasm32 pointers.");
_Static_assert(sizeof(CNA_GameCallbacks) == 32U, "Unexpected wasm32 CNA_GameCallbacks size.");
_Static_assert(offsetof(CNA_GameCallbacks, update) == 12U,
    "Unexpected wasm32 CNA_GameCallbacks::update offset.");
_Static_assert(offsetof(CNA_GameCallbacks, draw) == 16U,
    "Unexpected wasm32 CNA_GameCallbacks::draw offset.");
_Static_assert(sizeof(CNA_GameCreateInfo) == 48U, "Unexpected wasm32 CNA_GameCreateInfo size.");
_Static_assert(offsetof(CNA_GameCreateInfo, target_elapsed_time_ticks) == 16U,
    "Unexpected wasm32 CNA_GameCreateInfo::target_elapsed_time_ticks offset.");
_Static_assert(offsetof(CNA_GameCreateInfo, window_title) == 24U,
    "Unexpected wasm32 CNA_GameCreateInfo::window_title offset.");
_Static_assert(offsetof(CNA_GameCreateInfo, callbacks) == 40U,
    "Unexpected wasm32 CNA_GameCreateInfo::callbacks offset.");
_Static_assert(offsetof(CNA_GameCallbacks, load_content) == 8U,
    "Unexpected wasm32 CNA_GameCallbacks::load_content offset.");
_Static_assert(sizeof(CNA_PresentationParameters) == 44U,
    "Unexpected wasm32 CNA_PresentationParameters size.");
_Static_assert(sizeof(CNA_RenderTarget2DCreateInfo) == 40U,
    "Unexpected wasm32 CNA_RenderTarget2DCreateInfo size.");
_Static_assert(offsetof(CNA_RenderTarget2DCreateInfo, format) == 20U &&
        offsetof(CNA_RenderTarget2DCreateInfo, usage) == 32U,
    "Unexpected wasm32 CNA_RenderTarget2DCreateInfo field offsets.");
_Static_assert(sizeof(CNA_RenderTargetBinding) == 24U &&
        offsetof(CNA_RenderTargetBinding, render_target) == 8U &&
        offsetof(CNA_RenderTargetBinding, array_slice) == 16U,
    "Unexpected wasm32 CNA_RenderTargetBinding layout.");
_Static_assert(sizeof(CNA_UserPrimitives) == 48U &&
        offsetof(CNA_UserPrimitives, vertex_data) == 16U &&
        offsetof(CNA_UserPrimitives, vertex_declaration) == 24U &&
        offsetof(CNA_UserPrimitives, vertex_offset) == 32U,
    "Unexpected wasm32 CNA_UserPrimitives layout.");
_Static_assert(sizeof(CNA_VertexPositionColor) == 16U,
    "Unexpected wasm32 CNA_VertexPositionColor size.");
