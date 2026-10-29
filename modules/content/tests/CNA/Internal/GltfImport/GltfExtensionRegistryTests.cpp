// SPDX-License-Identifier: MS-PL
//
// GLTF-334 / GLTF-335 / GLTF-348: the extension registry, and the rules that keep it from becoming a
// third place the truth is written down.
//
// Before GLTF-334 there were two: `IsGltfExtensionSupportedEXT`'s hand-written list, which decides
// whether a file naming an extension in `extensionsRequired` loads at all, and the campaign's
// classification table, which is what a human reads. They already disagreed in spirit -- the table
// called KHR_texture_transform PARTIAL while the gate claimed it -- and nothing said why, because the
// two axes ("how completely is it implemented" and "do we accept a file that demands it") had never
// been separated.
//
// The registry separates them and this file holds it to two rules:
//
//   * the gate is a lookup into the registry, for every entry and for names not in it at all;
//   * an extension a corpus fixture declares must be classified, and an extension CNA claims must
//     have a fixture -- which is GLTF-335's "adding an extension requires a fixture and a
//     classification", enforced rather than asked for.
//
// The human-readable table is docs/gltf-limitations.md §1, which GltfLimitationsDoc holds to the
// registry row by row.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using CNA::Internal::GltfImport::FindGltfExtensionEXT;
using CNA::Internal::GltfImport::GltfExtensionRecordEXT;
using CNA::Internal::GltfImport::GltfExtensionRegistryEXT;
using CNA::Internal::GltfImport::GltfExtensionSupportEXT;
using CNA::Internal::GltfImport::GltfExtensionSupportNameEXT;
using CNA::Internal::GltfImport::ValidateGltfEXT;
using CNA::Internal::GltfImport::IsGltfExtensionSupportedEXT;
using CnaTest::GltfOracle::LoadedFixture;

namespace
{
    /// Every extension name declared by any corpus fixture, in `extensionsUsed` or `Required`.
    std::set<std::string> ExtensionsDeclaredByTheCorpus()
    {
        std::set<std::string> declared;
        for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds())
        {
            const LoadedFixture fixture(id);
            if (!fixture.Ok()) { continue; }
            for (const char* key : {"l1.extensionsUsed", "l1.extensionsRequired"})
            {
                for (const std::string& name :
                     CnaTest::GltfOracle::Strings(CnaTest::GltfOracle::Path(fixture.Expected(), key)))
                {
                    declared.insert(name);
                }
            }
        }
        return declared;
    }
}

// --- GLTF-334: one table, and everything reads from it -------------------------------------------

TEST(GltfExtensionRegistry, TheSupportGateIsALookupIntoTheRegistryForEveryEntry)
{
    // The property that makes the registry the single source of truth rather than a third copy: if
    // the gate could disagree with a record, the record would be documentation and the list would
    // still be the truth.
    ASSERT_FALSE(GltfExtensionRegistryEXT().empty());
    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        SCOPED_TRACE(record.name);
        EXPECT_EQ(record.claimed, IsGltfExtensionSupportedEXT(record.name));
        EXPECT_FALSE(record.note.empty()) << "a classification with no reason is not a decision";
        EXPECT_FALSE(record.task.empty()) << "every entry names the task that owns it";
    }
}

TEST(GltfExtensionRegistry, AnUnclassifiedExtensionIsNeitherFoundNorClaimed)
{
    // The default answer for a name nobody has thought about must be "no". An extension CNA has
    // never heard of cannot be one whose semantics it honours, and a gate that defaulted the other
    // way would accept every future extension the moment it was invented.
    EXPECT_EQ(nullptr, FindGltfExtensionEXT("KHR_materials_not_a_real_extension"));
    EXPECT_FALSE(IsGltfExtensionSupportedEXT("KHR_materials_not_a_real_extension"));
    EXPECT_FALSE(IsGltfExtensionSupportedEXT(""));
}

TEST(GltfExtensionRegistry, EveryNameAppearsExactlyOnce)
{
    // Two records for one name would make FindGltfExtensionEXT's answer depend on order, and the
    // one it did not return would look like a decision nobody made.
    std::set<std::string> seen;
    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        EXPECT_TRUE(seen.insert(record.name).second) << "duplicate registry entry " << record.name;
    }
}

TEST(GltfExtensionRegistry, ClaimingAnExtensionRequiresActuallyImplementingSomethingOfIt)
{
    // The two axes are independent -- an approximation may be claimed or not -- but not
    // arbitrarily: nothing CNA merely notices, does not handle, or has decided against may be
    // claimed. Without this, the gate could be widened one entry at a time with no visible rule.
    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        if (!record.claimed) { continue; }
        SCOPED_TRACE(record.name);
        EXPECT_LE(record.support, GltfExtensionSupportEXT::Approximated)
            << "claimed, but classified as " << GltfExtensionSupportNameEXT(record.support)
            << " -- a file that REQUIRES this would load and not get what it asked for";
    }
}

