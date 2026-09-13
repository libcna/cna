// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-166: split SPIR-V combined image samplers into the separate image and
// sampler pair WebGPU's shading model requires.
//
// MojoShader's "spirv" (Vulkan-mode) profile emits one `OpTypeSampledImage` global per D3D9
// sampler register -- Vulkan's VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER shape. WGSL has no such
// type: a texture binding and a sampler binding are always two separate resources, and naga's
// SPIR-V frontend rejects a load of a combined global with `invalid id %N`. Everything else
// MojoShader emits passes naga untouched, so the whole gap between the two is this one construct.
//
// The rewrite is bounded because MojoShader's emission pattern is fixed:
//
//     %si   = OpTypeSampledImage %img
//     %ptr  = OpTypePointer UniformConstant %si
//     %var  = OpVariable %ptr UniformConstant          ; DescriptorSet S, Binding B
//     ...
//     %tmp  = OpLoad %si %var
//     %res  = OpImageSample*Lod %vec4 %tmp %coord
//
// becomes
//
//     %si       = OpTypeSampledImage %img              ; kept: OpSampledImage still yields it
//     %smpT     = OpTypeSampler
//     %imgPtr   = OpTypePointer UniformConstant %img
//     %smpPtr   = OpTypePointer UniformConstant %smpT
//     %imgVar   = OpVariable %imgPtr UniformConstant   ; DescriptorSet S, Binding 2*B
//     %smpVar   = OpVariable %smpPtr UniformConstant   ; DescriptorSet S, Binding 2*B + 1
//     ...
//     %ti       = OpLoad %img %imgVar
//     %ts       = OpLoad %smpT %smpVar
//     %tmp      = OpSampledImage %si %ti %ts           ; SAME result id, so no use site moves
//
// Keeping the original `OpLoad`'s result id is what makes this a local edit: every downstream
// instruction that consumed the combined value still consumes it, and no id renumbering is needed.
//
// The doubled binding numbers are this file's own convention, not MojoShader's: WebGPU needs two
// binding slots where Vulkan needed one, and a renderer that builds its own bind-group layout is
// free to choose them as long as it chooses the same ones the shader was rewritten with. That is
// what SplitResult::samplers reports back.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace CnaSpirv
{
    /** @brief One D3D9 sampler register after the split, and the two bindings it now occupies. */
    struct SplitSamplerBinding
    {
        /** @brief Descriptor set the combined global carried (MojoShader: 0 vertex, 2 pixel). */
        std::uint32_t set = 0;
        /** @brief Binding the combined global carried; equal to the D3D9 sampler register. */
        std::uint32_t originalBinding = 0;
        /** @brief Binding the texture half now occupies. */
        std::uint32_t textureBinding = 0;
        /** @brief Binding the sampler half now occupies. */
        std::uint32_t samplerBinding = 0;
        /** @brief SPIR-V `Dim` of the image (1D=0, 2D=1, 3D=2, Cube=3). */
        std::uint32_t dim = 0;
        /** @brief Name MojoShader gave the combined global, e.g. "ps_s0". */
        std::string name;
    };

    /** @brief Rewritten module plus the binding map a bind-group layout must be built from. */
    struct SplitResult
    {
        std::vector<std::uint32_t> words;
        std::vector<SplitSamplerBinding> samplers;
        bool changed = false;
        std::string error;
    };

    namespace detail
    {
        constexpr std::uint32_t kMagic = 0x07230203u;
        constexpr std::uint32_t kOpName = 5;
        constexpr std::uint32_t kOpTypeImage = 25;
        constexpr std::uint32_t kOpTypeSampler = 26;
        constexpr std::uint32_t kOpTypeSampledImage = 27;
        constexpr std::uint32_t kOpTypePointer = 32;
        constexpr std::uint32_t kOpVariable = 59;
        constexpr std::uint32_t kOpLoad = 61;
        constexpr std::uint32_t kOpDecorate = 71;
        constexpr std::uint32_t kOpSampledImage = 86;
        constexpr std::uint32_t kStorageClassUniformConstant = 0;
        constexpr std::uint32_t kDecorationBinding = 33;
        constexpr std::uint32_t kDecorationDescriptorSet = 34;

        struct Instruction
        {
            std::uint32_t opcode = 0;
            std::vector<std::uint32_t> words;  ///< Full instruction including word 0.
        };

        inline std::string ReadLiteralString(const std::vector<std::uint32_t>& words,
                                             std::size_t first)
        {
            std::string out;
            for (std::size_t i = first; i < words.size(); ++i)
            {
                const std::uint32_t w = words[i];
                for (int b = 0; b < 4; ++b)
                {
                    const char c = static_cast<char>((w >> (8 * b)) & 0xFFu);
                    if (c == '\0') return out;
                    out.push_back(c);
                }
            }
            return out;
        }

        inline void PushString(std::vector<std::uint32_t>& words, const std::string& text)
        {
            std::uint32_t packed = 0;
            int byteIndex = 0;
            for (const char c : text)
            {
                packed |= static_cast<std::uint32_t>(static_cast<unsigned char>(c))
                          << (8 * byteIndex);
                if (++byteIndex == 4)
                {
                    words.push_back(packed);
                    packed = 0;
                    byteIndex = 0;
                }
            }
            words.push_back(packed);  // terminating NUL (and padding) always needs its own word
        }

        inline Instruction Make(std::uint32_t opcode, std::vector<std::uint32_t> operands)
        {
            Instruction inst;
            inst.opcode = opcode;
            inst.words.push_back(((static_cast<std::uint32_t>(operands.size()) + 1u) << 16) |
                                 opcode);
            for (const std::uint32_t w : operands) inst.words.push_back(w);
            return inst;
        }
    }

    /**
     * @brief Rewrites every combined image sampler in a SPIR-V module into an image/sampler pair.
     *
     * @param input SPIR-V words as MojoShader emitted them, patch table already trimmed off.
     * @return The rewritten module, the resulting bindings, and whether anything changed. On a
     *         malformed module `error` is set and `words` is returned unchanged.
     */
    [[nodiscard]] inline SplitResult SplitCombinedImageSamplers(
        const std::vector<std::uint32_t>& input)
    {
        using namespace detail;

        SplitResult result;
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

        // Pass 1 -- learn the type graph and find the combined globals.
        std::map<std::uint32_t, std::uint32_t> sampledImageToImage;   // %si -> %img
        std::map<std::uint32_t, std::uint32_t> imageDim;              // %img -> Dim
        std::map<std::uint32_t, std::uint32_t> pointerToSampledImage; // %ptr -> %si
        std::map<std::uint32_t, std::uint32_t> combinedVariables;     // %var -> %si
        std::map<std::uint32_t, std::string> names;
        std::uint32_t existingSamplerType = 0;
        std::map<std::uint32_t, std::uint32_t> existingImagePointer;  // %img -> %ptr

        for (const Instruction& inst : instructions)
        {
            switch (inst.opcode)
            {
                case kOpTypeImage:
                    if (inst.words.size() >= 4) imageDim[inst.words[1]] = inst.words[3];
                    break;
                case kOpTypeSampler:
                    if (inst.words.size() >= 2) existingSamplerType = inst.words[1];
                    break;
                case kOpTypeSampledImage:
                    if (inst.words.size() >= 3) sampledImageToImage[inst.words[1]] = inst.words[2];
                    break;
                case kOpTypePointer:
                    if (inst.words.size() >= 4 && inst.words[2] == kStorageClassUniformConstant)
                    {
                        pointerToSampledImage[inst.words[1]] = inst.words[3];
                        existingImagePointer[inst.words[3]] = inst.words[1];
                    }
                    break;
                case kOpName:
                    if (inst.words.size() >= 3) names[inst.words[1]] = ReadLiteralString(inst.words, 2);
                    break;
                default: break;
            }
        }
        for (const Instruction& inst : instructions)
        {
            if (inst.opcode != kOpVariable || inst.words.size() < 4) continue;
            if (inst.words[3] != kStorageClassUniformConstant) continue;
            const auto pointee = pointerToSampledImage.find(inst.words[1]);
            if (pointee == pointerToSampledImage.end()) continue;
            if (sampledImageToImage.count(pointee->second) == 0) continue;
            combinedVariables[inst.words[2]] = pointee->second;
        }

        if (combinedVariables.empty()) return result;  // nothing to do; words already == input

        // Pass 2 -- allocate ids for the halves.
        std::uint32_t bound = input[3];
        const auto NewId = [&bound]() { return bound++; };

        std::uint32_t samplerTypeId = existingSamplerType;
        if (samplerTypeId == 0) samplerTypeId = NewId();
        std::uint32_t samplerPointerId = 0;
        std::map<std::uint32_t, std::uint32_t> imagePointerId;  // %img -> %ptr

        struct Half
        {
            std::uint32_t imageVar = 0;
            std::uint32_t samplerVar = 0;
            std::uint32_t imageType = 0;
        };
        std::map<std::uint32_t, Half> halves;
        for (const auto& [var, sampledImage] : combinedVariables)
        {
            Half half;
            half.imageType = sampledImageToImage[sampledImage];
            half.imageVar = NewId();
            half.samplerVar = NewId();
            halves[var] = half;
            if (imagePointerId.count(half.imageType) == 0)
            {
                const auto existing = existingImagePointer.find(half.imageType);
                imagePointerId[half.imageType] =
                    existing != existingImagePointer.end() ? existing->second : NewId();
            }
        }
        samplerPointerId = NewId();

        // Pass 3 -- rebuild the stream.
        std::vector<Instruction> rebuilt;
        rebuilt.reserve(instructions.size() + combinedVariables.size() * 6);

        std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> setBinding;  // var -> set,binding
        for (const Instruction& inst : instructions)
        {
            if (inst.opcode == kOpDecorate && inst.words.size() >= 4 &&
                combinedVariables.count(inst.words[1]) != 0)
            {
                if (inst.words[2] == kDecorationDescriptorSet)
                    setBinding[inst.words[1]].first = inst.words[3];
                else if (inst.words[2] == kDecorationBinding)
                    setBinding[inst.words[1]].second = inst.words[3];
            }
        }

        // The new types must come after EVERY image type they point at, and MojoShader emits a
        // second OpTypeSampledImage (and its image type) later in the section when an effect uses
        // more than one texture kind -- EnvironmentMapEffect's 2D + cube pair is the case that
        // found this. Anchoring on the FIRST OpTypeSampledImage put an OpTypePointer in front of
        // the cube image type it referenced, which is what naga reported as `invalid id`.
        std::size_t lastSampledImageType = 0;
        for (std::size_t index = 0; index < instructions.size(); ++index)
            if (instructions[index].opcode == kOpTypeSampledImage) lastSampledImageType = index;

        bool typesEmitted = false;
        std::size_t position = 0;
        for (const Instruction& inst : instructions)
        {
            const std::size_t here = position++;
            // Decorations on a combined variable become decorations on both halves.
            if (inst.opcode == kOpDecorate && inst.words.size() >= 4 &&
                combinedVariables.count(inst.words[1]) != 0)
            {
                const Half& half = halves[inst.words[1]];
                const std::uint32_t decoration = inst.words[2];
                if (decoration == kDecorationBinding)
                {
                    const std::uint32_t original = inst.words[3];
                    rebuilt.push_back(Make(kOpDecorate,
                                           {half.imageVar, kDecorationBinding, original * 2u}));
                    rebuilt.push_back(Make(kOpDecorate,
                                           {half.samplerVar, kDecorationBinding, original * 2u + 1u}));
                }
                else
                {
                    std::vector<std::uint32_t> operands(inst.words.begin() + 1, inst.words.end());
                    operands[0] = half.imageVar;
                    rebuilt.push_back(Make(kOpDecorate, operands));
                    operands[0] = half.samplerVar;
                    rebuilt.push_back(Make(kOpDecorate, operands));
                }
                continue;  // the combined variable itself is gone
            }

            if (inst.opcode == kOpName && inst.words.size() >= 3 &&
                combinedVariables.count(inst.words[1]) != 0)
            {
                const Half& half = halves[inst.words[1]];
                const std::string base = ReadLiteralString(inst.words, 2);
                std::vector<std::uint32_t> operands{half.imageVar};
                PushString(operands, base + "_texture");
                rebuilt.push_back(Make(kOpName, operands));
                operands.assign(1, half.samplerVar);
                PushString(operands, base + "_sampler");
                rebuilt.push_back(Make(kOpName, operands));
                continue;
            }

            // The new types go in just after the last OpTypeSampledImage, which is inside the
            // types/constants/globals section and after every image type they reference.
            if (here == lastSampledImageType && !typesEmitted)
            {
                rebuilt.push_back(inst);
                if (existingSamplerType == 0)
                    rebuilt.push_back(Make(kOpTypeSampler, {samplerTypeId}));
                for (const auto& [imageType, pointerId] : imagePointerId)
                {
                    if (existingImagePointer.count(imageType) != 0) continue;
                    rebuilt.push_back(Make(kOpTypePointer,
                                           {pointerId, kStorageClassUniformConstant, imageType}));
                }
                rebuilt.push_back(Make(
                    kOpTypePointer, {samplerPointerId, kStorageClassUniformConstant, samplerTypeId}));
                typesEmitted = true;
                continue;
            }

            // A combined global becomes two globals.
            if (inst.opcode == kOpVariable && inst.words.size() >= 4 &&
                combinedVariables.count(inst.words[2]) != 0)
            {
                const Half& half = halves[inst.words[2]];
                rebuilt.push_back(Make(kOpVariable, {imagePointerId[half.imageType], half.imageVar,
                                                     kStorageClassUniformConstant}));
                rebuilt.push_back(Make(kOpVariable, {samplerPointerId, half.samplerVar,
                                                     kStorageClassUniformConstant}));
                continue;
            }

            // A load of a combined global becomes two loads and an OpSampledImage that keeps the
            // original result id.
            if (inst.opcode == kOpLoad && inst.words.size() >= 4 &&
                combinedVariables.count(inst.words[3]) != 0)
            {
                const std::uint32_t resultType = inst.words[1];
                const std::uint32_t resultId = inst.words[2];
                const Half& half = halves[inst.words[3]];
                const std::uint32_t imageValue = NewId();
                const std::uint32_t samplerValue = NewId();
                rebuilt.push_back(Make(kOpLoad, {half.imageType, imageValue, half.imageVar}));
                rebuilt.push_back(Make(kOpLoad, {samplerTypeId, samplerValue, half.samplerVar}));
                rebuilt.push_back(
                    Make(kOpSampledImage, {resultType, resultId, imageValue, samplerValue}));
                continue;
            }

            rebuilt.push_back(inst);
        }

        if (!typesEmitted)
        {
            result.error = "no OpTypeSampledImage to anchor the new types on";
            return result;
        }

        std::vector<std::uint32_t> out;
        out.reserve(input.size() + combinedVariables.size() * 12);
        out.assign(input.begin(), input.begin() + 5);
        out[3] = bound;
        for (const Instruction& inst : rebuilt)
            out.insert(out.end(), inst.words.begin(), inst.words.end());

        for (const auto& [var, sampledImage] : combinedVariables)
        {
            SplitSamplerBinding binding;
            binding.set = setBinding[var].first;
            binding.originalBinding = setBinding[var].second;
            binding.textureBinding = binding.originalBinding * 2u;
            binding.samplerBinding = binding.originalBinding * 2u + 1u;
            const auto dim = imageDim.find(sampledImageToImage[sampledImage]);
            binding.dim = dim != imageDim.end() ? dim->second : 0u;
            const auto name = names.find(var);
            binding.name = name != names.end() ? name->second : std::string();
            result.samplers.push_back(binding);
        }

        result.words = std::move(out);
        result.changed = true;
        return result;
    }
}
