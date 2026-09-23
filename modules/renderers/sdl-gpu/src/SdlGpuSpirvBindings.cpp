// SPDX-License-Identifier: MS-PL
//
// plans/plan_sdlgpu_modern_graphics.md SMG-0006. See the header for what this translates and why
// the translation belongs here rather than in the shared shader sources.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuSpirvBindings.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace CNA::Internal::Renderers::SdlGpu
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x07230203u;

        constexpr std::uint32_t kOpMemberName = 6;
        constexpr std::uint32_t kOpEntryPoint = 15;
        constexpr std::uint32_t kOpExecutionMode = 16;
        constexpr std::uint32_t kOpTypeInt = 21;
        constexpr std::uint32_t kOpTypeImage = 25;
        constexpr std::uint32_t kOpTypeSampler = 26;
        constexpr std::uint32_t kOpTypeSampledImage = 27;
        constexpr std::uint32_t kOpTypeArray = 28;
        constexpr std::uint32_t kOpTypeRuntimeArray = 29;
        constexpr std::uint32_t kOpTypeStruct = 30;
        constexpr std::uint32_t kOpTypePointer = 32;
        constexpr std::uint32_t kOpConstant = 43;
        constexpr std::uint32_t kOpFunction = 54;
        constexpr std::uint32_t kOpVariable = 59;
        constexpr std::uint32_t kOpDecorate = 71;
        constexpr std::uint32_t kOpMemberDecorate = 72;

        constexpr std::uint32_t kDecorationBlock = 2;
        constexpr std::uint32_t kDecorationBufferBlock = 3;
        constexpr std::uint32_t kDecorationArrayStride = 6;
        constexpr std::uint32_t kDecorationNonWritable = 24;
        constexpr std::uint32_t kDecorationLocation = 30;
        constexpr std::uint32_t kDecorationBinding = 33;
        constexpr std::uint32_t kDecorationDescriptorSet = 34;
        constexpr std::uint32_t kDecorationOffset = 35;

        constexpr std::uint32_t kStorageClassUniformConstant = 0;
        constexpr std::uint32_t kStorageClassInput = 1;
        constexpr std::uint32_t kStorageClassUniform = 2;
        constexpr std::uint32_t kStorageClassPushConstant = 9;
        constexpr std::uint32_t kStorageClassStorageBuffer = 12;

        constexpr std::uint32_t kExecutionModelVertex = 0;
        constexpr std::uint32_t kExecutionModelFragment = 4;
        constexpr std::uint32_t kExecutionModelGLCompute = 5;

        constexpr std::uint32_t kExecutionModeLocalSize = 17;

        /// One instruction, kept as its own words so the stream can be rewritten and re-emitted
        /// without recomputing anything but the instructions actually touched.
        struct Instruction
        {
            std::uint32_t opcode = 0;
            std::vector<std::uint32_t> words;
        };

        [[nodiscard]] Instruction MakeDecorate(
            std::uint32_t target, std::uint32_t decoration, std::uint32_t operand)
        {
            Instruction inst;
            inst.opcode = kOpDecorate;
            inst.words = {(4u << 16) | kOpDecorate, target, decoration, operand};
            return inst;
        }

        /// Opens the type/constant/global-variable section, which is exactly where the annotation
        /// section ends. New decorations are inserted immediately before the first of these.
        ///
        /// Asking the question this way round rather than "is this an annotation?" is deliberate:
        /// `OpLine`, `OpNop` and `OpString` are legal in the annotation section AND inside function
        /// bodies, so scanning for the last annotation-shaped instruction finds one in the middle
        /// of a function and inserts a decoration where no decoration may appear.
        [[nodiscard]] bool OpensTypeSection(std::uint32_t opcode)
        {
            // OpType* 19..39, OpConstant* 41..46, OpSpecConstant* 48..52, OpVariable 59,
            // OpFunction 54 (the section after them). Anything at or beyond OpTypeVoid that is not
            // a debug or annotation instruction belongs to the section this opens.
            return (opcode >= 19 && opcode <= 39)     // OpTypeVoid .. OpTypeForwardPointer
                || (opcode >= 41 && opcode <= 46)     // OpConstantTrue .. OpConstantNull
                || (opcode >= 48 && opcode <= 52)     // OpSpecConstantTrue .. OpSpecConstantOp
                || opcode == kOpVariable
                || opcode == kOpFunction;
        }

        constexpr std::uint32_t kOpTypeFloat = 22;
        constexpr std::uint32_t kOpTypeVector = 23;
        constexpr std::uint32_t kOpConstantComposite = 44;
        constexpr std::uint32_t kOpLoad = 61;
        constexpr std::uint32_t kOpStore = 62;
        constexpr std::uint32_t kOpReturn = 253;
        constexpr std::uint32_t kOpAccessChain = 65;
        constexpr std::uint32_t kOpFMul = 133;
        constexpr std::uint32_t kDecorationBuiltIn = 11;
        constexpr std::uint32_t kBuiltInPosition = 0;
        constexpr std::uint32_t kStorageClassOutput = 3;

        [[nodiscard]] Instruction MakeInstruction(
            const std::uint32_t opcode, const std::vector<std::uint32_t>& operands)
        {
            Instruction inst;
            inst.opcode = opcode;
            inst.words.push_back(
                ((static_cast<std::uint32_t>(operands.size()) + 1u) << 16) | opcode);
            for (const std::uint32_t word : operands) inst.words.push_back(word);
            return inst;
        }

        /// The `SDL_gpu` category a resource falls into, once its type has been walked.
        struct Classified
        {
            SpirvResourceKindEXT kind = SpirvResourceKindEXT::SampledTexture;
            bool readOnly = true;
            std::uint32_t arrayLength = 1;
        };
    }

    bool LooksLikeSpirvEXT(const void* bytes, const std::size_t byteCount) noexcept
    {
        if (bytes == nullptr || byteCount < sizeof(std::uint32_t)) return false;
        std::uint32_t magic = 0;
        std::memcpy(&magic, bytes, sizeof(magic));
        return magic == kMagic;
    }

    SpirvRemapResultEXT RemapSpirvForSdlGpuEXT(const void* bytes, const std::size_t byteCount)
    {
        SpirvRemapResultEXT result;
        if (bytes == nullptr || byteCount == 0)
        {
            result.error = "empty SPIR-V payload";
            return result;
        }
        if (byteCount % sizeof(std::uint32_t) != 0)
        {
            result.error = "SPIR-V payload is not a whole number of 32-bit words";
            return result;
        }
        std::vector<std::uint32_t> words(byteCount / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes, byteCount);
        return RemapSpirvForSdlGpuEXT(words.data(), words.size());
    }

    SpirvRemapResultEXT RemapSpirvForSdlGpuEXT(
        const std::uint32_t* wordData, const std::size_t wordCount)
    {
        SpirvRemapResultEXT result;
        if (wordData == nullptr || wordCount < 5 || wordData[0] != kMagic)
        {
            result.error = "not a SPIR-V module";
            return result;
        }

        const std::vector<std::uint32_t> input(wordData, wordData + wordCount);
        result.words = input;

        std::vector<Instruction> instructions;
        for (std::size_t i = 5; i < input.size();)
        {
            const std::uint32_t length = input[i] >> 16;
            if (length == 0 || i + length > input.size())
            {
                result.error = "truncated SPIR-V instruction stream";
                return result;
            }
            Instruction inst;
            inst.opcode = input[i] & 0xFFFFu;
            inst.words.assign(input.begin() + static_cast<std::ptrdiff_t>(i),
                              input.begin() + static_cast<std::ptrdiff_t>(i + length));
            instructions.push_back(std::move(inst));
            i += length;
        }

        // ---- pass 1: the type graph, the decorations and the entry point -------------------
        std::map<std::uint32_t, std::uint32_t> pointerStorage;   // %ptr  -> storage class
        std::map<std::uint32_t, std::uint32_t> pointerPointee;   // %ptr  -> pointee type
        std::map<std::uint32_t, std::uint32_t> arrayElement;     // %arr  -> element type
        std::map<std::uint32_t, std::uint32_t> arrayLengthId;    // %arr  -> length constant id
        std::map<std::uint32_t, std::uint32_t> constantValue;    // %const-> literal
        std::map<std::uint32_t, std::uint32_t> imageSampled;     // %img  -> Sampled operand
        std::map<std::uint32_t, std::uint32_t> imageSampledType; // %img  -> sampled type id
        std::set<std::uint32_t> integerScalarTypes;
        std::map<std::uint32_t, std::uint32_t> sampledImageUnderlying; // %sampledImg -> %img
        std::set<std::uint32_t> sampledImageTypes;
        std::set<std::uint32_t> samplerTypes;
        std::set<std::uint32_t> structTypes;
        std::set<std::uint32_t> blockStructs;
        std::set<std::uint32_t> bufferBlockStructs;
        std::set<std::uint32_t> nonWritableTargets;
        // %struct -> how many members carry NonWritable, and how many members were decorated at all
        std::map<std::uint32_t, std::set<std::uint32_t>> nonWritableMembers;
        std::map<std::uint32_t, std::set<std::uint32_t>> decoratedMembers;
        std::map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>> memberOffsets;
        std::map<std::uint32_t, std::map<std::uint32_t, std::string>> memberNames;
        std::map<std::uint32_t, std::uint32_t> variableType;     // %var  -> result type (%ptr)
        std::map<std::uint32_t, std::uint32_t> variableStorage;  // %var  -> storage class
        std::vector<std::uint32_t> variableOrder;
        std::map<std::uint32_t, std::uint32_t> declaredSet;
        std::map<std::uint32_t, std::uint32_t> declaredBinding;
        std::map<std::uint32_t, std::uint32_t> declaredLocation;

        for (const Instruction& inst : instructions)
        {
            const auto& w = inst.words;
            switch (inst.opcode)
            {
                case kOpEntryPoint:
                    if (w.size() >= 3 && result.stage == SpirvStageEXT::Unknown)
                    {
                        if (w[1] == kExecutionModelVertex) result.stage = SpirvStageEXT::Vertex;
                        else if (w[1] == kExecutionModelFragment) result.stage = SpirvStageEXT::Fragment;
                        else if (w[1] == kExecutionModelGLCompute) result.stage = SpirvStageEXT::Compute;
                    }
                    break;
                case kOpExecutionMode:
                    if (w.size() >= 6 && w[2] == kExecutionModeLocalSize)
                    {
                        result.localSizeX = w[3];
                        result.localSizeY = w[4];
                        result.localSizeZ = w[5];
                    }
                    break;
                case kOpTypeImage:
                    if (w.size() >= 8)
                    {
                        imageSampled[w[1]] = w[7];
                        imageSampledType[w[1]] = w[2];
                    }
                    break;
                case kOpTypeInt:
                    if (w.size() >= 2) integerScalarTypes.insert(w[1]);
                    break;
                case kOpTypeSampler:
                    if (w.size() >= 2) samplerTypes.insert(w[1]);
                    break;
                case kOpTypeSampledImage:
                    if (w.size() >= 3)
                    {
                        sampledImageTypes.insert(w[1]);
                        sampledImageUnderlying[w[1]] = w[2];
                    }
                    break;
                case kOpTypeArray:
                    if (w.size() >= 4)
                    {
                        arrayElement[w[1]] = w[2];
                        arrayLengthId[w[1]] = w[3];
                    }
                    break;
                case kOpTypeRuntimeArray:
                    if (w.size() >= 3) arrayElement[w[1]] = w[2];
                    break;
                case kOpTypeStruct:
                    if (w.size() >= 2) structTypes.insert(w[1]);
                    break;
                case kOpTypePointer:
                    if (w.size() >= 4)
                    {
                        pointerStorage[w[1]] = w[2];
                        pointerPointee[w[1]] = w[3];
                    }
                    break;
                case kOpConstant:
                    if (w.size() >= 4) constantValue[w[2]] = w[3];
                    break;
                case kOpVariable:
                    if (w.size() >= 4)
                    {
                        variableType[w[2]] = w[1];
                        variableStorage[w[2]] = w[3];
                        variableOrder.push_back(w[2]);
                    }
                    break;
                case kOpDecorate:
                    if (w.size() >= 3)
                    {
                        if (w[2] == kDecorationBlock) blockStructs.insert(w[1]);
                        else if (w[2] == kDecorationBufferBlock) bufferBlockStructs.insert(w[1]);
                        else if (w[2] == kDecorationNonWritable) nonWritableTargets.insert(w[1]);
                        else if (w.size() >= 4 && w[2] == kDecorationDescriptorSet) declaredSet[w[1]] = w[3];
                        else if (w.size() >= 4 && w[2] == kDecorationBinding) declaredBinding[w[1]] = w[3];
                        else if (w.size() >= 4 && w[2] == kDecorationLocation) declaredLocation[w[1]] = w[3];
                    }
                    break;
                case kOpMemberDecorate:
                    if (w.size() >= 4)
                    {
                        decoratedMembers[w[1]].insert(w[2]);
                        if (w[3] == kDecorationNonWritable) nonWritableMembers[w[1]].insert(w[2]);
                        if (w.size() >= 5 && w[3] == kDecorationOffset)
                            memberOffsets[w[1]][w[2]] = w[4];
                    }
                    break;
                case kOpMemberName:
                    if (w.size() >= 4)
                    {
                        std::string name;
                        for (std::size_t word = 3; word < w.size(); ++word)
                            for (int byte = 0; byte < 4; ++byte)
                            {
                                const char c = static_cast<char>((w[word] >> (byte * 8)) & 0xFFu);
                                if (c == '\0') { word = w.size(); break; }
                                name.push_back(c);
                            }
                        if (!name.empty()) memberNames[w[1]][w[2]] = std::move(name);
                    }
                    break;
                default: break;
            }
        }

        if (result.stage == SpirvStageEXT::Unknown)
        {
            result.error = "SPIR-V module declares no vertex, fragment or compute entry point";
            return result;
        }

        // Vertex inputs, for a pipeline that has to match whatever layout the shader asked for.
        if (result.stage == SpirvStageEXT::Vertex)
        {
            for (const std::uint32_t variable : variableOrder)
            {
                if (variableStorage[variable] != kStorageClassInput) continue;
                const auto location = declaredLocation.find(variable);
                if (location == declaredLocation.end()) continue;
                result.vertexInputLocations.push_back(location->second);
            }
            std::sort(result.vertexInputLocations.begin(), result.vertexInputLocations.end());
            result.vertexInputLocations.erase(
                std::unique(result.vertexInputLocations.begin(),
                            result.vertexInputLocations.end()),
                result.vertexInputLocations.end());
        }

        // ---- pass 2: classify every descriptor-bound variable ------------------------------
        struct Entry
        {
            std::uint32_t variable = 0;
            Classified classified;
            bool fromPushConstant = false;
            bool integerSampled = false;
            std::uint32_t originalSet = 0;
            std::uint32_t originalBinding = 0;
        };
        std::vector<Entry> entries;

        for (const std::uint32_t variable : variableOrder)
        {
            const std::uint32_t storage = variableStorage[variable];
            if (storage != kStorageClassUniformConstant && storage != kStorageClassUniform
                && storage != kStorageClassStorageBuffer && storage != kStorageClassPushConstant)
                continue;

            const auto pointerIt = pointerPointee.find(variableType[variable]);
            if (pointerIt == pointerPointee.end())
            {
                result.error = "SPIR-V variable has no resolvable pointer type";
                return result;
            }

            // Walk through array wrappers; an array of descriptors occupies that many slots.
            std::uint32_t base = pointerIt->second;
            std::uint32_t arrayLength = 1;
            while (arrayElement.count(base) != 0)
            {
                const auto lengthIt = arrayLengthId.find(base);
                std::uint32_t thisLength = 1;
                if (lengthIt != arrayLengthId.end())
                {
                    const auto value = constantValue.find(lengthIt->second);
                    thisLength = value != constantValue.end() ? value->second : 1u;
                }
                // A struct wrapped in an array is one buffer of N elements, not N descriptors;
                // only a descriptor array multiplies the slot count.
                if (structTypes.count(arrayElement[base]) != 0) { base = arrayElement[base]; break; }
                arrayLength *= std::max<std::uint32_t>(1u, thisLength);
                base = arrayElement[base];
            }

            Entry entry;
            entry.variable = variable;
            entry.classified.arrayLength = arrayLength;
            entry.fromPushConstant = storage == kStorageClassPushConstant;

            if (storage == kStorageClassPushConstant)
            {
                if (structTypes.count(base) == 0)
                {
                    result.error = "SPIR-V push-constant block is not a struct";
                    return result;
                }
                entry.classified.kind = SpirvResourceKindEXT::UniformBuffer;
                entry.classified.readOnly = true;
            }
            else if (storage == kStorageClassUniformConstant)
            {
                if (sampledImageTypes.count(base) != 0)
                {
                    entry.classified.kind = SpirvResourceKindEXT::SampledTexture;
                    const auto image = sampledImageUnderlying.find(base);
                    if (image != sampledImageUnderlying.end())
                    {
                        const auto texel = imageSampledType.find(image->second);
                        entry.integerSampled = texel != imageSampledType.end()
                            && integerScalarTypes.count(texel->second) != 0;
                    }
                }
                else if (imageSampled.count(base) != 0)
                {
                    // Sampled == 1 is "used with a sampler"; 2 is a storage image.
                    entry.classified.kind = imageSampled[base] == 1u
                        ? SpirvResourceKindEXT::SampledTexture
                        : SpirvResourceKindEXT::StorageTexture;
                    entry.classified.readOnly = nonWritableTargets.count(variable) != 0;
                }
                else if (samplerTypes.count(base) != 0)
                {
                    result.error =
                        "SPIR-V module declares a separate OpTypeSampler; SDL_gpu binds combined "
                        "image/samplers only";
                    return result;
                }
                else
                {
                    result.error =
                        "SPIR-V module declares a non-opaque UniformConstant variable, which has "
                        "no SDL_gpu descriptor to live in";
                    return result;
                }
            }
            else if (storage == kStorageClassStorageBuffer
                     || (storage == kStorageClassUniform && bufferBlockStructs.count(base) != 0))
            {
                entry.classified.kind = SpirvResourceKindEXT::StorageBuffer;
                const bool wholeVariableReadOnly = nonWritableTargets.count(variable) != 0;
                const auto members = nonWritableMembers.find(base);
                const auto decorated = decoratedMembers.find(base);
                const bool everyMemberReadOnly =
                    members != nonWritableMembers.end() && decorated != decoratedMembers.end()
                    && !members->second.empty()
                    && members->second.size() == decorated->second.size();
                entry.classified.readOnly = wholeVariableReadOnly || everyMemberReadOnly;
            }
            else if (storage == kStorageClassUniform && blockStructs.count(base) != 0)
            {
                entry.classified.kind = SpirvResourceKindEXT::UniformBuffer;
            }
            else
            {
                result.error =
                    "SPIR-V module declares a Uniform variable that is neither a Block nor a "
                    "BufferBlock";
                return result;
            }

            const auto setIt = declaredSet.find(variable);
            const auto bindingIt = declaredBinding.find(variable);
            entry.originalSet = setIt != declaredSet.end() ? setIt->second : 0u;
            entry.originalBinding = bindingIt != declaredBinding.end() ? bindingIt->second : 0u;
            entries.push_back(entry);
        }

        // ---- pass 3: assign SDL_gpu sets and slots -----------------------------------------
        // Within each category the order is (original set, original binding), so the mapping is a
        // deterministic function of the source module and not of instruction order.
        const bool compute = result.stage == SpirvStageEXT::Compute;

        auto collect = [&entries](const SpirvResourceKindEXT kind, const int readOnly) {
            std::vector<std::size_t> picked;
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].classified.kind != kind) continue;
                if (readOnly >= 0 && entries[i].classified.readOnly != (readOnly != 0)) continue;
                picked.push_back(i);
            }
            std::stable_sort(picked.begin(), picked.end(), [&entries](std::size_t a, std::size_t b) {
                if (entries[a].originalSet != entries[b].originalSet)
                    return entries[a].originalSet < entries[b].originalSet;
                return entries[a].originalBinding < entries[b].originalBinding;
            });
            return picked;
        };

        std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> assignment;  // %var -> set,binding
        std::map<std::uint32_t, std::uint32_t> slotOf;

        // One resource set is filled by appending categories in SDL's documented order, each
        // category's slot numbering restarting at zero because that is what a binding call indexes.
        auto fill = [&](const std::uint32_t set, const std::vector<std::vector<std::size_t>>& groups) {
            std::uint32_t binding = 0;
            for (const auto& group : groups)
            {
                std::uint32_t slot = 0;
                for (const std::size_t index : group)
                {
                    assignment[entries[index].variable] = {set, binding};
                    slotOf[entries[index].variable] = slot;
                    binding += entries[index].classified.arrayLength;
                    slot += entries[index].classified.arrayLength;
                }
            }
        };

        const std::vector<std::size_t> sampled = collect(SpirvResourceKindEXT::SampledTexture, -1);
        const std::vector<std::size_t> uniforms = collect(SpirvResourceKindEXT::UniformBuffer, -1);

        auto countSlots = [&entries](const std::vector<std::size_t>& group) {
            std::uint32_t total = 0;
            for (const std::size_t index : group) total += entries[index].classified.arrayLength;
            return total;
        };

        if (compute)
        {
            const std::vector<std::size_t> roTextures = collect(SpirvResourceKindEXT::StorageTexture, 1);
            const std::vector<std::size_t> rwTextures = collect(SpirvResourceKindEXT::StorageTexture, 0);
            const std::vector<std::size_t> roBuffers = collect(SpirvResourceKindEXT::StorageBuffer, 1);
            const std::vector<std::size_t> rwBuffers = collect(SpirvResourceKindEXT::StorageBuffer, 0);
            fill(0, {sampled, roTextures, roBuffers});
            fill(1, {rwTextures, rwBuffers});
            fill(2, {uniforms});
            result.samplerCount = countSlots(sampled);
            result.readOnlyStorageTextureCount = countSlots(roTextures);
            result.readWriteStorageTextureCount = countSlots(rwTextures);
            result.readOnlyStorageBufferCount = countSlots(roBuffers);
            result.readWriteStorageBufferCount = countSlots(rwBuffers);
        }
        else
        {
            const std::vector<std::size_t> textures = collect(SpirvResourceKindEXT::StorageTexture, -1);
            const std::vector<std::size_t> buffers = collect(SpirvResourceKindEXT::StorageBuffer, -1);
            const std::uint32_t resourceSet = result.stage == SpirvStageEXT::Vertex ? 0u : 2u;
            fill(resourceSet, {sampled, textures, buffers});
            fill(resourceSet + 1u, {uniforms});
            result.samplerCount = countSlots(sampled);
            result.readOnlyStorageTextureCount = countSlots(textures);
            result.readOnlyStorageBufferCount = countSlots(buffers);
        }
        result.uniformBufferCount = countSlots(uniforms);

        // ---- pass 4: rewrite ---------------------------------------------------------------
        std::uint32_t input3Bound = 0;
        std::set<std::uint32_t> pushConstantPointers;
        for (const auto& [pointer, storage] : pointerStorage)
            if (storage == kStorageClassPushConstant) pushConstantPointers.insert(pointer);

        const bool convertsPushConstants = !pushConstantPointers.empty();

        std::vector<Instruction> rewritten;
        rewritten.reserve(instructions.size() + entries.size() * 2);
        std::set<std::uint32_t> needsSetDecoration;
        std::set<std::uint32_t> needsBindingDecoration;
        for (const Entry& entry : entries)
        {
            if (declaredSet.count(entry.variable) == 0) needsSetDecoration.insert(entry.variable);
            if (declaredBinding.count(entry.variable) == 0)
                needsBindingDecoration.insert(entry.variable);
        }

        // Insert new decorations immediately BEFORE the first type/constant/variable instruction,
        // which is where the annotation section ends.
        std::size_t insertBefore = instructions.size();
        for (std::size_t i = 0; i < instructions.size(); ++i)
        {
            if (OpensTypeSection(instructions[i].opcode)) { insertBefore = i; break; }
        }

        for (std::size_t i = 0; i < instructions.size(); ++i)
        {
            Instruction inst = instructions[i];
            auto& w = inst.words;
            switch (inst.opcode)
            {
                case kOpDecorate:
                    if (w.size() >= 4 && assignment.count(w[1]) != 0)
                    {
                        if (w[2] == kDecorationDescriptorSet)
                        {
                            if (w[3] != assignment[w[1]].first) result.changed = true;
                            w[3] = assignment[w[1]].first;
                        }
                        else if (w[2] == kDecorationBinding)
                        {
                            if (w[3] != assignment[w[1]].second) result.changed = true;
                            w[3] = assignment[w[1]].second;
                        }
                    }
                    break;
                case kOpTypePointer:
                    if (w.size() >= 4 && w[2] == kStorageClassPushConstant)
                    {
                        w[2] = kStorageClassUniform;
                        result.changed = true;
                    }
                    break;
                case kOpVariable:
                    if (w.size() >= 4 && w[3] == kStorageClassPushConstant)
                    {
                        w[3] = kStorageClassUniform;
                        result.changed = true;
                    }
                    break;
                default: break;
            }
            if (i == insertBefore)
            {
                for (const Entry& entry : entries)
                {
                    if (needsSetDecoration.count(entry.variable) != 0)
                    {
                        rewritten.push_back(MakeDecorate(
                            entry.variable, kDecorationDescriptorSet,
                            assignment[entry.variable].first));
                        result.changed = true;
                    }
                    if (needsBindingDecoration.count(entry.variable) != 0)
                    {
                        rewritten.push_back(MakeDecorate(
                            entry.variable, kDecorationBinding, assignment[entry.variable].second));
                        result.changed = true;
                    }
                }
            }
            rewritten.push_back(std::move(inst));
        }

        // A converted push block must carry Block, which glslang already emits for it; a module
        // that somehow does not is refused rather than silently producing an invalid descriptor.
        if (convertsPushConstants)
        {
            for (const Entry& entry : entries)
            {
                if (!entry.fromPushConstant) continue;
                const std::uint32_t pointer = variableType[entry.variable];
                const std::uint32_t block = pointerPointee[pointer];
                if (blockStructs.count(block) == 0)
                {
                    result.error =
                        "SPIR-V push-constant block carries no Block decoration, so it cannot be "
                        "reinterpreted as an SDL_gpu uniform buffer";
                    return result;
                }
            }
        }

        // ---- pass 5: cancel SDL_gpu's clip-space Y flip, for a vertex module ----------------
        //
        // SDL's own Vulkan backend negates the viewport height it is given
        // (its Vulkan backend's viewport setter: `currentViewport.y = viewport->y + viewport->h;
        // `currentViewport.height = -viewport->h;`), so clip space on SDL_gpu is Y-up. CNA's
        // portable shaders are written to CNA's Vulkan renderer's convention, which is Y-down --
        // that renderer passes a POSITIVE height precisely because its shaders emit NDC directly.
        // A portable module therefore renders vertically mirrored here.
        //
        // This renderer's own stock sprite shader has always carried the matching negation by hand
        // (`sprite2d.vert.glsl`: `gl_Position = vec4(ndc.x, -ndc.y, ...)`). A portable module
        // cannot: it is shared with the renderers that must NOT negate. So the same negation is
        // applied to the module here, which keeps one convention across every renderer and leaves
        // the shared sources correct for all of them.
        if (result.stage == SpirvStageEXT::Vertex)
        {
            // glslang writes gl_Position as member 0 of the gl_PerVertex block, reached by an
            // OpAccessChain, not as a variable of its own -- so the BuiltIn decoration to look for
            // is an OpMemberDecorate. (A module that decorates a bare variable is handled too.)
            std::uint32_t positionStruct = 0;
            std::uint32_t positionVariable = 0;
            for (const Instruction& inst : rewritten)
            {
                const auto& w = inst.words;
                if (inst.opcode == kOpMemberDecorate && w.size() >= 5
                    && w[3] == kDecorationBuiltIn && w[4] == kBuiltInPosition && w[2] == 0)
                    positionStruct = w[1];
                else if (inst.opcode == kOpDecorate && w.size() >= 4
                         && w[2] == kDecorationBuiltIn && w[3] == kBuiltInPosition)
                    positionVariable = w[1];
            }
            if (positionStruct != 0)
            {
                for (const std::uint32_t variable : variableOrder)
                {
                    if (variableStorage[variable] != kStorageClassOutput) continue;
                    const auto pointer = variableType.find(variable);
                    if (pointer == variableType.end()) continue;
                    const auto pointee = pointerPointee.find(pointer->second);
                    if (pointee == pointerPointee.end() || pointee->second != positionStruct)
                        continue;
                    positionVariable = variable;
                    break;
                }
            }

            // The negation is applied ONCE, at the end of the entry point, by reading gl_Position
            // back and storing the product -- not per store. A shader may write it more than once,
            // and CNA's 3D portable programs deliberately write a component last
            // (`rigid.vulkan.vert.glsl`: `gl_Position.y = -gl_Position.y;`, the Vulkan renderer's
            // own convention for a 3D program). Negating each stored value would miss that one and
            // mis-order the rest; negating the final value is right whatever shape wrote it, and
            // leaves the net result exactly one flip away from what Vulkan shows, which is the
            // whole correction (see the block comment above).
            std::uint32_t vectorPointerType = 0;
            std::uint32_t vectorType = 0;
            std::uint32_t zeroIndex = 0;
            if (positionVariable != 0 && positionStruct == 0)
            {
                const auto pointer = variableType.find(positionVariable);
                if (pointer != variableType.end())
                {
                    vectorPointerType = pointer->second;
                    if (pointerPointee.count(vectorPointerType) != 0)
                        vectorType = pointerPointee[vectorPointerType];
                }
            }
            else if (positionVariable != 0)
            {
                for (const Instruction& inst : rewritten)
                {
                    const auto& w = inst.words;
                    // Exactly one index: a longer chain names a COMPONENT of gl_Position, whose
                    // pointer is a float and would be the wrong type to load the vector through.
                    if (inst.opcode != kOpAccessChain || w.size() != 5) continue;
                    if (w[3] != positionVariable) continue;
                    const auto index = constantValue.find(w[4]);
                    if (index == constantValue.end() || index->second != 0) continue;
                    vectorPointerType = w[1];
                    zeroIndex = w[4];
                    const auto pointee = pointerPointee.find(vectorPointerType);
                    if (pointee != pointerPointee.end()) vectorType = pointee->second;
                    break;
                }
            }

            std::uint32_t floatType = 0;
            if (vectorType != 0)
                for (const Instruction& inst : rewritten)
                    if (inst.opcode == kOpTypeVector && inst.words.size() >= 4
                        && inst.words[1] == vectorType)
                        floatType = inst.words[2];

            const bool canFlip = positionVariable != 0 && vectorPointerType != 0
                              && vectorType != 0 && floatType != 0
                              && (positionStruct == 0 || zeroIndex != 0);
            if (positionVariable != 0 && !canFlip)
            {
                result.error =
                    "SPIR-V vertex module writes gl_Position in a shape this renderer cannot apply "
                    "its clip-space Y correction to";
                return result;
            }

            if (canFlip)
            {
                std::uint32_t entryFunction = 0;
                for (const Instruction& inst : rewritten)
                    if (inst.opcode == kOpEntryPoint && inst.words.size() >= 3)
                    {
                        entryFunction = inst.words[2];
                        break;
                    }

                std::uint32_t bound = input[3];
                const std::uint32_t one = bound++;
                const std::uint32_t minusOne = bound++;
                const std::uint32_t flip = bound++;
                const float positive = 1.0f;
                const float negative = -1.0f;
                std::uint32_t positiveBits = 0;
                std::uint32_t negativeBits = 0;
                std::memcpy(&positiveBits, &positive, sizeof(positiveBits));
                std::memcpy(&negativeBits, &negative, sizeof(negativeBits));

                std::vector<Instruction> flipped;
                flipped.reserve(rewritten.size() + 8);
                bool constantsEmitted = false;
                bool insideEntry = false;
                for (const Instruction& inst : rewritten)
                {
                    if (!constantsEmitted && inst.opcode == kOpTypeVector
                        && inst.words.size() >= 2 && inst.words[1] == vectorType)
                    {
                        flipped.push_back(inst);
                        flipped.push_back(
                            MakeInstruction(kOpConstant, {floatType, one, positiveBits}));
                        flipped.push_back(
                            MakeInstruction(kOpConstant, {floatType, minusOne, negativeBits}));
                        flipped.push_back(MakeInstruction(
                            kOpConstantComposite, {vectorType, flip, one, minusOne, one, one}));
                        constantsEmitted = true;
                        continue;
                    }
                    if (inst.opcode == kOpFunction && inst.words.size() >= 3)
                        insideEntry = inst.words[2] == entryFunction;
                    if (insideEntry && inst.opcode == kOpReturn)
                    {
                        std::uint32_t pointer = positionVariable;
                        if (positionStruct != 0)
                        {
                            pointer = bound++;
                            flipped.push_back(MakeInstruction(
                                kOpAccessChain,
                                {vectorPointerType, pointer, positionVariable, zeroIndex}));
                        }
                        const std::uint32_t loaded = bound++;
                        const std::uint32_t product = bound++;
                        flipped.push_back(
                            MakeInstruction(kOpLoad, {vectorType, loaded, pointer}));
                        flipped.push_back(
                            MakeInstruction(kOpFMul, {vectorType, product, loaded, flip}));
                        flipped.push_back(MakeInstruction(kOpStore, {pointer, product}));
                        result.changed = true;
                    }
                    flipped.push_back(inst);
                }

                if (!constantsEmitted)
                {
                    result.error =
                        "SPIR-V vertex module declares no gl_Position vector type to build its "
                        "clip-space Y correction from";
                    return result;
                }
                rewritten = std::move(flipped);
                input3Bound = bound;
            }
        }

        std::vector<std::uint32_t> output(input.begin(), input.begin() + 5);
        if (input3Bound != 0) output[3] = input3Bound;
        for (const Instruction& inst : rewritten)
            output.insert(output.end(), inst.words.begin(), inst.words.end());
        result.words = std::move(output);

        // The first uniform block's named members, for name-addressed scalar uniforms. The block's
        // byte size is its last member's offset plus that member's size, which is not knowable from
        // the offsets alone -- so it is the last offset rounded up to sixteen, which is what std140
        // does to the block as a whole and is never smaller than the block really is.
        for (const Entry& entry : entries)
        {
            if (entry.classified.kind != SpirvResourceKindEXT::UniformBuffer) continue;
            const std::uint32_t block = pointerPointee[variableType[entry.variable]];
            const auto offsets = memberOffsets.find(block);
            if (offsets == memberOffsets.end()) break;
            const auto names = memberNames.find(block);
            std::uint32_t highest = 0;
            for (const auto& [member, byteOffset] : offsets->second)
            {
                highest = std::max(highest, byteOffset);
                if (names == memberNames.end()) continue;
                const auto named = names->second.find(member);
                if (named == names->second.end()) continue;
                result.uniformMembers.push_back(
                    SpirvUniformMemberEXT{named->second, byteOffset});
            }
            result.uniformBlockBytes = ((highest + 16u) + 15u) & ~15u;
            break;
        }

        result.resources.reserve(entries.size());
        for (const Entry& entry : entries)
        {
            SpirvResourceBindingEXT binding;
            binding.kind = entry.classified.kind;
            binding.originalSet = entry.originalSet;
            binding.originalBinding = entry.originalBinding;
            binding.assignedSet = assignment[entry.variable].first;
            binding.assignedBinding = assignment[entry.variable].second;
            binding.slot = slotOf[entry.variable];
            binding.arrayLength = entry.classified.arrayLength;
            binding.readOnly = entry.classified.readOnly;
            binding.convertedFromPushConstant = entry.fromPushConstant;
            binding.integerSampled = entry.integerSampled;
            result.resources.push_back(binding);
        }

        // SDL_gpu allows at most one uniform buffer slot per stage in the conventions CNA uses; a
        // module wanting more is not wrong, but the runtime below has nowhere to route the extras,
        // so it is named here rather than mis-bound at draw time.
        if (result.uniformBufferCount > 4u)
        {
            std::ostringstream message;
            message << "SPIR-V module declares " << result.uniformBufferCount
                    << " uniform buffers; SDL_gpu exposes four per stage";
            result.error = message.str();
            return result;
        }

        return result;
    }
}
