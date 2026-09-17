// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if CNA_DIAGNOSTICS_LEVEL >= 1

#include "CNA/Diagnostics/Diagnostics.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <unordered_set>

namespace
{
    using CNA::Diagnostics::Accuracy;
    using CNA::Diagnostics::GetProvider;
    using CNA::Diagnostics::Mode;
    using CNA::Diagnostics::ResourceId;
    using CNA::Diagnostics::ResourceKind;
    using CNA::Diagnostics::ResourceRecord;
    using CNA::Diagnostics::SetRuntimeMode;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    [[nodiscard]] std::unordered_set<ResourceId> ResourceIds()
    {
        std::unordered_set<ResourceId> ids;
        for (const ResourceRecord& resource : GetProvider().CaptureSnapshot().resources)
            ids.insert(resource.id);
        return ids;
    }

    TEST(GraphicsDiagnosticsTest, TextureMetadataUsesStableEstimatedPayloadAndUnregisters)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        GraphicsDevice device;
        const std::unordered_set<ResourceId> before = ResourceIds();

        ResourceId textureId = 0;
        {
            Texture2D texture(device, 13, 7, false, SurfaceFormat::Color);
            const auto snapshot = GetProvider().CaptureSnapshot();
            const auto found = std::find_if(
                snapshot.resources.begin(), snapshot.resources.end(),
                [&before](const ResourceRecord& resource)
                {
                    return !before.contains(resource.id) &&
                           resource.kind == ResourceKind::Texture2D &&
                           resource.width == 13 && resource.height == 7;
                });
            ASSERT_NE(found, snapshot.resources.end());
            textureId = found->id;
            EXPECT_EQ(found->estimatedBytes, 13u * 7u * 4u);
            EXPECT_EQ(found->byteAccuracy, Accuracy::Estimated);
            texture.Dispose();
        }

        const std::unordered_set<ResourceId> after = ResourceIds();
        EXPECT_FALSE(after.contains(textureId));
    }

    TEST(GraphicsDiagnosticsTest, AssignedReferenceIdentityHasOneDiagnosticResource)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        const std::size_t baseline = GetProvider().CaptureSnapshot().resources.size();
        {
            BlendState first;
            BlendState second;
            EXPECT_EQ(GetProvider().CaptureSnapshot().resources.size(), baseline + 2);
            second = first;
            EXPECT_EQ(GetProvider().CaptureSnapshot().resources.size(), baseline + 1);
        }
        EXPECT_EQ(GetProvider().CaptureSnapshot().resources.size(), baseline);
    }
}

#endif
