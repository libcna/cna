// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-208. See the header for what this does and why it does it here rather
// than in either target's own route.

#include "CNA/Internal/Renderers/MojoShader/SpirvSamplerLodBias.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x07230203u;

        constexpr std::uint32_t kOpDecorate = 71;
        constexpr std::uint32_t kOpMemberDecorate = 72;
        constexpr std::uint32_t kOpTypeVoid = 19;
        constexpr std::uint32_t kOpTypeInt = 21;
        constexpr std::uint32_t kOpTypeFloat = 22;
        constexpr std::uint32_t kOpTypeVector = 23;
        constexpr std::uint32_t kOpTypeImage = 25;
        constexpr std::uint32_t kOpTypeSampler = 26;
        constexpr std::uint32_t kOpTypeSampledImage = 27;
        constexpr std::uint32_t kOpTypeArray = 28;
        constexpr std::uint32_t kOpTypeStruct = 30;
        constexpr std::uint32_t kOpTypePointer = 32;
        constexpr std::uint32_t kOpTypeFunction = 33;
        constexpr std::uint32_t kOpConstant = 43;
        constexpr std::uint32_t kOpFunction = 54;
        constexpr std::uint32_t kOpVariable = 59;
        constexpr std::uint32_t kOpLoad = 61;
        constexpr std::uint32_t kOpAccessChain = 65;
        constexpr std::uint32_t kOpCopyObject = 83;
        constexpr std::uint32_t kOpSampledImage = 86;
        constexpr std::uint32_t kOpImageSampleImplicitLod = 87;

        constexpr std::uint32_t kDecorationBlock = 2;
        constexpr std::uint32_t kDecorationArrayStride = 6;
        constexpr std::uint32_t kDecorationDescriptorSet = 34;
        constexpr std::uint32_t kDecorationBinding = 33;
        constexpr std::uint32_t kDecorationOffset = 35;

        constexpr std::uint32_t kStorageClassUniformConstant = 0;
        constexpr std::uint32_t kStorageClassUniform = 2;

        constexpr std::uint32_t kImageOperandsBias = 0x1u;

        struct Instruction
        {
            std::uint32_t opcode = 0;
            std::vector<std::uint32_t> words;
        };

        Instruction Make(std::uint32_t opcode, std::vector<std::uint32_t> operands)
        {
            Instruction inst;
            inst.opcode = opcode;
            inst.words.push_back(((static_cast<std::uint32_t>(operands.size()) + 1u) << 16) |
                                 opcode);
            for (const std::uint32_t w : operands) inst.words.push_back(w);
            return inst;
        }

        /// True for an instruction that belongs to the module's type/constant/global-variable
        /// section. Insertion has to land inside that section, never before the annotations.
        bool IsDeclarationSection(std::uint32_t opcode)
        {
            switch (opcode)
            {
                case kOpTypeVoid:
                case kOpTypeInt:
                case kOpTypeFloat:
                case kOpTypeVector:
                case kOpTypeImage:
                case kOpTypeSampler:
                case kOpTypeSampledImage:
                case kOpTypeArray:
                case kOpTypeStruct:
                case kOpTypePointer:
                case kOpTypeFunction:
                case kOpConstant:
                case kOpVariable: return true;
                default: return false;
            }
        }
    }

    SpirvLodBiasResult InjectSamplerLodBias(const std::uint32_t* wordData, std::size_t wordCount,
                                            std::uint32_t descriptorSet, std::uint32_t binding)
    {
        const std::vector<std::uint32_t> input(wordData, wordData + wordCount);
        SpirvLodBiasResult result;
        result.words = input;
        if (input.size() < 5 || input[0] != kMagic)
        {
            result.error = "not a SPIR-V module";
            return result;
        }

        std::vector<Instruction> instructions;
        std::size_t i = 5;
        while (i < input.size())
        {
            const std::uint32_t word0 = input[i];
            const std::uint32_t length = word0 >> 16;
            if (length == 0 || i + length > input.size())
            {
                result.error = "truncated SPIR-V instruction stream";
                return result;
            }
            Instruction inst;
            inst.opcode = word0 & 0xFFFFu;
            inst.words.assign(input.begin() + static_cast<std::ptrdiff_t>(i),
                              input.begin() + static_cast<std::ptrdiff_t>(i + length));
            instructions.push_back(std::move(inst));
            i += length;
        }

        // Pass 1 -- the type graph, the sampler variables and which register each one is.
        std::uint32_t floatType = 0;
        std::uint32_t vec4Type = 0;
        std::uint32_t uintType = 0;
        std::map<std::uint32_t, std::uint32_t> vectorComponent;  // %vec -> component type
        std::map<std::uint32_t, std::uint32_t> vectorCount;      // %vec -> component count
        std::map<std::uint32_t, std::uint32_t> pointerPointee;   // %ptr -> pointee
        std::map<std::uint32_t, std::uint32_t> pointerStorage;   // %ptr -> storage class
        std::map<std::uint32_t, std::uint32_t> variablePointer;  // %var -> %ptr
        std::map<std::uint32_t, std::uint32_t> variableStorage;  // %var -> storage class
        std::set<std::uint32_t> imageTypes;
        std::set<std::uint32_t> samplerTypes;
        std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> setBinding;  // %var -> set,binding
        std::map<std::uint32_t, std::uint32_t> constantValue;  // %const -> literal (32-bit)
        std::map<std::uint32_t, std::uint32_t> constantType;   // %const -> type

        for (const Instruction& inst : instructions)
        {
            const auto& w = inst.words;
            switch (inst.opcode)
            {
                case kOpTypeFloat:
                    if (w.size() >= 3 && w[2] == 32 && floatType == 0) floatType = w[1];
                    break;
                case kOpTypeInt:
                    // Unsigned 32-bit only: an access-chain index wants OpTypeInt 32 0.
                    if (w.size() >= 4 && w[2] == 32 && w[3] == 0 && uintType == 0) uintType = w[1];
                    break;
                case kOpTypeVector:
                    if (w.size() >= 4)
                    {
                        vectorComponent[w[1]] = w[2];
                        vectorCount[w[1]] = w[3];
                    }
                    break;
                case kOpTypeImage:
                    if (w.size() >= 2) imageTypes.insert(w[1]);
                    break;
                case kOpTypeSampler:
                    if (w.size() >= 2) samplerTypes.insert(w[1]);
                    break;
                case kOpTypePointer:
                    if (w.size() >= 4)
                    {
                        pointerStorage[w[1]] = w[2];
                        pointerPointee[w[1]] = w[3];
                    }
                    break;
                case kOpVariable:
                    if (w.size() >= 4)
                    {
                        variablePointer[w[2]] = w[1];
                        variableStorage[w[2]] = w[3];
                    }
                    break;
                case kOpConstant:
                    if (w.size() >= 4)
                    {
                        constantType[w[2]] = w[1];
                        constantValue[w[2]] = w[3];
                    }
                    break;
                case kOpDecorate:
                    if (w.size() >= 4)
                    {
                        if (w[2] == kDecorationDescriptorSet) setBinding[w[1]].first = w[3];
                        else if (w[2] == kDecorationBinding) setBinding[w[1]].second = w[3];
                    }
                    break;
                default: break;
            }
        }

        for (const auto& [id, component] : vectorComponent)
        {
            if (component == floatType && floatType != 0 && vectorCount[id] == 4)
            {
                vec4Type = id;
                break;
            }
        }
        if (floatType == 0 || vec4Type == 0)
        {
            // A module with no float4 samples nothing this transformation is about.
            return result;
        }

        // A texture or sampler variable in the target set, mapped to the D3D9 register it came
        // from. `SplitCombinedImageSamplers` puts the texture half at 2*register and the sampler
        // half at 2*register + 1, which is the whole of the convention this reads back.
        const auto slotOfVariable = [&](std::uint32_t var) -> std::uint32_t {
            const auto ptr = variablePointer.find(var);
            if (ptr == variablePointer.end()) return UINT32_MAX;
            const auto storage = variableStorage.find(var);
            if (storage == variableStorage.end() || storage->second != kStorageClassUniformConstant)
                return UINT32_MAX;
            const auto pointee = pointerPointee.find(ptr->second);
            if (pointee == pointerPointee.end()) return UINT32_MAX;
            if (imageTypes.count(pointee->second) == 0 && samplerTypes.count(pointee->second) == 0)
                return UINT32_MAX;
            const auto decoration = setBinding.find(var);
            if (decoration == setBinding.end()) return UINT32_MAX;
            if (decoration->second.first != descriptorSet) return UINT32_MAX;
            return decoration->second.second / 2u;
        };

        // Pass 2 -- follow each OpImageSampleImplicitLod back to its register.
        std::map<std::uint32_t, std::uint32_t> valueSlot;  // %id -> register
        std::map<std::uint32_t, std::uint32_t> sampleSlot; // sample instruction index -> register
        std::map<std::uint32_t, std::uint32_t> sampleResultType;
        for (std::size_t index = 0; index < instructions.size(); ++index)
        {
            const Instruction& inst = instructions[index];
            const auto& w = inst.words;
            switch (inst.opcode)
            {
                case kOpLoad:
                    if (w.size() >= 4)
                    {
                        const std::uint32_t slot = slotOfVariable(w[3]);
                        if (slot != UINT32_MAX) valueSlot[w[2]] = slot;
                    }
                    break;
                case kOpCopyObject:
                    if (w.size() >= 4)
                    {
                        const auto it = valueSlot.find(w[3]);
                        if (it != valueSlot.end()) valueSlot[w[2]] = it->second;
                    }
                    break;
                case kOpSampledImage:
                    // %si = OpSampledImage %type %image %sampler -- the image half names the
                    // register, and the split guarantees the two halves agree.
                    if (w.size() >= 5)
                    {
                        auto it = valueSlot.find(w[3]);
                        if (it == valueSlot.end()) it = valueSlot.find(w[4]);
                        if (it != valueSlot.end()) valueSlot[w[2]] = it->second;
                    }
                    break;
                case kOpImageSampleImplicitLod:
                    // Only a sample with NO image operands yet: one that already carries an
                    // explicit operand is not this transformation's to rewrite.
                    if (w.size() == 5)
                    {
                        const auto it = valueSlot.find(w[3]);
                        if (it != valueSlot.end())
                        {
                            sampleSlot[static_cast<std::uint32_t>(index)] = it->second;
                            sampleResultType[static_cast<std::uint32_t>(index)] = w[1];
                        }
                    }
                    break;
                default: break;
            }
        }

        if (sampleSlot.empty()) return result;  // nothing samples here; leave the module alone

        std::set<std::uint32_t> slots;
        for (const auto& [index, slot] : sampleSlot) slots.insert(slot);
        for (const std::uint32_t slot : slots)
        {
            if (slot >= kSpirvLodBiasSlotCount)
            {
                result.error = "a compiled effect sampled register s" + std::to_string(slot) +
                               ", which is outside the " +
                               std::to_string(kSpirvLodBiasSlotCount) +
                               " D3D9 sampler registers the LOD-bias block carries";
                return result;
            }
            result.biasedSlots.push_back(slot);
        }

        // Pass 3 -- allocate the ids this needs, reusing whatever the module already declares.
        std::uint32_t bound = input[3];
        const auto NewId = [&bound]() { return bound++; };

        std::vector<Instruction> newTypes;
        if (uintType == 0)
        {
            uintType = NewId();
            newTypes.push_back(Make(kOpTypeInt, {uintType, 32u, 0u}));
        }
        // Index constants: member 0, one per biased register, and component 0.
        std::map<std::uint32_t, std::uint32_t> uintConstant;  // value -> %const
        for (const auto& [id, value] : constantValue)
        {
            const auto type = constantType.find(id);
            if (type != constantType.end() && type->second == uintType &&
                uintConstant.count(value) == 0)
            {
                uintConstant[value] = id;
            }
        }
        const auto ConstantFor = [&](std::uint32_t value) {
            const auto it = uintConstant.find(value);
            if (it != uintConstant.end()) return it->second;
            const std::uint32_t id = NewId();
            uintConstant[value] = id;
            newTypes.push_back(Make(kOpConstant, {uintType, id, value}));
            return id;
        };
        const std::uint32_t zeroConstant = ConstantFor(0u);
        const std::uint32_t countConstant = ConstantFor(kSpirvLodBiasSlotCount);
        std::map<std::uint32_t, std::uint32_t> slotConstant;
        for (const std::uint32_t slot : slots) slotConstant[slot] = ConstantFor(slot);

        const std::uint32_t arrayType = NewId();
        const std::uint32_t blockType = NewId();
        const std::uint32_t blockPointer = NewId();
        const std::uint32_t biasVariable = NewId();
        const std::uint32_t floatPointer = NewId();

        newTypes.push_back(Make(kOpTypeArray, {arrayType, vec4Type, countConstant}));
        newTypes.push_back(Make(kOpTypeStruct, {blockType, arrayType}));
        newTypes.push_back(Make(kOpTypePointer, {blockPointer, kStorageClassUniform, blockType}));
        newTypes.push_back(
            Make(kOpVariable, {blockPointer, biasVariable, kStorageClassUniform}));
        newTypes.push_back(Make(kOpTypePointer, {floatPointer, kStorageClassUniform, floatType}));

        std::vector<Instruction> newDecorations;
        newDecorations.push_back(Make(kOpDecorate, {arrayType, kDecorationArrayStride, 16u}));
        newDecorations.push_back(Make(kOpDecorate, {blockType, kDecorationBlock}));
        newDecorations.push_back(Make(kOpMemberDecorate, {blockType, 0u, kDecorationOffset, 0u}));
        newDecorations.push_back(
            Make(kOpDecorate, {biasVariable, kDecorationDescriptorSet, descriptorSet}));
        newDecorations.push_back(Make(kOpDecorate, {biasVariable, kDecorationBinding, binding}));

        // Pass 4 -- rebuild. Decorations join the annotation section, types and the variable join
        // the declaration section, and each sample grows a Bias operand fed by two new
        // instructions immediately in front of it.
        std::size_t firstDeclaration = instructions.size();
        std::size_t firstFunction = instructions.size();
        for (std::size_t index = 0; index < instructions.size(); ++index)
        {
            if (firstDeclaration == instructions.size() &&
                IsDeclarationSection(instructions[index].opcode))
            {
                // An OpVariable with Function storage lives inside a function, not here; the
                // declaration section is entered by the first TYPE, which always precedes it.
                if (instructions[index].opcode != kOpVariable) firstDeclaration = index;
            }
            if (instructions[index].opcode == kOpFunction)
            {
                firstFunction = index;
                break;
            }
        }
        if (firstDeclaration > firstFunction) firstDeclaration = firstFunction;

        std::vector<Instruction> rebuilt;
        rebuilt.reserve(instructions.size() + newTypes.size() + newDecorations.size() +
                        sampleSlot.size() * 2u);
        for (std::size_t index = 0; index < instructions.size(); ++index)
        {
            if (index == firstDeclaration)
            {
                for (const Instruction& inst : newDecorations) rebuilt.push_back(inst);
            }
            if (index == firstFunction)
            {
                for (const Instruction& inst : newTypes) rebuilt.push_back(inst);
            }

            const auto sample = sampleSlot.find(static_cast<std::uint32_t>(index));
            if (sample == sampleSlot.end())
            {
                rebuilt.push_back(instructions[index]);
                continue;
            }

            const std::uint32_t pointerId = NewId();
            const std::uint32_t biasId = NewId();
            rebuilt.push_back(Make(kOpAccessChain, {floatPointer, pointerId, biasVariable,
                                                    zeroConstant, slotConstant[sample->second],
                                                    zeroConstant}));
            rebuilt.push_back(Make(kOpLoad, {floatType, biasId, pointerId}));

            Instruction biased = instructions[index];
            biased.words.push_back(kImageOperandsBias);
            biased.words.push_back(biasId);
            biased.words[0] = ((static_cast<std::uint32_t>(biased.words.size())) << 16) |
                              kOpImageSampleImplicitLod;
            rebuilt.push_back(std::move(biased));
        }
        // A module whose every instruction precedes the insertion points (impossible for a real
        // shader, but the loop above must not silently drop them).
        if (firstDeclaration >= instructions.size())
            for (const Instruction& inst : newDecorations) rebuilt.push_back(inst);
        if (firstFunction >= instructions.size())
            for (const Instruction& inst : newTypes) rebuilt.push_back(inst);

        std::vector<std::uint32_t> out;
        out.reserve(input.size() + 64);
        out.assign(input.begin(), input.begin() + 5);
        out[3] = bound;
        for (const Instruction& inst : rebuilt)
            out.insert(out.end(), inst.words.begin(), inst.words.end());

        result.words = std::move(out);
        result.changed = true;
        return result;
    }
}
