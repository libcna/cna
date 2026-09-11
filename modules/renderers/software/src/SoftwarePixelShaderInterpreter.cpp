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

namespace CNA::Internal::Renderers::Software {
namespace {
using Vector = std::array<float, 4>;

constexpr std::size_t kTemporaryRegisterCount = 32;
constexpr std::size_t kInputRegisterCount = 16;
constexpr std::size_t kTextureRegisterCount = 8;
constexpr std::size_t kColorOutputRegisterCount = 4;
constexpr std::size_t kFloatConstantRegisterCount = 256;
constexpr std::size_t kIntegerConstantRegisterCount = 16;
constexpr std::size_t kBooleanConstantRegisterCount = 16;

struct DerivativeValue {
  std::size_t instruction = 0;
  std::uint16_t opcode = 0;
  Vector value{};
};

enum class RegisterType : std::uint8_t {
  Temporary = 0,
  Input = 1,
  Constant = 2,
  Texture = 3,
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

struct Operand {
  RegisterType type = RegisterType::Temporary;
  int number = 0;
  bool relative = false;
  std::uint8_t swizzle = 0xE4u;
  std::uint8_t sourceModifier = 0;
  std::uint8_t writeMask = 0xFu;
  std::uint8_t resultModifier = 0;
  std::uint8_t resultShift = 0;
};

[[nodiscard]] RegisterType DecodeRegisterType(std::uint32_t token) {
  return static_cast<RegisterType>(((token >> 28u) & 0x7u) |
                                   ((token >> 8u) & 0x18u));
}

void NormalizeConstantRegister(Operand &operand) {
  if (operand.type == RegisterType::Constant2) {
    operand.type = RegisterType::Constant;
    operand.number += 2048;
  } else if (operand.type == RegisterType::Constant3) {
    operand.type = RegisterType::Constant;
    operand.number += 4096;
  } else if (operand.type == RegisterType::Constant4) {
    operand.type = RegisterType::Constant;
    operand.number += 6144;
  }
}

[[nodiscard]] Operand DecodeDestination(std::uint32_t token) {
  Operand operand;
  operand.type = DecodeRegisterType(token);
  operand.number = static_cast<int>(token & 0x7FFu);
  operand.relative = (token & (1u << 13u)) != 0u;
  operand.writeMask = static_cast<std::uint8_t>((token >> 16u) & 0xFu);
  operand.resultModifier = static_cast<std::uint8_t>((token >> 20u) & 0xFu);
  operand.resultShift = static_cast<std::uint8_t>((token >> 24u) & 0xFu);
  NormalizeConstantRegister(operand);
  if (operand.relative)
    throw std::runtime_error("Software pixel shader: relative destination "
                             "addressing is unsupported.");
  return operand;
}

[[nodiscard]] Operand DecodeSource(const std::vector<std::uint32_t> &tokens,
                                   std::size_t &cursor,
                                   std::uint8_t majorVersion,
                                   RegisterType &relativeType,
                                   int &relativeComponent) {
  if (cursor >= tokens.size())
    throw std::runtime_error("Software pixel shader: missing source operand.");
  const std::uint32_t token = tokens[cursor++];
  Operand operand;
  operand.type = DecodeRegisterType(token);
  operand.number = static_cast<int>(token & 0x7FFu);
  operand.relative = (token & (1u << 13u)) != 0u;
  operand.swizzle = static_cast<std::uint8_t>((token >> 16u) & 0xFFu);
  operand.sourceModifier = static_cast<std::uint8_t>((token >> 24u) & 0xFu);
  NormalizeConstantRegister(operand);

  relativeType = RegisterType::Texture;
  relativeComponent = 0;
  if (operand.relative && majorVersion >= 2u) {
    if (cursor >= tokens.size())
      throw std::runtime_error(
          "Software pixel shader: missing relative-address operand.");
    const std::uint32_t relativeToken = tokens[cursor++];
    relativeType = DecodeRegisterType(relativeToken);
    relativeComponent = static_cast<int>((relativeToken >> 16u) & 0x3u);
  }
  return operand;
}

[[nodiscard]] float Dot(const Vector &left, const Vector &right, int count) {
  float result = 0.0f;
  for (int component = 0; component < count; ++component) {
    result += left[static_cast<std::size_t>(component)] *
              right[static_cast<std::size_t>(component)];
  }
  return result;
}

[[nodiscard]] bool Compare(float left, float right, std::uint8_t control) {
  switch (control) {
  case 1:
    return left > right;
  case 2:
    return left == right;
  case 3:
    return left >= right;
  case 4:
    return left < right;
  case 5:
    return left != right;
  case 6:
    return left <= right;
  default:
    throw std::runtime_error(
        "Software pixel shader: invalid comparison control.");
  }
}

class PixelMachine {
public:
  PixelMachine(const SoftwareShaderProgramEXT &program,
               std::span<const float> floatRegisters,
               std::span<const int> integerRegisters,
               std::span<const unsigned char> booleanRegisters,
               std::span<const SoftwareShaderSemanticValueEXT> inputs,
               const ISoftwarePixelSamplerEXT *sampler,
               const SoftwarePixelShaderBuiltinsEXT *builtins = nullptr,
               std::span<const DerivativeValue> derivativeValues = {},
               std::vector<DerivativeValue> *derivativeSources = nullptr)
      : program_(program), floatRegisters_(floatRegisters),
        integerRegisters_(integerRegisters),
        booleanRegisters_(booleanRegisters), sampler_(sampler), builtins_(builtins),
        derivativeValues_(derivativeValues), derivativeSources_(derivativeSources) {
    if (program.stage != SoftwareShaderStageEXT::Pixel)
      throw std::invalid_argument("Software pixel shader: a vertex program "
                                  "cannot run as a pixel program.");
    for (auto &input : inputRegisters_)
      input = {0.0f, 0.0f, 0.0f, 1.0f};
    for (auto &input : textureRegisters_)
      input = {0.0f, 0.0f, 0.0f, 1.0f};

    for (const SoftwareShaderSemanticValueEXT &value : inputs) {
      if (value.usage == MOJOSHADER_USAGE_COLOR &&
          value.usageIndex < inputRegisters_.size()) {
        inputRegisters_[value.usageIndex] = value.value;
      }
      if (value.usage == MOJOSHADER_USAGE_TEXCOORD &&
          value.usageIndex < textureRegisters_.size()) {
        textureRegisters_[value.usageIndex] = value.value;
      }
    }
    for (const SoftwareShaderSemanticEXT &declaration :
         program.inputSemantics) {
      const auto match = std::find_if(
          inputs.begin(), inputs.end(), [&](const auto &candidate) {
            return candidate.usage == declaration.usage &&
                   candidate.usageIndex == declaration.usageIndex;
          });
      if (match == inputs.end())
        continue;
      if (declaration.registerType == 1u) {
        if (declaration.registerNumber >= inputRegisters_.size())
          throw std::runtime_error(
              "Software pixel shader: input register exceeds the XNA limit.");
        inputRegisters_[declaration.registerNumber] = match->value;
      } else if (declaration.registerType == 3u) {
        if (declaration.registerNumber >= textureRegisters_.size())
          throw std::runtime_error(
              "Software pixel shader: texture register exceeds the XNA limit.");
        textureRegisters_[declaration.registerNumber] = match->value;
      }
    }
  }

