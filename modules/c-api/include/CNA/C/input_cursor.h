// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_CURSOR_H
#define CNA_C_INPUT_CURSOR_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned mouse-cursor handle. */
typedef CNA_Handle CNA_MouseCursorHandle;

/** @brief Fixed-width identity of one of the stock system cursors. */
typedef uint32_t CNA_MouseCursorStock;

/** @brief The default arrow. */
#define CNA_MOUSE_CURSOR_STOCK_ARROW UINT32_C(0)
/** @brief The crosshair. */
#define CNA_MOUSE_CURSOR_STOCK_CROSSHAIR UINT32_C(1)
/** @brief The pointing hand. */
#define CNA_MOUSE_CURSOR_STOCK_HAND UINT32_C(2)
/** @brief The text I-beam. */
#define CNA_MOUSE_CURSOR_STOCK_IBEAM UINT32_C(3)
/** @brief The not-allowed cursor. */
#define CNA_MOUSE_CURSOR_STOCK_NO UINT32_C(4)
/** @brief The move-in-any-direction cursor. */
#define CNA_MOUSE_CURSOR_STOCK_SIZE_ALL UINT32_C(5)
/** @brief The north-east/south-west resize cursor. */
#define CNA_MOUSE_CURSOR_STOCK_SIZE_NESW UINT32_C(6)
/** @brief The north/south resize cursor. */
#define CNA_MOUSE_CURSOR_STOCK_SIZE_NS UINT32_C(7)
/** @brief The north-west/south-east resize cursor. */
#define CNA_MOUSE_CURSOR_STOCK_SIZE_NWSE UINT32_C(8)
/** @brief The west/east resize cursor. */
#define CNA_MOUSE_CURSOR_STOCK_SIZE_WE UINT32_C(9)
/** @brief The wait cursor. */
#define CNA_MOUSE_CURSOR_STOCK_WAIT UINT32_C(10)
/** @brief The wait-with-arrow cursor. */
#define CNA_MOUSE_CURSOR_STOCK_WAIT_ARROW UINT32_C(11)

/**
 * @brief Creates an empty mouse cursor.
 *
 * @param out_cursor Receives an owned cursor handle that wraps no native cursor.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This reproduces the canonical default constructor, which produces a cursor with nothing behind
 * it. Setting it is accepted and changes nothing.
 */
CNA_C_API CNA_Result cna_mouse_cursor_create_ext(CNA_MouseCursorHandle* out_cursor);

/**
 * @brief Borrows one of the stock system cursors.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param stock One `CNA_MOUSE_CURSOR_STOCK_*` identity.
 * @param out_cursor Receives a handle naming the process-wide stock cursor.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * All twelve canonical stock properties collapse into this one route. The canonical stock cursors
 * are **process-lifetime singletons whose disposal is a deliberate no-op**, so the handle is a
 * borrowed view: destroying it releases the handle and never frees the shared native cursor, and
 * `cna_mouse_cursor_dispose` on it succeeds without doing anything — exactly as the canonical
 * `Dispose` does. Two calls for the same identity name the same underlying cursor.
 */
CNA_C_API CNA_Result cna_mouse_cursor_get_stock_ext(
    CNA_Handle game,
    CNA_MouseCursorStock stock,
    CNA_MouseCursorHandle* out_cursor);

/**
 * @brief Creates a cursor from a texture.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param texture Owned Texture2D handle to draw the cursor from.
 * @param origin_x Horizontal hot-spot offset inside the texture.
 * @param origin_y Vertical hot-spot offset inside the texture.
 * @param out_cursor Receives an owned cursor handle.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` when the texture's format is not a
 *         canonical color format, matching the canonical refusal; or a documented
 *         handle/thread/native failure.
 *
 * The returned cursor owns its native resource and must be released with
 * `cna_mouse_cursor_destroy`. It does not keep the texture alive: the canonical factory copies the
 * pixels, so the texture may be released immediately afterwards.
 */
CNA_C_API CNA_Result cna_mouse_cursor_create_from_texture2d(
    CNA_Handle game,
    CNA_Handle texture,
    int32_t origin_x,
    int32_t origin_y,
    CNA_MouseCursorHandle* out_cursor);

/**
 * @brief Disposes a cursor's native resource without releasing its handle.
 *
 * @param cursor Owned cursor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Disposal is idempotent, and on a stock cursor it is a deliberate no-op, because the canonical
 * singleton must survive for the process. The handle stays valid afterwards and still needs
 * `cna_mouse_cursor_destroy`.
 */
CNA_C_API CNA_Result cna_mouse_cursor_dispose(CNA_MouseCursorHandle cursor);

/**
 * @brief Releases a cursor handle.
 *
 * @param cursor Owned cursor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing a created cursor frees its native resource; releasing a stock cursor frees only the
 * handle.
 */
CNA_C_API CNA_Result cna_mouse_cursor_destroy(CNA_MouseCursorHandle cursor);

/**
 * @brief Makes a cursor the active one.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param cursor Owned or stock cursor handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical operation returns nothing, so a backend that cannot change the cursor — and a
 * cursor with nothing behind it — succeed and change nothing. The C API does not keep the cursor
 * alive on the caller's behalf: releasing it while it is the active cursor is the caller's
 * responsibility to avoid, exactly as with the canonical reference.
 */
CNA_C_API CNA_Result cna_mouse_set_cursor_ext(CNA_Handle game, CNA_MouseCursorHandle cursor);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_CURSOR_H
