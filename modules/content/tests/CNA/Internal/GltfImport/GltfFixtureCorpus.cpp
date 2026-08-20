// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-003/GLTF-004 -- see GltfFixtureCorpus.hpp. Test scope only.

#include "GltfFixtureCorpus.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace CnaTest::GltfOracle
{
    namespace
    {
        std::string ReadTextFile(const std::filesystem::path& path, std::string& error)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                error = "cannot open " + path.string();
                return {};
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    std::filesystem::path CorpusDirectory()
    {
        // The CTest registration runs from the repository root; a developer invoking the binary
        // from a build directory should not have to know that, so walk a few parents too.
        static const std::filesystem::path resolved = []() -> std::filesystem::path {
            std::filesystem::path base = ".";
            for (int depth = 0; depth < 5; ++depth)
            {
                const std::filesystem::path candidate = base / "tests" / "assets" / "gltf";
                std::error_code ec;
                if (std::filesystem::is_directory(candidate, ec)) { return candidate; }
                base /= "..";
            }
            return {};
        }();
        return resolved;
    }

    const CNA::Internal::JsonValue& JsonNull()
    {
        static const CNA::Internal::JsonValue nullValue;
        return nullValue;
    }

    const CNA::Internal::JsonValue& CorpusManifest()
    {
        static const CNA::Internal::JsonValue manifest = []() -> CNA::Internal::JsonValue {
            const std::filesystem::path dir = CorpusDirectory();
            if (dir.empty()) { return {}; }
            std::string error;
            const std::string text = ReadTextFile(dir / "manifest.json", error);
            if (text.empty()) { return {}; }
            try { return CNA::Internal::ParseJson(text); }
            catch (const CNA::Internal::JsonParseException&) { return {}; }
        }();
        return manifest;
    }

    std::vector<std::string> CorpusFixtureIds()
    {
        std::vector<std::string> ids;
        const CNA::Internal::JsonValue& assets = Member(CorpusManifest(), "assets");
        if (assets.type != CNA::Internal::JsonType::Array) { return ids; }
        for (const CNA::Internal::JsonValue& asset : assets.arrayValue)
        {
            const std::string id = StringOr(asset, "id", "");
            if (!id.empty()) { ids.push_back(id); }
        }
        return ids;
    }

    LoadedFixture::LoadedFixture(const std::string& id)
        : id_(id)
    {
        const std::filesystem::path dir = CorpusDirectory();
        if (dir.empty())
        {
            error_ = "fixture corpus directory tests/assets/gltf not found -- run the suite from "
                     "the repository root, or regenerate with 'PYTHONPATH=tools python3 -m "
                     "gltf_fixtures --out tests/assets/gltf'";
            return;
        }
        assetPath_ = dir / (id + ".gltf");

        std::string readError;
        const std::string expectedText = ReadTextFile(dir / (id + ".expected.json"), readError);
        if (expectedText.empty())
        {
            error_ = "cannot read expectation manifest for '" + id + "': " + readError;
            return;
        }
        try { expected_ = CNA::Internal::ParseJson(expectedText); }
        catch (const CNA::Internal::JsonParseException& e)
        {
            error_ = "malformed expectation manifest for '" + id + "': " + e.what();
            return;
        }

        cgltf_options options{};
        const std::string assetString = assetPath_.string();
        cgltf_result result = cgltf_parse_file(&options, assetString.c_str(), &data_);
        if (result != cgltf_result_success)
        {
            data_ = nullptr;
            error_ = "cgltf_parse_file failed for '" + assetString + "' (result " +
                     std::to_string(static_cast<int>(result)) + ")";
            return;
        }
        result = cgltf_load_buffers(&options, data_, assetString.c_str());
        if (result != cgltf_result_success)
        {
            error_ = "cgltf_load_buffers failed for '" + assetString + "' (result " +
                     std::to_string(static_cast<int>(result)) + ")";
            return;
        }
        ok_ = true;
    }

    LoadedFixture::~LoadedFixture()
    {
        if (data_ != nullptr) { cgltf_free(data_); }
    }

    const CNA::Internal::JsonValue& Member(const CNA::Internal::JsonValue& value,
                                            const std::string& key)
    {
        const CNA::Internal::JsonValue* found = value.FindMember(key);
        return found != nullptr ? *found : JsonNull();
    }

    const CNA::Internal::JsonValue& Path(const CNA::Internal::JsonValue& value,
                                          const std::string& dottedPath)
    {
        const CNA::Internal::JsonValue* current = &value;
        std::size_t start = 0;
        while (start <= dottedPath.size())
        {
            const std::size_t dot = dottedPath.find('.', start);
            const std::string key = dottedPath.substr(
                start, dot == std::string::npos ? std::string::npos : dot - start);
            current = &Member(*current, key);
            if (dot == std::string::npos) { break; }
            start = dot + 1;
        }
        return *current;
    }

    std::vector<double> Numbers(const CNA::Internal::JsonValue& value)
    {
        // Flattens nested numeric arrays. Manifests store per-element geometry as arrays of
        // tuples ([[0,0,0],[1,0,0],...]) because that is what a human reviewing a fixture wants
        // to read, while every comparison here works on a flat component list. Flattening in one
        // place keeps the two representations from having to agree anywhere else.
        std::vector<double> out;
        if (value.type != CNA::Internal::JsonType::Array) { return out; }
        for (const CNA::Internal::JsonValue& item : value.arrayValue)
        {
            if (item.type == CNA::Internal::JsonType::Number)
            {
                out.push_back(item.numberValue);
            }
            else if (item.type == CNA::Internal::JsonType::Array)
            {
                const std::vector<double> nested = Numbers(item);
                if (nested.empty() && !item.arrayValue.empty()) { return {}; }
                out.insert(out.end(), nested.begin(), nested.end());
            }
            else
            {
                return {};
            }
        }
        return out;
    }

    std::vector<std::string> Strings(const CNA::Internal::JsonValue& value)
    {
        std::vector<std::string> out;
        if (value.type != CNA::Internal::JsonType::Array) { return out; }
        out.reserve(value.arrayValue.size());
        for (const CNA::Internal::JsonValue& item : value.arrayValue)
        {
            if (item.type != CNA::Internal::JsonType::String) { return {}; }
            out.push_back(item.stringValue);
        }
        return out;
    }

    double NumberOr(const CNA::Internal::JsonValue& value, const std::string& key, double fallback)
    {
        const CNA::Internal::JsonValue& member = Member(value, key);
        return member.type == CNA::Internal::JsonType::Number ? member.numberValue : fallback;
    }

    std::string StringOr(const CNA::Internal::JsonValue& value, const std::string& key,
                          const std::string& fallback)
    {
        const CNA::Internal::JsonValue& member = Member(value, key);
        return member.type == CNA::Internal::JsonType::String ? member.stringValue : fallback;
    }

    bool BoolOr(const CNA::Internal::JsonValue& value, const std::string& key, bool fallback)
    {
        const CNA::Internal::JsonValue& member = Member(value, key);
        return member.type == CNA::Internal::JsonType::Boolean ? member.boolValue : fallback;
    }

    bool IsRejectionFixture(const CNA::Internal::JsonValue& expected)
    {
        return Member(expected, "rejection").type == CNA::Internal::JsonType::Object;
    }

    bool RequiresUnavailableDraco(const CNA::Internal::JsonValue& expected)
    {
#ifdef CNA_DRACO_AVAILABLE
        (void)expected;
        return false;
#else
        const std::vector<std::string> required = Strings(Path(expected, "l1.extensionsRequired"));
        return std::find(required.begin(), required.end(), "KHR_draco_mesh_compression") !=
               required.end();
#endif
    }

    bool IsOpenDefect(const CNA::Internal::JsonValue& defect)
    {
        const std::string status = StringOr(defect, "status", "");
        return status == "known-failing" || status == "partially-remediated";
    }

    bool IsKnownDefectField(const CNA::Internal::JsonValue& expected, const std::string& layer,
                             const std::string& field)
    {
        const CNA::Internal::JsonValue& defects = Member(expected, "defects");
        if (defects.type != CNA::Internal::JsonType::Array) { return false; }
        for (const CNA::Internal::JsonValue& defect : defects.arrayValue)
        {
            // A defect the corpus records as fixed suppresses nothing: its fixture goes back under
            // full conformance assertion, which is what turns the record into a regression witness.
            if (!IsOpenDefect(defect)) { continue; }
            // divergentFieldsByLayer is authoritative -- a defect usually breaks exactly one layer,
            // but an import CNA rejects outright has no semantic mesh at L3 *and* no world
            // geometry at L4, and both have to be declared for the suppression to be honest.
            for (const std::string& divergent :
                 Strings(Member(Member(defect, "divergentFieldsByLayer"), layer)))
            {
                if (divergent == field) { return true; }
            }
        }
        return false;
    }
}
