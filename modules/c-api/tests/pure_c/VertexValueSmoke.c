// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(CNA_VertexType) == sizeof(uint32_t),
               "CNA_VertexType must remain fixed width");
_Static_assert(CNA_VERTEX_TYPE_POSITION_COLOR == UINT32_C(0) &&
                   CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE == UINT32_C(1) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE == UINT32_C(2) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED == UINT32_C(3) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE == UINT32_C(4) &&
                   CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED == UINT32_C(5) &&
                   CNA_VERTEX_TYPE_POSITION_TEXTURE == UINT32_C(6),
               "CNA built-in vertex identities must remain stable");
_Static_assert(sizeof(CNA_VertexPositionColor) == 16U &&
                   sizeof(CNA_VertexPositionColorTexture) == 24U &&
                   sizeof(CNA_VertexPositionNormalTangentTexture) == 48U &&
                   sizeof(CNA_VertexPositionNormalTangentTextureSkinned) == 68U &&
                   sizeof(CNA_VertexPositionNormalTexture) == 32U &&
                   sizeof(CNA_VertexPositionNormalTextureSkinned) == 52U &&
                   sizeof(CNA_VertexPositionTexture) == 20U &&
                   sizeof(CNA_VertexValue) == 68U && _Alignof(CNA_VertexValue) == 4U,
               "CNA built-in vertex layouts must remain stable");

typedef struct ExpectedElement {
    int32_t offset;
    CNA_VertexElementFormat format;
    CNA_VertexElementUsage usage;
} ExpectedElement;

typedef struct VertexCase {
    CNA_VertexType type;
    uint32_t stride;
    uint64_t element_count;
    const ExpectedElement* elements;
    const char* text;
} VertexCase;

static const ExpectedElement PositionColorElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_COLOR, CNA_VERTEX_ELEMENT_USAGE_COLOR}
};
static const ExpectedElement PositionColorTextureElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_COLOR, CNA_VERTEX_ELEMENT_USAGE_COLOR},
    {16U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE}
};
static const ExpectedElement PositionNormalTangentTextureElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_NORMAL},
    {24U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR4, CNA_VERTEX_ELEMENT_USAGE_TANGENT},
    {40U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE}
};
static const ExpectedElement PositionNormalTangentTextureSkinnedElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_NORMAL},
    {24U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR4, CNA_VERTEX_ELEMENT_USAGE_TANGENT},
    {40U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE},
    {48U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR4, CNA_VERTEX_ELEMENT_USAGE_BLEND_WEIGHT},
    {64U, CNA_VERTEX_ELEMENT_FORMAT_BYTE4, CNA_VERTEX_ELEMENT_USAGE_BLEND_INDICES}
};
static const ExpectedElement PositionNormalTextureElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_NORMAL},
    {24U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE}
};
static const ExpectedElement PositionNormalTextureSkinnedElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_NORMAL},
    {24U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE},
    {32U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR4, CNA_VERTEX_ELEMENT_USAGE_BLEND_WEIGHT},
    {48U, CNA_VERTEX_ELEMENT_FORMAT_BYTE4, CNA_VERTEX_ELEMENT_USAGE_BLEND_INDICES}
};
static const ExpectedElement PositionTextureElements[] = {
    {0U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION},
    {12U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR2, CNA_VERTEX_ELEMENT_USAGE_TEXTURE_COORDINATE}
};

