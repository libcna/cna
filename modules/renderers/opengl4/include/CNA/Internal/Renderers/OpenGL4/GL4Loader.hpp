// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_opengl4.md GL4-2: a small, hand-rolled loader for the subset of desktop OpenGL 4.x core
// profile functions this renderer actually calls. Deliberately NOT a vendored third-party loader
// (no glad/GLEW dependency added to the tree) -- mirrors this project's existing "zero new
// third-party dependency" preference for a from-scratch native renderer (see plans/plan_sdlgpu.md's own
// "Why a GPU renderer" rationale). The handful of pre-1.2 entry points (glClear, glViewport,
// glGenTextures, glTexImage2D, glReadPixels, ...) are declared by the platform's own <GL/gl.h> (or
// macOS/<OpenGL/gl.h>) and are linked directly against libGL/OpenGL.framework -- only functions
// introduced by GL 1.2+ (buffers, VAOs, shaders/programs, GL_TEXTURE0+, separate blend
// funcs/equations, mipmap generation) need a runtime platform GL lookup, done once by
// LoadGL4Functions() right after the GL context is made current.
//
// Every loaded entry point is named gl4_<realName> (e.g. gl4_glCreateShader) rather than shadowing
// the real GL name -- this avoids any ambiguity with the pre-1.2 functions declared by the system
// GL header and linked normally, and matches how a caller reads: "this one came from the loader".

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace CNA::Internal::Renderers::OpenGL4::GL4
{
    // Local aliases for the handful of GL 1.5+ typedefs not guaranteed to be declared by the
    // platform's own (GL 1.1-vintage) <GL/gl.h>. Deliberately distinct names from the real
    // GLchar/GLsizeiptr/GLintptr Khronos tokens to avoid any redefinition risk if a platform
    // header (or something else this renderer later includes) does declare them.
    using GLchar4    = char;
    using GLsizeiptr4 = std::ptrdiff_t;
    using GLintptr4   = std::ptrdiff_t;

    // ---- Tokens not guaranteed to be defined by a GL-1.1-vintage <GL/gl.h> ----
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
// plans/plan_opengl4.md GL4-33: needed by the generic VertexElement-to-GL-attribute mapper's
// HalfVector2/HalfVector4 case (GL 3.0 core / ARB_half_float_vertex).
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_FUNC_SUBTRACT
#define GL_FUNC_SUBTRACT 0x800A
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif
#ifndef GL_DEBUG_OUTPUT
#define GL_DEBUG_OUTPUT 0x92E0
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_STENCIL_ATTACHMENT
#define GL_STENCIL_ATTACHMENT 0x8D20
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16 0x81A5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_INCR_WRAP
#define GL_INCR_WRAP 0x8507
#endif
#ifndef GL_DECR_WRAP
#define GL_DECR_WRAP 0x8508
#endif
#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_INTERNALFORMAT_SUPPORTED
#define GL_INTERNALFORMAT_SUPPORTED 0x826F
#endif
#ifndef GL_FRAMEBUFFER_RENDERABLE
#define GL_FRAMEBUFFER_RENDERABLE 0x8289
#endif
#ifndef GL_FULL_SUPPORT
#define GL_FULL_SUPPORT 0x82B7
#endif
#ifndef GL_CAVEAT_SUPPORT
#define GL_CAVEAT_SUPPORT 0x82B8
#endif

    // plans/plan_opengl4_modern_graphics.md GL4-0009: tokens the EasyGL-parity draw, format and
    // debug paths need. Guarded because a modern <GL/gl.h> (Mesa includes glext.h) already
    // defines them while a GL-1.1-vintage header (Windows) does not.
#ifndef GL_SAMPLE_MASK
#define GL_SAMPLE_MASK 0x8E51
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_SUBPIXEL_BITS
#define GL_SUBPIXEL_BITS 0x0D50
#endif
#ifndef GL_CONTEXT_PROFILE_MASK
#define GL_CONTEXT_PROFILE_MASK 0x9126
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef GL_CONTEXT_FLAGS
#define GL_CONTEXT_FLAGS 0x821E
#endif
#ifndef GL_CONTEXT_FLAG_DEBUG_BIT
#define GL_CONTEXT_FLAG_DEBUG_BIT 0x00000002
#endif
#ifndef GL_DEBUG_OUTPUT_SYNCHRONOUS
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#endif
#ifndef GL_DEBUG_SOURCE_API
#define GL_DEBUG_SOURCE_API 0x8246
#endif
#ifndef GL_DEBUG_SOURCE_WINDOW_SYSTEM
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM 0x8247
#endif
#ifndef GL_DEBUG_SOURCE_SHADER_COMPILER
#define GL_DEBUG_SOURCE_SHADER_COMPILER 0x8248
#endif
#ifndef GL_DEBUG_SOURCE_THIRD_PARTY
#define GL_DEBUG_SOURCE_THIRD_PARTY 0x8249
#endif
#ifndef GL_DEBUG_SOURCE_APPLICATION
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#endif
#ifndef GL_DEBUG_SOURCE_OTHER
#define GL_DEBUG_SOURCE_OTHER 0x824B
#endif
#ifndef GL_DEBUG_TYPE_ERROR
#define GL_DEBUG_TYPE_ERROR 0x824C
#endif
#ifndef GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#endif
#ifndef GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR 0x824E
#endif
#ifndef GL_DEBUG_TYPE_PORTABILITY
#define GL_DEBUG_TYPE_PORTABILITY 0x824F
#endif
#ifndef GL_DEBUG_TYPE_PERFORMANCE
#define GL_DEBUG_TYPE_PERFORMANCE 0x8250
#endif
#ifndef GL_DEBUG_TYPE_OTHER
#define GL_DEBUG_TYPE_OTHER 0x8251
#endif
#ifndef GL_DEBUG_TYPE_MARKER
#define GL_DEBUG_TYPE_MARKER 0x8268
#endif
#ifndef GL_DEBUG_TYPE_PUSH_GROUP
#define GL_DEBUG_TYPE_PUSH_GROUP 0x8269
#endif
#ifndef GL_DEBUG_TYPE_POP_GROUP
#define GL_DEBUG_TYPE_POP_GROUP 0x826A
#endif
#ifndef GL_DEBUG_SEVERITY_HIGH
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#endif
#ifndef GL_DEBUG_SEVERITY_MEDIUM
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#endif
#ifndef GL_DEBUG_SEVERITY_LOW
#define GL_DEBUG_SEVERITY_LOW 0x9148
#endif
#ifndef GL_DEBUG_SEVERITY_NOTIFICATION
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#endif
#ifndef GL_DONT_CARE
#define GL_DONT_CARE 0x1100
#endif
#ifndef GL_TEXTURE_SWIZZLE_R
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#endif
#ifndef GL_TEXTURE_SWIZZLE_G
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#endif
#ifndef GL_TEXTURE_SWIZZLE_B
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif
#ifndef GL_MAX_DRAW_BUFFERS
#define GL_MAX_DRAW_BUFFERS 0x8824
#endif
#ifndef GL_MAX_COLOR_ATTACHMENTS
#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_RGB565
#define GL_RGB565 0x8D62
#endif
#ifndef GL_RGBA8_SNORM
#define GL_RGBA8_SNORM 0x8F97
#endif
#ifndef GL_RG8_SNORM
#define GL_RG8_SNORM 0x8F95
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2 0x8059
#endif
#ifndef GL_RG16
#define GL_RG16 0x822C
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif
#ifndef GL_R16F
#define GL_R16F 0x822D
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif
#ifndef GL_RGBA4
#define GL_RGBA4 0x8056
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif
#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4_REV
#define GL_UNSIGNED_SHORT_4_4_4_4_REV 0x8365
#endif
#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_DEPTH32F_STENCIL8
#define GL_DEPTH32F_STENCIL8 0x8CAD
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL 0x813C
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_TEXTURE_MIN_LOD
#define GL_TEXTURE_MIN_LOD 0x813A
#endif
#ifndef GL_TEXTURE_MAX_LOD
#define GL_TEXTURE_MAX_LOD 0x813B
#endif
#ifndef GL_MAX_TEXTURE_LOD_BIAS
#define GL_MAX_TEXTURE_LOD_BIAS 0x84FD
#endif
#ifndef GL_UNPACK_IMAGE_HEIGHT
#define GL_UNPACK_IMAGE_HEIGHT 0x806E
#endif
#ifndef GL_PACK_IMAGE_HEIGHT
#define GL_PACK_IMAGE_HEIGHT 0x806C
#endif
#ifndef GL_UNPACK_SKIP_IMAGES
#define GL_UNPACK_SKIP_IMAGES 0x806D
#endif
#ifndef GL_PACK_SKIP_IMAGES
#define GL_PACK_SKIP_IMAGES 0x806B
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER 0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#endif
#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#endif
#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#endif
#ifndef GL_POLYGON_OFFSET_LINE
#define GL_POLYGON_OFFSET_LINE 0x2A02
#endif
#ifndef GL_POLYGON_OFFSET_POINT
#define GL_POLYGON_OFFSET_POINT 0x2A01
#endif
#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_ACTIVE_ATTRIBUTES
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#endif
#ifndef GL_ACTIVE_ATTRIBUTE_MAX_LENGTH
#define GL_ACTIVE_ATTRIBUTE_MAX_LENGTH 0x8B8A
#endif
#ifndef GL_INT_VEC2
#define GL_INT_VEC2 0x8B53
#define GL_INT_VEC3 0x8B54
#define GL_INT_VEC4 0x8B55
#endif
#ifndef GL_UNSIGNED_INT_VEC2
#define GL_UNSIGNED_INT_VEC2 0x8DC6
#define GL_UNSIGNED_INT_VEC3 0x8DC7
#define GL_UNSIGNED_INT_VEC4 0x8DC8
#endif
#ifndef GL_TEXTURE_BINDING_CUBE_MAP
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#endif
#ifndef GL_TEXTURE_BINDING_3D
#define GL_TEXTURE_BINDING_3D 0x806A
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#endif
#ifndef GL_RENDERBUFFER_BINDING
#define GL_RENDERBUFFER_BINDING 0x8CA7
#endif
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif
#ifndef GL_FRAMEBUFFER_UNDEFINED
#define GL_FRAMEBUFFER_UNDEFINED 0x8219
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 0x8CDB
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER 0x8CDC
#endif
#ifndef GL_FRAMEBUFFER_UNSUPPORTED
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS 0x8DA8
#endif
#ifndef GL_INVALID_FRAMEBUFFER_OPERATION
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506
#endif
#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D94
#endif
// plans/plan_opengl4_modern_graphics.md GL4-0012: the texture/render-target layer's per-format
// sample-count query (GL_SAMPLES through glGetInternalformativ) and the pixel-buffer bindings its
// transfers set aside and restore.
#ifndef GL_SAMPLES
#define GL_SAMPLES 0x80A9
#endif
#ifndef GL_PIXEL_PACK_BUFFER_BINDING
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
// plans/plan_opengl4_modern_graphics.md GL4-0025: compute, shader-storage and uniform buffers,
// memory barriers, image load/store, timer queries and the limits Workstream B reports.
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_R16
#define GL_R16 0x822A
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_BINDING
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3
#endif
#ifndef GL_UNIFORM_BUFFER_BINDING
#define GL_UNIFORM_BUFFER_BINDING 0x8A28
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif
#ifndef GL_DRAW_INDIRECT_BUFFER_BINDING
#define GL_DRAW_INDIRECT_BUFFER_BINDING 0x8F43
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS 0x8864
#endif
#ifndef GL_MAX_COMPUTE_WORK_GROUP_COUNT
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT 0x91BE
#endif
#ifndef GL_MAX_COMPUTE_WORK_GROUP_SIZE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE 0x91BF
#endif
#ifndef GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#endif
#ifndef GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS
#define GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS 0x90D6
#endif
#ifndef GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS
#define GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS 0x90DB
#endif
#ifndef GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS 0x90DD
#endif
#ifndef GL_MAX_SHADER_STORAGE_BLOCK_SIZE
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE 0x90DE
#endif
#ifndef GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT
#define GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT 0x90DF
#endif
#ifndef GL_MAX_UNIFORM_BLOCK_SIZE
#define GL_MAX_UNIFORM_BLOCK_SIZE 0x8A30
#endif
#ifndef GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8A34
#endif
#ifndef GL_MAX_COMPUTE_UNIFORM_BLOCKS
#define GL_MAX_COMPUTE_UNIFORM_BLOCKS 0x91BB
#endif
#ifndef GL_MAX_UNIFORM_BUFFER_BINDINGS
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#endif
#ifndef GL_MAX_COMPUTE_IMAGE_UNIFORMS
#define GL_MAX_COMPUTE_IMAGE_UNIFORMS 0x91BD
#endif
#ifndef GL_MAX_IMAGE_UNITS
#define GL_MAX_IMAGE_UNITS 0x8F38
#endif
#ifndef GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS 0x91BC
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#endif
#ifndef GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#endif
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif
#ifndef GL_MAX_VERTEX_ATTRIB_BINDINGS
#define GL_MAX_VERTEX_ATTRIB_BINDINGS 0x82DA
#endif
#ifndef GL_MAX_COLOR_ATTACHMENTS
#define GL_MAX_COLOR_ATTACHMENTS 0x8CDF
#endif
#ifndef GL_MAX_ARRAY_TEXTURE_LAYERS
#define GL_MAX_ARRAY_TEXTURE_LAYERS 0x88FF
#endif
#ifndef GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#endif
#ifndef GL_ELEMENT_ARRAY_BARRIER_BIT
#define GL_ELEMENT_ARRAY_BARRIER_BIT 0x00000002
#endif
#ifndef GL_UNIFORM_BARRIER_BIT
#define GL_UNIFORM_BARRIER_BIT 0x00000004
#endif
#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#endif
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_COMMAND_BARRIER_BIT
#define GL_COMMAND_BARRIER_BIT 0x00000040
#endif
#ifndef GL_PIXEL_BUFFER_BARRIER_BIT
#define GL_PIXEL_BUFFER_BARRIER_BIT 0x00000080
#endif
#ifndef GL_TEXTURE_UPDATE_BARRIER_BIT
#define GL_TEXTURE_UPDATE_BARRIER_BIT 0x00000100
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif
#ifndef GL_FRAMEBUFFER_BARRIER_BIT
#define GL_FRAMEBUFFER_BARRIER_BIT 0x00000400
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_ALL_BARRIER_BITS
#define GL_ALL_BARRIER_BITS 0xFFFFFFFF
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_IMAGE_BINDING_NAME
#define GL_IMAGE_BINDING_NAME 0x8F3A
#endif
#ifndef GL_FILTER
#define GL_FILTER 0x829A
#endif
#ifndef GL_FRAMEBUFFER_BLEND
#define GL_FRAMEBUFFER_BLEND 0x828B
#endif
#ifndef GL_MIPMAP
#define GL_MIPMAP 0x8293
#endif
#ifndef GL_FRAGMENT_TEXTURE
#define GL_FRAGMENT_TEXTURE 0x829F
#endif
#ifndef GL_COMPUTE_TEXTURE
#define GL_COMPUTE_TEXTURE 0x82A0
#endif
#ifndef GL_SHADER_IMAGE_LOAD
#define GL_SHADER_IMAGE_LOAD 0x82A4
#endif
#ifndef GL_SHADER_IMAGE_STORE
#define GL_SHADER_IMAGE_STORE 0x82A5
#endif
#ifndef GL_SHADER_IMAGE_ATOMIC
#define GL_SHADER_IMAGE_ATOMIC 0x82A6
#endif
#ifndef GL_NUM_SAMPLE_COUNTS
#define GL_NUM_SAMPLE_COUNTS 0x9380
#endif

    // ---- Function pointer types for the loaded subset (Khronos-standard PFN names) ----
    using PFNGL4GENBUFFERSPROC              = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDBUFFERPROC              = void (*)(GLenum, GLuint);
    using PFNGL4BUFFERDATAPROC              = void (*)(GLenum, GLsizeiptr4, const void*, GLenum);
    using PFNGL4BUFFERSUBDATAPROC           = void (*)(GLenum, GLintptr4, GLsizeiptr4, const void*);
    using PFNGL4DELETEBUFFERSPROC           = void (*)(GLsizei, const GLuint*);

    using PFNGL4GENVERTEXARRAYSPROC         = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDVERTEXARRAYPROC         = void (*)(GLuint);
    using PFNGL4DELETEVERTEXARRAYSPROC      = void (*)(GLsizei, const GLuint*);
    using PFNGL4VERTEXATTRIBPOINTERPROC     = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    using PFNGL4ENABLEVERTEXATTRIBARRAYPROC = void (*)(GLuint);
    using PFNGL4DISABLEVERTEXATTRIBARRAYPROC= void (*)(GLuint);
    // plans/plan_opengl4.md GL4-22: SkinnedEffect's BlendIndices attribute is a real integer vertex
    // attribute (bone indices, read as uvec4/ivec4 in GLSL) -- glVertexAttribPointer's implicit
    // int-to-float conversion is wrong for this case, so the true GL 3.0 core
    // glVertexAttribIPointer entry point is needed instead.
    using PFNGL4VERTEXATTRIBIPOINTERPROC    = void (*)(GLuint, GLint, GLenum, GLsizei, const void*);

    using PFNGL4CREATESHADERPROC            = GLuint (*)(GLenum);
    using PFNGL4SHADERSOURCEPROC            = void (*)(GLuint, GLsizei, const GLchar4* const*, const GLint*);
    using PFNGL4COMPILESHADERPROC           = void (*)(GLuint);
    using PFNGL4GETSHADERIVPROC             = void (*)(GLuint, GLenum, GLint*);
    using PFNGL4GETSHADERINFOLOGPROC        = void (*)(GLuint, GLsizei, GLsizei*, GLchar4*);
    using PFNGL4DELETESHADERPROC            = void (*)(GLuint);
    using PFNGL4CREATEPROGRAMPROC           = GLuint (*)();
    using PFNGL4ATTACHSHADERPROC            = void (*)(GLuint, GLuint);
    using PFNGL4LINKPROGRAMPROC             = void (*)(GLuint);
    using PFNGL4GETPROGRAMIVPROC            = void (*)(GLuint, GLenum, GLint*);
    using PFNGL4GETPROGRAMINFOLOGPROC       = void (*)(GLuint, GLsizei, GLsizei*, GLchar4*);
    using PFNGL4DELETEPROGRAMPROC           = void (*)(GLuint);
    using PFNGL4USEPROGRAMPROC              = void (*)(GLuint);
    using PFNGL4BINDATTRIBLOCATIONPROC      = void (*)(GLuint, GLuint, const GLchar4*);

    using PFNGL4GETUNIFORMLOCATIONPROC      = GLint (*)(GLuint, const GLchar4*);
    // plans/plan_opengl4_modern_graphics.md GL4-0023: active-attribute reflection (GL 2.0 core).
    using PFNGL4GETACTIVEATTRIBPROC         = void (*)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar4*);
    using PFNGL4GETATTRIBLOCATIONPROC       = GLint (*)(GLuint, const GLchar4*);
    using PFNGL4UNIFORM1IPROC               = void (*)(GLint, GLint);
    using PFNGL4UNIFORM1FPROC               = void (*)(GLint, GLfloat);
    using PFNGL4UNIFORM2FPROC               = void (*)(GLint, GLfloat, GLfloat);
    using PFNGL4UNIFORM3FPROC               = void (*)(GLint, GLfloat, GLfloat, GLfloat);
    using PFNGL4UNIFORM4FPROC               = void (*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    using PFNGL4UNIFORM1FVPROC              = void (*)(GLint, GLsizei, const GLfloat*);
    using PFNGL4UNIFORM2FVPROC              = void (*)(GLint, GLsizei, const GLfloat*);
    using PFNGL4UNIFORMMATRIX4FVPROC        = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);

    using PFNGL4ACTIVETEXTUREPROC           = void (*)(GLenum);
    using PFNGL4GENERATEMIPMAPPROC          = void (*)(GLenum);

    using PFNGL4BLENDFUNCSEPARATEPROC       = void (*)(GLenum, GLenum, GLenum, GLenum);
    using PFNGL4BLENDEQUATIONSEPARATEPROC   = void (*)(GLenum, GLenum);
    using PFNGL4BLENDCOLORPROC              = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);

    using PFNGL4GENSAMPLERSPROC             = void (*)(GLsizei, GLuint*);
    using PFNGL4DELETESAMPLERSPROC          = void (*)(GLsizei, const GLuint*);
    using PFNGL4BINDSAMPLERPROC             = void (*)(GLuint, GLuint);
    using PFNGL4SAMPLERPARAMETERIPROC       = void (*)(GLuint, GLenum, GLint);
    using PFNGL4SAMPLERPARAMETERFPROC       = void (*)(GLuint, GLenum, GLfloat);

    // plans/plan_opengl4.md GL4-14: RenderTarget2D FBO support.
    using PFNGL4GENFRAMEBUFFERSPROC             = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDFRAMEBUFFERPROC             = void (*)(GLenum, GLuint);
    using PFNGL4DELETEFRAMEBUFFERSPROC          = void (*)(GLsizei, const GLuint*);
    using PFNGL4FRAMEBUFFERTEXTURE2DPROC        = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using PFNGL4CHECKFRAMEBUFFERSTATUSPROC      = GLenum (*)(GLenum);
    using PFNGL4GENRENDERBUFFERSPROC            = void (*)(GLsizei, GLuint*);
    using PFNGL4BINDRENDERBUFFERPROC            = void (*)(GLenum, GLuint);
    using PFNGL4DELETERENDERBUFFERSPROC         = void (*)(GLsizei, const GLuint*);
    using PFNGL4RENDERBUFFERSTORAGEPROC         = void (*)(GLenum, GLenum, GLsizei, GLsizei);
    using PFNGL4RENDERBUFFERSTORAGEMULTISAMPLEPROC = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    using PFNGL4FRAMEBUFFERRENDERBUFFERPROC     = void (*)(GLenum, GLenum, GLenum, GLuint);
    using PFNGL4BLITFRAMEBUFFERPROC             = void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    using PFNGL4DRAWBUFFERSPROC                 = void (*)(GLsizei, const GLenum*);

    // plans/plan_opengl4.md GL4-16: two-sided (front/back) stencil state -- GL 2.0 core, not
    // guaranteed to be declared by a GL-1.1-vintage <GL/gl.h>.
    using PFNGL4STENCILFUNCSEPARATEPROC         = void (*)(GLenum, GLenum, GLint, GLuint);
    using PFNGL4STENCILOPSEPARATEPROC           = void (*)(GLenum, GLenum, GLenum, GLenum);
    using PFNGL4STENCILMASKSEPARATEPROC         = void (*)(GLenum, GLuint);
    using PFNGL4COLORMASKIPROC                  = void (*)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);

    // plans/plan_opengl4.md GL4-20: plain Texture3D -- GL 1.2 core, not guaranteed to be declared by a
    // GL-1.1-vintage <GL/gl.h> (same rationale as the other GL4-prefixed entries above).
    using PFNGL4TEXIMAGE3DPROC                  = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    using PFNGL4TEXSUBIMAGE3DPROC               = void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
    // GL 3.0 core -- attaches one Z-layer of a 3D texture to an FBO, used for Texture3D::GetData's
    // per-slice glReadPixels loop (desktop GL has no per-sub-rectangle 3D-texture readback other
    // than this FBO-per-slice approach).
    using PFNGL4FRAMEBUFFERTEXTURELAYERPROC     = void (*)(GLenum, GLenum, GLuint, GLint, GLint);

    // plans/plan_opengl4.md GL4-24: real occlusion queries -- GL 1.5 core, not guaranteed to be
    // declared by a GL-1.1-vintage <GL/gl.h>.
    using PFNGL4GENQUERIESPROC                  = void (*)(GLsizei, GLuint*);
    using PFNGL4DELETEQUERIESPROC               = void (*)(GLsizei, const GLuint*);
    using PFNGL4BEGINQUERYPROC                  = void (*)(GLenum, GLuint);
    using PFNGL4ENDQUERYPROC                    = void (*)(GLenum);
    using PFNGL4GETQUERYOBJECTUIVPROC           = void (*)(GLuint, GLenum, GLuint*);

    // plans/plan_opengl4.md GL4-27: real GpuDrawParams::baseVertex support -- GL 3.2 core
    // (ARB_draw_elements_base_vertex, core since GL 3.2), not guaranteed to be declared by a
    // GL-1.1-vintage <GL/gl.h>.
    using PFNGL4DRAWELEMENTSBASEVERTEXPROC      = void (*)(GLenum, GLsizei, GLenum, const void*, GLint);

    // plans/plan_opengl4.md GL4-33: real GpuDrawParams-driven hardware instancing -- GL 3.1 core
    // (glDrawElementsInstanced) and GL 3.3 core / ARB_instanced_arrays (glVertexAttribDivisor),
    // neither guaranteed to be declared by a GL-1.1-vintage <GL/gl.h>.
    using PFNGL4DRAWELEMENTSINSTANCEDPROC       = void (*)(GLenum, GLsizei, GLenum, const void*, GLsizei);
    using PFNGL4VERTEXATTRIBDIVISORPROC         = void (*)(GLuint, GLuint);

    // plans/plan_modern.md MOD-2260: optional post-4.1 entry points. These are deliberately kept
    // out of LoadGL4Functions(): failure to resolve any of them narrows the modern subset instead
    // of preventing creation of the renderer's portable GL 4.1/XNA path.
    using PFNGL4GETSTRINGIPROC                   = const GLubyte* (*)(GLenum, GLuint);
    using PFNGL4DISPATCHCOMPUTEPROC              = void (*)(GLuint, GLuint, GLuint);
    using PFNGL4BINDBUFFERBASEPROC               = void (*)(GLenum, GLuint, GLuint);
    using PFNGL4GETINTEGERI_VPROC                = void (*)(GLenum, GLuint, GLint*);
    using PFNGL4BINDIMAGETEXTUREPROC             = void (*)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
    using PFNGL4MEMORYBARRIERPROC                = void (*)(GLbitfield);
    using PFNGL4DRAWARRAYSINDIRECTPROC           = void (*)(GLenum, const void*);
    using PFNGL4DRAWELEMENTSINDIRECTPROC         = void (*)(GLenum, GLenum, const void*);
    using PFNGL4QUERYCOUNTERPROC                 = void (*)(GLuint, GLenum);
    using PFNGL4GETQUERYOBJECTUI64VPROC          = void (*)(GLuint, GLenum, std::uint64_t*);
    using PFNGL4DRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC =
        void (*)(GLenum, GLsizei, GLenum, const void*, GLsizei, GLint, GLuint);
    using PFNGL4GETINTERNALFORMATIVPROC          = void (*)(GLenum, GLenum, GLenum, GLsizei, GLint*);
    using PFNGL4GETINTEGER64VPROC                = void (*)(GLenum, std::int64_t*);
    using PFNGL4PROGRAMUNIFORM1IPROC             = void (*)(GLuint, GLint, GLint);
    using PFNGL4PROGRAMUNIFORM1FPROC             = void (*)(GLuint, GLint, GLfloat);
    using PFNGL4GETQUERYIVPROC                   = void (*)(GLenum, GLenum, GLint*);

    // plans/plan_opengl4_modern_graphics.md GL4-0009: further GL <= 4.1 core entry points the
    // EasyGL-parity paths need (vector/matrix uniforms for the shared stock programs, instanced
    // base-vertex and non-indexed instanced draws, the multisample coverage mask, compressed and
    // typed texture transfers, buffer copies/readback and per-buffer clears). All are core in
    // every 4.1 context, so a missing one is a broken driver, not a narrower capability.
    using PFNGL4UNIFORM3FVPROC                   = void (*)(GLint, GLsizei, const GLfloat*);
    using PFNGL4UNIFORM4FVPROC                   = void (*)(GLint, GLsizei, const GLfloat*);
    using PFNGL4UNIFORM1IVPROC                   = void (*)(GLint, GLsizei, const GLint*);
    using PFNGL4UNIFORMMATRIX3FVPROC             = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
    using PFNGL4DRAWELEMENTSINSTANCEDBASEVERTEXPROC =
        void (*)(GLenum, GLsizei, GLenum, const void*, GLsizei, GLint);
    using PFNGL4DRAWARRAYSINSTANCEDPROC          = void (*)(GLenum, GLint, GLsizei, GLsizei);
    using PFNGL4SAMPLEMASKIPROC                  = void (*)(GLuint, GLbitfield);
    using PFNGL4COMPRESSEDTEXIMAGE2DPROC         =
        void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
    using PFNGL4COMPRESSEDTEXSUBIMAGE2DPROC      =
        void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*);
    using PFNGL4GETCOMPRESSEDTEXIMAGEPROC        = void (*)(GLenum, GLint, void*);
    using PFNGL4GETBUFFERSUBDATAPROC             = void (*)(GLenum, GLintptr4, GLsizeiptr4, void*);
    using PFNGL4COPYBUFFERSUBDATAPROC            =
        void (*)(GLenum, GLenum, GLintptr4, GLintptr4, GLsizeiptr4);
    using PFNGL4CLEARBUFFERFVPROC                = void (*)(GLenum, GLint, const GLfloat*);
    using PFNGL4CLEARBUFFERIVPROC                = void (*)(GLenum, GLint, const GLint*);
    using PFNGL4CLEARBUFFERFIPROC                = void (*)(GLenum, GLint, GLfloat, GLint);
    using PFNGL4FRAMEBUFFERTEXTUREPROC           = void (*)(GLenum, GLenum, GLuint, GLint);
    using PFNGL4GETFRAMEBUFFERATTACHMENTPARAMETERIVPROC = void (*)(GLenum, GLenum, GLenum, GLint*);
    using PFNGL4GETSAMPLERPARAMETERIVPROC        = void (*)(GLuint, GLenum, GLint*);
    using PFNGL4ISPROGRAMPROC                    = GLboolean (*)(GLuint);

    // KHR_debug (core in 4.3): optional. The callback type is spelled with the platform calling
    // convention because the driver invokes it.
