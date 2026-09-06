// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-203. See SpirvToWgsl.hpp for what this translates and why the input
// language is a small fixed subset rather than SPIR-V at large.

#include "CNA/Internal/Renderers/MojoShader/SpirvToWgsl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CNA::Internal::Renderers::MojoShaderEffect
{
    namespace
    {
        // ------------------------------------------------------------------------------------
        // SPIR-V constants. Spelled here rather than pulled from MojoShader's bundled spirv.h so
        // this file compiles in a configuration that has the CNA half without the C library.
        // ------------------------------------------------------------------------------------
        constexpr std::uint32_t kMagic = 0x07230203u;

        enum Op : std::uint32_t
        {
            OpName = 5,
            OpMemberName = 6,
            OpExtInstImport = 11,
            OpExtInst = 12,
            OpMemoryModel = 14,
            OpEntryPoint = 15,
            OpExecutionMode = 16,
            OpCapability = 17,
            OpTypeVoid = 19,
            OpTypeBool = 20,
            OpTypeInt = 21,
            OpTypeFloat = 22,
            OpTypeVector = 23,
            OpTypeMatrix = 24,
            OpTypeImage = 25,
            OpTypeSampler = 26,
            OpTypeSampledImage = 27,
            OpTypeArray = 28,
            OpTypeStruct = 30,
            OpTypePointer = 32,
            OpTypeFunction = 33,
            OpConstantTrue = 41,
            OpConstantFalse = 42,
            OpConstant = 43,
            OpConstantComposite = 44,
            OpFunction = 54,
            OpFunctionParameter = 55,
            OpFunctionEnd = 56,
            OpFunctionCall = 57,
            OpVariable = 59,
            OpLoad = 61,
            OpStore = 62,
            OpAccessChain = 65,
            OpDecorate = 71,
            OpMemberDecorate = 72,
            OpVectorShuffle = 79,
            OpCompositeConstruct = 80,
            OpCompositeExtract = 81,
            OpCopyObject = 83,
            OpSampledImage = 86,
            OpImageSampleImplicitLod = 87,
            OpImageSampleExplicitLod = 88,
            OpConvertFToS = 110,
            OpFNegate = 127,
            OpIAdd = 128,
            OpFAdd = 129,
            OpISub = 130,
            OpFSub = 131,
            OpIMul = 132,
            OpFMul = 133,
            OpSDiv = 135,
            OpFDiv = 136,
            OpDot = 148,
            OpAny = 154,
            OpAll = 155,
            OpLogicalOr = 166,
            OpLogicalAnd = 167,
            OpLogicalNot = 168,
            OpSelect = 169,
            OpFOrdEqual = 180,
            OpFOrdNotEqual = 182,
            OpFOrdLessThan = 184,
            OpFOrdGreaterThan = 186,
            OpFUnordGreaterThan = 187,
            OpFOrdLessThanEqual = 188,
            OpFOrdGreaterThanEqual = 190,
            OpFUnordGreaterThanEqual = 191,
            OpPhi = 245,
            OpLoopMerge = 246,
            OpSelectionMerge = 247,
            OpLabel = 248,
            OpBranch = 249,
            OpBranchConditional = 250,
            OpSwitch = 251,
            OpKill = 252,
            OpReturn = 253,
            OpReturnValue = 254,
            OpUnreachable = 255,
        };

        enum Decoration : std::uint32_t
        {
            DecorationBlock = 2,
            DecorationArrayStride = 6,
            DecorationBuiltIn = 11,
            DecorationLocation = 30,
            DecorationBinding = 33,
            DecorationDescriptorSet = 34,
            DecorationOffset = 35,
        };

        enum StorageClass : std::uint32_t
        {
            StorageUniformConstant = 0,
            StorageInput = 1,
            StorageUniform = 2,
            StorageOutput = 3,
            StorageFunction = 7,
            StoragePrivate = 6,
        };

        enum BuiltIn : std::uint32_t
        {
            BuiltInPosition = 0,
            BuiltInPointSize = 1,
            BuiltInFragCoord = 15,
            BuiltInPointCoord = 16,
            BuiltInFrontFacing = 17,
            BuiltInFragDepth = 22,
        };

        enum ExecutionModel : std::uint32_t
        {
            ExecutionModelVertex = 0,
            ExecutionModelFragment = 4,
        };

        enum GlslStd450 : std::uint32_t
        {
            Glsl450Round = 1,
            Glsl450FAbs = 4,
            Glsl450FSign = 6,
            Glsl450Floor = 8,
            Glsl450Ceil = 9,
            Glsl450Fract = 10,
            Glsl450Sin = 13,
            Glsl450Cos = 14,
            Glsl450Pow = 26,
            Glsl450Exp2 = 29,
            Glsl450Log2 = 30,
            Glsl450Sqrt = 31,
            Glsl450InverseSqrt = 32,
            Glsl450FMin = 37,
            Glsl450FMax = 40,
            Glsl450FClamp = 43,
            Glsl450FMix = 46,
            Glsl450Cross = 68,
            Glsl450Normalize = 69,
            Glsl450Reflect = 71,
        };

        /// Thrown internally so a refusal can carry its message out of any depth of the walk. The
        /// public entry point catches it and reports it as a result, never as an exception.
        struct Refusal : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        [[noreturn]] void Refuse(const std::string& what) { throw Refusal(what); }

        enum class TypeKind
        {
            Unknown,
            Void,
            Bool,
            Int,
            Float,
            Vector,
            Array,
            Struct,
            Pointer,
            Image,
            Sampler,
            SampledImage,
            Function,
        };

        struct Type
        {
            TypeKind kind = TypeKind::Unknown;
            /// Component type (vector), element type (array/pointer), sampled type (image).
            std::uint32_t inner = 0;
            /// Component count (vector) or element count (array).
            std::uint32_t count = 0;
            std::uint32_t width = 0;
            bool isSigned = false;
            std::uint32_t storageClass = 0;
            std::vector<std::uint32_t> members;
            /// OpTypeImage operands actually consulted.
            std::uint32_t dim = 0;
            std::uint32_t depth = 0;
            std::uint32_t arrayed = 0;
            std::uint32_t multisampled = 0;
            std::uint32_t sampled = 0;
        };

        struct Variable
        {
            std::uint32_t typeId = 0;   // pointer type
            std::uint32_t storageClass = 0;
            std::uint32_t initializer = 0;
        };

        struct Constant
        {
            std::uint32_t typeId = 0;
            /// Raw literal words (scalar) or constituent ids (composite).
            std::vector<std::uint32_t> words;
            bool composite = false;
        };

        struct Instruction
        {
            std::uint32_t opcode = 0;
            const std::uint32_t* words = nullptr;   // points at the opcode word
            std::uint32_t length = 0;
            std::uint32_t operand(std::size_t index) const { return words[index + 1]; }
        };

        struct Block
        {
            std::uint32_t label = 0;
            std::vector<Instruction> body;      // everything before the terminator
            Instruction merge{};                // OpSelectionMerge/OpLoopMerge, if any
            Instruction terminator{};
        };

        // -----------------------------------------------------------------------------------
        // The translator.
        // -----------------------------------------------------------------------------------
        class Translator
        {
        public:
            Translator(const std::uint32_t* words, std::size_t wordCount)
                : words_(words), wordCount_(wordCount)
            {
            }

            SpirvToWgslResult Run()
            {
                SpirvToWgslResult result;
                try
                {
                    Parse();
                    Emit();
                    result.wgsl = out_;
                    result.entryPoint = entryName_;
                }
                catch (const Refusal& refusal)
                {
                    result.wgsl.clear();
                    result.error = refusal.what();
                }
                return result;
            }

        private:
            // --- module tables -------------------------------------------------------------
            const std::uint32_t* words_ = nullptr;
            std::size_t wordCount_ = 0;

            std::unordered_map<std::uint32_t, Type> types_;
            std::unordered_map<std::uint32_t, Variable> variables_;
            std::unordered_map<std::uint32_t, Constant> constants_;
            std::unordered_map<std::uint32_t, std::string> spirvNames_;
            std::unordered_map<std::uint32_t, std::map<std::uint32_t, std::vector<std::uint32_t>>>
                decorations_;
            std::map<std::pair<std::uint32_t, std::uint32_t>,
                     std::map<std::uint32_t, std::vector<std::uint32_t>>>
                memberDecorations_;
            std::unordered_set<std::uint32_t> glslExtSets_;

            std::uint32_t executionModel_ = 0;
            std::uint32_t entryFunction_ = 0;
            std::string entryName_;
            std::vector<std::uint32_t> entryInterface_;

            std::vector<Block> blocks_;
            std::unordered_map<std::uint32_t, std::size_t> blockIndex_;

            // --- emission state ------------------------------------------------------------
            std::string out_;
            std::string body_;
            int indent_ = 2;
            std::unordered_map<std::uint32_t, std::string> expr_;
            std::unordered_map<std::uint32_t, std::uint32_t> resultType_;
            /// OpSampledImage result -> the two module-scope handles it pairs.
            std::unordered_map<std::uint32_t, std::pair<std::string, std::string>> sampledPairs_;
            std::unordered_map<std::uint32_t, std::string> wgslName_;
            std::unordered_set<std::string> usedNames_;
            /// Uniform struct members widened from a scalar array to a vec4 array to satisfy
            /// WGSL's 16-byte uniform stride rule; an access chain into one appends `.x`.
            std::set<std::pair<std::uint32_t, std::uint32_t>> widenedMembers_;

            // ------------------------------------------------------------------------------
            const Type& TypeOf(std::uint32_t id) const
            {
                const auto it = types_.find(id);
                if (it == types_.end())
                    Refuse("SPIR-V id %" + std::to_string(id) + " is not a type this module declared");
                return it->second;
            }

            bool HasDecoration(std::uint32_t id, std::uint32_t decoration) const
            {
                const auto it = decorations_.find(id);
                return it != decorations_.end() && it->second.count(decoration) != 0;
            }

            std::uint32_t DecorationValue(std::uint32_t id, std::uint32_t decoration,
                                          std::uint32_t fallback = 0) const
            {
                const auto it = decorations_.find(id);
                if (it == decorations_.end()) return fallback;
                const auto found = it->second.find(decoration);
                if (found == it->second.end() || found->second.empty()) return fallback;
                return found->second.front();
            }

            std::uint32_t MemberDecorationValue(std::uint32_t structId, std::uint32_t member,
                                                std::uint32_t decoration,
                                                std::uint32_t fallback = 0) const
            {
                const auto it = memberDecorations_.find({structId, member});
                if (it == memberDecorations_.end()) return fallback;
                const auto found = it->second.find(decoration);
                if (found == it->second.end() || found->second.empty()) return fallback;
                return found->second.front();
            }

            // --- parsing -------------------------------------------------------------------
            void Parse()
            {
                if (wordCount_ < 5 || words_ == nullptr || words_[0] != kMagic)
                    Refuse("not a SPIR-V module (missing magic number)");

                std::size_t i = 5;
                bool inFunction = false;
                Block current{};
                bool haveBlock = false;

                while (i < wordCount_)
                {
                    const std::uint32_t opcode = words_[i] & 0xFFFFu;
                    const std::uint32_t length = words_[i] >> 16;
                    if (length == 0 || i + length > wordCount_)
                        Refuse("truncated SPIR-V instruction stream");

                    Instruction instruction{opcode, &words_[i], length, };
                    if (!inFunction)
                    {
                        if (opcode == OpFunction)
                        {
                            if (entryFunction_ != 0 && instruction.operand(1) != entryFunction_)
                                Refuse("this module declares more than one function, which the "
                                       "compiled-effect SPIR-V subset never does");
                            inFunction = true;
                        }
                        else
                        {
                            ParseModuleLevel(instruction);
                        }
                    }
                    else if (opcode == OpFunctionEnd)
                    {
                        if (haveBlock) { blocks_.push_back(current); haveBlock = false; }
                        inFunction = false;
                    }
                    else if (opcode == OpFunctionParameter || opcode == OpFunctionCall)
                    {
                        Refuse("OpFunctionCall/OpFunctionParameter: the compiled-effect SPIR-V "
                               "subset has exactly one parameterless function");
                    }
                    else if (opcode == OpLabel)
                    {
                        if (haveBlock) blocks_.push_back(current);
                        current = Block{};
                        current.label = instruction.operand(0);
                        haveBlock = true;
                    }
                    else if (haveBlock)
                    {
                        if (opcode == OpSelectionMerge || opcode == OpLoopMerge)
                        {
                            current.merge = instruction;
                        }
                        else if (IsTerminator(opcode))
                        {
                            current.terminator = instruction;
                        }
                        else
                        {
                            current.body.push_back(instruction);
                        }
                    }

                    i += length;
                }

                for (std::size_t b = 0; b < blocks_.size(); ++b)
                    blockIndex_[blocks_[b].label] = b;

                if (entryFunction_ == 0) Refuse("this module declares no entry point");
                if (blocks_.empty()) Refuse("this module's entry point has no body");
            }

            static bool IsTerminator(std::uint32_t opcode)
            {
                return opcode == OpBranch || opcode == OpBranchConditional || opcode == OpSwitch ||
                       opcode == OpKill || opcode == OpReturn || opcode == OpReturnValue ||
                       opcode == OpUnreachable;
            }

            static std::string LiteralString(const std::uint32_t* words, std::uint32_t available,
                                             std::uint32_t& consumedWords)
            {
                const char* text = reinterpret_cast<const char*>(words);
                const std::size_t maxBytes = static_cast<std::size_t>(available) * 4u;
                const std::size_t length = ::strnlen(text, maxBytes);
                if (length == maxBytes) Refuse("unterminated SPIR-V literal string");
                consumedWords = static_cast<std::uint32_t>((length + 1 + 3) / 4);
                return std::string(text, length);
            }

            void ParseModuleLevel(const Instruction& instruction)
            {
                switch (instruction.opcode)
                {
                    case OpCapability:
                    {
                        // 1 = Shader. Anything else changes what the module may contain.
                        if (instruction.operand(0) != 1u)
                        {
                            Refuse("SPIR-V capability " + std::to_string(instruction.operand(0)) +
                                   " is outside the compiled-effect subset (only Shader is)");
                        }
                        break;
                    }
                    case OpExtInstImport:
                    {
                        std::uint32_t consumed = 0;
                        const std::string name = LiteralString(&instruction.words[2],
                                                               instruction.length - 2, consumed);
                        if (name != "GLSL.std.450")
                            Refuse("extended instruction set '" + name + "' is not translated");
                        glslExtSets_.insert(instruction.operand(0));
                        break;
                    }
                    case OpMemoryModel:
                    {
                        if (instruction.operand(0) != 0u)   // Logical
                            Refuse("only the Logical addressing model is translated");
                        break;
                    }
                    case OpEntryPoint:
                    {
                        executionModel_ = instruction.operand(0);
                        entryFunction_ = instruction.operand(1);
                        std::uint32_t consumed = 0;
                        entryName_ = LiteralString(&instruction.words[3], instruction.length - 3,
                                                   consumed);
                        for (std::uint32_t w = 3 + consumed; w < instruction.length; ++w)
                            entryInterface_.push_back(instruction.words[w]);
                        break;
                    }
                    case OpExecutionMode:
                    {
                        const std::uint32_t mode = instruction.operand(1);
                        // 7 = OriginUpperLeft, which WGSL is unconditionally.
                        if (mode != 7u)
                        {
                            Refuse("execution mode " + std::to_string(mode) +
                                   " has no WGSL equivalent in this subset");
                        }
                        break;
                    }
                    case OpName:
                    {
                        std::uint32_t consumed = 0;
                        spirvNames_[instruction.operand(0)] =
                            LiteralString(&instruction.words[2], instruction.length - 2, consumed);
                        break;
                    }
                    case OpMemberName: break;
                    case OpDecorate:
                    {
                        std::vector<std::uint32_t> literals;
                        for (std::uint32_t w = 3; w < instruction.length; ++w)
                            literals.push_back(instruction.words[w]);
                        decorations_[instruction.operand(0)][instruction.operand(1)] = literals;
                        break;
                    }
                    case OpMemberDecorate:
                    {
                        std::vector<std::uint32_t> literals;
                        for (std::uint32_t w = 4; w < instruction.length; ++w)
                            literals.push_back(instruction.words[w]);
                        memberDecorations_[{instruction.operand(0), instruction.operand(1)}]
                                          [instruction.operand(2)] = literals;
                        break;
                    }
                    case OpTypeVoid:   types_[instruction.operand(0)].kind = TypeKind::Void; break;
                    case OpTypeBool:   types_[instruction.operand(0)].kind = TypeKind::Bool; break;
                    case OpTypeSampler:
                        types_[instruction.operand(0)].kind = TypeKind::Sampler;
                        break;
                    case OpTypeInt:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Int;
                        type.width = instruction.operand(1);
                        type.isSigned = instruction.operand(2) != 0;
                        if (type.width != 32u) Refuse("only 32-bit integers are translated");
                        break;
                    }
                    case OpTypeFloat:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Float;
                        type.width = instruction.operand(1);
                        if (type.width != 32u) Refuse("only 32-bit floats are translated");
                        break;
                    }
                    case OpTypeVector:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Vector;
                        type.inner = instruction.operand(1);
                        type.count = instruction.operand(2);
                        break;
                    }
                    case OpTypeMatrix:
                        Refuse("OpTypeMatrix: MojoShader's SPIR-V profile emits no matrices, so "
                               "this subset does not translate one");
                        break;
                    case OpTypeImage:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Image;
                        type.inner = instruction.operand(1);
                        type.dim = instruction.operand(2);
                        type.depth = instruction.operand(3);
                        type.arrayed = instruction.operand(4);
                        type.multisampled = instruction.operand(5);
                        type.sampled = instruction.operand(6);
                        break;
                    }
                    case OpTypeSampledImage:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::SampledImage;
                        type.inner = instruction.operand(1);
                        break;
                    }
                    case OpTypeArray:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Array;
                        type.inner = instruction.operand(1);
                        type.count = ConstantScalar(instruction.operand(2));
                        break;
                    }
                    case OpTypeStruct:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Struct;
                        for (std::uint32_t w = 2; w < instruction.length; ++w)
                            type.members.push_back(instruction.words[w]);
                        break;
                    }
                    case OpTypePointer:
                    {
                        Type& type = types_[instruction.operand(0)];
                        type.kind = TypeKind::Pointer;
                        type.storageClass = instruction.operand(1);
                        type.inner = instruction.operand(2);
                        break;
                    }
                    case OpTypeFunction:
                        types_[instruction.operand(0)].kind = TypeKind::Function;
                        break;
                    case OpConstant:
                    {
                        Constant& constant = constants_[instruction.operand(1)];
                        constant.typeId = instruction.operand(0);
                        for (std::uint32_t w = 3; w < instruction.length; ++w)
                            constant.words.push_back(instruction.words[w]);
                        break;
                    }
                    case OpConstantComposite:
                    {
                        Constant& constant = constants_[instruction.operand(1)];
                        constant.typeId = instruction.operand(0);
                        constant.composite = true;
                        for (std::uint32_t w = 3; w < instruction.length; ++w)
                            constant.words.push_back(instruction.words[w]);
                        break;
                    }
                    case OpConstantTrue:
                    case OpConstantFalse:
                    {
                        Constant& constant = constants_[instruction.operand(1)];
                        constant.typeId = instruction.operand(0);
                        constant.words.push_back(instruction.opcode == OpConstantTrue ? 1u : 0u);
                        break;
                    }
                    case OpVariable:
                    {
                        Variable& variable = variables_[instruction.operand(1)];
                        variable.typeId = instruction.operand(0);
                        variable.storageClass = instruction.operand(2);
                        if (instruction.length > 4) variable.initializer = instruction.operand(3);
                        break;
                    }
                    default:
                        // Anything else at module scope is either harmless (OpSource-style
                        // debug info, which MojoShader does not emit) or outside the subset.
                        break;
                }
            }

            std::uint32_t ConstantScalar(std::uint32_t id) const
            {
                const auto it = constants_.find(id);
                if (it == constants_.end() || it->second.words.empty())
                    Refuse("expected a scalar constant for id %" + std::to_string(id));
                return it->second.words.front();
            }

            // --- naming --------------------------------------------------------------------
            std::string NameFor(std::uint32_t id)
            {
                const auto existing = wgslName_.find(id);
                if (existing != wgslName_.end()) return existing->second;

                std::string base;
                const auto named = spirvNames_.find(id);
                if (named != spirvNames_.end() && !named->second.empty()) base = named->second;
                if (base.empty()) base = "v" + std::to_string(id);
                for (char& c : base)
                {
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9')))
                    {
                        c = '_';
                    }
                }
                if (base.empty() || !((base[0] >= 'a' && base[0] <= 'z') ||
                                      (base[0] >= 'A' && base[0] <= 'Z')))
                {
                    base = "x" + base;
                }
                std::string candidate = base;
                if (usedNames_.count(candidate) != 0)
                    candidate = base + "_" + std::to_string(id);
                usedNames_.insert(candidate);
                wgslName_[id] = candidate;
                return candidate;
            }

            // --- WGSL type spelling --------------------------------------------------------
            std::string ScalarName(const Type& type) const
            {
                switch (type.kind)
                {
                    case TypeKind::Bool: return "bool";
                    case TypeKind::Float: return "f32";
                    case TypeKind::Int: return type.isSigned ? "i32" : "u32";
                    default: break;
                }
                Refuse("expected a scalar type");
            }

            std::string TypeName(std::uint32_t id)
            {
                const Type& type = TypeOf(id);
                switch (type.kind)
                {
                    case TypeKind::Bool:
                    case TypeKind::Int:
                    case TypeKind::Float:
                        return ScalarName(type);
                    case TypeKind::Vector:
                        return "vec" + std::to_string(type.count) + "<" +
                               ScalarName(TypeOf(type.inner)) + ">";
                    case TypeKind::Array:
                        return "array<" + TypeName(type.inner) + ", " +
                               std::to_string(type.count) + ">";
                    case TypeKind::Struct:
                        return NameFor(id);
                    case TypeKind::Image:
                        return ImageTypeName(type);
                    case TypeKind::Sampler:
                        return "sampler";
                    default: break;
                }
                Refuse("no WGSL spelling for SPIR-V type %" + std::to_string(id));
            }

            std::string ImageTypeName(const Type& type) const
            {
                if (type.multisampled != 0)
                    Refuse("multisampled textures are outside the compiled-effect subset");
                if (type.depth == 1u)
                    Refuse("depth textures are outside the compiled-effect subset");
                if (type.sampled != 1u)
                    Refuse("storage textures are outside the compiled-effect subset");
                const std::string sampled = ScalarName(TypeOf(type.inner));
                switch (type.dim)
                {
                    case 0: return "texture_1d<" + sampled + ">";
                    case 1:
                        return type.arrayed != 0 ? "texture_2d_array<" + sampled + ">"
                                                 : "texture_2d<" + sampled + ">";
                    case 2: return "texture_3d<" + sampled + ">";
                    case 3:
                        return type.arrayed != 0 ? "texture_cube_array<" + sampled + ">"
                                                 : "texture_cube<" + sampled + ">";
                    default: break;
                }
                Refuse("texture dimensionality " + std::to_string(type.dim) +
                       " is outside the compiled-effect subset");
            }

            /// Coordinate components WGSL's textureSample takes for this image.
            std::uint32_t CoordinateComponents(const Type& image) const
            {
                switch (image.dim)
                {
                    case 0: return image.arrayed != 0 ? 2u : 1u;
                    case 1: return image.arrayed != 0 ? 3u : 2u;
                    case 2: return 3u;
                    case 3: return image.arrayed != 0 ? 4u : 3u;
                    default: break;
                }
                Refuse("texture dimensionality " + std::to_string(image.dim) +
                       " is outside the compiled-effect subset");
            }

            // --- literals ------------------------------------------------------------------
            static std::string FloatLiteral(std::uint32_t bits)
            {
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                if (!std::isfinite(value))
                {
                    // WGSL has no infinity or NaN literal; the bit pattern is the exact spelling.
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer), "bitcast<f32>(0x%08xu)", bits);
                    return buffer;
                }
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
                std::string text = buffer;
                if (text.find('.') == std::string::npos && text.find('e') == std::string::npos &&
                    text.find("inf") == std::string::npos)
                {
                    text += ".0";
                }
                return text + "f";
            }

            std::string ConstantExpression(std::uint32_t id)
            {
                const auto it = constants_.find(id);
                if (it == constants_.end())
                    Refuse("id %" + std::to_string(id) + " is not a constant");
                const Constant& constant = it->second;
                const Type& type = TypeOf(constant.typeId);
                if (constant.composite)
                {
                    std::string text = TypeName(constant.typeId) + "(";
                    for (std::size_t c = 0; c < constant.words.size(); ++c)
                    {
                        if (c != 0) text += ", ";
                        text += ValueExpression(constant.words[c]);
                    }
                    return text + ")";
                }
                switch (type.kind)
                {
                    case TypeKind::Float: return FloatLiteral(constant.words.front());
                    case TypeKind::Int:
                        return type.isSigned
                                   ? std::to_string(
                                         static_cast<std::int32_t>(constant.words.front())) + "i"
                                   : std::to_string(constant.words.front()) + "u";
                    case TypeKind::Bool:
                        return constant.words.front() != 0 ? "true" : "false";
                    default: break;
                }
                Refuse("constant of an untranslated type");
            }

            /// Expression text for any id already produced -- a constant, an SSA result, or a
            /// module-scope handle.
            std::string ValueExpression(std::uint32_t id)
            {
                const auto existing = expr_.find(id);
                if (existing != expr_.end()) return existing->second;
                if (constants_.count(id) != 0)
                {
                    const std::string text = ConstantExpression(id);
                    expr_[id] = text;
                    resultType_[id] = constants_[id].typeId;
                    return text;
                }
                Refuse("SPIR-V id %" + std::to_string(id) + " is used before it is defined");
            }

            std::uint32_t TypeIdOfValue(std::uint32_t id)
            {
                const auto it = resultType_.find(id);
                if (it != resultType_.end()) return it->second;
                const auto constant = constants_.find(id);
                if (constant != constants_.end()) return constant->second.typeId;
                Refuse("SPIR-V id %" + std::to_string(id) + " has no known type");
            }

            // --- emission ------------------------------------------------------------------
            void Line(const std::string& text)
            {
                body_.append(static_cast<std::size_t>(indent_), ' ');
                body_ += text;
                body_ += '\n';
            }

            void Emit()
            {
                if (executionModel_ != ExecutionModelVertex &&
                    executionModel_ != ExecutionModelFragment)
                {
                    Refuse("only vertex and fragment entry points are translated");
                }

                std::string globals;
                EmitStructTypes(globals);
                EmitResourceVariables(globals);
                EmitPrivateAndIoVariables(globals);

                indent_ = 4;
                EmitBlockChain(blocks_.front().label, 0);

                out_ = "// Generated from MojoShader SPIR-V by CNA "
                       "(plans/plan_webgpu.md WEBGPU-203). Do not edit.\n";
                out_ += globals;
                out_ += "\nfn " + entryName_ + "_body() {\n";
                out_ += body_;
                out_ += "}\n";
                out_ += EmitEntryWrapper();
            }

            void EmitStructTypes(std::string& globals)
            {
                for (const auto& [id, variable] : variables_)
                {
                    if (variable.storageClass != StorageUniform) continue;
                    const Type& pointer = TypeOf(variable.typeId);
                    const std::uint32_t structId = pointer.inner;
                    const Type& block = TypeOf(structId);
                    if (block.kind != TypeKind::Struct)
                        Refuse("a Uniform variable whose pointee is not a struct");
                    if (!HasDecoration(structId, DecorationBlock))
                        Refuse("a Uniform struct without the Block decoration");

                    // MojoShader names the variable but not its type, so name the type after the
                    // variable rather than after its numeric id -- the WGSL a person reads when a
                    // browser reports a shader error should say `ps_uniforms_t`, not `v72`.
                    if (spirvNames_.count(structId) == 0 && spirvNames_.count(id) != 0)
                        spirvNames_[structId] = spirvNames_[id] + "_t";
                    globals += "struct " + NameFor(structId) + " {\n";
                    for (std::size_t m = 0; m < block.members.size(); ++m)
                    {
                        globals += "    m" + std::to_string(m) + " : " +
                                   UniformMemberTypeName(structId, static_cast<std::uint32_t>(m),
                                                         block.members[m]) +
                                   ",\n";
                    }
                    globals += "};\n";
                }
            }

            /// WGSL's uniform address space requires an array element stride that is a multiple of
            /// 16. MojoShader decorates every uniform array with ArrayStride 16, including the
            /// scalar `bool` array, so a scalar element is widened to a vec4 here and the access
            /// chain that reads it takes `.x`. Layout is preserved exactly; only the spelling
            /// changes.
            std::string UniformMemberTypeName(std::uint32_t structId, std::uint32_t member,
                                              std::uint32_t memberType)
            {
                const Type& type = TypeOf(memberType);
                if (type.kind != TypeKind::Array) return TypeName(memberType);
                const Type& element = TypeOf(type.inner);
                const std::uint32_t stride =
                    DecorationValue(memberType, DecorationArrayStride, 0u);
                if (element.kind == TypeKind::Vector && element.count == 4u)
                {
                    if (stride != 0u && stride != 16u)
                    {
                        Refuse("uniform array stride " + std::to_string(stride) +
                               " is not the 16 bytes WGSL's uniform address space requires");
                    }
                    return TypeName(memberType);
                }
                if ((element.kind == TypeKind::Int || element.kind == TypeKind::Float) &&
                    stride == 16u)
                {
                    widenedMembers_.insert({structId, member});
                    return "array<vec4<" + ScalarName(element) + ">, " +
                           std::to_string(type.count) + ">";
                }
                Refuse("uniform member " + std::to_string(member) +
                       " has a layout WGSL's uniform address space cannot express");
            }

            void EmitResourceVariables(std::string& globals)
            {
                // Sorted by (set, binding) so the output is deterministic for regression tests.
                std::vector<std::pair<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>>
                    ordered;
                for (const auto& [id, variable] : variables_)
                {
                    if (variable.storageClass != StorageUniform &&
                        variable.storageClass != StorageUniformConstant)
                    {
                        continue;
                    }
                    ordered.push_back({{DecorationValue(id, DecorationDescriptorSet),
                                        DecorationValue(id, DecorationBinding)},
                                       id});
                }
                std::sort(ordered.begin(), ordered.end());

                for (const auto& [key, id] : ordered)
                {
                    const Variable& variable = variables_.at(id);
                    const Type& pointer = TypeOf(variable.typeId);
                    const std::string attributes = "@group(" + std::to_string(key.first) +
                                                   ") @binding(" + std::to_string(key.second) +
                                                   ") ";
                    if (variable.storageClass == StorageUniform)
                    {
                        globals += attributes + "var<uniform> " + NameFor(id) + " : " +
                                   TypeName(pointer.inner) + ";\n";
                        expr_[id] = NameFor(id);
                    }
                    else
                    {
                        const Type& resource = TypeOf(pointer.inner);
                        if (resource.kind == TypeKind::SampledImage)
                        {
                            Refuse("a combined image sampler reached the WGSL translator: WGSL "
                                   "has no such type, so SplitCombinedImageSamplers() must run "
                                   "first");
                        }
                        globals += attributes + "var " + NameFor(id) + " : " +
                                   TypeName(pointer.inner) + ";\n";
                        expr_[id] = NameFor(id);
                    }
                    resultType_[id] = variable.typeId;
                }
            }

            void EmitPrivateAndIoVariables(std::string& globals)
            {
                std::vector<std::uint32_t> ordered;
                for (const auto& [id, variable] : variables_)
                {
                    if (variable.storageClass == StorageUniform ||
                        variable.storageClass == StorageUniformConstant)
                    {
                        continue;
                    }
                    if (variable.storageClass == StorageFunction)
                        Refuse("a Function-storage OpVariable, which this subset never emits");
                    ordered.push_back(id);
                }
                std::sort(ordered.begin(), ordered.end());

                for (const std::uint32_t id : ordered)
                {
                    const Variable& variable = variables_.at(id);
                    const Type& pointer = TypeOf(variable.typeId);
                    // Input and Output become private shadows; the entry wrapper moves the values
                    // across. See the header comment for why.
                    std::string declaration = "var<private> " + NameFor(id) + " : " +
                                              TypeName(pointer.inner);
                    if (variable.initializer != 0)
                        declaration += " = " + ConstantExpression(variable.initializer);
                    globals += declaration + ";\n";
                    expr_[id] = NameFor(id);
                    resultType_[id] = variable.typeId;
                }
            }

            // --- control flow --------------------------------------------------------------
            const Block& BlockAt(std::uint32_t label) const
            {
                const auto it = blockIndex_.find(label);
                if (it == blockIndex_.end())
                    Refuse("branch to an undeclared block %" + std::to_string(label));
                return blocks_[it->second];
            }

            /// Emits @p label and everything structurally dominated by it, stopping when it
            /// reaches @p stopAt (0 means "to the end of the function").
            void EmitBlockChain(std::uint32_t label, std::uint32_t stopAt)
            {
                std::uint32_t current = label;
                while (current != 0 && current != stopAt)
                {
                    const Block& block = BlockAt(current);
                    for (const Instruction& instruction : block.body) EmitInstruction(instruction);

                    if (block.merge.opcode == OpLoopMerge)
                        Refuse("OpLoopMerge: MojoShader's SPIR-V profile emits no loops, so this "
                               "subset does not reconstruct one");

                    const Instruction& terminator = block.terminator;
                    switch (terminator.opcode)
                    {
                        case OpReturn:
                            if (stopAt != 0) Line("return;");
                            current = 0;
                            break;
                        case OpKill:
                            Line("discard;");
                            current = 0;
                            break;
                        case OpBranch:
                            current = terminator.operand(0);
                            break;
                        case OpBranchConditional:
                        {
                            if (block.merge.opcode != OpSelectionMerge)
                            {
                                Refuse("a conditional branch with no OpSelectionMerge, which this "
                                       "subset's structured control flow always has");
                            }
                            const std::uint32_t merge = block.merge.operand(0);
                            const std::uint32_t whenTrue = terminator.operand(1);
                            const std::uint32_t whenFalse = terminator.operand(2);
                            const std::string condition = ValueExpression(terminator.operand(0));
                            if (whenFalse == merge)
                            {
                                Line("if (" + condition + ") {");
                                indent_ += 2;
                                EmitBlockChain(whenTrue, merge);
                                indent_ -= 2;
                                Line("}");
                            }
                            else if (whenTrue == merge)
                            {
                                Line("if (!(" + condition + ")) {");
                                indent_ += 2;
                                EmitBlockChain(whenFalse, merge);
                                indent_ -= 2;
                                Line("}");
                            }
                            else
                            {
                                Line("if (" + condition + ") {");
                                indent_ += 2;
                                EmitBlockChain(whenTrue, merge);
                                indent_ -= 2;
                                Line("} else {");
                                indent_ += 2;
                                EmitBlockChain(whenFalse, merge);
                                indent_ -= 2;
                                Line("}");
                            }
                            current = merge;
                            break;
                        }
                        case OpSwitch:
                            Refuse("OpSwitch is outside the compiled-effect subset");
                        case OpReturnValue:
                            Refuse("OpReturnValue: this subset's one function returns void");
                        case OpUnreachable:
                            current = 0;
                            break;
                        default:
                            Refuse("block %" + std::to_string(block.label) +
                                   " has no terminator this subset understands");
                    }
                }
            }

            // --- instructions --------------------------------------------------------------
            void Define(std::uint32_t id, std::uint32_t typeId, const std::string& expression)
            {
                resultType_[id] = typeId;
                const std::string name = "v" + std::to_string(id);
                Line("let " + name + " = " + expression + ";");
                expr_[id] = name;
            }

            void EmitInstruction(const Instruction& instruction)
            {
                switch (instruction.opcode)
                {
                    case OpVariable:
                        Refuse("a Function-storage OpVariable, which this subset never emits");
                    case OpLoad: EmitLoad(instruction); return;
                    case OpStore:
                        Line(ValueExpression(instruction.operand(0)) + " = " +
                             ValueExpression(instruction.operand(1)) + ";");
                        return;
                    case OpAccessChain: EmitAccessChain(instruction); return;
                    case OpCopyObject:
                        expr_[instruction.operand(1)] = ValueExpression(instruction.operand(2));
                        resultType_[instruction.operand(1)] = instruction.operand(0);
                        return;
                    case OpSampledImage:
                        sampledPairs_[instruction.operand(1)] = {
                            ValueExpression(instruction.operand(2)),
                            ValueExpression(instruction.operand(3))};
                        resultType_[instruction.operand(1)] = instruction.operand(0);
                        return;
                    case OpImageSampleImplicitLod: EmitImageSample(instruction); return;
                    case OpImageSampleExplicitLod:
                        Refuse("OpImageSampleExplicitLod is outside the compiled-effect subset");
                    case OpVectorShuffle: EmitVectorShuffle(instruction); return;
                    case OpCompositeConstruct: EmitCompositeConstruct(instruction); return;
                    case OpCompositeExtract: EmitCompositeExtract(instruction); return;
                    case OpExtInst: EmitExtInst(instruction); return;
                    case OpPhi:
                        Refuse("OpPhi: this subset's control flow never joins two value versions");
                    default: break;
                }
                EmitArithmetic(instruction);
            }

            void EmitLoad(const Instruction& instruction)
            {
                const std::uint32_t resultId = instruction.operand(1);
                const std::uint32_t pointerId = instruction.operand(2);
                const std::uint32_t typeId = instruction.operand(0);
                const Type& loaded = TypeOf(typeId);
                // A texture or sampler is a handle in WGSL, not a value: the module-scope name IS
                // the expression, and no `let` may be introduced for it.
                if (loaded.kind == TypeKind::Image || loaded.kind == TypeKind::Sampler)
                {
                    expr_[resultId] = ValueExpression(pointerId);
                    resultType_[resultId] = typeId;
                    return;
                }
                Define(resultId, typeId, ValueExpression(pointerId));
            }

            void EmitAccessChain(const Instruction& instruction)
            {
                const std::uint32_t resultId = instruction.operand(1);
                const std::uint32_t baseId = instruction.operand(2);
                std::string path = ValueExpression(baseId);
                std::uint32_t currentType = TypeOf(TypeIdOfValue(baseId)).inner;

                for (std::uint32_t w = 4; w < instruction.length; ++w)
                {
                    const std::uint32_t indexId = instruction.words[w];
                    const Type& type = TypeOf(currentType);
                    if (type.kind == TypeKind::Struct)
                    {
                        const std::uint32_t member = ConstantScalar(indexId);
                        if (member >= type.members.size())
                            Refuse("struct member index out of range");
                        path += ".m" + std::to_string(member);
                        // Remember which struct/member we are inside, so a widened scalar array
                        // can take its `.x` after the element index below.
                        const bool widened =
                            widenedMembers_.count({currentType, member}) != 0;
                        currentType = type.members[member];
                        if (widened)
                        {
                            const Type& array = TypeOf(currentType);
                            if (w + 1 >= instruction.length)
                                Refuse("a widened uniform array used without an element index");
                            path += "[" + IndexExpression(instruction.words[++w]) + "].x";
                            currentType = array.inner;
                        }
                    }
                    else if (type.kind == TypeKind::Array || type.kind == TypeKind::Vector)
                    {
                        path += "[" + IndexExpression(indexId) + "]";
                        currentType = type.inner;
                    }
                    else
                    {
                        Refuse("an access chain into a type this subset does not index");
                    }
                }
                // A pointer keeps its access path rather than becoming a `let`: WGSL has no
                // pointer-to-uniform value, and the path is what a load or store needs anyway.
                expr_[resultId] = path;
                resultType_[resultId] = instruction.operand(0);
            }

            /// WGSL indexes with i32/u32; MojoShader's indices are already 32-bit ints.
            std::string IndexExpression(std::uint32_t id)
            {
                const std::string text = ValueExpression(id);
                const Type& type = TypeOf(TypeIdOfValue(id));
                if (type.kind != TypeKind::Int)
                    Refuse("a non-integer index in an access chain");
                return text;
            }

            void EmitImageSample(const Instruction& instruction)
            {
                const std::uint32_t resultId = instruction.operand(1);
                const std::uint32_t sampledId = instruction.operand(2);
                const std::uint32_t coordinateId = instruction.operand(3);
                if (instruction.length > 5)
                {
                    Refuse("image operands (bias, lod, offset) are outside the compiled-effect "
                           "subset");
                }
                const auto pair = sampledPairs_.find(sampledId);
                if (pair == sampledPairs_.end())
                    Refuse("an image sample whose sampled image was never formed");

                // The image's dimensionality decides the coordinate arity; MojoShader hands over
                // whatever vector the shader had.
                const Type& sampledImage = TypeOf(TypeIdOfValue(sampledId));
                const Type& image = TypeOf(sampledImage.inner);
                const std::uint32_t needed = CoordinateComponents(image);
                const std::string coordinate =
                    Narrow(ValueExpression(coordinateId), TypeIdOfValue(coordinateId), needed);
                Define(resultId, instruction.operand(0),
                       "textureSample(" + pair->second.first + ", " + pair->second.second + ", " +
                           coordinate + ")");
            }

            /// Swizzles a value down to @p components, which is what a texture coordinate needs
            /// when the shader carries it in a wider vector.
            std::string Narrow(const std::string& expression, std::uint32_t typeId,
                               std::uint32_t components)
            {
                const Type& type = TypeOf(typeId);
                const std::uint32_t have = type.kind == TypeKind::Vector ? type.count : 1u;
                if (have == components) return expression;
                if (have < components)
                {
                    Refuse("a texture coordinate with " + std::to_string(have) +
                           " components where " + std::to_string(components) + " are required");
                }
                static const char* kComponents = "xyzw";
                std::string swizzle = ".";
                for (std::uint32_t c = 0; c < components; ++c) swizzle += kComponents[c];
                return "(" + expression + ")" + swizzle;
            }

            void EmitVectorShuffle(const Instruction& instruction)
            {
                const std::uint32_t resultId = instruction.operand(1);
                const std::uint32_t typeId = instruction.operand(0);
                const std::uint32_t firstId = instruction.operand(2);
                const std::uint32_t secondId = instruction.operand(3);
                const Type& firstType = TypeOf(TypeIdOfValue(firstId));
                const std::uint32_t firstCount =
                    firstType.kind == TypeKind::Vector ? firstType.count : 1u;
                const std::string first = ValueExpression(firstId);
                const std::string second = ValueExpression(secondId);

                static const char* kComponents = "xyzw";
                std::string text = TypeName(typeId) + "(";
                for (std::uint32_t w = 5; w < instruction.length; ++w)
                {
                    if (w != 5) text += ", ";
                    const std::uint32_t component = instruction.words[w];
                    if (component == 0xFFFFFFFFu)
                    {
                        // An undefined component. WGSL has no undef; zero is the only value that
                        // is both defined and cannot change a result that never reads it.
                        text += "0.0f";
                    }
                    else if (component < firstCount)
                    {
                        text += "(" + first + ")." + kComponents[component];
                    }
                    else
                    {
                        text += "(" + second + ")." + kComponents[component - firstCount];
                    }
                }
                Define(resultId, typeId, text + ")");
            }

            void EmitCompositeConstruct(const Instruction& instruction)
            {
                std::string text = TypeName(instruction.operand(0)) + "(";
                for (std::uint32_t w = 3; w < instruction.length; ++w)
                {
                    if (w != 3) text += ", ";
                    text += ValueExpression(instruction.words[w]);
                }
                Define(instruction.operand(1), instruction.operand(0), text + ")");
            }

            void EmitCompositeExtract(const Instruction& instruction)
            {
                if (instruction.length != 5)
                    Refuse("only a single-level OpCompositeExtract is translated");
                const std::uint32_t component = instruction.words[4];
                static const char* kComponents = "xyzw";
                if (component > 3) Refuse("OpCompositeExtract beyond a vector's four components");
                Define(instruction.operand(1), instruction.operand(0),
                       "(" + ValueExpression(instruction.operand(2)) + ")." +
                           kComponents[component]);
            }

            void EmitExtInst(const Instruction& instruction)
            {
                if (glslExtSets_.count(instruction.operand(2)) == 0)
                    Refuse("an extended instruction from a set this subset does not translate");
                const std::uint32_t which = instruction.operand(3);
                std::vector<std::string> arguments;
                for (std::uint32_t w = 5; w < instruction.length; ++w)
                    arguments.push_back(ValueExpression(instruction.words[w]));

                const auto call = [&](const char* name, std::size_t arity) {
                    if (arguments.size() != arity)
                    {
                        Refuse(std::string(name) + " with " + std::to_string(arguments.size()) +
                               " arguments");
                    }
                    std::string text = std::string(name) + "(";
                    for (std::size_t a = 0; a < arguments.size(); ++a)
                    {
                        if (a != 0) text += ", ";
                        text += arguments[a];
                    }
                    return text + ")";
                };

                std::string expression;
                switch (which)
                {
                    case Glsl450Round: expression = call("round", 1); break;
                    case Glsl450FAbs: expression = call("abs", 1); break;
                    case Glsl450FSign: expression = call("sign", 1); break;
                    case Glsl450Floor: expression = call("floor", 1); break;
                    case Glsl450Ceil: expression = call("ceil", 1); break;
                    case Glsl450Fract: expression = call("fract", 1); break;
                    case Glsl450Sin: expression = call("sin", 1); break;
                    case Glsl450Cos: expression = call("cos", 1); break;
                    case Glsl450Pow: expression = call("pow", 2); break;
                    case Glsl450Exp2: expression = call("exp2", 1); break;
                    case Glsl450Log2: expression = call("log2", 1); break;
                    case Glsl450Sqrt: expression = call("sqrt", 1); break;
                    case Glsl450InverseSqrt: expression = call("inverseSqrt", 1); break;
                    case Glsl450FMin: expression = call("min", 2); break;
                    case Glsl450FMax: expression = call("max", 2); break;
                    case Glsl450FClamp: expression = call("clamp", 3); break;
                    case Glsl450FMix: expression = call("mix", 3); break;
                    case Glsl450Cross: expression = call("cross", 2); break;
                    case Glsl450Normalize: expression = call("normalize", 1); break;
                    case Glsl450Reflect: expression = call("reflect", 2); break;
                    default:
                        Refuse("GLSL.std.450 instruction " + std::to_string(which) +
                               " is outside the compiled-effect subset");
                }
                Define(instruction.operand(1), instruction.operand(0), expression);
            }

            void EmitArithmetic(const Instruction& instruction)
            {
                const auto binary = [&](const char* op) {
                    return "(" + ValueExpression(instruction.operand(2)) + " " + op + " " +
                           ValueExpression(instruction.operand(3)) + ")";
                };

                std::string expression;
                switch (instruction.opcode)
                {
                    case OpFNegate:
                        expression = "-(" + ValueExpression(instruction.operand(2)) + ")";
                        break;
                    case OpIAdd:
                    case OpFAdd: expression = binary("+"); break;
                    case OpISub:
                    case OpFSub: expression = binary("-"); break;
                    case OpIMul:
                    case OpFMul: expression = binary("*"); break;
                    case OpSDiv:
                    case OpFDiv: expression = binary("/"); break;
                    case OpDot:
                        expression = "dot(" + ValueExpression(instruction.operand(2)) + ", " +
                                     ValueExpression(instruction.operand(3)) + ")";
                        break;
                    case OpAny:
                        expression = "any(" + ValueExpression(instruction.operand(2)) + ")";
                        break;
                    case OpAll:
                        expression = "all(" + ValueExpression(instruction.operand(2)) + ")";
                        break;
                    case OpLogicalOr: expression = binary("||"); break;
                    case OpLogicalAnd: expression = binary("&&"); break;
                    case OpLogicalNot:
                        expression = "!(" + ValueExpression(instruction.operand(2)) + ")";
                        break;
                    case OpConvertFToS:
                        expression = TypeName(instruction.operand(0)) + "(" +
                                     ValueExpression(instruction.operand(2)) + ")";
                        break;
                    case OpSelect:
                        // WGSL takes the false value first; SPIR-V names the true value first.
                        expression = "select(" + ValueExpression(instruction.operand(4)) + ", " +
                                     ValueExpression(instruction.operand(3)) + ", " +
                                     ValueExpression(instruction.operand(2)) + ")";
                        break;
                    case OpFOrdEqual: expression = binary("=="); break;
                    case OpFOrdNotEqual: expression = binary("!="); break;
                    case OpFOrdLessThan: expression = binary("<"); break;
                    case OpFOrdLessThanEqual: expression = binary("<="); break;
                    case OpFOrdGreaterThan: expression = binary(">"); break;
                    case OpFOrdGreaterThanEqual: expression = binary(">="); break;
                    // The unordered forms are true when either operand is NaN, which is exactly
                    // the negation of the opposite ordered comparison. WGSL has no unordered
                    // operator, so spell the negation rather than approximate with the ordered
                    // one.
                    case OpFUnordGreaterThan: expression = "!" + binary("<="); break;
                    case OpFUnordGreaterThanEqual: expression = "!" + binary("<"); break;
                    default:
                        Refuse("SPIR-V opcode " + std::to_string(instruction.opcode) +
                               " is outside the compiled-effect subset");
                }
                Define(instruction.operand(1), instruction.operand(0), expression);
            }

            // --- entry point wrapper -------------------------------------------------------
            struct IoMember
            {
                std::uint32_t id = 0;
                std::string attribute;
                std::string type;
                std::string field;
                std::uint32_t order = 0;
            };

            std::string BuiltInAttribute(std::uint32_t builtIn, bool isInput) const
            {
                switch (builtIn)
                {
                    case BuiltInPosition:
                        return isInput ? "@builtin(position)" : "@builtin(position)";
                    case BuiltInFragCoord: return "@builtin(position)";
                    case BuiltInFrontFacing: return "@builtin(front_facing)";
                    case BuiltInFragDepth: return "@builtin(frag_depth)";
                    case BuiltInPointSize:
                        Refuse("BuiltIn PointSize: WebGPU has no point size, so a shader that "
                               "writes one cannot be expressed in WGSL");
                    case BuiltInPointCoord:
                        Refuse("BuiltIn PointCoord: WebGPU has no point-sprite coordinate, so a "
                               "shader that reads one cannot be expressed in WGSL");
                    default: break;
                }
                Refuse("BuiltIn " + std::to_string(builtIn) +
                       " is outside the compiled-effect subset");
            }

            std::vector<IoMember> CollectIo(std::uint32_t storageClass)
            {
                std::vector<IoMember> members;
                for (const std::uint32_t id : entryInterface_)
                {
                    const auto variable = variables_.find(id);
                    if (variable == variables_.end()) continue;
                    if (variable->second.storageClass != storageClass) continue;
                    if (std::any_of(members.begin(), members.end(),
                                    [id](const IoMember& m) { return m.id == id; }))
                    {
                        continue;
                    }

                    IoMember member;
                    member.id = id;
                    member.type = TypeName(TypeOf(variable->second.typeId).inner);
                    if (HasDecoration(id, DecorationBuiltIn))
                    {
                        member.attribute = BuiltInAttribute(
                            DecorationValue(id, DecorationBuiltIn), storageClass == StorageInput);
                        member.field = "builtin" + std::to_string(id);
                        member.order = 0xFFFFu;
                    }
                    else if (HasDecoration(id, DecorationLocation))
                    {
                        const std::uint32_t location = DecorationValue(id, DecorationLocation);
                        member.attribute = "@location(" + std::to_string(location) + ")";
                        member.field = "loc" + std::to_string(location);
                        member.order = location;
                    }
                    else
                    {
                        Refuse("entry point variable %" + std::to_string(id) +
                               " carries neither a Location nor a BuiltIn decoration");
                    }
                    members.push_back(member);
                }
                std::sort(members.begin(), members.end(),
                          [](const IoMember& a, const IoMember& b) { return a.order < b.order; });
                return members;
            }

            std::string EmitEntryWrapper()
            {
                const std::vector<IoMember> inputs = CollectIo(StorageInput);
                const std::vector<IoMember> outputs = CollectIo(StorageOutput);
                const std::string stage =
                    executionModel_ == ExecutionModelVertex ? "@vertex" : "@fragment";

                std::string text;
                if (!inputs.empty())
                {
                    text += "\nstruct " + entryName_ + "In {\n";
                    for (const IoMember& member : inputs)
                    {
                        text += "    " + member.attribute + " " + member.field + " : " +
                                member.type + ",\n";
                    }
                    text += "};\n";
                }
                if (outputs.empty())
                    Refuse("an entry point with no outputs, which no compiled effect produces");

                text += "\nstruct " + entryName_ + "Out {\n";
                for (const IoMember& member : outputs)
                {
                    text += "    " + member.attribute + " " + member.field + " : " + member.type +
                            ",\n";
                }
                text += "};\n";

                text += "\n" + stage + " fn " + entryName_ + "(";
                if (!inputs.empty()) text += "stageIn : " + entryName_ + "In";
                text += ") -> " + entryName_ + "Out {\n";
                for (const IoMember& member : inputs)
                    text += "    " + NameFor(member.id) + " = stageIn." + member.field + ";\n";
                text += "    " + entryName_ + "_body();\n";
                text += "    var stageOut : " + entryName_ + "Out;\n";
                for (const IoMember& member : outputs)
                    text += "    stageOut." + member.field + " = " + NameFor(member.id) + ";\n";
                text += "    return stageOut;\n";
                text += "}\n";
                return text;
            }
        };
    }

    SpirvToWgslResult TranslateSpirvToWgsl(const std::uint32_t* words, std::size_t wordCount)
    {
        Translator translator(words, wordCount);
        return translator.Run();
    }
}
