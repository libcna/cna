// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-021 / GLTF-022 / GLTF-023 / GLTF-024: the container-level validation pass.
//
// Before this, cgltf_validate() had ZERO occurrences in CNA production code and extensionsRequired
// was never read. A file whose accessor reached past its own bufferView decoded whatever was
// adjacent in memory; a file declaring an extension CNA does not implement imported silently and
// wrongly. Both "worked" in the only sense a parse-only reader can measure.
//
// The three checks are deliberately different in severity, and this file asserts that difference
// rather than just asserting that something throws:
//
//   * a structural violation REJECTS   -- decoding it would read outside the file's buffers;
//   * an unsupported REQUIRED extension REJECTS -- the author said it cannot be read without it;
//   * an unsupported USED extension WARNS -- by definition the file is expected to load anyway.

#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include "GltfFixtureCorpus.hpp"

using namespace CNA::Internal::GltfImport;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using CnaTest::GltfOracle::BoolOr;
using CnaTest::GltfOracle::CorpusFixtureIds;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::StringOr;

namespace
{
    /// Extracts every primitive of a fixture and returns the first failure, or "" if all succeeded.
    ///
    /// The stage AFTER validation. Some malformed files are structurally sound accessor-by-accessor
    /// and only contradict each other, which `cgltf_validate` cannot see -- those have to be
    /// refused here or become undefined behaviour in the packing loop.
    std::string ExtractionErrorFor(const LoadedFixture& fixture)
    {
        const cgltf_data& data = fixture.Data();
        // Building each skin is part of extraction: GLTF-261's palette-size refusal lives in
        // BuildSkeleton, not ExtractMesh, and a probe that only extracted primitives would report
        // an over-limit rig as importing cleanly.
        const SceneGraphOut scene = BuildSceneGraph(&data);
        std::vector<SkeletonResult> skeletons;
        for (cgltf_size s = 0; s < data.skins_count; ++s)
        {
            try
            {
                skeletons.push_back(BuildSkeleton(&data.skins[s], scene,
                                                   Microsoft::Xna::Framework::Matrix::getIdentityProperty(),
                                                   1.0f));
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
        }

        // Extraction is run once per skeleton, and unskinned only when the file has no skins at
        // all. GLTF-254's out-of-range-joint refusal lives in the packing loop's skinning branch,
        // which a probe passing nullptr never reaches -- the same shape of blind spot GLTF-261's
        // skin loop above was added to close, one layer further down. Which skin actually owns a
        // given mesh does not matter here: the probe is asking whether ANY path refuses the file.
        const auto extractWith = [&](const SkeletonResult* skeleton) -> std::string {
            for (cgltf_size m = 0; m < data.meshes_count; ++m)
            {
                for (cgltf_size p = 0; p < data.meshes[m].primitives_count; ++p)
                {
                    try
                    {
                        (void)ExtractMesh(&data, data.meshes[m].primitives[p], "probe", skeleton,
                                           1.0f);
                    }
                    catch (const std::exception& e)
                    {
                        return e.what();
                    }
                }
            }
            return {};
        };

        if (skeletons.empty())
        {
            const std::string error = extractWith(nullptr);
            if (!error.empty()) { return error; }
        }
        for (const SkeletonResult& skeleton : skeletons)
        {
            const std::string error = extractWith(&skeleton);
            if (!error.empty()) { return error; }
        }

        // Clips are extraction too, and the third instance of the same blind spot the two comments
        // above describe: GLTF-313's non-monotonic-sampler refusal lives in the channel loader, so
        // a probe that only extracted meshes and skeletons reported a file with a backwards
        // animation timeline as importing perfectly cleanly.
        std::vector<std::string> clipWarnings;
        try
        {
            (void)ExtractSceneNodeClips(&data, scene, 1.0f, clipWarnings);
            for (const SkeletonResult& skeleton : skeletons)
            {
                (void)ExtractClips(&data, skeleton, 1.0f, clipWarnings);
            }
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    /// Runs the validation pass over a fixture and returns the diagnostic, or "" if it passed.
    std::string ValidationErrorFor(const LoadedFixture& fixture, std::vector<std::string>& warnings)
    {
        try
        {
            ValidateGltfEXT(&fixture.Data(), fixture.Id(), warnings);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }
}

TEST(GltfContainerValidation, EveryFixtureDeclaringARejectionIsRejectedAndSaysWhy)
{
    // Driven from the manifest rather than a list re-typed here, so a fixture added to the corpus
    // with a `rejection` block cannot be forgotten by this suite.
    int checked = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& rejection = Member(fixture.Expected(), "rejection");
        if (rejection.type != JsonType::Object) { continue; }
        SCOPED_TRACE(id);
        ++checked;

        // The file must PARSE -- that is the whole point. A rejection fixture is one a parse-only
        // reader would happily accept, so if it failed to parse it would prove nothing.
        //
        // WHERE it is refused is part of the expectation, not an implementation detail. A file
        // whose accessor reaches past its own bufferView is caught by structural VALIDATION; a
        // file whose two attribute accessors are each individually valid but disagree with each
        // other (GLTF-060) passes validation entirely and is refused later, by EXTRACTION, because
        // nothing reads outside a buffer until CNA's own packing loop indexes the short stream.
        // Asserting the stage keeps those two from being conflated into "something threw".
        const std::string stage = StringOr(rejection, "stage", "");
        ASSERT_TRUE(stage == "validation" || stage == "extraction")
            << "unknown rejection stage '" << stage << "'";

        std::vector<std::string> warnings;
        std::string error = stage == "extraction" ? ExtractionErrorFor(fixture)
                                                  : ValidationErrorFor(fixture, warnings);
        ASSERT_FALSE(error.empty())
            << "the fixture imported without complaint -- the " << stage << " stage did not run, "
               "or no longer catches what this fixture declares";

        // A rejection that does not say what is wrong is barely better than a silent one.
        for (const JsonValue& fragment : Member(rejection, "errorContains").arrayValue)
        {
            const std::string& text = fragment.stringValue;
            EXPECT_NE(std::string::npos, error.find(text))
                << "the diagnostic does not name '" << text << "': " << error;
        }

        // Some files are refused by more than one layer, and where that is true it is part of the
        // expectation rather than a happy accident. `accessor-count-mismatch` is caught by
        // structural validation -- so both loaders reject it early -- AND by extraction, which
        // matters because `ExtractMesh` is also called directly, without validation, by the L3
        // oracle itself. A defence-in-depth check that quietly stopped working would otherwise be
        // invisible behind the layer in front of it.
        if (CnaTest::GltfOracle::BoolOr(rejection, "alsoRefusedAtExtraction", false))
        {
            const std::string extractionError = ExtractionErrorFor(fixture);
            ASSERT_FALSE(extractionError.empty())
                << "validation catches this file but extraction does not -- a caller that skips "
                   "validation would read out of bounds";
            for (const JsonValue& fragment : Member(rejection, "extractionErrorContains").arrayValue)
            {
                EXPECT_NE(std::string::npos, extractionError.find(fragment.stringValue))
                    << "the extraction diagnostic does not name '" << fragment.stringValue
                    << "': " << extractionError;
            }
        }
    }
    EXPECT_GT(checked, 0) << "no rejection fixtures in the corpus -- the suite proved nothing";
}

TEST(GltfContainerValidation, EveryOtherCorpusFixturePassesValidationCleanly)
{
    // The converse, and the one that stops validation being over-eager: every fixture that is NOT
    // declared as a rejection must pass.
    //
    // A warning is allowed only where the fixture asked for one. A fixture declaring no extension
    // at all must produce none -- that is the over-eagerness this guards against -- and a fixture
    // that does declare one may be warned about it, but only by name: GLTF-024 makes an
    // unimplemented `extensionsUsed` entry a warning rather than a rejection, and `mat-unlit` and
    // `skin-unlit` exist precisely to reach the non-PBR strides through such a material.
    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (Member(fixture.Expected(), "rejection").type == JsonType::Object) { continue; }
        SCOPED_TRACE(id);

        std::vector<std::string> warnings;
        const std::string validationError = ValidationErrorFor(fixture, warnings);
        if (CnaTest::GltfOracle::RequiresUnavailableDraco(fixture.Expected()))
        {
            EXPECT_NE(std::string::npos, validationError.find("KHR_draco_mesh_compression"))
                << "the decoder-free build did not name the unavailable required extension: "
                << validationError;
            EXPECT_NE(std::string::npos, validationError.find("does not implement"))
                << "the decoder-free refusal does not explain why import cannot proceed: "
                << validationError;
            continue;
        }

        EXPECT_EQ("", validationError)
            << "a valid fixture was rejected -- validation is too strict";

        if (fixture.Data().extensions_used_count == 0)
        {
            EXPECT_TRUE(warnings.empty())
                << "unexpected warning: " << (warnings.empty() ? std::string() : warnings.front());
            continue;
        }
        for (const std::string& warning : warnings)
        {
            bool namesADeclaredExtension = false;
            for (cgltf_size e = 0; e < fixture.Data().extensions_used_count; ++e)
            {
                const char* name = fixture.Data().extensions_used[e];
                if (name != nullptr && warning.find(name) != std::string::npos)
                {
                    namesADeclaredExtension = true;
                }
            }
            EXPECT_TRUE(namesADeclaredExtension)
                << "a warning about something the fixture never declared: " << warning;
        }
    }
}

TEST(GltfContainerValidation, AnUnsupportedUsedExtensionWarnsInsteadOfRejecting)
{
    // GLTF-024's severity, asserted against GLTF-023's on the same extension name. The distinction
    // is the whole point: extensionsUsed is advisory, extensionsRequired is not.
    const LoadedFixture required("gltf-required-extension-unsupported");
    ASSERT_TRUE(required.Ok()) << required.Error();

    // The fixture lists the extension in BOTH arrays, as the specification requires -- so if the
    // required check were removed, the used check alone would let it through with a warning. That
    // is exactly the silent-and-wrong import GLTF-023 exists to prevent.
    ASSERT_GT(static_cast<std::size_t>(required.Data().extensions_required_count), 0u);
    ASSERT_GT(static_cast<std::size_t>(required.Data().extensions_used_count), 0u);

    std::vector<std::string> warnings;
    const std::string error = ValidationErrorFor(required, warnings);
    EXPECT_NE(std::string::npos, error.find("extensionsRequired"))
        << "a required unsupported extension was downgraded to a warning: " << error;
}

TEST(GltfContainerValidation, SupportedExtensionsAreTheOnesCnaActuallyImplements)
{
    // "Implements" is stricter than "notices", and the difference is load-bearing: ExtractMesh
    // reads KHR_materials_pbrSpecularGlossiness only to keep such a material OFF the
    // metallic-roughness path (GLTF-215). It is not implemented -- its parameters are dropped --
    // so a file requiring it must be rejected.
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_texture_transform"));
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_materials_emissive_strength"));
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_lights_punctual"));
    // KHR_materials_unlit joined them at GLTF-337: it now maps to LightingEnabled = false with
    // baseColorFactor as the diffuse colour, rather than merely being detected. It was the
    // textbook case for "detecting is not implementing" until it stopped being one.
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_materials_unlit"));
    // GLTF-341/342 preserve the source table and sparse mappings and expose selection on Model;
    // the direct and offline paths both swap the complete material-dependent part state.
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_materials_variants"));
    // GLTF-343 carries IOR through both effects into F0/F90 consumed by every PBR renderer.
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_materials_ior"));
    // GLTF-344's factor-only state is consumed too, but a required use may contain either of the
    // two optional texture inputs the importer still cannot represent.
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_specular"));

    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_pbrSpecularGlossiness"))
        << "detecting an extension is not implementing it";
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("EXT_mesh_gpu_instancing"));
    EXPECT_FALSE(IsGltfExtensionSupportedEXT(""));

#ifdef CNA_DRACO_AVAILABLE
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_draco_mesh_compression"))
        << "this build has libdraco, so a Draco-compressed file is importable";
#else
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_draco_mesh_compression"))
        << "this build has no libdraco, so a file REQUIRING Draco must be rejected up front "
           "rather than failing later inside ExtractMesh";
#endif
}

// --- GLTF-036: §3.6.2.4 data alignment ----------------------------------------------------------

namespace
{
    /// A document whose sparse VEC3<float> values bufferView starts at @p valuesOffset. Everything
    /// else is held constant, so the offset is the only variable between the aligned and
    /// misaligned cases.
    std::string SparseDocumentWithValuesOffset(int valuesOffset)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 76, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAEAAAEBAAACAQA==" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 60, "byteLength": 2 },
    { "buffer": 0, "byteOffset": )GLTF") + std::to_string(valuesOffset) + R"GLTF(, "byteLength": 12 }
  ],
  "accessors": [
    { "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [2,3,4],
      "sparse": { "count": 1,
                  "indices": { "bufferView": 0, "componentType": 5123 },
                  "values": { "bufferView": 1 } } }
  ]
})GLTF";
    }

    /// Parses an in-memory document and runs validation on it, returning the refusal or "". When
    /// `warningsOut` is given it receives the warnings the pass produced, which is how the
    /// report-rather-than-reject rules are asserted.
    std::string ValidationErrorForDocument(const std::string& json,
                                           std::vector<std::string>* warningsOut = nullptr)
    {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        if (cgltf_parse(&options, json.data(), json.size(), &data) != cgltf_result_success)
        {
            return "the fixture document did not parse";
        }
        struct Guard { cgltf_data* d; ~Guard() { cgltf_free(d); } } guard{data};
        if (cgltf_load_buffers(&options, data, ".") != cgltf_result_success)
        {
            return "the fixture document's buffers did not load";
        }

        std::vector<std::string> warnings;
        try
        {
            ValidateGltfEXT(data, "alignment-fixture.gltf", warnings);
            if (warningsOut != nullptr) { *warningsOut = warnings; }
            return {};
        }
        catch (const std::exception& e)
        {
            if (warningsOut != nullptr) { *warningsOut = warnings; }
            return e.what();
        }
    }
}

