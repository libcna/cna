// SPDX-License-Identifier: MS-PL
//
// FX-023 existence gate: prove that a Shader Model 2.0 pixel-shader program with a hand-authored
// D3D9 constant table can be assembled byte-by-byte and accepted by CNA's pinned MojoShader, with
// its sampler symbol reported through MOJOSHADER_parseData. Everything CNA needs for compiled
// sampler-state conformance -- MOJOSHADER_samplerStateRegister records -- is derived from exactly
// those CTAB symbols, so without this the synthetic conformance fixture cannot cover samplers.
//
// Build (see fna3d-spike/README.md for the shared pattern):
//   ccache cc -I<fna3d-src>/MojoShader fx_shader_object_spike.c \
//       <build>/_deps/fna3d-build/libmojoshader.a -lm -o fx_shader_object_spike

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mojoshader.h"

typedef struct Buffer
{
    unsigned char *bytes;
    unsigned int size;
    unsigned int capacity;
} Buffer;

static void reserve(Buffer *buffer, unsigned int extra)
{
    if (buffer->size + extra <= buffer->capacity) return;
    unsigned int capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < buffer->size + extra) capacity *= 2;
    buffer->bytes = (unsigned char *) realloc(buffer->bytes, capacity);
    buffer->capacity = capacity;
}

static void put_u32(Buffer *buffer, unsigned int value)
{
    reserve(buffer, 4);
    buffer->bytes[buffer->size++] = (unsigned char) (value);
    buffer->bytes[buffer->size++] = (unsigned char) (value >> 8);
    buffer->bytes[buffer->size++] = (unsigned char) (value >> 16);
    buffer->bytes[buffer->size++] = (unsigned char) (value >> 24);
}

static void put_u16(Buffer *buffer, unsigned int value)
{
    reserve(buffer, 2);
    buffer->bytes[buffer->size++] = (unsigned char) (value);
    buffer->bytes[buffer->size++] = (unsigned char) (value >> 8);
}

static unsigned int put_string(Buffer *buffer, const char *value)
{
    const unsigned int offset = buffer->size;
    const unsigned int length = (unsigned int) strlen(value) + 1;
    reserve(buffer, length);
    memcpy(buffer->bytes + buffer->size, value, length);
    buffer->size += length;
    return offset;
}

static void patch_u32(Buffer *buffer, unsigned int offset, unsigned int value)
{
    buffer->bytes[offset] = (unsigned char) (value);
    buffer->bytes[offset + 1] = (unsigned char) (value >> 8);
    buffer->bytes[offset + 2] = (unsigned char) (value >> 16);
    buffer->bytes[offset + 3] = (unsigned char) (value >> 24);
}

#define VERSION_PS_2_0 0xFFFF0200u
#define REGTYPE_BITS(t) ((((unsigned int) (t) & 0x7u) << 28) | (((unsigned int) (t) >> 3) << 11))
#define REG_CONST 2
#define REG_COLOROUT 8

/* Builds the CTAB comment body: two symbols, one float4 constant and one 2D sampler. */
static Buffer build_ctab_body(void)
{
    Buffer body = {0};
    put_u32(&body, 28);          /* 0  Size (D3DXSHADER_CONSTANTTABLE) */
    put_u32(&body, 0);           /* 4  Creator, patched */
    put_u32(&body, VERSION_PS_2_0); /* 8  Version -- must equal the shader version token */
    put_u32(&body, 2);           /* 12 Constants */
    put_u32(&body, 28);          /* 16 ConstantInfo offset */
    put_u32(&body, 0);           /* 20 Flags */
    put_u32(&body, 0);           /* 24 Target, patched */

    const unsigned int constantInfo = body.size;
    for (int i = 0; i < 2; ++i)
    {
        put_u32(&body, 0);       /* Name, patched */
        put_u16(&body, 0);       /* RegisterSet, patched */
        put_u16(&body, 0);       /* RegisterIndex, patched */
        put_u16(&body, 1);       /* RegisterCount */
        put_u16(&body, 0);       /* Reserved */
        put_u32(&body, 0);       /* TypeInfo, patched */
        put_u32(&body, 0);       /* DefaultValue */
    }

    const unsigned int tintType = body.size;
    put_u16(&body, MOJOSHADER_SYMCLASS_VECTOR);
    put_u16(&body, MOJOSHADER_SYMTYPE_FLOAT);
    put_u16(&body, 1);           /* rows */
    put_u16(&body, 4);           /* columns */
    put_u16(&body, 1);           /* elements */
    put_u16(&body, 0);           /* struct members */
    put_u32(&body, 0);           /* struct member info */

    const unsigned int samplerType = body.size;
    put_u16(&body, MOJOSHADER_SYMCLASS_OBJECT);
    put_u16(&body, MOJOSHADER_SYMTYPE_SAMPLER2D);
    put_u16(&body, 1);
    put_u16(&body, 1);
    put_u16(&body, 1);
    put_u16(&body, 0);
    put_u32(&body, 0);

    const unsigned int tintName = put_string(&body, "FxTint");
    const unsigned int samplerName = put_string(&body, "FxSampler");
    const unsigned int target = put_string(&body, "ps_2_0");
    const unsigned int creator = put_string(&body, "CNA synthetic conformance fixture");
    while ((body.size & 3u) != 0) { reserve(&body, 1); body.bytes[body.size++] = 0; }

    patch_u32(&body, 4, creator);
    patch_u32(&body, 24, target);

    patch_u32(&body, constantInfo + 0, tintName);
    body.bytes[constantInfo + 4] = 2;  /* RegisterSet: float */
    body.bytes[constantInfo + 6] = 0;  /* RegisterIndex c0 */
    patch_u32(&body, constantInfo + 12, tintType);

    patch_u32(&body, constantInfo + 20 + 0, samplerName);
    body.bytes[constantInfo + 20 + 4] = 3;  /* RegisterSet: sampler */
    body.bytes[constantInfo + 20 + 6] = 0;  /* RegisterIndex s0 */
    patch_u32(&body, constantInfo + 20 + 12, samplerType);
    return body;
}

