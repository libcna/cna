// SPDX-License-Identifier: MS-PL
//
// See the header for why a fragment module is adapted to the pipeline that draws it.

#include "CNA/Internal/Renderers/MojoShader/SpirvPipelineAdaptation.hpp"

#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x07230203u;
        constexpr std::size_t kHeaderWords = 5;
        constexpr std::size_t kBoundWord = 3;

        constexpr std::uint32_t kOpEntryPoint = 15;
        constexpr std::uint32_t kOpTypeFloat = 22;
        constexpr std::uint32_t kOpTypeVector = 23;
        constexpr std::uint32_t kOpTypePointer = 32;
        constexpr std::uint32_t kOpConstant = 43;
        constexpr std::uint32_t kOpConstantComposite = 44;
        constexpr std::uint32_t kOpFunction = 54;
        constexpr std::uint32_t kOpFunctionEnd = 56;
        constexpr std::uint32_t kOpVariable = 59;
        constexpr std::uint32_t kOpLoad = 61;
        constexpr std::uint32_t kOpStore = 62;
        constexpr std::uint32_t kOpCompositeConstruct = 80;
        constexpr std::uint32_t kOpCompositeExtract = 81;
        constexpr std::uint32_t kOpFAdd = 129;
        constexpr std::uint32_t kOpFMul = 133;
        constexpr std::uint32_t kOpReturn = 253;
        constexpr std::uint32_t kOpCopyMemory = 63;
        constexpr std::uint32_t kOpAccessChain = 65;
        constexpr std::uint32_t kOpInBoundsAccessChain = 66;
        constexpr std::uint32_t kOpPtrAccessChain = 67;
        constexpr std::uint32_t kOpDecorate = 71;
        constexpr std::uint32_t kOpSelect = 169;

        constexpr std::uint32_t kDecorationBuiltIn = 11;
        constexpr std::uint32_t kDecorationLocation = 30;
        constexpr std::uint32_t kDecorationComponent = 31;
        constexpr std::uint32_t kDecorationIndex = 32;
        constexpr std::uint32_t kBuiltInPosition = 0;
        constexpr std::uint32_t kBuiltInFrontFacing = 17;

        constexpr std::uint32_t kStorageClassOutput = 3;
        constexpr std::uint32_t kStorageClassPrivate = 6;

        /// Calls @p visit(offset, opcode, wordCount) for each instruction; false on a malformed one.
        template <typename Visit>
        bool ForEachInstruction(const std::vector<std::uint32_t>& words, Visit&& visit)
        {
            for (std::size_t offset = kHeaderWords; offset < words.size();)
            {
                const std::uint32_t wordCount = words[offset] >> 16;
                if (wordCount == 0 || offset + wordCount > words.size()) return false;
                visit(offset, words[offset] & 0xFFFFu, wordCount);
                offset += wordCount;
            }
            return true;
        }

        SpirvPipelineAdaptationResult Start(const std::uint32_t* words, std::size_t wordCount)
        {
            SpirvPipelineAdaptationResult result;
            if (words != nullptr) result.words.assign(words, words + wordCount);
            if (result.words.size() < kHeaderWords || result.words[0] != kMagic)
                result.error = "not a SPIR-V module";
            return result;
        }

        /// Index of the first interface id of the OpEntryPoint at @p at: past its literal name.
        std::size_t EntryPointInterfaceStart(const std::vector<std::uint32_t>& words,
                                             std::size_t at, std::uint32_t count)
        {
            std::size_t word = at + 3;
            while (word < at + count)
            {
                const std::uint32_t packed = words[word++];
                if ((packed & 0xFF000000u) == 0 || (packed & 0x00FF0000u) == 0 ||
                    (packed & 0x0000FF00u) == 0 || (packed & 0x000000FFu) == 0)
                {
                    break;
                }
            }
            return word;
        }
    }

    SpirvPipelineAdaptationResult MirrorFrontFacing(const std::uint32_t* words, std::size_t wordCount)
    {
        SpirvPipelineAdaptationResult result = Start(words, wordCount);
        if (!result.error.empty()) return result;

        std::set<std::uint32_t> frontFacingVariables;
        const bool walked = ForEachInstruction(result.words,
            [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
                if (opcode == kOpDecorate && count >= 4 &&
                    result.words[at + 2] == kDecorationBuiltIn &&
                    result.words[at + 3] == kBuiltInFrontFacing)
                {
                    frontFacingVariables.insert(result.words[at + 1]);
                }
            });
        if (!walked)
        {
            result.error = "malformed SPIR-V instruction stream";
            return result;
        }
        if (frontFacingVariables.empty()) return result;

        std::set<std::uint32_t> loads;
        ForEachInstruction(result.words, [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
            if (opcode == kOpLoad && count >= 4 && frontFacingVariables.count(result.words[at + 3]) != 0)
                loads.insert(result.words[at + 2]);
        });

        std::vector<std::uint32_t> mirrored = result.words;
        std::set<std::uint32_t> selected;
        ForEachInstruction(mirrored, [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
            if (opcode == kOpSelect && count == 6 && loads.count(mirrored[at + 3]) != 0)
            {
                std::swap(mirrored[at + 4], mirrored[at + 5]);
                selected.insert(mirrored[at + 3]);
            }
        });
        if (selected.size() != loads.size())
        {
            result.error = "FrontFacing is read other than through MojoShader's vFace select";
            return result;
        }
        result.changed = !selected.empty();
        result.words = std::move(mirrored);
        return result;
    }

    SpirvPipelineAdaptationResult DropUnattachedColorOutputs(const std::uint32_t* words,
                                                           std::size_t wordCount,
                                                           std::uint32_t attachedCount)
    {
        SpirvPipelineAdaptationResult result = Start(words, wordCount);
        if (!result.error.empty()) return result;
        const std::vector<std::uint32_t>& in = result.words;

        std::map<std::uint32_t, std::uint32_t> locations;  // variable -> Location
        std::set<std::uint32_t> builtIns;
        std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> pointers;  // id -> (class, pointee)
        std::map<std::uint32_t, std::uint32_t> privatePointers;  // pointee -> Private pointer id
        std::map<std::uint32_t, std::uint32_t> outputVariables;  // variable -> pointer type
        const bool walked = ForEachInstruction(in,
            [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
                if (opcode == kOpDecorate && count >= 4 && in[at + 2] == kDecorationLocation)
                    locations[in[at + 1]] = in[at + 3];
                else if (opcode == kOpDecorate && count >= 3 && in[at + 2] == kDecorationBuiltIn)
                    builtIns.insert(in[at + 1]);
                else if (opcode == kOpTypePointer && count == 4)
                {
                    pointers[in[at + 1]] = {in[at + 2], in[at + 3]};
                    if (in[at + 2] == kStorageClassPrivate) privatePointers.emplace(in[at + 3], in[at + 1]);
                }
                else if (opcode == kOpVariable && count >= 4 && in[at + 3] == kStorageClassOutput)
                    outputVariables[in[at + 2]] = in[at + 1];
            });
        if (!walked)
        {
            result.error = "malformed SPIR-V instruction stream";
            return result;
        }

        std::set<std::uint32_t> dropped;
        for (const auto& [variable, pointerType] : outputVariables)
        {
            const auto location = locations.find(variable);
            if (location != locations.end() && location->second >= attachedCount &&
                builtIns.count(variable) == 0 && pointers.count(pointerType) != 0)
            {
                dropped.insert(variable);
            }
        }
        if (dropped.empty()) return result;

        bool chained = false;
        ForEachInstruction(in, [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
            if ((opcode == kOpAccessChain || opcode == kOpInBoundsAccessChain ||
                 opcode == kOpPtrAccessChain) && count >= 4 && dropped.count(in[at + 3]) != 0)
                chained = true;
            if (opcode == kOpCopyMemory && count >= 3 &&
                (dropped.count(in[at + 1]) != 0 || dropped.count(in[at + 2]) != 0))
                chained = true;
        });
        if (chained)
        {
            result.error = "a colour output is addressed other than through OpLoad/OpStore";
            return result;
        }

        // A Private pointer to each dropped output's type, declared right after its Output pointer.
        std::uint32_t bound = in[kBoundWord];
        std::map<std::uint32_t, std::uint32_t> newPointerAfter;  // Output pointer -> new Private id
        std::map<std::uint32_t, std::uint32_t> retype;           // variable -> Private pointer
        for (const std::uint32_t variable : dropped)
        {
            const std::uint32_t outputPointer = outputVariables[variable];
            const std::uint32_t pointee = pointers[outputPointer].second;
            auto existing = privatePointers.find(pointee);
            if (existing == privatePointers.end())
            {
                existing = privatePointers.emplace(pointee, bound++).first;
                newPointerAfter[outputPointer] = existing->second;
            }
            retype[variable] = existing->second;
        }

        std::vector<std::uint32_t> out(in.begin(), in.begin() + kHeaderWords);
        out[kBoundWord] = bound;
        ForEachInstruction(in, [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
            if (opcode == kOpDecorate && count >= 3 && dropped.count(in[at + 1]) != 0 &&
                (in[at + 2] == kDecorationLocation || in[at + 2] == kDecorationComponent ||
                 in[at + 2] == kDecorationIndex))
            {
                return;
            }
            if (opcode == kOpEntryPoint)
            {
                const std::size_t interfaceStart = EntryPointInterfaceStart(in, at, count);
                std::vector<std::uint32_t> kept(in.begin() + static_cast<std::ptrdiff_t>(at),
                                                in.begin() + static_cast<std::ptrdiff_t>(interfaceStart));
                for (std::size_t word = interfaceStart; word < at + count; ++word)
                    if (dropped.count(in[word]) == 0) kept.push_back(in[word]);
                kept[0] = (static_cast<std::uint32_t>(kept.size()) << 16) | kOpEntryPoint;
                out.insert(out.end(), kept.begin(), kept.end());
                return;
            }
            const std::size_t first = out.size();
            out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(at),
                       in.begin() + static_cast<std::ptrdiff_t>(at + count));
            if (opcode == kOpVariable && count >= 4 && retype.count(in[at + 2]) != 0)
            {
                out[first + 1] = retype[in[at + 2]];
                out[first + 3] = kStorageClassPrivate;
            }
            if (opcode == kOpTypePointer && count == 4)
            {
                const auto added = newPointerAfter.find(in[at + 1]);
                if (added != newPointerAfter.end())
                {
                    out.push_back((4u << 16) | kOpTypePointer);
                    out.push_back(added->second);
                    out.push_back(kStorageClassPrivate);
                    out.push_back(in[at + 3]);
                }
            }
        });
        result.words = std::move(out);
        result.changed = true;
        return result;
    }

    SpirvPipelineAdaptationResult OffsetClipPosition(const std::uint32_t* words,
                                                     std::size_t wordCount, float offsetX,
                                                     float offsetY)
    {
        SpirvPipelineAdaptationResult result = Start(words, wordCount);
        if (!result.error.empty() || (offsetX == 0.0f && offsetY == 0.0f)) return result;
        const std::vector<std::uint32_t>& in = result.words;

        std::set<std::uint32_t> positions;
        std::map<std::uint32_t, std::uint32_t> variableTypes;  // Output variable -> pointer type
        std::map<std::uint32_t, std::uint32_t> pointees;       // pointer type -> pointee
        std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> vectors;  // id -> (component, count)
        std::set<std::uint32_t> floats32;
        std::uint32_t entryFunction = 0;
        const bool walked = ForEachInstruction(in,
            [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
                if (opcode == kOpDecorate && count >= 4 && in[at + 2] == kDecorationBuiltIn &&
                    in[at + 3] == kBuiltInPosition)
                    positions.insert(in[at + 1]);
                else if (opcode == kOpVariable && count >= 4 && in[at + 3] == kStorageClassOutput)
                    variableTypes[in[at + 2]] = in[at + 1];
                else if (opcode == kOpTypePointer && count == 4)
                    pointees[in[at + 1]] = in[at + 3];
                else if (opcode == kOpTypeVector && count == 4)
                    vectors[in[at + 1]] = {in[at + 2], in[at + 3]};
                else if (opcode == kOpTypeFloat && count >= 3 && in[at + 2] == 32)
                    floats32.insert(in[at + 1]);
                else if (opcode == kOpEntryPoint && count >= 3 && entryFunction == 0)
                    entryFunction = in[at + 2];
            });
        if (!walked)
        {
            result.error = "malformed SPIR-V instruction stream";
            return result;
        }
        if (positions.size() != 1 || variableTypes.count(*positions.begin()) == 0 ||
            entryFunction == 0)
        {
            result.error = "the vertex module has no single Position output";
            return result;
        }
        const std::uint32_t position = *positions.begin();
        const auto pointee = pointees.find(variableTypes[position]);
        const auto vector = pointee != pointees.end() ? vectors.find(pointee->second) : vectors.end();
        if (vector == vectors.end() || vector->second.second != 4 ||
            floats32.count(vector->second.first) == 0)
        {
            result.error = "the vertex module's Position output is not a float4";
            return result;
        }
        const std::uint32_t vec4 = vector->first;
        const std::uint32_t float32 = vector->second.first;

        std::uint32_t bound = in[kBoundWord];
        const std::uint32_t idX = bound++, idY = bound++, idZero = bound++, idOffset = bound++;
        const auto bits = [](float value) {
            std::uint32_t word = 0;
            std::memcpy(&word, &value, sizeof(word));
            return word;
        };

        std::vector<std::uint32_t> out(in.begin(), in.begin() + kHeaderWords);
        bool inEntry = false;
        ForEachInstruction(in, [&](std::size_t at, std::uint32_t opcode, std::uint32_t count) {
            if (opcode == kOpFunction && count >= 3) inEntry = in[at + 2] == entryFunction;
            else if (opcode == kOpFunctionEnd) inEntry = false;
            if (inEntry && opcode == kOpReturn)
            {
                // Only opcodes SpirvToWgsl's compiled-effect subset also translates.
                const std::uint32_t loaded = bound++, w = bound++, ww = bound++, scaled = bound++,
                                    moved = bound++;
                out.insert(out.end(), {(4u << 16) | kOpLoad, vec4, loaded, position,
                                       (5u << 16) | kOpCompositeExtract, float32, w, loaded, 3u,
                                       (7u << 16) | kOpCompositeConstruct, vec4, ww, w, w, w, w,
                                       (5u << 16) | kOpFMul, vec4, scaled, idOffset, ww,
                                       (5u << 16) | kOpFAdd, vec4, moved, loaded, scaled,
                                       (3u << 16) | kOpStore, position, moved});
            }
            out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(at),
                       in.begin() + static_cast<std::ptrdiff_t>(at + count));
            if (opcode == kOpTypeVector && count == 4 && in[at + 1] == vec4)
            {
                out.insert(out.end(), {(4u << 16) | kOpConstant, float32, idX, bits(offsetX),
                                       (4u << 16) | kOpConstant, float32, idY, bits(offsetY),
                                       (4u << 16) | kOpConstant, float32, idZero, 0u,
                                       (7u << 16) | kOpConstantComposite, vec4, idOffset, idX, idY,
                                       idZero, idZero});
            }
        });
        out[kBoundWord] = bound;
        result.words = std::move(out);
        result.changed = true;
        return result;
    }
}