TEST(GltfContainerValidation, AMisalignedSparseValuesArrayIsRejected)
{
    // The real finding behind REMED-NA-016, which the audit recorded as a UBSan-confirmed
    // misaligned float load inside cgltf. It is neither a cgltf bug nor a CNA decode bug: it is a
    // MALFORMED FILE that CNA accepted. §3.6.2.4 makes an accessor's effective offset a multiple of
    // its component size, cgltf_validate does not check it, and cgltf reads a component with a raw
    // `*(const float*)` cast -- so 62, two off a float boundary, makes the parser perform a
    // misaligned load. Undefined behaviour, and an outright fault on targets without unaligned
    // access.
    const std::string error = ValidationErrorForDocument(SparseDocumentWithValuesOffset(62));
    ASSERT_FALSE(error.empty()) << "a misaligned sparse values array was accepted";
    EXPECT_NE(std::string::npos, error.find("3.6.2.4")) << error;
    EXPECT_NE(std::string::npos, error.find("sparse values array")) << error;
    EXPECT_NE(std::string::npos, error.find("62")) << error;
}

TEST(GltfContainerValidation, TheSameDocumentAlignedPassesValidation)
{
    // The control. Without it, "rejected" could just as well mean the document was broken in some
    // other way -- the offset is the only thing that differs between this and the case above.
    EXPECT_EQ("", ValidationErrorForDocument(SparseDocumentWithValuesOffset(64)));
}

