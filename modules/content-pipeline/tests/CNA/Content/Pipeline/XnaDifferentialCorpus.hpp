// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_xnapipeline_parity.md XNAPP-265/XNAPP-267 (§23): what the two differential corpora
// share.
//
// `tools/xna-pipeline-oracle/differential/` runs Microsoft's own `BuildContent` over a committed
// manifest and records what it did. Two tests read those recordings -- one asks whether the same
// sources build (`XnaDifferentialBuildTests.cpp`), the other whether the same *failures* happen
// (`XnaErrorParityTests.cpp`) -- and both need the same four things: the manifest, the recording,
// a source root holding one case's source, and an environment that is put back afterwards.
//
// The JSON is read by scanning rather than by parsing. Both manifests are written by this
// repository, so their shape is known rather than arbitrary, and a test that pulls in a parser
// would be measuring the parser too.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "System/Environment.hpp"

namespace CNA::Tests::XnaDifferential
{
    /**
     * @brief Finds a repository-relative path from the working directory or from this file.
     *
     * @param relative The path as the repository spells it.
     * @return The located path, or @p relative when nothing above either root holds it.
     */
    inline std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    /**
     * @brief Reads a whole file as bytes.
     *
     * @param path The file.
     * @return Its content, or an empty string when it cannot be opened.
     */
    inline std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /**
     * @brief One scalar field of one JSON object, found by scanning rather than parsing.
     *
     * @param object The object's text, braces included.
     * @param key The field name.
     * @return The value with its quotes removed, or an empty string when the field is absent.
     */
    inline std::string Field(const std::string& object, const std::string& key)
    {
        const std::string needle = "\"" + key + "\":";
        const std::size_t at = object.find(needle);
        if (at == std::string::npos) { return {}; }
        std::size_t from = object.find_first_not_of(" \t", at + needle.size());
        if (from == std::string::npos) { return {}; }
        if (object[from] == '"')
        {
            const std::size_t end = object.find('"', from + 1u);
            return object.substr(from + 1u, end - from - 1u);
        }
        const std::size_t end = object.find_first_of(",}", from);
        return object.substr(from, end - from);
    }

    /**
     * @brief One string-array field of one JSON object.
     *
     * @param object The object's text.
     * @param key The field name.
     * @return Its elements, unescaped only for the sequences these manifests use.
     */
    inline std::vector<std::string> Strings(const std::string& object, const std::string& key)
    {
        std::vector<std::string> values;
        const std::string needle = "\"" + key + "\":";
        std::size_t at = object.find(needle);
        if (at == std::string::npos) { return values; }
        at = object.find('[', at);
        const std::size_t close = object.find(']', at);
        if (at == std::string::npos || close == std::string::npos) { return values; }
        std::size_t from = at + 1u;
        while (from < close)
        {
            const std::size_t start = object.find('"', from);
            if (start == std::string::npos || start > close) { break; }
            std::string value;
            std::size_t scan = start + 1u;
            while (scan < object.size() && object[scan] != '"')
            {
                if (object[scan] == '\\' && scan + 1u < object.size())
                {
                    ++scan;
                    const char escape = object[scan++];
                    value += escape == 'n' ? '\n' : escape == 't' ? '\t' : escape;
                    continue;
                }
                value += object[scan++];
            }
            values.push_back(value);
            from = scan + 1u;
        }
        return values;
    }

    /**
     * @brief Splits a flat JSON array of objects into the text of each object.
     *
     * @param text The whole document.
     * @param arrayKey The name of the array field.
     * @return One string per element, braces included.
     */
    inline std::vector<std::string> Objects(const std::string& text, const std::string& arrayKey)
    {
        std::vector<std::string> objects;
        const std::size_t start = text.find("\"" + arrayKey + "\"");
        if (start == std::string::npos) { return objects; }
        int depth = 0;
        std::size_t from = 0;
        for (std::size_t at = start; at < text.size(); ++at)
        {
            if (text[at] == '{')
            {
                if (depth == 0) { from = at; }
                ++depth;
            }
            else if (text[at] == '}')
            {
                --depth;
                if (depth == 0) { objects.push_back(text.substr(from, at - from + 1u)); }
            }
            else if (text[at] == ']' && depth == 0)
            {
                break;
            }
        }
        return objects;
    }

