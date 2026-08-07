#include "CNA/Internal/Backends/Skia/SkiaGlslToSkslTranslatorEXT.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include <array>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        enum class TokenKind
        {
            Identifier,
            Number,
            Punct,
            Preprocessor,
            End,
        };

        struct Token final
        {
            TokenKind kind = TokenKind::End;
            std::string text;
            int line = 0;
            int column = 0;
        };

        [[nodiscard]] std::string Loc(const Token& token)
        {
            return std::to_string(token.line) + ":" + std::to_string(token.column);
        }

        // docs/skia-glsl-to-sksl-translator-contract.md's unconditional reject list -- identifiers
        // disallowed anywhere in the source, regardless of surrounding grammar.
        const std::unordered_set<std::string>& DisallowedIdentifiers()
        {
            static const std::unordered_set<std::string> names = {
                "discard", "gl_FragDepth", "dFdx", "dFdy", "fwidth", "textureLod", "precision",
                "samplerCube", "sampler3D", "cnaSampleCubeEXT", "cnaSampleVolumeEXT", "cnaSampleUV",
                "CNA_GL_RT_SAMPLE_UV_DECL",
            };
            return names;
        }

        [[nodiscard]] bool IsIdentStart(char c) noexcept
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        [[nodiscard]] bool IsIdentChar(char c) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        [[nodiscard]] std::vector<Token> Tokenize(const std::string& source, std::string& error)
        {
            std::vector<Token> tokens;
            int line = 1;
            int column = 1;
            std::size_t i = 0;
            const std::size_t n = source.size();
            auto advance = [&](std::size_t count = 1) {
                for (std::size_t k = 0; k < count && i < n; ++k)
                {
                    if (source[i] == '\n') { ++line; column = 1; }
                    else { ++column; }
                    ++i;
                }
            };
            while (i < n)
            {
                const char c = source[i];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
                if (c == '/' && i + 1 < n && source[i + 1] == '/')
                {
                    while (i < n && source[i] != '\n') advance();
                    continue;
                }
                if (c == '/' && i + 1 < n && source[i + 1] == '*')
                {
                    advance(2);
                    while (i + 1 < n && !(source[i] == '*' && source[i + 1] == '/')) advance();
                    advance(2);
                    continue;
                }
                if (c == '#')
                {
                    const int startLine = line, startColumn = column;
                    std::size_t start = i;
                    while (i < n && source[i] != '\n') advance();
                    tokens.push_back({TokenKind::Preprocessor, source.substr(start, i - start),
                                      startLine, startColumn});
                    continue;
                }
                if (IsIdentStart(c))
                {
                    const int startLine = line, startColumn = column;
                    std::size_t start = i;
                    while (i < n && IsIdentChar(source[i])) advance();
                    tokens.push_back({TokenKind::Identifier, source.substr(start, i - start),
                                      startLine, startColumn});
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c))
                    || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(source[i + 1]))))
                {
                    const int startLine = line, startColumn = column;
                    std::size_t start = i;
                    while (i < n && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '.'))
                        advance();
                    tokens.push_back({TokenKind::Number, source.substr(start, i - start),
                                      startLine, startColumn});
                    continue;
                }
                // Multi-character punctuation, longest match first.
                static const std::array<const char*, 10> kMulti = {
                    "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=",
                };
                bool matchedMulti = false;
                for (const char* op : kMulti)
                {
                    const std::size_t len = std::char_traits<char>::length(op);
                    if (source.compare(i, len, op) == 0)
                    {
                        tokens.push_back({TokenKind::Punct, op, line, column});
                        advance(len);
                        matchedMulti = true;
                        break;
                    }
                }
                if (matchedMulti) continue;
                tokens.push_back({TokenKind::Punct, std::string(1, c), line, column});
                advance();
            }
            tokens.push_back({TokenKind::End, "", line, column});
            (void)error;
            return tokens;
        }

        // Type-keyword rename table (docs/skia-glsl-to-sksl-translator-contract.md).
        const std::unordered_map<std::string, std::string>& TypeRenames()
        {
            static const std::unordered_map<std::string, std::string> renames = {
                {"vec2", "float2"}, {"vec3", "float3"}, {"vec4", "float4"},
                {"mat3", "float3x3"}, {"mat4", "float4x4"},
            };
            return renames;
        }

        [[nodiscard]] bool IsAcceptedType(const std::string& text) noexcept
        {
            return text == "float" || text == "int" || text == "vec2" || text == "vec3"
                || text == "vec4" || text == "mat3" || text == "mat4" || text == "sampler2D";
        }

        struct UniformDeclEXT final { std::string type; std::string name; };

        class TranslatorEXT final
        {
        public:
            explicit TranslatorEXT(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

            [[nodiscard]] GlslToSkslTranslationResultEXT Run()
            {
                if (const std::string reject = RejectScan(); !reject.empty())
                    return Fail(reject);
                if (const std::string reject = ParseTopLevel(); !reject.empty())
                    return Fail(reject);
                return EmitEXT();
            }

        private:
            std::vector<Token> tokens_;
            std::vector<UniformDeclEXT> uniforms_;
            // Maps each declared `sampler2D` uniform's original GLSL name to the reserved mesh-ABI
            // child name it is renamed to (`cnaTexture0`, `cnaTexture1`, ... in declaration order)
            // -- the mesh ABI (SkiaMeshEffectBackend, SKIA-154) requires exactly this naming for
            // its shader children, unlike `in`/`out` variables, which keep their own GLSL names
            // (see docs/skia-glsl-to-sksl-translator-contract.md's own rationale for those two).
            std::unordered_map<std::string, std::string> samplerRenames_;
            int nextSamplerUnit_ = 0;
            std::string inName_;
            std::string outName_;
            std::size_t bodyStart_ = 0;
            std::size_t bodyEnd_ = 0;

            [[nodiscard]] static GlslToSkslTranslationResultEXT Fail(std::string message)
            {
                GlslToSkslTranslationResultEXT result;
                result.success = false;
                result.errorMessage = std::move(message);
                return result;
            }

            [[nodiscard]] std::string RejectScan() const
            {
                for (const Token& token : tokens_)
                {
                    if (token.kind == TokenKind::Preprocessor)
                    {
                        std::string text = token.text;
                        std::size_t firstNonHashSpace = text.find_first_not_of(" \t", 1);
                        const bool isVersion = firstNonHashSpace != std::string::npos
                            && text.compare(firstNonHashSpace, 7, "version") == 0;
                        if (!isVersion)
                        {
                            return "Skia GLSL-to-SkSL translator: preprocessor directives other "
                                   "than `#version` are not supported (" + Loc(token) + ").";
                        }
                    }
                    if (token.kind != TokenKind::Identifier) continue;
                    if (DisallowedIdentifiers().count(token.text) != 0u)
                    {
                        return "Skia GLSL-to-SkSL translator: `" + token.text
                            + "` is not part of the accepted grammar (" + Loc(token) + ").";
                    }
                }
                return {};
            }

            [[nodiscard]] std::string ParseTopLevel()
            {
                std::size_t i = 0;
                bool sawMain = false;
                while (tokens_[i].kind != TokenKind::End)
                {
                    const Token& first = tokens_[i];
                    if (first.kind == TokenKind::Preprocessor) { ++i; continue; }
                    if (first.kind != TokenKind::Identifier)
                    {
                        return "Skia GLSL-to-SkSL translator: unexpected top-level token `"
                            + first.text + "` (" + Loc(first) + ").";
                    }
                    if (first.text == "uniform")
                    {
                        const Token& type = tokens_[i + 1];
                        const Token& name = tokens_[i + 2];
                        const Token& semi = tokens_[i + 3];
                        if (!IsAcceptedType(type.text) || name.kind != TokenKind::Identifier
                            || semi.text != ";")
                        {
                            return "Skia GLSL-to-SkSL translator: malformed `uniform` declaration ("
                                + Loc(first) + ").";
                        }
                        uniforms_.push_back({type.text, name.text});
                        if (type.text == "sampler2D")
                        {
                            if (nextSamplerUnit_ > kSkiaSkslMaxTextureUnitEXT)
                            {
                                return "Skia GLSL-to-SkSL translator: at most 8 `sampler2D` "
                                       "uniforms are supported (cnaTexture0-7) (" + Loc(first) + ").";
                            }
                            samplerRenames_[name.text] = "cnaTexture" + std::to_string(nextSamplerUnit_++);
                        }
                        i += 4;
                        continue;
                    }
                    if (first.text == "in")
                    {
                        const Token& type = tokens_[i + 1];
                        const Token& name = tokens_[i + 2];
                        const Token& semi = tokens_[i + 3];
                        if (!inName_.empty())
                        {
                            return "Skia GLSL-to-SkSL translator: a second `in` varying is not "
                                   "supported -- only one `in vec2` UV varying is accepted ("
                                + Loc(first) + ").";
                        }
                        if (type.text != "vec2" || name.kind != TokenKind::Identifier || semi.text != ";")
                        {
                            return "Skia GLSL-to-SkSL translator: `in` varyings other than a single "
                                   "`in vec2` are not supported (" + Loc(first) + ").";
                        }
                        inName_ = name.text;
                        i += 4;
                        continue;
                    }
                    if (first.text == "out")
                    {
                        const Token& type = tokens_[i + 1];
                        const Token& name = tokens_[i + 2];
                        const Token& semi = tokens_[i + 3];
                        if (!outName_.empty())
                        {
                            return "Skia GLSL-to-SkSL translator: a second `out` declaration is not "
                                   "supported (multiple render targets are rejected) (" + Loc(first) + ").";
                        }
                        if (type.text != "vec4" || name.kind != TokenKind::Identifier || semi.text != ";")
                        {
                            return "Skia GLSL-to-SkSL translator: `out` declarations other than a "
                                   "single `out vec4` are not supported (" + Loc(first) + ").";
                        }
                        outName_ = name.text;
                        i += 4;
                        continue;
                    }
                    // Function definition: <type> <name> ( <params> ) { <body> }
                    if (IsAcceptedType(first.text) || first.text == "void")
                    {
                        const Token& name = tokens_[i + 1];
                        if (name.kind != TokenKind::Identifier || tokens_[i + 2].text != "(")
                        {
                            return "Skia GLSL-to-SkSL translator: unexpected top-level declaration ("
                                + Loc(first) + ").";
                        }
                        std::size_t j = i + 3;
                        while (tokens_[j].text != ")" && tokens_[j].kind != TokenKind::End) ++j;
                        const bool emptyParams = (j == i + 3);
                        ++j; // consume ')'
                        if (tokens_[j].text != "{")
                        {
                            return "Skia GLSL-to-SkSL translator: expected a function body after `"
                                + name.text + "(...)` (" + Loc(first) + ").";
                        }
                        const std::size_t braceOpen = j;
                        int depth = 0;
                        std::size_t k = braceOpen;
                        for (; tokens_[k].kind != TokenKind::End; ++k)
                        {
                            if (tokens_[k].text == "{") ++depth;
                            else if (tokens_[k].text == "}") { --depth; if (depth == 0) break; }
                        }
                        if (depth != 0)
                        {
                            return "Skia GLSL-to-SkSL translator: unbalanced braces in function `"
                                + name.text + "` (" + Loc(first) + ").";
                        }
                        const std::size_t braceClose = k;
                        if (name.text == "main")
                        {
                            if (sawMain)
                            {
                                return "Skia GLSL-to-SkSL translator: more than one `main` function "
                                       "is not supported (" + Loc(first) + ").";
                            }
                            if (first.text != "void" || !emptyParams)
                            {
                                return "Skia GLSL-to-SkSL translator: `main` must be exactly `void "
                                       "main()` (" + Loc(first) + ").";
                            }
                            sawMain = true;
                            bodyStart_ = braceOpen + 1;
                            bodyEnd_ = braceClose;
                        }
                        else
                        {
                            return "Skia GLSL-to-SkSL translator: helper function definitions (`"
                                + name.text + "`) are not supported by this MVP grammar -- only a "
                                "single `void main()` is accepted (" + Loc(first) + ").";
                        }
                        i = braceClose + 1;
                        continue;
                    }
                    return "Skia GLSL-to-SkSL translator: unexpected top-level token `" + first.text
                        + "` (" + Loc(first) + ").";
                }
                if (!sawMain)
                    return "Skia GLSL-to-SkSL translator: no `void main()` function was found.";
                if (inName_.empty())
                {
                    return "Skia GLSL-to-SkSL translator: this MVP grammar requires exactly one "
                           "`in vec2` UV varying declaration.";
                }
                if (outName_.empty())
                {
                    return "Skia GLSL-to-SkSL translator: this MVP grammar requires exactly one "
                           "`out vec4` colour declaration.";
                }
                return {};
            }

            [[nodiscard]] GlslToSkslTranslationResultEXT EmitEXT() const
            {
                std::ostringstream out;
                for (const UniformDeclEXT& uniform : uniforms_)
                {
                    if (uniform.type == "sampler2D")
                    {
                        // Renamed to the mesh ABI's own reserved cnaTexture0-7 child convention --
                        // see samplerRenames_'s own doc comment for why this differs from in/out.
                        out << "uniform shader " << samplerRenames_.at(uniform.name) << ";\n";
                        continue;
                    }
                    const std::string skslType = TypeRenames().count(uniform.type)
                        ? TypeRenames().at(uniform.type) : uniform.type;
                    out << "uniform " << skslType << " " << uniform.name << ";\n";
                }
                out << "half4 main(float2 " << inName_ << ") {\n";
                out << "half4 " << outName_ << ";\n";
                out << EmitBody();
                out << "\nreturn " << outName_ << ";\n}\n";

                GlslToSkslTranslationResultEXT result;
                result.success = true;
                result.sksl = out.str();
                return result;
            }

            [[nodiscard]] std::string EmitBody() const
            {
                std::ostringstream out;
                for (std::size_t i = bodyStart_; i < bodyEnd_; )
                {
                    const Token& token = tokens_[i];
                    // texture ( SAMPLER , args... ) -> cnaTextureN.eval( args... )
                    if (token.kind == TokenKind::Identifier && token.text == "texture"
                        && tokens_[i + 1].text == "(" && tokens_[i + 2].kind == TokenKind::Identifier
                        && samplerRenames_.count(tokens_[i + 2].text) != 0u
                        && tokens_[i + 3].text == ",")
                    {
                        out << samplerRenames_.at(tokens_[i + 2].text) << ".eval(";
                        std::size_t j = i + 4;
                        int depth = 1;
                        for (; tokens_[j].kind != TokenKind::End && depth > 0; ++j)
                        {
                            if (tokens_[j].text == "(") ++depth;
                            else if (tokens_[j].text == ")") { --depth; if (depth == 0) break; }
                            out << EmitToken(tokens_[j]) << " ";
                        }
                        out << ")";
                        i = j + 1;
                        continue;
                    }
                    out << EmitToken(token) << " ";
                    ++i;
                }
                return out.str();
            }

            [[nodiscard]] static std::string EmitToken(const Token& token)
            {
                if (token.kind == TokenKind::Identifier)
                {
                    const auto it = TypeRenames().find(token.text);
                    if (it != TypeRenames().end()) return it->second;
                }
                return token.text;
            }
        };
    }

    GlslToSkslTranslationResultEXT TranslateGlslToSkslEXT(const std::string& glslSource)
    {
        // SKIA-156: bound the raw GLSL input before tokenizing at all, matching the same ceiling
        // already applied to translated SkSL output at compile time (kSkiaSkslMaxSourceBytesEXT) --
        // a malicious or pathological caller cannot force unbounded tokenizer work.
        if (glslSource.size() > kSkiaSkslMaxSourceBytesEXT)
        {
            GlslToSkslTranslationResultEXT result;
            result.success = false;
            result.errorMessage = "Skia GLSL-to-SkSL translator: source exceeds the 65536-byte "
                "safety limit.";
            return result;
        }
        std::string tokenizeError;
        std::vector<Token> tokens = Tokenize(glslSource, tokenizeError);
        if (!tokenizeError.empty())
        {
            GlslToSkslTranslationResultEXT result;
            result.success = false;
            result.errorMessage = tokenizeError;
            return result;
        }
        TranslatorEXT translator(std::move(tokens));
        return translator.Run();
    }
} // namespace CNA::Internal::Backends::Skia