static CNA_VertexValue sample_vertex(const CNA_VertexType type)
{
    const CNA_Vector3 position = {1.0F, 2.0F, 3.0F};
    const CNA_Vector3 normal = {4.0F, 5.0F, 6.0F};
    const CNA_Vector4 tangent = {7.0F, 8.0F, 9.0F, 10.0F};
    const CNA_Vector2 texture = {11.0F, 12.0F};
    const CNA_Vector4 weight = {13.0F, 14.0F, 15.0F, 16.0F};
    const CNA_Color color = {4U, 5U, 6U, 7U};
    CNA_VertexValue result;
    memset(&result, 0, sizeof(result));
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            result.position_color = (CNA_VertexPositionColor){position, color};
            break;
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            result.position_color_texture =
                (CNA_VertexPositionColorTexture){position, color, texture};
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            result.position_normal_tangent_texture =
                (CNA_VertexPositionNormalTangentTexture){position, normal, tangent, texture};
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            result.position_normal_tangent_texture_skinned =
                (CNA_VertexPositionNormalTangentTextureSkinned){
                    position, normal, tangent, texture, weight, {1U, 2U, 3U, 4U}};
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            result.position_normal_texture =
                (CNA_VertexPositionNormalTexture){position, normal, texture};
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            result.position_normal_texture_skinned =
                (CNA_VertexPositionNormalTextureSkinned){
                    position, normal, texture, weight, {1U, 2U, 3U, 4U}};
            break;
        default:
            result.position_texture = (CNA_VertexPositionTexture){position, texture};
            break;
    }
    return result;
}

static void mutate_vertex(const CNA_VertexType type, CNA_VertexValue* const value)
{
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            value->position_color.position.x = -1.0F;
            break;
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            value->position_color_texture.position.x = -1.0F;
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            value->position_normal_tangent_texture.position.x = -1.0F;
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            value->position_normal_tangent_texture_skinned.position.x = -1.0F;
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            value->position_normal_texture.position.x = -1.0F;
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            value->position_normal_texture_skinned.position.x = -1.0F;
            break;
        default:
            value->position_texture.position.x = -1.0F;
            break;
    }
}

