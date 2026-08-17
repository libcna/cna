// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-101 / GLTF-102: is the L3 sweep total, and does an arbitrary attribute set
// import without losing anything in silence?
//
// `GltfConformanceL3` already compares every manifest primitive field by field. What it cannot do
// is notice what it never looked at: a fixture whose `l3.primitives` is empty passes it vacuously,
// and a primitive the importer produces that no manifest entry names is simply never compared.
// Both are the same failure -- a sweep that reports success over a set it silently shrank -- and
// this file asserts the partition in both directions.
//
// The fuzz below asks the harder version of the same question. Every corpus fixture authors an
// attribute set somebody chose; a real exporter emits combinations nobody anticipated. The
// property under test is not "it does not crash" -- that is the floor -- but the campaign's own
// thesis: **every authored attribute either arrives or is named in a report**. A drop with no
// report entry is precisely the class of bug this whole plan exists to remove.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

using namespace CnaTest::GltfOracle;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;

namespace
{
    /// Base64, for the scratch documents' `data:` buffer URIs.
    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t a = bytes[i];
            const std::uint32_t b = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
            const std::uint32_t c = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
            const std::uint32_t triple = (a << 16) | (b << 8) | c;
            out += kAlphabet[(triple >> 18) & 0x3F];
            out += kAlphabet[(triple >> 12) & 0x3F];
            out += (i + 1 < bytes.size()) ? kAlphabet[(triple >> 6) & 0x3F] : '=';
            out += (i + 2 < bytes.size()) ? kAlphabet[triple & 0x3F] : '=';
        }
        return out;
    }

    void AppendFloat(std::vector<std::uint8_t>& out, float value)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &value, sizeof(bytes));
        out.insert(out.end(), std::begin(bytes), std::end(bytes));
    }

    void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }

    /// One attribute the fuzz may author: its glTF name and its accessor shape.
    struct FuzzAttribute
    {
        const char* name;
        const char* type;         ///< SCALAR / VEC2 / VEC3 / VEC4
        int componentType;        ///< 5126 FLOAT, 5123 UNSIGNED_SHORT
        int components;
    };

    /// Every attribute shape a real exporter emits, plus the two CNA deliberately ignores.
    ///
    /// JOINTS_0 and WEIGHTS_0 are listed adjacently and drawn as a pair by the generator below:
    /// authoring one without the other is a malformed file, and this fuzz is over VALID
    /// permutations -- the malformed cases have their own robustness fixtures.
    const std::vector<FuzzAttribute> kFuzzAttributes = {
        {"NORMAL",     "VEC3", 5126, 3},
        {"TANGENT",    "VEC4", 5126, 4},
        {"TEXCOORD_0", "VEC2", 5126, 2},
        {"TEXCOORD_1", "VEC2", 5126, 2},
        {"COLOR_0",    "VEC4", 5126, 4},
        {"COLOR_1",    "VEC4", 5126, 4},
        {"_BATCHID",   "SCALAR", 5126, 1},
    };

    constexpr int kFuzzVertexCount = 3;

    /// Builds a valid single-primitive glTF document authoring exactly @p attributes.
    ///
    /// Values are deliberately unremarkable -- a unit normal, in-range UVs and colours -- because
    /// what varies is the attribute SET, not the data. A fuzz that also randomised values would
    /// be testing the decoders, which have their own exact fixtures.
    std::string BuildFuzzDocument(const std::vector<std::string>& attributes, bool skinned,
                                   bool withMaterial, bool unlit)
    {
        std::vector<std::uint8_t> buffer;
        std::string accessorsJson;
        std::string viewsJson;
        std::string attributesJson;
        int accessorIndex = 0;

        const auto addAccessor = [&](const std::string& name, const char* type, int componentType,
                                      int components, bool normalized) {
            const std::size_t offset = buffer.size();
            for (int v = 0; v < kFuzzVertexCount; ++v)
            {
                for (int c = 0; c < components; ++c)
                {
                    if (componentType == 5123) { AppendU16(buffer, static_cast<std::uint16_t>(0)); }
                    else if (name == "NORMAL") { AppendFloat(buffer, c == 2 ? 1.0f : 0.0f); }
                    else if (name == "TANGENT")
                    {
                        AppendFloat(buffer, c == 0 ? 1.0f : (c == 3 ? 1.0f : 0.0f));
                    }
                    else if (name.rfind("WEIGHTS", 0) == 0) { AppendFloat(buffer, c == 0 ? 1.0f : 0.0f); }
                    else { AppendFloat(buffer, 0.5f); }
                }
            }
            const std::size_t length = buffer.size() - offset;

            if (!viewsJson.empty()) { viewsJson += ", "; }
            viewsJson += "{ \"buffer\": 0, \"byteOffset\": " + std::to_string(offset) +
                         ", \"byteLength\": " + std::to_string(length) + " }";
            if (!accessorsJson.empty()) { accessorsJson += ", "; }
            accessorsJson += "{ \"bufferView\": " + std::to_string(accessorIndex) +
                             ", \"componentType\": " + std::to_string(componentType) +
                             (normalized ? ", \"normalized\": true" : "") +
                             ", \"count\": " + std::to_string(kFuzzVertexCount) +
                             ", \"type\": \"" + type + "\" }";
            if (!attributesJson.empty()) { attributesJson += ", "; }
            attributesJson += "\"" + name + "\": " + std::to_string(accessorIndex);
            ++accessorIndex;
        };

        // POSITION first and always: §3.7.2.1 requires it, and its count drives everything.
        {
            const std::size_t offset = buffer.size();
            AppendFloat(buffer, 0.0f); AppendFloat(buffer, 0.0f); AppendFloat(buffer, 0.0f);
            AppendFloat(buffer, 1.0f); AppendFloat(buffer, 0.0f); AppendFloat(buffer, 0.0f);
            AppendFloat(buffer, 0.0f); AppendFloat(buffer, 1.0f); AppendFloat(buffer, 0.0f);
            viewsJson += "{ \"buffer\": 0, \"byteOffset\": " + std::to_string(offset) +
                         ", \"byteLength\": " + std::to_string(buffer.size() - offset) + " }";
            accessorsJson += "{ \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, "
                             "\"type\": \"VEC3\", \"min\": [0,0,0], \"max\": [1,1,0] }";
            attributesJson += "\"POSITION\": 0";
            ++accessorIndex;
        }

        for (const std::string& name : attributes)
        {
            const auto found = std::find_if(
                kFuzzAttributes.begin(), kFuzzAttributes.end(),
                [&name](const FuzzAttribute& a) { return name == a.name; });
            if (found == kFuzzAttributes.end()) { continue; }
            addAccessor(name, found->type, found->componentType, found->components, false);
        }
        std::string inverseBindJson;
        if (skinned)
        {
            addAccessor("JOINTS_0", "VEC4", 5123, 4, false);
            addAccessor("WEIGHTS_0", "VEC4", 5126, 4, false);

            // A real `skins` entry, not just the two attributes. The importer only looks JOINTS_0
            // and WEIGHTS_0 up when it has a skeleton to resolve them against, so a document
            // carrying the attributes and no skin is simply unskinned -- which is how the first
            // version of this fuzz reached two strides while reporting success.
            const std::size_t ibmOffset = buffer.size();
            for (int e = 0; e < 16; ++e) { AppendFloat(buffer, (e % 5 == 0) ? 1.0f : 0.0f); }
            viewsJson += ", { \"buffer\": 0, \"byteOffset\": " + std::to_string(ibmOffset) +
                         ", \"byteLength\": 64 }";
            accessorsJson += ", { \"bufferView\": " + std::to_string(accessorIndex) +
                             ", \"componentType\": 5126, \"count\": 1, \"type\": \"MAT4\" }";
            inverseBindJson = ", \"skins\": [ { \"joints\": [2], \"inverseBindMatrices\": " +
                              std::to_string(accessorIndex) + " } ]";
            ++accessorIndex;
        }

        const std::size_t indexOffset = buffer.size();
        AppendU16(buffer, 0); AppendU16(buffer, 1); AppendU16(buffer, 2);
        viewsJson += ", { \"buffer\": 0, \"byteOffset\": " + std::to_string(indexOffset) +
                     ", \"byteLength\": 6 }";
        accessorsJson += ", { \"bufferView\": " + std::to_string(accessorIndex) +
                         ", \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }";
        const int indexAccessor = accessorIndex;

        std::string materialJson;
        std::string materialRef;
        std::string extensionsUsedJson;
        if (withMaterial)
        {
            // The unlit variant is what reaches the non-PBR strides (32 unskinned, 52 skinned).
            // Without it the fuzz only ever produces metallic-roughness materials, because that is
            // glTF's default in two separate ways -- including "no material at all".
            materialJson = ", \"materials\": [ { \"name\": \"Fuzz\", "
                           "\"pbrMetallicRoughness\": { \"baseColorFactor\": [1,1,1,1] }";
            if (unlit)
            {
                materialJson += ", \"extensions\": { \"KHR_materials_unlit\": {} }";
                extensionsUsedJson = ", \"extensionsUsed\": [ \"KHR_materials_unlit\" ]";
            }
            materialJson += " } ]";
            materialRef = ", \"material\": 0";
        }

        // Node 0 is the scene root, node 1 the mesh, node 2 the joint -- present only when
        // skinned, and referenced by the skin above.
        const std::string nodesJson = skinned
            ? "[ { \"name\": \"Root\", \"children\": [1, 2] }, "
              "{ \"name\": \"FuzzNode\", \"mesh\": 0, \"skin\": 0 }, "
              "{ \"name\": \"Joint\" } ]"
            : "[ { \"name\": \"Root\", \"children\": [1] }, "
              "{ \"name\": \"FuzzNode\", \"mesh\": 0 } ]";

        return std::string("{ \"asset\": { \"version\": \"2.0\" }") + extensionsUsedJson +
               ", \"scene\": 0, \"scenes\": [ { \"nodes\": [0] } ], " +
               "\"nodes\": " + nodesJson + ", " +
               "\"meshes\": [ { \"name\": \"FuzzMesh\", \"primitives\": [ { \"attributes\": { " +
               attributesJson + " }, \"indices\": " + std::to_string(indexAccessor) +
               materialRef + ", \"mode\": 4 } ] } ]" + materialJson + inverseBindJson +
               ", \"accessors\": [ " + accessorsJson + " ]" +
               ", \"bufferViews\": [ " + viewsJson + " ]" +
               ", \"buffers\": [ { \"byteLength\": " + std::to_string(buffer.size()) +
               ", \"uri\": \"data:application/octet-stream;base64," + Base64(buffer) + "\" } ] }";
    }

    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    bool ParseText(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }
}

