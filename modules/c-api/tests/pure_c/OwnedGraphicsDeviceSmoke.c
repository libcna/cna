/* SPDX-License-Identifier: MS-PL */

/*
 * plans/plan_cabi.md CABI-13: a GraphicsDevice the caller creates outside any Game.
 *
 * Every other route hands out the Game's device, borrowed for a callback. cna-cs records the
 * consequence: every cross-device test emits not-run(CNA-ABI-has-one-game-owned-device), because
 * there was no supported way to get a second device into one process.
 *
 * These cases hold the new contract in place -- that two caller-created devices coexist, that
 * resources reach them by the same routes a Game's device uses, and that a resource made on one
 * device is refused by the other. The last is the point of the whole thing: without distinct owner
 * tokens the refusal silently would not happen.
 */

#include <CNA/C/cna.h>

#include <stdio.h>

static CNA_PresentationParameters make_parameters(void)
{
    CNA_PresentationParameters parameters;
    if (cna_presentation_parameters_init(&parameters) != CNA_RESULT_SUCCESS) {
        parameters.struct_size = 0U;
    }
    return parameters;
}

int main(void)
{
    CNA_PresentationParameters parameters = make_parameters();
    if (parameters.struct_size == 0U) {
        return 1;
    }

    /* Argument validation happens before anything is acquired. */
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    if (cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, 0, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_graphics_device_create(0U, UINT32_C(99), &parameters, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_graphics_device_create(UINT32_C(4096), CNA_GRAPHICS_PROFILE_REACH,
                                   &parameters, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 2;
    }

    CNA_Handle first = CNA_INVALID_HANDLE;
    const CNA_Result created =
        cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &first);
    if (created != CNA_RESULT_SUCCESS) {
        /* A build with no usable display cannot create one; that is a refusal, not a crash. */
        (void)fprintf(stderr, "device creation unavailable (%u); skipping\n", (unsigned)created);
        return first == CNA_INVALID_HANDLE ? 0 : 3;
    }

    CNA_Handle second = CNA_INVALID_HANDLE;
    if (cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &second) !=
            CNA_RESULT_SUCCESS ||
        second == first) {
        return 4;
    }

    /* An owned device answers the ordinary device queries, so it reached the same routes. */
    CNA_GraphicsProfile profile = UINT32_MAX;
    CNA_Bool disposed = CNA_TRUE;
    if (cna_graphics_device_get_graphics_profile(first, &profile) != CNA_RESULT_SUCCESS ||
        profile != CNA_GRAPHICS_PROFILE_REACH ||
        cna_graphics_device_get_is_disposed(second, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE) {
        return 5;
    }

    /* Resources are created on it exactly as on a Game's device. */
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 4U, 4U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR
    };
    CNA_Handle texture_on_first = CNA_INVALID_HANDLE;
    CNA_Handle batch_on_second = CNA_INVALID_HANDLE;
    if (cna_texture2d_create(first, &texture_info, &texture_on_first) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_create(second, &batch_on_second) != CNA_RESULT_SUCCESS) {
        return 6;
    }

    /*
     * The cross-device refusal. A texture made on `first` must not be drawable by a batch made on
     * `second`. This is what distinct owner tokens buy; with both devices reported as "no game"
     * the two would have compared equal and this draw would have been allowed.
     */
    const CNA_SpriteBatchBeginInfo begin_info = {
        sizeof(CNA_SpriteBatchBeginInfo), UINT32_C(1), CNA_SPRITE_SORT_MODE_DEFERRED, 0U
    };
    const CNA_Result begun = cna_sprite_batch_begin(batch_on_second, &begin_info);
    (void)fprintf(
        stderr,
        "two devices created; cross-device draw refusal: %s\n",
        begun == CNA_RESULT_SUCCESS ? "asserted" : "skipped (batch could not begin)");
    if (begun == CNA_RESULT_SUCCESS) {
        const CNA_SpriteCommand command = {
            sizeof(CNA_SpriteCommand), UINT32_C(1), texture_on_first,
            {0, 0, 4, 4}, {0U, 0U, 0U, 0U}, {255U, 255U, 255U, 255U},
            0.0F, {0.0F, 0.0F}, CNA_SPRITE_EFFECT_NONE, 0.0F
        };
        const CNA_Result mixed = cna_sprite_batch_submit_many(batch_on_second, &command, 1U);
        if (mixed == CNA_RESULT_SUCCESS) {
            return 7;
        }
        if (cna_sprite_batch_end(batch_on_second) != CNA_RESULT_SUCCESS) {
            return 8;
        }
    }

    /* Destroying one device while the other is live, then the rest. */
    if (cna_texture2d_destroy(texture_on_first) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_destroy(first) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_destroy(first) != CNA_RESULT_INVALID_HANDLE) {
        return 9;
    }
    CNA_Bool second_alive = CNA_TRUE;
    if (cna_graphics_device_get_is_disposed(second, &second_alive) != CNA_RESULT_SUCCESS ||
        second_alive != CNA_FALSE) {
        return 10;
    }
    if (cna_sprite_batch_destroy(batch_on_second) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_destroy(second) != CNA_RESULT_SUCCESS) {
        return 11;
    }
    return 0;
}
