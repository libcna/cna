// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
using Microsoft::Xna::Framework::Content::ContentManager;
namespace {
class Tq045Services final : public System::IServiceProvider {
public:
    void* GetService(const std::type_info&) const override { return nullptr; }
};
}
TEST(ContentManagerLifecycleTq045Test, ConstructorsRetainProviderAndEmptyRootIsValid)
{
    // Refresh .ctor IL_003d-004b; one-argument .ctor IL_0002 uses String.Empty.
    Tq045Services services;
    ContentManager implicitRoot(&services), emptyRoot(&services, ""), namedRoot(&services, "A/../B");
    EXPECT_EQ(implicitRoot.getServiceProviderProperty(), &services);
    EXPECT_EQ(emptyRoot.getServiceProviderProperty(), &services);
    EXPECT_EQ(namedRoot.getServiceProviderProperty(), &services);
    EXPECT_EQ(implicitRoot.getRootDirectoryProperty(), "");
    EXPECT_EQ(emptyRoot.getRootDirectoryProperty(), "");
    EXPECT_EQ(namedRoot.getRootDirectoryProperty(), "A/../B");
    const std::string root = "";
    namedRoot.setRootDirectoryProperty(root);
    EXPECT_EQ(namedRoot.getRootDirectoryProperty(), "");
    namedRoot.setRootDirectoryProperty(std::string("Next"));
    EXPECT_EQ(namedRoot.getRootDirectoryProperty(), "Next");
}