// --- GLTF-101: the L3 sweep must be total in both directions ------------------------------------

TEST(GltfAttributeCoverage, EveryImportableFixtureDeclaresAnL3PrimitiveToCompare)
{
    // `GltfConformanceL3` iterates the MANIFEST's primitives, so a fixture whose `l3.primitives`
    // is empty passes it without comparing anything at all. Nothing said so; the sweep reported
    // success over a set it had silently shrunk to nothing.
    std::size_t importable = 0;
    std::size_t manifestPrimitives = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (IsRejectionFixture(fixture.Expected())) { continue; }
        SCOPED_TRACE(id);
        ++importable;

        const JsonValue& primitives = Path(fixture.Expected(), "l3.primitives");
        ASSERT_EQ(JsonType::Array, primitives.type) << "no l3.primitives array at all";
        EXPECT_FALSE(primitives.arrayValue.empty())
            << "an importable fixture with no L3 primitive is compared field-by-field against "
               "nothing, and the sweep still passes";
        manifestPrimitives += primitives.arrayValue.size();
    }
    EXPECT_GT(importable, 0u) << "no importable fixture at all -- the sweep proved nothing";
    EXPECT_GE(manifestPrimitives, importable)
        << "fewer manifest primitives than fixtures, so at least one declares none";
}

