// SPDX-License-Identifier: MS-PL

#include "CnaLifetimeSequence.h"

#define RESOURCE_COUNT 14U

static uint64_t disposing_callbacks;
static uint64_t reentrant_releases;

static int stale_device_diagnostic(void)
{
    CNA_Handle stale = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    const CNA_RenderTarget2DCreateInfo info = {sizeof(CNA_RenderTarget2DCreateInfo), 1U,
        4U, 4U, CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, CNA_DEPTH_FORMAT_NONE,
        0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0U};
    static const char expected[] = "The graphics-device handle is invalid for this call.";
    char message[sizeof(expected)];
    uint64_t bytes = 0U;
    begin_sequence("resource-lifetime", 0U);
    EXPECT_RESULT(cna_vertex_declaration_create_empty(&stale), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_vertex_declaration_destroy(stale), CNA_RESULT_SUCCESS);
    /* The device lookup fails before creating any native window/renderer. */
    EXPECT_RESULT(cna_render_target2d_create(stale, &info, &rejected), CNA_RESULT_INVALID_HANDLE);
    REQUIRE(rejected == CNA_INVALID_HANDLE);
    EXPECT_RESULT(cna_error_copy_last_message(message, sizeof(message), &bytes), CNA_RESULT_SUCCESS);
    REQUIRE(bytes == sizeof(expected) - 1U);
    for (uint64_t i = 0U; i < bytes; ++i) REQUIRE(message[i] == expected[i]);
    printf("stale-device diagnostic: %.*s\n", (int)bytes, message);
    return 1;
}

typedef struct ResourceSlot {
    CNA_Handle handle;
    CNA_Handle stale;
    CNA_Handle registration;
    CNA_Bool disposed;
    unsigned calls;
    unsigned observed_calls;
    CNA_Result callback_query;
    CNA_Result callback_release;
    CNA_Bool destroy_in_callback;
} ResourceSlot;

static void on_disposing(CNA_Handle resource, void* context)
{
    ResourceSlot* slot = (ResourceSlot*)context;
    CNA_Bool disposed = CNA_FALSE;
    ++disposing_callbacks;
    ++slot->calls;
    slot->callback_query = cna_graphics_resource_get_is_disposed(resource, &disposed);
    if (disposed != CNA_TRUE) slot->callback_query = CNA_RESULT_INTERNAL;
    /* Unlike game driving, an owned standalone resource has no callback-scope teardown ban.
       The outer dispose call must keep its native object alive until the callback returns. */
    if (slot->destroy_in_callback == CNA_TRUE) {
        ++reentrant_releases;
        slot->callback_release = cna_vertex_declaration_destroy(resource);
    }
}

static int create_resource(ResourceSlot* slot)
{
    EXPECT_RESULT(cna_vertex_declaration_create_empty(&slot->handle), CNA_RESULT_SUCCESS);
    REQUIRE(slot->handle != CNA_INVALID_HANDLE && slot->handle != slot->stale);
    slot->disposed = CNA_FALSE;
    slot->calls = 0U;
    slot->observed_calls = 0U;
    slot->callback_query = CNA_RESULT_SUCCESS;
    slot->callback_release = CNA_RESULT_SUCCESS;
    slot->destroy_in_callback = CNA_FALSE;
    EXPECT_RESULT(cna_graphics_resource_subscribe_disposing(slot->handle, on_disposing,
        slot, &slot->registration), CNA_RESULT_SUCCESS);
    return 1;
}

static int check_resource(ResourceSlot* slot)
{
    CNA_Bool disposed = CNA_FALSE;
    REQUIRE(slot->calls == slot->observed_calls);
    REQUIRE(slot->callback_query == CNA_RESULT_SUCCESS && slot->callback_release == CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_graphics_resource_get_is_disposed(slot->handle, &disposed), CNA_RESULT_SUCCESS);
    REQUIRE(disposed == slot->disposed);
    return 1;
}

