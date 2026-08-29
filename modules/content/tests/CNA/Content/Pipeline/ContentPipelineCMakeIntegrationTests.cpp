// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

TEST(ContentPipelineCMakeIntegrationTest, HelperBuildsTheSameCliManagedArtifactTree)
{
#if !defined(CNA_CONTENT_CMAKE_FIXTURE_OUTPUT)
    GTEST_SKIP() << "native CMake content integration is unavailable in this configuration";
#else
    const std::filesystem::path output(CNA_CONTENT_CMAKE_FIXTURE_OUTPUT);
    const std::filesystem::path artifact = output / "Nested" / "curve.cnb";
    ASSERT_TRUE(std::filesystem::is_regular_file(artifact));

    std::ifstream artifactStream(artifact, std::ios::binary);
    const std::vector<std::uint8_t> artifactBytes{
        std::istreambuf_iterator<char>(artifactStream), std::istreambuf_iterator<char>()};
    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(artifactBytes, "CMake content fixture");
    EXPECT_EQ(document.AssetTypeId(), Cnb::CnbAssetTypeId::Curve);
    EXPECT_EQ(document.Metadata().contentName, "Nested/curve");
    EXPECT_EQ(Cnb::DecodeCurveFromCnb(document).getKeysProperty().getCountProperty(), 2);

    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    ASSERT_TRUE(std::filesystem::is_regular_file(manifestPath));
    std::ifstream manifestStream(manifestPath, std::ios::binary);
    const std::string manifestText{std::istreambuf_iterator<char>(manifestStream),
                                   std::istreambuf_iterator<char>()};
    const Pipeline::ContentBuildManifest manifest =
        Pipeline::ContentBuildManifest::Parse(manifestText);
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("Nested/curve");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->source, "Nested/curve.cnj");
    ASSERT_EQ(entry->outputs.size(), 1u);
    EXPECT_EQ(entry->outputs.front().path, "Nested/curve.cnb");
#endif
}
