// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-145: the shared half of VULKAN-144 (finding F-15).
//
// Every 3D pipeline factory in this renderer bakes its VkVertexInputAttributeDescription array from
// the buffer's byte STRIDE, through a ten-entry table. That is why a 28-byte record carrying
// TextureCoordinate0@12 Vector2 is refused outright: the stride says "28", the table has no 28, and
// nothing in the pipeline knows where the UV actually is. BuildVulkanVertexInputLayoutEXT builds it
// from the declaration instead.
//
// This row changes no renderer behaviour -- no pipeline factory uses the builder yet -- so the test
// is about the builder's contract, and the properties it asserts are the ones the conversion will
// depend on:
//
//   A  Two declarations of the SAME STRIDE with elements at different offsets produce different
//      attribute arrays AND different hashes. This is the whole defect: the stride table gives both
//      the same pipeline, and one of them then renders from the wrong bytes.
//   B  Declaration ORDER does not change the result. XNB model data routinely lists
//      TextureCoordinate before Normal, and matching by (usage, usageIndex) means either order
//      yields the same attributes and the same hash. This is the property that makes semantic
//      matching better than EasyGL's index-based binding, so it is asserted rather than assumed.
//   C  Each attribute takes its own element's offset and format, at the location of the input that
//      consumes it -- checked field by field, not by hash.
//   D  An input the declaration does not supply is REPORTED, not given a made-up offset. A Vulkan
//      shader input with no attribute description reads undefined data, so this has to be a
//      question the caller answers, and the diagnostic must name which input went unsupplied.
//   E  An element format this renderer has no VkFormat for is reported separately from a missing
//      one -- a declaration that said nothing and one that said something unrepresentable are
//      different problems for a caller.
//   F  An empty declaration yields "no opinion", so a caller keeps its stride-derived layout. That
//      is VertexBuffer(device, count), which many existing tests use.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanVertexInputLayout.hpp"

#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Graphics::DeclaredVertexLayout;
using CNA::Internal::Graphics::StockProgramInput;
using CNA::Internal::Renderers::Vulkan::BuildVulkanVertexInputLayoutEXT;
using CNA::Internal::Renderers::Vulkan::DescribeVulkanVertexInputGapsEXT;
using CNA::Internal::Renderers::Vulkan::VulkanVertexInputLayoutEXT;

namespace
{
    int gPass = 0;
    int gFail = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++gPass; else ++gFail;
    }

    DeclaredVertexLayout Declare(int stride, std::vector<VertexElement> elements)
    {
        DeclaredVertexLayout out;
        out.Remember(VertexDeclaration(stride, std::move(elements)));
        return out;
    }

    // The lit-textured stock program's inputs, in the order its vertex shader declares them.
    constexpr StockProgramInput kPos{
        VertexElementUsage::Position, 0, VertexElementFormat::Vector3, "aPos"};
    constexpr StockProgramInput kNormal{
        VertexElementUsage::Normal, 0, VertexElementFormat::Vector3, "aNormal"};
    constexpr StockProgramInput kUv{
        VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
    constexpr StockProgramInput kLit[] = {kPos, kNormal, kUv};

    std::string Describe(const VulkanVertexInputLayoutEXT& l)
    {
        std::string out = "count=" + std::to_string(l.attributeCount);
        for (std::uint32_t i = 0; i < l.attributeCount; ++i)
            out += " [loc " + std::to_string(l.attributes[i].location)
                 + " fmt " + std::to_string(static_cast<int>(l.attributes[i].format))
                 + " off " + std::to_string(l.attributes[i].offset) + "]";
        return out;
    }
}