TEST(GltfContainerValidation, AMisalignedBaseAccessorIsRejectedToo)
{
    // The base array has the same exposure as the sparse one; a check that covered only sparse
    // accessors would leave the far more common case open.
    const std::string error = ValidationErrorForDocument(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 76, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAEAAAEBAAACAQA==" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 2, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" }
  ]
})GLTF");
    ASSERT_FALSE(error.empty()) << "a misaligned base bufferView was accepted";
    EXPECT_NE(std::string::npos, error.find("base bufferView")) << error;
}

TEST(GltfContainerValidation, AByteStrideThatIsNotAMultipleOfFourIsRejected)
{
    // §3.6.2.1. A stride of 14 keeps element 0 aligned and misaligns every element after it, which
    // is the same undefined behaviour arriving one element later.
    const std::string error = ValidationErrorForDocument(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 76, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AQAAAAAAAEAAAEBAAACAQA==" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 44, "byteStride": 14 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" }
  ]
})GLTF");
    ASSERT_FALSE(error.empty()) << "a byteStride of 14 was accepted";
    EXPECT_NE(std::string::npos, error.find("multiple of 4")) << error;
}

TEST(GltfContainerValidation, EveryCorpusFixtureSatisfiesTheAlignmentRule)
{
    // The generator packs everything at 4-byte alignment, and this is what says so out loud: if a
    // future fixture is authored misaligned, it fails here rather than becoming a UBSan report in
    // some unrelated test months later.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const cgltf_data& data = fixture.Data();
        for (cgltf_size i = 0; i < data.accessors_count; ++i)
        {
            const cgltf_accessor& accessor = data.accessors[i];
            const cgltf_size size = cgltf_component_size(accessor.component_type);
            if (size == 0) { continue; }
            if (accessor.buffer_view != nullptr)
            {
                EXPECT_EQ(0u, (accessor.buffer_view->offset + accessor.offset) % size)
                    << "accessor " << i << "'s base offset is not a multiple of " << size;
            }
            if (accessor.is_sparse == 0) { continue; }
            const cgltf_accessor_sparse& sparse = accessor.sparse;
            if (sparse.values_buffer_view != nullptr)
            {
                EXPECT_EQ(0u, (sparse.values_buffer_view->offset + sparse.values_byte_offset) % size)
                    << "accessor " << i << "'s sparse values offset is not a multiple of " << size;
            }
        }
    }
}

