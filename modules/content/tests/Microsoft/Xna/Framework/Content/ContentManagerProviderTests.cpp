// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <optional>
#include <string>

#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ResourceContentManager.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Resources/ResourceManager.hpp"

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ResourceContentManager;

namespace
{
    template<class Construct>
    void ExpectNullProvider(Construct construct)
    {
        try
        {
            construct();
            FAIL() << "null serviceProvider must be refused";
        }
        catch (const System::ArgumentNullException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), "serviceProvider");
        }
    }

    class CountingEmptyProvider final : public System::IServiceProvider
    {
    public:
        mutable int lookups = 0;
        void* GetService(const std::type_info&) const override
        {
            ++lookups;
            return nullptr;
        }
    };
}

TEST(ContentManagerProviderTest, ProviderOnlyRejectsNullWithParameterName)
{
    // XNA Refresh ContentManager::.ctor(IServiceProvider) IL_0000-000c delegates
    // to .ctor(IServiceProvider, string), whose IL_0021-002e throws with this name.
    ExpectNullProvider([] { ContentManager manager(nullptr); });
}

TEST(ContentManagerProviderTest, ProviderAndRootRejectsNullForEmptyAndNonemptyRoots)
{
    // XNA Refresh ContentManager::.ctor(IServiceProvider, string) IL_0021-002e:
    // null provider refusal is independent of the root string's contents.
    for (const char* root : {"", "Content"})
    {
        SCOPED_TRACE(root);
        ExpectNullProvider([&] { ContentManager manager(nullptr, root); });
    }
}

TEST(ContentManagerProviderTest, BothOverloadsRetainProviderWithoutLookingUpServices)
{
    // XNA Refresh .ctor IL_0044-0046 stores the provider; neither constructor
    // calls GetService. The one-argument overload supplies string.Empty at IL_0002.
    CountingEmptyProvider services;
    ContentManager emptyRoot(&services);
    ContentManager explicitRoot(&services, "assets/../unchanged");
    EXPECT_EQ(emptyRoot.getServiceProviderProperty(), &services);
    EXPECT_EQ(explicitRoot.getServiceProviderProperty(), &services);
    EXPECT_EQ(emptyRoot.getRootDirectoryProperty(), "");
    EXPECT_EQ(explicitRoot.getRootDirectoryProperty(), "assets/../unchanged");
    EXPECT_EQ(services.lookups, 0);
}

TEST(ContentManagerProviderTest, ResourceConstructorRejectsNullProviderWithValidResources)
{
    // XNA Refresh ResourceContentManager::.ctor IL_0000-0002 calls the base
    // before touching the resource manager. No resource lookup is permitted here.
    int lookups = 0;
    System::Resources::ResourceManager resources("TQ047", [&](auto, auto, auto)
        -> std::optional<std::string> { ++lookups; return std::nullopt; });
    ExpectNullProvider([&] { ResourceContentManager manager(nullptr, &resources); });
    EXPECT_EQ(lookups, 0);
    ResourceContentManager valid(CNA::TestSupport::EmptyServices(), &resources);
    EXPECT_EQ(valid.getServiceProviderProperty(), CNA::TestSupport::EmptyServices());
    EXPECT_EQ(valid.getRootDirectoryProperty(), "");
    EXPECT_EQ(lookups, 0);
}

TEST(ContentManagerProviderTest, ResourceConstructorRejectsProviderBeforeResourceManager)
{
    // XNA Refresh ResourceContentManager::.ctor IL_0000-0002 precedes IL_0007-0014:
    // when both are null, the exception names serviceProvider, not resourceManager.
    ExpectNullProvider([] { ResourceContentManager manager(nullptr, nullptr); });
}

TEST(ContentManagerProviderTest, ResourceExtensionStillRequiresItsExplicitProvider)
{
    // CNAEXT one-provider constructor delegates to the XNA base overload.
    // Provider-free use remains ContentManager(), not ResourceContentManager(nullptr).
    ExpectNullProvider([] { ResourceContentManager manager(nullptr); });
    ResourceContentManager valid(CNA::TestSupport::EmptyServices());
    EXPECT_EQ(valid.getServiceProviderProperty(), CNA::TestSupport::EmptyServices());
    EXPECT_EQ(valid.getRootDirectoryProperty(), "");
}
