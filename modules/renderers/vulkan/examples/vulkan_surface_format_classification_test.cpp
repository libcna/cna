// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-170 -- the renderer's SurfaceFormat verdict must agree with what
// Texture2D construction actually does, for EVERY SurfaceFormat value, not for a chosen few.
//
// Two separate things decide whether a Texture2D can be created, and the whole point of this
// fixture is that they are kept apart:
//
//   * the GraphicsProfile, which refuses eleven formats at Reach whatever the hardware can do
//     (System::NotSupportedException, XNA's own exception type for its own restriction); and
//   * the RENDERER, through IGraphicsRenderer::ClassifySurfaceFormatEXT's tri-state verdict --
//     Supported ("I store this"), Unsupported ("I would store it, this device cannot") and
//     Defer ("I have not looked; the framework's rule applies", and that rule admits Color alone).
//
// The discriminating assertion is that the verdict PREDICTS construction. A renderer that answered
// Supported for a format vkCreateImage would refuse, or Defer for a format it actually stores, is
// exactly what this fails on -- and that is the failure mode VULKAN-170 exists to prevent as
// VULKAN-172/173/174 add formats one at a time. It is written to keep working as they do: nothing
// here lists which formats are expected to be supported, so a format promoted in the renderer's own
// table needs no edit here, while a promotion the storage cannot back turns this red.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::RendererFormatVerdict;

namespace
{
    struct NamedFormat { SurfaceFormat format; const char* name; };

    /// Every SurfaceFormat the public enum declares, in declaration order. Listed by hand on
    /// purpose: a value added to the enum should make this file's own count assertion fail and be
    /// classified deliberately, not swept in by a loop over an ordinal range.
    const std::vector<NamedFormat> kAllFormats = {
        { SurfaceFormat::Color,           "Color" },
        { SurfaceFormat::Bgr565,          "Bgr565" },
        { SurfaceFormat::Bgra5551,        "Bgra5551" },
        { SurfaceFormat::Bgra4444,        "Bgra4444" },
        { SurfaceFormat::Dxt1,            "Dxt1" },
        { SurfaceFormat::Dxt3,            "Dxt3" },
        { SurfaceFormat::Dxt5,            "Dxt5" },
        { SurfaceFormat::NormalizedByte2, "NormalizedByte2" },
        { SurfaceFormat::NormalizedByte4, "NormalizedByte4" },
        { SurfaceFormat::Rgba1010102,     "Rgba1010102" },
        { SurfaceFormat::Rg32,            "Rg32" },
        { SurfaceFormat::Rgba64,          "Rgba64" },
        { SurfaceFormat::Alpha8,          "Alpha8" },
        { SurfaceFormat::Single,          "Single" },
        { SurfaceFormat::Vector2,         "Vector2" },
        { SurfaceFormat::Vector4,         "Vector4" },
        { SurfaceFormat::HalfSingle,      "HalfSingle" },
        { SurfaceFormat::HalfVector2,     "HalfVector2" },
        { SurfaceFormat::HalfVector4,     "HalfVector4" },
        { SurfaceFormat::HdrBlendable,    "HdrBlendable" },
        { SurfaceFormat::ColorBgraEXT,    "ColorBgraEXT" },
        { SurfaceFormat::ColorSrgbEXT,    "ColorSrgbEXT" },
        { SurfaceFormat::Dxt5SrgbEXT,     "Dxt5SrgbEXT" },
        { SurfaceFormat::Bc7EXT,          "Bc7EXT" },
        { SurfaceFormat::Bc7SrgbEXT,      "Bc7SrgbEXT" },
        { SurfaceFormat::ByteEXT,         "ByteEXT" },
        { SurfaceFormat::UShortEXT,       "UShortEXT" },
    };

    const char* VerdictName(RendererFormatVerdict v)
    {
        switch (v)
        {
            case RendererFormatVerdict::Supported:   return "Supported";
            case RendererFormatVerdict::Unsupported: return "Unsupported";
            case RendererFormatVerdict::Defer:       return "Defer";
        }
        return "?";
    }

    /// What one construction attempt did, without judging it yet.
    struct Attempt
    {
        bool        constructed = false;
        bool        profileRefused = false;   ///< System::NotSupportedException: the profile's own no.
        std::string message;
        int         reportedFormat = -1;      ///< GetSurfaceFormatEXT() when it constructed (F-11).
        VkFormat    nativeFormat = VK_FORMAT_UNDEFINED;  ///< The VkImage's real format.
    };
}

class SurfaceFormatClassificationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;
    int pass_ = 0;
    int total_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++pass_;
    }

    static Attempt TryCreate(GraphicsDevice& dev, SurfaceFormat format)
    {
        Attempt a;
        try
        {
            Texture2D t(dev, 4, 4, false, format);
            a.constructed = true;
            const auto r = t.GetRendererWeak().lock();
            a.reportedFormat = r ? r->GetSurfaceFormatEXT() : -1;
            if (auto* vkTex =
                    dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanTextureRenderer*>(r.get()))
                a.nativeFormat = vkTex->GetVkFormatEXT();
        }
        catch (const System::NotSupportedException& e)
        {
            a.profileRefused = true;
            a.message = e.what();
        }
        catch (const std::exception& e)
        {
            a.message = e.what();
        }
        return a;
    }

public:
    SurfaceFormatClassificationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = dev.GetRenderer();
        const GraphicsProfile startProfile = dev.getGraphicsProfileProperty();

        // A0. The enum has not grown behind this file's back.
        check(kAllFormats.size() == 27,
              "A0 the SurfaceFormat list this file classifies is complete (" +
                  std::to_string(kAllFormats.size()) + " values)");

        // plans/plan_vulkan_parity.md VKPAR-0020: both sweeps run at BOTH profiles. At Reach
        // alone the eleven HiDef formats only ever reach leg B, so the storage this renderer
        // holds for them would be classified here and never constructed.
        for (const GraphicsProfile profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
        {
            dev.SetGraphicsProfileEXT(profile);
            const std::string profileTag =
                profile == GraphicsProfile::HiDef ? "[HiDef] " : "[Reach] ";
            int supported = 0, unsupported = 0, deferred = 0;

            for (const auto& nf : kAllFormats)
            {
                const int ordinal = static_cast<int>(nf.format);
                const RendererFormatVerdict verdict = renderer.ClassifySurfaceFormatEXT(ordinal);
                const bool profileAllows = Texture::IsFormatAllowedByProfileEXT(profile, nf.format);
                const Attempt a = TryCreate(dev, nf.format);
                const std::string where = profileTag + nf.name + " (verdict " + VerdictName(verdict) + ")";

                switch (verdict)
                {
                    case RendererFormatVerdict::Supported:  ++supported;   break;
                    case RendererFormatVerdict::Unsupported: ++unsupported; break;
                    case RendererFormatVerdict::Defer:      ++deferred;    break;
                }

                if (!profileAllows)
                {
                    // The profile is asked first and refuses with XNA's own exception type. The
                    // renderer's verdict is not what decided this case, so it is not judged here --
                    // only that the refusal came from the profile and named itself.
                    check(!a.constructed && a.profileRefused,
                          "B " + where + ": the GraphicsProfile refuses it, and says so [" +
                              a.message + "]");
                    continue;
                }

                switch (verdict)
                {
                    case RendererFormatVerdict::Supported:
                        check(a.constructed,
                              "C " + where + ": Supported must construct [" + a.message + "]");
                        if (a.constructed)
                        {
                            // plan_vulkan.md VULKAN-172: the renderer's storage description must
                            // agree with the FRAMEWORK'S OWN arithmetic for the same format. This is
                            // not bookkeeping -- the shared layer sizes every upload with
                            // Texture::GetFormatSizeEXT and GetBlockSizeSquaredEXT, while this
                            // renderer sizes its staging buffer and its memcpy from its own table, so
                            // a disagreement is a buffer OVER-READ rather than a wrong picture. That
                            // is why it is asserted here and cannot be left to the pixel tests:
                            // measured, doubling Dxt1's block size to 16 leaves
                            // Vulkan_DxtFormat at 3/3, because the GPU walks BC1 blocks by the
                            // VkFormat and only the byte count moves.
                            {
                                CNA::Internal::Renderers::Vulkan::VulkanRenderer
                                    ::VulkanSurfaceFormatStorageEXT storage{};
                                auto* vk = dynamic_cast<
                                    CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(&renderer);
                                const bool mapped =
                                    vk != nullptr &&
                                    vk->MapSurfaceFormatToStorageEXT(ordinal, storage);
                                const int frameworkUnitBytes = Texture::GetFormatSizeEXT(nf.format);
                                const int frameworkBlockSq = Texture::GetBlockSizeSquaredEXT(nf.format);
                                check(mapped &&
                                          storage.bytesPerTexel == frameworkUnitBytes &&
                                          storage.blockExtent * storage.blockExtent == frameworkBlockSq,
                                      "D2 " + where + ": the storage description agrees with the "
                                      "framework's own sizing (" +
                                          std::to_string(storage.bytesPerTexel) + "B x " +
                                          std::to_string(storage.blockExtent) + "^2 vs " +
                                          std::to_string(frameworkUnitBytes) + "B x " +
                                          std::to_string(frameworkBlockSq) + ")");
                            }
                            // F-11: the texture reports the format it was created with.
                            check(a.reportedFormat == ordinal,
                                  "D " + where + ": the texture reports its own format (" +
                                      std::to_string(a.reportedFormat) + " vs " +
                                      std::to_string(ordinal) + ")");
                        }
                        break;
                    case RendererFormatVerdict::Unsupported:
                        // "Refuses by name": the renderer's own refusal, not the profile's, and the
                        // message identifies the format rather than being a bare failure.
                        check(!a.constructed && !a.profileRefused &&
                                  a.message.find(std::to_string(ordinal)) != std::string::npos,
                              "E " + where + ": Unsupported must refuse by name [" + a.message + "]");
                        break;
                    case RendererFormatVerdict::Defer:
                        // Defer hands the decision to Texture::ValidateFormat, which admits Color only.
                        check(a.constructed == (nf.format == SurfaceFormat::Color),
                              "F " + where + ": Defer follows the framework rule (Color only) [" +
                                  a.message + "]");
                        break;
                }
            }

            // G. The classification is not vacuous in the direction that matters: at least one format
            // is claimed, and it is the one every renderer must carry. Without this leg a renderer that
            // deferred EVERYTHING would satisfy every check above.
            check(renderer.ClassifySurfaceFormatEXT(static_cast<int>(SurfaceFormat::Color)) ==
                      RendererFormatVerdict::Supported,
                  "G Color is claimed by the renderer itself, not merely deferred to the framework");

            std::printf("[INFO] verdicts: %d Supported, %d Unsupported, %d Defer\n",
                        supported, unsupported, deferred);

            // ---------------------------------------------------------------------------------
            // plan_vulkan.md VULKAN-171 -- the RENDER-TARGET verdict, which is a different question
            // from the texture one and is asked separately here for that reason.
            //
            // MOD-2223 added a separate nine-format RenderTarget2D allocation table. It deliberately
            // differs from texture storage: float/HDR formats are target-backed and sampleable while
            // packed/BC/SNORM textures do not become renderable by accident. This sweep proves the
            // two implementation boundaries stay independent and construction follows the RT one.
            // ---------------------------------------------------------------------------------
            {
                int rtSupported = 0, rtUnsupported = 0, rtDeferred = 0;
                for (const auto& nf : kAllFormats)
                {
                    const int ordinal = static_cast<int>(nf.format);
                    const RendererFormatVerdict verdict = renderer.ClassifyRenderTargetFormatEXT(ordinal);
                    switch (verdict)
                    {
                        case RendererFormatVerdict::Supported:   ++rtSupported;   break;
                        case RendererFormatVerdict::Unsupported: ++rtUnsupported; break;
                        case RendererFormatVerdict::Defer:       ++rtDeferred;    break;
                    }

                    bool constructed = false;
                    int reportedFormat = -1;
                    std::string message;
                    try
                    {
                        RenderTarget2D rt(dev, 4, 4, false, nf.format, DepthFormat::None, 0,
                                          RenderTargetUsage::DiscardContents);
                        constructed = true;
                        reportedFormat = static_cast<int>(rt.getFormatProperty());
                    }
                    catch (const std::exception& e) { message = e.what(); }

                    const std::string where =
                        profileTag + "RT " + nf.name + " (verdict " + VerdictName(verdict) + ")";

                    // The public query must agree with the constructor, always -- that is
                    // GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT's whole promise. It
                    // answers the EXACT question, so the profile is part of it: a Supported verdict
                    // for a HiDef-only format is still "no" at Reach.
                    const bool exact =
                        Texture::IsRenderTargetFormatAllowedByProfileEXT(profile, nf.format) &&
                        (verdict == RendererFormatVerdict::Supported ||
                         (verdict == RendererFormatVerdict::Defer && nf.format == SurfaceFormat::Color));
                    const bool publicSaysYes = dev.SupportsSurfaceFormatAsRenderTargetEXT(nf.format);
                    check(publicSaysYes == exact,
                          "J " + where + ": SupportsSurfaceFormatAsRenderTargetEXT matches the verdict "
                          "at this profile");

                    // plans/plan_vulkan_parity.md VKPAR-0020: the format a RenderTarget2D is given is
                    // a PREFERENCE. XNA's constructor asks QueryRenderTargetFormat and builds the
                    // target in the selected format, falling back to Color rather than throwing
                    // (plans/plan_software.md SOFTWARE-216). So construction never refuses on format,
                    // and what it reports is the exact format when the query says yes, Color when it
                    // says no -- whether the no came from the profile, the device, or a deferral.
                    check(constructed,
                          "K " + where + ": a preferred format is never refused [" + message + "]");
                    const int wantFormat = static_cast<int>(exact ? nf.format : SurfaceFormat::Color);
                    check(!constructed || reportedFormat == wantFormat,
                          "L " + where + ": reports the format it was built in (" +
                              std::to_string(reportedFormat) + ", want " +
                              std::to_string(wantFormat) + ")");
                }
                std::printf("[INFO] render-target verdicts: %d Supported, %d Unsupported, %d Defer\n",
                            rtSupported, rtUnsupported, rtDeferred);

                int mappedTargets = 0;
                auto* vk = dynamic_cast<
                    CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(&renderer);
                for (const auto& nf : kAllFormats)
                {
                    CNA::Internal::Renderers::Vulkan::VulkanRenderer
                        ::VulkanSurfaceFormatStorageEXT storage{};
                    if (vk != nullptr && vk->MapRenderTargetFormatToStorageEXT(
                            static_cast<int>(nf.format), storage))
                        ++mappedTargets;
                }
                check(mappedTargets == 9 && rtSupported + rtUnsupported == mappedTargets &&
                          rtDeferred == static_cast<int>(kAllFormats.size()) - mappedTargets,
                      "O renderability verdicts exactly match the independent nine-format target "
                      "allocation table (" + std::to_string(rtSupported) + " supported, " +
                          std::to_string(rtUnsupported) + " device-refused, " +
                          std::to_string(rtDeferred) + " deferred)");
            }

        }
        dev.SetGraphicsProfileEXT(startProfile);

        // I. The Unsupported arm, forced -- and it is forced because it is otherwise unreachable.
        // Every driver this renderer runs on reports SAMPLED_IMAGE|TRANSFER_DST for the one format
        // in the storage table, so without the injection this arm is code no test has ever run.
        // The control (Color constructs before and after) is what stops the injection itself from
        // being mistaken for the refusal.
        {
            using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
            const int colorOrdinal = static_cast<int>(SurfaceFormat::Color);
            check(TryCreate(dev, SurfaceFormat::Color).constructed,
                  "I1 control: Color constructs with no injection");
            VulkanRenderer::SetSurfaceFormatUnsupportedForTestEXT(colorOrdinal);
            const bool verdictMoved =
                renderer.ClassifySurfaceFormatEXT(colorOrdinal) == RendererFormatVerdict::Unsupported;
            const Attempt refused = TryCreate(dev, SurfaceFormat::Color);
            VulkanRenderer::SetSurfaceFormatUnsupportedForTestEXT(-1);
            check(verdictMoved, "I2 the injected device refusal reaches the verdict");
            check(!refused.constructed && !refused.profileRefused,
                  "I3 an Unsupported format is refused by the RENDERER, not by the profile [" +
                      refused.message + "]");
            // "Refuses by name" is this row's own wording, and the first run of this leg showed the
            // shared refusal did NOT name the format -- it said only "has not passed the renderer's
            // promotion gate". Texture2D.cpp now names the ordinal, and this asserts it, so a
            // regression to the anonymous message is caught rather than tolerated.
            check(refused.message.find("SurfaceFormat " + std::to_string(colorOrdinal)) !=
                      std::string::npos,
                  "I4 and the refusal names the format [" + refused.message + "]");
            check(TryCreate(dev, SurfaceFormat::Color).constructed,
                  "I5 control: Color constructs again once the injection is cleared");
        }

        // H. The Khronos layer's verdict on the sweep.
        {
            using CNA::Internal::Renderers::Vulkan::VulkanRenderer;
#ifdef NDEBUG
            // plans/plan_gpu_test_isolation.md GTI-0004: a build with NDEBUG (Release,
            // RelWithDebInfo) runs without the validation layer by design -- VulkanRenderer's
            // sEnableValidation is off under NDEBUG -- so a "no validation message" claim cannot be
            // measured there. Reported as SKIP, not as a failure and never as a vacuous pass. A
            // Debug build still asserts the layer is live, as before.
            std::printf("[SKIP] %s: this build runs without the Vulkan validation layer (NDEBUG)\n",
                        "H1/H2 validation layer loaded, no validation message");
#else
            check(VulkanRenderer::IsValidationActiveEXT(),
                  "H1 VK_LAYER_KHRONOS_validation is loaded, so the count below means something");
            auto* vk = dynamic_cast<VulkanRenderer*>(&renderer);
            if (vk != nullptr)
            {
                const auto& msgs = vk->GetValidationMessagesEXT();
                check(msgs.empty(),
                      "H2 no Vulkan validation message" +
                          (msgs.empty() ? std::string{} : std::string(" -- first: ") + msgs.front()));
            }
            else
            {
                check(false, "H2 Vulkan renderer not reachable");
            }
#endif
        }

        std::printf("%d/%d checks passed\n", pass_, total_);
        result_ = (pass_ == total_ && total_ > 0) ? 0 : 1;
        Exit();
    }
};

int main()
{
    SurfaceFormatClassificationTest test;
    test.Run();
    return test.Result();
}
