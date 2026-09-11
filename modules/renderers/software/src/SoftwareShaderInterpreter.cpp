// SPDX-License-Identifier: MS-PL

#if defined(CNA_SOFTWARE_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/Software/SoftwareCompiledEffect.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        using Vector = std::array<float, 4>;

        constexpr std::size_t kTemporaryRegisterCount = 32;
        constexpr std::size_t kInputRegisterCount = 16;
        constexpr std::size_t kOutputRegisterCount = 16;
        constexpr std::size_t kFloatConstantRegisterCount = 256;
        constexpr std::size_t kIntegerConstantRegisterCount = 16;
        constexpr std::size_t kBooleanConstantRegisterCount = 16;

        enum class RegisterType : std::uint8_t
        {
            Temporary = 0,
            Input = 1,
            Constant = 2,
            Address = 3,
            RasterOutput = 4,
            AttributeOutput = 5,
            TextureCoordinateOutput = 6,
            IntegerConstant = 7,
            ColorOutput = 8,
            DepthOutput = 9,
            Sampler = 10,
            Constant2 = 11,
            Constant3 = 12,
            Constant4 = 13,
            BooleanConstant = 14,
            Loop = 15,
            TemporaryFloat16 = 16,
            Miscellaneous = 17,
            Label = 18,
            Predicate = 19
        };

        struct Operand
        {
            RegisterType type = RegisterType::Temporary;
            int number = 0;
            bool relative = false;
            std::uint8_t swizzle = 0xE4u;
            std::uint8_t sourceModifier = 0;
            std::uint8_t writeMask = 0xFu;
            std::uint8_t resultModifier = 0;
        };

        [[nodiscard]] RegisterType DecodeRegisterType(std::uint32_t token)
        {
            return static_cast<RegisterType>(((token >> 28u) & 0x7u) | ((token >> 8u) & 0x18u));
        }

        void NormalizeConstantRegister(Operand& operand)
        {
            if (operand.type == RegisterType::Constant2)
            {
                operand.type = RegisterType::Constant;
                operand.number += 2048;
            }
            else if (operand.type == RegisterType::Constant3)
            {
                operand.type = RegisterType::Constant;
                operand.number += 4096;
            }
            else if (operand.type == RegisterType::Constant4)
            {
                operand.type = RegisterType::Constant;
                operand.number += 6144;
            }
        }

        [[nodiscard]] Operand DecodeDestination(std::uint32_t token)
        {
            Operand operand;
            operand.type = DecodeRegisterType(token);
            operand.number = static_cast<int>(token & 0x7FFu);
            operand.relative = (token & (1u << 13u)) != 0u;
            operand.writeMask = static_cast<std::uint8_t>((token >> 16u) & 0xFu);
            operand.resultModifier = static_cast<std::uint8_t>((token >> 20u) & 0xFu);
            NormalizeConstantRegister(operand);
            if (operand.relative)
                throw std::runtime_error(
                    "Software vertex shader: relative destination addressing is unsupported.");
            return operand;
        }

        [[nodiscard]] Operand DecodeSource(const std::vector<std::uint32_t>& tokens,
                                           std::size_t& cursor, std::uint8_t majorVersion,
                                           RegisterType& relativeType, int& relativeComponent)
        {
            if (cursor >= tokens.size())
                throw std::runtime_error("Software vertex shader: missing source operand.");
            const std::uint32_t token = tokens[cursor++];
            Operand operand;
            operand.type = DecodeRegisterType(token);
            operand.number = static_cast<int>(token & 0x7FFu);
            operand.relative = (token & (1u << 13u)) != 0u;
            operand.swizzle = static_cast<std::uint8_t>((token >> 16u) & 0xFFu);
            operand.sourceModifier = static_cast<std::uint8_t>((token >> 24u) & 0xFu);
            NormalizeConstantRegister(operand);

            relativeType = RegisterType::Address;
            relativeComponent = 0;
            if (operand.relative && majorVersion >= 2u)
            {
                if (cursor >= tokens.size())
                    throw std::runtime_error(
                        "Software vertex shader: missing relative-address operand.");
                const std::uint32_t relativeToken = tokens[cursor++];
                relativeType = DecodeRegisterType(relativeToken);
                relativeComponent = static_cast<int>((relativeToken >> 16u) & 0x3u);
            }
            return operand;
        }

        [[nodiscard]] float Dot(const Vector& left, const Vector& right, int componentCount)
        {
            float result = 0.0f;
            for (int component = 0; component < componentCount; ++component)
            {
                result += left[static_cast<std::size_t>(component)] *
                          right[static_cast<std::size_t>(component)];
            }
            return result;
        }

        [[nodiscard]] bool Compare(float left, float right, std::uint8_t control)
        {
            switch (control)
            {
            case 1: return left > right;
            case 2: return left == right;
            case 3: return left >= right;
            case 4: return left < right;
            case 5: return left != right;
            case 6: return left <= right;
            default:
                throw std::runtime_error(
                    "Software vertex shader: invalid comparison control.");
            }
        }

        class VertexMachine
        {
        public:
            VertexMachine(const SoftwareShaderProgramEXT& program,
                          std::span<const float> floatRegisters,
                          std::span<const int> integerRegisters,
                          std::span<const unsigned char> booleanRegisters,
                          std::span<const SoftwareShaderSemanticValueEXT> inputs,
                          const ISoftwarePixelSamplerEXT* sampler)
                : program_(program), floatRegisters_(floatRegisters),
                  integerRegisters_(integerRegisters), booleanRegisters_(booleanRegisters),
                  sampler_(sampler)
            {
                if (program.stage != SoftwareShaderStageEXT::Vertex)
                    throw std::invalid_argument(
                        "Software vertex shader: a pixel program cannot run as a vertex program.");
                for (auto& input : inputRegisters_)
                    input = {0.0f, 0.0f, 0.0f, 1.0f};
                for (const SoftwareShaderSemanticEXT& declaration : program.inputSemantics)
                {
                    if (declaration.registerNumber >= inputRegisters_.size())
                        throw std::runtime_error(
                            "Software vertex shader: input register exceeds the XNA limit.");
                    const auto match =
                        std::find_if(inputs.begin(), inputs.end(),
                                     [&](const auto& candidate)
                                     {
                                         return candidate.usage == declaration.usage &&
                                                candidate.usageIndex == declaration.usageIndex;
                                     });
                    if (match != inputs.end())
                        inputRegisters_[declaration.registerNumber] = match->value;
                }
            }

            [[nodiscard]] SoftwareVertexShaderResultEXT Execute()
            {
                struct ConditionalFrame
                {
                    bool parentActive;
                    bool condition;
                    bool sawElse;
                };
                struct LoopFrame
                {
                    std::size_t bodyBegin;
                    std::size_t end;
                    std::size_t conditionalDepth;
                    int remaining;
                    int current;
                    int step;
                    int previousLoopRegister;
                    std::uint16_t endOpcode;
                };
                struct CallFrame
                {
                    std::size_t returnAddress;
                    std::size_t conditionalDepth;
                    std::size_t loopDepth;
                };
                std::vector<ConditionalFrame> conditionals;
                std::vector<LoopFrame> loops;
                std::vector<CallFrame> calls;
                const auto labels = CollectLabels();
                bool active = true;
                std::size_t pc = 0u;
                while (pc < program_.instructions.size())
                {
                    const SoftwareShaderInstructionEXT& instruction = program_.instructions[pc];
                    if (instruction.opcode == 40u || instruction.opcode == 41u)
                    {
                        const bool condition = active && EvaluateConditional(instruction);
                        conditionals.push_back({active, condition, false});
                        active = active && condition;
                        ++pc;
                    }
                    else if (instruction.opcode == 42u)
                    {
                        if (conditionals.empty() || conditionals.back().sawElse)
                            throw std::runtime_error(
                                "Software vertex shader: ELSE without a matching IF.");
                        conditionals.back().sawElse = true;
                        active = conditionals.back().parentActive &&
                                 !conditionals.back().condition;
                        ++pc;
                    }
                    else if (instruction.opcode == 43u)
                    {
                        if (conditionals.empty())
                            throw std::runtime_error(
                                "Software vertex shader: ENDIF without a matching IF.");
                        active = conditionals.back().parentActive;
                        conditionals.pop_back();
                        ++pc;
                    }
                    else if (instruction.opcode == 27u || instruction.opcode == 38u)
                    {
                        const std::size_t end = FindMatchingLoop(pc);
                        if (!active)
                        {
                            pc = end + 1u;
                            continue;
                        }
                        const auto parameters = ReadLoopParameters(instruction);
                        const int iterationCount = parameters[0];
                        if (iterationCount < 0 || iterationCount > 255)
                        {
                            throw std::runtime_error(
                                "Software vertex shader: loop iteration count exceeds the D3D limit.");
                        }
                        if (iterationCount == 0)
                        {
                            pc = end + 1u;
                            continue;
                        }
                        const bool isLoop = instruction.opcode == 27u;
                        loops.push_back({pc + 1u, end, conditionals.size(), iterationCount,
                                         parameters[1], parameters[2], loopRegister_,
                                         static_cast<std::uint16_t>(isLoop ? 29u : 39u)});
                        if (isLoop)
                            loopRegister_ = parameters[1];
                        ++pc;
                    }
                    else if (instruction.opcode == 29u || instruction.opcode == 39u)
                    {
                        if (loops.empty() || loops.back().end != pc ||
                            loops.back().endOpcode != instruction.opcode)
                        {
                            throw std::runtime_error(
                                "Software vertex shader: loop terminator without a matching loop.");
                        }
                        if (conditionals.size() != loops.back().conditionalDepth)
                        {
                            throw std::runtime_error(
                                "Software vertex shader: conditional crosses a loop boundary.");
                        }
                        LoopFrame& frame = loops.back();
                        --frame.remaining;
                        if (frame.remaining > 0)
                        {
                            if (frame.endOpcode == 29u)
                            {
                                frame.current += frame.step;
                                loopRegister_ = frame.current;
                            }
                            pc = frame.bodyBegin;
                        }
                        else
                        {
                            if (frame.endOpcode == 29u)
                                loopRegister_ = frame.previousLoopRegister;
                            loops.pop_back();
                            ++pc;
                        }
                    }
                    else if (instruction.opcode == 44u || instruction.opcode == 45u ||
                             instruction.opcode == 96u)
                    {
                        if (!active)
                        {
                            ++pc;
                            continue;
                        }
                        if (loops.empty())
                            throw std::runtime_error(
                                "Software vertex shader: BREAK without an active loop.");
                        if (!EvaluateBreak(instruction))
                        {
                            ++pc;
                            continue;
                        }
                        const LoopFrame frame = loops.back();
                        conditionals.resize(frame.conditionalDepth);
                        active = true;
                        if (frame.endOpcode == 29u)
                            loopRegister_ = frame.previousLoopRegister;
                        loops.pop_back();
                        pc = frame.end + 1u;
                    }
                    else if (instruction.opcode == 25u || instruction.opcode == 26u)
                    {
                        if (!active)
                        {
                            ++pc;
                            continue;
                        }
                        std::size_t cursor = 1u;
                        const int label = ReadLabel(instruction.tokens, cursor);
                        bool takeCall = true;
                        if (instruction.opcode == 26u)
                            takeCall = ReadSource(instruction.tokens, cursor)[0] != 0.0f;
                        if (cursor != instruction.tokens.size())
                            throw std::runtime_error(
                                "Software vertex shader: malformed CALL instruction.");
                        if (!takeCall)
                        {
                            ++pc;
                            continue;
                        }
                        const auto target = std::find_if(
                            labels.begin(), labels.end(),
                            [label](const auto& candidate) { return candidate.first == label; });
                        if (target == labels.end())
                            throw std::runtime_error(
                                "Software vertex shader: CALL names an undefined label.");
                        if (target->second <= pc)
                            throw std::runtime_error(
                                "Software vertex shader: only forward CALL targets are legal.");
                        const std::size_t maxCallDepth =
                            program_.majorVersion >= 3u || program_.minorVersion == 0xFFu ? 4u : 1u;
                        if (calls.size() >= maxCallDepth)
                            throw std::runtime_error(
                                "Software vertex shader: call nesting exceeds the D3D limit.");
                        calls.push_back({pc + 1u, conditionals.size(), loops.size()});
                        pc = target->second + 1u;
                    }
                    else if (instruction.opcode == 28u)
                    {
                        if (!active)
                        {
                            ++pc;
                            continue;
                        }
                        if (calls.empty())
                        {
                            if (!conditionals.empty() || !loops.empty())
                                throw std::runtime_error(
                                    "Software vertex shader: main RET crosses a flow boundary.");
                            pc = program_.instructions.size();
                            continue;
                        }
                        const CallFrame frame = calls.back();
                        if (conditionals.size() != frame.conditionalDepth ||
                            loops.size() != frame.loopDepth)
                        {
                            throw std::runtime_error(
                                "Software vertex shader: RET crosses a subroutine flow boundary.");
                        }
                        calls.pop_back();
                        pc = frame.returnAddress;
                    }
                    else if (instruction.opcode == 30u)
                    {
                        throw std::runtime_error(
                            "Software vertex shader: LABEL reached without a CALL.");
                    }
                    else if (active)
                    {
                        ExecuteInstruction(instruction);
                        ++pc;
                    }
                    else
                        ++pc;
                }
                if (!conditionals.empty())
                    throw std::runtime_error(
                        "Software vertex shader: IF without a matching ENDIF.");
                if (!loops.empty())
                    throw std::runtime_error(
                        "Software vertex shader: loop without a matching terminator.");
                if (!calls.empty())
                    throw std::runtime_error(
                        "Software vertex shader: subroutine ended without RET.");
                return BuildResult();
            }

        private:
            [[nodiscard]] Vector ReadRaw(RegisterType type, int number) const
            {
                if (number < 0)
                    throw std::runtime_error("Software vertex shader: negative register index.");
                const auto checked = [&](const auto& registers, const char* family) -> Vector
                {
                    if (static_cast<std::size_t>(number) >= registers.size())
                    {
                        throw std::runtime_error(std::string("Software vertex shader: ") + family +
                                                 " register is out of range.");
                    }
                    return registers[static_cast<std::size_t>(number)];
                };

                switch (type)
                {
                case RegisterType::Temporary:
                case RegisterType::TemporaryFloat16:
                    return checked(temporaryRegisters_, "temporary");
                case RegisterType::Input:
                    return checked(inputRegisters_, "input");
                case RegisterType::Address:
                {
                    if (number != 0)
                        throw std::runtime_error(
                            "Software vertex shader: address register is out of range.");
                    return {static_cast<float>(addressRegister_[0]),
                            static_cast<float>(addressRegister_[1]),
                            static_cast<float>(addressRegister_[2]),
                            static_cast<float>(addressRegister_[3])};
                }
                case RegisterType::RasterOutput:
                    return checked(rasterOutputs_, "raster output");
                case RegisterType::AttributeOutput:
                    return checked(attributeOutputs_, "attribute output");
                case RegisterType::TextureCoordinateOutput:
                    return checked(outputRegisters_, "output");
                case RegisterType::Constant:
                {
                    if (number < static_cast<int>(kFloatConstantRegisterCount) &&
                        localFloatDefined_[static_cast<std::size_t>(number)])
                    {
                        return localFloatConstants_[static_cast<std::size_t>(number)];
                    }
                    const std::size_t offset = static_cast<std::size_t>(number) * 4u;
                    if (offset + 4u > floatRegisters_.size())
                        throw std::runtime_error(
                            "Software vertex shader: float constant register is out of range.");
                    return {floatRegisters_[offset], floatRegisters_[offset + 1u],
                            floatRegisters_[offset + 2u], floatRegisters_[offset + 3u]};
                }
                case RegisterType::IntegerConstant:
                {
                    if (number < static_cast<int>(kIntegerConstantRegisterCount) &&
                        localIntegerDefined_[static_cast<std::size_t>(number)])
                    {
                        const auto& value =
                            localIntegerConstants_[static_cast<std::size_t>(number)];
                        return {static_cast<float>(value[0]), static_cast<float>(value[1]),
                                static_cast<float>(value[2]), static_cast<float>(value[3])};
                    }
                    const std::size_t offset = static_cast<std::size_t>(number) * 4u;
                    if (offset + 4u > integerRegisters_.size())
                        throw std::runtime_error(
                            "Software vertex shader: integer constant register is out of range.");
                    return {static_cast<float>(integerRegisters_[offset]),
                            static_cast<float>(integerRegisters_[offset + 1u]),
                            static_cast<float>(integerRegisters_[offset + 2u]),
                            static_cast<float>(integerRegisters_[offset + 3u])};
                }
                case RegisterType::BooleanConstant:
                {
                    if (number < static_cast<int>(kBooleanConstantRegisterCount) &&
                        localBooleanDefined_[static_cast<std::size_t>(number)])
                    {
                        const float value =
                            localBooleanConstants_[static_cast<std::size_t>(number)] ? 1.0f : 0.0f;
                        return {value, value, value, value};
                    }
                    if (static_cast<std::size_t>(number) >= booleanRegisters_.size())
                        throw std::runtime_error(
                            "Software vertex shader: Boolean constant register is out of range.");
                    const float value =
                        booleanRegisters_[static_cast<std::size_t>(number)] != 0u ? 1.0f : 0.0f;
                    return {value, value, value, value};
                }
                case RegisterType::Loop:
                {
                    const float value = static_cast<float>(loopRegister_);
                    return {value, value, value, value};
                }
                case RegisterType::Predicate:
                    return {predicateRegister_[0] ? 1.0f : 0.0f,
                            predicateRegister_[1] ? 1.0f : 0.0f,
                            predicateRegister_[2] ? 1.0f : 0.0f,
                            predicateRegister_[3] ? 1.0f : 0.0f};
                default:
                    throw std::runtime_error(
                        "Software vertex shader: unsupported source register type " +
                        std::to_string(static_cast<unsigned>(type)) + ".");
                }
            }

            [[nodiscard]] Vector ReadSource(const std::vector<std::uint32_t>& tokens,
                                            std::size_t& cursor)
            {
                RegisterType relativeType;
                int relativeComponent = 0;
                Operand operand = DecodeSource(tokens, cursor, program_.majorVersion, relativeType,
                                               relativeComponent);
                if (operand.relative)
                {
                    const Vector relative = ReadRaw(relativeType, 0);
                    operand.number +=
                        static_cast<int>(relative[static_cast<std::size_t>(relativeComponent)]);
                }
                const Vector raw = ReadRaw(operand.type, operand.number);
                Vector value{};
                for (int component = 0; component < 4; ++component)
                {
                    const auto selected = static_cast<std::size_t>(
                        (operand.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
                    value[static_cast<std::size_t>(component)] = raw[selected];
                }
                switch (operand.sourceModifier)
                {
                case 0:
                    break;
                case 1:
                    for (float& component : value)
                        component = -component;
                    break;
                case 2:
                    for (float& component : value)
                        component -= 0.5f;
                    break;
                case 3:
                    for (float& component : value)
                        component = -(component - 0.5f);
                    break;
                case 4:
                    for (float& component : value)
                        component = 2.0f * (component - 0.5f);
                    break;
                case 5:
                    for (float& component : value)
                        component = -2.0f * (component - 0.5f);
                    break;
                case 6:
                    for (float& component : value)
                        component = 1.0f - component;
                    break;
                case 7:
                    for (float& component : value)
                        component *= 2.0f;
                    break;
                case 8:
                    for (float& component : value)
                        component *= -2.0f;
                    break;
                case 9:
                {
                    const float divisor = value[2];
                    for (float& component : value)
                        component /= divisor;
                    break;
                }
                case 10:
                {
                    const float divisor = value[3];
                    for (float& component : value)
                        component /= divisor;
                    break;
                }
                case 11:
                    for (float& component : value)
                        component = std::abs(component);
                    break;
                case 12:
                    for (float& component : value)
                        component = -std::abs(component);
                    break;
                case 13:
                    for (float& component : value)
                        component = component == 0.0f ? 1.0f : 0.0f;
                    break;
                default:
                    throw std::runtime_error(
                        "Software vertex shader: unsupported source modifier.");
                }
                return value;
            }

            [[nodiscard]] bool EvaluateConditional(
                const SoftwareShaderInstructionEXT& instruction)
            {
                std::size_t cursor = 1u;
                const Vector source0 = ReadSource(instruction.tokens, cursor);
                if (instruction.opcode == 40u)
                    return source0[0] != 0.0f;
                const Vector source1 = ReadSource(instruction.tokens, cursor);
                return Compare(source0[0], source1[0], instruction.controls);
            }

            void PrepareInstructionPredicate(const SoftwareShaderInstructionEXT& instruction)
            {
                predicateWriteMask_ = 0xFu;
                if (!instruction.predicated)
                    return;
                if (program_.majorVersion < 3u || instruction.tokens.size() < 2u)
                    throw std::runtime_error(
                        "Software vertex shader: malformed predicated instruction.");

                std::size_t cursor = instruction.tokens.size() - 1u;
                RegisterType relativeType;
                int relativeComponent = 0;
                const Operand predicate = DecodeSource(
                    instruction.tokens, cursor, program_.majorVersion,
                    relativeType, relativeComponent);
                if (cursor != instruction.tokens.size() ||
                    predicate.type != RegisterType::Predicate || predicate.number != 0 ||
                    predicate.relative ||
                    (predicate.sourceModifier != 0u && predicate.sourceModifier != 13u))
                {
                    throw std::runtime_error(
                        "Software vertex shader: invalid instruction predicate.");
                }

                predicateWriteMask_ = 0u;
                for (int component = 0; component < 4; ++component)
                {
                    const auto selected = static_cast<std::size_t>(
                        (predicate.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
                    bool enabled = predicateRegister_[selected];
                    if (predicate.sourceModifier == 13u)
                        enabled = !enabled;
                    if (enabled)
                        predicateWriteMask_ |= static_cast<std::uint8_t>(1u << component);
                }
            }

            [[nodiscard]] int ReadLabel(const std::vector<std::uint32_t>& tokens,
                                        std::size_t& cursor) const
            {
                RegisterType relativeType;
                int relativeComponent = 0;
                const Operand operand = DecodeSource(tokens, cursor, program_.majorVersion,
                                                     relativeType, relativeComponent);
                if (operand.type != RegisterType::Label || operand.relative ||
                    operand.sourceModifier != 0u)
                {
                    throw std::runtime_error(
                        "Software vertex shader: CALL/LABEL requires a direct label register.");
                }
                return operand.number;
            }

            [[nodiscard]] std::vector<std::pair<int, std::size_t>> CollectLabels() const
            {
                std::vector<std::pair<int, std::size_t>> labels;
                for (std::size_t index = 0u; index < program_.instructions.size(); ++index)
                {
                    const auto& instruction = program_.instructions[index];
                    if (instruction.opcode != 30u)
                        continue;
                    std::size_t cursor = 1u;
                    const int label = ReadLabel(instruction.tokens, cursor);
                    if (cursor != instruction.tokens.size())
                        throw std::runtime_error(
                            "Software vertex shader: malformed LABEL instruction.");
                    if (std::find_if(labels.begin(), labels.end(),
                                     [label](const auto& candidate)
                                     { return candidate.first == label; }) != labels.end())
                    {
                        throw std::runtime_error(
                            "Software vertex shader: duplicate LABEL instruction.");
                    }
                    labels.emplace_back(label, index);
                }
                return labels;
            }

            [[nodiscard]] std::size_t FindMatchingLoop(std::size_t start) const
            {
                const std::uint16_t expectedEnd =
                    program_.instructions[start].opcode == 27u ? 29u : 39u;
                int depth = 1;
                for (std::size_t cursor = start + 1u; cursor < program_.instructions.size();
                     ++cursor)
                {
                    const std::uint16_t opcode = program_.instructions[cursor].opcode;
                    if (opcode == 27u || opcode == 38u)
                        ++depth;
                    else if (opcode == 29u || opcode == 39u)
                    {
                        --depth;
                        if (depth == 0)
                        {
                            if (opcode != expectedEnd)
                                throw std::runtime_error(
                                    "Software vertex shader: mismatched loop terminator.");
                            return cursor;
                        }
                    }
                }
                throw std::runtime_error(
                    "Software vertex shader: loop without a matching terminator.");
            }

            [[nodiscard]] std::array<int, 3> ReadLoopParameters(
                const SoftwareShaderInstructionEXT& instruction)
            {
                std::size_t cursor = 1u;
                if (instruction.opcode == 27u)
                    static_cast<void>(ReadSource(instruction.tokens, cursor));
                const Vector source = ReadSource(instruction.tokens, cursor);
                if (cursor != instruction.tokens.size())
                    throw std::runtime_error(
                        "Software vertex shader: malformed loop instruction.");
                if (!std::isfinite(source[0]) || source[0] < 0.0f || source[0] > 255.0f)
                    throw std::runtime_error(
                        "Software vertex shader: loop iteration count exceeds the D3D limit.");
                if (instruction.opcode == 38u)
                    return {static_cast<int>(source[0]), 0, 0};
                if (!std::isfinite(source[1]) || !std::isfinite(source[2]) ||
                    !std::isfinite(source[3]) || source[1] < 0.0f || source[1] > 255.0f ||
                    source[2] < -128.0f || source[2] > 127.0f || source[3] != 0.0f)
                {
                    throw std::runtime_error(
                        "Software vertex shader: loop parameters exceed the D3D limits.");
                }
                return {static_cast<int>(source[0]), static_cast<int>(source[1]),
                        static_cast<int>(source[2])};
            }

            [[nodiscard]] bool EvaluateBreak(
                const SoftwareShaderInstructionEXT& instruction)
            {
                if (instruction.opcode == 44u)
                {
                    if (instruction.tokens.size() != 1u)
                        throw std::runtime_error(
                            "Software vertex shader: malformed BREAK instruction.");
                    return true;
                }
                std::size_t cursor = 1u;
                const Vector source0 = ReadSource(instruction.tokens, cursor);
                bool result = source0[0] != 0.0f;
                if (instruction.opcode == 45u)
                {
                    const Vector source1 = ReadSource(instruction.tokens, cursor);
                    result = Compare(source0[0], source1[0], instruction.controls);
                }
                if (cursor != instruction.tokens.size())
                    throw std::runtime_error(
                        "Software vertex shader: malformed conditional BREAK instruction.");
                return result;
            }

            [[nodiscard]] Vector ReadMatrixRow(Operand base, int row)
            {
                base.number += row;
                const Vector raw = ReadRaw(base.type, base.number);
                Vector value{};
                for (int component = 0; component < 4; ++component)
                {
                    const auto selected = static_cast<std::size_t>(
                        (base.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
                    value[static_cast<std::size_t>(component)] = raw[selected];
                }
                if (base.sourceModifier == 1u)
                    for (float& component : value)
                        component = -component;
                else if (base.sourceModifier != 0u)
                    throw std::runtime_error(
                        "Software vertex shader: unsupported matrix source modifier.");
                return value;
            }

            void Write(const Operand& destination, Vector value)
            {
                const std::uint8_t writeMask =
                    static_cast<std::uint8_t>(destination.writeMask & predicateWriteMask_);
                if ((destination.resultModifier & 0x1u) != 0u)
                    for (float& component : value)
                        component = std::clamp(component, 0.0f, 1.0f);

                Vector* target = nullptr;
                switch (destination.type)
                {
                case RegisterType::Temporary:
                case RegisterType::TemporaryFloat16:
                    if (destination.number >= 0 &&
                        static_cast<std::size_t>(destination.number) < temporaryRegisters_.size())
                        target = &temporaryRegisters_[static_cast<std::size_t>(destination.number)];
                    break;
                case RegisterType::RasterOutput:
                    if (destination.number >= 0 &&
                        static_cast<std::size_t>(destination.number) < rasterOutputs_.size())
                        target = &rasterOutputs_[static_cast<std::size_t>(destination.number)];
                    break;
                case RegisterType::AttributeOutput:
                    if (destination.number >= 0 &&
                        static_cast<std::size_t>(destination.number) < attributeOutputs_.size())
                        target = &attributeOutputs_[static_cast<std::size_t>(destination.number)];
                    break;
                case RegisterType::TextureCoordinateOutput:
                    if (destination.number >= 0 &&
                        static_cast<std::size_t>(destination.number) < outputRegisters_.size())
                        target = &outputRegisters_[static_cast<std::size_t>(destination.number)];
                    break;
                case RegisterType::Address:
                    for (int component = 0; component < 4; ++component)
                    {
                        if ((writeMask & (1u << component)) != 0u)
                            addressRegister_[static_cast<std::size_t>(component)] =
                                static_cast<int>(value[static_cast<std::size_t>(component)]);
                    }
                    return;
                case RegisterType::Predicate:
                    for (int component = 0; component < 4; ++component)
                    {
                        if ((writeMask & (1u << component)) != 0u)
                        {
                            predicateRegister_[static_cast<std::size_t>(component)] =
                                value[static_cast<std::size_t>(component)] != 0.0f;
                        }
                    }
                    return;
                default:
                    break;
                }
                if (target == nullptr)
                    throw std::runtime_error(
                        "Software vertex shader: unsupported destination register type " +
                        std::to_string(static_cast<unsigned>(destination.type)) + ".");
                for (int component = 0; component < 4; ++component)
                {
                    if ((writeMask & (1u << component)) != 0u)
                    {
                        (*target)[static_cast<std::size_t>(component)] =
                            value[static_cast<std::size_t>(component)];
                    }
                }
            }

            void DefineFloat(const std::vector<std::uint32_t>& tokens)
            {
                if (tokens.size() != 6u)
                    throw std::runtime_error("Software vertex shader: malformed DEF instruction.");
                const Operand destination = DecodeDestination(tokens[1]);
                if (destination.type != RegisterType::Constant || destination.number < 0 ||
                    static_cast<std::size_t>(destination.number) >= localFloatConstants_.size())
                    throw std::runtime_error("Software vertex shader: invalid DEF destination.");
                auto& value = localFloatConstants_[static_cast<std::size_t>(destination.number)];
                for (int component = 0; component < 4; ++component)
                {
                    value[static_cast<std::size_t>(component)] =
                        std::bit_cast<float>(tokens[static_cast<std::size_t>(component) + 2u]);
                }
                localFloatDefined_[static_cast<std::size_t>(destination.number)] = true;
            }

            void DefineInteger(const std::vector<std::uint32_t>& tokens)
            {
                if (tokens.size() != 6u)
                    throw std::runtime_error("Software vertex shader: malformed DEFI instruction.");
                const Operand destination = DecodeDestination(tokens[1]);
                if (destination.type != RegisterType::IntegerConstant || destination.number < 0 ||
                    static_cast<std::size_t>(destination.number) >= localIntegerConstants_.size())
                    throw std::runtime_error("Software vertex shader: invalid DEFI destination.");
                auto& value = localIntegerConstants_[static_cast<std::size_t>(destination.number)];
                for (int component = 0; component < 4; ++component)
                {
                    value[static_cast<std::size_t>(component)] =
                        static_cast<std::int32_t>(tokens[static_cast<std::size_t>(component) + 2u]);
                }
                localIntegerDefined_[static_cast<std::size_t>(destination.number)] = true;
            }

            void DefineBoolean(const std::vector<std::uint32_t>& tokens)
            {
                if (tokens.size() != 3u)
                    throw std::runtime_error("Software vertex shader: malformed DEFB instruction.");
                const Operand destination = DecodeDestination(tokens[1]);
                if (destination.type != RegisterType::BooleanConstant || destination.number < 0 ||
                    static_cast<std::size_t>(destination.number) >= localBooleanConstants_.size())
                    throw std::runtime_error("Software vertex shader: invalid DEFB destination.");
                localBooleanConstants_[static_cast<std::size_t>(destination.number)] =
                    tokens[2] != 0u;
                localBooleanDefined_[static_cast<std::size_t>(destination.number)] = true;
            }

            void ExecuteInstruction(const SoftwareShaderInstructionEXT& instruction)
            {
                const auto& tokens = instruction.tokens;
                if (tokens.empty())
                    throw std::runtime_error("Software vertex shader: empty instruction.");
                if (instruction.opcode == 0u || instruction.opcode == 31u ||
                    instruction.opcode == 0xFFFEu)
                    return;
                PrepareInstructionPredicate(instruction);
                if (instruction.opcode == 81u)
                {
                    DefineFloat(tokens);
                    return;
                }
                if (instruction.opcode == 48u)
                {
                    DefineInteger(tokens);
                    return;
                }
                if (instruction.opcode == 47u)
                {
                    DefineBoolean(tokens);
                    return;
                }
                if (instruction.opcode == 95u) // TEXLDL (vs_3_0)
                {
                    const std::size_t expected = 4u + (instruction.predicated ? 1u : 0u);
                    if (program_.majorVersion != 3u || tokens.size() != expected)
                        throw std::runtime_error(
                            "Software vertex shader: malformed TEXLDL instruction.");
                    if (sampler_ == nullptr)
                        throw std::runtime_error(
                            "Software vertex shader: TEXLDL has no sampler provider.");
                    const Operand destination = DecodeDestination(tokens[1]);
                    std::size_t cursor = 2u;
                    const Vector coordinate = ReadSource(tokens, cursor);
                    RegisterType relativeType;
                    int relativeComponent = 0;
                    const Operand samplerOperand = DecodeSource(
                        tokens, cursor, program_.majorVersion, relativeType, relativeComponent);
                    if (samplerOperand.type != RegisterType::Sampler ||
                        samplerOperand.relative || samplerOperand.sourceModifier != 0u)
                    {
                        throw std::runtime_error(
                            "Software vertex shader: TEXLDL requires a direct sampler register.");
                    }
                    const auto declaration = std::find_if(
                        program_.samplers.begin(), program_.samplers.end(),
                        [&samplerOperand](const SoftwareShaderSamplerEXT& candidate)
                        {
                            return candidate.registerNumber == samplerOperand.number;
                        });
                    if (declaration == program_.samplers.end())
                        throw std::runtime_error(
                            "Software vertex shader: TEXLDL sampler was not declared.");
                    SoftwarePixelSampleRequestEXT request;
                    request.samplerRegister =
                        static_cast<std::uint8_t>(samplerOperand.number);
                    request.samplerType = declaration->type;
                    request.coordinate = coordinate;
                    request.lodMode = SoftwareTextureLodModeEXT::Explicit;
                    request.lod = coordinate[3];
                    const Vector sample = sampler_->SampleEXT(request);
                    Vector result{};
                    for (int component = 0; component < 4; ++component)
                    {
                        const auto selected = static_cast<std::size_t>(
                            (samplerOperand.swizzle >>
                             static_cast<unsigned>(component * 2)) & 0x3u);
                        result[static_cast<std::size_t>(component)] = sample[selected];
                    }
                    Write(destination, result);
                    return;
                }

                if (tokens.size() < 3u)
                    throw std::runtime_error("Software vertex shader: malformed instruction.");
                const Operand destination = DecodeDestination(tokens[1]);
                std::size_t cursor = 2u;
                const Vector source0 = ReadSource(tokens, cursor);
                Vector source1{};
                Vector source2{};
                Vector result{};

                switch (instruction.opcode)
                {
                case 1: // MOV
                    result = source0;
                    break;
                case 2:  // ADD
                case 3:  // SUB
                case 5:  // MUL
                case 8:  // DP3
                case 9:  // DP4
                case 10: // MIN
                case 11: // MAX
                case 12: // SLT
                case 13: // SGE
                case 17: // DST
                case 20: // M4X4
                case 21: // M4X3
                case 22: // M3X4
                case 23: // M3X3
                case 24: // M3X2
                case 32: // POW
                case 33: // CRS
                case 94: // SETP
                    source1 = ReadSource(tokens, cursor);
                    break;
                case 4:  // MAD
                case 18: // LRP
                case 34: // SGN (two temporary operands are semantically ignored)
                    source1 = ReadSource(tokens, cursor);
                    source2 = ReadSource(tokens, cursor);
                    break;
                case 37: // SINCOS
                    if (program_.majorVersion < 3u)
                    {
                        source1 = ReadSource(tokens, cursor);
                        source2 = ReadSource(tokens, cursor);
                    }
                    break;
                default:
                    break;
                }

                switch (instruction.opcode)
                {
                case 1:
                    break;
                case 2:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] + source1[i];
                    break;
                case 3:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] - source1[i];
                    break;
                case 4:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] * source1[i] + source2[i];
                    break;
                case 5:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] * source1[i];
                    break;
                case 6:
                {
                    const float reciprocal =
                        source0[0] == 0.0f ? std::numeric_limits<float>::max() : 1.0f / source0[0];
                    result.fill(reciprocal);
                    break;
                }
                case 7:
                {
                    const float reciprocalSquareRoot = source0[0] == 0.0f
                                                           ? std::numeric_limits<float>::max()
                                                           : 1.0f / std::sqrt(std::abs(source0[0]));
                    result.fill(reciprocalSquareRoot);
                    break;
                }
                case 8:
                    result.fill(Dot(source0, source1, 3));
                    break;
                case 9:
                    result.fill(Dot(source0, source1, 4));
                    break;
                case 10:
                    for (int i = 0; i < 4; ++i)
                        result[i] = std::min(source0[i], source1[i]);
                    break;
                case 11:
                    for (int i = 0; i < 4; ++i)
                        result[i] = std::max(source0[i], source1[i]);
                    break;
                case 12:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] < source1[i] ? 1.0f : 0.0f;
                    break;
                case 13:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] >= source1[i] ? 1.0f : 0.0f;
                    break;
                case 14:
                case 78: // EXPP uses EXP behavior in the EasyGL MojoShader profile.
                    for (int i = 0; i < 4; ++i)
                        result[i] = std::exp2(source0[i]);
                    break;
                case 15:
                case 79: // LOGP is the lower-precision LOG form; EasyGL keeps full precision.
                {
                    const float magnitude = std::abs(source0[0]);
                    const float logarithm = magnitude == 0.0f
                                                ? -std::numeric_limits<float>::max()
                                                : std::log2(magnitude);
                    result.fill(logarithm);
                    break;
                }
                case 94:
                    for (int i = 0; i < 4; ++i)
                    {
                        result[i] = Compare(source0[i], source1[i], instruction.controls)
                                        ? 1.0f
                                        : 0.0f;
                    }
                    break;
                case 16:
                {
                    const float power = std::clamp(source0[3], -127.9961f, 127.9961f);
                    result = {1.0f, 0.0f, 0.0f, 1.0f};
                    if (source0[0] > 0.0f)
                    {
                        result[1] = source0[0];
                        if (source0[1] > 0.0f)
                            result[2] = std::pow(source0[1], power);
                    }
                    break;
                }
                case 17:
                    result = {1.0f, source0[1] * source1[1], source0[2], source1[3]};
                    break;
                case 18:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] * source1[i] + (1.0f - source0[i]) * source2[i];
                    break;
                case 19:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] - std::floor(source0[i]);
                    break;
                case 20:
                case 21:
                case 22:
                case 23:
                case 24:
                {
                    std::size_t sourceCursor = 2u;
                    const Vector vector = ReadSource(tokens, sourceCursor);
                    RegisterType relativeType;
                    int relativeComponent = 0;
                    Operand rowBase = DecodeSource(tokens, sourceCursor, program_.majorVersion,
                                                   relativeType, relativeComponent);
                    if (rowBase.relative)
                    {
                        const Vector relative = ReadRaw(relativeType, 0);
                        rowBase.number +=
                            static_cast<int>(relative[static_cast<std::size_t>(relativeComponent)]);
                    }
                    const int sourceComponents =
                        instruction.opcode == 20u || instruction.opcode == 21u ? 4 : 3;
                    const int rows = instruction.opcode == 20u || instruction.opcode == 22u ? 4
                                     : instruction.opcode == 24u                            ? 2
                                                                                            : 3;
                    for (int row = 0; row < rows; ++row)
                        result[static_cast<std::size_t>(row)] =
                            Dot(vector, ReadMatrixRow(rowBase, row), sourceComponents);
                    break;
                }
                case 32:
                    result.fill(std::pow(std::abs(source0[0]), source1[0]));
                    break;
                case 33:
                    result = {source0[1] * source1[2] - source0[2] * source1[1],
                              source0[2] * source1[0] - source0[0] * source1[2],
                              source0[0] * source1[1] - source0[1] * source1[0], 1.0f};
                    break;
                case 34:
                    for (int i = 0; i < 4; ++i)
                        result[i] = source0[i] > 0.0f ? 1.0f : source0[i] < 0.0f ? -1.0f : 0.0f;
                    break;
                case 35:
                    for (int i = 0; i < 4; ++i)
                        result[i] = std::abs(source0[i]);
                    break;
                case 36:
                {
                    const float squaredLength = source0[0] * source0[0] +
                                                source0[1] * source0[1] +
                                                source0[2] * source0[2];
                    const float factor = squaredLength == 0.0f
                                             ? std::numeric_limits<float>::max()
                                             : 1.0f / std::sqrt(squaredLength);
                    for (int component = 0; component < 4; ++component)
                        result[static_cast<std::size_t>(component)] =
                            source0[static_cast<std::size_t>(component)] * factor;
                    break;
                }
                case 37:
                    result[0] = std::cos(source0[0]);
                    result[1] = std::sin(source0[0]);
                    break;
                case 46:
                    for (int i = 0; i < 4; ++i)
                    {
                        const float value = source0[i];
                        result[i] = std::floor(std::abs(value) + 0.5f) * (value > 0.0f   ? 1.0f
                                                                          : value < 0.0f ? -1.0f
                                                                                         : 0.0f);
                    }
                    break;
                default:
                    throw std::runtime_error("Software vertex shader: unsupported opcode " +
                                             std::to_string(instruction.opcode) + ".");
                }
                Write(destination, result);
            }

            [[nodiscard]] SoftwareVertexShaderResultEXT BuildResult() const
            {
                SoftwareVertexShaderResultEXT result;
                if (program_.majorVersion < 3u)
                {
                    result.position = rasterOutputs_[0];
                    result.pointSize = rasterOutputs_[2][0];
                    for (int index = 0; index < 2; ++index)
                    {
                        SoftwareShaderSemanticValueEXT value;
                        value.usage = MOJOSHADER_USAGE_COLOR;
                        value.usageIndex = static_cast<std::uint8_t>(index);
                        value.value = attributeOutputs_[static_cast<std::size_t>(index)];
                        for (float& component : value.value)
                            component = std::clamp(component, 0.0f, 1.0f);
                        result.varyings.push_back(value);
                    }
                    for (int index = 0; index < 8; ++index)
                    {
                        result.varyings.push_back(
                            {MOJOSHADER_USAGE_TEXCOORD, static_cast<std::uint8_t>(index),
                             outputRegisters_[static_cast<std::size_t>(index)]});
                    }
                    result.varyings.push_back({MOJOSHADER_USAGE_FOG,
                                               0u,
                                               {rasterOutputs_[1][0], rasterOutputs_[1][0],
                                                rasterOutputs_[1][0], rasterOutputs_[1][0]}});
                    return result;
                }

                bool foundPosition = false;
                for (const SoftwareShaderSemanticEXT& semantic : program_.outputSemantics)
                {
                    if (semantic.registerNumber >= outputRegisters_.size())
                        throw std::runtime_error(
                            "Software vertex shader: output register exceeds the XNA limit.");
                    SoftwareShaderSemanticValueEXT value{semantic.usage, semantic.usageIndex,
                                                         outputRegisters_[semantic.registerNumber]};
                    if (semantic.usage == MOJOSHADER_USAGE_POSITION && semantic.usageIndex == 0u)
                    {
                        result.position = value.value;
                        foundPosition = true;
                    }
                    else if (semantic.usage == MOJOSHADER_USAGE_POINTSIZE)
                    {
                        result.pointSize = value.value[0];
                    }
                    else
                    {
                        if (semantic.usage == MOJOSHADER_USAGE_COLOR)
                            for (float& component : value.value)
                                component = std::clamp(component, 0.0f, 1.0f);
                        result.varyings.push_back(value);
                    }
                }
                if (!foundPosition)
                    throw std::runtime_error(
                        "Software vertex shader: Shader Model 3 program has no POSITION0 output.");
                return result;
            }

            const SoftwareShaderProgramEXT& program_;
            std::span<const float> floatRegisters_;
            std::span<const int> integerRegisters_;
            std::span<const unsigned char> booleanRegisters_;
            const ISoftwarePixelSamplerEXT* sampler_ = nullptr;
            std::array<Vector, kTemporaryRegisterCount> temporaryRegisters_{};
            std::array<Vector, kInputRegisterCount> inputRegisters_{};
            std::array<Vector, 3> rasterOutputs_{};
            std::array<Vector, 2> attributeOutputs_{};
            std::array<Vector, kOutputRegisterCount> outputRegisters_{};
            std::array<int, 4> addressRegister_{};
            int loopRegister_ = 0;
            std::array<bool, 4> predicateRegister_{};
            std::uint8_t predicateWriteMask_ = 0xFu;
            std::array<Vector, kFloatConstantRegisterCount> localFloatConstants_{};
            std::array<bool, kFloatConstantRegisterCount> localFloatDefined_{};
            std::array<std::array<int, 4>, kIntegerConstantRegisterCount> localIntegerConstants_{};
            std::array<bool, kIntegerConstantRegisterCount> localIntegerDefined_{};
            std::array<bool, kBooleanConstantRegisterCount> localBooleanConstants_{};
            std::array<bool, kBooleanConstantRegisterCount> localBooleanDefined_{};
        };
    } // namespace

    SoftwareVertexShaderResultEXT ExecuteSoftwareVertexShaderEXT(
        const SoftwareShaderProgramEXT& program, std::span<const float> floatRegisters,
        std::span<const int> integerRegisters, std::span<const unsigned char> booleanRegisters,
        std::span<const SoftwareShaderSemanticValueEXT> inputs,
        const ISoftwarePixelSamplerEXT* sampler)
    {
        return VertexMachine(program, floatRegisters, integerRegisters, booleanRegisters, inputs,
                             sampler)
            .Execute();
    }
} // namespace CNA::Internal::Renderers::Software

#endif // CNA_SOFTWARE_COMPILED_EFFECTS