// --- GLTF-061: the decoded values against the file's own declared bounds ------------------------
//
// §3.6.2 makes `min`/`max` required on POSITION and optional elsewhere, and they are the one piece
// of redundancy the format gives a reader: the author states the bounds, and a decoder producing
// values outside them has decoded something other than what was written.
//
// Nothing read them before. That is why D4 -- a sparse index accessor decoding to all zeros --
// could collapse a quad to a point with every layer reporting success: the file said the positions
// spanned [0,1] and the decode produced them, so only the *indices* were wrong and nothing
// compared anything.

TEST(GltfContainerValidation, DecodedValuesOutsideTheDeclaredBoundsAreReportedNotRejected)
{
    // The bounds claim [0,1] on every axis; the data reaches 5 on X. That is the shape of a decode
    // gone wrong -- a mis-strided read, a swapped component, a sparse override landing in the wrong
    // element -- and it must be reported.
    //
    // A WARNING rather than a rejection, deliberately: a file whose bounds are merely stale is
    // common and harmless, and the values may still be exactly what the file contains. Refusing
    // would turn a diagnostic into a load failure for assets that render correctly today.
    std::vector<std::string> warnings;
    const std::string error = ValidationErrorForDocument(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "mode": 4 } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACgQAAAAAAAAAAAAAAAAAAAgD8AAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 1] }
  ]
})GLTF", &warnings);

    EXPECT_EQ("", error) << "a bounds disagreement must warn, not reject: " << error;
    ASSERT_FALSE(warnings.empty()) << "the decoded data leaves the declared bounds and nothing said so";

    bool named = false;
    for (const std::string& warning : warnings)
    {
        if (warning.find("declared max") != std::string::npos &&
            warning.find("GLTF-061") != std::string::npos)
        {
            named = true;
            // Both numbers have to appear, because "out of bounds" alone does not say whether the
            // bounds are stale by a rounding error or the decode is wrong by a factor of five.
            EXPECT_NE(std::string::npos, warning.find("5.0")) << warning;
        }
    }
    EXPECT_TRUE(named) << "no warning named the bound that was exceeded";
}