static Buffer build_pixel_shader(void)
{
    Buffer ctab = build_ctab_body();
    Buffer shader = {0};
    put_u32(&shader, VERSION_PS_2_0);

    const unsigned int commentTokens = 1 + (ctab.size / 4);  /* 'CTAB' plus the body */
    put_u32(&shader, 0x0000FFFEu | (commentTokens << 16));
    put_u32(&shader, 0x42415443u);  /* 'CTAB' */
    reserve(&shader, ctab.size);
    memcpy(shader.bytes + shader.size, ctab.bytes, ctab.size);
    shader.size += ctab.size;
    free(ctab.bytes);

    /* mov oC0, c0 -- one destination and one source token. */
    put_u32(&shader, 0x00000001u | (2u << 24));
    put_u32(&shader, 0x80000000u | REGTYPE_BITS(REG_COLOROUT) | (0xFu << 16) | 0u);
    put_u32(&shader, 0x80000000u | REGTYPE_BITS(REG_CONST) | (0xE4u << 16) | 0u);
    put_u32(&shader, 0x0000FFFFu);
    return shader;
}

static int check_profile(const char *profile, const unsigned char *bytes, unsigned int size);

int main(void)
{
    Buffer shader = build_pixel_shader();
    printf("assembled ps_2_0 program: %u bytes\n", shader.size);

    /* FNA3D picks its driver at runtime; the fixture must survive every profile CNA can hit. */
    int failures = 0;
    failures |= check_profile(MOJOSHADER_PROFILE_SPIRV, shader.bytes, shader.size);
    failures |= check_profile(MOJOSHADER_PROFILE_GLSL120, shader.bytes, shader.size);
    free(shader.bytes);
    printf(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return failures;
}

static int check_profile(const char *profile, const unsigned char *bytes, unsigned int size)
{
    printf("=== profile %s ===\n", profile);
    const MOJOSHADER_parseData *data = MOJOSHADER_parse(
        profile, "main", bytes, size, NULL, 0, NULL, 0, NULL, NULL, NULL);
    int failures = 0;
    if (data == NULL)
    {
        printf("FAIL: MOJOSHADER_parse returned NULL\n");
        return 1;
    }
    for (int i = 0; i < data->error_count; ++i)
    {
        printf("FAIL: error: %s\n", data->errors[i].error);
        failures = 1;
    }
    printf("shader_type=%d version=%d.%d symbols=%d\n",
           (int) data->shader_type, data->major_ver, data->minor_ver, data->symbol_count);
    int sawSampler = 0;
    for (int i = 0; i < data->symbol_count; ++i)
    {
        const MOJOSHADER_symbol *symbol = &data->symbols[i];
        printf("  symbol[%d] name=%s regset=%d index=%u count=%u class=%d type=%d\n",
               i, symbol->name, (int) symbol->register_set, symbol->register_index,
               symbol->register_count, (int) symbol->info.parameter_class,
               (int) symbol->info.parameter_type);
        if (symbol->register_set == MOJOSHADER_SYMREGSET_SAMPLER) sawSampler = 1;
    }
    if (!sawSampler)
    {
        printf("FAIL: no sampler symbol was reported\n");
        failures = 1;
    }
    printf("output_len=%d\n", data->output_len);

    MOJOSHADER_freeParseData(data);
    return failures;
}
