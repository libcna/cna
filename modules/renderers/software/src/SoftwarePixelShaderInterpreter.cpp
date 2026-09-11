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

class PixelMachine {
public:
  PixelMachine(const SoftwareShaderProgramEXT &program,
               std::span<const float> floatRegisters,
               std::span<const int> integerRegisters,
               std::span<const unsigned char> booleanRegisters,
               std::span<const SoftwareShaderSemanticValueEXT> inputs,
               const ISoftwarePixelSamplerEXT *sampler)
      : program_(program), floatRegisters_(floatRegisters),
        integerRegisters_(integerRegisters),
        booleanRegisters_(booleanRegisters), sampler_(sampler) {
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
    for (const SoftwareShaderInstructionEXT &instruction :
         program_.instructions) {
      if (discarded_)
        break;
      ExecuteInstruction(instruction);
    }
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
      if (static_cast<std::size_t>(number) >= booleanRegisters_.size())
        throw std::runtime_error("Software pixel shader: Boolean constant "
                                 "register is out of range.");
      const float value =
          booleanRegisters_[static_cast<std::size_t>(number)] != 0u ? 1.0f
                                                                    : 0.0f;
      return {value, value, value, value};
    }
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
    const Vector raw = ReadRaw(operand.type, operand.number);
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
    case 9: {
      const float divisor = value[2];
      for (float &component : value)
        component /= divisor;
      break;
    }
    case 10: {
      const float divisor = value[3];
      for (float &component : value)
        component /= divisor;
      break;
    }
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

  void Write(const Operand &destination, Vector value) {
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
      depthOutput_ = value[0];
      depthWritten_ = true;
      return;
    default:
      break;
    }
    if (target == nullptr)
      throw std::runtime_error(
          "Software pixel shader: unsupported destination register type " +
          std::to_string(static_cast<unsigned>(destination.type)) + ".");
    for (int component = 0; component < 4; ++component) {
      if ((destination.writeMask & (1u << component)) != 0u) {
        (*target)[static_cast<std::size_t>(component)] =
            value[static_cast<std::size_t>(component)];
      }
    }
    if (written != nullptr)
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
                              const Vector &gradientY = {}) const {
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
    if (program_.majorVersion < 2u) {
      if (program_.minorVersion == 4u) {
        if (tokens.size() != 3u)
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
      if (tokens.size() != 2u)
        throw std::runtime_error(
            "Software pixel shader: malformed Shader Model 1 TEX instruction.");
      const Operand destination = DecodeDestination(tokens[1]);
      const Vector coordinate = ReadRaw(destination.type, destination.number);
      Write(destination,
            Sample(destination, coordinate, destination.number,
                   SoftwareTextureLodModeEXT::Implicit, 0.0f));
      return;
    }

    if (tokens.size() != 4u)
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
    const Vector raw = ReadRaw(operand.type, operand.number);
    Vector value{};
    for (int component = 0; component < 4; ++component) {
      const auto selected = static_cast<std::size_t>(
          (operand.swizzle >> static_cast<unsigned>(component * 2)) & 0x3u);
      value[static_cast<std::size_t>(component)] = raw[selected];
    }
    if (operand.sourceModifier == 1u)
      for (float &component : value)
        component = -component;
    else if (operand.sourceModifier != 0u)
      throw std::runtime_error(
          "Software pixel shader: texture coordinate uses an unsupported modifier.");
    return value;
  }

  void ExecuteTextureLodInstruction(
      const SoftwareShaderInstructionEXT &instruction) {
    const auto &tokens = instruction.tokens;
    const std::size_t expected = instruction.opcode == 94u ? 6u : 4u;
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

  void ExecuteInstruction(const SoftwareShaderInstructionEXT &instruction) {
    const auto &tokens = instruction.tokens;
    if (tokens.empty())
      throw std::runtime_error("Software pixel shader: empty instruction.");
    if (instruction.opcode == 0u || instruction.opcode == 31u ||
        instruction.opcode == 0xFFFEu)
      return;
    if (instruction.predicated)
      throw std::runtime_error("Software pixel shader: predicated instructions "
                               "require SOFTWARE-165.");
    if (instruction.opcode == 81u) {
      DefineFloat(tokens);
      return;
    }
    if (instruction.opcode == 64u) {
      if (tokens.size() != 2u)
        throw std::runtime_error(
            "Software pixel shader: unsupported TEXCOORD instruction shape.");
      const Operand destination = DecodeDestination(tokens[1]);
      Write(destination, ReadRaw(destination.type, destination.number));
      return;
    }
    if (instruction.opcode == 65u) {
      if (tokens.size() != 2u)
        throw std::runtime_error(
            "Software pixel shader: malformed TEXKILL instruction.");
      const Operand source = DecodeDestination(tokens[1]);
      const Vector value = ReadRaw(source.type, source.number);
      discarded_ = value[0] < 0.0f || value[1] < 0.0f || value[2] < 0.0f;
      return;
    }
    if (instruction.opcode == 66u) {
      ExecuteTextureInstruction(instruction);
      return;
    }
    if (instruction.opcode == 94u || instruction.opcode == 95u) {
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
    case 32:
    case 33:
      source1 = ReadSource(tokens, cursor);
      break;
    case 4:
    case 18:
    case 34:
    case 88:
      source1 = ReadSource(tokens, cursor);
      source2 = ReadSource(tokens, cursor);
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
      for (int i = 0; i < 4; ++i)
        result[i] = std::exp2(source0[i]);
      break;
    case 15:
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
    case 88:
      for (int i = 0; i < 4; ++i)
        result[i] = source0[i] >= 0.0f ? source1[i] : source2[i];
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
  std::array<Vector, kTemporaryRegisterCount> temporaryRegisters_{};
  std::array<bool, kTemporaryRegisterCount> temporaryWritten_{};
  std::array<Vector, kInputRegisterCount> inputRegisters_{};
  std::array<Vector, kTextureRegisterCount> textureRegisters_{};
  std::array<Vector, kColorOutputRegisterCount> colorOutputs_{};
  std::array<bool, kColorOutputRegisterCount> colorOutputWritten_{};
  float depthOutput_ = 0.0f;
  bool depthWritten_ = false;
  bool discarded_ = false;
  std::array<Vector, kFloatConstantRegisterCount> localFloatConstants_{};
  std::array<bool, kFloatConstantRegisterCount> localFloatDefined_{};
};
} // namespace

SoftwarePixelShaderResultEXT ExecuteSoftwarePixelShaderEXT(
    const SoftwareShaderProgramEXT &program,
    std::span<const float> floatRegisters,
    std::span<const int> integerRegisters,
    std::span<const unsigned char> booleanRegisters,
    std::span<const SoftwareShaderSemanticValueEXT> inputs,
    const ISoftwarePixelSamplerEXT *sampler) {
  return PixelMachine(program, floatRegisters, integerRegisters,
                      booleanRegisters, inputs, sampler)
      .Execute();
}
} // namespace CNA::Internal::Renderers::Software

#endif // CNA_SOFTWARE_COMPILED_EFFECTS