TEST(GltfAttributeCoverage, EveryPrimitiveTheImporterProducesIsNamedByTheManifest)
{
    // The converse, and the one that actually catches a regression: a primitive the importer emits
    // that no manifest entry names is never compared to anything. A multi-primitive mesh whose
    // second primitive stopped being described would import however it liked, and every layer
    // above would report success.
    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (IsRejectionFixture(fixture.Expected())) { continue; }
        SCOPED_TRACE(id);

        std::set<std::pair<int, int>> described;
        for (const JsonValue& row : Path(fixture.Expected(), "l3.primitives").arrayValue)
        {
            described.emplace(static_cast<int>(NumberOr(row, "mesh", -1)),
                              static_cast<int>(NumberOr(row, "primitive", -1)));
        }

        for (const ExtractedPrimitive& actual : ExtractSceneMeshesEXT(fixture.Data()))
        {
            EXPECT_NE(described.end(), described.find({actual.mesh, actual.primitive}))
                << "the importer produced mesh " << actual.mesh << " primitive "
                << actual.primitive << ", which the manifest does not describe -- so nothing "
                   "compares it against anything";
        }
    }
}

// --- GLTF-102: an arbitrary attribute set arrives, or is named -----------------------------------

TEST(GltfAttributeCoverage, EveryAuthoredAttributeEitherArrivesOrIsNamedInAReport)
{
    // The campaign's own thesis, applied to attribute sets nobody chose. Every corpus fixture
    // authors a combination somebody designed; a real exporter emits ones nobody anticipated, and
    // the failure mode is never a crash -- it is a stream that quietly does not arrive.
    //
    // Seeded deterministically: a fuzz whose failures cannot be reproduced is a flake generator.
    // The seed is stated here rather than taken from the clock for exactly that reason.
    using namespace CNA::Internal::GltfImport;

    std::mt19937 rng(20260812u);
    std::size_t cases = 0;
    std::set<int> stridesReached;

    for (int iteration = 0; iteration < 200; ++iteration)
    {
        std::vector<std::string> attributes;
        for (const FuzzAttribute& candidate : kFuzzAttributes)
        {
            if (rng() % 2 == 0) { attributes.push_back(candidate.name); }
        }
        // Declaration order is part of the permutation: glTF's `attributes` is a JSON object, so
        // nothing constrains it, and an importer keying off position rather than name would pass
        // every fixture in the corpus and fail here.
        std::shuffle(attributes.begin(), attributes.end(), rng);
        const bool skinned = (rng() % 2) == 0;
        const bool withMaterial = (rng() % 2) == 0;
        const bool unlit = withMaterial && (rng() % 2) == 0;

        const std::string json = BuildFuzzDocument(attributes, skinned, withMaterial, unlit);
        Parsed parsed;
        ASSERT_TRUE(ParseText(parsed, json))
            << "iteration " << iteration << " produced a document cgltf cannot parse, so the fuzz "
               "is testing its own generator rather than the importer:\n" << json;

        std::vector<std::string> validationWarnings;
        ASSERT_NO_THROW(ValidateGltfEXT(parsed.data, "fuzz", validationWarnings))
            << "iteration " << iteration << ": a VALID permutation was refused";

        SceneGraphOut scene = BuildSceneGraph(parsed.data);
        std::vector<SkeletonResult> skeletons;
        for (cgltf_size s = 0; s < parsed.data->skins_count; ++s)
        {
            skeletons.push_back(BuildSkeleton(
                &parsed.data->skins[s], scene,
                Microsoft::Xna::Framework::Matrix::getIdentityProperty(), 1.0f));
        }

        MeshOut out;
        ASSERT_NO_THROW(
            out = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "fuzz",
                              skeletons.empty() ? nullptr : &skeletons.front(), 1.0f))
            << "iteration " << iteration << ": a valid permutation threw";
        ++cases;
        stridesReached.insert(out.stride);

        const auto authored = [&attributes](const char* name) {
            return std::find(attributes.begin(), attributes.end(), name) != attributes.end();
        };

        // COLOR_1 and _BATCHID are the two CNA deliberately ignores, and both must be NAMED --
        // that is GLTF-091/GLTF-092's whole point, and the property most likely to regress when
        // the attribute loop is touched for some unrelated reason.
        if (authored("COLOR_1"))
        {
            EXPECT_GT(out.extraColorSetsEXT, 0)
                << "iteration " << iteration << ": COLOR_1 was authored and not counted";
        }
        else
        {
            EXPECT_EQ(0, out.extraColorSetsEXT)
                << "iteration " << iteration << ": a colour set was counted that nobody authored";
        }

        if (authored("_BATCHID"))
        {
            EXPECT_FALSE(out.ignoredCustomAttributesEXT.empty())
                << "iteration " << iteration << ": _BATCHID was authored and not named";
            EXPECT_NE(out.ignoredCustomAttributesEXT.end(),
                      std::find(out.ignoredCustomAttributesEXT.begin(),
                                out.ignoredCustomAttributesEXT.end(), "_BATCHID"));
        }
        else
        {
            EXPECT_TRUE(out.ignoredCustomAttributesEXT.empty())
                << "iteration " << iteration << ": a custom attribute was named that nobody "
                   "authored -- a report nobody can trust is worse than none";
        }

        // A dropped NORMAL or TANGENT is a stride consequence, so the two reports must agree with
        // the layout that was actually selected rather than with each other.
        //
        // plan_gltf.md GLTF-278/GLTF-462: asked of the CANONICAL TABLE rather than of a literal
        // stride list. The list this replaced said a tangent survives at stride 48 or 68 only, which
        // was already wrong for the dual-UV strides 60 and 76 and became reachable the moment a
        // vertex-coloured primitive started taking stride 60. A test carrying its own copy of an ABI
        // asserts the copy.
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        const auto slotExists = [&](VertexElementUsage usage) {
            const CNA::Internal::Graphics::InferredVertexLayout layout =
                CNA::Internal::Graphics::InferredLayoutForStride(
                    out.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
            if (!layout.known) { return false; }
            for (std::size_t i = 0; i < layout.count; ++i)
            {
                if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == 0)
                {
                    return true;
                }
            }
            return false;
        };
        if (authored("NORMAL"))
        {
            EXPECT_EQ(!slotExists(VertexElementUsage::Normal), out.droppedNormalForStrideEXT)
                << "iteration " << iteration << ": stride " << out.stride
                << " and the dropped-normal report disagree";
        }
        if (authored("TANGENT"))
        {
            // GLTF-461: an authored tangent is also ignored when the primitive authors no NORMAL,
            // which §3.7.2.1 requires and which is reported separately -- so the stride report is
            // only asked about the stride when normals were not generated.
            if (!out.generatedNormalsEXT)
            {
                EXPECT_EQ(!slotExists(VertexElementUsage::Tangent), out.droppedTangentForStrideEXT)
                    << "iteration " << iteration << ": stride " << out.stride
                    << " and the dropped-tangent report disagree";
            }
            else
            {
                EXPECT_TRUE(out.ignoredTangentForGeneratedNormalsEXT ||
                            out.droppedTangentForStrideEXT)
                    << "iteration " << iteration
                    << ": an authored TANGENT was neither carried nor named as ignored";
            }
        }

        // Anything the stride cannot carry must be named by the GLTF-100 summary too, so the
        // per-symptom reports and the combination report cannot drift apart.
        if (out.droppedNormalForStrideEXT && authored("NORMAL"))
        {
            EXPECT_FALSE(out.unrepresentableForStrideEXT.empty())
                << "iteration " << iteration << ": a normal was dropped and the layout summary "
                   "says the combination loses nothing";
        }
    }

    EXPECT_EQ(200u, cases);
    // Six of the ABI's eight strides, from attribute sets alone. The two it cannot reach are 16
    // (position-only, which needs a primitive with no other attribute at all) and 20
    // (DualTextureEffect, which needs two real images) -- both have their own exact fixtures, and
    // stating the floor rather than the set keeps this from failing when a new stride appears.
    EXPECT_GE(stridesReached.size(), 6u)
        << "the permutations reached only " << stridesReached.size()
        << " strides, so most of the layout table went unexercised";
}
