// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "CnaTestThreads.h"

_Static_assert(sizeof(CNA_VertexDeclarationHandle) == 8U,
               "CNA_VertexDeclarationHandle size changed");
_Static_assert(sizeof(CNA_VertexBufferHandle) == 8U,
               "CNA_VertexBufferHandle size changed");
_Static_assert(sizeof(CNA_VertexBufferBinding) == 16U &&
                   _Alignof(CNA_VertexBufferBinding) == 8U,
               "CNA_VertexBufferBinding layout changed");
_Static_assert(offsetof(CNA_VertexBufferBinding, vertex_buffer) == 0U &&
                   offsetof(CNA_VertexBufferBinding, vertex_offset) == 8U &&
                   offsetof(CNA_VertexBufferBinding, instance_frequency) == 12U,
               "CNA_VertexBufferBinding field offsets changed");

static const CNA_VertexElement Elements[] = {
    {0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0},
    {12, CNA_VERTEX_ELEMENT_FORMAT_COLOR, CNA_VERTEX_ELEMENT_USAGE_COLOR, 0},
    {16, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE, 0}
};

static int element_equals(const CNA_VertexElement left, const CNA_VertexElement right)
{
    return left.offset == right.offset && left.format == right.format &&
        left.usage == right.usage && left.usage_index == right.usage_index;
}

static int validate_declaration(
    const CNA_VertexDeclarationHandle declaration,
    const int32_t expected_stride,
    const uint64_t expected_count)
{
    int32_t stride = -1;
    uint64_t count = UINT64_MAX;
    CNA_VertexElement copied[3] = {
        {-1, UINT32_MAX, UINT32_MAX, -1},
        {-1, UINT32_MAX, UINT32_MAX, -1},
        {-1, UINT32_MAX, UINT32_MAX, -1}
    };
    if (cna_vertex_declaration_get_stride(declaration, &stride) != CNA_RESULT_SUCCESS ||
        stride != expected_stride ||
        cna_vertex_declaration_copy_elements(declaration, 0, 0U, &count) !=
            (expected_count == 0U ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL) ||
        count != expected_count ||
        cna_vertex_declaration_copy_elements(
            declaration, copied, sizeof(copied) / sizeof(copied[0]), &count) !=
            CNA_RESULT_SUCCESS || count != expected_count) {
        return 0;
    }
    for (uint64_t index = 0U; index < expected_count; ++index) {
        if (!element_equals(copied[index], Elements[index])) {
            return 0;
        }
    }
    return 1;
}