#if defined(_WIN32)
#define CNA_GL4_APIENTRY __stdcall
#else
#define CNA_GL4_APIENTRY
#endif
    using GL4DebugProc = void (CNA_GL4_APIENTRY*)(GLenum source, GLenum type, GLuint id,
                                                  GLenum severity, GLsizei length,
                                                  const GLchar4* message, const void* userParam);
    using PFNGL4DEBUGMESSAGECALLBACKPROC         = void (*)(GL4DebugProc, const void*);
    using PFNGL4DEBUGMESSAGECONTROLPROC          =
        void (*)(GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean);
    using PFNGL4DEBUGMESSAGEINSERTPROC           =
        void (*)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar4*);
    using PFNGL4PUSHDEBUGGROUPPROC               = void (*)(GLenum, GLuint, GLsizei, const GLchar4*);
    using PFNGL4POPDEBUGGROUPPROC                = void (*)();
    using PFNGL4OBJECTLABELPROC                  = void (*)(GLenum, GLuint, GLsizei, const GLchar4*);

    // ---- Loaded function pointers ----
    extern PFNGL4GENBUFFERSPROC               gl4_glGenBuffers;
    extern PFNGL4BINDBUFFERPROC               gl4_glBindBuffer;
    extern PFNGL4BUFFERDATAPROC               gl4_glBufferData;
    extern PFNGL4BUFFERSUBDATAPROC            gl4_glBufferSubData;
    extern PFNGL4DELETEBUFFERSPROC            gl4_glDeleteBuffers;

    extern PFNGL4GENVERTEXARRAYSPROC          gl4_glGenVertexArrays;
    extern PFNGL4BINDVERTEXARRAYPROC          gl4_glBindVertexArray;
    extern PFNGL4DELETEVERTEXARRAYSPROC       gl4_glDeleteVertexArrays;
    extern PFNGL4VERTEXATTRIBPOINTERPROC      gl4_glVertexAttribPointer;
    extern PFNGL4ENABLEVERTEXATTRIBARRAYPROC  gl4_glEnableVertexAttribArray;
    extern PFNGL4DISABLEVERTEXATTRIBARRAYPROC gl4_glDisableVertexAttribArray;
    extern PFNGL4VERTEXATTRIBIPOINTERPROC     gl4_glVertexAttribIPointer;

    extern PFNGL4CREATESHADERPROC             gl4_glCreateShader;
    extern PFNGL4SHADERSOURCEPROC             gl4_glShaderSource;
    extern PFNGL4COMPILESHADERPROC            gl4_glCompileShader;
    extern PFNGL4GETSHADERIVPROC              gl4_glGetShaderiv;
    extern PFNGL4GETSHADERINFOLOGPROC         gl4_glGetShaderInfoLog;
    extern PFNGL4DELETESHADERPROC             gl4_glDeleteShader;
    extern PFNGL4CREATEPROGRAMPROC            gl4_glCreateProgram;
    extern PFNGL4ATTACHSHADERPROC             gl4_glAttachShader;
    extern PFNGL4LINKPROGRAMPROC              gl4_glLinkProgram;
    extern PFNGL4GETPROGRAMIVPROC             gl4_glGetProgramiv;
    extern PFNGL4GETPROGRAMINFOLOGPROC        gl4_glGetProgramInfoLog;
    extern PFNGL4DELETEPROGRAMPROC            gl4_glDeleteProgram;
    extern PFNGL4USEPROGRAMPROC               gl4_glUseProgram;
    extern PFNGL4BINDATTRIBLOCATIONPROC       gl4_glBindAttribLocation;

    extern PFNGL4GETUNIFORMLOCATIONPROC       gl4_glGetUniformLocation;
    extern PFNGL4GETACTIVEATTRIBPROC          gl4_glGetActiveAttrib;
    extern PFNGL4GETATTRIBLOCATIONPROC        gl4_glGetAttribLocation;
    extern PFNGL4UNIFORM1IPROC                gl4_glUniform1i;
    extern PFNGL4UNIFORM1FPROC                gl4_glUniform1f;
    extern PFNGL4UNIFORM2FPROC                gl4_glUniform2f;
    extern PFNGL4UNIFORM3FPROC                gl4_glUniform3f;
    extern PFNGL4UNIFORM4FPROC                gl4_glUniform4f;
    extern PFNGL4UNIFORM1FVPROC               gl4_glUniform1fv;
    extern PFNGL4UNIFORM2FVPROC               gl4_glUniform2fv;
    extern PFNGL4UNIFORMMATRIX4FVPROC         gl4_glUniformMatrix4fv;

    extern PFNGL4ACTIVETEXTUREPROC            gl4_glActiveTexture;
    extern PFNGL4GENERATEMIPMAPPROC           gl4_glGenerateMipmap;

    extern PFNGL4BLENDFUNCSEPARATEPROC        gl4_glBlendFuncSeparate;
    extern PFNGL4BLENDEQUATIONSEPARATEPROC    gl4_glBlendEquationSeparate;
    extern PFNGL4BLENDCOLORPROC               gl4_glBlendColor;

    extern PFNGL4GENSAMPLERSPROC              gl4_glGenSamplers;
    extern PFNGL4DELETESAMPLERSPROC           gl4_glDeleteSamplers;
    extern PFNGL4BINDSAMPLERPROC              gl4_glBindSampler;
    extern PFNGL4SAMPLERPARAMETERIPROC        gl4_glSamplerParameteri;
    extern PFNGL4SAMPLERPARAMETERFPROC        gl4_glSamplerParameterf;

    extern PFNGL4GENFRAMEBUFFERSPROC             gl4_glGenFramebuffers;
    extern PFNGL4BINDFRAMEBUFFERPROC             gl4_glBindFramebuffer;
    extern PFNGL4DELETEFRAMEBUFFERSPROC          gl4_glDeleteFramebuffers;
    extern PFNGL4FRAMEBUFFERTEXTURE2DPROC        gl4_glFramebufferTexture2D;
    extern PFNGL4CHECKFRAMEBUFFERSTATUSPROC      gl4_glCheckFramebufferStatus;
    extern PFNGL4GENRENDERBUFFERSPROC            gl4_glGenRenderbuffers;
    extern PFNGL4BINDRENDERBUFFERPROC            gl4_glBindRenderbuffer;
    extern PFNGL4DELETERENDERBUFFERSPROC         gl4_glDeleteRenderbuffers;
    extern PFNGL4RENDERBUFFERSTORAGEPROC         gl4_glRenderbufferStorage;
    extern PFNGL4RENDERBUFFERSTORAGEMULTISAMPLEPROC gl4_glRenderbufferStorageMultisample;
    extern PFNGL4FRAMEBUFFERRENDERBUFFERPROC     gl4_glFramebufferRenderbuffer;
    extern PFNGL4BLITFRAMEBUFFERPROC             gl4_glBlitFramebuffer;
    extern PFNGL4DRAWBUFFERSPROC                 gl4_glDrawBuffers;

    extern PFNGL4STENCILFUNCSEPARATEPROC         gl4_glStencilFuncSeparate;
    extern PFNGL4STENCILOPSEPARATEPROC           gl4_glStencilOpSeparate;
    extern PFNGL4STENCILMASKSEPARATEPROC         gl4_glStencilMaskSeparate;
    extern PFNGL4COLORMASKIPROC                  gl4_glColorMaski;

    extern PFNGL4TEXIMAGE3DPROC                  gl4_glTexImage3D;
    extern PFNGL4TEXSUBIMAGE3DPROC               gl4_glTexSubImage3D;
    extern PFNGL4FRAMEBUFFERTEXTURELAYERPROC     gl4_glFramebufferTextureLayer;

    extern PFNGL4GENQUERIESPROC                  gl4_glGenQueries;
    extern PFNGL4DELETEQUERIESPROC               gl4_glDeleteQueries;
    extern PFNGL4BEGINQUERYPROC                  gl4_glBeginQuery;
    extern PFNGL4ENDQUERYPROC                    gl4_glEndQuery;
    extern PFNGL4GETQUERYOBJECTUIVPROC           gl4_glGetQueryObjectuiv;

    extern PFNGL4DRAWELEMENTSBASEVERTEXPROC      gl4_glDrawElementsBaseVertex;

    extern PFNGL4DRAWELEMENTSINSTANCEDPROC       gl4_glDrawElementsInstanced;
    extern PFNGL4VERTEXATTRIBDIVISORPROC         gl4_glVertexAttribDivisor;

    extern PFNGL4GETSTRINGIPROC                  gl4_glGetStringi;
    extern PFNGL4DISPATCHCOMPUTEPROC             gl4_glDispatchCompute;
    extern PFNGL4BINDBUFFERBASEPROC              gl4_glBindBufferBase;
    extern PFNGL4GETINTEGERI_VPROC               gl4_glGetIntegeri_v;
    extern PFNGL4BINDIMAGETEXTUREPROC            gl4_glBindImageTexture;
    extern PFNGL4MEMORYBARRIERPROC               gl4_glMemoryBarrier;
    extern PFNGL4DRAWARRAYSINDIRECTPROC          gl4_glDrawArraysIndirect;
    extern PFNGL4DRAWELEMENTSINDIRECTPROC        gl4_glDrawElementsIndirect;
    extern PFNGL4QUERYCOUNTERPROC                gl4_glQueryCounter;
    extern PFNGL4GETQUERYOBJECTUI64VPROC         gl4_glGetQueryObjectui64v;
    extern PFNGL4DRAWELEMENTSINSTANCEDBASEVERTEXBASEINSTANCEPROC
        gl4_glDrawElementsInstancedBaseVertexBaseInstance;
    extern PFNGL4GETINTERNALFORMATIVPROC         gl4_glGetInternalformativ;
    // GL4-0025: 64-bit limit queries and program-object uniforms that do not disturb the bound
    // program. Optional like the rest of this block; the compute route requires them.
    extern PFNGL4GETINTEGER64VPROC               gl4_glGetInteger64v;
    extern PFNGL4PROGRAMUNIFORM1IPROC            gl4_glProgramUniform1i;
    extern PFNGL4PROGRAMUNIFORM1FPROC            gl4_glProgramUniform1f;
    /** GL4-0027: the timestamp counter width, asked before a GPU timer is promised. */
    extern PFNGL4GETQUERYIVPROC                  gl4_glGetQueryiv;

    // GL4-0009 mandatory additions (see the types above).
    extern PFNGL4UNIFORM3FVPROC                   gl4_glUniform3fv;
    extern PFNGL4UNIFORM4FVPROC                   gl4_glUniform4fv;
    extern PFNGL4UNIFORM1IVPROC                   gl4_glUniform1iv;
    extern PFNGL4UNIFORMMATRIX3FVPROC             gl4_glUniformMatrix3fv;
    extern PFNGL4DRAWELEMENTSINSTANCEDBASEVERTEXPROC gl4_glDrawElementsInstancedBaseVertex;
    extern PFNGL4DRAWARRAYSINSTANCEDPROC          gl4_glDrawArraysInstanced;
    extern PFNGL4SAMPLEMASKIPROC                  gl4_glSampleMaski;
    extern PFNGL4COMPRESSEDTEXIMAGE2DPROC         gl4_glCompressedTexImage2D;
    extern PFNGL4COMPRESSEDTEXSUBIMAGE2DPROC      gl4_glCompressedTexSubImage2D;
    extern PFNGL4GETCOMPRESSEDTEXIMAGEPROC        gl4_glGetCompressedTexImage;
    extern PFNGL4GETBUFFERSUBDATAPROC             gl4_glGetBufferSubData;
    extern PFNGL4COPYBUFFERSUBDATAPROC            gl4_glCopyBufferSubData;
    extern PFNGL4CLEARBUFFERFVPROC                gl4_glClearBufferfv;
    extern PFNGL4CLEARBUFFERIVPROC                gl4_glClearBufferiv;
    extern PFNGL4CLEARBUFFERFIPROC                gl4_glClearBufferfi;
    extern PFNGL4FRAMEBUFFERTEXTUREPROC           gl4_glFramebufferTexture;
    extern PFNGL4GETFRAMEBUFFERATTACHMENTPARAMETERIVPROC gl4_glGetFramebufferAttachmentParameteriv;
    extern PFNGL4GETSAMPLERPARAMETERIVPROC        gl4_glGetSamplerParameteriv;
    extern PFNGL4ISPROGRAMPROC                    gl4_glIsProgram;

    // GL4-0009 optional KHR_debug entry points; null when the context has neither 4.3 nor the
    // extension, in which case debug output is reported unavailable rather than faked.
    extern PFNGL4DEBUGMESSAGECALLBACKPROC         gl4_glDebugMessageCallback;
    extern PFNGL4DEBUGMESSAGECONTROLPROC          gl4_glDebugMessageControl;
    extern PFNGL4DEBUGMESSAGEINSERTPROC           gl4_glDebugMessageInsert;
    extern PFNGL4PUSHDEBUGGROUPPROC               gl4_glPushDebugGroup;
    extern PFNGL4POPDEBUGGROUPPROC                gl4_glPopDebugGroup;
    extern PFNGL4OBJECTLABELPROC                  gl4_glObjectLabel;

    /// Generic function-pointer-getter type matching the platform GL service callback.
    using GetProcAddressFn = void* (*)(const char* name);

    /**
     * @brief Raw version, extension and entry-point facts used to classify the modern GL subset.
     *
     * Kept separate from the classifier so the GL 4.1 floor and extension-only routes can be
     * tested deterministically without requiring a deliberately old physical driver.
     */
    struct ModernCapabilityInputs
    {
        /** @brief Context-reported desktop OpenGL major version. */
        int contextMajor = 0;
        /** @brief Context-reported desktop OpenGL minor version. */
        int contextMinor = 0;
        /** @brief Whether `GL_ARB_compute_shader` is advertised. */
        bool computeShaderExtension = false;
        /** @brief Whether `GL_ARB_shader_storage_buffer_object` is advertised. */
        bool shaderStorageBufferExtension = false;
        /** @brief Whether `GL_ARB_shader_image_load_store` is advertised. */
        bool imageLoadStoreExtension = false;
        /** @brief Whether `GL_EXT_texture_array` is advertised. */
        bool textureArrayExtension = false;
        /** @brief Whether `GL_ARB_draw_indirect` is advertised. */
        bool indirectDrawingExtension = false;
        /** @brief Whether `GL_ARB_timer_query` is advertised. */
        bool timerQueryExtension = false;
        /** @brief Whether `GL_ARB_base_instance` is advertised. */
        bool baseInstanceExtension = false;
        /** @brief Whether `GL_ARB_internalformat_query2` is advertised. */
        bool internalFormatQuery2Extension = false;
        /** @brief Whether every entry point needed for compute dispatch resolved. */
        bool computeEntryPoints = false;
        /** @brief Whether every entry point needed for shader-storage binding resolved. */
        bool shaderStorageBufferEntryPoints = false;
        /** @brief Whether every entry point needed for storage-image use resolved. */
        bool imageLoadStoreEntryPoints = false;
        /** @brief Whether every entry point needed for texture-array allocation resolved. */
        bool textureArrayEntryPoints = false;
        /** @brief Whether both indexed and non-indexed indirect-draw entry points resolved. */
        bool indirectDrawingEntryPoints = false;
        /** @brief Whether every entry point needed for timestamp queries resolved. */
        bool timerQueryEntryPoints = false;
        /** @brief Whether the base-instance indexed draw entry point resolved. */
        bool baseInstanceEntryPoints = false;
        /** @brief Whether the internal-format query entry point resolved. */
        bool internalFormatQueryEntryPoints = false;
    };

    /** @brief Independently classified native modern-feature facts for one current GL context. */
    struct ModernCapabilities
    {
        /** @brief Context-reported desktop OpenGL major version. */
        int contextMajor = 0;
        /** @brief Context-reported desktop OpenGL minor version. */
        int contextMinor = 0;
        /** @brief Native compute dispatch is available; this does not imply a CNA implementation. */
        bool computeShadersNative = false;
        /** @brief Native shader-storage buffers are available; this does not imply CNA support. */
        bool shaderStorageBuffersNative = false;
        /** @brief Native image load/store is available; this does not imply CNA support. */
        bool imageLoadStoreNative = false;
        /** @brief Native two-dimensional texture arrays are available. */
        bool textureArraysNative = false;
        /** @brief Native indexed and non-indexed indirect drawing are both available. */
        bool indirectDrawingNative = false;
        /** @brief Native timestamp queries are available. */
        bool gpuTimersNative = false;
        /** @brief Native indexed base-instance drawing is available. */
        bool baseInstanceDrawingNative = false;
        /** @brief Full internal-format queries are available. */
        bool internalFormatQueriesNative = false;
        /** @brief Whether the RGBA16F renderability answer was obtained from the driver. */
        bool rgba16FloatRenderableKnown = false;
        /** @brief Whether the driver reports RGBA16F as a renderable texture format. */
        bool rgba16FloatRenderable = false;
        /** @brief Whether the RGBA32F renderability answer was obtained from the driver. */
        bool rgba32FloatRenderableKnown = false;
        /** @brief Whether the driver reports RGBA32F as a renderable texture format. */
        bool rgba32FloatRenderable = false;
    };

    /**
     * @brief Classifies modern support from independent version, extension and function facts.
     *
     * @param inputs Facts gathered for one current OpenGL context.
     * @return Independently classified native capabilities; format-query results remain unknown.
     */
    [[nodiscard]] ModernCapabilities ClassifyModernCapabilities(
        const ModernCapabilityInputs& inputs);

    /**
     * @brief Resolves optional functions and probes the modern subset of the current context.
     *
     * Missing post-4.1 functions are recorded as unavailable and never make renderer creation
     * fail. The returned facts describe native GL availability only; renderer overrides must not
     * advertise a CNA feature until its implementation and observable contract are complete.
     *
     * @param getProcAddress Function used to resolve GL entry points for the current context.
     * @return Version, independently classified features and queried float-format support.
     */
    [[nodiscard]] ModernCapabilities DiscoverModernCapabilities(
        GetProcAddressFn getProcAddress);

    /**
     * @brief Resolves every function pointer declared above via @p getProcAddress.
     *
     * Must be called once, with a real current GL context bound, before any other GL4::gl4_*
     * call. Returns false (and leaves a diagnostic on stderr) if any entry point could not be
     * resolved -- every function this renderer actually calls is mandatory in a real GL 4.x core
     * context, so a partial load is treated as a hard failure rather than a capability to probe.
     *
     * @param getProcAddress Function used to resolve each GL entry point by name.
     * @return true if every entry point resolved successfully.
     */
    bool LoadGL4Functions(GetProcAddressFn getProcAddress);
}