TEST(GltfExtensionRegistry, AnApproximationMayBeUnclaimedAndOneIs)
{
    // The control for the rule above, and the reason the two columns exist separately at all.
    // KHR_materials_transmission is approximated AND refused: the approximation is good enough to
    // render with and not good enough to promise, and collapsing support into claimed would force
    // that decision to be wrong in one direction or the other.
    const GltfExtensionRecordEXT* transmission =
        FindGltfExtensionEXT("KHR_materials_transmission");
    ASSERT_NE(nullptr, transmission);
    EXPECT_EQ(GltfExtensionSupportEXT::Approximated, transmission->support);
    EXPECT_FALSE(transmission->claimed)
        << "a file REQUIRING transmission is asking for refraction CNA cannot deliver";

    const GltfExtensionRecordEXT* lights = FindGltfExtensionEXT("KHR_lights_punctual");
    ASSERT_NE(nullptr, lights);
    EXPECT_EQ(GltfExtensionSupportEXT::Approximated, lights->support);
    EXPECT_TRUE(lights->claimed)
        << "the two approximations are classified alike and answered differently, which is the "
           "whole reason support and claimed are separate columns";
}

// --- GLTF-335: adding an extension requires a fixture and a classification -----------------------

TEST(GltfExtensionRegistry, EveryExtensionTheCorpusDeclaresIsClassified)
{
    // Half of GLTF-335's template. A fixture that declares an extension nobody classified is a
    // file CNA silently ignores or loudly refuses depending on a list the fixture's author never
    // saw -- so the fixture would be asserting the behaviour of an accident.
    const std::set<std::string> declared = ExtensionsDeclaredByTheCorpus();
    ASSERT_FALSE(declared.empty()) << "no corpus fixture declares any extension";

    for (const std::string& name : declared)
    {
        EXPECT_NE(nullptr, FindGltfExtensionEXT(name))
            << "a corpus fixture declares " << name << ", which the registry does not classify. "
               "Add a record: its classification decides whether a file REQUIRING it loads.";
    }
}

TEST(GltfExtensionRegistry, EveryExtensionCnaClaimsHasAFixtureThatDeclaresIt)
{
    // The other half, and the sharper one. Claiming an extension means accepting a file that
    // cannot work without it; a claim with no fixture is a promise nothing tests. The exemption is
    // narrow and stated per entry rather than blanket: an extension whose support is compiled out
    // in this build cannot have a passing fixture here, and refusing to run would report a missing
    // dependency as a conformance failure.
    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        if (!record.claimed) { continue; }
        SCOPED_TRACE(record.name);
        const std::set<std::string> declared = ExtensionsDeclaredByTheCorpus();
        EXPECT_NE(declared.end(), declared.find(record.name))
            << "CNA claims " << record.name << " -- accepting any file that REQUIRES it -- but no "
               "corpus fixture declares it, so nothing checks the claim.";
    }
}

// --- GLTF-348: NOT_DESIRED is a decision, and a required use is still refused --------------------

TEST(GltfExtensionRegistry, TheNotDesiredExtensionsAreRecordedWithAReasonAndNoneIsClaimed)
{
    // NOT_DESIRED differs from UNSUPPORTED in exactly one way that matters: it means somebody
    // decided, so a future reader finds a rationale instead of an omission and does not spend a
    // day implementing something the project does not want. The rationale is the note; the test
    // is that there is one, that it is specific, and that the decision has no effect on the gate
    // -- a file REQUIRING one of these is still refused by name.
    const std::vector<std::string> notDesired = {
        "KHR_materials_iridescence", "KHR_materials_anisotropy", "KHR_materials_dispersion"};

    for (const std::string& name : notDesired)
    {
        SCOPED_TRACE(name);
        const GltfExtensionRecordEXT* record = FindGltfExtensionEXT(name);
        ASSERT_NE(nullptr, record) << "classified nowhere, so it is an omission rather than a "
                                      "decision -- which is exactly what GLTF-348 is about";
        EXPECT_EQ(GltfExtensionSupportEXT::NotDesired, record->support);
        EXPECT_FALSE(record->claimed);
        EXPECT_FALSE(IsGltfExtensionSupportedEXT(name))
            << "a file that REQUIRES this must be refused by name, not loaded and left to look "
               "wrong -- 'not desired' is not 'harmless to pretend'";
        EXPECT_GT(record->note.size(), 40u)
            << "the note is the whole difference between a decision and an omission";
    }
}

// --- GLTF-345 / GLTF-346 / GLTF-347: a deferral has to be reported ------------------------------