    /**
     * @brief The `parameters` object of a corpus row, as name/value pairs.
     *
     * @param row The row's text.
     * @return The pairs in the order the manifest wrote them.
     */
    inline std::vector<std::pair<std::string, std::string>> Parameters(const std::string& row)
    {
        std::vector<std::pair<std::string, std::string>> parameters;
        const std::size_t at = row.find("\"parameters\":");
        if (at == std::string::npos) { return parameters; }
        const std::size_t open = row.find('{', at);
        const std::size_t close = row.find('}', open);
        if (open == std::string::npos || close == std::string::npos) { return parameters; }
        const std::string body = row.substr(open + 1u, close - open - 1u);
        std::size_t from = 0;
        while (true)
        {
            const std::size_t keyStart = body.find('"', from);
            if (keyStart == std::string::npos) { break; }
            const std::size_t keyEnd = body.find('"', keyStart + 1u);
            const std::size_t valueStart = body.find('"', keyEnd + 1u);
            const std::size_t valueEnd = body.find('"', valueStart + 1u);
            if (valueEnd == std::string::npos) { break; }
            parameters.emplace_back(body.substr(keyStart + 1u, keyEnd - keyStart - 1u),
                                    body.substr(valueStart + 1u, valueEnd - valueStart - 1u));
            from = valueEnd + 1u;
        }
        return parameters;
    }

    /**
     * @brief Sets environment variables for the length of a test and puts them back.
     *
     * Restoring matters more than setting: a variable left behind is seen by every later test in
     * the process, and a leaked `CNA_FXC` sends unrelated builds through a Wine compiler they never
     * asked for. Only a null value removes a variable, so a previously-unset one is restored by
     * passing the empty optional through rather than an empty string.
     */
    class ScopedEnvironment
    {
    public:
        /** @brief Records the current value and sets a new one. */
        void Set(const std::string& name, const std::string& value)
        {
            previous_.emplace_back(name, System::Environment::GetEnvironmentVariable(name));
            System::Environment::SetEnvironmentVariable(name, value);
        }

        /** @brief Restores every variable this object set, newest first. */
        ~ScopedEnvironment()
        {
            for (auto entry = previous_.rbegin(); entry != previous_.rend(); ++entry)
            {
                System::Environment::SetEnvironmentVariable(entry->first, entry->second);
            }
        }

        /** @brief Creates an object that has set nothing. */
        ScopedEnvironment() = default;
        ScopedEnvironment(const ScopedEnvironment&) = delete;
        ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    private:
        std::vector<std::pair<std::string, std::optional<std::string>>> previous_;
    };

    /**
     * @brief Points the build at the effect compiler XNA used, when this machine has one.
     *
     * @param environment The guard that will put the variables back.
     * @return true when an `.fx` case can be built here at all.
     */
    inline bool ConfigureEffectCompiler(ScopedEnvironment& environment)
    {
        // Present-but-empty is not configured: a variable can be set to an empty string, and a
        // build told the compiler is "" fails instantly in a way that reads like a disagreement
        // about content.
        const char* configured = std::getenv("CNA_FXC");
        if (configured != nullptr && *configured != '\0') { return true; }
        std::error_code error;
        const std::filesystem::path fxc(
            "/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/fxc.exe");
        const char* home = std::getenv("HOME");
        const std::filesystem::path prefix =
            std::filesystem::path(home == nullptr ? "" : home) / ".wine-cna-xna40";
        if (!std::filesystem::exists(fxc, error) || error) { return false; }
        if (!std::filesystem::exists(prefix, error) || error) { return false; }
        environment.Set("CNA_FXC", fxc.string());
        environment.Set("CNA_FXC_LAUNCHER", "wine");
        environment.Set("WINEPREFIX", prefix.string());
        environment.Set("WINEDEBUG", "-all");
        return true;
    }