int main()
{
    // ---- A: same stride, different offsets, different layouts -------------------------------
    // Both are 32-byte records the stride table calls "32" and hands the same pipeline. They are
    // not the same vertex: the second swaps where the normal and the UV live.
    const DeclaredVertexLayout a = Declare(32, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    const DeclaredVertexLayout b = Declare(32, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(20, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });

    const auto la = BuildVulkanVertexInputLayoutEXT(a, kLit, std::size(kLit));
    const auto lb = BuildVulkanVertexInputLayoutEXT(b, kLit, std::size(kLit));

    check(la.Hash() != lb.Hash(),
          "A two 32-byte declarations with different offsets hash differently",
          "0x" + std::to_string(la.Hash()) + " vs 0x" + std::to_string(lb.Hash()));
    check(la.attributes[1].offset == 12 && lb.attributes[1].offset == 20,
          "A each layout took its own declaration's normal offset",
          std::to_string(la.attributes[1].offset) + " vs " + std::to_string(lb.attributes[1].offset));

    // ---- B: declaration order is irrelevant ---------------------------------------------------
    // The same three elements, listed UV first -- the order compiled XNB model data commonly uses.
    const DeclaredVertexLayout aReordered = Declare(32, {
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
    });
    const auto lr = BuildVulkanVertexInputLayoutEXT(aReordered, kLit, std::size(kLit));
    check(lr.Hash() == la.Hash(),
          "B listing the same elements in a different order gives the same layout",
          Describe(lr));

    // ---- C: field-by-field, not by hash --------------------------------------------------------
    bool fields = la.attributeCount == 3;
    fields = fields && la.attributes[0].location == 0 && la.attributes[0].offset == 0
                    && la.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT;
    fields = fields && la.attributes[1].location == 1 && la.attributes[1].offset == 12
                    && la.attributes[1].format == VK_FORMAT_R32G32B32_SFLOAT;
    fields = fields && la.attributes[2].location == 2 && la.attributes[2].offset == 24
                    && la.attributes[2].format == VK_FORMAT_R32G32_SFLOAT;
    check(fields, "C every attribute is at its input's location with its element's offset/format",
          Describe(la));
    check(la.IsComplete(), "C a fully supplied declaration is complete", Describe(la));

    // ---- D: an unsupplied input is reported, never invented ------------------------------------
    const DeclaredVertexLayout noNormal = Declare(20, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    const auto ln = BuildVulkanVertexInputLayoutEXT(noNormal, kLit, std::size(kLit));
    check(ln.missingInputMask == (1u << 1) && ln.attributeCount == 2 && !ln.IsComplete(),
          "D the unsupplied normal produces no attribute and one missing bit",
          Describe(ln) + ", missingMask=" + std::to_string(ln.missingInputMask));
    const std::string gaps = DescribeVulkanVertexInputGapsEXT(ln, kLit, std::size(kLit));
    check(gaps.find("aNormal") != std::string::npos && gaps.find("not declared") != std::string::npos,
          "D the diagnostic names which input went unsupplied", gaps);
    // And the UV still landed at its own location, not shuffled up into the gap.
    check(ln.attributes[1].location == 2 && ln.attributes[1].offset == 12,
          "D the inputs after a gap keep their own locations",
          Describe(ln));

    // ---- E: an unrepresentable format is a different report ------------------------------------
    const DeclaredVertexLayout badFormat = Declare(32, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, static_cast<VertexElementFormat>(999), VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    const auto lf = BuildVulkanVertexInputLayoutEXT(badFormat, kLit, std::size(kLit));
    check(lf.unrepresentableInputMask == (1u << 1) && lf.missingInputMask == 0 && !lf.IsComplete(),
          "E an unrepresentable element format is reported apart from a missing one",
          "missing=" + std::to_string(lf.missingInputMask)
              + " unrepresentable=" + std::to_string(lf.unrepresentableInputMask));

    // ---- E2: a float spelling of an integer input is unrepresentable HERE -----------------------
    // BlendIndices may legally arrive as Byte4 or Vector4 -- a content processor writes either
    // (plans/plan_fx.md FX-127) -- and VULKAN-151 made the skinned shaders take it as `vec4` so
    // that ONE shader serves both. Vector4 binds natively; Byte4 binds through
    // VK_FORMAT_R8G8B8A8_USCALED, an unnormalised integer-to-float conversion. The conversion is
    // offered only where the declaration named the input's own `alternateFormat` and only where
    // the device can carry the format, so both halves are asserted here -- including that a device
    // without it still REPORTS rather than binds.
    {
        constexpr StockProgramInput kWeights{
            VertexElementUsage::BlendWeight, 0, VertexElementFormat::Vector4, "aBoneWeights"};
        constexpr StockProgramInput kIndices{
            VertexElementUsage::BlendIndices, 0, VertexElementFormat::Vector4, "aBoneIndices",
            VertexElementFormat::Byte4};
        constexpr StockProgramInput kSkinned[] = { kPos, kWeights, kIndices };

        const DeclaredVertexLayout byteIndices = Declare(32, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(28, VertexElementFormat::Byte4,   VertexElementUsage::BlendIndices, 0),
        });
        const DeclaredVertexLayout floatIndices = Declare(44, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(28, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0),
        });
        const auto lb = BuildVulkanVertexInputLayoutEXT(byteIndices, kSkinned, std::size(kSkinned),
                                                       /*uscaledVertexFormatSupported=*/true);
        const auto lbNoUscaled = BuildVulkanVertexInputLayoutEXT(byteIndices, kSkinned,
                                                                std::size(kSkinned),
                                                                /*uscaled=*/false);
        const auto lfi = BuildVulkanVertexInputLayoutEXT(floatIndices, kSkinned, std::size(kSkinned),
                                                        /*uscaledVertexFormatSupported=*/true);
        check(lb.IsComplete() && lb.attributes[2].format == VK_FORMAT_R8G8B8A8_USCALED,
              "E2 the Byte4 spelling of BlendIndices binds through the unnormalised conversion",
              Describe(lb));
        check(lfi.IsComplete() && lfi.attributes[2].format == VK_FORMAT_R32G32B32A32_SFLOAT,
              "E2 the Vector4 spelling binds natively, so one shader serves both",
              Describe(lfi));
        check(lb.attributes[2].offset == 28 && lfi.attributes[2].offset == 28,
              "E2 both spellings keep the declaration's own byte offset",
              Describe(lb) + " / " + Describe(lfi));
        check(!lbNoUscaled.IsComplete() && lbNoUscaled.unrepresentableInputMask == (1u << 2)
                  && lbNoUscaled.missingInputMask == 0,
              "E2 a device without the _USCALED format REPORTS the Byte4 spelling rather than "
              "binding it to a float input",
              "missing=" + std::to_string(lbNoUscaled.missingInputMask)
                  + " unrepresentable=" + std::to_string(lbNoUscaled.unrepresentableInputMask));
    }

    // ---- E3: the conversion is not a licence to reinterpret any integer element -----------------
    // It is offered ONLY where the declaration named the input's own alternateFormat. An integer
    // element arriving at a float input the program never sanctioned that spelling for is still
    // unrepresentable, and a float element at an INTEGER input always is -- that direction is a
    // Vulkan usage error and no format converts it.
    {
        constexpr StockProgramInput kUvOnly{
            VertexElementUsage::TextureCoordinate, 0, VertexElementFormat::Vector2, "aUV"};
        constexpr StockProgramInput kProgram[] = { kPos, kUvOnly };
        const DeclaredVertexLayout shortUv = Declare(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Short2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
        const auto ls = BuildVulkanVertexInputLayoutEXT(shortUv, kProgram, std::size(kProgram),
                                                       /*uscaledVertexFormatSupported=*/true);
        check(!ls.IsComplete() && ls.unrepresentableInputMask == (1u << 1),
              "E3 an integer element the program's table does not sanction stays unrepresentable, "
              "even where the device could convert it",
              Describe(ls));
    }

    // ---- F: an empty declaration has no opinion ------------------------------------------------
    const DeclaredVertexLayout none;
    const auto le = BuildVulkanVertexInputLayoutEXT(none, kLit, std::size(kLit));
    check(le.empty && le.attributeCount == 0 && le.Hash() == 0 && !le.IsComplete(),
          "F an empty declaration yields an empty layout, so the caller keeps its stride path",
          Describe(le));

    // The dual-texture case F-15 names by name: a 28-byte record the stride table has no entry for.
    constexpr StockProgramInput kUv1{
        VertexElementUsage::TextureCoordinate, 1, VertexElementFormat::Vector2, "aUV1"};
    constexpr StockProgramInput kDual[] = {kPos, kUv, kUv1};
    const DeclaredVertexLayout dual = Declare(28, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
    });
    const auto ld = BuildVulkanVertexInputLayoutEXT(dual, kDual, std::size(kDual));
    check(ld.IsComplete() && ld.attributeCount == 3
              && ld.attributes[1].offset == 12 && ld.attributes[2].offset == 20,
          "F the 28-byte dual-texture record F-15 names is fully expressible from its declaration",
          Describe(ld));

    std::printf("=== %d/%d PASS ===\n", gPass, gPass + gFail);
    return gFail > 0 ? 1 : 0;
}
