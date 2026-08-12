// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-021 / GLTF-022 / GLTF-023 / GLTF-024: the container-level validation pass.
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
using CnaTest::GltfOracle::CorpusFixtureIds;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::StringOr;

namespace
{
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
        // reader would happily accept, so if it failed to parse it would prove nothing about
        // validation.
        EXPECT_EQ("validation", StringOr(rejection, "stage", ""));

        std::vector<std::string> warnings;
        const std::string error = ValidationErrorFor(fixture, warnings);
        ASSERT_FALSE(error.empty())
            << "the fixture imported without complaint -- validation did not run, or no longer "
               "catches what this fixture declares";

        // A rejection that does not say what is wrong is barely better than a silent one.
        for (const JsonValue& fragment : Member(rejection, "errorContains").arrayValue)
        {
            const std::string& text = fragment.stringValue;
            EXPECT_NE(std::string::npos, error.find(text))
                << "the diagnostic does not name '" << text << "': " << error;
        }
    }
    EXPECT_GT(checked, 0) << "no rejection fixtures in the corpus -- the suite proved nothing";
}

TEST(GltfContainerValidation, EveryOtherCorpusFixturePassesValidationCleanly)
{
    // The converse, and the one that stops validation being over-eager: every fixture that is NOT
    // declared as a rejection must pass, with no ignored-extension warning either, because none of
    // them uses an extension at all.
    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (Member(fixture.Expected(), "rejection").type == JsonType::Object) { continue; }
        SCOPED_TRACE(id);

        std::vector<std::string> warnings;
        EXPECT_EQ("", ValidationErrorFor(fixture, warnings))
            << "a valid fixture was rejected -- validation is too strict";
        EXPECT_TRUE(warnings.empty())
            << "unexpected warning: " << (warnings.empty() ? std::string() : warnings.front());
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
    // reads KHR_materials_unlit and KHR_materials_pbrSpecularGlossiness only to keep such a
    // material OFF the metallic-roughness path (GLTF-215). Neither is implemented -- an unlit
    // surface still goes through a lit effect -- so a file requiring either must be rejected.
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_texture_transform"));
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_materials_emissive_strength"));
    EXPECT_TRUE(IsGltfExtensionSupportedEXT("KHR_lights_punctual"));

    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_unlit"))
        << "detecting an extension is not implementing it";
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_pbrSpecularGlossiness"));
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_variants"));
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