  [[nodiscard]] SoftwarePixelShaderResultEXT Execute() {
    struct ConditionalFrame {
      bool parentActive;
      bool condition;
      bool sawElse;
    };
    struct LoopFrame {
      std::size_t bodyBegin;
      std::size_t end;
      std::size_t conditionalDepth;
      int remaining;
      int current;
      int step;
      int previousLoopRegister;
      std::uint16_t endOpcode;
    };
    struct CallFrame {
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
    while (pc < program_.instructions.size()) {
      if (discarded_)
        break;
      const SoftwareShaderInstructionEXT &instruction = program_.instructions[pc];
      if (instruction.opcode == 40u || instruction.opcode == 41u) {
        const bool condition = active && EvaluateConditional(instruction);
        conditionals.push_back({active, condition, false});
        active = active && condition;
        ++pc;
      } else if (instruction.opcode == 42u) {
        if (conditionals.empty() || conditionals.back().sawElse)
          throw std::runtime_error(
              "Software pixel shader: ELSE without a matching IF.");
        conditionals.back().sawElse = true;
        active =
            conditionals.back().parentActive && !conditionals.back().condition;
        ++pc;
      } else if (instruction.opcode == 43u) {
        if (conditionals.empty())
          throw std::runtime_error(
              "Software pixel shader: ENDIF without a matching IF.");
        active = conditionals.back().parentActive;
        conditionals.pop_back();
        ++pc;
      } else if (instruction.opcode == 27u || instruction.opcode == 38u) {
        const std::size_t end = FindMatchingLoop(pc);
        if (!active) {
          pc = end + 1u;
          continue;
        }
        const auto parameters = ReadLoopParameters(instruction);
        const int iterationCount = parameters[0];
        if (iterationCount < 0 || iterationCount > 255)
          throw std::runtime_error(
              "Software pixel shader: loop iteration count exceeds the D3D limit.");
        if (iterationCount == 0) {
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
      } else if (instruction.opcode == 29u || instruction.opcode == 39u) {
        if (loops.empty() || loops.back().end != pc ||
            loops.back().endOpcode != instruction.opcode)
          throw std::runtime_error(
              "Software pixel shader: loop terminator without a matching loop.");
        if (conditionals.size() != loops.back().conditionalDepth)
          throw std::runtime_error(
              "Software pixel shader: conditional crosses a loop boundary.");
        LoopFrame &frame = loops.back();
        --frame.remaining;
        if (frame.remaining > 0) {
          if (frame.endOpcode == 29u) {
            frame.current += frame.step;
            loopRegister_ = frame.current;
          }
          pc = frame.bodyBegin;
        } else {
          if (frame.endOpcode == 29u)
            loopRegister_ = frame.previousLoopRegister;
          loops.pop_back();
          ++pc;
        }
      } else if (instruction.opcode == 44u || instruction.opcode == 45u ||
                 instruction.opcode == 96u) {
        if (!active) {
          ++pc;
          continue;
        }
        if (loops.empty())
          throw std::runtime_error(
              "Software pixel shader: BREAK without an active loop.");
        if (!EvaluateBreak(instruction)) {
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
      } else if (instruction.opcode == 25u || instruction.opcode == 26u) {
        if (!active) {
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
              "Software pixel shader: malformed CALL instruction.");
        if (!takeCall) {
          ++pc;
          continue;
        }
        const auto target = std::find_if(
            labels.begin(), labels.end(),
            [label](const auto &candidate) { return candidate.first == label; });
        if (target == labels.end())
          throw std::runtime_error(
              "Software pixel shader: CALL names an undefined label.");
        if (target->second <= pc)
          throw std::runtime_error(
              "Software pixel shader: only forward CALL targets are legal.");
        const std::size_t maxCallDepth =
            program_.majorVersion >= 3u || program_.minorVersion == 0xFFu ? 4u : 1u;
        if (calls.size() >= maxCallDepth)
          throw std::runtime_error(
              "Software pixel shader: call nesting exceeds the D3D limit.");
        calls.push_back({pc + 1u, conditionals.size(), loops.size()});
        pc = target->second + 1u;
      } else if (instruction.opcode == 28u) {
        if (!active) {
          ++pc;
          continue;
        }
        if (calls.empty()) {
          if (!conditionals.empty() || !loops.empty())
            throw std::runtime_error(
                "Software pixel shader: main RET crosses a flow boundary.");
          pc = program_.instructions.size();
          continue;
        }
        const CallFrame frame = calls.back();
        if (conditionals.size() != frame.conditionalDepth ||
            loops.size() != frame.loopDepth)
          throw std::runtime_error(
              "Software pixel shader: RET crosses a subroutine flow boundary.");
        calls.pop_back();
        pc = frame.returnAddress;
      } else if (instruction.opcode == 30u) {
        throw std::runtime_error(
            "Software pixel shader: LABEL reached without a CALL.");
      } else if (active) {
        ExecuteInstruction(instruction, pc);
        ++pc;
      } else
        ++pc;
    }
    if (!discarded_ && !conditionals.empty())
      throw std::runtime_error(
          "Software pixel shader: IF without a matching ENDIF.");
    if (!discarded_ && !loops.empty())
      throw std::runtime_error(
          "Software pixel shader: loop without a matching terminator.");
    if (!discarded_ && !calls.empty())
      throw std::runtime_error(
          "Software pixel shader: subroutine ended without RET.");
    SoftwarePixelShaderResultEXT result;
    result.discarded = discarded_;
    result.depth = depthOutput_;
    result.depthWritten = depthWritten_;
    if (program_.majorVersion < 2u) {
      if (temporaryWritten_[0]) {
        result.colors[0] = temporaryRegisters_[0];
        result.colorWriteMask = 1u;
      }
    } else {
      result.colors = colorOutputs_;
      for (std::size_t output = 0; output < colorOutputWritten_.size();
           ++output) {
        if (colorOutputWritten_[output])
          result.colorWriteMask |= static_cast<std::uint8_t>(1u << output);
      }
    }
    return result;
  }

private:
  [[nodiscard]] Vector ReadRaw(RegisterType type, int number) const {
    if (number < 0)
      throw std::runtime_error(
          "Software pixel shader: negative register index.");
    const auto checked = [&](const auto &registers,
                             const char *family) -> Vector {
      if (static_cast<std::size_t>(number) >= registers.size()) {
        throw std::runtime_error(std::string("Software pixel shader: ") +
                                 family + " register is out of range.");
      }
      return registers[static_cast<std::size_t>(number)];
    };

    switch (type) {
    case RegisterType::Temporary:
    case RegisterType::TemporaryFloat16:
      return checked(temporaryRegisters_, "temporary");
    case RegisterType::Input:
      return checked(inputRegisters_, "input");
    case RegisterType::Texture:
      return checked(textureRegisters_, "texture");
    case RegisterType::ColorOutput:
      return checked(colorOutputs_, "colour output");
    case RegisterType::DepthOutput:
      return {depthOutput_, depthOutput_, depthOutput_, depthOutput_};
    case RegisterType::Constant: {
      if (number < static_cast<int>(kFloatConstantRegisterCount) &&
          localFloatDefined_[static_cast<std::size_t>(number)]) {
        return localFloatConstants_[static_cast<std::size_t>(number)];
      }
      const std::size_t offset = static_cast<std::size_t>(number) * 4u;
      if (offset + 4u > floatRegisters_.size())
        throw std::runtime_error(
            "Software pixel shader: float constant register is out of range.");
      return {floatRegisters_[offset], floatRegisters_[offset + 1u],
              floatRegisters_[offset + 2u], floatRegisters_[offset + 3u]};
    }
    case RegisterType::IntegerConstant: {
      if (number < static_cast<int>(kIntegerConstantRegisterCount) &&
          localIntegerDefined_[static_cast<std::size_t>(number)]) {
        const auto &value =
            localIntegerConstants_[static_cast<std::size_t>(number)];
        return {static_cast<float>(value[0]), static_cast<float>(value[1]),
                static_cast<float>(value[2]), static_cast<float>(value[3])};
      }
      const std::size_t offset = static_cast<std::size_t>(number) * 4u;
      if (offset + 4u > integerRegisters_.size())
        throw std::runtime_error("Software pixel shader: integer constant "
                                 "register is out of range.");
      return {static_cast<float>(integerRegisters_[offset]),
              static_cast<float>(integerRegisters_[offset + 1u]),
              static_cast<float>(integerRegisters_[offset + 2u]),
              static_cast<float>(integerRegisters_[offset + 3u])};
    }
    case RegisterType::BooleanConstant: {
      if (number < static_cast<int>(kBooleanConstantRegisterCount) &&
          localBooleanDefined_[static_cast<std::size_t>(number)]) {
        const float value =
            localBooleanConstants_[static_cast<std::size_t>(number)] ? 1.0f
                                                                     : 0.0f;
        return {value, value, value, value};
      }
      if (static_cast<std::size_t>(number) >= booleanRegisters_.size())
        throw std::runtime_error("Software pixel shader: Boolean constant "
                                 "register is out of range.");
      const float value =
          booleanRegisters_[static_cast<std::size_t>(number)] != 0u ? 1.0f
                                                                    : 0.0f;
      return {value, value, value, value};
    }
    case RegisterType::Loop: {
      const float value = static_cast<float>(loopRegister_);
      return {value, value, value, value};
    }
    case RegisterType::Predicate:
      return {predicateRegister_[0] ? 1.0f : 0.0f,
              predicateRegister_[1] ? 1.0f : 0.0f,
              predicateRegister_[2] ? 1.0f : 0.0f,
              predicateRegister_[3] ? 1.0f : 0.0f};
    case RegisterType::Miscellaneous:
      if (number == 0)
        return builtins_ != nullptr ? builtins_->position
                                    : Vector{0.0f, 0.0f, 0.0f, 1.0f};
      if (number == 1) {
        const float face = builtins_ != nullptr ? builtins_->face : 1.0f;
        return {face, face, face, face};
      }
      throw std::runtime_error(
          "Software pixel shader: miscellaneous register is out of range.");
    default:
      throw std::runtime_error(
          "Software pixel shader: unsupported source register type " +
          std::to_string(static_cast<unsigned>(type)) + ".");
    }
  }

  [[nodiscard]] Vector ReadSource(const std::vector<std::uint32_t> &tokens,
                                  std::size_t &cursor) {
    RegisterType relativeType;
    int relativeComponent = 0;
    Operand operand = DecodeSource(tokens, cursor, program_.majorVersion,
                                   relativeType, relativeComponent);
    if (operand.relative) {
      const Vector relative = ReadRaw(relativeType, 0);
      operand.number += static_cast<int>(
          relative[static_cast<std::size_t>(relativeComponent)]);
    }
    Vector raw = ReadRaw(operand.type, operand.number);
    if (operand.sourceModifier == 9u || operand.sourceModifier == 10u) {
      const float divisor = raw[operand.sourceModifier == 9u ? 2u : 3u];
      raw[0] = divisor == 0.0f ? 1.0f : raw[0] / divisor;
      raw[1] = divisor == 0.0f ? 1.0f : raw[1] / divisor;
    }
    Vector value{};
    for (int component = 0; component < 4; ++component) {
      const auto selected = static_cast<std::size_t>(
          (operand.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
      value[static_cast<std::size_t>(component)] = raw[selected];
    }
    switch (operand.sourceModifier) {
    case 0:
      break;
    case 1:
      for (float &component : value)
        component = -component;
      break;
    case 2:
      for (float &component : value)
        component -= 0.5f;
      break;
    case 3:
      for (float &component : value)
        component = -(component - 0.5f);
      break;
    case 4:
      for (float &component : value)
        component = 2.0f * (component - 0.5f);
      break;
    case 5:
      for (float &component : value)
        component = -2.0f * (component - 0.5f);
      break;
    case 6:
      for (float &component : value)
        component = 1.0f - component;
      break;
    case 7:
      for (float &component : value)
        component *= 2.0f;
      break;
    case 8:
      for (float &component : value)
        component *= -2.0f;
      break;
    case 9:
    case 10:
      break;
    case 11:
      for (float &component : value)
        component = std::abs(component);
      break;
    case 12:
      for (float &component : value)
        component = -std::abs(component);
      break;
    case 13:
      for (float &component : value)
        component = component == 0.0f ? 1.0f : 0.0f;
      break;
    default:
      throw std::runtime_error(
          "Software pixel shader: unsupported source modifier.");
    }
    return value;
  }

  [[nodiscard]] bool EvaluateConditional(
      const SoftwareShaderInstructionEXT &instruction) {
    std::size_t cursor = 1u;
    const Vector source0 = ReadSource(instruction.tokens, cursor);
    if (instruction.opcode == 40u)
      return source0[0] != 0.0f;
    const Vector source1 = ReadSource(instruction.tokens, cursor);
    return Compare(source0[0], source1[0], instruction.controls);
  }

  void PrepareInstructionPredicate(
      const SoftwareShaderInstructionEXT &instruction) {
    predicateWriteMask_ = 0xFu;
    if (!instruction.predicated)
      return;
    if (program_.majorVersion < 3u || instruction.tokens.size() < 2u)
      throw std::runtime_error(
          "Software pixel shader: malformed predicated instruction.");

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
      throw std::runtime_error(
          "Software pixel shader: invalid instruction predicate.");

    predicateWriteMask_ = 0u;
    for (int component = 0; component < 4; ++component) {
      const auto selected = static_cast<std::size_t>(
          (predicate.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
      bool enabled = predicateRegister_[selected];
      if (predicate.sourceModifier == 13u)
        enabled = !enabled;
      if (enabled)
        predicateWriteMask_ |= static_cast<std::uint8_t>(1u << component);
    }
  }

  [[nodiscard]] int ReadLabel(const std::vector<std::uint32_t> &tokens,
                              std::size_t &cursor) const {
    RegisterType relativeType;
    int relativeComponent = 0;
    const Operand operand = DecodeSource(tokens, cursor, program_.majorVersion,
                                         relativeType, relativeComponent);
    if (operand.type != RegisterType::Label || operand.relative ||
        operand.sourceModifier != 0u)
      throw std::runtime_error(
          "Software pixel shader: CALL/LABEL requires a direct label register.");
    return operand.number;
  }

  [[nodiscard]] std::vector<std::pair<int, std::size_t>>
  CollectLabels() const {
    std::vector<std::pair<int, std::size_t>> labels;
    for (std::size_t index = 0u; index < program_.instructions.size(); ++index) {
      const auto &instruction = program_.instructions[index];
      if (instruction.opcode != 30u)
        continue;
      std::size_t cursor = 1u;
      const int label = ReadLabel(instruction.tokens, cursor);
      if (cursor != instruction.tokens.size())
        throw std::runtime_error(
            "Software pixel shader: malformed LABEL instruction.");
      if (std::find_if(labels.begin(), labels.end(),
                       [label](const auto &candidate) {
                         return candidate.first == label;
                       }) != labels.end())
        throw std::runtime_error(
            "Software pixel shader: duplicate LABEL instruction.");
      labels.emplace_back(label, index);
    }
    return labels;
  }

  [[nodiscard]] std::size_t FindMatchingLoop(std::size_t start) const {
    const std::uint16_t expectedEnd =
        program_.instructions[start].opcode == 27u ? 29u : 39u;
    int depth = 1;
    for (std::size_t cursor = start + 1u; cursor < program_.instructions.size();
         ++cursor) {
      const std::uint16_t opcode = program_.instructions[cursor].opcode;
      if (opcode == 27u || opcode == 38u) {
        ++depth;
      } else if (opcode == 29u || opcode == 39u) {
        --depth;
        if (depth == 0) {
          if (opcode != expectedEnd)
            throw std::runtime_error(
                "Software pixel shader: mismatched loop terminator.");
          return cursor;
        }
      }
    }
    throw std::runtime_error(
        "Software pixel shader: loop without a matching terminator.");
  }

  [[nodiscard]] std::array<int, 3>
  ReadLoopParameters(const SoftwareShaderInstructionEXT &instruction) {
    std::size_t cursor = 1u;
    if (instruction.opcode == 27u)
      static_cast<void>(ReadSource(instruction.tokens, cursor));
    const Vector source = ReadSource(instruction.tokens, cursor);
    if (cursor != instruction.tokens.size())
      throw std::runtime_error(
          "Software pixel shader: malformed loop instruction.");
    if (!std::isfinite(source[0]) || source[0] < 0.0f || source[0] > 255.0f)
      throw std::runtime_error(
          "Software pixel shader: loop iteration count exceeds the D3D limit.");
    if (instruction.opcode == 38u)
      return {static_cast<int>(source[0]), 0, 0};
    if (!std::isfinite(source[1]) || !std::isfinite(source[2]) ||
        !std::isfinite(source[3]) || source[1] < 0.0f || source[1] > 255.0f ||
        source[2] < -128.0f || source[2] > 127.0f || source[3] != 0.0f)
      throw std::runtime_error(
          "Software pixel shader: loop parameters exceed the D3D limits.");
    return {static_cast<int>(source[0]), static_cast<int>(source[1]),
            static_cast<int>(source[2])};
  }

  [[nodiscard]] bool
  EvaluateBreak(const SoftwareShaderInstructionEXT &instruction) {
    if (instruction.opcode == 44u) {
      if (instruction.tokens.size() != 1u)
        throw std::runtime_error(
            "Software pixel shader: malformed BREAK instruction.");
      return true;
    }
    std::size_t cursor = 1u;
    const Vector source0 = ReadSource(instruction.tokens, cursor);
    bool result = source0[0] != 0.0f;
    if (instruction.opcode == 45u) {
      const Vector source1 = ReadSource(instruction.tokens, cursor);
      result = Compare(source0[0], source1[0], instruction.controls);
    }
    if (cursor != instruction.tokens.size())
      throw std::runtime_error(
          "Software pixel shader: malformed conditional BREAK instruction.");
    return result;
  }

  [[nodiscard]] Vector ReadMatrixRow(Operand base, int row) const {
    base.number += row;
    const Vector raw = ReadRaw(base.type, base.number);
    Vector value{};
    for (int component = 0; component < 4; ++component) {
      const auto selected = static_cast<std::size_t>(
          (base.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
      value[static_cast<std::size_t>(component)] = raw[selected];
    }
    if (base.sourceModifier == 1u) {
      for (float &component : value)
        component = -component;
    } else if (base.sourceModifier != 0u) {
      throw std::runtime_error(
          "Software pixel shader: unsupported matrix source modifier.");
    }
    return value;
  }

  void Write(const Operand &destination, Vector value) {
    const std::uint8_t writeMask =
        static_cast<std::uint8_t>(destination.writeMask & predicateWriteMask_);
    if (destination.resultShift >= 1u && destination.resultShift <= 3u) {
      const float factor = static_cast<float>(1u << destination.resultShift);
      for (float &component : value)
        component *= factor;
    } else if (destination.resultShift >= 13u) {
      const float divisor =
          static_cast<float>(1u << (16u - destination.resultShift));
      for (float &component : value)
        component /= divisor;
    } else if (destination.resultShift != 0u) {
      throw std::runtime_error(
          "Software pixel shader: unsupported destination shift.");
    }
    if ((destination.resultModifier & 0x1u) != 0u)
      for (float &component : value)
        component = std::clamp(component, 0.0f, 1.0f);

    Vector *target = nullptr;
    bool *written = nullptr;
    switch (destination.type) {
    case RegisterType::Temporary:
    case RegisterType::TemporaryFloat16:
      if (destination.number >= 0 &&
          static_cast<std::size_t>(destination.number) <
              temporaryRegisters_.size()) {
        target =
            &temporaryRegisters_[static_cast<std::size_t>(destination.number)];
        written =
            &temporaryWritten_[static_cast<std::size_t>(destination.number)];
      }
      break;
    case RegisterType::Texture:
      if (destination.number >= 0 &&
          static_cast<std::size_t>(destination.number) <
              textureRegisters_.size())
        target =
            &textureRegisters_[static_cast<std::size_t>(destination.number)];
      break;
    case RegisterType::ColorOutput:
      if (destination.number >= 0 &&
          static_cast<std::size_t>(destination.number) < colorOutputs_.size()) {
        target = &colorOutputs_[static_cast<std::size_t>(destination.number)];
        written =
            &colorOutputWritten_[static_cast<std::size_t>(destination.number)];
      }
      break;
    case RegisterType::DepthOutput:
      if ((writeMask & 0x1u) != 0u) {
        depthOutput_ = value[0];
        depthWritten_ = true;
      }
      return;
    case RegisterType::Predicate:
      for (int component = 0; component < 4; ++component) {
        if ((writeMask & (1u << component)) != 0u)
          predicateRegister_[static_cast<std::size_t>(component)] =
              value[static_cast<std::size_t>(component)] != 0.0f;
      }
      return;
    default:
      break;
    }
    if (target == nullptr)
      throw std::runtime_error(
          "Software pixel shader: unsupported destination register type " +
          std::to_string(static_cast<unsigned>(destination.type)) + ".");
    for (int component = 0; component < 4; ++component) {
      if ((writeMask & (1u << component)) != 0u) {
        (*target)[static_cast<std::size_t>(component)] =
            value[static_cast<std::size_t>(component)];
      }
    }
    if (written != nullptr && writeMask != 0u)
      *written = true;
  }

  void DefineFloat(const std::vector<std::uint32_t> &tokens) {
    if (tokens.size() != 6u)
      throw std::runtime_error(
          "Software pixel shader: malformed DEF instruction.");
    const Operand destination = DecodeDestination(tokens[1]);
    if (destination.type != RegisterType::Constant || destination.number < 0 ||
        static_cast<std::size_t>(destination.number) >=
            localFloatConstants_.size())
      throw std::runtime_error(
          "Software pixel shader: invalid DEF destination.");
    auto &value =
        localFloatConstants_[static_cast<std::size_t>(destination.number)];
    for (int component = 0; component < 4; ++component) {
      value[static_cast<std::size_t>(component)] = std::bit_cast<float>(
          tokens[static_cast<std::size_t>(component) + 2u]);
    }
    localFloatDefined_[static_cast<std::size_t>(destination.number)] = true;
  }

  void DefineInteger(const std::vector<std::uint32_t> &tokens) {
    if (tokens.size() != 6u)
      throw std::runtime_error(
          "Software pixel shader: malformed DEFI instruction.");
    const Operand destination = DecodeDestination(tokens[1]);
    if (destination.type != RegisterType::IntegerConstant ||
        destination.number < 0 ||
        static_cast<std::size_t>(destination.number) >=
            localIntegerConstants_.size())
      throw std::runtime_error(
          "Software pixel shader: invalid DEFI destination.");
    auto &value =
        localIntegerConstants_[static_cast<std::size_t>(destination.number)];
    for (int component = 0; component < 4; ++component)
      value[static_cast<std::size_t>(component)] = static_cast<std::int32_t>(
          tokens[static_cast<std::size_t>(component) + 2u]);
    localIntegerDefined_[static_cast<std::size_t>(destination.number)] = true;
  }

  void DefineBoolean(const std::vector<std::uint32_t> &tokens) {
    if (tokens.size() != 3u)
      throw std::runtime_error(
          "Software pixel shader: malformed DEFB instruction.");
    const Operand destination = DecodeDestination(tokens[1]);
    if (destination.type != RegisterType::BooleanConstant ||
        destination.number < 0 ||
        static_cast<std::size_t>(destination.number) >=
            localBooleanConstants_.size())
      throw std::runtime_error(
          "Software pixel shader: invalid DEFB destination.");
    localBooleanConstants_[static_cast<std::size_t>(destination.number)] =
        tokens[2] != 0u;
    localBooleanDefined_[static_cast<std::size_t>(destination.number)] = true;
  }

  [[nodiscard]] SoftwareShaderSamplerTypeEXT
  SamplerType(int samplerRegister) const {
    const auto declaration = std::find_if(
        program_.samplers.begin(), program_.samplers.end(),
        [samplerRegister](const SoftwareShaderSamplerEXT &candidate) {
          return candidate.registerNumber == samplerRegister;
        });
    if (declaration != program_.samplers.end())
      return declaration->type;
    return program_.majorVersion < 2u
               ? SoftwareShaderSamplerTypeEXT::Texture2D
               : SoftwareShaderSamplerTypeEXT::Unknown;
  }

  [[nodiscard]] Operand DecodeSampler(
      const std::vector<std::uint32_t> &tokens, std::size_t &cursor) const {
    RegisterType relativeType;
    int relativeComponent = 0;
    const Operand operand = DecodeSource(tokens, cursor, program_.majorVersion,
                                         relativeType, relativeComponent);
    if (operand.relative || operand.type != RegisterType::Sampler ||
        operand.number < 0 || operand.number >= 16)
      throw std::runtime_error(
          "Software pixel shader: invalid sampler source operand.");
    return operand;
  }

  [[nodiscard]] Vector Sample(const Operand &coordinateOperand,
                              const Vector &coordinate, int samplerRegister,
                              SoftwareTextureLodModeEXT lodMode, float lod,
                              const Vector &gradientX = {},
                              const Vector &gradientY = {},
                              const std::array<std::uint8_t, 3> &coordinateComponents =
                                  {0u, 1u, 2u},
                              const std::array<std::int8_t, 2> &legacyMatrix2Rows =
                                  {-1, -1}) const {
    if (sampler_ == nullptr)
      throw std::runtime_error(
          "Software pixel shader: texture instruction has no sampler provider.");
    if (coordinateOperand.number < 0 || coordinateOperand.number >= 16)
      throw std::runtime_error(
          "Software pixel shader: texture-coordinate register is out of range.");
    SoftwarePixelSampleRequestEXT request;
    request.samplerRegister = static_cast<std::uint8_t>(samplerRegister);
    request.coordinateRegister =
        static_cast<std::uint8_t>(coordinateOperand.number);
    request.coordinateComponents = coordinateComponents;
    request.legacyMatrix2RowRegisters = legacyMatrix2Rows;
    request.samplerType = SamplerType(samplerRegister);
    request.coordinate = coordinate;
    request.lodMode = lodMode;
    request.lod = lod;
    request.gradientX = gradientX;
    request.gradientY = gradientY;
    return sampler_->SampleEXT(request);
  }

  void ExecuteTextureInstruction(
      const SoftwareShaderInstructionEXT &instruction) {
    const auto &tokens = instruction.tokens;
    const std::size_t predicateTokens = instruction.predicated ? 1u : 0u;
    if (program_.majorVersion < 2u) {
      if (program_.minorVersion == 4u) {
        if (tokens.size() != 3u + predicateTokens)
          throw std::runtime_error(
              "Software pixel shader: malformed ps_1_4 TEX instruction.");
        const Operand destination = DecodeDestination(tokens[1]);
        std::size_t cursor = 2u;
        RegisterType relativeType;
        int relativeComponent = 0;
        const Operand coordinateOperand = DecodeSource(
            tokens, cursor, program_.majorVersion, relativeType,
            relativeComponent);
        const Vector coordinate = ReadSourceFromOperand(coordinateOperand);
        Write(destination,
              Sample(coordinateOperand, coordinate, destination.number,
                     SoftwareTextureLodModeEXT::Implicit, 0.0f));
        return;
      }
      if (tokens.size() != 2u + predicateTokens)
        throw std::runtime_error(
            "Software pixel shader: malformed Shader Model 1 TEX instruction.");
      const Operand destination = DecodeDestination(tokens[1]);
      const Vector coordinate = ReadRaw(destination.type, destination.number);
      Write(destination,
            Sample(destination, coordinate, destination.number,
                   SoftwareTextureLodModeEXT::Implicit, 0.0f));
      return;
    }

    if (tokens.size() != 4u + predicateTokens)
      throw std::runtime_error(
          "Software pixel shader: malformed TEX instruction.");
    const Operand destination = DecodeDestination(tokens[1]);
    std::size_t cursor = 2u;
    RegisterType relativeType;
    int relativeComponent = 0;
    const Operand coordinateOperand = DecodeSource(
        tokens, cursor, program_.majorVersion, relativeType,
        relativeComponent);
    Vector coordinate = ReadSourceFromOperand(coordinateOperand);
    const Operand sampler = DecodeSampler(tokens, cursor);
    float lod = 0.0f;
    if (instruction.controls == 1u) {
      const float divisor = coordinate[3];
      for (int component = 0; component < 3; ++component)
        coordinate[static_cast<std::size_t>(component)] /= divisor;
    } else if (instruction.controls == 2u) {
      lod = coordinate[3];
    } else if (instruction.controls != 0u) {
      throw std::runtime_error(
          "Software pixel shader: unsupported TEX instruction control.");
    }
    Write(destination,
          Sample(coordinateOperand, coordinate, sampler.number,
                 SoftwareTextureLodModeEXT::Implicit, lod));
  }

  [[nodiscard]] Vector ReadSourceFromOperand(const Operand &operand) const {
    if (operand.relative)
      throw std::runtime_error(
          "Software pixel shader: relative texture coordinates require SOFTWARE-165.");
    Vector raw = ReadRaw(operand.type, operand.number);
    if (operand.sourceModifier == 9u || operand.sourceModifier == 10u) {
      const float divisor = raw[operand.sourceModifier == 9u ? 2u : 3u];
      raw[0] = divisor == 0.0f ? 1.0f : raw[0] / divisor;
      raw[1] = divisor == 0.0f ? 1.0f : raw[1] / divisor;
    }
    Vector value{};
    for (int component = 0; component < 4; ++component) {
      const auto selected = static_cast<std::size_t>(
          (operand.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
      value[static_cast<std::size_t>(component)] = raw[selected];
    }
    if (operand.sourceModifier == 1u)
      for (float &component : value)
        component = -component;
    else if (operand.sourceModifier != 0u && operand.sourceModifier != 9u &&
             operand.sourceModifier != 10u)
      throw std::runtime_error(
          "Software pixel shader: texture coordinate uses an unsupported modifier.");
    return value;
  }

  void ExecuteTextureLodInstruction(
      const SoftwareShaderInstructionEXT &instruction) {
    const auto &tokens = instruction.tokens;
    const std::size_t expected =
        (instruction.opcode == 93u ? 6u : 4u) + (instruction.predicated ? 1u : 0u);
    if (tokens.size() != expected)
      throw std::runtime_error(
          "Software pixel shader: malformed explicit-LOD texture instruction.");
    const Operand destination = DecodeDestination(tokens[1]);
    std::size_t cursor = 2u;
    RegisterType relativeType;
    int relativeComponent = 0;
    const Operand coordinateOperand = DecodeSource(
        tokens, cursor, program_.majorVersion, relativeType,
        relativeComponent);
    const Vector coordinate = ReadSourceFromOperand(coordinateOperand);
    const Operand sampler = DecodeSampler(tokens, cursor);
    if (instruction.opcode == 95u) {
      Write(destination,
            Sample(coordinateOperand, coordinate, sampler.number,
                   SoftwareTextureLodModeEXT::Explicit, coordinate[3]));
      return;
    }
    const Vector gradientX = ReadSource(tokens, cursor);
    const Vector gradientY = ReadSource(tokens, cursor);
    Write(destination,
          Sample(coordinateOperand, coordinate, sampler.number,
                 SoftwareTextureLodModeEXT::Gradients, 0.0f, gradientX,
                 gradientY));
  }

  void ExecuteInstruction(const SoftwareShaderInstructionEXT &instruction,
                          std::size_t instructionIndex) {
    const auto &tokens = instruction.tokens;
    if (tokens.empty())
      throw std::runtime_error("Software pixel shader: empty instruction.");
    if (instruction.opcode == 0u || instruction.opcode == 31u ||
        instruction.opcode == 0xFFFEu || instruction.opcode == 0xFFFDu)
      return;
    PrepareInstructionPredicate(instruction);
    if (instruction.opcode == 81u) {
      DefineFloat(tokens);
      return;
    }
    if (instruction.opcode == 47u) {
      DefineBoolean(tokens);
      return;
    }
    if (instruction.opcode == 48u) {
      DefineInteger(tokens);
      return;
    }
    if (instruction.opcode == 64u) {
      const Operand destination = DecodeDestination(tokens[1]);
      if (program_.majorVersion == 1u && program_.minorVersion == 4u) {
        if (tokens.size() != 3u)
          throw std::runtime_error(
              "Software pixel shader: malformed ps_1_4 TEXCRD instruction.");
        std::size_t cursor = 2u;
        Write(destination, ReadSource(tokens, cursor));
      } else {
        if (tokens.size() != 2u + (instruction.predicated ? 1u : 0u))
          throw std::runtime_error(
              "Software pixel shader: unsupported TEXCOORD instruction shape.");
        Write(destination, ReadRaw(destination.type, destination.number));
      }
      return;
    }
    if (instruction.opcode == 65u) {
      if (tokens.size() != 2u + (instruction.predicated ? 1u : 0u))
        throw std::runtime_error(
            "Software pixel shader: malformed TEXKILL instruction.");
      const Operand source = DecodeDestination(tokens[1]);
      const Vector value = ReadRaw(source.type, source.number);
      discarded_ = ((predicateWriteMask_ & 0x1u) != 0u && value[0] < 0.0f) ||
                   ((predicateWriteMask_ & 0x2u) != 0u && value[1] < 0.0f) ||
                   ((predicateWriteMask_ & 0x4u) != 0u && value[2] < 0.0f);
      return;
    }
    if (instruction.opcode == 66u) {
      ExecuteTextureInstruction(instruction);
      return;
    }
    if (instruction.opcode == 69u || instruction.opcode == 70u) {
      if (program_.majorVersion != 1u || program_.minorVersion >= 4u ||
          program_.minorVersion < 1u ||
          (instruction.opcode == 70u && program_.minorVersion < 2u) ||
          tokens.size() != 3u)
        throw std::runtime_error(
            "Software pixel shader: malformed TEXREG2 component remap.");
      const Operand destination = DecodeDestination(tokens[1]);
      if (destination.type != RegisterType::Texture)
        throw std::runtime_error(
            "Software pixel shader: TEXREG2 destination is not a texture register.");
      std::size_t cursor = 2u;
      RegisterType relativeType;
      int relativeComponent = 0;
      const Operand source = DecodeSource(tokens, cursor, program_.majorVersion,
                                          relativeType, relativeComponent);
      if (source.relative || source.type != RegisterType::Texture ||
          source.sourceModifier != 0u || source.swizzle != 0xE4u ||
          cursor != tokens.size())
        throw std::runtime_error(
            "Software pixel shader: TEXREG2 source is not an unsigned direct texture register.");
      const Vector value = ReadRaw(source.type, source.number);
      const std::array<std::uint8_t, 3> components = instruction.opcode == 69u
          ? std::array<std::uint8_t, 3>{3u, 0u, 2u}
          : std::array<std::uint8_t, 3>{1u, 2u, 0u};
      const Vector coordinate = {
          value[components[0]], value[components[1]], value[components[2]], 1.0f};
      Write(destination,
            Sample(source, coordinate, destination.number,
                   SoftwareTextureLodModeEXT::Implicit, 0.0f, {}, {}, components));
      return;
    }
    if (instruction.opcode == 71u) {
      if (program_.majorVersion != 1u || program_.minorVersion >= 4u ||
          tokens.size() != 3u || legacyTextureMatrix2Source_ >= 0)
        throw std::runtime_error(
            "Software pixel shader: malformed TEXM3X2PAD sequence.");
      const Operand destination = DecodeDestination(tokens[1]);
      std::size_t cursor = 2u;
      RegisterType relativeType;
      int relativeComponent = 0;
      const Operand source = DecodeSource(tokens, cursor, program_.majorVersion,
                                          relativeType, relativeComponent);
      if (destination.type != RegisterType::Texture || source.relative ||
          source.type != RegisterType::Texture || source.sourceModifier != 0u ||
          source.swizzle != 0xE4u || destination.number <= source.number ||
          cursor != tokens.size())
        throw std::runtime_error(
            "Software pixel shader: invalid TEXM3X2PAD register sequence.");
      legacyTextureMatrix2Dot_ =
          Dot(ReadRaw(destination.type, destination.number),
              ReadRaw(source.type, source.number), 3);
      legacyTextureMatrix2Source_ = source.number;
      legacyTextureMatrix2PadDestination_ = destination.number;
      return;
    }
    if (instruction.opcode == 72u) {
      if (program_.majorVersion != 1u || program_.minorVersion >= 4u ||
          tokens.size() != 3u || legacyTextureMatrix2Source_ < 0)
        throw std::runtime_error(
            "Software pixel shader: TEXM3X2TEX has no matching pad.");
      const Operand destination = DecodeDestination(tokens[1]);
      std::size_t cursor = 2u;
      RegisterType relativeType;
      int relativeComponent = 0;
      const Operand source = DecodeSource(tokens, cursor, program_.majorVersion,
                                          relativeType, relativeComponent);
      if (destination.type != RegisterType::Texture || source.relative ||
          source.type != RegisterType::Texture || source.sourceModifier != 0u ||
          source.swizzle != 0xE4u || source.number != legacyTextureMatrix2Source_ ||
          destination.number != legacyTextureMatrix2PadDestination_ + 1 ||
          cursor != tokens.size())
        throw std::runtime_error(
            "Software pixel shader: invalid TEXM3X2TEX register sequence.");
      const Vector vector = ReadRaw(source.type, source.number);
      const Vector row = ReadRaw(destination.type, destination.number);
      const Vector coordinate = {
          legacyTextureMatrix2Dot_, Dot(row, vector, 3), 0.0f, 1.0f};
      const std::array<std::int8_t, 2> matrixRows = {
          static_cast<std::int8_t>(legacyTextureMatrix2PadDestination_),
          static_cast<std::int8_t>(destination.number)};
      legacyTextureMatrix2Source_ = -1;
      legacyTextureMatrix2PadDestination_ = -1;
      Write(destination,
            Sample(source, coordinate, destination.number,
                   SoftwareTextureLodModeEXT::Implicit, 0.0f, {}, {},
                   {0u, 1u, 2u}, matrixRows));
      return;
    }
    if (instruction.opcode == 73u) {
      if (tokens.size() != 3u || legacyTextureMatrixDotCount_ >= 2u)
        throw std::runtime_error(
            "Software pixel shader: malformed TEXM3X3PAD sequence.");
      const Operand destination = DecodeDestination(tokens[1]);
      if (destination.type != RegisterType::Texture)
        throw std::runtime_error(
            "Software pixel shader: TEXM3X3PAD destination is not a texture register.");
      std::size_t cursor = 2u;
      RegisterType relativeType;
      int relativeComponent = 0;
      const Operand source = DecodeSource(tokens, cursor, program_.majorVersion,
                                          relativeType, relativeComponent);
      if (source.relative || source.type != RegisterType::Texture ||
          cursor != tokens.size())
        throw std::runtime_error(
            "Software pixel shader: TEXM3X3PAD source is not a direct texture register.");
      legacyTextureMatrixDots_[legacyTextureMatrixDotCount_++] =
          Dot(ReadRaw(destination.type, destination.number),
              ReadSourceFromOperand(source), 3);
      return;
    }
    if (instruction.opcode == 86u) {
      if (tokens.size() != 3u || legacyTextureMatrixDotCount_ != 2u)
        throw std::runtime_error(
            "Software pixel shader: TEXM3X3 has no matching pad pair.");
      const Operand destination = DecodeDestination(tokens[1]);
      if (destination.type != RegisterType::Texture)
        throw std::runtime_error(
            "Software pixel shader: TEXM3X3 destination is not a texture register.");
      std::size_t cursor = 2u;
      RegisterType relativeType;
      int relativeComponent = 0;
      const Operand source = DecodeSource(tokens, cursor, program_.majorVersion,
                                          relativeType, relativeComponent);
      if (source.relative || source.type != RegisterType::Texture ||
          cursor != tokens.size())
        throw std::runtime_error(
            "Software pixel shader: TEXM3X3 source is not a direct texture register.");
      const Vector row = ReadRaw(destination.type, destination.number);
      const Vector vector = ReadSourceFromOperand(source);
      Write(destination, {legacyTextureMatrixDots_[0], legacyTextureMatrixDots_[1],
                          Dot(row, vector, 3), 1.0f});
      legacyTextureMatrixDotCount_ = 0u;
      return;
    }
    if (instruction.opcode == 93u || instruction.opcode == 95u) {
      ExecuteTextureLodInstruction(instruction);
      return;
    }

    if (tokens.size() < 3u)
      throw std::runtime_error("Software pixel shader: malformed instruction.");
    const Operand destination = DecodeDestination(tokens[1]);
    std::size_t cursor = 2u;
    const Vector source0 = ReadSource(tokens, cursor);
    Vector source1{};
    Vector source2{};
    Vector result{};
    if (instruction.opcode == 91u || instruction.opcode == 92u) {
      if (derivativeSources_ == nullptr)
        throw std::runtime_error(
            "Software pixel shader: DSX/DSY requires 2x2 quad execution.");
      derivativeSources_->push_back(
          DerivativeValue{instructionIndex, instruction.opcode, source0});
      if (derivativeCursor_ < derivativeValues_.size()) {
        const DerivativeValue &value = derivativeValues_[derivativeCursor_];
        if (value.instruction != instructionIndex ||
            value.opcode != instruction.opcode)
          throw std::runtime_error(
              "Software pixel shader: derivative control flow changed between quad passes.");
        result = value.value;
      }
      ++derivativeCursor_;
      Write(destination, result);
      return;
    }
    switch (instruction.opcode) {
    case 2:
    case 3:
    case 5:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 17:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 32:
    case 33:
    case 94:
      source1 = ReadSource(tokens, cursor);
      break;
    case 4:
    case 18:
    case 34:
    case 80:
    case 88:
    case 90:
      source1 = ReadSource(tokens, cursor);
      source2 = ReadSource(tokens, cursor);
      break;
    case 37:
      if (program_.majorVersion < 3u) {
        source1 = ReadSource(tokens, cursor);
        source2 = ReadSource(tokens, cursor);
      }
      break;
    default:
      break;
    }

    switch (instruction.opcode) {
    case 1:
      result = source0;
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
    case 6: {
      const float reciprocal = source0[0] == 0.0f
                                   ? std::numeric_limits<float>::max()
                                   : 1.0f / source0[0];
      result.fill(reciprocal);
      break;
    }
    case 7: {
      const float reciprocalSquareRoot =
          source0[0] == 0.0f ? std::numeric_limits<float>::max()
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
    case 78:
      for (int i = 0; i < 4; ++i)
        result[i] = std::exp2(source0[i]);
      break;
    case 15:
    case 79:
      for (int i = 0; i < 4; ++i)
        result[i] = std::log2(source0[i]);
      break;
    case 16: {
      const float power = std::clamp(source0[3], -127.9961f, 127.9961f);
      result = {1.0f, 0.0f, 0.0f, 1.0f};
      if (source0[0] > 0.0f) {
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
    case 24: {
      std::size_t sourceCursor = 2u;
      const Vector vector = ReadSource(tokens, sourceCursor);
      RegisterType relativeType;
      int relativeComponent = 0;
      Operand rowBase = DecodeSource(tokens, sourceCursor, program_.majorVersion,
                                     relativeType, relativeComponent);
      if (rowBase.relative) {
        const Vector relative = ReadRaw(relativeType, 0);
        rowBase.number += static_cast<int>(
            relative[static_cast<std::size_t>(relativeComponent)]);
      }
      const int sourceComponents =
          instruction.opcode == 20u || instruction.opcode == 21u ? 4 : 3;
      const int rows = instruction.opcode == 20u || instruction.opcode == 22u
                           ? 4
                           : instruction.opcode == 24u ? 2 : 3;
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
    case 36: {
      float squaredLength = 0.0f;
      for (int component = 0; component < 4; ++component) {
        if ((destination.writeMask & (1u << component)) != 0u)
          squaredLength += source0[component] * source0[component];
      }
      const float length = std::sqrt(squaredLength);
      result.fill(0.0f);
      if (length != 0.0f)
        for (int component = 0; component < 4; ++component)
          result[component] = source0[component] / length;
      break;
    }
    case 37:
      result[0] = std::cos(source0[0]);
      result[1] = std::sin(source0[0]);
      break;
    case 80:
      for (int i = 0; i < 4; ++i)
        result[i] = source0[i] > 0.5f ? source1[i] : source2[i];
      break;
    case 88:
      for (int i = 0; i < 4; ++i)
        result[i] = source0[i] >= 0.0f ? source1[i] : source2[i];
      break;
    case 90:
      result.fill(source0[0] * source1[0] + source0[1] * source1[1] + source2[0]);
      break;
    case 94:
      for (int i = 0; i < 4; ++i)
        result[i] = Compare(source0[i], source1[i], instruction.controls)
                        ? 1.0f
                        : 0.0f;
      break;
    default:
      throw std::runtime_error("Software pixel shader: unsupported opcode " +
                               std::to_string(instruction.opcode) + ".");
    }
    Write(destination, result);
  }

  const SoftwareShaderProgramEXT &program_;
  std::span<const float> floatRegisters_;
  std::span<const int> integerRegisters_;
  std::span<const unsigned char> booleanRegisters_;
  const ISoftwarePixelSamplerEXT *sampler_ = nullptr;
  const SoftwarePixelShaderBuiltinsEXT *builtins_ = nullptr;
  std::span<const DerivativeValue> derivativeValues_;
  std::vector<DerivativeValue> *derivativeSources_ = nullptr;
  std::size_t derivativeCursor_ = 0;
  std::array<Vector, kTemporaryRegisterCount> temporaryRegisters_{};
  std::array<bool, kTemporaryRegisterCount> temporaryWritten_{};
  std::array<Vector, kInputRegisterCount> inputRegisters_{};
  std::array<Vector, kTextureRegisterCount> textureRegisters_{};
  std::array<Vector, kColorOutputRegisterCount> colorOutputs_{};
  std::array<bool, kColorOutputRegisterCount> colorOutputWritten_{};
  float depthOutput_ = 0.0f;
  bool depthWritten_ = false;
  bool discarded_ = false;
  std::array<float, 2> legacyTextureMatrixDots_{};
  std::size_t legacyTextureMatrixDotCount_ = 0u;
  float legacyTextureMatrix2Dot_ = 0.0f;
  int legacyTextureMatrix2Source_ = -1;
  int legacyTextureMatrix2PadDestination_ = -1;
  std::array<bool, 4> predicateRegister_{};
  std::uint8_t predicateWriteMask_ = 0xFu;
  int loopRegister_ = 0;
  std::array<Vector, kFloatConstantRegisterCount> localFloatConstants_{};
  std::array<bool, kFloatConstantRegisterCount> localFloatDefined_{};
  std::array<std::array<int, 4>, kIntegerConstantRegisterCount>
      localIntegerConstants_{};
  std::array<bool, kIntegerConstantRegisterCount> localIntegerDefined_{};
  std::array<bool, kBooleanConstantRegisterCount> localBooleanConstants_{};
  std::array<bool, kBooleanConstantRegisterCount> localBooleanDefined_{};
};
} // namespace

SoftwarePixelShaderResultEXT ExecuteSoftwarePixelShaderEXT(
    const SoftwareShaderProgramEXT &program,
    std::span<const float> floatRegisters,
    std::span<const int> integerRegisters,
    std::span<const unsigned char> booleanRegisters,
    std::span<const SoftwareShaderSemanticValueEXT> inputs,
    const ISoftwarePixelSamplerEXT *sampler,
    const SoftwarePixelShaderBuiltinsEXT *builtins) {
  return PixelMachine(program, floatRegisters, integerRegisters,
                      booleanRegisters, inputs, sampler, builtins)
      .Execute();
}

std::array<SoftwarePixelShaderResultEXT, 4> ExecuteSoftwarePixelShaderQuadEXT(
    const SoftwareShaderProgramEXT &program,
    std::span<const float> floatRegisters,
    std::span<const int> integerRegisters,
    std::span<const unsigned char> booleanRegisters,
    const std::array<std::span<const SoftwareShaderSemanticValueEXT>, 4> &inputs,
    const ISoftwarePixelSamplerEXT *sampler,
    const std::array<SoftwarePixelShaderBuiltinsEXT, 4> *builtins) {
  std::array<std::vector<DerivativeValue>, 4> values;
  std::array<std::vector<DerivativeValue>, 4> sources;
  std::array<SoftwarePixelShaderResultEXT, 4> results;
  std::size_t maximumPasses = 2u;
  for (std::size_t pass = 0; pass < maximumPasses; ++pass) {
    for (std::size_t lane = 0; lane < 4u; ++lane) {
      sources[lane].clear();
      const SoftwarePixelShaderBuiltinsEXT *laneBuiltins =
          builtins != nullptr ? &(*builtins)[lane] : nullptr;
      results[lane] = PixelMachine(program, floatRegisters, integerRegisters,
                                   booleanRegisters, inputs[lane], sampler, laneBuiltins,
                                   values[lane], &sources[lane])
                          .Execute();
    }

    const std::size_t count = sources[0].size();
    for (std::size_t lane = 1; lane < 4u; ++lane) {
      if (sources[lane].size() != count)
        throw std::runtime_error(
            "Software pixel shader: DSX/DSY occurs in non-uniform quad control flow.");
    }
    if (pass == 0u)
      maximumPasses = std::max<std::size_t>(2u, count + 2u);

    std::array<std::vector<DerivativeValue>, 4> next;
    for (auto &lane : next)
      lane.reserve(count);
    for (std::size_t occurrence = 0; occurrence < count; ++occurrence) {
      const std::size_t instruction = sources[0][occurrence].instruction;
      const std::uint16_t opcode = sources[0][occurrence].opcode;
      for (std::size_t lane = 1; lane < 4u; ++lane) {
        if (sources[lane][occurrence].instruction != instruction ||
            sources[lane][occurrence].opcode != opcode)
          throw std::runtime_error(
              "Software pixel shader: DSX/DSY occurs in non-uniform quad control flow.");
      }
      const auto difference = [&](std::size_t high, std::size_t low) {
        Vector result{};
        for (std::size_t component = 0; component < result.size(); ++component)
          result[component] = sources[high][occurrence].value[component] -
                              sources[low][occurrence].value[component];
        return result;
      };
      std::array<Vector, 4> derivative{};
      if (opcode == 91u) {
        derivative[0] = derivative[1] = difference(1u, 0u);
        derivative[2] = derivative[3] = difference(3u, 2u);
      } else {
        derivative[0] = derivative[2] = difference(2u, 0u);
        derivative[1] = derivative[3] = difference(3u, 1u);
      }
      for (std::size_t lane = 0; lane < 4u; ++lane)
        next[lane].push_back(DerivativeValue{instruction, opcode, derivative[lane]});
    }

    const auto sameValues = [](const auto &left, const auto &right) {
      if (left.size() != right.size())
        return false;
      for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].instruction != right[index].instruction ||
            left[index].opcode != right[index].opcode)
          return false;
        for (std::size_t component = 0; component < 4u; ++component) {
          if (std::bit_cast<std::uint32_t>(left[index].value[component]) !=
              std::bit_cast<std::uint32_t>(right[index].value[component]))
            return false;
        }
      }
      return true;
    };
    bool stable = true;
    for (std::size_t lane = 0; lane < 4u; ++lane)
      stable = stable && sameValues(values[lane], next[lane]);
    if (stable)
      return results;
    values = std::move(next);
  }
  throw std::runtime_error(
      "Software pixel shader: derivative evaluation did not converge.");
}
} // namespace CNA::Internal::Renderers::Software

#endif // CNA_SOFTWARE_COMPILED_EFFECTS