static int run_sequence(unsigned index)
{
    /* Keep callback contexts valid even if a failed assertion leaves handles for exit cleanup. */
    static ResourceSlot slots[RESOURCE_COUNT];
    CNA_Handle sentinel = CNA_INVALID_HANDLE;
    begin_sequence("resource-lifetime", index);
    for (unsigned i = 0U; i < RESOURCE_COUNT; ++i) slots[i] = (ResourceSlot){0};
    EXPECT_RESULT(cna_curve_create(&sentinel), CNA_RESULT_SUCCESS);
    for (unsigned i = 0U; i < RESOURCE_COUNT; ++i) REQUIRE(create_resource(&slots[i]));
    for (sequence_step = 0U; sequence_step < OPERATION_COUNT; ++sequence_step) {
        const unsigned which = next_random() % RESOURCE_COUNT;
        const unsigned operation = next_random() % 7U;
        ResourceSlot* slot = &slots[which];
        sequence_trace[sequence_step] = operation * 32U + which;
        switch (operation) {
        case 0: case 1:
            if (slot->disposed == CNA_FALSE && slot->registration != CNA_INVALID_HANDLE)
                ++slot->observed_calls;
            EXPECT_RESULT(cna_graphics_resource_dispose(slot->handle), CNA_RESULT_SUCCESS);
            slot->disposed = CNA_TRUE;
            break;
        case 2:
            if (slot->registration != CNA_INVALID_HANDLE) {
                EXPECT_RESULT(cna_graphics_resource_unsubscribe_disposing(slot->registration), CNA_RESULT_SUCCESS);
                EXPECT_RESULT(cna_graphics_resource_unsubscribe_disposing(slot->registration), CNA_RESULT_INVALID_HANDLE);
                slot->registration = CNA_INVALID_HANDLE;
            }
            break;
        case 3: case 4:
            /* A detached subscription may outlive its resource, but still needs explicit release. */
            if (slot->disposed == CNA_FALSE && slot->registration != CNA_INVALID_HANDLE)
                ++slot->observed_calls;
            EXPECT_RESULT(cna_vertex_declaration_destroy(slot->handle), CNA_RESULT_SUCCESS);
            REQUIRE(slot->calls == slot->observed_calls && slot->callback_query == CNA_RESULT_SUCCESS);
            slot->stale = slot->handle;
            if (slot->registration != CNA_INVALID_HANDLE)
                EXPECT_RESULT(cna_graphics_resource_unsubscribe_disposing(slot->registration), CNA_RESULT_SUCCESS);
            EXPECT_RESULT(cna_vertex_declaration_destroy(slot->stale), CNA_RESULT_INVALID_HANDLE);
            REQUIRE(create_resource(slot));
            break;
        case 5:
            if (slot->disposed == CNA_FALSE && slot->registration != CNA_INVALID_HANDLE) {
                slot->destroy_in_callback = CNA_TRUE;
                ++slot->observed_calls;
                EXPECT_RESULT(cna_graphics_resource_dispose(slot->handle), CNA_RESULT_SUCCESS);
                REQUIRE(slot->calls == slot->observed_calls && slot->callback_query == CNA_RESULT_SUCCESS &&
                    slot->callback_release == CNA_RESULT_SUCCESS);
                slot->stale = slot->handle;
                EXPECT_RESULT(cna_vertex_declaration_destroy(slot->stale), CNA_RESULT_INVALID_HANDLE);
                EXPECT_RESULT(cna_graphics_resource_unsubscribe_disposing(slot->registration), CNA_RESULT_SUCCESS);
                REQUIRE(create_resource(slot));
            }
            break;
        default: {
            CNA_Handle rejected = sentinel;
            EXPECT_RESULT(cna_graphics_resource_subscribe_disposing(slot->handle, NULL, slot, &rejected),
                CNA_RESULT_INVALID_ARGUMENT);
            REQUIRE(rejected == CNA_INVALID_HANDLE);
            break;
        }
        }
        REQUIRE(check_resource(slot));
        if (slot->stale != CNA_INVALID_HANDLE) {
            CNA_Bool output = CNA_TRUE;
            EXPECT_RESULT(cna_graphics_resource_get_is_disposed(slot->stale, &output), CNA_RESULT_INVALID_HANDLE);
            REQUIRE(output == CNA_TRUE);
        }
        {
            CNA_Bool constant = CNA_FALSE;
            EXPECT_RESULT(cna_curve_get_is_constant(sentinel, &constant), CNA_RESULT_SUCCESS);
            REQUIRE(constant == CNA_TRUE);
        }
    }
    for (unsigned i = 0U; i < RESOURCE_COUNT; ++i) {
        ResourceSlot* slot = &slots[i];
        if (slot->disposed == CNA_FALSE && slot->registration != CNA_INVALID_HANDLE)
            ++slot->observed_calls;
        EXPECT_RESULT(cna_vertex_declaration_destroy(slot->handle), CNA_RESULT_SUCCESS);
        REQUIRE(slot->calls == slot->observed_calls && slot->callback_query == CNA_RESULT_SUCCESS);
        if (slot->registration != CNA_INVALID_HANDLE)
            EXPECT_RESULT(cna_graphics_resource_unsubscribe_disposing(slot->registration), CNA_RESULT_SUCCESS);
    }
    EXPECT_RESULT(cna_curve_destroy(sentinel), CNA_RESULT_SUCCESS);
    return 1;
}

int main(int argc, char** argv)
{
    unsigned first, end;
    if (!sequence_range(argc, argv, &first, &end)) return 2;
    if (!stale_device_diagnostic()) return 1;
    for (unsigned i = first; i < end; ++i)
        if (!run_sequence(i)) return 1;
    sequence_summary(end - first);
    printf("disposal callbacks=%" PRIu64 ", owned-handle releases inside callback=%" PRIu64 "\n",
        disposing_callbacks, reentrant_releases);
    if (disposing_callbacks == 0U || reentrant_releases == 0U) return 1;
    return 0;
}
