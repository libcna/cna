#include "CNA/Internal/Renderers/D3D9/D3D9ConstantTable.hpp"

#include <cstdint>
#include <cstring>

namespace CNA::Internal::Renderers::D3D9
{
    namespace
    {
        constexpr uint32_t kCommentOpcodeMask = 0xFFFFu;
        constexpr uint32_t kCommentOpcode = 0xFFFEu;
        constexpr uint32_t kEndToken = 0x0000FFFFu;
        constexpr uint32_t kCtabFourCC = 0x42415443u; // 'C','T','A','B' little-endian

        uint32_t ReadU32(const uint8_t* base, size_t offset)
        {
            uint32_t v;
            std::memcpy(&v, base + offset, sizeof(v));
            return v;
        }

        uint16_t ReadU16(const uint8_t* base, size_t offset)
        {
            uint16_t v;
            std::memcpy(&v, base + offset, sizeof(v));
            return v;
        }

        std::string ReadCString(const uint8_t* ctab, size_t ctabSize, uint32_t offset)
        {
            if (offset >= ctabSize) return {};
            const char* s = reinterpret_cast<const char*>(ctab + offset);
            const size_t maxLen = ctabSize - offset;
            size_t len = 0;
            while (len < maxLen && s[len] != '\0') ++len;
            return std::string(s, len);
        }

        D3D9RegisterSetEXT MapRegisterSet(uint16_t raw)
        {
            // Real D3DXREGISTER_SET values (D3DXRS_BOOL=0, D3DXRS_INT4=1, D3DXRS_FLOAT4=2,
            // D3DXRS_SAMPLER=3).
            switch (raw)
            {
                case 0: return D3D9RegisterSetEXT::Bool;
                case 1: return D3D9RegisterSetEXT::Int4;
                case 2: return D3D9RegisterSetEXT::Float4;
                case 3: return D3D9RegisterSetEXT::Sampler;
                default: return D3D9RegisterSetEXT::Unknown;
            }
        }

        // Parses the D3DXSHADER_CONSTANTTABLE header + D3DXSHADER_CONSTANTINFO[] array.
        // `data` points 4 bytes PAST the 'CTAB' FourCC (i.e. right at the Size field) -- empirically
        // confirmed against real d3dcompiler_47.dll output (D3D9ConstantTableTests.cpp): every
        // offset field in this structure, and every nested Name/TypeInfo/DefaultValue offset
        // inside D3DXSHADER_CONSTANTINFO, is relative to THIS position, not to the 'CTAB' FourCC's
        // own position 4 bytes earlier (a real, easy-to-get-backwards detail this project does not
        // just assume from memory of the documented format -- see this file's own validation test
        // for the cross-check against D3DDisassemble()'s independent "// Registers:" output that
        // caught the original, off-by-4 version of this function during development).
        std::vector<D3D9ShaderConstantEXT> ParseCtabPayload(const uint8_t* data, size_t dataSize)
        {
            std::vector<D3D9ShaderConstantEXT> result;

            // D3DXSHADER_CONSTANTTABLE, offsets relative to data[0]:
            //   +0  Size (sizeof(D3DXSHADER_CONSTANTTABLE), expected 28)
            //   +4  Creator (string offset, unused here)
            //   +8  Version
            //   +12 Constants (constant count)
            //   +16 ConstantInfo (offset to D3DXSHADER_CONSTANTINFO[])
            //   +20 Flags
            //   +24 Target (string offset, unused here)
            if (dataSize < 28) return result;

            const uint32_t numConstants = ReadU32(data, 12);
            const uint32_t constantInfoOffset = ReadU32(data, 16);

            // sizeof(D3DXSHADER_CONSTANTINFO) == 20 bytes, offsets relative to data[0] as well:
            //   +0  Name (string offset)
            //   +4  RegisterSet (WORD)
            //   +6  RegisterIndex (WORD)
            //   +8  RegisterCount (WORD)
            //   +10 Reserved (WORD)
            //   +12 TypeInfo (offset, unused here)
            //   +16 DefaultValue (offset, unused here)
            constexpr size_t kConstantInfoStride = 20;

            // Sanity bound: a real CTAB's own Constants count can never exceed what could fit in
            // the remaining payload. A garbled/corrupted comment (or, empirically confirmed via
            // this file's own mutation-testing pass, a caller that got the FourCC-skip offset
            // wrong) can otherwise read a huge garbage count here, and an unbounded reserve() below
            // throws std::bad_alloc rather than the graceful "return empty" this function's own
            // header comment already promises for malformed input.
            if (numConstants > dataSize / kConstantInfoStride) return result;

            result.reserve(numConstants);
            for (uint32_t c = 0; c < numConstants; ++c)
            {
                const size_t entryOffset = static_cast<size_t>(constantInfoOffset) + c * kConstantInfoStride;
                if (entryOffset + kConstantInfoStride > dataSize) break;

                const uint32_t nameOffset = ReadU32(data, entryOffset + 0);
                const uint16_t registerSet = ReadU16(data, entryOffset + 4);
                const uint16_t registerIndex = ReadU16(data, entryOffset + 6);
                const uint16_t registerCount = ReadU16(data, entryOffset + 8);

                D3D9ShaderConstantEXT constant;
                constant.name = ReadCString(data, dataSize, nameOffset);
                constant.registerSet = MapRegisterSet(registerSet);
                constant.registerIndex = registerIndex;
                constant.registerCount = registerCount;

                if (!constant.name.empty())
                    result.push_back(std::move(constant));
            }

            return result;
        }
    }

