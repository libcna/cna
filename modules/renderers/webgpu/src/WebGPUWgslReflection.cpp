// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/WebGPU/WebGPUWgslReflection.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <utility>

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        enum class TokenKind { Identifier, Number, Punct, End };

        struct Token
        {
            TokenKind kind = TokenKind::End;
            std::string text;
        };

        // WGSL comments nest (`/* /* */ */`), unlike C's; a nested one must not end the outer.
        std::string StripComments(std::string_view source)
        {
            std::string out;
            out.reserve(source.size());
            std::size_t i = 0;
            while (i < source.size())
            {
                if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    while (i < source.size() && source[i] != '\n') ++i;
                    continue;
                }
                if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*')
                {
                    int depth = 0;
                    while (i < source.size())
                    {
                        if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*')
                        {
                            ++depth;
                            i += 2;
                        }
                        else if (source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/')
                        {
                            --depth;
                            i += 2;
                            if (depth == 0) break;
                        }
                        else
                        {
                            ++i;
                        }
                    }
                    out.push_back(' ');
                    continue;
                }
                out.push_back(source[i]);
                ++i;
            }
            return out;
        }

        std::vector<Token> Tokenize(const std::string& text)
        {
            std::vector<Token> tokens;
            std::size_t i = 0;
            const auto isIdentStart = [](char c) {
                return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
            };
            const auto isIdent = [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
            };
            while (i < text.size())
            {
                const char c = text[i];
                if (std::isspace(static_cast<unsigned char>(c)) != 0)
                {
                    ++i;
                    continue;
                }
                if (isIdentStart(c))
                {
                    const std::size_t start = i;
                    while (i < text.size() && isIdent(text[i])) ++i;
                    tokens.push_back({TokenKind::Identifier, text.substr(start, i - start)});
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
                    (c == '.' && i + 1 < text.size() &&
                     std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0))
                {
                    const std::size_t start = i;
                    while (i < text.size() &&
                           (isIdent(text[i]) || text[i] == '.' ||
                            ((text[i] == '+' || text[i] == '-') && i > start &&
                             (text[i - 1] == 'e' || text[i - 1] == 'E'))))
                        ++i;
                    tokens.push_back({TokenKind::Number, text.substr(start, i - start)});
                    continue;
                }
                // Two-character operators matter only where they could be mistaken for template
                // brackets; `->` is the one this parser needs to recognise.
                if (c == '-' && i + 1 < text.size() && text[i + 1] == '>')
                {
                    tokens.push_back({TokenKind::Punct, "->"});
                    i += 2;
                    continue;
                }
                tokens.push_back({TokenKind::Punct, std::string(1, c)});
                ++i;
            }
            tokens.push_back({TokenKind::End, {}});
            return tokens;
        }

        std::optional<std::uint64_t> ParseInteger(std::string_view text)
        {
            while (!text.empty() && (text.back() == 'u' || text.back() == 'i'))
                text.remove_suffix(1);
            if (text.empty()) return std::nullopt;
            std::uint64_t value = 0;
            int base = 10;
            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            {
                base = 16;
                text.remove_prefix(2);
            }
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), value, base);
            if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
            return value;
        }

        struct Attribute
        {
            std::string name;
            std::vector<std::string> args;   // raw token texts, one per comma-separated argument
        };

        struct ParsedMember
        {
            std::vector<Attribute> attributes;
            std::string name;
            std::string type;
        };

        struct ParsedParameter
        {
            std::vector<Attribute> attributes;
            std::string name;
            std::string type;
        };

        [[nodiscard]] const Attribute* FindAttribute(
            const std::vector<Attribute>& attributes, std::string_view name)
        {
            for (const Attribute& a : attributes)
                if (a.name == name) return &a;
            return nullptr;
        }

        std::uint32_t RoundUp(std::uint32_t align, std::uint32_t value)
        {
            return align <= 1 ? value : (value + align - 1) / align * align;
        }

        // Splits `name<a, b<c>, d>` into the head name and its top-level template arguments.
        void SplitTemplate(std::string_view type, std::string& head, std::vector<std::string>& args)
        {
            head.clear();
            args.clear();
            const std::size_t open = type.find('<');
            if (open == std::string_view::npos || type.back() != '>')
            {
                head = std::string(type);
                return;
            }
            head = std::string(type.substr(0, open));
            const auto trimmed = [](const std::string& s) {
                const std::size_t first = s.find_first_not_of(" \t");
                if (first == std::string::npos) return std::string();
                const std::size_t last = s.find_last_not_of(" \t");
                return s.substr(first, last - first + 1);
            };
            int depth = 0;
            std::string current;
            for (std::size_t i = open + 1; i + 1 < type.size(); ++i)
            {
                const char c = type[i];
                if (c == '<') ++depth;
                if (c == '>') --depth;
                if (c == ',' && depth == 0)
                {
                    args.push_back(trimmed(current));
                    current.clear();
                    continue;
                }
                current.push_back(c);
            }
            if (!trimmed(current).empty()) args.push_back(trimmed(current));
        }

        struct ScalarInfo
        {
            std::uint32_t size = 0;
            bool known = false;
        };

        ScalarInfo ScalarOf(std::string_view name)
        {
            if (name == "f32" || name == "i32" || name == "u32") return {4, true};
            if (name == "f16") return {2, true};
            return {};
        }

        // WGSL's predeclared vector/matrix shorthands (`vec4f`, `mat4x4f` ...).
        std::string ExpandShorthand(const std::string& type)
        {
            const auto scalarFor = [](char suffix) -> const char* {
                switch (suffix)
                {
                case 'f': return "f32";
                case 'i': return "i32";
                case 'u': return "u32";
                case 'h': return "f16";
                default: return nullptr;
                }
            };
            if (type.size() == 5 && type.compare(0, 3, "vec") == 0 &&
                type[3] >= '2' && type[3] <= '4')
            {
                if (const char* s = scalarFor(type[4]))
                    return type.substr(0, 4) + "<" + s + ">";
            }
            if (type.size() == 7 && type.compare(0, 3, "mat") == 0 && type[4] == 'x' &&
                type[3] >= '2' && type[3] <= '4' && type[5] >= '2' && type[5] <= '4')
            {
                if (const char* s = scalarFor(type[6]))
                    return type.substr(0, 6) + "<" + s + ">";
            }
            return type;
        }

        class Parser
        {
        public:
            explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

            WgslModuleReflection Run()
            {
                WgslModuleReflection result;
                while (Peek().kind != TokenKind::End && error_.empty())
                    ParseModuleScope(result);
                if (!error_.empty())
                {
                    result.ok = false;
                    result.error = error_;
                    return result;
                }
                result.structs = std::move(structs_);
                result.ok = true;
                return result;
            }

        private:
            const Token& Peek(std::size_t ahead = 0) const
            {
                const std::size_t at = std::min(position_ + ahead, tokens_.size() - 1);
                return tokens_[at];
            }

            Token Next()
            {
                Token t = Peek();
                if (position_ < tokens_.size() - 1) ++position_;
                return t;
            }

            bool Accept(std::string_view punct)
            {
                if (Peek().kind == TokenKind::Punct && Peek().text == punct)
                {
                    Next();
                    return true;
                }
                return false;
            }

            bool Expect(std::string_view punct, const char* where)
            {
                if (Accept(punct)) return true;
                Fail(std::string("expected '") + std::string(punct) + "' " + where + ", found '" +
                     Peek().text + "'");
                return false;
            }

            void Fail(std::string message)
            {
                if (error_.empty()) error_ = std::move(message);
            }

            std::vector<Attribute> ParseAttributes()
            {
                std::vector<Attribute> attributes;
                while (Accept("@"))
                {
                    Attribute a;
                    a.name = Next().text;
                    if (Accept("("))
                    {
                        std::string current;
                        int depth = 0;
                        while (Peek().kind != TokenKind::End)
                        {
                            if (Peek().kind == TokenKind::Punct && Peek().text == ")" && depth == 0)
                                break;
                            const Token t = Next();
                            if (t.kind == TokenKind::Punct && t.text == "(") ++depth;
                            if (t.kind == TokenKind::Punct && t.text == ")") --depth;
                            if (t.kind == TokenKind::Punct && t.text == "," && depth == 0)
                            {
                                a.args.push_back(current);
                                current.clear();
                                continue;
                            }
                            current += t.text;
                        }
                        if (!current.empty()) a.args.push_back(current);
                        Expect(")", "closing an attribute");
                    }
                    attributes.push_back(std::move(a));
                }
                return attributes;
            }

            // A type expression: IDENT [ '<' arg (',' arg)* [','] '>' ]. Arguments are types or
            // constant expressions (array counts), both kept as normalised text.
            std::string ParseType()
            {
                if (Peek().kind != TokenKind::Identifier)
                {
                    Fail("expected a type, found '" + Peek().text + "'");
                    return {};
                }
                std::string type = Next().text;
                if (Accept("<"))
                {
                    type += "<";
                    bool first = true;
                    while (!(Peek().kind == TokenKind::Punct && Peek().text == ">"))
                    {
                        if (Peek().kind == TokenKind::End)
                        {
                            Fail("unterminated template list in a type");
                            return {};
                        }
                        if (!first) type += ", ";
                        first = false;
                        if (Peek().kind == TokenKind::Identifier)
                        {
                            type += ParseType();
                        }
                        else
                        {
                            // A constant expression (an array count). Keep its tokens verbatim.
                            std::string expression;
                            while (!(Peek().kind == TokenKind::Punct &&
                                     (Peek().text == "," || Peek().text == ">")) &&
                                   Peek().kind != TokenKind::End)
                                expression += Next().text;
                            type += expression;
                        }
                        if (!Accept(",")) break;
                        if (Peek().kind == TokenKind::Punct && Peek().text == ">") break;
                    }
                    Expect(">", "closing a template list");
                    type += ">";
                }
                return type;
            }

            void SkipBalancedUntil(std::string_view terminator)
            {
                int depth = 0;
                while (Peek().kind != TokenKind::End)
                {
                    const Token& t = Peek();
                    if (t.kind == TokenKind::Punct)
                    {
                        if (depth == 0 && t.text == terminator)
                        {
                            Next();
                            return;
                        }
                        if (t.text == "(" || t.text == "[" || t.text == "{") ++depth;
                        if (t.text == ")" || t.text == "]" || t.text == "}") --depth;
                    }
                    Next();
                }
            }

            void SkipBlock()
            {
                if (!Expect("{", "opening a block")) return;
                int depth = 1;
                while (depth > 0 && Peek().kind != TokenKind::End)
                {
                    const Token t = Next();
                    if (t.kind != TokenKind::Punct) continue;
                    if (t.text == "{") ++depth;
                    if (t.text == "}") --depth;
                }
            }

            void ParseModuleScope(WgslModuleReflection& result)
            {
                const std::vector<Attribute> attributes = ParseAttributes();
                const Token keyword = Next();
                if (keyword.kind != TokenKind::Identifier)
                {
                    if (keyword.kind == TokenKind::Punct && keyword.text == ";") return;
                    Fail("unexpected '" + keyword.text + "' at module scope");
                    return;
                }
                if (keyword.text == "struct")
                {
                    ParseStruct();
                    return;
                }
                if (keyword.text == "alias")
                {
                    const std::string name = Next().text;
                    if (!Expect("=", "in an alias")) return;
                    aliases_[name] = ParseType();
                    Accept(";");
                    return;
                }
                if (keyword.text == "var")
                {
                    ParseVar(attributes, result);
                    return;
                }
                if (keyword.text == "fn")
                {
                    ParseFunction(attributes, result);
                    return;
                }
                if (keyword.text == "const" || keyword.text == "override")
                {
                    const std::string name = Next().text;
                    if (Accept(":")) ParseType();
                    if (Accept("="))
                    {
                        std::string expression;
                        int depth = 0;
                        while (Peek().kind != TokenKind::End &&
                               !(depth == 0 && Peek().kind == TokenKind::Punct && Peek().text == ";"))
                        {
                            const Token t = Next();
                            if (t.kind == TokenKind::Punct && (t.text == "(" || t.text == "["))
                                ++depth;
                            if (t.kind == TokenKind::Punct && (t.text == ")" || t.text == "]"))
                                --depth;
                            expression += t.text;
                        }
                        if (const auto value = ParseInteger(expression)) constants_[name] = *value;
                    }
                    Accept(";");
                    return;
                }
                if (keyword.text == "enable" || keyword.text == "requires" ||
                    keyword.text == "diagnostic" || keyword.text == "const_assert" ||
                    keyword.text == "let")
                {
                    SkipBalancedUntil(";");
                    return;
                }
                Fail("unsupported module-scope declaration '" + keyword.text + "'");
            }

            void ParseStruct()
            {
                WgslStruct parsed;
                parsed.name = Next().text;
                if (!Expect("{", "opening a struct")) return;
                std::vector<ParsedMember> members;
                while (!Accept("}"))
                {
                    if (Peek().kind == TokenKind::End)
                    {
                        Fail("unterminated struct '" + parsed.name + "'");
                        return;
                    }
                    ParsedMember member;
                    member.attributes = ParseAttributes();
                    member.name = Next().text;
                    if (!Expect(":", "after a struct member name")) return;
                    member.type = ParseType();
                    if (!error_.empty()) return;
                    members.push_back(std::move(member));
                    if (!Accept(",")) Accept(";");
                }
                Accept(";");

                // Layout. A struct that holds a non-host-shareable member (a builtin I/O struct
                // with `bool`, say) keeps its members but a zero layout; it can never be the type
                // of a uniform/storage binding, so nothing depends on its size.
                bool hostShareable = true;
                std::uint32_t cursor = 0;
                std::uint32_t align = 1;
                for (std::size_t i = 0; i < members.size(); ++i)
                {
                    const ParsedMember& m = members[i];
                    WgslStructMember out;
                    out.name = m.name;
                    out.type = m.type;
                    const auto layout = WgslLayoutOf(m.type, structs_, aliases_);
                    if (!layout)
                    {
                        hostShareable = false;
                        parsed.members.push_back(std::move(out));
                        continue;
                    }
                    std::uint32_t memberAlign = layout->align;
                    std::uint32_t memberSize = layout->size;
                    if (const Attribute* a = FindAttribute(m.attributes, "align");
                        a != nullptr && !a->args.empty())
                    {
                        if (const auto v = ParseInteger(a->args[0])) memberAlign = static_cast<std::uint32_t>(*v);
                    }
                    if (const Attribute* a = FindAttribute(m.attributes, "size");
                        a != nullptr && !a->args.empty())
                    {
                        if (const auto v = ParseInteger(a->args[0])) memberSize = static_cast<std::uint32_t>(*v);
                    }
                    cursor = RoundUp(memberAlign, cursor);
                    out.offset = cursor;
                    out.size = memberSize;
                    cursor += memberSize;
                    align = std::max(align, memberAlign);
                    if (layout->runtimeSized) parsed.layout.runtimeSized = true;
                    parsed.members.push_back(std::move(out));
                }
                if (hostShareable)
                {
                    parsed.layout.align = align;
                    parsed.layout.size = RoundUp(align, cursor);
                }
                else
                {
                    parsed.layout = {};
                }
                structMembers_[parsed.name] = std::move(members);
                structs_[parsed.name] = std::move(parsed);
            }

            void ParseVar(const std::vector<Attribute>& attributes, WgslModuleReflection& result)
            {
                std::string addressSpace;
                std::string access;
                if (Accept("<"))
                {
                    addressSpace = Next().text;
                    if (Accept(",")) access = Next().text;
                    if (!Expect(">", "closing a var template")) return;
                }
                const std::string name = Next().text;
                std::string type;
                if (Accept(":")) type = ParseType();
                SkipBalancedUntil(";");
                if (!error_.empty()) return;

                if (addressSpace == "private" || addressSpace == "workgroup" ||
                    addressSpace == "function" || addressSpace == "immediate" ||
                    addressSpace == "push_constant")
                {
                    if (addressSpace == "immediate" || addressSpace == "push_constant")
                        Fail("'var<" + addressSpace + "> " + name +
                             "' is a wgpu-native immediate-data block, which this renderer does "
                             "not bind; declare it as var<uniform> instead");
                    return;
                }

                const Attribute* group = FindAttribute(attributes, "group");
                const Attribute* binding = FindAttribute(attributes, "binding");
                if (group == nullptr || binding == nullptr || group->args.empty() ||
                    binding->args.empty())
                {
                    Fail("resource '" + name + "' has no @group/@binding");
                    return;
                }
                const auto g = ParseInteger(group->args[0]);
                const auto b = ParseInteger(binding->args[0]);
                if (!g || !b)
                {
                    Fail("resource '" + name + "' has a non-literal @group/@binding");
                    return;
                }

                WgslResourceBinding resource;
                resource.group = static_cast<std::uint32_t>(*g);
                resource.binding = static_cast<std::uint32_t>(*b);
                resource.name = name;
                resource.type = ResolveAlias(type);

                if (addressSpace == "uniform" || addressSpace == "storage")
                {
                    if (addressSpace == "uniform")
                        resource.kind = WgslResourceKind::UniformBuffer;
                    else
                        resource.kind = access == "read_write" ? WgslResourceKind::StorageBuffer
                                                               : WgslResourceKind::ReadOnlyStorageBuffer;
                    const auto layout = WgslLayoutOf(resource.type, structs_, aliases_);
                    if (!layout)
                    {
                        Fail("buffer '" + name + "' has a type this renderer cannot lay out: '" +
                             resource.type + "'");
                        return;
                    }
                    resource.minBindingSize = layout->size;
                    result.resources.push_back(std::move(resource));
                    return;
                }
                if (!addressSpace.empty())
                {
                    Fail("resource '" + name + "' uses unsupported address space '" + addressSpace + "'");
                    return;
                }
                if (!ClassifyHandle(resource))
                {
                    Fail("resource '" + name + "' has an unsupported handle type '" + resource.type + "'");
                    return;
                }
                result.resources.push_back(std::move(resource));
            }

            std::string ResolveAlias(const std::string& type) const
            {
                std::string current = type;
                for (int i = 0; i < 16; ++i)
                {
                    const auto it = aliases_.find(current);
                    if (it == aliases_.end()) break;
                    current = it->second;
                }
                return current;
            }

            static bool ClassifyHandle(WgslResourceBinding& r)
            {
                std::string head;
                std::vector<std::string> args;
                SplitTemplate(r.type, head, args);
                if (head == "sampler")
                {
                    r.kind = WgslResourceKind::Sampler;
                    return true;
                }
                if (head == "sampler_comparison")
                {
                    r.kind = WgslResourceKind::ComparisonSampler;
                    return true;
                }
                struct Dim { const char* suffix; WGPUTextureViewDimension dim; };
                static constexpr Dim kSampled[] = {
                    {"texture_1d", WGPUTextureViewDimension_1D},
                    {"texture_2d", WGPUTextureViewDimension_2D},
                    {"texture_2d_array", WGPUTextureViewDimension_2DArray},
                    {"texture_3d", WGPUTextureViewDimension_3D},
                    {"texture_cube", WGPUTextureViewDimension_Cube},
                    {"texture_cube_array", WGPUTextureViewDimension_CubeArray},
                    {"texture_multisampled_2d", WGPUTextureViewDimension_2D},
                };
                for (const Dim& d : kSampled)
                {
                    if (head != d.suffix) continue;
                    r.kind = WgslResourceKind::SampledTexture;
                    r.viewDimension = d.dim;
                    r.multisampled = head == "texture_multisampled_2d";
                    const std::string scalar = args.empty() ? std::string("f32") : args[0];
                    if (scalar == "f32") r.sampleType = WGPUTextureSampleType_Float;
                    else if (scalar == "i32") r.sampleType = WGPUTextureSampleType_Sint;
                    else if (scalar == "u32") r.sampleType = WGPUTextureSampleType_Uint;
                    else return false;
                    return true;
                }
                static constexpr Dim kDepth[] = {
                    {"texture_depth_2d", WGPUTextureViewDimension_2D},
                    {"texture_depth_2d_array", WGPUTextureViewDimension_2DArray},
                    {"texture_depth_cube", WGPUTextureViewDimension_Cube},
                    {"texture_depth_cube_array", WGPUTextureViewDimension_CubeArray},
                    {"texture_depth_multisampled_2d", WGPUTextureViewDimension_2D},
                };
                for (const Dim& d : kDepth)
                {
                    if (head != d.suffix) continue;
                    r.kind = WgslResourceKind::SampledTexture;
                    r.viewDimension = d.dim;
                    r.sampleType = WGPUTextureSampleType_Depth;
                    r.multisampled = head == "texture_depth_multisampled_2d";
                    return true;
                }
                static constexpr Dim kStorage[] = {
                    {"texture_storage_1d", WGPUTextureViewDimension_1D},
                    {"texture_storage_2d", WGPUTextureViewDimension_2D},
                    {"texture_storage_2d_array", WGPUTextureViewDimension_2DArray},
                    {"texture_storage_3d", WGPUTextureViewDimension_3D},
                };
                for (const Dim& d : kStorage)
                {
                    if (head != d.suffix) continue;
                    if (args.size() != 2) return false;
                    r.kind = WgslResourceKind::StorageTexture;
                    r.viewDimension = d.dim;
                    r.storageFormat = WgslTexelFormatFromName(args[0]);
                    if (r.storageFormat == WGPUTextureFormat_Undefined) return false;
                    if (args[1] == "write") r.storageAccess = WGPUStorageTextureAccess_WriteOnly;
                    else if (args[1] == "read") r.storageAccess = WGPUStorageTextureAccess_ReadOnly;
                    else if (args[1] == "read_write")
                        r.storageAccess = WGPUStorageTextureAccess_ReadWrite;
                    else return false;
                    return true;
                }
                return false;
            }

            std::vector<ParsedParameter> ParseParameters()
            {
                std::vector<ParsedParameter> parameters;
                if (!Expect("(", "opening a parameter list")) return parameters;
                while (!Accept(")"))
                {
                    if (Peek().kind == TokenKind::End)
                    {
                        Fail("unterminated parameter list");
                        return parameters;
                    }
                    ParsedParameter p;
                    p.attributes = ParseAttributes();
                    p.name = Next().text;
                    if (!Expect(":", "after a parameter name")) return parameters;
                    p.type = ResolveAlias(ParseType());
                    parameters.push_back(std::move(p));
                    Accept(",");
                }
                return parameters;
            }

            void CollectLocations(const std::vector<Attribute>& attributes, const std::string& type,
                                  std::vector<WgslVertexInput>& out)
            {
                if (const Attribute* location = FindAttribute(attributes, "location");
                    location != nullptr && !location->args.empty())
                {
                    if (const auto v = ParseInteger(location->args[0]))
                        out.push_back({static_cast<std::uint32_t>(*v), type});
                    return;
                }
                const auto it = structMembers_.find(type);
                if (it == structMembers_.end()) return;
                for (const ParsedMember& m : it->second)
                    CollectLocations(m.attributes, ResolveAlias(m.type), out);
            }

            void ParseFunction(const std::vector<Attribute>& attributes, WgslModuleReflection& result)
            {
                const std::string name = Next().text;
                const std::vector<ParsedParameter> parameters = ParseParameters();
                std::vector<Attribute> returnAttributes;
                std::string returnType;
                if (Accept("->"))
                {
                    returnAttributes = ParseAttributes();
                    returnType = ResolveAlias(ParseType());
                }
                SkipBlock();
                if (!error_.empty()) return;

                WgslEntryPoint entry;
                entry.name = name;
                if (FindAttribute(attributes, "vertex") != nullptr)
                    entry.stage = WGPUShaderStage_Vertex;
                else if (FindAttribute(attributes, "fragment") != nullptr)
                    entry.stage = WGPUShaderStage_Fragment;
                else if (FindAttribute(attributes, "compute") != nullptr)
                    entry.stage = WGPUShaderStage_Compute;
                else
                    return;   // an ordinary function

                if (entry.stage == WGPUShaderStage_Vertex)
                {
                    for (const ParsedParameter& p : parameters)
                        CollectLocations(p.attributes, p.type, entry.vertexInputs);
                    std::sort(entry.vertexInputs.begin(), entry.vertexInputs.end(),
                              [](const WgslVertexInput& a, const WgslVertexInput& b) {
                                  return a.location < b.location;
                              });
                }
                if (entry.stage == WGPUShaderStage_Fragment && !returnType.empty())
                {
                    std::vector<WgslVertexInput> outputs;
                    CollectLocations(returnAttributes, returnType, outputs);
                    for (const WgslVertexInput& o : outputs)
                        entry.colorOutputCount = std::max(entry.colorOutputCount, o.location + 1);
                }
                if (entry.stage == WGPUShaderStage_Compute)
                {
                    const Attribute* size = FindAttribute(attributes, "workgroup_size");
                    if (size == nullptr || size->args.empty())
                    {
                        Fail("compute entry point '" + name + "' has no @workgroup_size");
                        return;
                    }
                    for (std::size_t axis = 0; axis < 3 && axis < size->args.size(); ++axis)
                    {
                        std::optional<std::uint64_t> value = ParseInteger(size->args[axis]);
                        if (!value)
                        {
                            const auto it = constants_.find(size->args[axis]);
                            if (it != constants_.end()) value = it->second;
                        }
                        if (!value)
                        {
                            Fail("compute entry point '" + name +
                                 "' has a @workgroup_size this renderer cannot evaluate: '" +
                                 size->args[axis] + "'");
                            return;
                        }
                        entry.workgroupSize[axis] = static_cast<std::uint32_t>(*value);
                    }
                }
                result.entryPoints.push_back(std::move(entry));
            }

            std::vector<Token> tokens_;
            std::size_t position_ = 0;
            std::string error_;
            std::unordered_map<std::string, WgslStruct> structs_;
            std::unordered_map<std::string, std::vector<ParsedMember>> structMembers_;
            std::unordered_map<std::string, std::string> aliases_;
            std::unordered_map<std::string, std::uint64_t> constants_;
        };
    }

    const WgslResourceBinding* WgslModuleReflection::FindResource(
        std::uint32_t group, std::uint32_t binding) const noexcept
    {
        for (const WgslResourceBinding& r : resources)
            if (r.group == group && r.binding == binding) return &r;
        return nullptr;
    }

    const WgslEntryPoint* WgslModuleReflection::FindEntryPoint(WGPUShaderStage stage) const noexcept
    {
        for (const WgslEntryPoint& e : entryPoints)
            if (e.stage == stage) return &e;
        return nullptr;
    }

    WgslModuleReflection ReflectWgsl(std::string_view source)
    {
        Parser parser(Tokenize(StripComments(source)));
        return parser.Run();
    }

    std::optional<WgslTypeLayout> WgslLayoutOf(
        std::string_view typeText, const std::unordered_map<std::string, WgslStruct>& structs,
        const std::unordered_map<std::string, std::string>& aliases)
    {
        std::string type = ExpandShorthand(std::string(typeText));
        for (int i = 0; i < 16; ++i)
        {
            const auto it = aliases.find(type);
            if (it == aliases.end()) break;
            type = ExpandShorthand(it->second);
        }

        if (const ScalarInfo scalar = ScalarOf(type); scalar.known)
            return WgslTypeLayout{scalar.size, scalar.size, false};

        std::string head;
        std::vector<std::string> args;
        SplitTemplate(type, head, args);

        if (head == "atomic" && args.size() == 1)
            return WgslTypeLayout{4, 4, false};

        if (head.size() == 4 && head.compare(0, 3, "vec") == 0 && args.size() == 1)
        {
            const ScalarInfo scalar = ScalarOf(args[0]);
            if (!scalar.known) return std::nullopt;
            const std::uint32_t n = static_cast<std::uint32_t>(head[3] - '0');
            if (n < 2 || n > 4) return std::nullopt;
            const std::uint32_t size = n * scalar.size;
            const std::uint32_t align = (n == 2 ? 2 : 4) * scalar.size;
            return WgslTypeLayout{size, align, false};
        }

        if (head.size() == 6 && head.compare(0, 3, "mat") == 0 && head[4] == 'x' && args.size() == 1)
        {
            const ScalarInfo scalar = ScalarOf(args[0]);
            if (!scalar.known) return std::nullopt;
            const std::uint32_t columns = static_cast<std::uint32_t>(head[3] - '0');
            const std::uint32_t rows = static_cast<std::uint32_t>(head[5] - '0');
            if (columns < 2 || columns > 4 || rows < 2 || rows > 4) return std::nullopt;
            const std::uint32_t columnSize = rows * scalar.size;
            const std::uint32_t columnAlign = (rows == 2 ? 2 : 4) * scalar.size;
            return WgslTypeLayout{columns * RoundUp(columnAlign, columnSize), columnAlign, false};
        }

        if (head == "array" && (args.size() == 1 || args.size() == 2))
        {
            const auto element = WgslLayoutOf(args[0], structs, aliases);
            if (!element || element->runtimeSized) return std::nullopt;
            const std::uint32_t stride = RoundUp(element->align, element->size);
            if (args.size() == 1)
                return WgslTypeLayout{stride, element->align, true};
            const auto count = ParseInteger(args[1]);
            if (!count || *count == 0) return std::nullopt;
            return WgslTypeLayout{static_cast<std::uint32_t>(*count) * stride, element->align, false};
        }

        if (args.empty())
        {
            const auto it = structs.find(type);
            if (it != structs.end() && it->second.layout.size > 0)
                return it->second.layout;
        }
        return std::nullopt;
    }

    WGPUTextureFormat WgslTexelFormatFromName(std::string_view name) noexcept
    {
        struct Entry { std::string_view name; WGPUTextureFormat format; };
        static constexpr Entry kFormats[] = {
            {"rgba8unorm", WGPUTextureFormat_RGBA8Unorm},
            {"rgba8snorm", WGPUTextureFormat_RGBA8Snorm},
            {"rgba8uint", WGPUTextureFormat_RGBA8Uint},
            {"rgba8sint", WGPUTextureFormat_RGBA8Sint},
            {"rgba16uint", WGPUTextureFormat_RGBA16Uint},
            {"rgba16sint", WGPUTextureFormat_RGBA16Sint},
            {"rgba16float", WGPUTextureFormat_RGBA16Float},
            {"r32uint", WGPUTextureFormat_R32Uint},
            {"r32sint", WGPUTextureFormat_R32Sint},
            {"r32float", WGPUTextureFormat_R32Float},
            {"rg32uint", WGPUTextureFormat_RG32Uint},
            {"rg32sint", WGPUTextureFormat_RG32Sint},
            {"rg32float", WGPUTextureFormat_RG32Float},
            {"rgba32uint", WGPUTextureFormat_RGBA32Uint},
            {"rgba32sint", WGPUTextureFormat_RGBA32Sint},
            {"rgba32float", WGPUTextureFormat_RGBA32Float},
            {"bgra8unorm", WGPUTextureFormat_BGRA8Unorm},
            {"r8unorm", WGPUTextureFormat_R8Unorm},
            {"r8snorm", WGPUTextureFormat_R8Snorm},
            {"r8uint", WGPUTextureFormat_R8Uint},
            {"r8sint", WGPUTextureFormat_R8Sint},
            {"rg8unorm", WGPUTextureFormat_RG8Unorm},
            {"rg8snorm", WGPUTextureFormat_RG8Snorm},
            {"rg8uint", WGPUTextureFormat_RG8Uint},
            {"rg8sint", WGPUTextureFormat_RG8Sint},
            {"r16uint", WGPUTextureFormat_R16Uint},
            {"r16sint", WGPUTextureFormat_R16Sint},
            {"r16float", WGPUTextureFormat_R16Float},
            {"rg16uint", WGPUTextureFormat_RG16Uint},
            {"rg16sint", WGPUTextureFormat_RG16Sint},
            {"rg16float", WGPUTextureFormat_RG16Float},
            {"rgb10a2uint", WGPUTextureFormat_RGB10A2Uint},
            {"rgb10a2unorm", WGPUTextureFormat_RGB10A2Unorm},
            {"rg11b10ufloat", WGPUTextureFormat_RG11B10Ufloat},
        };
        for (const Entry& e : kFormats)
            if (e.name == name) return e.format;
        return WGPUTextureFormat_Undefined;
    }
}
