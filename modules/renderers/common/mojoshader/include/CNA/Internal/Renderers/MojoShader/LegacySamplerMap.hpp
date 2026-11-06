// SPDX-License-Identifier: MS-PL
#pragma once

#include "mojoshader.h"

#include <vector>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    struct LegacySamplerMap
    {
        std::vector<MOJOSHADER_samplerMap> entries;
        bool differs = false;
    };

    /**
     * @brief Infers ps_1.x sampler dimensions from shader symbols when legacy opcodes lack declarations.
     *
     * @param parsed Initial MojoShader parse result.
     * @return Sampler overrides and whether the initial parse used a different dimension.
     */
    inline LegacySamplerMap InferLegacySamplerMap(const MOJOSHADER_parseData* parsed)
    {
        LegacySamplerMap result;
        if (parsed == nullptr || parsed->error_count != 0 ||
            parsed->shader_type != MOJOSHADER_TYPE_PIXEL || parsed->major_ver != 1)
            return result;

        for (unsigned int symbolIndex = 0; symbolIndex < parsed->symbol_count; ++symbolIndex)
        {
            const MOJOSHADER_symbol& symbol = parsed->symbols[symbolIndex];
            if (symbol.register_set != MOJOSHADER_SYMREGSET_SAMPLER)
                continue;
            MOJOSHADER_samplerType type = MOJOSHADER_SAMPLER_UNKNOWN;
            if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER ||
                symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER1D ||
                symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER2D)
                type = MOJOSHADER_SAMPLER_2D;
            else if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER3D)
                type = MOJOSHADER_SAMPLER_VOLUME;
            else if (symbol.info.parameter_type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
                type = MOJOSHADER_SAMPLER_CUBE;
            if (type == MOJOSHADER_SAMPLER_UNKNOWN)
                continue;

            result.entries.push_back({static_cast<int>(symbol.register_index), type});
            const MOJOSHADER_sampler* parsedSampler = nullptr;
            for (int samplerIndex = 0; samplerIndex < parsed->sampler_count; ++samplerIndex)
                if (parsed->samplers[samplerIndex].index ==
                    static_cast<int>(symbol.register_index))
                    parsedSampler = &parsed->samplers[samplerIndex];
            result.differs = result.differs || parsedSampler == nullptr || parsedSampler->type != type;
        }
        return result;
    }
}