    std::vector<D3D9ShaderConstantEXT> ParseConstantTableEXT(const void* bytecode, size_t byteLength)
    {
        std::vector<D3D9ShaderConstantEXT> result;
        if (!bytecode || byteLength < 8) return result;

        const auto* bytes = static_cast<const uint8_t*>(bytecode);

        // bytes[0..4) is the shader version token (e.g. 0xFFFE0300 for vs_3_0) -- not a comment,
        // skip it unconditionally before the token walk begins.
        size_t i = 4;

        while (i + 4 <= byteLength)
        {
            const uint32_t token = ReadU32(bytes, i);

            if (token == kEndToken)
                break; // D3DSIO_END -- no more tokens, and therefore no (more) CTAB comment.

            if ((token & kCommentOpcodeMask) == kCommentOpcode)
            {
                const uint32_t sizeDwords = (token >> 16) & 0x7FFFu;
                const size_t payloadOffset = i + 4;
                const size_t payloadBytes = static_cast<size_t>(sizeDwords) * 4;

                if (payloadBytes >= 4 && payloadOffset + payloadBytes <= byteLength)
                {
                    if (ReadU32(bytes, payloadOffset) == kCtabFourCC)
                    {
                        // Skip the 4-byte 'CTAB' FourCC itself -- ParseCtabPayload's own offset
                        // arithmetic is relative to right after it, not to the FourCC's position.
                        result = ParseCtabPayload(bytes + payloadOffset + 4, payloadBytes - 4);
                        // Real fxc output carries exactly one CTAB comment per shader; stop once
                        // found rather than assuming a second one could exist.
                        break;
                    }
                }

                i = payloadOffset + payloadBytes;
                continue;
            }

            // An ordinary instruction-stream DWORD -- comment tokens are self-describing (their
            // own header carries their full length), but decoding a real instruction's own length
            // needs a full D3D9 opcode table this parser deliberately does not implement (it only
            // needs to locate comment tokens, mirroring D9-73's own compare_against_fxb.py
            // strip_comments() strategy, already proven against 66 real compiled shaders): step
            // forward one DWORD at a time and keep looking for the next comment/END token.
            i += 4;
        }

        return result;
    }
}
