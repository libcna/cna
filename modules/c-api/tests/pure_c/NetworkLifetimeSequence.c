// SPDX-License-Identifier: MS-PL

#include "CnaLifetimeSequence.h"
#include <string.h>

/* Owned roster views and nonempty machine copies block session disposal. Signed-in aliases
   retain the native signed-in identity independently of both its original token and the session. */
enum SlotKind { ROSTER, STANDALONE, MACHINE, SIGNED_IN };
typedef struct Slot {
    CNA_Handle handle;
    CNA_Handle stale;
    enum SlotKind kind;
    enum SlotKind stale_kind;
} Slot;

static CNA_StringView text(const char* value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static int acquire(Slot* slot, CNA_Handle session, unsigned kind)
{
    slot->kind = (enum SlotKind)kind;
    if (kind == ROSTER || kind == SIGNED_IN) {
        CNA_Handle gamer = CNA_INVALID_HANDLE;
        EXPECT_RESULT(cna_network_session_get_gamer(session,
            CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &gamer), CNA_RESULT_SUCCESS);
        if (kind == ROSTER) {
            slot->handle = gamer;
        } else {
            EXPECT_RESULT(cna_local_network_gamer_get_signed_in_gamer(gamer, &slot->handle),
                CNA_RESULT_SUCCESS);
            EXPECT_RESULT(cna_network_gamer_destroy(gamer), CNA_RESULT_SUCCESS);
        }
    } else if (kind == STANDALONE) {
        EXPECT_RESULT(cna_network_gamer_create(session, text("sequence remote"), &slot->handle),
            CNA_RESULT_SUCCESS);
    } else {
        CNA_Handle gamer = CNA_INVALID_HANDLE;
        EXPECT_RESULT(cna_network_session_get_gamer(session,
            CNA_NETWORK_SESSION_ROSTER_LOCAL, 0, &gamer), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_network_gamer_copy_machine(gamer, &slot->handle), CNA_RESULT_SUCCESS);
        EXPECT_RESULT(cna_network_gamer_destroy(gamer), CNA_RESULT_SUCCESS);
    }
    REQUIRE(slot->handle != CNA_INVALID_HANDLE && slot->handle != slot->stale);
    return 1;
}

static CNA_Result release_slot(const Slot* slot, CNA_Handle handle)
{
    if (slot->kind == SIGNED_IN) return cna_signed_in_gamer_destroy(handle);
    if (slot->kind == MACHINE) return cna_network_machine_destroy(handle);
    return cna_network_gamer_destroy(handle);
}

static int check_slot(const Slot* slot, CNA_Handle session, int32_t party_size)
{
    if (slot->kind == SIGNED_IN) {
        int32_t observed = -1;
        EXPECT_RESULT(cna_signed_in_gamer_get_party_size(slot->handle, &observed), CNA_RESULT_SUCCESS);
        REQUIRE(observed == party_size);
    } else if (slot->kind == MACHINE) {
        int32_t count = -1;
        EXPECT_RESULT(cna_network_machine_get_gamer_count(slot->handle, &count), CNA_RESULT_SUCCESS);
        REQUIRE(count == 1);
    } else {
        CNA_Handle parent = CNA_INVALID_HANDLE;
        EXPECT_RESULT(cna_network_gamer_get_session(slot->handle, &parent), CNA_RESULT_SUCCESS);
        REQUIRE(parent == session);
    }
    return 1;
}

static int check_stale(const Slot* slot)
{
    if (slot->stale == CNA_INVALID_HANDLE) return 1;
    if (slot->stale_kind == SIGNED_IN) {
        int32_t value = -1;
        EXPECT_RESULT(cna_signed_in_gamer_get_party_size(slot->stale, &value), CNA_RESULT_INVALID_HANDLE);
        REQUIRE(value == -1);
    } else if (slot->stale_kind == MACHINE) {
        int32_t count = -1;
        EXPECT_RESULT(cna_network_machine_get_gamer_count(slot->stale, &count), CNA_RESULT_INVALID_HANDLE);
        REQUIRE(count == -1);
    } else {
        CNA_Handle parent = slot->handle;
        EXPECT_RESULT(cna_network_gamer_get_session(slot->stale, &parent), CNA_RESULT_INVALID_HANDLE);
        REQUIRE(parent == CNA_INVALID_HANDLE);
    }
    return 1;
}

static int run_sequence(unsigned index)
{
    Slot slots[SLOT_COUNT] = {{0}};
    CNA_Handle source = CNA_INVALID_HANDLE;
    CNA_Handle session = CNA_INVALID_HANDLE;
    CNA_Handle sentinel = CNA_INVALID_HANDLE;
    CNA_Bool disposed = CNA_TRUE;
    int32_t party_size = 0;
    int32_t instances = -1;
    begin_sequence("network-lifetime", index);
    EXPECT_RESULT(cna_signed_in_gamer_create_ext(text("unrelated sentinel"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_TWO, &sentinel), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_signed_in_gamer_set_party_size(sentinel, 99), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_signed_in_gamer_create_ext(text("retained source"), CNA_FALSE, CNA_FALSE,
        CNA_PLAYER_INDEX_ONE, &source), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_signed_in_gamer_get_party_size(source, &party_size), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_network_session_create_with_local_gamers(CNA_NETWORK_SESSION_TYPE_LOCAL,
        &source, 1U, 4, 0, CNA_INVALID_HANDLE, &session), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_signed_in_gamer_destroy(source), CNA_RESULT_SUCCESS);
    for (unsigned i = 0U; i < SLOT_COUNT; ++i)
        REQUIRE(acquire(&slots[i], session, i % 4U));

    for (sequence_step = 0U; sequence_step < OPERATION_COUNT; ++sequence_step) {
        const unsigned which = next_random() % SLOT_COUNT;
        const unsigned operation = next_random() % 8U;
        Slot* slot = &slots[which];
        sequence_trace[sequence_step] = operation * 32U + which;
        switch (operation) {
        case 0: case 1:
            if (slot->handle != CNA_INVALID_HANDLE) {
                EXPECT_RESULT(release_slot(slot, slot->handle), CNA_RESULT_SUCCESS);
                slot->stale = slot->handle;
                slot->stale_kind = slot->kind;
                slot->handle = CNA_INVALID_HANDLE;
                EXPECT_RESULT(release_slot(slot, slot->stale), CNA_RESULT_INVALID_HANDLE);
            } else {
                REQUIRE(acquire(slot, session, next_random() % 4U));
            }
            break;
        case 2: case 3: {
            unsigned blockers = 0U;
            for (unsigned i = 0U; i < SLOT_COUNT; ++i)
                if (slots[i].handle != CNA_INVALID_HANDLE && slots[i].kind != SIGNED_IN) ++blockers;
            /* Keep one roster view alive when random release drained the graph. */
            if (blockers == 0U) {
                if (slot->handle != CNA_INVALID_HANDLE) {
                    EXPECT_RESULT(release_slot(slot, slot->handle), CNA_RESULT_SUCCESS);
                    slot->stale = slot->handle;
                    slot->stale_kind = slot->kind;
                }
                REQUIRE(acquire(slot, session, ROSTER));
            }
            if (operation == 2U)
                EXPECT_RESULT(cna_network_session_dispose(session), CNA_RESULT_INVALID_STATE);
            else
                EXPECT_RESULT(cna_network_session_destroy(session), CNA_RESULT_INVALID_STATE);
            EXPECT_RESULT(cna_network_session_get_is_disposed(session, &disposed), CNA_RESULT_SUCCESS);
            REQUIRE(disposed == CNA_FALSE);
            break;
        }
        case 4:
            if (slot->handle != CNA_INVALID_HANDLE)
                REQUIRE(check_slot(slot, session, party_size));
            break;
        case 5:
            if (slot->handle != CNA_INVALID_HANDLE && slot->kind == SIGNED_IN) {
                party_size = (int32_t)(next_random() % 4U);
                EXPECT_RESULT(cna_signed_in_gamer_set_party_size(slot->handle, party_size), CNA_RESULT_SUCCESS);
                for (unsigned i = 0U; i < SLOT_COUNT; ++i)
                    if (slots[i].handle != CNA_INVALID_HANDLE && slots[i].kind == SIGNED_IN)
                        REQUIRE(check_slot(&slots[i], session, party_size));
            }
            break;
        case 6: {
            CNA_Handle rejected = sentinel;
            EXPECT_RESULT(cna_local_network_gamer_get_signed_in_gamer(sentinel, &rejected),
                CNA_RESULT_INVALID_HANDLE);
            REQUIRE(rejected == CNA_INVALID_HANDLE);
            break;
        }
        default: {
            CNA_Handle rejected = sentinel;
            EXPECT_RESULT(cna_signed_in_gamer_create_ext(text("failed creation"), (CNA_Bool)2U,
                CNA_FALSE, CNA_PLAYER_INDEX_ONE, &rejected), CNA_RESULT_INVALID_ARGUMENT);
            REQUIRE(rejected == CNA_INVALID_HANDLE);
            REQUIRE(check_stale(slot));
            break;
        }
        }
        REQUIRE(check_stale(slot));
        {
            int32_t value = -1;
            EXPECT_RESULT(cna_signed_in_gamer_get_party_size(sentinel, &value), CNA_RESULT_SUCCESS);
            REQUIRE(value == 99);
        }
    }
    /* Retained signed-in aliases survive successful native parent disposal and token release. */
    for (unsigned i = 0U; i < SLOT_COUNT; ++i) {
        if (slots[i].handle != CNA_INVALID_HANDLE && slots[i].kind != SIGNED_IN) {
            EXPECT_RESULT(release_slot(&slots[i], slots[i].handle), CNA_RESULT_SUCCESS);
            slots[i].handle = CNA_INVALID_HANDLE;
        }
    }
    EXPECT_RESULT(cna_network_session_dispose(session), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_network_session_dispose(session), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_network_session_get_is_disposed(session, &disposed), CNA_RESULT_SUCCESS);
    REQUIRE(disposed == CNA_TRUE);
    EXPECT_RESULT(cna_network_session_destroy(session), CNA_RESULT_SUCCESS);
    EXPECT_RESULT(cna_network_session_destroy(session), CNA_RESULT_INVALID_HANDLE);
    for (unsigned i = 0U; i < SLOT_COUNT; ++i) {
        if (slots[i].handle != CNA_INVALID_HANDLE) {
            REQUIRE(check_slot(&slots[i], session, party_size));
            EXPECT_RESULT(release_slot(&slots[i], slots[i].handle), CNA_RESULT_SUCCESS);
        }
    }
    EXPECT_RESULT(cna_network_session_get_instance_count_ext(&instances), CNA_RESULT_SUCCESS);
    REQUIRE(instances == 0);
    EXPECT_RESULT(cna_signed_in_gamer_destroy(sentinel), CNA_RESULT_SUCCESS);
    return 1;
}

int main(int argc, char** argv)
{
    unsigned first, end;
    if (!sequence_range(argc, argv, &first, &end)) return 2;
    for (unsigned i = first; i < end; ++i)
        if (!run_sequence(i)) return 1;
    sequence_summary(end - first);
    return 0;
}