    /** @brief A source root holding one corpus source, removed when the case ends. */
    class OneSource
    {
    public:
        /**
         * @brief Copies one corpus source, and whatever it names beside it, into a fresh root.
         *
         * @param label A name unique to this case, used for the directory.
         * @param source The source's path under `tests/assets/xna40`.
         */
        OneSource(const std::string& label, const std::string& source)
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp265_" + label))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            const std::filesystem::path from = Locate("tests/assets/xna40") / source;
            name_ = from.filename().string();
            std::error_code error;
            std::filesystem::copy_file(from, Source() / name_,
                                       std::filesystem::copy_options::overwrite_existing, error);
            // A model's material names a texture beside it, so every image in the model corpus
            // travels with a model source. Every image rather than a list of names, because a list
            // goes stale the moment a fixture names a texture nobody remembered to add and the
            // symptom is a case that quietly stops being compared -- and only for a model, because
            // `BuildContent` builds the whole source root and a texture source's own directory
            // holds the malformed fixtures the refusal cases are made of.
            const std::string sourceExtension = from.extension().string();
            if (sourceExtension == ".x" || sourceExtension == ".fbx")
            {
                for (const std::filesystem::directory_entry& sibling :
                     std::filesystem::directory_iterator(from.parent_path(), error))
                {
                    if (error) { break; }
                    const std::string extension = sibling.path().extension().string();
                    if (extension == ".png" || extension == ".dds" || extension == ".tga" ||
                        extension == ".bmp" || extension == ".jpg")
                    {
                        std::error_code copyError;
                        std::filesystem::copy_file(sibling.path(),
                                                   Source() / sibling.path().filename(),
                                                   std::filesystem::copy_options::overwrite_existing,
                                                   copyError);
                    }
                }
            }
            // An effect's `#include` is resolved beside the source, so a header travels with it.
            // Only headers: an `.fx` source's own directory holds the effects the refusal cases
            // are made of, and `BuildContent` builds the whole source root.
            if (sourceExtension == ".fx")
            {
                for (const std::filesystem::directory_entry& sibling :
                     std::filesystem::directory_iterator(from.parent_path(), error))
                {
                    if (error) { break; }
                    const std::string extension = sibling.path().extension().string();
                    if (extension == ".fxh" || extension == ".h" || extension == ".inc")
                    {
                        std::error_code copyError;
                        std::filesystem::copy_file(sibling.path(),
                                                   Source() / sibling.path().filename(),
                                                   std::filesystem::copy_options::overwrite_existing,
                                                   copyError);
                    }
                }
            }
            // The two sides resolve a font differently on purpose: XNA asks Windows for an
            // installed family, CNA looks for a file, so that a content build produces the same
            // bytes on every machine. The corpus names the family Wine already resolves for XNA;
            // the same face is put beside the description under that name, which is where CNA
            // looks first. One committed copy of the font, presented to each side the way that
            // side resolves one (plans/plan_xnapipeline_parity.md XNAPP-267).
            if (sourceExtension == ".spritefont")
            {
                std::error_code copyError;
                std::filesystem::copy_file(Locate("tests/assets/fonts/LiberationMono-Regular.ttf"),
                                           Source() / "Liberation Mono.ttf",
                                           std::filesystem::copy_options::overwrite_existing,
                                           copyError);
            }
        }

        /** @brief Removes the root and everything built in it. */
        ~OneSource()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }

        OneSource(const OneSource&) = delete;
        OneSource& operator=(const OneSource&) = delete;

        /** @brief The directory the source was copied into. */
        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        /** @brief The directory a build writes to. */
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }
        /** @brief The source's file name. */
        [[nodiscard]] const std::string& Name() const { return name_; }

    private:
        std::filesystem::path root_;
        std::string name_;
    };

    /** @brief Tells whether @p haystack contains @p needle, ignoring case. */
    inline bool ContainsIgnoringCase(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty()) { return true; }
        const auto fold = [](unsigned char c) { return std::tolower(c); };
        const auto found = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [&fold](char one, char other) { return fold(static_cast<unsigned char>(one)) ==
                                                   fold(static_cast<unsigned char>(other)); });
        return found != haystack.end();
    }
}