TEST(GltfExtensionRegistry, EveryDeferredMaterialExtensionIsReportedByNameRatherThanIgnoredSilently)
{
    // Clearcoat, sheen and volume are each deferred by decision -- a BRDF lobe or a volumetric term
    // no CNA stock effect has -- and each row's acceptance is "implemented **or explicitly deferred
    // with a report entry**". This is the report entry, asserted rather than asserted-to-exist:
    // `mat-unimplemented-extensions` declares all three with non-default parameters, and validation
    // must produce a warning naming each one.
    //
    // Non-default parameters matter. A reader that "supported" these by parsing them and applying
    // nothing would be indistinguishable from one that ignored them if every authored value were
    // the extension's own default.
    const LoadedFixture fixture("mat-unimplemented-extensions");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    std::vector<std::string> warnings;
    ASSERT_NO_THROW(ValidateGltfEXT(&fixture.Data(), "mat-unimplemented-extensions", warnings))
        << "a file that merely USES an unimplemented extension must import, not be refused -- "
           "extensionsRequired is the normative list, and none of these is on it";

    const std::vector<std::string> deferred{
        "KHR_materials_clearcoat", "KHR_materials_sheen", "KHR_materials_volume"};
    for (const std::string& name : deferred)
    {
        SCOPED_TRACE(name);
        const GltfExtensionRecordEXT* record = FindGltfExtensionEXT(name);
        ASSERT_NE(nullptr, record) << "the registry does not classify " << name;
        EXPECT_EQ(std::string("PARSED_BUT_IGNORED"), GltfExtensionSupportNameEXT(record->support))
            << name << " is no longer deferred; if it was implemented, this test and its row are "
                       "the things to update";
        EXPECT_FALSE(record->claimed)
            << name << " is claimed, so a file REQUIRING it would now load -- with the extension "
                       "still ignored, which is the outcome the claim flag exists to prevent";

        const bool named = std::any_of(warnings.begin(), warnings.end(),
                                        [&name](const std::string& warning) {
                                            return warning.find(name) != std::string::npos;
                                        });
        EXPECT_TRUE(named) << "the import produced no warning naming " << name
                           << ", so its contribution is absent from the result and nothing says so";
    }
    EXPECT_GE(warnings.size(), deferred.size());
}

TEST(GltfExtensionRegistry, FresnelExtensionsExposeOnlyTheRemainingTextureResidue)
{
    const LoadedFixture fixture("mat-factor-only-gold");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    std::vector<std::string> warnings;
    ASSERT_NO_THROW(ValidateGltfEXT(&fixture.Data(), "mat-factor-only-gold", warnings));

    const auto warningFor = [&warnings](const std::string& extension) -> std::string {
        for (const std::string& warning : warnings)
        {
            if (warning.find(extension) != std::string::npos) { return warning; }
        }
        return {};
    };

    const GltfExtensionRecordEXT* ior = FindGltfExtensionEXT("KHR_materials_ior");
    ASSERT_NE(nullptr, ior);
    EXPECT_EQ(GltfExtensionSupportEXT::Implemented, ior->support);
    EXPECT_TRUE(ior->claimed);
    EXPECT_TRUE(warningFor("KHR_materials_ior").empty())
        << "a fully consumed IOR factor is still reported as a limitation";

    const GltfExtensionRecordEXT* specular =
        FindGltfExtensionEXT("KHR_materials_specular");
    ASSERT_NE(nullptr, specular);
    EXPECT_EQ(GltfExtensionSupportEXT::ImplementedWithNamedLimit, specular->support);
    EXPECT_FALSE(specular->claimed)
        << "a required use may need either texture on a renderer whose binding is still pending";

    const std::string warning = warningFor("KHR_materials_specular");
    ASSERT_FALSE(warning.empty()) << "the partial renderer coverage was silent";
    EXPECT_NE(std::string::npos, warning.find("specularTexture"))
        << "the scalar specular texture residue is not named: " << warning;
    EXPECT_NE(std::string::npos, warning.find("specularColorTexture"))
        << "the colour specular texture residue is not named: " << warning;
}

TEST(GltfExtensionRegistry, RequiredMeshQuantizationIsClaimedAndValidated)
{
    // The quantized-normal fixtures used to author extension-only formats as if they were core
    // glTF. Both now declare the required extension and its alignment rules; CNA already decodes
    // the formats generically, so the support gate must claim what the source actually requires.
    const GltfExtensionRecordEXT* record = FindGltfExtensionEXT("KHR_mesh_quantization");
    ASSERT_NE(nullptr, record);
    EXPECT_EQ(GltfExtensionSupportEXT::Implemented, record->support);
    EXPECT_TRUE(record->claimed);

    for (const std::string& id : {"normal-quantized", "normalized-i8-normal"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const std::vector<std::string> required =
            CnaTest::GltfOracle::Strings(
                CnaTest::GltfOracle::Path(fixture.Expected(), "l1.extensionsRequired"));
        EXPECT_NE(required.end(), std::find(required.begin(), required.end(),
                                            "KHR_mesh_quantization"));
        std::vector<std::string> warnings;
        EXPECT_NO_THROW(ValidateGltfEXT(&fixture.Data(), id, warnings));
    }
}
