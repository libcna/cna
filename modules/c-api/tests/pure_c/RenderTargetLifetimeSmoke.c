/* SPDX-License-Identifier: MS-PL */

/*
 * Destroying a render target that its GraphicsDevice still has bound must fail as ordinary
 * invalid public API state, never as a process abort. RenderTargetBinding holds a non-owning
 * Texture*, so a destroy that went through would leave the device's binding list dangling and
 * the next device call would dereference freed memory.
 *
 * CNA already refuses it: RenderTarget2D::Dispose and RenderTargetCube::Dispose each scan
 * GetRenderTargets() for themselves and throw InvalidOperationException, which the C exception
 * barrier maps to the CNA_RESULT_INVALID_STATE that render_target.h documents. Nothing pinned
 * that end to end from C, which is what this test is for -- the refusal is a published ABI
 * contract, not an implementation detail of two Dispose overrides.
 *
 * GraphicsSurfaceSmoke.c asserts the 2D half of it, but only inside its
 * `info.renderer_available == CNA_TRUE` branch, so it goes silent exactly where no renderer can
 * bind anything -- and a silent skip reads identically to a pass. Every case here reports which
 * arm it took, and the cube route, which has its own destroy entry point, is covered too.
 */

#include <CNA/C/cna.h>

#include <stddef.h>
#include <stdio.h>

typedef struct RenderTargetLifetimeState {
    CNA_Handle render_target_2d;
    CNA_Handle render_target_cube;
    CNA_Handle second_render_target;
    int validated;
} RenderTargetLifetimeState;

static CNA_RenderTargetBinding make_binding(const CNA_Handle target)
{
    const CNA_RenderTargetBinding binding = {
        sizeof(CNA_RenderTargetBinding), UINT32_C(1), target,
        0, CNA_CUBE_MAP_FACE_POSITIVE_X
    };
    return binding;
}

static CNA_Result unbind(const CNA_Handle graphics_device)
{
    return cna_graphics_device_set_render_target2d(graphics_device, CNA_INVALID_HANDLE);
}