static int validate_defaults(void)
{
    for (CNA_VertexType type = CNA_VERTEX_TYPE_POSITION_COLOR;
         type <= CNA_VERTEX_TYPE_POSITION_TEXTURE; ++type) {
        CNA_VertexValue actual;
        CNA_VertexValue expected;
        memset(&actual, 0xa5, sizeof(actual));
        memset(&expected, 0, sizeof(expected));
        if (type == CNA_VERTEX_TYPE_POSITION_COLOR) {
            expected.position_color.color = (CNA_Color){255U, 255U, 255U, 255U};
        }
        if (cna_vertex_value_init_default(type, &actual) != CNA_RESULT_SUCCESS ||
            memcmp(&actual, &expected, sizeof(actual)) != 0) {
            return 0;
        }
    }
    return cna_vertex_value_init_default(CNA_VERTEX_TYPE_POSITION_TEXTURE + 1U, 0) ==
        CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_vertex_case(const VertexCase* const test_case)
{
    const CNA_VertexValue value = sample_vertex(test_case->type);
    CNA_VertexValue different = value;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = -1;
    uint32_t stride = 0U;
    uint64_t count = 0U;
    CNA_VertexElement elements[6];
    char bytes[256];
    char too_small = 'v';

    mutate_vertex(test_case->type, &different);
    if (cna_vertex_value_equals(test_case->type, &value, &value, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_vertex_value_equals(test_case->type, &value, &different, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_FALSE ||
        cna_vertex_value_not_equals(test_case->type, &value, &different, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_TRUE ||
        cna_vertex_value_not_equals(test_case->type, &value, &value, &predicate) !=
            CNA_RESULT_SUCCESS || predicate != CNA_FALSE ||
        cna_vertex_value_get_hash_code(test_case->type, &value, &hash) != CNA_RESULT_SUCCESS ||
        hash != 0 ||
        cna_vertex_type_get_stride(test_case->type, &stride) != CNA_RESULT_SUCCESS ||
        stride != test_case->stride ||
        cna_vertex_type_copy_elements(test_case->type, 0, 0U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || count != test_case->element_count ||
        cna_vertex_type_copy_elements(test_case->type, elements, 6U, &count) !=
            CNA_RESULT_SUCCESS || count != test_case->element_count) {
        return 0;
    }
    for (uint64_t index = 0U; index < count; ++index) {
        if (elements[index].offset != test_case->elements[index].offset ||
            elements[index].format != test_case->elements[index].format ||
            elements[index].usage != test_case->elements[index].usage ||
            elements[index].usage_index != 0U) {
            return 0;
        }
    }

    const uint64_t expected_length = (uint64_t)strlen(test_case->text);
    if (cna_vertex_value_get_string_byte_count(test_case->type, &value, &count) !=
            CNA_RESULT_SUCCESS || count != expected_length ||
        cna_vertex_value_copy_string(
            test_case->type, &value, &too_small, 1U, &count) != CNA_RESULT_BUFFER_TOO_SMALL ||
        too_small != 'v' || count != expected_length ||
        cna_vertex_value_copy_string(
            test_case->type, &value, bytes, sizeof(bytes), &count) != CNA_RESULT_SUCCESS ||
        count != expected_length || memcmp(bytes, test_case->text, (size_t)count) != 0) {
        return 0;
    }
    return 1;
}

static int validate_all_vertex_types(void)
{
    static const VertexCase cases[] = {
        {CNA_VERTEX_TYPE_POSITION_COLOR, 16U, 2U, PositionColorElements,
         "{{Position:{X:1 Y:2 Z:3} Color:{R:4 G:5 B:6 A:7}}}"},
        {CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE, 24U, 3U, PositionColorTextureElements,
         "{{Position:{X:1 Y:2 Z:3} Color:{R:4 G:5 B:6 A:7} "
         "TextureCoordinate:{X:11 Y:12}}}"},
        {CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE, 48U, 4U,
         PositionNormalTangentTextureElements,
         "{{Position:{X:1 Y:2 Z:3} Normal:{X:4 Y:5 Z:6} Tangent:{X:7 Y:8 Z:9 W:10} "
         "TextureCoordinate:{X:11 Y:12}}}"},
        {CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED, 68U, 6U,
         PositionNormalTangentTextureSkinnedElements,
         "{{Position:{X:1 Y:2 Z:3} Normal:{X:4 Y:5 Z:6} Tangent:{X:7 Y:8 Z:9 W:10} "
         "TextureCoordinate:{X:11 Y:12} BlendWeight:{X:13 Y:14 Z:15 W:16} "
         "BlendIndices:{1 2 3 4}}}"},
        {CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE, 32U, 3U, PositionNormalTextureElements,
         "{{Position:{X:1 Y:2 Z:3} Normal:{X:4 Y:5 Z:6} "
         "TextureCoordinate:{X:11 Y:12}}}"},
        {CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED, 52U, 5U,
         PositionNormalTextureSkinnedElements,
         "{{Position:{X:1 Y:2 Z:3} Normal:{X:4 Y:5 Z:6} TextureCoordinate:{X:11 Y:12} "
         "BlendWeight:{X:13 Y:14 Z:15 W:16} BlendIndices:{1 2 3 4}}}"},
        {CNA_VERTEX_TYPE_POSITION_TEXTURE, 20U, 2U, PositionTextureElements,
         "{{Position:{X:1 Y:2 Z:3} TextureCoordinate:{X:11 Y:12}}}"}
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!validate_vertex_case(&cases[index])) {
            return 0;
        }
    }
    return 1;
}

static int validate_vertex_element(void)
{
    const CNA_VertexElement value = {
        20U, CNA_VERTEX_ELEMENT_FORMAT_VECTOR4, CNA_VERTEX_ELEMENT_USAGE_TANGENT, 3U};
    CNA_VertexElement different = value;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = -1;
    uint64_t count = 0U;
    char bytes[96];
    char too_small = 'e';
    static const char Expected[] =
        "{{Offset:20 Format:Vector4 Usage:Tangent UsageIndex: 3}}";
    different.offset = 24U;
    if (cna_vertex_element_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vertex_element_equals(value, different, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_vertex_element_not_equals(value, different, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vertex_element_not_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_vertex_element_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS || hash != 0 ||
        cna_vertex_element_get_string_byte_count(value, &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(Expected) - 1U ||
        cna_vertex_element_copy_string(value, &too_small, 1U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'e' ||
        cna_vertex_element_copy_string(value, bytes, sizeof(bytes), &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(Expected) - 1U ||
        memcmp(bytes, Expected, sizeof(Expected) - 1U) != 0) {
        return 0;
    }
    return 1;
}

static int validate_failures_are_atomic(void)
{
    const CNA_VertexType invalid_type = UINT32_C(7);
    const CNA_VertexValue input = sample_vertex(CNA_VERTEX_TYPE_POSITION_TEXTURE);
    CNA_VertexValue output;
    CNA_VertexValue sentinel;
    CNA_Bool predicate = UINT8_C(77);
    int32_t hash = INT32_C(77);
    uint32_t stride = UINT32_C(77);
    uint64_t count = UINT64_C(77);
    CNA_VertexElement element = {
        1U, CNA_VERTEX_ELEMENT_FORMAT_SINGLE, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0U};
    const CNA_VertexElement sentinel_element = element;
    CNA_VertexElement invalid_element = element;
    memset(&output, 0xa5, sizeof(output));
    sentinel = output;

    if (cna_vertex_value_init_default(invalid_type, &output) != CNA_RESULT_INVALID_ARGUMENT ||
        memcmp(&output, &sentinel, sizeof(output)) != 0 ||
        cna_vertex_value_equals(invalid_type, &input, &input, &predicate) !=
            CNA_RESULT_INVALID_ARGUMENT || predicate != UINT8_C(77) ||
        cna_vertex_value_not_equals(invalid_type, &input, &input, &predicate) !=
            CNA_RESULT_INVALID_ARGUMENT || predicate != UINT8_C(77) ||
        cna_vertex_value_get_hash_code(invalid_type, &input, &hash) !=
            CNA_RESULT_INVALID_ARGUMENT || hash != INT32_C(77) ||
        cna_vertex_value_get_string_byte_count(invalid_type, &input, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || count != UINT64_C(77) ||
        cna_vertex_type_get_stride(invalid_type, &stride) != CNA_RESULT_INVALID_ARGUMENT ||
        stride != UINT32_C(77) ||
        cna_vertex_type_copy_elements(invalid_type, &element, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || memcmp(&element, &sentinel_element, sizeof(element)) != 0 ||
        count != UINT64_C(77)) {
        return 0;
    }

    invalid_element.format = UINT32_C(12);
    if (cna_vertex_element_equals(invalid_element, invalid_element, &predicate) !=
            CNA_RESULT_INVALID_ARGUMENT || predicate != UINT8_C(77) ||
        cna_vertex_element_get_hash_code(invalid_element, &hash) !=
            CNA_RESULT_INVALID_ARGUMENT || hash != INT32_C(77)) {
        return 0;
    }
    invalid_element = sentinel_element;
    invalid_element.usage = UINT32_C(13);
    if (cna_vertex_element_not_equals(invalid_element, invalid_element, &predicate) !=
            CNA_RESULT_INVALID_ARGUMENT || predicate != UINT8_C(77) ||
        cna_vertex_element_get_string_byte_count(invalid_element, &count) !=
            CNA_RESULT_INVALID_ARGUMENT || count != UINT64_C(77)) {
        return 0;
    }

    if (cna_vertex_value_init_default(CNA_VERTEX_TYPE_POSITION_TEXTURE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_value_equals(CNA_VERTEX_TYPE_POSITION_TEXTURE, 0, &input, &predicate) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_value_not_equals(CNA_VERTEX_TYPE_POSITION_TEXTURE, &input, 0, &predicate) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_value_get_hash_code(CNA_VERTEX_TYPE_POSITION_TEXTURE, &input, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_value_get_string_byte_count(CNA_VERTEX_TYPE_POSITION_TEXTURE, &input, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_value_copy_string(
            CNA_VERTEX_TYPE_POSITION_TEXTURE, &input, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_type_get_stride(CNA_VERTEX_TYPE_POSITION_TEXTURE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_type_copy_elements(CNA_VERTEX_TYPE_POSITION_TEXTURE, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_element_equals(sentinel_element, sentinel_element, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_element_not_equals(sentinel_element, sentinel_element, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_element_get_hash_code(sentinel_element, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_element_get_string_byte_count(sentinel_element, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_element_copy_string(sentinel_element, 0, 1U, &count) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

int main(void)
{
    return validate_defaults() && validate_all_vertex_types() && validate_vertex_element() &&
            validate_failures_are_atomic() ? 0 : 1;
}