static int validate_type_name(const CNA_VertexDeclarationHandle declaration)
{
    static const char Expected[] =
        "Microsoft.Xna.Framework.Graphics.VertexDeclaration";
    uint64_t count = UINT64_MAX;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 't';
    if (cna_vertex_declaration_get_type_name_byte_count(declaration, &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(Expected) - 1U ||
        cna_vertex_declaration_copy_type_name(declaration, &too_small, 1U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 't' ||
        count != sizeof(Expected) - 1U ||
        cna_vertex_declaration_copy_type_name(
            declaration, bytes, sizeof(bytes), &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_creation_and_copy(void)
{
    CNA_VertexDeclarationHandle empty = CNA_INVALID_HANDLE;
    CNA_VertexDeclarationHandle automatic = CNA_INVALID_HANDLE;
    CNA_VertexDeclarationHandle explicit_stride = CNA_INVALID_HANDLE;
    if (cna_vertex_declaration_create_empty(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_declaration_create_empty(&empty) != CNA_RESULT_SUCCESS ||
        empty == CNA_INVALID_HANDLE || !validate_declaration(empty, 0, 0U) ||
        !validate_type_name(empty) ||
        cna_vertex_declaration_create(
            Elements, sizeof(Elements) / sizeof(Elements[0]), &automatic) !=
            CNA_RESULT_SUCCESS || automatic == CNA_INVALID_HANDLE ||
        !validate_declaration(automatic, 24, 3U) ||
        cna_vertex_declaration_create_with_stride(
            32, Elements, sizeof(Elements) / sizeof(Elements[0]), &explicit_stride) !=
            CNA_RESULT_SUCCESS || explicit_stride == CNA_INVALID_HANDLE ||
        !validate_declaration(explicit_stride, 32, 3U)) {
        return 0;
    }

    /* vertex_resources.h copy contract; Refresh .ctor IL_0011-001f/GetVertexElements
       IL_0000-0010 independently clone input and output element arrays. */
    CNA_VertexElement mutable_input[3];
    CNA_VertexElement snapshot[4];
    CNA_VertexDeclarationHandle isolated = CNA_INVALID_HANDLE;
    memcpy(mutable_input, Elements, sizeof(mutable_input));
    memset(snapshot, 0xa5, sizeof(snapshot));
    uint64_t snapshot_count = 77U;
    if (cna_vertex_declaration_create(mutable_input, 3U, &isolated) != CNA_RESULT_SUCCESS) return 0;
    mutable_input[0].offset = 88;
    if (cna_vertex_declaration_copy_elements(isolated, snapshot, 4U, &snapshot_count) != CNA_RESULT_SUCCESS ||
        snapshot_count != 3U) return 0;
    for (size_t i = 0; i < 3U; ++i) if (!element_equals(snapshot[i], Elements[i])) return 0;
    const unsigned char* unused = (const unsigned char*)&snapshot[3];
    for (size_t i = 0; i < sizeof(snapshot[3]); ++i) if (unused[i] != 0xa5) return 0;
    snapshot[0].offset = 44;
    if (!validate_declaration(isolated, 24, 3U) ||
        cna_vertex_declaration_destroy(isolated) != CNA_RESULT_SUCCESS) return 0;

    CNA_VertexElement destination = {-7, UINT32_MAX, UINT32_MAX, -7};
    const CNA_VertexElement sentinel = destination;
    uint64_t count = UINT64_MAX;
    if (cna_vertex_declaration_copy_elements(automatic, &destination, 1U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || count != 3U ||
        !element_equals(destination, sentinel) ||
        cna_vertex_declaration_destroy(empty) != CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_destroy(automatic) != CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_destroy(explicit_stride) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_binding(void)
{
    const CNA_VertexBufferBinding default_binding = {
        CNA_INVALID_HANDLE, 0, 0};
    CNA_VertexBufferBinding binding = {
        UINT64_C(0xaaaaaaaaaaaaaaaa), -7, -8};
    const CNA_VertexBufferBinding sentinel = binding;
    const CNA_VertexBufferHandle token = UINT64_C(0x0000000100000001);
    if (default_binding.vertex_buffer != CNA_INVALID_HANDLE ||
        default_binding.vertex_offset != 0 || default_binding.instance_frequency != 0 ||
        cna_vertex_buffer_binding_init(token, 2, 3, &binding) != CNA_RESULT_SUCCESS ||
        binding.vertex_buffer != token || binding.vertex_offset != 2 ||
        binding.instance_frequency != 3) {
        return 0;
    }

    binding = sentinel;
    if (cna_vertex_buffer_binding_init(CNA_INVALID_HANDLE, 0, 0, &binding) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        memcmp(&binding, &sentinel, sizeof(binding)) != 0 ||
        cna_vertex_buffer_binding_init(token, -1, 0, &binding) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        memcmp(&binding, &sentinel, sizeof(binding)) != 0 ||
        cna_vertex_buffer_binding_init(token, 0, -1, &binding) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        memcmp(&binding, &sentinel, sizeof(binding)) != 0 ||
        cna_vertex_buffer_binding_init(token, 0, 0, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_creation_failures(void)
{
    CNA_VertexDeclarationHandle handle = UINT64_MAX;
    CNA_VertexElement invalid = Elements[0];
    if (cna_vertex_declaration_create(0, 0U, &handle) != CNA_RESULT_INVALID_ARGUMENT ||
        handle != CNA_INVALID_HANDLE ||
        cna_vertex_declaration_create_with_stride(16, 0, 0U, &handle) !=
            CNA_RESULT_INVALID_ARGUMENT || handle != CNA_INVALID_HANDLE ||
        cna_vertex_declaration_create_with_stride(
            0, Elements, sizeof(Elements) / sizeof(Elements[0]), &handle) !=
            CNA_RESULT_INVALID_ARGUMENT || handle != CNA_INVALID_HANDLE ||
        cna_vertex_declaration_create(Elements, 1U, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_declaration_create_with_stride(16, Elements, 1U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    invalid.format = UINT32_C(12);
    handle = UINT64_MAX;
    if (cna_vertex_declaration_create(&invalid, 1U, &handle) !=
            CNA_RESULT_INVALID_ARGUMENT || handle != CNA_INVALID_HANDLE) {
        return 0;
    }
    invalid = Elements[0];
    invalid.usage = UINT32_C(13);
    handle = UINT64_MAX;
    if (cna_vertex_declaration_create(&invalid, 1U, &handle) !=
            CNA_RESULT_INVALID_ARGUMENT || handle != CNA_INVALID_HANDLE) {
        return 0;
    }
    invalid = Elements[0];
    invalid.offset = -1;
    handle = UINT64_MAX;
    if (cna_vertex_declaration_create(&invalid, 1U, &handle) !=
            CNA_RESULT_INVALID_ARGUMENT || handle != CNA_INVALID_HANDLE) {
        return 0;
    }
    invalid = Elements[0];
    invalid.usage_index = -1;
    handle = UINT64_MAX;
    if (cna_vertex_declaration_create(&invalid, 1U, &handle) !=
            CNA_RESULT_INVALID_ARGUMENT || handle != CNA_INVALID_HANDLE) {
        return 0;
    }
    invalid = Elements[0];
    invalid.offset = INT32_MAX;
    handle = UINT64_MAX;
    if (cna_vertex_declaration_create(&invalid, 1U, &handle) != CNA_RESULT_OVERFLOW ||
        handle != CNA_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

typedef struct WrongThreadState {
    CNA_VertexDeclarationHandle declaration;
    CNA_Result result;
} WrongThreadState;

static int get_stride_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    int32_t stride = -1;
    state->result = cna_vertex_declaration_get_stride(state->declaration, &stride);
    return 0;
}

static int validate_handle_failures(void)
{
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    int32_t stride = 77;
    uint64_t count = 77U;
    char byte = 'x';
    if (cna_vertex_declaration_create(Elements, 1U, &declaration) != CNA_RESULT_SUCCESS ||
        cna_curve_create(&wrong_kind) != CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_get_stride(wrong_kind, &stride) != CNA_RESULT_INVALID_HANDLE ||
        stride != 77 ||
        cna_vertex_declaration_destroy(wrong_kind) != CNA_RESULT_INVALID_HANDLE ||
        cna_vertex_declaration_get_stride(declaration, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_declaration_copy_elements(declaration, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || count != 77U ||
        cna_vertex_declaration_copy_elements(declaration, 0, 0U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_declaration_get_type_name_byte_count(declaration, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_declaration_copy_type_name(declaration, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || count != 77U || byte != 'x') {
        return 0;
    }

    WrongThreadState wrong_thread = {declaration, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, get_stride_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 0;
    }

    if (cna_vertex_declaration_destroy(declaration) != CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_get_stride(declaration, &stride) != CNA_RESULT_INVALID_HANDLE ||
        stride != 77 ||
        cna_vertex_declaration_destroy(declaration) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_destroy(wrong_kind) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}


/* Refresh GetTypeSize IL_0003-0053; byte sizes are literal, not computed by CNA.
   vertex_resources.h promises a copied array and the supplied explicit stride. */
static int validate_every_format_and_stride_route(void)
{
    static const int32_t sizes[] = {4, 8, 12, 16, 4, 4, 4, 8, 4, 8, 4, 8};
    for (uint32_t ordinal = 0U; ordinal < 12U; ++ordinal) {
        const CNA_VertexElement expected = {8, ordinal, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE, 15};
        for (int explicit_stride = 0; explicit_stride < 2; ++explicit_stride) {
            CNA_VertexDeclarationHandle handle = CNA_INVALID_HANDLE;
            const CNA_Result created = explicit_stride != 0
                ? cna_vertex_declaration_create_with_stride(32, &expected, 1U, &handle)
                : cna_vertex_declaration_create(&expected, 1U, &handle);
            if (created != CNA_RESULT_SUCCESS) return 0;
            int32_t stride = -1;
            uint64_t count = 77U;
            CNA_VertexElement copied = {-1, UINT32_MAX, UINT32_MAX, -1};
            const int ok = cna_vertex_declaration_get_stride(handle, &stride) == CNA_RESULT_SUCCESS &&
                stride == (explicit_stride != 0 ? 32 : 8 + sizes[ordinal]) &&
                cna_vertex_declaration_copy_elements(handle, &copied, 1U, &count) == CNA_RESULT_SUCCESS &&
                count == 1U && element_equals(copied, expected);
            const CNA_Result destroyed = cna_vertex_declaration_destroy(handle);
            if (!ok || destroyed != CNA_RESULT_SUCCESS) return 0;
        }
    }
    return 1;
}

/* Refresh structural validator: offset alignment IL_0117-0127, duplicate
   IL_0128-01ab, overlap IL_01bf-0238. The C create contract invalidates output
   on every failure. These are native structural errors after C element conversion. */
static int validate_structural_creation_failures(void)
{
    static const CNA_VertexElement cases[][2] = {
        {{0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE, 3},
         {8, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE, 3}},
        {{0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0},
         {8, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE, 0}},
        {{2, CNA_VERTEX_ELEMENT_FORMAT_SINGLE, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0},
         {12, CNA_VERTEX_ELEMENT_FORMAT_SINGLE, CNA_VERTEX_ELEMENT_USAGE_COLOR, 0}}
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CNA_VertexDeclarationHandle handle = UINT64_MAX;
        if (cna_vertex_declaration_create(cases[i], 2U, &handle) != CNA_RESULT_INVALID_ARGUMENT ||
            handle != CNA_INVALID_HANDLE) return 0;
        handle = UINT64_MAX;
        if (cna_vertex_declaration_create_with_stride(16, cases[i], 2U, &handle) != CNA_RESULT_INVALID_ARGUMENT ||
            handle != CNA_INVALID_HANDLE) return 0;
    }
    const CNA_VertexElement outside = {12, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_NORMAL, 2};
    CNA_VertexDeclarationHandle handle = UINT64_MAX;
    if (cna_vertex_declaration_create_with_stride(16, &outside, 1U, &handle) != CNA_RESULT_INVALID_ARGUMENT ||
        handle != CNA_INVALID_HANDLE) return 0;
    handle = UINT64_MAX;
    if (cna_vertex_declaration_create_with_stride(15, Elements, 1U, &handle) != CNA_RESULT_INVALID_ARGUMENT ||
        handle != CNA_INVALID_HANDLE) return 0;
    return 1;
}

/* Refresh Dispose is idempotent; declaration metadata getters do not check disposed.
   C vertex_resources.h separates dispose (live owned handle) from destroy (release). */
static int validate_disposal_keeps_declaration_metadata(void)
{
    CNA_VertexDeclarationHandle handle = CNA_INVALID_HANDLE;
    if (cna_vertex_declaration_create(Elements, 3U, &handle) != CNA_RESULT_SUCCESS) return 0;
    CNA_Bool disposed = CNA_TRUE;
    int ok = cna_vertex_declaration_get_is_disposed(handle, &disposed) == CNA_RESULT_SUCCESS &&
        disposed == CNA_FALSE &&
        cna_vertex_declaration_dispose(handle) == CNA_RESULT_SUCCESS &&
        cna_vertex_declaration_get_is_disposed(handle, &disposed) == CNA_RESULT_SUCCESS &&
        disposed == CNA_TRUE &&
        cna_vertex_declaration_dispose(handle) == CNA_RESULT_SUCCESS &&
        validate_declaration(handle, 24, 3U) && validate_type_name(handle);
    const CNA_Result destroyed = cna_vertex_declaration_destroy(handle);
    disposed = CNA_FALSE;
    ok = ok && destroyed == CNA_RESULT_SUCCESS &&
        cna_vertex_declaration_get_is_disposed(handle, &disposed) == CNA_RESULT_INVALID_HANDLE &&
        disposed == CNA_FALSE;
    return ok;
}

int main(void)
{
    if (!validate_creation_and_copy()) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_binding()) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_creation_failures()) {
        return CNA_TEST_FAIL(3);
    }
    if (!validate_every_format_and_stride_route()) return CNA_TEST_FAIL(5);
    if (!validate_structural_creation_failures()) return CNA_TEST_FAIL(6);
    if (!validate_disposal_keeps_declaration_metadata()) return CNA_TEST_FAIL(7);
    return validate_handle_failures() ? 0 : 4;
}