TEST(GltfContainerValidation, DataInsideItsDeclaredBoundsProducesNoBoundsWarning)
{
    // The control, and the one that decides whether this check is usable at all: bounds are
    // authored as decimal text and values as binary floats, so an exporter's rounding routinely
    // puts an extreme value a few ULPs outside its stated bound. A check without tolerance would
    // warn on a large share of real files and be turned off within a week.
    std::vector<std::string> warnings;
    const std::string error = ValidationErrorForDocument(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "mode": 4 } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 0] }
  ]
})GLTF", &warnings);

    EXPECT_EQ("", error) << error;
    for (const std::string& warning : warnings)
    {
        EXPECT_EQ(std::string::npos, warning.find("GLTF-061"))
            << "data inside its bounds was reported: " << warning;
    }
}

TEST(GltfContainerValidation, EveryCorpusFixtureAgreesWithItsOwnDeclaredBounds)
{
    // The sweep that gives the check its value: every generated fixture states bounds derived from
    // the same source as its data, so any disagreement is a decode defect rather than an authoring
    // one. This is the assertion that would have caught D4 on the campaign baseline.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (CnaTest::GltfOracle::IsRejectionFixture(fixture.Expected())) { continue; }

        std::vector<std::string> warnings;
        CrossCheckAccessorBoundsEXT(&fixture.Data(), warnings);
        for (const std::string& warning : warnings)
        {
            ADD_FAILURE() << warning;
        }
    }
}