/* A bound target refuses destruction, stays usable, and destroys once unbound. */
static CNA_Result validate_bound_destroy_refused(
    const CNA_Handle graphics_device,
    RenderTargetLifetimeState* const state)
{
    const CNA_RenderTargetBinding binding = make_binding(state->render_target_2d);
    const CNA_Result bound =
        cna_graphics_device_set_render_targets(graphics_device, &binding, 1U);
    if (bound != CNA_RESULT_SUCCESS && bound != CNA_RESULT_NOT_SUPPORTED) {
        return CNA_RESULT_INVALID_STATE;
    }
    (void)fprintf(
        stderr,
        "render-target 2D binding: %s\n",
        bound == CNA_RESULT_SUCCESS ? "bound (refusal asserted)" : "not supported (refusal skipped)");
    if (bound == CNA_RESULT_SUCCESS) {
        /* The refusal, and the retry that proves the failed destroy changed nothing. */
        if (cna_render_target_destroy(state->render_target_2d) != CNA_RESULT_INVALID_STATE ||
            cna_render_target_destroy(state->render_target_2d) != CNA_RESULT_INVALID_STATE) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* The handle survived the refusal: the device still reports it bound, and the target
           still answers for itself. */
        uint64_t binding_count = 0U;
        CNA_RenderTargetInfo info = {0};
        info.struct_size = sizeof(CNA_RenderTargetInfo);
        info.struct_version = UINT32_C(1);
        if (cna_graphics_device_get_render_target_count(
                graphics_device, &binding_count) != CNA_RESULT_SUCCESS ||
            binding_count != 1U ||
            cna_render_target_get_info(state->render_target_2d, &info) != CNA_RESULT_SUCCESS ||
            info.width != 4U) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* Replacing the bound target releases the first one and binds the second. */
        const CNA_RenderTargetBinding replacement = make_binding(state->second_render_target);
        if (cna_graphics_device_set_render_targets(
                graphics_device, &replacement, 1U) != CNA_RESULT_SUCCESS ||
            cna_render_target_destroy(state->second_render_target) != CNA_RESULT_INVALID_STATE) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* The target that was replaced is no longer bound, so it destroys. */
        if (cna_render_target_destroy(state->render_target_2d) != CNA_RESULT_SUCCESS ||
            cna_render_target_destroy(state->render_target_2d) != CNA_RESULT_INVALID_HANDLE) {
            return CNA_RESULT_INVALID_STATE;
        }
        state->render_target_2d = CNA_INVALID_HANDLE;
        if (unbind(graphics_device) != CNA_RESULT_SUCCESS ||
            cna_render_target_destroy(state->second_render_target) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
        state->second_render_target = CNA_INVALID_HANDLE;
    } else {
        /* Nothing bound, so nothing to refuse: both targets destroy directly. This arm keeps the
           test meaningful on a build whose renderer cannot bind, instead of going silent. */
        if (cna_render_target_destroy(state->render_target_2d) != CNA_RESULT_SUCCESS ||
            cna_render_target_destroy(state->second_render_target) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
        state->render_target_2d = CNA_INVALID_HANDLE;
        state->second_render_target = CNA_INVALID_HANDLE;
    }
    return CNA_RESULT_SUCCESS;
}

/* The cube path carries its own guard, on its own destroy route. */
static CNA_Result validate_bound_cube_destroy_refused(
    const CNA_Handle graphics_device,
    RenderTargetLifetimeState* const state)
{
    const CNA_Result bound = cna_graphics_device_set_render_target_cube(
        graphics_device, state->render_target_cube, CNA_CUBE_MAP_FACE_NEGATIVE_Y);
    if (bound != CNA_RESULT_SUCCESS && bound != CNA_RESULT_NOT_SUPPORTED) {
        return CNA_RESULT_INVALID_STATE;
    }
    (void)fprintf(
        stderr,
        "render-target cube binding: %s\n",
        bound == CNA_RESULT_SUCCESS ? "bound (refusal asserted)" : "not supported (refusal skipped)");
    if (bound == CNA_RESULT_SUCCESS) {
        if (cna_render_target_destroy(state->render_target_cube) != CNA_RESULT_INVALID_STATE ||
            unbind(graphics_device) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
    }
    if (cna_render_target_destroy(state->render_target_cube) != CNA_RESULT_SUCCESS ||
        cna_render_target_destroy(state->render_target_cube) != CNA_RESULT_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->render_target_cube = CNA_INVALID_HANDLE;
    return CNA_RESULT_SUCCESS;
}

/* Counts ContentLost notifications for one render target. */
static void on_content_lost(const CNA_Handle render_target, void* const context)
{
    (void)render_target;
    *(int*)context += 1;
}

/*
 * The ContentLost subscription (CABI-24). The event itself is only reachable on the three renderer
 * families that can report a device reset -- DIRECTX9, DIRECT2D and SKIA -- so what is asserted
 * here is the subscription contract: it registers, it refuses malformed arguments, it releases
 * once, and on a renderer that cannot lose a device it stays silent rather than inventing a
 * notification.
 */
static CNA_Result validate_content_lost_subscription(RenderTargetLifetimeState* const state)
{
    int notifications = 0;
    CNA_RenderTargetEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    CNA_RenderTargetEventRegistrationHandle rejected = UINT64_MAX;

    if (cna_render_target_subscribe_content_lost(
            state->render_target_cube, on_content_lost, &notifications, &registration) !=
            CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* A null callback and a null output are refused, and neither writes a handle. */
    if (cna_render_target_subscribe_content_lost(
            state->render_target_cube, 0, &notifications, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_render_target_subscribe_content_lost(
            state->render_target_cube, on_content_lost, &notifications, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* A handle of the wrong family is not a render target. */
    if (cna_render_target_subscribe_content_lost(
            CNA_INVALID_HANDLE, on_content_lost, &notifications, &rejected) ==
        CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* Nothing has lost a device, so nothing was notified. */
    if (notifications != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    /* The registration is owned: released once, and refused the second time. */
    if (cna_render_target_unsubscribe_content_lost(registration) != CNA_RESULT_SUCCESS ||
        cna_render_target_unsubscribe_content_lost(registration) != CNA_RESULT_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    (void)out_error;
    RenderTargetLifetimeState* const state = (RenderTargetLifetimeState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_RenderTarget2DCreateInfo create_2d = {
        sizeof(CNA_RenderTarget2DCreateInfo), UINT32_C(1), 4U, 4U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0U
    };
    const CNA_RenderTargetCubeCreateInfo create_cube = {
        sizeof(CNA_RenderTargetCubeCreateInfo), UINT32_C(1), 4U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS
    };
    if (cna_render_target2d_create(
            graphics_device, &create_2d, &state->render_target_2d) != CNA_RESULT_SUCCESS ||
        cna_render_target2d_create(
            graphics_device, &create_2d, &state->second_render_target) != CNA_RESULT_SUCCESS ||
        cna_render_target_cube_create(
            graphics_device, &create_cube, &state->render_target_cube) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (validate_bound_destroy_refused(graphics_device, state) != CNA_RESULT_SUCCESS ||
        validate_content_lost_subscription(state) != CNA_RESULT_SUCCESS ||
        validate_bound_cube_destroy_refused(graphics_device, state) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    RenderTargetLifetimeState state = {
        CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, 0
    };
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    static const char title[] = "C API render target lifetime";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        state.validated != 1) {
        return 1;
    }
    /* Every target was destroyed inside the frame, so the game owes nothing and shuts down. */
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 2;
    }
    return 0;
}
