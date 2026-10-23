// SPDX-License-Identifier: MS-PL
//
// The one translation unit that defines CGLTF_IMPLEMENTATION (cgltf.h's single-header convention).
//
// Link boundary -- keep this file separate from GltfImportCore.cpp. cgltf is a small parser that
// more than one subsystem uses: GltfImportCore (glTF models, which also pulls in the Draco decoder)
// and the GamerServices avatar GLB reader. When the implementation lived inside GltfImportCore.cpp,
// the avatar reader's reference to cgltf_parse extracted that whole object, and with it Draco,
// into every program that touched avatars -- see docs/link-size-architecture.md.

// The declarations first: cgltf.h's implementation section has no include guard of its own.
#include "CNA/Internal/GltfImport/CgltfInternals.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <vector>

namespace CNA::Internal::GltfImport
{
    cgltf_size CgltfComponentReadIndex(const void* in, cgltf_component_type componentType)
    {
        return cgltf_component_read_index(in, componentType);
    }

    cgltf_bool CgltfElementReadFloat(const uint8_t* element, cgltf_type type,
                                     cgltf_component_type componentType, cgltf_bool normalized,
                                     cgltf_float* out, cgltf_size elementSize)
    {
        return cgltf_element_read_float(element, type, componentType, normalized, out, elementSize);
    }

    bool CgltfFindInvalidInterpolation(const cgltf_data* data, cgltf_size& animationIndex,
                                       cgltf_size& samplerIndex, std::string& token)
    {
        if (data == nullptr || data->json == nullptr || data->json_size == 0
            || data->animations_count == 0)
        {
            return false;
        }
        // The same tokenizer cgltf_parse used on the same bytes, so a document it accepted
        // tokenizes here too.
        jsmn_parser parser;
        jsmn_init(&parser);
        const int count = jsmn_parse(&parser, data->json, data->json_size, nullptr, 0);
        if (count <= 0) { return false; }
        std::vector<jsmntok_t> tokens(static_cast<std::size_t>(count) + 1);
        jsmn_init(&parser);
        if (jsmn_parse(&parser, data->json, data->json_size, tokens.data(),
                       static_cast<size_t>(count)) <= 0)
        {
            return false;
        }
        tokens[static_cast<std::size_t>(count)].type = JSMN_UNDEFINED;
        const jsmntok_t* const t = tokens.data();
        const auto* const json = reinterpret_cast<const uint8_t*>(data->json);
        const auto isKey = [&](int i, const char* key) {
            return t[i].type == JSMN_STRING && cgltf_json_strcmp(&t[i], json, key) == 0;
        };
        if (t[0].type != JSMN_OBJECT) { return false; }

        // Each loop walks an object's keys or an array's elements; cgltf_skip_json steps over a
        // whole value and answers -1 for a malformed one, which ends the walk.
        int i = 1;
        for (int member = 0; member < t[0].size && i > 0; ++member, i = cgltf_skip_json(t, i + 1))
        {
            if (!isKey(i, "animations") || t[i + 1].type != JSMN_ARRAY) { continue; }
            int animation = i + 2;
            for (int a = 0; a < t[i + 1].size && animation > 0;
                 ++a, animation = cgltf_skip_json(t, animation))
            {
                if (t[animation].type != JSMN_OBJECT) { continue; }
                int field = animation + 1;
                for (int f = 0; f < t[animation].size && field > 0;
                     ++f, field = cgltf_skip_json(t, field + 1))
                {
                    if (!isKey(field, "samplers") || t[field + 1].type != JSMN_ARRAY) { continue; }
                    int sampler = field + 2;
                    for (int s = 0; s < t[field + 1].size && sampler > 0;
                         ++s, sampler = cgltf_skip_json(t, sampler))
                    {
                        if (t[sampler].type != JSMN_OBJECT) { continue; }
                        int key = sampler + 1;
                        for (int k = 0; k < t[sampler].size && key > 0;
                             ++k, key = cgltf_skip_json(t, key + 1))
                        {
                            if (!isKey(key, "interpolation")) { continue; }
                            const jsmntok_t& value = t[key + 1];
                            if (value.type == JSMN_STRING
                                && (cgltf_json_strcmp(&value, json, "LINEAR") == 0
                                    || cgltf_json_strcmp(&value, json, "STEP") == 0
                                    || cgltf_json_strcmp(&value, json, "CUBICSPLINE") == 0))
                            {
                                continue;
                            }
                            animationIndex = static_cast<cgltf_size>(a);
                            samplerIndex = static_cast<cgltf_size>(s);
                            token.assign(data->json + value.start,
                                         static_cast<std::size_t>(value.end - value.start));
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
}
