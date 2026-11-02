// SPDX-License-Identifier: MS-PL
// graphics Task 479 (Phase 53): CNA-side reference-value dump tool.
//
// Mirrors tools/fna-reference/{NonRenderingApiReference,PackedVectorReference,
// ViewportReference,ValueEdgeReference}.cs -- same inputs and keys, with retained XNA
// differences left visible. scripts/compare-fna-reference.py compares the real outputs.
// None of these categories need
// a live GraphicsDevice (confirmed while building the FNA-side harness, Tasks 472/473/476), so
// this tool runs standalone with no window/renderer initialization.
//
// Usage: cna_reference_dump [output.json]
// Defaults to writing cna-reference-values.json in the current directory.

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include "JsonWriter.hpp"

#include <cmath>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <cstdio>
#include <fstream>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
using cna_reference::JsonWriter;

namespace
{
    // ---- Enums (mirrors NonRenderingApiReference.cs's Enums list exactly) --------------------

    JsonWriter DumpEnums()
    {
        JsonWriter out;

        JsonWriter blend;
        blend.Add("One", static_cast<int>(Blend::One));
        blend.Add("Zero", static_cast<int>(Blend::Zero));
        blend.Add("SourceColor", static_cast<int>(Blend::SourceColor));
        blend.Add("InverseSourceColor", static_cast<int>(Blend::InverseSourceColor));
        blend.Add("SourceAlpha", static_cast<int>(Blend::SourceAlpha));
        blend.Add("InverseSourceAlpha", static_cast<int>(Blend::InverseSourceAlpha));
        blend.Add("DestinationColor", static_cast<int>(Blend::DestinationColor));
        blend.Add("InverseDestinationColor", static_cast<int>(Blend::InverseDestinationColor));
        blend.Add("DestinationAlpha", static_cast<int>(Blend::DestinationAlpha));
        blend.Add("InverseDestinationAlpha", static_cast<int>(Blend::InverseDestinationAlpha));
        blend.Add("BlendFactor", static_cast<int>(Blend::BlendFactor));
        blend.Add("InverseBlendFactor", static_cast<int>(Blend::InverseBlendFactor));
        blend.Add("SourceAlphaSaturation", static_cast<int>(Blend::SourceAlphaSaturation));
        out.Add("Blend", blend);

        JsonWriter blendFunction;
        blendFunction.Add("Add", static_cast<int>(BlendFunction::Add));
        blendFunction.Add("Subtract", static_cast<int>(BlendFunction::Subtract));
        blendFunction.Add("ReverseSubtract", static_cast<int>(BlendFunction::ReverseSubtract));
        blendFunction.Add("Max", static_cast<int>(BlendFunction::Max));
        blendFunction.Add("Min", static_cast<int>(BlendFunction::Min));
        out.Add("BlendFunction", blendFunction);

        JsonWriter colorWriteChannels;
        colorWriteChannels.Add("None", static_cast<int>(ColorWriteChannels::None));
        colorWriteChannels.Add("Red", static_cast<int>(ColorWriteChannels::Red));
        colorWriteChannels.Add("Green", static_cast<int>(ColorWriteChannels::Green));
        colorWriteChannels.Add("Blue", static_cast<int>(ColorWriteChannels::Blue));
        colorWriteChannels.Add("Alpha", static_cast<int>(ColorWriteChannels::Alpha));
        colorWriteChannels.Add("All", static_cast<int>(ColorWriteChannels::All));
        out.Add("ColorWriteChannels", colorWriteChannels);

        JsonWriter compareFunction;
        compareFunction.Add("Always", static_cast<int>(CompareFunction::Always));
        compareFunction.Add("Never", static_cast<int>(CompareFunction::Never));
        compareFunction.Add("Less", static_cast<int>(CompareFunction::Less));
        compareFunction.Add("LessEqual", static_cast<int>(CompareFunction::LessEqual));
        compareFunction.Add("Equal", static_cast<int>(CompareFunction::Equal));
        compareFunction.Add("GreaterEqual", static_cast<int>(CompareFunction::GreaterEqual));
        compareFunction.Add("Greater", static_cast<int>(CompareFunction::Greater));
        compareFunction.Add("NotEqual", static_cast<int>(CompareFunction::NotEqual));
        out.Add("CompareFunction", compareFunction);

        JsonWriter cullMode;
        cullMode.Add("None", static_cast<int>(CullMode::None));
        cullMode.Add("CullClockwiseFace", static_cast<int>(CullMode::CullClockwiseFace));
        cullMode.Add("CullCounterClockwiseFace", static_cast<int>(CullMode::CullCounterClockwiseFace));
        out.Add("CullMode", cullMode);

        JsonWriter fillMode;
        fillMode.Add("Solid", static_cast<int>(FillMode::Solid));
        fillMode.Add("WireFrame", static_cast<int>(FillMode::WireFrame));
        out.Add("FillMode", fillMode);

        JsonWriter stencilOperation;
        stencilOperation.Add("Keep", static_cast<int>(StencilOperation::Keep));
        stencilOperation.Add("Zero", static_cast<int>(StencilOperation::Zero));
        stencilOperation.Add("Replace", static_cast<int>(StencilOperation::Replace));
        stencilOperation.Add("Increment", static_cast<int>(StencilOperation::Increment));
        stencilOperation.Add("Decrement", static_cast<int>(StencilOperation::Decrement));
        stencilOperation.Add("IncrementSaturation", static_cast<int>(StencilOperation::IncrementSaturation));
        stencilOperation.Add("DecrementSaturation", static_cast<int>(StencilOperation::DecrementSaturation));
        stencilOperation.Add("Invert", static_cast<int>(StencilOperation::Invert));
        out.Add("StencilOperation", stencilOperation);

        JsonWriter textureAddressMode;
        textureAddressMode.Add("Wrap", static_cast<int>(TextureAddressMode::Wrap));
        textureAddressMode.Add("Clamp", static_cast<int>(TextureAddressMode::Clamp));
        textureAddressMode.Add("Mirror", static_cast<int>(TextureAddressMode::Mirror));
        out.Add("TextureAddressMode", textureAddressMode);

        JsonWriter textureFilter;
        textureFilter.Add("Linear", static_cast<int>(TextureFilter::Linear));
        textureFilter.Add("Point", static_cast<int>(TextureFilter::Point));
        textureFilter.Add("Anisotropic", static_cast<int>(TextureFilter::Anisotropic));
        textureFilter.Add("LinearMipPoint", static_cast<int>(TextureFilter::LinearMipPoint));
        textureFilter.Add("PointMipLinear", static_cast<int>(TextureFilter::PointMipLinear));
        textureFilter.Add("MinLinearMagPointMipLinear", static_cast<int>(TextureFilter::MinLinearMagPointMipLinear));
        textureFilter.Add("MinLinearMagPointMipPoint", static_cast<int>(TextureFilter::MinLinearMagPointMipPoint));
        textureFilter.Add("MinPointMagLinearMipLinear", static_cast<int>(TextureFilter::MinPointMagLinearMipLinear));
        textureFilter.Add("MinPointMagLinearMipPoint", static_cast<int>(TextureFilter::MinPointMagLinearMipPoint));
        out.Add("TextureFilter", textureFilter);

        JsonWriter primitiveType;
        primitiveType.Add("TriangleList", static_cast<int>(PrimitiveType::TriangleList));
        primitiveType.Add("TriangleStrip", static_cast<int>(PrimitiveType::TriangleStrip));
        primitiveType.Add("LineList", static_cast<int>(PrimitiveType::LineList));
        primitiveType.Add("LineStrip", static_cast<int>(PrimitiveType::LineStrip));
        // PointListEXT is a CNA/CNAEXT extension, not part of real XNA/FNA -- deliberately
        // included here anyway (extra CNA-only keys are fine; the comparison script only
        // requires every FNA-side key to be present and matching, not the reverse).
        primitiveType.Add("PointListEXT", static_cast<int>(PrimitiveType::PointListEXT));
        out.Add("PrimitiveType", primitiveType);

        JsonWriter surfaceFormat;
        surfaceFormat.Add("Color", static_cast<int>(SurfaceFormat::Color));
        surfaceFormat.Add("Bgr565", static_cast<int>(SurfaceFormat::Bgr565));
        surfaceFormat.Add("Bgra5551", static_cast<int>(SurfaceFormat::Bgra5551));
        surfaceFormat.Add("Bgra4444", static_cast<int>(SurfaceFormat::Bgra4444));
        surfaceFormat.Add("Dxt1", static_cast<int>(SurfaceFormat::Dxt1));
        surfaceFormat.Add("Dxt3", static_cast<int>(SurfaceFormat::Dxt3));
        surfaceFormat.Add("Dxt5", static_cast<int>(SurfaceFormat::Dxt5));
        surfaceFormat.Add("NormalizedByte2", static_cast<int>(SurfaceFormat::NormalizedByte2));
        surfaceFormat.Add("NormalizedByte4", static_cast<int>(SurfaceFormat::NormalizedByte4));
        surfaceFormat.Add("Rgba1010102", static_cast<int>(SurfaceFormat::Rgba1010102));
        surfaceFormat.Add("Rg32", static_cast<int>(SurfaceFormat::Rg32));
        surfaceFormat.Add("Rgba64", static_cast<int>(SurfaceFormat::Rgba64));
        surfaceFormat.Add("Alpha8", static_cast<int>(SurfaceFormat::Alpha8));
        surfaceFormat.Add("Single", static_cast<int>(SurfaceFormat::Single));
        surfaceFormat.Add("Vector2", static_cast<int>(SurfaceFormat::Vector2));
        surfaceFormat.Add("Vector4", static_cast<int>(SurfaceFormat::Vector4));
        surfaceFormat.Add("HalfSingle", static_cast<int>(SurfaceFormat::HalfSingle));
        surfaceFormat.Add("HalfVector2", static_cast<int>(SurfaceFormat::HalfVector2));
        surfaceFormat.Add("HalfVector4", static_cast<int>(SurfaceFormat::HalfVector4));
        surfaceFormat.Add("HdrBlendable", static_cast<int>(SurfaceFormat::HdrBlendable));
        // These 6 -EXT-suffixed CNA names match real members added to FNA's own SurfaceFormat
        // enum in newer FNA releases (confirmed against the locally-built FNA.dll, version
        // 26.5.0.0) -- not CNA-only extensions, despite the EXT naming convention CNA otherwise
        // reserves for non-XNA additions.
        surfaceFormat.Add("ColorBgraEXT", static_cast<int>(SurfaceFormat::ColorBgraEXT));
        surfaceFormat.Add("ColorSrgbEXT", static_cast<int>(SurfaceFormat::ColorSrgbEXT));
        surfaceFormat.Add("Dxt5SrgbEXT", static_cast<int>(SurfaceFormat::Dxt5SrgbEXT));
        surfaceFormat.Add("Bc7EXT", static_cast<int>(SurfaceFormat::Bc7EXT));
        surfaceFormat.Add("Bc7SrgbEXT", static_cast<int>(SurfaceFormat::Bc7SrgbEXT));
        surfaceFormat.Add("ByteEXT", static_cast<int>(SurfaceFormat::ByteEXT));
        surfaceFormat.Add("UShortEXT", static_cast<int>(SurfaceFormat::UShortEXT));
        out.Add("SurfaceFormat", surfaceFormat);

        JsonWriter clearOptions;
        clearOptions.Add("Target", static_cast<int>(ClearOptions::Target));
        clearOptions.Add("DepthBuffer", static_cast<int>(ClearOptions::DepthBuffer));
        clearOptions.Add("Stencil", static_cast<int>(ClearOptions::Stencil));
        out.Add("ClearOptions", clearOptions);

        JsonWriter bufferUsage;
        bufferUsage.Add("None", static_cast<int>(BufferUsage::None));
        bufferUsage.Add("WriteOnly", static_cast<int>(BufferUsage::WriteOnly));
        out.Add("BufferUsage", bufferUsage);

        JsonWriter setDataOptions;
        setDataOptions.Add("None", static_cast<int>(SetDataOptions::None));
        setDataOptions.Add("Discard", static_cast<int>(SetDataOptions::Discard));
        setDataOptions.Add("NoOverwrite", static_cast<int>(SetDataOptions::NoOverwrite));
        out.Add("SetDataOptions", setDataOptions);

        JsonWriter indexElementSize;
        indexElementSize.Add("SixteenBits", static_cast<int>(IndexElementSize::SixteenBits));
        indexElementSize.Add("ThirtyTwoBits", static_cast<int>(IndexElementSize::ThirtyTwoBits));
        out.Add("IndexElementSize", indexElementSize);

        JsonWriter renderTargetUsage;
        renderTargetUsage.Add("DiscardContents", static_cast<int>(RenderTargetUsage::DiscardContents));
        renderTargetUsage.Add("PreserveContents", static_cast<int>(RenderTargetUsage::PreserveContents));
        renderTargetUsage.Add("PlatformContents", static_cast<int>(RenderTargetUsage::PlatformContents));
        out.Add("RenderTargetUsage", renderTargetUsage);

        JsonWriter presentInterval;
        presentInterval.Add("Default", static_cast<int>(PresentInterval::Default));
        presentInterval.Add("One", static_cast<int>(PresentInterval::One));
        presentInterval.Add("Two", static_cast<int>(PresentInterval::Two));
        presentInterval.Add("Immediate", static_cast<int>(PresentInterval::Immediate));
        out.Add("PresentInterval", presentInterval);

        JsonWriter graphicsProfile;
        graphicsProfile.Add("Reach", static_cast<int>(GraphicsProfile::Reach));
        graphicsProfile.Add("HiDef", static_cast<int>(GraphicsProfile::HiDef));
        out.Add("GraphicsProfile", graphicsProfile);

        JsonWriter depthFormat;
        depthFormat.Add("None", static_cast<int>(DepthFormat::None));
        depthFormat.Add("Depth16", static_cast<int>(DepthFormat::Depth16));
        depthFormat.Add("Depth24", static_cast<int>(DepthFormat::Depth24));
        depthFormat.Add("Depth24Stencil8", static_cast<int>(DepthFormat::Depth24Stencil8));
        out.Add("DepthFormat", depthFormat);

        JsonWriter spriteSortMode;
        spriteSortMode.Add("Deferred", static_cast<int>(SpriteSortMode::Deferred));
        spriteSortMode.Add("Immediate", static_cast<int>(SpriteSortMode::Immediate));
        spriteSortMode.Add("Texture", static_cast<int>(SpriteSortMode::Texture));
        spriteSortMode.Add("BackToFront", static_cast<int>(SpriteSortMode::BackToFront));
        spriteSortMode.Add("FrontToBack", static_cast<int>(SpriteSortMode::FrontToBack));
        out.Add("SpriteSortMode", spriteSortMode);

        JsonWriter spriteEffects;
        spriteEffects.Add("None", static_cast<int>(SpriteEffects::None));
        spriteEffects.Add("FlipHorizontally", static_cast<int>(SpriteEffects::FlipHorizontally));
        spriteEffects.Add("FlipVertically", static_cast<int>(SpriteEffects::FlipVertically));
        out.Add("SpriteEffects", spriteEffects);

        return out;
    }

    // ---- State presets (mirrors NonRenderingApiReference.cs's DumpProperties calls) ----------

    JsonWriter DumpBlendState(const BlendState& s)
    {
        JsonWriter w;
        w.Add("AlphaBlendFunction", static_cast<int>(s.getAlphaBlendFunctionProperty()));
        w.Add("AlphaDestinationBlend", static_cast<int>(s.getAlphaDestinationBlendProperty()));
        w.Add("AlphaSourceBlend", static_cast<int>(s.getAlphaSourceBlendProperty()));
        w.Add("ColorBlendFunction", static_cast<int>(s.getColorBlendFunctionProperty()));
        w.Add("ColorDestinationBlend", static_cast<int>(s.getColorDestinationBlendProperty()));
        w.Add("ColorSourceBlend", static_cast<int>(s.getColorSourceBlendProperty()));
        w.Add("ColorWriteChannels", static_cast<int>(s.getColorWriteChannelsProperty()));
        w.Add("ColorWriteChannels1", static_cast<int>(s.getColorWriteChannels1Property()));
        w.Add("ColorWriteChannels2", static_cast<int>(s.getColorWriteChannels2Property()));
        w.Add("ColorWriteChannels3", static_cast<int>(s.getColorWriteChannels3Property()));
        w.Add("BlendFactor", s.getBlendFactorProperty().ToString());
        w.Add("MultiSampleMask", s.getMultiSampleMaskProperty());
        w.Add("IsDisposed", s.getIsDisposedProperty());
        w.Add("Name", s.getNameProperty());
        return w;
    }

    JsonWriter DumpDepthStencilState(const DepthStencilState& s)
    {
        JsonWriter w;
        w.Add("DepthBufferEnable", s.getDepthBufferEnableProperty());
        w.Add("DepthBufferWriteEnable", s.getDepthBufferWriteEnableProperty());
        w.Add("DepthBufferFunction", static_cast<int>(s.getDepthBufferFunctionProperty()));
        w.Add("StencilEnable", s.getStencilEnableProperty());
        w.Add("StencilFunction", static_cast<int>(s.getStencilFunctionProperty()));
        w.Add("StencilMask", s.getStencilMaskProperty());
        w.Add("StencilWriteMask", s.getStencilWriteMaskProperty());
        w.Add("ReferenceStencil", s.getReferenceStencilProperty());
        w.Add("StencilFail", static_cast<int>(s.getStencilFailProperty()));
        w.Add("StencilDepthBufferFail", static_cast<int>(s.getStencilDepthBufferFailProperty()));
        w.Add("StencilPass", static_cast<int>(s.getStencilPassProperty()));
        w.Add("TwoSidedStencilMode", s.getTwoSidedStencilModeProperty());
        w.Add("CounterClockwiseStencilFunction", static_cast<int>(s.getCounterClockwiseStencilFunctionProperty()));
        w.Add("CounterClockwiseStencilFail", static_cast<int>(s.getCounterClockwiseStencilFailProperty()));
        w.Add("CounterClockwiseStencilDepthBufferFail", static_cast<int>(s.getCounterClockwiseStencilDepthBufferFailProperty()));
        w.Add("CounterClockwiseStencilPass", static_cast<int>(s.getCounterClockwiseStencilPassProperty()));
        w.Add("IsDisposed", s.getIsDisposedProperty());
        w.Add("Name", s.getNameProperty());
        return w;
    }

    JsonWriter DumpRasterizerState(const RasterizerState& s)
    {
        JsonWriter w;
        w.Add("CullMode", static_cast<int>(s.getCullModeProperty()));
        w.Add("DepthBias", static_cast<double>(s.getDepthBiasProperty()));
        w.Add("FillMode", static_cast<int>(s.getFillModeProperty()));
        w.Add("MultiSampleAntiAlias", s.getMultiSampleAntiAliasProperty());
        w.Add("ScissorTestEnable", s.getScissorTestEnableProperty());
        w.Add("SlopeScaleDepthBias", static_cast<double>(s.getSlopeScaleDepthBiasProperty()));
        w.Add("IsDisposed", s.getIsDisposedProperty());
        w.Add("Name", s.getNameProperty());
        return w;
    }

    JsonWriter DumpSamplerState(const SamplerState& s)
    {
        JsonWriter w;
        w.Add("AddressU", static_cast<int>(s.getAddressUProperty()));
        w.Add("AddressV", static_cast<int>(s.getAddressVProperty()));
        w.Add("AddressW", static_cast<int>(s.getAddressWProperty()));
        w.Add("Filter", static_cast<int>(s.getFilterProperty()));
        w.Add("MaxAnisotropy", s.getMaxAnisotropyProperty());
        w.Add("MaxMipLevel", s.getMaxMipLevelProperty());
        w.Add("MipMapLevelOfDetailBias", static_cast<double>(s.getMipMapLevelOfDetailBiasProperty()));
        w.Add("IsDisposed", s.getIsDisposedProperty());
        w.Add("Name", s.getNameProperty());
        return w;
    }

    JsonWriter DumpStatePresets()
    {
        JsonWriter w;
        w.Add("BlendState_Additive", DumpBlendState(BlendState::Additive));
        w.Add("BlendState_AlphaBlend", DumpBlendState(BlendState::AlphaBlend));
        w.Add("BlendState_NonPremultiplied", DumpBlendState(BlendState::NonPremultiplied));
        w.Add("BlendState_Opaque", DumpBlendState(BlendState::Opaque));
        w.Add("DepthStencilState_Default", DumpDepthStencilState(DepthStencilState::Default));
        w.Add("DepthStencilState_DepthRead", DumpDepthStencilState(DepthStencilState::DepthRead));
        w.Add("DepthStencilState_None", DumpDepthStencilState(DepthStencilState::None));
        w.Add("RasterizerState_CullClockwise", DumpRasterizerState(RasterizerState::CullClockwise));
        w.Add("RasterizerState_CullCounterClockwise", DumpRasterizerState(RasterizerState::CullCounterClockwise));
        w.Add("RasterizerState_CullNone", DumpRasterizerState(RasterizerState::CullNone));
        w.Add("SamplerState_AnisotropicClamp", DumpSamplerState(SamplerState::AnisotropicClamp));
        w.Add("SamplerState_AnisotropicWrap", DumpSamplerState(SamplerState::AnisotropicWrap));
        w.Add("SamplerState_LinearClamp", DumpSamplerState(SamplerState::LinearClamp));
        w.Add("SamplerState_LinearWrap", DumpSamplerState(SamplerState::LinearWrap));
        w.Add("SamplerState_PointClamp", DumpSamplerState(SamplerState::PointClamp));
        w.Add("SamplerState_PointWrap", DumpSamplerState(SamplerState::PointWrap));
        return w;
    }

    // ---- PackedVector (mirrors PackedVectorReference.cs's exact input values) ----------------

    std::string Label(std::initializer_list<float> values)
    {
        std::string s = "(";
        bool first = true;
        for (float v : values)
        {
            if (!first) s += ",";
            first = false;
            // Match C#'s float.ToString(InvariantCulture): integral values print without a
            // trailing ".0" (e.g. "1" not "1.0"), matching Label()'s own C# output exactly.
            if (v == static_cast<long long>(v))
            {
                s += std::to_string(static_cast<long long>(v));
            }
            else
            {
                std::ostringstream oss;
                oss << v;
                s += oss.str();
            }
        }
        return s + ")";
    }

    std::string Fixed2(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
        return buf;
    }

    std::string HexU64(std::uint64_t v, int width)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%0*llx", width, static_cast<unsigned long long>(v));
        return buf;
    }

    JsonWriter DumpPackedVector()
    {
        JsonWriter out;

        {
            JsonWriter cases;
            for (float a : {0.00f, 0.25f, 0.50f, 1.00f})
            {
                Alpha8 v(a);
                cases.Add(Fixed2(a), JsonWriter().Add("packed", static_cast<int>(v.getPackedValueProperty())));
            }
            out.Add("Alpha8", cases);
        }
        {
            JsonWriter cases;
            float inputs[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5f, 0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                Bgr565 v(c[0], c[1], c[2]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2]}), JsonWriter()
                    .Add("packed", static_cast<int>(v.getPackedValueProperty()))
                    .Add("ToVector3_X", static_cast<double>(u.X))
                    .Add("ToVector3_Y", static_cast<double>(u.Y))
                    .Add("ToVector3_Z", static_cast<double>(u.Z)));
            }
            out.Add("Bgr565", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0, 1, 0, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                Bgra4444 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", static_cast<int>(v.getPackedValueProperty()))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("Bgra4444", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0, 0, 1, 0}, {0.5f, 0.5f, 0.5f, 1}};
            for (auto& c : inputs)
            {
                Bgra5551 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", static_cast<int>(v.getPackedValueProperty()))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("Bgra5551", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {100, 150, 200, 128}};
            for (auto& c : inputs)
            {
                Byte4 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("Byte4", cases);
        }
        {
            JsonWriter cases;
            for (float f : {0.0f, 1.0f, -1.0f, 0.5f, 0.25f})
            {
                HalfSingle v(f);
                cases.Add(Fixed2(f), JsonWriter()
                    .Add("packed", static_cast<int>(v.getPackedValueProperty()))
                    .Add("ToSingle", static_cast<double>(v.ToSingle())));
            }
            out.Add("HalfSingle", cases);
        }
        {
            JsonWriter cases;
            float inputs[][2] = {{1, 0}, {0, 1}, {0.5f, 0.5f}, {-1, -1}};
            for (auto& c : inputs)
            {
                HalfVector2 v(c[0], c[1]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector2_X", static_cast<double>(u.X))
                    .Add("ToVector2_Y", static_cast<double>(u.Y)));
            }
            out.Add("HalfVector2", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0.5f, 0.5f, 0.5f, 0.5f}, {-1, -1, -1, -1}};
            for (auto& c : inputs)
            {
                HalfVector4 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", HexU64(v.getPackedValueProperty(), 16))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("HalfVector4", cases);
        }
        {
            JsonWriter cases;
            float inputs[][2] = {{1, 0}, {0, 1}, {-1, -1}, {0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                NormalizedByte2 v(c[0], c[1]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1]}), JsonWriter()
                    .Add("packed", static_cast<int>(v.getPackedValueProperty()))
                    .Add("ToVector2_X", static_cast<double>(u.X))
                    .Add("ToVector2_Y", static_cast<double>(u.Y)));
            }
            out.Add("NormalizedByte2", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0.5f, 0.5f, 0.5f, 0.5f}, {-1, -1, -1, -1}};
            for (auto& c : inputs)
            {
                NormalizedByte4 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("NormalizedByte4", cases);
        }
        {
            JsonWriter cases;
            float inputs[][2] = {{1, 0}, {0, 1}, {-1, -1}, {0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                NormalizedShort2 v(c[0], c[1]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector2_X", static_cast<double>(u.X))
                    .Add("ToVector2_Y", static_cast<double>(u.Y)));
            }
            out.Add("NormalizedShort2", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0.5f, 0.5f, 0.5f, 0.5f}, {-1, -1, -1, -1}};
            for (auto& c : inputs)
            {
                NormalizedShort4 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", HexU64(v.getPackedValueProperty(), 16))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("NormalizedShort4", cases);
        }
        {
            JsonWriter cases;
            float inputs[][2] = {{1, 0}, {0, 1}, {0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                Rg32 v(c[0], c[1]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector2_X", static_cast<double>(u.X))
                    .Add("ToVector2_Y", static_cast<double>(u.Y)));
            }
            out.Add("Rg32", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0, 1, 0, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                Rgba1010102 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("Rgba1010102", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{1, 0, 0, 1}, {0, 1, 0, 0.5f}, {0.5f, 0.5f, 0.5f, 0.5f}};
            for (auto& c : inputs)
            {
                Rgba64 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", HexU64(v.getPackedValueProperty(), 16))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("Rgba64", cases);
        }
        {
            JsonWriter cases;
            float inputs[][2] = {{32767, 0}, {0, 32767}, {-32768, -32768}, {100, 200}};
            for (auto& c : inputs)
            {
                Short2 v(c[0], c[1]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1]}), JsonWriter()
                    .Add("packed", static_cast<double>(v.getPackedValueProperty()))
                    .Add("ToVector2_X", static_cast<double>(u.X))
                    .Add("ToVector2_Y", static_cast<double>(u.Y)));
            }
            out.Add("Short2", cases);
        }
        {
            JsonWriter cases;
            float inputs[][4] = {{32767, 0, 0, 32767}, {0, 32767, -32768, 0}, {100, 200, 300, 400}};
            for (auto& c : inputs)
            {
                Short4 v(c[0], c[1], c[2], c[3]);
                Vector4 u = v.ToVector4();
                cases.Add(Label({c[0], c[1], c[2], c[3]}), JsonWriter()
                    .Add("packed", HexU64(v.getPackedValueProperty(), 16))
                    .Add("ToVector4_X", static_cast<double>(u.X))
                    .Add("ToVector4_Y", static_cast<double>(u.Y))
                    .Add("ToVector4_Z", static_cast<double>(u.Z))
                    .Add("ToVector4_W", static_cast<double>(u.W)));
            }
            out.Add("Short4", cases);
        }

        return out;
    }

    // ---- Viewport (mirrors ViewportReference.cs's exact cases) --------------------------------

    void AddCameraCase(JsonWriter& cases, const Viewport& viewport, Vector3 source,
                        const Matrix& projection, const Matrix& view, const Matrix& world,
                        const std::string& label)
    {
        Vector3 projected = viewport.Project(source, projection, view, world);
        Vector3 roundTrip = viewport.Unproject(projected, projection, view, world);

        double dx = static_cast<double>(roundTrip.X) - static_cast<double>(source.X);
        double dy = static_cast<double>(roundTrip.Y) - static_cast<double>(source.Y);
        double dz = static_cast<double>(roundTrip.Z) - static_cast<double>(source.Z);
        double roundTripError = std::sqrt(dx * dx + dy * dy + dz * dz);

        cases.Add(label, JsonWriter()
            .Add("source_X", static_cast<double>(source.X))
            .Add("source_Y", static_cast<double>(source.Y))
            .Add("source_Z", static_cast<double>(source.Z))
            .Add("Project_X", static_cast<double>(projected.X))
            .Add("Project_Y", static_cast<double>(projected.Y))
            .Add("Project_Z", static_cast<double>(projected.Z))
            .Add("Unproject_X", static_cast<double>(roundTrip.X))
            .Add("Unproject_Y", static_cast<double>(roundTrip.Y))
            .Add("Unproject_Z", static_cast<double>(roundTrip.Z))
            .Add("roundTripError", roundTripError));
    }

    JsonWriter DumpViewport()
    {
        JsonWriter cases;
        Viewport viewport(0, 0, 800, 480);
        Matrix identity = Matrix::getIdentityProperty();

        AddCameraCase(cases, viewport, Vector3(0, 0, 0), identity, identity, identity, "identity_origin");
        AddCameraCase(cases, viewport, Vector3(1, 1, 0), identity, identity, identity, "identity_topRight");
        AddCameraCase(cases, viewport, Vector3(-1, -1, 0), identity, identity, identity, "identity_bottomLeft");

        Matrix projection = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 800.0f / 480.0f, 0.1f, 100.0f);
        Matrix view = Matrix::CreateLookAt(Vector3(0, 0, 5), Vector3(0, 0, 0), Vector3(0, 1, 0));
        Matrix world = identity;

        AddCameraCase(cases, viewport, Vector3(0, 0, 0), projection, view, world, "camera_worldOrigin");
        AddCameraCase(cases, viewport, Vector3(1, 1, 0), projection, view, world, "camera_offAxis");

        return cases;
    }
    constexpr std::uint32_t EdgeSeed = 0xC0FFEE03u;

    float NextEdgeValue(std::uint32_t& state)
    {
        state = state * 1664525u + 1013904223u;
        return (static_cast<int>((state >> 16) % 257) - 128) / 8.0f;
    }

    void EdgeScalar(JsonWriter& output, const std::string& key, float value)
    {
        if (std::isnan(value)) output.Add(key, std::string("nan"));
        else if (std::isinf(value)) output.Add(key, std::string(value > 0 ? "+inf" : "-inf"));
        else if (value == 0) output.Add(key, std::string(std::signbit(value) ? "-zero" : "+zero"));
        else output.Add(key, static_cast<double>(value));
    }

    JsonWriter EdgeVector(const Vector2& value)
    {
        JsonWriter output;
        EdgeScalar(output, "X", value.X);
        EdgeScalar(output, "Y", value.Y);
        return output;
    }

    JsonWriter EdgeVectorCase(Vector2 value, Vector2 divisor)
    {
        const Vector2 min(-2, -2), max(2, 2);
        JsonWriter output;
        output.Add("input", EdgeVector(value)).Add("divisor", EdgeVector(divisor));
        output.Add("divide", EdgeVector(Vector2::Divide(value, divisor)));
        Vector2 result;
        Vector2::Divide(value, divisor, result);
        output.Add("divideOut", EdgeVector(result));
        output.Add("divideOperator", EdgeVector(value / divisor));
        output.Add("scalarDivide", EdgeVector(Vector2::Divide(value, divisor.X)));
        Vector2::Divide(value, divisor.X, result);
        output.Add("scalarDivideOut", EdgeVector(result));
        output.Add("scalarDivideOperator", EdgeVector(value / divisor.X));
        output.Add("clamp", EdgeVector(Vector2::Clamp(value, min, max)));
        Vector2::Clamp(value, min, max, result);
        output.Add("clampOut", EdgeVector(result));
        output.Add("reversedClamp", EdgeVector(Vector2::Clamp(value, max, min)));
        Vector2::Clamp(value, max, min, result);
        output.Add("reversedClampOut", EdgeVector(result));
        const Vector2 copy(value.X, value.Y);
        output.Add("equalsCopy", value.Equals(copy)).Add("operatorEqualCopy", value == copy)
            .Add("operatorUnequalCopy", value != copy)
            .Add("copyHashEqual", value.GetHashCode() == copy.GetHashCode());
        return output;
    }

    JsonWriter DumpVector2Edges()
    {
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float maximum = std::numeric_limits<float>::max();
        const float tiny = std::numeric_limits<float>::denorm_min();
        JsonWriter cases;
        cases.Add("zero", EdgeVectorCase(Vector2::Zero, Vector2(0, -0.0f)))
            .Add("signedZero", EdgeVectorCase(Vector2(-0.0f, 0), Vector2(2, -2)))
            .Add("boundaries", EdgeVectorCase(Vector2(-2, 2), Vector2(-1, 1)))
            .Add("outside", EdgeVectorCase(Vector2(-2.125f, 2.125f), Vector2(0, -0.0f)))
            .Add("infinity", EdgeVectorCase(Vector2(inf, -inf), Vector2(2, -2)))
            .Add("nan", EdgeVectorCase(Vector2(nan, 1), Vector2(1, nan)))
            .Add("max", EdgeVectorCase(Vector2(maximum, -maximum), Vector2(0.5f, 2)))
            .Add("subnormal", EdgeVectorCase(Vector2(tiny, -tiny), Vector2(2, 2)));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 8; ++i)
        {
            // Evaluate in source order: C++ function-argument order is unspecified.
            const float x = NextEdgeValue(state), y = NextEdgeValue(state);
            const float dx = NextEdgeValue(state), dy = NextEdgeValue(state);
            cases.Add("seeded" + std::to_string(i), EdgeVectorCase(Vector2(x, y), Vector2(dx, dy)));
        }
        JsonWriter magnitudes;
        const Vector2 inputs[] = {Vector2::Zero, Vector2(3, 4), Vector2(1e20f, 0),
            Vector2(1e-30f, 0), Vector2(inf, 0), Vector2(nan, 0)};
        const char* names[] = {"zero", "threeFour", "large", "tiny", "infinity", "nan"};
        for (int i = 0; i < 6; ++i)
        {
            Vector2 value = inputs[i], result;
            JsonWriter item;
            item.Add("input", EdgeVector(value));
            EdgeScalar(item, "length", value.Length());
            EdgeScalar(item, "lengthSquared", value.LengthSquared());
            item.Add("normalize", EdgeVector(Vector2::Normalize(value)));
            Vector2::Normalize(value, result);
            item.Add("normalizeOut", EdgeVector(result));
            value.Normalize();
            item.Add("normalizeInstance", EdgeVector(value));
            magnitudes.Add(names[i], item);
        }
        const Vector2 a(1.5f, -2.25f), b(1.5f, 2.25f);
        JsonWriter output;
        output.Add("seed", static_cast<double>(EdgeSeed)).Add("arithmetic", cases)
            .Add("magnitude", magnitudes).Add("format", a.ToString())
            .Add("unequalEquals", a.Equals(b)).Add("unequalOperator", a == b).Add("unequalNotOperator", a != b)
            .Add("signedZeroEquals", Vector2(-0.0f, 0).Equals(Vector2::Zero))
            .Add("signedZeroHashEqual", Vector2(-0.0f, 0).GetHashCode() == Vector2::Zero.GetHashCode());
        return output;
    }

    JsonWriter EdgeMatrixFields(const Matrix& matrix)
    {
        JsonWriter output;
        const float values[] = {matrix.M11, matrix.M12, matrix.M13, matrix.M14,
            matrix.M21, matrix.M22, matrix.M23, matrix.M24, matrix.M31, matrix.M32, matrix.M33, matrix.M34,
            matrix.M41, matrix.M42, matrix.M43, matrix.M44};
        for (int i = 0; i < 16; ++i)
            EdgeScalar(output, "M" + std::to_string(i / 4 + 1) + std::to_string(i % 4 + 1), values[i]);
        return output;
    }

    void EdgeProject(int kind, float a, float b, float near, float far, Matrix& result)
    {
        if (kind == 0) Matrix::CreatePerspective(a, b, near, far, result);
        else if (kind == 1) Matrix::CreatePerspectiveFieldOfView(a, b, near, far, result);
        else Matrix::CreatePerspectiveOffCenter(-a, a, -b, b, near, far, result);
    }

    Matrix EdgeProject(int kind, float a, float b, float near, float far)
    {
        if (kind == 0) return Matrix::CreatePerspective(a, b, near, far);
        if (kind == 1) return Matrix::CreatePerspectiveFieldOfView(a, b, near, far);
        return Matrix::CreatePerspectiveOffCenter(-a, a, -b, b, near, far);
    }

    JsonWriter EdgeProjectionCase(int kind, float a, float b, float near, float far)
    {
        JsonWriter output;
        EdgeScalar(output, "a", a); EdgeScalar(output, "b", b);
        EdgeScalar(output, "near", near); EdgeScalar(output, "far", far);
        try
        {
            output.Add("value", EdgeMatrixFields(EdgeProject(kind, a, b, near, far)))
                .Add("valueException", std::string("none"));
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            output.Add("valueException", std::string("ArgumentOutOfRangeException"));
        }
        catch (const System::ArgumentException&)
        {
            output.Add("valueException", std::string("ArgumentException"));
        }
        Matrix result = Matrix::CreateTranslation(7, 8, 9);
        try
        {
            EdgeProject(kind, a, b, near, far, result);
            output.Add("outException", std::string("none"));
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            output.Add("outException", std::string("ArgumentOutOfRangeException"));
        }
        catch (const System::ArgumentException&)
        {
            output.Add("outException", std::string("ArgumentException"));
        }
        output.Add("out", EdgeMatrixFields(result));
        return output;
    }

    JsonWriter DumpMatrixPerspectiveEdges()
    {
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        JsonWriter output;
        output.Add("seed", static_cast<double>(EdgeSeed));
        const char* kinds[] = {"size", "fov", "offCenter"};
        for (int kind = 0; kind < 3; ++kind)
        {
            JsonWriter cases;
            cases.Add("ordinary", EdgeProjectionCase(kind, 1, 2, 1, 2))
                .Add("zeroDimension", EdgeProjectionCase(kind, 0, 0, 1, 2))
                .Add("negativeDimension", EdgeProjectionCase(kind, -1, -2, 1, 2))
                .Add("nearZero", EdgeProjectionCase(kind, 1, 2, 0, 2))
                .Add("farZero", EdgeProjectionCase(kind, 1, 2, 1, 0))
                .Add("equalDepth", EdgeProjectionCase(kind, 1, 2, 2, 2))
                .Add("reversedDepth", EdgeProjectionCase(kind, 1, 2, 3, 2))
                .Add("bothZero", EdgeProjectionCase(kind, 1, 2, 0, 0))
                .Add("nanNear", EdgeProjectionCase(kind, 1, 2, nan, 2))
                .Add("nanFar", EdgeProjectionCase(kind, 1, 2, 1, nan))
                .Add("infiniteFar", EdgeProjectionCase(kind, 1, 2, 1, inf))
                .Add("nanDimension", EdgeProjectionCase(kind, nan, 2, 1, 2));
            std::uint32_t state = EdgeSeed;
            for (int i = 0; i < 4; ++i)
            {
                const float near = std::abs(NextEdgeValue(state)) + 1;
                const float far = near + std::abs(NextEdgeValue(state)) + 1;
                cases.Add("seeded" + std::to_string(i), EdgeProjectionCase(kind, 1, 2, near, far));
            }
            if (kind == 1)
            {
                cases.Add("pi", EdgeProjectionCase(kind, MathHelper::Pi, 1, 1, 2));
                cases.Add("belowPi", EdgeProjectionCase(kind, std::nextafter(MathHelper::Pi, 0.0f), 1, 1, 2));
                cases.Add("fnaPi", EdgeProjectionCase(kind, 3.141593f, 1, 1, 2));
                cases.Add("infiniteAngle", EdgeProjectionCase(kind, inf, 1, 1, 2));
            }
            output.Add(kinds[kind], cases);
        }
        return output;
    }
    JsonWriter EdgeVector(Vector3 value)
    {
        auto output = JsonWriter();
        EdgeScalar(output, "X", value.X);
        EdgeScalar(output, "Y", value.Y);
        EdgeScalar(output, "Z", value.Z);
        return output;
    }

    JsonWriter EdgeVectorCase(Vector3 value, Vector3 divisor)
    {
        auto min = Vector3(-2, -2, -2);
        auto max = Vector3(2, 2, 2);
        auto output = JsonWriter().Add("input", EdgeVector(value)).Add("divisor", EdgeVector(divisor));
        output.Add("divide", EdgeVector(Vector3::Divide(value, divisor)));
        Vector3 result;
        Vector3::Divide(value, divisor, result);
        output.Add("divideOut", EdgeVector(result));
        output.Add("divideOperator", EdgeVector(value / divisor));
        output.Add("scalarDivide", EdgeVector(Vector3::Divide(value, divisor.X)));
        Vector3::Divide(value, divisor.X, result);
        output.Add("scalarDivideOut", EdgeVector(result));
        output.Add("scalarDivideOperator", EdgeVector(value / divisor.X));
        output.Add("clamp", EdgeVector(Vector3::Clamp(value, min, max)));
        Vector3::Clamp(value, min, max, result);
        output.Add("clampOut", EdgeVector(result));
        output.Add("reversedClamp", EdgeVector(Vector3::Clamp(value, max, min)));
        Vector3::Clamp(value, max, min, result);
        output.Add("reversedClampOut", EdgeVector(result));
        auto copy = Vector3(value.X, value.Y, value.Z);
        output.Add("equalsCopy", value.Equals(copy)).Add("operatorEqualCopy", value == copy)
            .Add("operatorUnequalCopy", value != copy)
            .Add("copyHashEqual", value.GetHashCode() == copy.GetHashCode());
        return output;
    }

    JsonWriter EdgeSpatialCase(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float amount)
    {
        auto output = JsonWriter().Add("a", EdgeVector(a)).Add("b", EdgeVector(b))
            .Add("c", EdgeVector(c)).Add("d", EdgeVector(d));
        EdgeScalar(output, "amount", amount);
        Vector3 result;
        output.Add("lerp", EdgeVector(Vector3::Lerp(a, b, amount)));
        Vector3::Lerp(a, b, amount, result);
        output.Add("lerpOut", EdgeVector(result));
        output.Add("smoothStep", EdgeVector(Vector3::SmoothStep(a, b, amount)));
        Vector3::SmoothStep(a, b, amount, result);
        output.Add("smoothStepOut", EdgeVector(result));
        output.Add("hermite", EdgeVector(Vector3::Hermite(a, b, c, d, amount)));
        Vector3::Hermite(a, b, c, d, amount, result);
        output.Add("hermiteOut", EdgeVector(result));
        output.Add("catmullRom", EdgeVector(Vector3::CatmullRom(a, b, c, d, amount)));
        Vector3::CatmullRom(a, b, c, d, amount, result);
        output.Add("catmullRomOut", EdgeVector(result));
        output.Add("barycentric", EdgeVector(Vector3::Barycentric(a, b, c, amount, 0.25f)));
        Vector3::Barycentric(a, b, c, amount, 0.25f, result);
        output.Add("barycentricOut", EdgeVector(result));
        output.Add("cross", EdgeVector(Vector3::Cross(a, b)));
        Vector3::Cross(a, b, result);
        output.Add("crossOut", EdgeVector(result));
        output.Add("reflect", EdgeVector(Vector3::Reflect(a, b)));
        Vector3::Reflect(a, b, result);
        output.Add("reflectOut", EdgeVector(result));
        auto matrix = Matrix(2, 0, 1, 0.5f, 0, -3, 0, 0, 1, 0, 4, 0, 7, 8, 9, 1);
        auto rotation = Quaternion(0.25f, -0.5f, 0.75f, 1);
        output.Add("transform", EdgeVector(Vector3::Transform(a, matrix)));
        Vector3::Transform(a, matrix, result);
        output.Add("transformOut", EdgeVector(result));
        output.Add("rotate", EdgeVector(Vector3::Transform(a, rotation)));
        Vector3::Transform(a, rotation, result);
        output.Add("rotateOut", EdgeVector(result));
        output.Add("normal", EdgeVector(Vector3::TransformNormal(a, matrix)));
        Vector3::TransformNormal(a, matrix, result);
        output.Add("normalOut", EdgeVector(result));
        return output;
    }

    JsonWriter DumpVector3Edges()
    {
        auto cases = JsonWriter();
        cases.Add("zero", EdgeVectorCase(Vector3(0, 0, 0), Vector3(0, -0.0f, 2)));
        cases.Add("signedZero", EdgeVectorCase(Vector3(-0.0f, 0, -0.0f), Vector3(2, -2, -2)));
        cases.Add("boundaries", EdgeVectorCase(Vector3(-2, 2, -2), Vector3(-1, 1, 2)));
        cases.Add("outside", EdgeVectorCase(Vector3(-2.125f, 2.125f, -2.125f), Vector3(0, -0.0f, 0)));
        cases.Add("infinity", EdgeVectorCase(Vector3(std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()), Vector3(2, -2, 2)));
        cases.Add("nan", EdgeVectorCase(Vector3(std::numeric_limits<float>::quiet_NaN(), 1, std::numeric_limits<float>::quiet_NaN()), Vector3(1, std::numeric_limits<float>::quiet_NaN(), 1)));
        cases.Add("max", EdgeVectorCase(Vector3(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Vector3(0.5f, 2, 0.5f)));
        cases.Add("subnormal", EdgeVectorCase(Vector3(std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::denorm_min()), Vector3(2, 2, 2)));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 8; ++i)
        {
            // List initialization sequences random draws in source component order.
            auto value = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto divisor = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            cases.Add("seeded" + std::to_string(i), EdgeVectorCase(value, divisor));
        }
        auto magnitudes = JsonWriter();
        Vector3 inputs[] = { Vector3::Zero, Vector3(3, 4, 0), Vector3(1e20f, 0, 0),
            Vector3(1e-30f, 0, 0), Vector3(std::numeric_limits<float>::infinity(), 0, 0), Vector3(std::numeric_limits<float>::quiet_NaN(), 0, 0) };
        const char* names[] = { "zero", "threeFour", "large", "tiny", "infinity", "nan" };
        for (int i = 0; i < std::size(inputs); ++i)
        {
            auto value = inputs[i];
            auto item = JsonWriter().Add("input", EdgeVector(value));
            EdgeScalar(item, "length", value.Length());
            EdgeScalar(item, "lengthSquared", value.LengthSquared());
            item.Add("normalize", EdgeVector(Vector3::Normalize(value)));
            Vector3 result;
            Vector3::Normalize(value, result);
            item.Add("normalizeOut", EdgeVector(result));
            value.Normalize();
            item.Add("normalizeInstance", EdgeVector(value));
            magnitudes.Add(names[i], item);
        }
        auto spatial = JsonWriter();
        float amounts[] = { -1, 0, 0.25f, 0.5f, 1, 2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        for (int i = 0; i < std::size(amounts); ++i)
            spatial.Add("fixed" + std::to_string(i), EdgeSpatialCase(Vector3(1, 2, 3), Vector3(4, 3, 2),
                Vector3(-2, 1, 0), Vector3(2, -1, 4), amounts[i]));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto a = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto b = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto c = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto d = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            spatial.Add("seeded" + std::to_string(i), EdgeSpatialCase(a, b, c, d, amounts[i]));
        }
        JsonWriter products;
        const Vector3 crossInputs[] = {Vector3::Zero, Vector3(1e20f, 1e20f, 0)};
        for (int i = 0; i < 2; ++i)
        {
            const Vector3 left = crossInputs[i], right = left;
            auto item = JsonWriter().Add("a", EdgeVector(left)).Add("b", EdgeVector(right))
                .Add("cross", EdgeVector(Vector3::Cross(left, right)));
            Vector3 result;
            Vector3::Cross(left, right, result);
            item.Add("crossOut", EdgeVector(result));
            products.Add(i == 0 ? "zero" : "wideCancellation", item);
        }
        auto first = Vector3(1.5f, -2.25f, 3.5f);
        auto other = Vector3(1.5f, 2.25f, 3.5f);
        const std::string format = first.ToString();
        return JsonWriter().Add("seed", static_cast<double>(EdgeSeed)).Add("arithmetic", cases)
            .Add("magnitude", magnitudes).Add("spatial", spatial).Add("products", products).Add("format", format)
            .Add("unequalEquals", first.Equals(other)).Add("unequalOperator", first == other)
            .Add("unequalNotOperator", first != other)
            .Add("signedZeroEquals", Vector3(-0.0f).Equals(Vector3::Zero))
            .Add("signedZeroHashEqual", Vector3(-0.0f).GetHashCode() == Vector3::Zero.GetHashCode());
    }

    JsonWriter EdgeVector(Vector4 value)
    {
        auto output = JsonWriter();
        EdgeScalar(output, "X", value.X);
        EdgeScalar(output, "Y", value.Y);
        EdgeScalar(output, "Z", value.Z);
        EdgeScalar(output, "W", value.W);
        return output;
    }

    JsonWriter EdgeVectorCase(Vector4 value, Vector4 divisor)
    {
        auto min = Vector4(-2, -2, -2, -2);
        auto max = Vector4(2, 2, 2, 2);
        auto output = JsonWriter().Add("input", EdgeVector(value)).Add("divisor", EdgeVector(divisor));
        output.Add("divide", EdgeVector(Vector4::Divide(value, divisor)));
        Vector4 result;
        Vector4::Divide(value, divisor, result);
        output.Add("divideOut", EdgeVector(result));
        output.Add("divideOperator", EdgeVector(value / divisor));
        output.Add("scalarDivide", EdgeVector(Vector4::Divide(value, divisor.X)));
        Vector4::Divide(value, divisor.X, result);
        output.Add("scalarDivideOut", EdgeVector(result));
        output.Add("scalarDivideOperator", EdgeVector(value / divisor.X));
        output.Add("clamp", EdgeVector(Vector4::Clamp(value, min, max)));
        Vector4::Clamp(value, min, max, result);
        output.Add("clampOut", EdgeVector(result));
        output.Add("reversedClamp", EdgeVector(Vector4::Clamp(value, max, min)));
        Vector4::Clamp(value, max, min, result);
        output.Add("reversedClampOut", EdgeVector(result));
        auto copy = Vector4(value.X, value.Y, value.Z, value.W);
        output.Add("equalsCopy", value.Equals(copy)).Add("operatorEqualCopy", value == copy)
            .Add("operatorUnequalCopy", value != copy)
            .Add("copyHashEqual", value.GetHashCode() == copy.GetHashCode());
        return output;
    }

    JsonWriter EdgeSpatialCase(Vector4 a, Vector4 b, Vector4 c, Vector4 d, float amount)
    {
        auto output = JsonWriter().Add("a", EdgeVector(a)).Add("b", EdgeVector(b))
            .Add("c", EdgeVector(c)).Add("d", EdgeVector(d));
        EdgeScalar(output, "amount", amount);
        Vector4 result;
        output.Add("lerp", EdgeVector(Vector4::Lerp(a, b, amount)));
        Vector4::Lerp(a, b, amount, result);
        output.Add("lerpOut", EdgeVector(result));
        output.Add("smoothStep", EdgeVector(Vector4::SmoothStep(a, b, amount)));
        Vector4::SmoothStep(a, b, amount, result);
        output.Add("smoothStepOut", EdgeVector(result));
        output.Add("hermite", EdgeVector(Vector4::Hermite(a, b, c, d, amount)));
        Vector4::Hermite(a, b, c, d, amount, result);
        output.Add("hermiteOut", EdgeVector(result));
        output.Add("catmullRom", EdgeVector(Vector4::CatmullRom(a, b, c, d, amount)));
        Vector4::CatmullRom(a, b, c, d, amount, result);
        output.Add("catmullRomOut", EdgeVector(result));
        output.Add("barycentric", EdgeVector(Vector4::Barycentric(a, b, c, amount, 0.25f)));
        Vector4::Barycentric(a, b, c, amount, 0.25f, result);
        output.Add("barycentricOut", EdgeVector(result));
        auto matrix = Matrix(2, 0, 1, 0.5f, 0, -3, 0, 0, 1, 0, 4, 0, 7, 8, 9, 1);
        auto rotation = Quaternion(0.25f, -0.5f, 0.75f, 1);
        output.Add("transform", EdgeVector(Vector4::Transform(a, matrix)));
        Vector4::Transform(a, matrix, result);
        output.Add("transformOut", EdgeVector(result));
        output.Add("rotate", EdgeVector(Vector4::Transform(a, rotation)));
        Vector4::Transform(a, rotation, result);
        output.Add("rotateOut", EdgeVector(result));
        auto a2 = Vector2(a.X, a.Y);
        auto a3 = Vector3(a.X, a.Y, a.Z);
        output.Add("transform2", EdgeVector(Vector4::Transform(a2, matrix)));
        Vector4::Transform(a2, matrix, result);
        output.Add("transform2Out", EdgeVector(result));
        output.Add("rotate2", EdgeVector(Vector4::Transform(a2, rotation)));
        Vector4::Transform(a2, rotation, result);
        output.Add("rotate2Out", EdgeVector(result));
        output.Add("transform3", EdgeVector(Vector4::Transform(a3, matrix)));
        Vector4::Transform(a3, matrix, result);
        output.Add("transform3Out", EdgeVector(result));
        output.Add("rotate3", EdgeVector(Vector4::Transform(a3, rotation)));
        Vector4::Transform(a3, rotation, result);
        output.Add("rotate3Out", EdgeVector(result));
        return output;
    }

    JsonWriter DumpVector4Edges()
    {
        auto cases = JsonWriter();
        cases.Add("zero", EdgeVectorCase(Vector4(0, 0, 0, 0), Vector4(0, -0.0f, 2, -2)));
        cases.Add("signedZero", EdgeVectorCase(Vector4(-0.0f, 0, -0.0f, 0), Vector4(2, -2, -2, 2)));
        cases.Add("boundaries", EdgeVectorCase(Vector4(-2, 2, -2, 2), Vector4(-1, 1, 2, -2)));
        cases.Add("outside", EdgeVectorCase(Vector4(-2.125f, 2.125f, -2.125f, 2.125f), Vector4(0, -0.0f, 0, -0.0f)));
        cases.Add("infinity", EdgeVectorCase(Vector4(std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()), Vector4(2, -2, 2, -2)));
        cases.Add("nan", EdgeVectorCase(Vector4(std::numeric_limits<float>::quiet_NaN(), 1, std::numeric_limits<float>::quiet_NaN(), 1), Vector4(1, std::numeric_limits<float>::quiet_NaN(), 1, std::numeric_limits<float>::quiet_NaN())));
        cases.Add("max", EdgeVectorCase(Vector4(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()), Vector4(0.5f, 2, 0.5f, 2)));
        cases.Add("subnormal", EdgeVectorCase(Vector4(std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min()), Vector4(2, 2, 2, 2)));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 8; ++i)
        {
            auto value = Vector4{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto divisor = Vector4{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            cases.Add("seeded" + std::to_string(i), EdgeVectorCase(value, divisor));
        }
        auto magnitudes = JsonWriter();
        Vector4 inputs[] = { Vector4::Zero, Vector4(3, 4, 0, 0), Vector4(1e20f, 0, 0, 0),
            Vector4(1e-30f, 0, 0, 0), Vector4(std::numeric_limits<float>::infinity(), 0, 0, 0), Vector4(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0) };
        const char* names[] = { "zero", "threeFour", "large", "tiny", "infinity", "nan" };
        for (int i = 0; i < std::size(inputs); ++i)
        {
            auto value = inputs[i];
            auto item = JsonWriter().Add("input", EdgeVector(value));
            EdgeScalar(item, "length", value.Length());
            EdgeScalar(item, "lengthSquared", value.LengthSquared());
            item.Add("normalize", EdgeVector(Vector4::Normalize(value)));
            Vector4 result;
            Vector4::Normalize(value, result);
            item.Add("normalizeOut", EdgeVector(result));
            value.Normalize();
            item.Add("normalizeInstance", EdgeVector(value));
            magnitudes.Add(names[i], item);
        }
        auto spatial = JsonWriter();
        float amounts[] = { -1, 0, 0.25f, 0.5f, 1, 2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        for (int i = 0; i < std::size(amounts); ++i)
            spatial.Add("fixed" + std::to_string(i), EdgeSpatialCase(Vector4(1, 2, 3, 4), Vector4(4, 3, 2, 1),
                Vector4(-2, 1, 0, 3), Vector4(2, -1, 4, -3), amounts[i]));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto a = Vector4{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto b = Vector4{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto c = Vector4{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto d = Vector4{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            spatial.Add("seeded" + std::to_string(i), EdgeSpatialCase(a, b, c, d, amounts[i]));
        }
        auto first = Vector4(1.5f, -2.25f, 3.5f, -4.75f);
        auto other = Vector4(1.5f, 2.25f, 3.5f, -4.75f);
        const std::string format = first.ToString();
        return JsonWriter().Add("seed", static_cast<double>(EdgeSeed)).Add("arithmetic", cases)
            .Add("magnitude", magnitudes).Add("spatial", spatial).Add("format", format)
            .Add("unequalEquals", first.Equals(other)).Add("unequalOperator", first == other)
            .Add("unequalNotOperator", first != other)
            .Add("signedZeroEquals", Vector4(-0.0f).Equals(Vector4::Zero))
            .Add("signedZeroHashEqual", Vector4(-0.0f).GetHashCode() == Vector4::Zero.GetHashCode());
    }

    JsonWriter EdgeRotation(Quaternion value)
    {
        auto output = JsonWriter();
        EdgeScalar(output, "X", value.X); EdgeScalar(output, "Y", value.Y);
        EdgeScalar(output, "Z", value.Z); EdgeScalar(output, "W", value.W);
        return output;
    }

    JsonWriter EdgeQuaternionCase(Quaternion value)
    {
        Quaternion result;
        auto output = JsonWriter().Add("input", EdgeRotation(value));
        EdgeScalar(output, "length", value.Length());
        EdgeScalar(output, "lengthSquared", value.LengthSquared());
        output.Add("normalize", EdgeRotation(Quaternion::Normalize(value)));
        Quaternion::Normalize(value, result);
        output.Add("normalizeOut", EdgeRotation(result));
        auto copy = value;
        copy.Normalize();
        output.Add("normalizeInstance", EdgeRotation(copy));
        output.Add("inverse", EdgeRotation(Quaternion::Inverse(value)));
        Quaternion::Inverse(value, result);
        output.Add("inverseOut", EdgeRotation(result));
        output.Add("conjugate", EdgeRotation(Quaternion::Conjugate(value)));
        Quaternion::Conjugate(value, result);
        output.Add("conjugateOut", EdgeRotation(result));
        copy = value;
        copy.Conjugate();
        output.Add("conjugateInstance", EdgeRotation(copy));
        return output;
    }

    JsonWriter EdgeQuaternionPair(Quaternion a, Quaternion b, float amount)
    {
        Quaternion result;
        auto output = JsonWriter().Add("a", EdgeRotation(a)).Add("b", EdgeRotation(b));
        EdgeScalar(output, "amount", amount);
        output.Add("lerp", EdgeRotation(Quaternion::Lerp(a, b, amount)));
        Quaternion::Lerp(a, b, amount, result);
        output.Add("lerpOut", EdgeRotation(result));
        output.Add("slerp", EdgeRotation(Quaternion::Slerp(a, b, amount)));
        Quaternion::Slerp(a, b, amount, result);
        output.Add("slerpOut", EdgeRotation(result));
        output.Add("concatenate", EdgeRotation(Quaternion::Concatenate(a, b)));
        Quaternion::Concatenate(a, b, result);
        output.Add("concatenateOut", EdgeRotation(result));
        return output;
    }

    JsonWriter EdgeQuaternionFactory(Vector3 axis, float angle, float pitch, float roll)
    {
        Quaternion result;
        auto output = JsonWriter().Add("axis", EdgeVector(axis));
        EdgeScalar(output, "angle", angle); EdgeScalar(output, "pitch", pitch); EdgeScalar(output, "roll", roll);
        output.Add("axisAngle", EdgeRotation(Quaternion::CreateFromAxisAngle(axis, angle)));
        Quaternion::CreateFromAxisAngle(axis, angle, result);
        output.Add("axisAngleOut", EdgeRotation(result));
        output.Add("yawPitchRoll", EdgeRotation(Quaternion::CreateFromYawPitchRoll(angle, pitch, roll)));
        Quaternion::CreateFromYawPitchRoll(angle, pitch, roll, result);
        output.Add("yawPitchRollOut", EdgeRotation(result));
        return output;
    }

    JsonWriter DumpQuaternionEdges()
    {
        auto unary = JsonWriter();
        Quaternion inputs[] = { Quaternion(), Quaternion::Identity, Quaternion(1, 2, 3, 4),
            Quaternion(1e20f, 0, 0, 0), Quaternion(1e-30f, 0, 0, 0),
            Quaternion(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0), Quaternion(std::numeric_limits<float>::infinity(), 0, 0, 0) };
        const char* names[] = { "zero", "identity", "nonunit", "large", "tiny", "nan", "infinity" };
        for (int i = 0; i < std::size(inputs); ++i) unary.Add(names[i], EdgeQuaternionCase(inputs[i]));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
            unary.Add("seeded" + std::to_string(i), EdgeQuaternionCase(Quaternion{
                NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)}));

        auto pairs = JsonWriter();
        auto identity = Quaternion::Identity;
        auto halfTurn = Quaternion(0, 0, 1, 0);
        pairs.Add("identical", EdgeQuaternionPair(identity, identity, 0.5f));
        pairs.Add("opposite", EdgeQuaternionPair(identity, Quaternion(0, 0, 0, -1), 0.5f));
        pairs.Add("zero", EdgeQuaternionPair(Quaternion(), Quaternion(), 0.5f));
        pairs.Add("nearThreshold", EdgeQuaternionPair(identity, Quaternion(0, 0, 0, 0.999999f), 0.5f));
        float amounts[] = { -1, 0, 0.5f, 1, 2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        for (int i = 0; i < std::size(amounts); ++i)
            pairs.Add("orthogonal" + std::to_string(i), EdgeQuaternionPair(identity, halfTurn, amounts[i]));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            float amount = NextEdgeValue(state) / 8;
            pairs.Add("seeded" + std::to_string(i), EdgeQuaternionPair(Quaternion(0.5f, 0.5f, 0.5f, 0.5f),
                Quaternion(0.5f, -0.5f, 0.5f, -0.5f), amount));
        }

        auto factories = JsonWriter();
        float angles[] = { 0, MathHelper::PiOver2, MathHelper::Pi, -MathHelper::PiOver2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        for (int i = 0; i < std::size(angles); ++i)
            factories.Add("angle" + std::to_string(i), EdgeQuaternionFactory(Vector3::UnitY, angles[i], 0, 0));
        factories.Add("zeroAxis", EdgeQuaternionFactory(Vector3::Zero, MathHelper::PiOver2, 0, 0));
        factories.Add("nonunitAxis", EdgeQuaternionFactory(Vector3(2, -1, 0.5f), MathHelper::PiOver2, 0.5f, -0.25f));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            float yaw = NextEdgeValue(state) / 8, pitch = NextEdgeValue(state) / 8, roll = NextEdgeValue(state) / 8;
            factories.Add("seeded" + std::to_string(i), EdgeQuaternionFactory(Vector3::UnitX, yaw, pitch, roll));
        }

        auto matrices = JsonWriter();
        Matrix rotations[] = { Matrix::getIdentityProperty(),
            Matrix(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1),
            Matrix(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1),
            Matrix(-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1), Matrix() };
        const char* matrixNames[] = { "identity", "xHalfTurn", "yHalfTurn", "zHalfTurn", "zero" };
        for (int i = 0; i < std::size(rotations); ++i)
        {
            auto matrix = rotations[i];
            Quaternion result;
            auto item = JsonWriter().Add("input", EdgeMatrixFields(matrix))
                .Add("value", EdgeRotation(Quaternion::CreateFromRotationMatrix(matrix)));
            Quaternion::CreateFromRotationMatrix(matrix, result);
            item.Add("out", EdgeRotation(result));
            matrices.Add(matrixNames[i], item);
        }
        return JsonWriter().Add("seed", static_cast<double>(EdgeSeed)).Add("unary", unary).Add("pairs", pairs)
            .Add("factories", factories).Add("matrices", matrices);
    }
    JsonWriter MatrixCamera(Vector3 position, Vector3 target, Vector3 up,
        std::optional<Vector3> cameraForward, std::optional<Vector3> objectForward)
    {
        auto output = JsonWriter{}.Add("position", EdgeVector(position)).Add("target", EdgeVector(target))
            .Add("up", EdgeVector(up)).Add("hasCameraForward", cameraForward.has_value())
            .Add("hasObjectForward", objectForward.has_value())
            .Add("cameraForward", EdgeVector(cameraForward.value_or(Vector3{})))
            .Add("objectForward", EdgeVector(objectForward.value_or(Vector3{})));
        output.Add("lookAt", EdgeMatrixFields(Matrix::CreateLookAt(position, target, up)));
        Matrix result;
        Matrix::CreateLookAt(position, target, up, result);
        output.Add("lookAtOut", EdgeMatrixFields(result));
        output.Add("world", EdgeMatrixFields(Matrix::CreateWorld(position, target, up)));
        Matrix::CreateWorld(position, target, up, result);
        output.Add("worldOut", EdgeMatrixFields(result));
        output.Add("billboard", EdgeMatrixFields(Matrix::CreateBillboard(position, target, up, cameraForward)));
        Matrix::CreateBillboard(position, target, up, cameraForward, result);
        output.Add("billboardOut", EdgeMatrixFields(result));
        output.Add("constrained", EdgeMatrixFields(Matrix::CreateConstrainedBillboard(position, target, up, cameraForward, objectForward)));
        Matrix::CreateConstrainedBillboard(position, target, up, cameraForward, objectForward, result);
        output.Add("constrainedOut", EdgeMatrixFields(result));
        return output;
    }

    JsonWriter MatrixRotation(Vector3 axis, float angle, float pitch, float roll, Quaternion quaternion)
    {
        auto output = JsonWriter{}.Add("axis", EdgeVector(axis)).Add("quaternion", EdgeRotation(quaternion));
        EdgeScalar(output, "angle", angle); EdgeScalar(output, "pitch", pitch); EdgeScalar(output, "roll", roll);
        output.Add("axisAngle", EdgeMatrixFields(Matrix::CreateFromAxisAngle(axis, angle)));
        Matrix result;
        Matrix::CreateFromAxisAngle(axis, angle, result);
        output.Add("axisAngleOut", EdgeMatrixFields(result));
        output.Add("quaternionValue", EdgeMatrixFields(Matrix::CreateFromQuaternion(quaternion)));
        Matrix::CreateFromQuaternion(quaternion, result);
        output.Add("quaternionOut", EdgeMatrixFields(result));
        output.Add("yawPitchRoll", EdgeMatrixFields(Matrix::CreateFromYawPitchRoll(angle, pitch, roll)));
        Matrix::CreateFromYawPitchRoll(angle, pitch, roll, result);
        output.Add("yawPitchRollOut", EdgeMatrixFields(result));
        return output;
    }

    JsonWriter MatrixPlane(Plane plane, Vector3 light)
    {
        auto output = JsonWriter{}.Add("normal", EdgeVector(plane.Normal)).Add("light", EdgeVector(light));
        EdgeScalar(output, "d", plane.D);
        output.Add("reflection", EdgeMatrixFields(Matrix::CreateReflection(plane)));
        Matrix result;
        Matrix::CreateReflection(plane, result);
        output.Add("reflectionOut", EdgeMatrixFields(result));
        output.Add("shadow", EdgeMatrixFields(Matrix::CreateShadow(light, plane)));
        Matrix::CreateShadow(light, plane, result);
        output.Add("shadowOut", EdgeMatrixFields(result));
        return output;
    }

    JsonWriter MatrixAlgebra(Matrix value, Matrix other, float amount)
    {
        auto output = JsonWriter{}.Add("input", EdgeMatrixFields(value)).Add("other", EdgeMatrixFields(other));
        EdgeScalar(output, "amount", amount); EdgeScalar(output, "determinant", value.Determinant());
        output.Add("inverse", EdgeMatrixFields(Matrix::Invert(value)));
        Matrix result;
        Matrix::Invert(value, result);
        output.Add("inverseOut", EdgeMatrixFields(result));
        auto copy = value;
        Matrix::Invert(copy, copy);
        output.Add("inverseAliased", EdgeMatrixFields(copy));
        output.Add("transpose", EdgeMatrixFields(Matrix::Transpose(value)));
        Matrix::Transpose(value, result);
        output.Add("transposeOut", EdgeMatrixFields(result));
        copy = value;
        Matrix::Transpose(copy, copy);
        output.Add("transposeAliased", EdgeMatrixFields(copy));
        output.Add("lerp", EdgeMatrixFields(Matrix::Lerp(value, other, amount)));
        Matrix::Lerp(value, other, amount, result);
        output.Add("lerpOut", EdgeMatrixFields(result));
        copy = value;
        Matrix::Lerp(copy, other, amount, copy);
        output.Add("lerpAliasedFirst", EdgeMatrixFields(copy));
        copy = other;
        Matrix::Lerp(value, copy, amount, copy);
        output.Add("lerpAliasedSecond", EdgeMatrixFields(copy));
        return output;
    }

    JsonWriter MatrixDecomposition(Matrix value)
    {
        auto output = JsonWriter{}.Add("input", EdgeMatrixFields(value));
        auto scale = Vector3{11, 12, 13};
        auto translation = Vector3{21, 22, 23};
        auto rotation = Quaternion{31, 32, 33, 34};
        bool accepted = false;
        std::string exception = "none";
        try { accepted = value.Decompose(scale, rotation, translation); }
        catch (const std::exception& error) { exception = error.what(); }
        return output.Add("accepted", accepted).Add("exception", exception).Add("scale", EdgeVector(scale))
            .Add("rotation", EdgeRotation(rotation)).Add("translation", EdgeVector(translation));
    }

    JsonWriter DumpMatrixEdges()
    {
        auto cameras = JsonWriter{};
        cameras.Add("ordinary", MatrixCamera(Vector3{0, 0, 5}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("coincident", MatrixCamera(Vector3::Zero, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("explicitForward", MatrixCamera(Vector3::Zero, Vector3::Zero, Vector3::Up, Vector3::Forward, Vector3::Right));
        cameras.Add("nonunitForward", MatrixCamera(Vector3::Zero, Vector3::Zero, Vector3::Up, Vector3{0, 0, -2}, Vector3::Up));
        cameras.Add("zeroUp", MatrixCamera(Vector3::One, Vector3::Forward, Vector3::Zero, std::nullopt, std::nullopt));
        cameras.Add("parallelUp", MatrixCamera(Vector3::Up, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("parallelForwards", MatrixCamera(Vector3::Up, Vector3::Zero, Vector3::Up, Vector3::Up, Vector3::Up));
        cameras.Add("forwardAxis", MatrixCamera(Vector3::Forward, Vector3::Zero, Vector3::Forward, std::nullopt, std::nullopt));
        cameras.Add("belowDistance", MatrixCamera(Vector3{0, 0, 0.009999f}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("aboveDistance", MatrixCamera(Vector3{0, 0, 0.010001f}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("belowParallel", MatrixCamera(Vector3{0.06f, 0.998f, 0}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("aboveParallel", MatrixCamera(Vector3{0.05f, 0.999f, 0}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("large", MatrixCamera(Vector3{1e20f, 0, 0}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("tiny", MatrixCamera(Vector3{1e-30f, 0, 0}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("nan", MatrixCamera(Vector3{std::numeric_limits<float>::quiet_NaN(), 0, 0}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        cameras.Add("infinity", MatrixCamera(Vector3{std::numeric_limits<float>::infinity(), 0, 0}, Vector3::Zero, Vector3::Up, std::nullopt, std::nullopt));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto position = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto target = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            auto up = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            cameras.Add("seeded" + std::to_string(i), MatrixCamera(position, target, up, Vector3::Forward, Vector3::Right));
        }

        auto rotations = JsonWriter{};
        float angles[] = { 0, MathHelper::PiOver2, MathHelper::Pi, -MathHelper::PiOver2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        for (int i = 0; i < std::size(angles); ++i)
            rotations.Add("angle" + std::to_string(i), MatrixRotation(Vector3::UnitY, angles[i], 0, 0, Quaternion::Identity));
        rotations.Add("zero", MatrixRotation(Vector3::Zero, 0.5f, 0, 0, Quaternion{}));
        rotations.Add("nonunit", MatrixRotation(Vector3{2, -1, 0.5f}, 0.390625f, 0.5f, -0.25f, Quaternion{1, 2, 3, 4}));
        rotations.Add("large", MatrixRotation(Vector3{1e20f, 0, 0}, 0.5f, 0, 0, Quaternion{1e20f, 0, 0, 0}));
        rotations.Add("tiny", MatrixRotation(Vector3{1e-30f, 0, 0}, 0.5f, 0, 0, Quaternion{1e-30f, 0, 0, 0}));
        state = EdgeSeed;
        for (int i = 0; i < 8; ++i)
        {
            float angle = NextEdgeValue(state) / 8, pitch = NextEdgeValue(state) / 8, roll = NextEdgeValue(state) / 8;
            rotations.Add("seeded" + std::to_string(i), MatrixRotation(Vector3::UnitX, angle, pitch, roll, Quaternion{0.5f, -0.5f, 0.5f, -0.5f}));
        }

        auto planes = JsonWriter{};
        Plane planeInputs[] = { Plane{Vector3::Up, -2}, Plane{Vector3{0, 2, 0}, -4},
            Plane{Vector3::Zero, 1}, Plane{Vector3{0, 1.0000001f, 0}, -2},
            Plane{Vector3{1e20f, 0, 0}, -1}, Plane{Vector3{1e-30f, 0, 0}, -1},
            Plane{Vector3{std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1}, Plane{Vector3{std::numeric_limits<float>::infinity(), 0, 0}, 1} };
        const char* planeNames[] = { "unit", "nonunit", "zero", "nearUnit", "large", "tiny", "nan", "infinity" };
        for (int i = 0; i < std::size(planeInputs); ++i)
            planes.Add(planeNames[i], MatrixPlane(planeInputs[i], Vector3{1, -2, 3}));
        planes.Add("parallelLight", MatrixPlane(Plane{Vector3::Up, -2}, Vector3::Right));
        planes.Add("zeroLight", MatrixPlane(Plane{Vector3::Up, -2}, Vector3::Zero));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto normal = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            float d = NextEdgeValue(state);
            planes.Add("seeded" + std::to_string(i), MatrixPlane(Plane{normal, d}, Vector3{1, -2, 3}));
        }

        auto algebra = JsonWriter{};
        auto identity = Matrix::getIdentityProperty();
        Matrix matrices[] = { identity, Matrix{}, Matrix::CreateScale(1, 0, 1),
            Matrix::CreateScale(1e-30f, 1, 1), Matrix::CreateScale(1e20f, 1e20f, 1e20f),
            Matrix{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
            Matrix{1, 1, 0, 0, 1, 1.0000001f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
            Matrix{-0.0f, 2, 3, 4, 5, std::numeric_limits<float>::quiet_NaN(), 7, 8, 9, 10, std::numeric_limits<float>::infinity(), 12, 13, 14, 15, 16} };
        const char* names[] = { "identity", "zero", "singular", "tiny", "large", "sequential", "nearSingular", "ieee" };
        for (int i = 0; i < std::size(matrices); ++i)
            algebra.Add(names[i], MatrixAlgebra(matrices[i], identity, 0.5f));
        float amounts[] = { -1, 0, 1, 2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        for (int i = 0; i < std::size(amounts); ++i)
            algebra.Add("amount" + std::to_string(i), MatrixAlgebra(matrices[5], identity, amounts[i]));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto matrix = Matrix{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state),
                NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state),
                NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state),
                NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            algebra.Add("seeded" + std::to_string(i), MatrixAlgebra(matrix, identity, 0.25f));
        }

        auto decomposition = JsonWriter{};
        Vector3 scales[] = { Vector3::One, Vector3{-2, 3, 4}, Vector3{-1, 1, 1},
            Vector3{-2, -3, 4}, Vector3{1, 0, 1}, Vector3::Zero, Vector3{1e-5f, 1, 1},
            Vector3{1e-4f, 1, 1}, Vector3{1e20f, 1, 1}, Vector3{std::numeric_limits<float>::quiet_NaN(), 1, 1},
            Vector3{std::numeric_limits<float>::infinity(), 1, 1} };
        const char* scaleNames[] = { "unit", "negative", "negativeTie", "twoNegative", "zeroAxis", "allZero", "belowEpsilon", "atEpsilon", "large", "nan", "infinity" };
        for (int i = 0; i < std::size(scales); ++i)
        {
            auto matrix = Matrix::CreateScale(scales[i]);
            matrix.M41 = 7; matrix.M42 = 8; matrix.M43 = 9;
            decomposition.Add(scaleNames[i], MatrixDecomposition(matrix));
        }
        auto shear = identity; shear.M12 = 0.5f;
        decomposition.Add("shear", MatrixDecomposition(shear));
        auto fourth = identity; fourth.M14 = 3; fourth.M24 = -4; fourth.M34 = 5; fourth.M44 = 7;
        decomposition.Add("fourthColumn", MatrixDecomposition(fourth));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            float x = NextEdgeValue(state), y = NextEdgeValue(state), z = NextEdgeValue(state);
            auto matrix = Matrix::CreateScale(x, y, z);
            matrix.M41 = 7; matrix.M42 = 8; matrix.M43 = 9;
            decomposition.Add("seeded" + std::to_string(i), MatrixDecomposition(matrix));
        }
        return JsonWriter{}.Add("seed", static_cast<double>(EdgeSeed)).Add("cameras", cameras).Add("rotations", rotations)
            .Add("planes", planes).Add("algebra", algebra).Add("decomposition", decomposition);
    }

    JsonWriter GeometryPlaneFields(Plane value)
    {
        auto result = JsonWriter{}.Add("normal", EdgeVector(value.Normal));
        EdgeScalar(result, "d", value.D);
        return result;
    }

    JsonWriter GeometryBoxFields(BoundingBox value)
    {
        return JsonWriter{}.Add("min", EdgeVector(value.Min)).Add("max", EdgeVector(value.Max));
    }

    JsonWriter GeometrySphereFields(BoundingSphere value)
    {
        auto result = JsonWriter{}.Add("center", EdgeVector(value.Center));
        EdgeScalar(result, "radius", value.Radius);
        return result;
    }

    JsonWriter GeometryCorners(const std::vector<Vector3>& values)
    {
        auto result = JsonWriter{};
        for (int i = 0; i < std::size(values); ++i) result.Add("p" + std::to_string(i), EdgeVector(values[i]));
        return result;
    }

    void GeometryDistance(JsonWriter& output, const std::string& name, std::optional<float> value)
    {
        if (value.has_value()) EdgeScalar(output, name, value.value());
        else output.Add(name, std::string("none"));
    }

    JsonWriter GeometryPlaneCase(Plane plane, Vector3 point, Matrix matrix, Quaternion quaternion)
    {
        auto output = JsonWriter{}.Add("input", GeometryPlaneFields(plane)).Add("point", EdgeVector(point))
            .Add("matrix", EdgeMatrixFields(matrix)).Add("quaternion", EdgeRotation(quaternion));
        auto vector = Vector4{point, 2};
        EdgeScalar(output, "dot", plane.Dot(vector));
        float dot;
        plane.Dot(vector, dot);
        EdgeScalar(output, "dotOut", dot);
        EdgeScalar(output, "dotCoordinate", plane.DotCoordinate(point));
        plane.DotCoordinate(point, dot);
        EdgeScalar(output, "dotCoordinateOut", dot);
        EdgeScalar(output, "dotNormal", plane.DotNormal(point));
        plane.DotNormal(point, dot);
        EdgeScalar(output, "dotNormalOut", dot);
        output.Add("normalize", GeometryPlaneFields(Plane::Normalize(plane)));
        Plane result;
        Plane::Normalize(plane, result);
        output.Add("normalizeOut", GeometryPlaneFields(result));
        auto copy = plane; copy.Normalize();
        output.Add("normalizeInstance", GeometryPlaneFields(copy));
        copy = plane; Plane::Normalize(copy, copy);
        output.Add("normalizeAliased", GeometryPlaneFields(copy));
        output.Add("matrixTransform", GeometryPlaneFields(Plane::Transform(plane, matrix)));
        Plane::Transform(plane, matrix, result);
        output.Add("matrixTransformOut", GeometryPlaneFields(result));
        copy = plane; Plane::Transform(copy, matrix, copy);
        output.Add("matrixTransformAliased", GeometryPlaneFields(copy));
        output.Add("quaternionTransform", GeometryPlaneFields(Plane::Transform(plane, quaternion)));
        Plane::Transform(plane, quaternion, result);
        output.Add("quaternionTransformOut", GeometryPlaneFields(result));
        copy = plane; Plane::Transform(copy, quaternion, copy);
        output.Add("quaternionTransformAliased", GeometryPlaneFields(copy));
        return output;
    }

    JsonWriter GeometryRelation(BoundingBox box, BoundingSphere sphere, Ray ray, Plane plane, Vector3 point)
    {
        auto output = JsonWriter{}.Add("box", GeometryBoxFields(box)).Add("sphere", GeometrySphereFields(sphere))
            .Add("origin", EdgeVector(ray.Position)).Add("direction", EdgeVector(ray.Direction))
            .Add("plane", GeometryPlaneFields(plane)).Add("point", EdgeVector(point));
        auto otherBox = BoundingBox{Vector3{-1}, Vector3{1}};
        auto otherSphere = BoundingSphere{Vector3{0, 0, 0.5f}, 1};
        auto frustum = BoundingFrustum{Matrix::getIdentityProperty()};
        ContainmentType containment;
        PlaneIntersectionType side;
        bool hit;
        std::optional<float> distance;
        output.Add("boxContainsBox", static_cast<int>(box.Contains(otherBox)));
        box.Contains(otherBox, containment);
        output.Add("boxContainsBoxOut", static_cast<int>(containment));
        output.Add("boxContainsSphere", static_cast<int>(box.Contains(sphere)));
        box.Contains(sphere, containment);
        output.Add("boxContainsSphereOut", static_cast<int>(containment));
        output.Add("boxContainsPoint", static_cast<int>(box.Contains(point)));
        box.Contains(point, containment);
        output.Add("boxContainsPointOut", static_cast<int>(containment));
        output.Add("boxContainsFrustum", static_cast<int>(box.Contains(frustum)));
        output.Add("sphereContainsBox", static_cast<int>(sphere.Contains(box)));
        sphere.Contains(box, containment);
        output.Add("sphereContainsBoxOut", static_cast<int>(containment));
        output.Add("sphereContainsSphere", static_cast<int>(sphere.Contains(otherSphere)));
        sphere.Contains(otherSphere, containment);
        output.Add("sphereContainsSphereOut", static_cast<int>(containment));
        output.Add("sphereContainsPoint", static_cast<int>(sphere.Contains(point)));
        sphere.Contains(point, containment);
        output.Add("sphereContainsPointOut", static_cast<int>(containment));
        output.Add("sphereContainsFrustum", static_cast<int>(sphere.Contains(frustum)));
        output.Add("frustumContainsBox", static_cast<int>(frustum.Contains(box)));
        frustum.Contains(box, containment);
        output.Add("frustumContainsBoxOut", static_cast<int>(containment));
        output.Add("frustumContainsSphere", static_cast<int>(frustum.Contains(sphere)));
        frustum.Contains(sphere, containment);
        output.Add("frustumContainsSphereOut", static_cast<int>(containment));
        output.Add("frustumContainsPoint", static_cast<int>(frustum.Contains(point)));
        frustum.Contains(point, containment);
        output.Add("frustumContainsPointOut", static_cast<int>(containment));
        output.Add("boxIntersectsBox", box.Intersects(otherBox));
        box.Intersects(otherBox, hit);
        output.Add("boxIntersectsBoxOut", hit);
        output.Add("boxIntersectsSphere", box.Intersects(sphere));
        box.Intersects(sphere, hit);
        output.Add("boxIntersectsSphereOut", hit);
        output.Add("boxIntersectsPlane", static_cast<int>(box.Intersects(plane)));
        box.Intersects(plane, side);
        output.Add("boxIntersectsPlaneOut", static_cast<int>(side));
        GeometryDistance(output, "boxIntersectsRay", box.Intersects(ray));
        box.Intersects(ray, distance);
        GeometryDistance(output, "boxIntersectsRayOut", distance);
        output.Add("boxIntersectsFrustum", box.Intersects(frustum));
        output.Add("sphereIntersectsBox", sphere.Intersects(box));
        sphere.Intersects(box, hit);
        output.Add("sphereIntersectsBoxOut", hit);
        output.Add("sphereIntersectsSphere", sphere.Intersects(otherSphere));
        sphere.Intersects(otherSphere, hit);
        output.Add("sphereIntersectsSphereOut", hit);
        output.Add("sphereIntersectsPlane", static_cast<int>(sphere.Intersects(plane)));
        sphere.Intersects(plane, side);
        output.Add("sphereIntersectsPlaneOut", static_cast<int>(side));
        GeometryDistance(output, "sphereIntersectsRay", sphere.Intersects(ray));
        sphere.Intersects(ray, distance);
        GeometryDistance(output, "sphereIntersectsRayOut", distance);
        output.Add("sphereIntersectsFrustum", sphere.Intersects(frustum));
        GeometryDistance(output, "rayIntersectsBox", ray.Intersects(box));
        ray.Intersects(box, distance);
        GeometryDistance(output, "rayIntersectsBoxOut", distance);
        GeometryDistance(output, "rayIntersectsSphere", ray.Intersects(sphere));
        ray.Intersects(sphere, distance);
        GeometryDistance(output, "rayIntersectsSphereOut", distance);
        GeometryDistance(output, "rayIntersectsPlane", ray.Intersects(plane));
        ray.Intersects(plane, distance);
        GeometryDistance(output, "rayIntersectsPlaneOut", distance);
        output.Add("planeIntersectsBox", static_cast<int>(plane.Intersects(box)));
        plane.Intersects(box, side);
        output.Add("planeIntersectsBoxOut", static_cast<int>(side));
        output.Add("planeIntersectsSphere", static_cast<int>(plane.Intersects(sphere)));
        plane.Intersects(sphere, side);
        output.Add("planeIntersectsSphereOut", static_cast<int>(side));
        output.Add("planeIntersectsFrustum", static_cast<int>(plane.Intersects(frustum)));
        output.Add("frustumIntersectsBox", frustum.Intersects(box));
        frustum.Intersects(box, hit);
        output.Add("frustumIntersectsBoxOut", hit);
        output.Add("frustumIntersectsSphere", frustum.Intersects(sphere));
        frustum.Intersects(sphere, hit);
        output.Add("frustumIntersectsSphereOut", hit);
        output.Add("frustumIntersectsPlane", static_cast<int>(frustum.Intersects(plane)));
        frustum.Intersects(plane, side);
        output.Add("frustumIntersectsPlaneOut", static_cast<int>(side));
        output.Add("mergedBox", GeometryBoxFields(BoundingBox::CreateMerged(box, otherBox)));
        BoundingBox boxResult;
        BoundingBox::CreateMerged(box, otherBox, boxResult);
        output.Add("mergedBoxOut", GeometryBoxFields(boxResult));
        output.Add("boxFromSphere", GeometryBoxFields(BoundingBox::CreateFromSphere(sphere)));
        BoundingBox::CreateFromSphere(sphere, boxResult);
        output.Add("boxFromSphereOut", GeometryBoxFields(boxResult));
        output.Add("sphereFromBox", GeometrySphereFields(BoundingSphere::CreateFromBoundingBox(box)));
        BoundingSphere sphereResult;
        BoundingSphere::CreateFromBoundingBox(box, sphereResult);
        output.Add("sphereFromBoxOut", GeometrySphereFields(sphereResult));
        output.Add("mergedSphere", GeometrySphereFields(BoundingSphere::CreateMerged(sphere, otherSphere)));
        BoundingSphere::CreateMerged(sphere, otherSphere, sphereResult);
        output.Add("mergedSphereOut", GeometrySphereFields(sphereResult));
        return output;
    }

    JsonWriter GeometryPointSet(const std::vector<Vector3>& points)
    {
        auto output = JsonWriter{}.Add("points", GeometryCorners(points)).Add("count", static_cast<int>(std::size(points)));
        try { output.Add("box", GeometryBoxFields(BoundingBox::CreateFromPoints(points))).Add("boxException", std::string("none")); }
        catch (const System::ArgumentException&) { output.Add("boxException", std::string("ArgumentException")); }
        try { output.Add("sphere", GeometrySphereFields(BoundingSphere::CreateFromPoints(points))).Add("sphereException", std::string("none")); }
        catch (const System::ArgumentException&) { output.Add("sphereException", std::string("ArgumentException")); }
        return output;
    }

    JsonWriter GeometryFrustumCase(Matrix matrix)
    {
        auto frustum = BoundingFrustum{matrix};
        auto other = BoundingFrustum{Matrix::getIdentityProperty()};
        auto output = JsonWriter{}.Add("matrix", EdgeMatrixFields(matrix))
            .Add("contains", static_cast<int>(frustum.Contains(other))).Add("reverseContains", static_cast<int>(other.Contains(frustum)))
            .Add("intersects", frustum.Intersects(other)).Add("reverseIntersects", other.Intersects(frustum))
            .Add("corners", GeometryCorners(frustum.GetCorners()))
            .Add("sphere", GeometrySphereFields(BoundingSphere::CreateFromFrustum(frustum)));
        auto corners = std::vector<Vector3>(8); frustum.GetCorners(corners);
        output.Add("cornersOut", GeometryCorners(corners));
        return output;
    }

    BoundingSphere NegativeGeometrySphere()
    {
        auto result = BoundingSphere{Vector3::Zero, 1};
        result.Radius = -1;
        return result;
    }

    JsonWriter DumpGeometryEdges()
    {
        auto planes = JsonWriter{};
        Plane inputs[] = { Plane{Vector3::Up, -2}, Plane{Vector3{0, 2, 0}, -4},
            Plane{Vector3::Zero, 1}, Plane{Vector3{0.8414709568023682f, 0.5403022766113281f, 0}, -2},
            Plane{Vector3{1e20f, 0, 0}, -1}, Plane{Vector3{1e-30f, 0, 0}, -1},
            Plane{Vector3{std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1}, Plane{Vector3{std::numeric_limits<float>::infinity(), 0, 0}, 1} };
        const char* names[] = { "unit", "scaled", "zero", "nearUnit", "large", "tiny", "nan", "infinity" };
        auto transform = Matrix{2, 0, 0, 0, 0, 4, 0, 0, 0, 0, -1, 0, 7, 8, 9, 1};
        auto rotation = Quaternion{0.5f, -0.5f, 0.5f, -0.5f};
        for (int i = 0; i < std::size(inputs); ++i)
            planes.Add(names[i], GeometryPlaneCase(inputs[i], Vector3{1, -2, 3}, transform, rotation));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto normal = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            float d = NextEdgeValue(state);
            planes.Add("seeded" + std::to_string(i), GeometryPlaneCase(Plane{normal, d}, Vector3{1, -2, 3}, transform, rotation));
        }
        planes.Add("singular", GeometryPlaneCase(inputs[0], Vector3::One, Matrix{}, rotation));

        auto relations = JsonWriter{};
        auto unitBox = BoundingBox{Vector3{-1}, Vector3{1}};
        auto unitSphere = BoundingSphere{Vector3{0, 0, 0.5f}, 1};
        auto unitPlane = Plane{Vector3::Up, 0};
        relations.Add("inside", GeometryRelation(unitBox, unitSphere, Ray{Vector3::Zero, Vector3::Right}, unitPlane, Vector3::Zero));
        relations.Add("boundary", GeometryRelation(unitBox, BoundingSphere{Vector3{2, 0, 0.5f}, 1}, Ray{Vector3::Right, Vector3::Right}, unitPlane, Vector3::Right));
        relations.Add("disjoint", GeometryRelation(BoundingBox{Vector3{3}, Vector3{4}}, BoundingSphere{Vector3{5}, 1}, Ray{Vector3{5}, Vector3::Right}, unitPlane, Vector3{5}));
        relations.Add("zero", GeometryRelation(BoundingBox{Vector3::Zero, Vector3::Zero}, BoundingSphere{Vector3::Zero, 0}, Ray{Vector3::Zero, Vector3::Zero}, Plane{Vector3::Zero, 0}, Vector3::Zero));
        relations.Add("insideOut", GeometryRelation(BoundingBox{Vector3::One, -Vector3::One}, unitSphere, Ray{Vector3::Zero, Vector3::Right}, unitPlane, Vector3::Zero));
        relations.Add("negativeRadius", GeometryRelation(unitBox, NegativeGeometrySphere(), Ray{Vector3::Zero, Vector3::Right}, unitPlane, Vector3::Right));
        relations.Add("stationary", GeometryRelation(unitBox, unitSphere, Ray{Vector3::Zero, Vector3::Zero}, unitPlane, Vector3::Zero));
        relations.Add("parallelOutside", GeometryRelation(unitBox, unitSphere, Ray{Vector3{2, 0, 0}, Vector3::Up}, unitPlane, Vector3{0, 0, -0.000005f}));
        float thresholds[] = { 0.0000005f, 0.000001f, 0.000002f, 0.000009f, 0.00001f, 0.000011f };
        for (int i = 0; i < std::size(thresholds); ++i)
            relations.Add("threshold" + std::to_string(i), GeometryRelation(unitBox, unitSphere,
                Ray{Vector3{-2, 0, 0}, Vector3{thresholds[i], 0, 0}}, Plane{Vector3::Right, 1}, Vector3::Right));
        state = EdgeSeed;
        for (int i = 0; i < 6; ++i)
        {
            auto center = Vector3{NextEdgeValue(state) / 8, NextEdgeValue(state) / 8, NextEdgeValue(state) / 8};
            float radius = std::abs(NextEdgeValue(state)) / 8;
            auto extent = Vector3{0.25f, 0.5f, 0.75f};
            relations.Add("seeded" + std::to_string(i), GeometryRelation(BoundingBox{center - extent, center + extent},
                BoundingSphere{center, radius}, Ray{center, Vector3::Right}, unitPlane, center));
        }

        auto sets = JsonWriter{};
        sets.Add("empty", GeometryPointSet(std::vector<Vector3>{}));
        sets.Add("single", GeometryPointSet(std::vector<Vector3>{ Vector3{1, 2, 3} }));
        sets.Add("repeated", GeometryPointSet(std::vector<Vector3>{ Vector3::One, Vector3::One, Vector3::One }));
        sets.Add("pair", GeometryPointSet(std::vector<Vector3>{ Vector3{-2, 0, 0}, Vector3{2, 0, 0} }));
        sets.Add("triangleTie", GeometryPointSet(std::vector<Vector3>{ Vector3::Zero, Vector3::Right, Vector3::Up }));
        sets.Add("largePair", GeometryPointSet(std::vector<Vector3>{ Vector3{-1e20f, 0, 0}, Vector3{1e20f, 0, 0} }));
        sets.Add("tinyPair", GeometryPointSet(std::vector<Vector3>{ Vector3{-1e-30f, 0, 0}, Vector3{1e-30f, 0, 0} }));
        state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto points = std::vector<Vector3>(5);
            for (int j = 0; j < std::size(points); ++j)
                points[j] = Vector3{NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            sets.Add("seeded" + std::to_string(i), GeometryPointSet(points));
        }
        auto frustums = JsonWriter{};
        frustums.Add("identity", GeometryFrustumCase(Matrix::getIdentityProperty()));
        frustums.Add("touching", GeometryFrustumCase(Matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, 0, 1}));
        frustums.Add("separated", GeometryFrustumCase(Matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 0, 0, 1}));
        frustums.Add("enclosing", GeometryFrustumCase(Matrix{0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1}));
        frustums.Add("enclosed", GeometryFrustumCase(Matrix{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1}));
        return JsonWriter{}.Add("seed", static_cast<double>(EdgeSeed)).Add("planes", planes).Add("relations", relations)
            .Add("pointSets", sets).Add("frustums", frustums);
    }


    std::string EdgePackingHex(std::uint64_t value)
    {
        std::string result(16, '0');
        constexpr char digits[] = "0123456789abcdef";
        for (int i = 15; i >= 0; --i) { result[i] = digits[value & 15]; value >>= 4; }
        return result;
    }

    template<class T>
    JsonWriter EdgePackingCase(Vector4 input, T value, T vectorValue, std::uint64_t raw)
    {
        auto output = JsonWriter().Add("input", EdgeVector(input))
            .Add("constructor", EdgePackingHex(value.getPackedValueProperty()))
            .Add("vectorConstructor", EdgePackingHex(vectorValue.getPackedValueProperty()))
            .Add("unpack", EdgeVector(value.ToVector4())).Add("vectorUnpack", EdgeVector(vectorValue.ToVector4()));
        T packed; packed.PackFromVector4(input);
        output.Add("pack", EdgePackingHex(packed.getPackedValueProperty())).Add("packUnpack", EdgeVector(packed.ToVector4()));
        packed.setPackedValueProperty(static_cast<decltype(packed.getPackedValueProperty())>(raw));
        output.Add("raw", EdgePackingHex(packed.getPackedValueProperty())).Add("rawUnpack", EdgeVector(packed.ToVector4()));
        Vector4 expanded = packed.ToVector4(); packed.PackFromVector4(expanded);
        return output.Add("repackedRaw", EdgePackingHex(packed.getPackedValueProperty()));
    }

    Vector4 EdgePackingInput(int index, Vector4 scales, std::uint32_t& state)
    {
        float nan = std::bit_cast<float>(0x7fc00000);
        if (index == 0) return Vector4::Zero;
        if (index == 1) return Vector4{-0.0f, 0, -0.0f, 0};
        if (index == 2) return Vector4::One;
        if (index == 3) return Vector4{-1};
        if (index == 4) return Vector4{0.25f, 0.5f, 0.75f, 1};
        auto ties = Vector4{0.5f / scales.X, 1.5f / scales.Y, 2.5f / scales.Z, 3.5f / scales.W};
        if (index == 5) return ties;
        if (index == 6 || index == 7)
        {
            int step = index == 6 ? -1 : 1;
            return Vector4{std::bit_cast<float>(std::bit_cast<std::int32_t>(ties.X) + step),
                std::bit_cast<float>(std::bit_cast<std::int32_t>(ties.Y) + step),
                std::bit_cast<float>(std::bit_cast<std::int32_t>(ties.Z) + step),
                std::bit_cast<float>(std::bit_cast<std::int32_t>(ties.W) + step)};
        }
        if (index == 8) return Vector4{-1.125f, -1, -0.125f, 0};
        if (index == 9) return Vector4{0, 0.875f, 1, 1.125f};
        if (index == 10) return Vector4{-0.5f, 0.5f, 254.5f, 255.5f};
        if (index == 11) return Vector4{-32769, -32768, 32767, 32768};
        if (index == 12) return Vector4{1.00048828125f, 1.00146484375f, 65504, 65520};
        if (index == 13) return Vector4{0.0000000298023223876953125f, 0.0000000894069671630859375f,
            0.000000059604644775390625f, 0.000060975551605224609375f};
        if (index == 14) return Vector4{nan, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f};
        return Vector4{NextEdgeValue(state) / 8, NextEdgeValue(state) / 8, NextEdgeValue(state) / 8, NextEdgeValue(state) / 8};
    }

    JsonWriter EdgePackingColor(Color value)
    {
        return JsonWriter().Add("packed", EdgePackingHex(value.getPackedValueProperty()))
            .Add("r", static_cast<int>(value.getRProperty())).Add("g", static_cast<int>(value.getGProperty())).Add("b", static_cast<int>(value.getBProperty())).Add("a", static_cast<int>(value.getAProperty()))
            .Add("vector3", EdgeVector(value.ToVector3())).Add("vector4", EdgeVector(value.ToVector4()));
    }

    JsonWriter DumpPackingEdges()
    {
        const char* names[] = { "zero", "signedZero", "one", "negative", "quarters", "ties", "belowTies", "aboveTies",
            "clampLow", "clampHigh", "byteBoundary", "shortBoundary", "halfBoundary", "halfTiny", "nonfinite",
            "seeded0", "seeded1", "seeded2", "seeded3" };
        std::uint64_t rawValues[] = { 0ULL, 1ULL, UINT64_MAX, 0x8000800080008000ULL, 0x7fff7fff7fff7fffULL, 0x7e007c00fc008000ULL };
        auto packedTypes = JsonWriter();
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(255, 255, 255, 255), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffULL;
                cases.Add(names[i], EdgePackingCase(input, Alpha8(input.W), Alpha8(input.W), raw));
            }
            packedTypes.Add("Alpha8", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(31, 63, 31, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffULL;
                cases.Add(names[i], EdgePackingCase(input, Bgr565(input.X, input.Y, input.Z), Bgr565(Vector3(input.X, input.Y, input.Z)), raw));
            }
            packedTypes.Add("Bgr565", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(15, 15, 15, 15), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffULL;
                cases.Add(names[i], EdgePackingCase(input, Bgra4444(input.X, input.Y, input.Z, input.W), Bgra4444(input), raw));
            }
            packedTypes.Add("Bgra4444", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(31, 31, 31, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffULL;
                cases.Add(names[i], EdgePackingCase(input, Bgra5551(input.X, input.Y, input.Z, input.W), Bgra5551(input), raw));
            }
            packedTypes.Add("Bgra5551", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1, 1, 1, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, Byte4(input.X, input.Y, input.Z, input.W), Byte4(input), raw));
            }
            packedTypes.Add("Byte4", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1, 1, 1, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffULL;
                cases.Add(names[i], EdgePackingCase(input, HalfSingle(input.X), HalfSingle(input.X), raw));
            }
            packedTypes.Add("HalfSingle", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1, 1, 1, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, HalfVector2(input.X, input.Y), HalfVector2(Vector2(input.X, input.Y)), raw));
            }
            packedTypes.Add("HalfVector2", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1, 1, 1, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & UINT64_MAX;
                cases.Add(names[i], EdgePackingCase(input, HalfVector4(input.X, input.Y, input.Z, input.W), HalfVector4(input), raw));
            }
            packedTypes.Add("HalfVector4", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(127, 127, 127, 127), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffULL;
                cases.Add(names[i], EdgePackingCase(input, NormalizedByte2(input.X, input.Y), NormalizedByte2(Vector2(input.X, input.Y)), raw));
            }
            packedTypes.Add("NormalizedByte2", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(127, 127, 127, 127), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, NormalizedByte4(input.X, input.Y, input.Z, input.W), NormalizedByte4(input), raw));
            }
            packedTypes.Add("NormalizedByte4", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(32767, 32767, 32767, 32767), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, NormalizedShort2(input.X, input.Y), NormalizedShort2(Vector2(input.X, input.Y)), raw));
            }
            packedTypes.Add("NormalizedShort2", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(32767, 32767, 32767, 32767), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & UINT64_MAX;
                cases.Add(names[i], EdgePackingCase(input, NormalizedShort4(input.X, input.Y, input.Z, input.W), NormalizedShort4(input), raw));
            }
            packedTypes.Add("NormalizedShort4", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(65535, 65535, 65535, 65535), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, Rg32(input.X, input.Y), Rg32(Vector2(input.X, input.Y)), raw));
            }
            packedTypes.Add("Rg32", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1023, 1023, 1023, 3), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, Rgba1010102(input.X, input.Y, input.Z, input.W), Rgba1010102(input), raw));
            }
            packedTypes.Add("Rgba1010102", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(65535, 65535, 65535, 65535), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & UINT64_MAX;
                cases.Add(names[i], EdgePackingCase(input, Rgba64(input.X, input.Y, input.Z, input.W), Rgba64(input), raw));
            }
            packedTypes.Add("Rgba64", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1, 1, 1, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & 0xffffffffULL;
                cases.Add(names[i], EdgePackingCase(input, Short2(input.X, input.Y), Short2(Vector2(input.X, input.Y)), raw));
            }
            packedTypes.Add("Short2", cases);
        }
        {
            auto cases = JsonWriter(); std::uint32_t state = EdgeSeed;
            for (int i = 0; i < std::size(names); ++i)
            {
                Vector4 input = EdgePackingInput(i, Vector4(1, 1, 1, 1), state);
                std::uint64_t raw = rawValues[i % std::size(rawValues)] & UINT64_MAX;
                cases.Add(names[i], EdgePackingCase(input, Short4(input.X, input.Y, input.Z, input.W), Short4(input), raw));
            }
            packedTypes.Add("Short4", cases);
        }
        auto colors = JsonWriter(); std::uint32_t colorState = EdgeSeed;
        for (int i = 0; i < std::size(names); ++i)
        {
            Vector4 input = EdgePackingInput(i, Vector4{255}, colorState);
            Color packed; packed.PackFromVector4(input);
            auto raw = Color{}; raw.setPackedValueProperty(static_cast<UInt32>(rawValues[i % std::size(rawValues)]));
            colors.Add(names[i], JsonWriter().Add("input", EdgeVector(input))
                .Add("vector4", EdgePackingColor(Color{input}))
                .Add("floats4", EdgePackingColor(Color{input.X, input.Y, input.Z, input.W}))
                .Add("vector3", EdgePackingColor(Color{Vector3{input.X, input.Y, input.Z}}))
                .Add("floats3", EdgePackingColor(Color{input.X, input.Y, input.Z}))
                .Add("pack", EdgePackingColor(packed)).Add("raw", EdgePackingColor(raw))
                .Add("nonpremultiplied", EdgePackingColor(Color::FromNonPremultiplied(input))));
        }
        auto integers = JsonWriter();
        int integerInputs[][4] = { {0, 1, 254, 255}, {-1, 0, 255, 256}, {1, 2, 3, 128},
            {32767, -32768, 256, 255}, {INT32_MIN, 0, INT32_MAX, 0}, {255, 254, 1, -1} };
        for (int i = 0; i < std::size(integerInputs); ++i)
        {
            int r = integerInputs[i][0], g = integerInputs[i][1], b = integerInputs[i][2], a = integerInputs[i][3];
            integers.Add("case" + std::to_string(i), JsonWriter().Add("r", r).Add("g", g).Add("b", b).Add("a", a)
                .Add("rgb", EdgePackingColor(Color{r, g, b})).Add("rgba", EdgePackingColor(Color{r, g, b, a}))
                .Add("nonpremultiplied", EdgePackingColor(Color::FromNonPremultiplied(r, g, b, a))));
        }
        auto interpolation = JsonWriter();
        float factors[] = { -1, -0.0f, 0, 0.1f, 0.5f, 0.99999f, 1, 2, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(), 1.0f / 131072, 3.0f / 131072, 0.99999994f };
        auto first = Color{255, 1, 0, 254}; auto second = Color{0, 254, 1, 0};
        for (int i = 0; i < std::size(factors) + 4; ++i)
        {
            float amount = i < std::size(factors) ? factors[i] : NextEdgeValue(colorState) / 8;
            auto item = JsonWriter(); EdgeScalar(item, "amount", amount);
            item.Add("lerp", EdgePackingColor(Color::Lerp(first, second, amount)))
                .Add("multiply", EdgePackingColor(Color::Multiply(first, amount)))
                .Add("operator", EdgePackingColor(first * amount));
            interpolation.Add("case" + std::to_string(i), item);
        }
        return JsonWriter().Add("seed", static_cast<double>(EdgeSeed)).Add("packedTypes", packedTypes).Add("colors", colors)
            .Add("integers", integers).Add("interpolation", interpolation);
    }

    Curve CurveBuild(const std::vector<Vector4>& keys, bool step, bool restoreOrder = true)
    {
        auto curve = Curve{};
        for (int i = 0; i < std::size(keys); ++i)
            curve.getKeysProperty().Add(CurveKey{keys[i].X, keys[i].Y, keys[i].Z, keys[i].W,
                step ? CurveContinuity::Step : CurveContinuity::Smooth});
        if (restoreOrder)
            for (int i = 0; i < std::size(keys); ++i)
                curve.getKeysProperty().setItemProperty(i, CurveKey{keys[i].X, keys[i].Y, keys[i].Z, keys[i].W,
                    step ? CurveContinuity::Step : CurveContinuity::Smooth});
        return curve;
    }

    JsonWriter CurveKeys(Curve curve)
    {
        auto result = JsonWriter{};
        for (int i = 0; i < curve.getKeysProperty().getCountProperty(); ++i)
        {
            auto key = curve.getKeysProperty()[i];
            auto item = JsonWriter{};
            EdgeScalar(item, "position", key.getPositionProperty()); EdgeScalar(item, "value", key.getValueProperty());
            EdgeScalar(item, "in", key.getTangentInProperty()); EdgeScalar(item, "out", key.getTangentOutProperty());
            item.Add("continuity", static_cast<int>(key.getContinuityProperty()));
            result.Add("k" + std::to_string(i), item);
        }
        return result;
    }

    JsonWriter CurveCase(const std::vector<Vector4>& keys, bool step)
    {
        auto curve = CurveBuild(keys, step);
        auto output = JsonWriter{}.Add("keys", CurveKeys(curve))
            .Add("insertionKeys", CurveKeys(CurveBuild(keys, step, false))).Add("count", curve.getKeysProperty().getCountProperty())
            .Add("constant", curve.getIsConstantProperty()).Add("defaultPre", static_cast<int>(curve.getPreLoopProperty())).Add("defaultPost", static_cast<int>(curve.getPostLoopProperty()));
        auto evaluations = JsonWriter{};
        float first = std::size(keys) == 0 ? 0 : keys[0].X;
        float span = std::size(keys) <= 1 ? 1 : keys[std::size(keys) - 1].X - first;
        float amounts[] = { -2, -1, -0.5f, -0.0f, 0, 0.25f, 0.5f, 1, 1.5f, 2, 3 };
        for (int pre = 0; pre < 5; ++pre)
        for (int post = 0; post < 5; ++post)
        {
            curve.setPreLoopProperty(static_cast<CurveLoopType>(pre)); curve.setPostLoopProperty(static_cast<CurveLoopType>(post));
            auto item = JsonWriter{}.Add("pre", static_cast<int>(curve.getPreLoopProperty())).Add("post", static_cast<int>(curve.getPostLoopProperty()));
            auto values = JsonWriter{};
            for (int i = 0; i <= std::size(amounts); ++i)
            {
                float position = i == std::size(amounts) ? std::numeric_limits<float>::quiet_NaN() : first + span * amounts[i];
                auto value = JsonWriter{}; EdgeScalar(value, "position", position); EdgeScalar(value, "value", curve.Evaluate(position));
                values.Add("p" + std::to_string(i), value);
            }
            evaluations.Add("pre" + std::to_string(pre) + "post" + std::to_string(post), item.Add("values", values));
        }
        auto tangents = JsonWriter{};
        for (int input = 0; input < 3; ++input)
        for (int outgoing = 0; outgoing < 3; ++outgoing)
        {
            auto both = CurveBuild(keys, step); both.ComputeTangents(static_cast<CurveTangent>(input), static_cast<CurveTangent>(outgoing));
            auto each = CurveBuild(keys, step);
            for (int i = 0; i < std::size(keys); ++i) each.ComputeTangent(i, static_cast<CurveTangent>(input), static_cast<CurveTangent>(outgoing));
            auto single = CurveBuild(keys, step); single.ComputeTangents(static_cast<CurveTangent>(input));
            auto singleEach = CurveBuild(keys, step);
            for (int i = 0; i < std::size(keys); ++i) singleEach.ComputeTangent(i, static_cast<CurveTangent>(input));
            tangents.Add("in" + std::to_string(input) + "out" + std::to_string(outgoing), JsonWriter{}.Add("both", CurveKeys(both))
                .Add("each", CurveKeys(each)).Add("single", CurveKeys(single)).Add("singleEach", CurveKeys(singleEach)));
        }
        try { curve.ComputeTangent(-1, CurveTangent::Flat); output.Add("negativeIndex", std::string("none")); }
        catch (const System::ArgumentOutOfRangeException&) { output.Add("negativeIndex", std::string("ArgumentOutOfRangeException")); }
        try { curve.ComputeTangent(static_cast<int>(std::size(keys)), CurveTangent::Flat, CurveTangent::Linear); output.Add("endIndex", std::string("none")); }
        catch (const System::ArgumentOutOfRangeException&) { output.Add("endIndex", std::string("ArgumentOutOfRangeException")); }
        return output.Add("evaluations", evaluations).Add("tangents", tangents);
    }

    JsonWriter DumpCurveEdges()
    {
        auto cases = JsonWriter{};
        cases.Add("empty", CurveCase(std::vector<Vector4>{}, false));
        cases.Add("single", CurveCase(std::vector<Vector4>{ Vector4{2, 7, 3, 4} }, false));
        cases.Add("smooth", CurveCase(std::vector<Vector4>{ Vector4{0, 1, 2, 3}, Vector4{1, -2, 4, 5}, Vector4{3, 5, 6, 7} }, false));
        cases.Add("stepShifted", CurveCase(std::vector<Vector4>{ Vector4{2, 1, 0, 0}, Vector4{4, 9, 0, 0} }, true));
        cases.Add("duplicateStart", CurveCase(std::vector<Vector4>{ Vector4{0, 0, 0, 0}, Vector4{0, 10, 0, 0}, Vector4{1, 20, 0, 0} }, false));
        cases.Add("allEqual", CurveCase(std::vector<Vector4>{ Vector4{0, 3, 1, 2}, Vector4{0, 9, 4, 5} }, false));
        cases.Add("tiny", CurveCase(std::vector<Vector4>{ Vector4{0, 7, 0, 0}, Vector4{1e-11f, 9, 0, 0} }, false));
        cases.Add("epsilon", CurveCase(std::vector<Vector4>{ Vector4{0, 7, 0, 0}, Vector4{0.00000011920928955078125f, 9, 0, 0} }, false));
        std::uint32_t state = EdgeSeed;
        for (int i = 0; i < 4; ++i)
        {
            auto keys = std::vector<Vector4>(3);
            for (int k = 0; k < std::size(keys); ++k)
                keys[k] = Vector4{static_cast<float>(k == 2 ? 3 : k), NextEdgeValue(state), NextEdgeValue(state), NextEdgeValue(state)};
            cases.Add("seeded" + std::to_string(i), CurveCase(keys, false));
        }
        return JsonWriter{}.Add("seed", static_cast<double>(EdgeSeed)).Add("cases", cases);
    }

}

int main(int argc, char** argv)
{
    std::string outputPath = argc > 1 ? argv[1] : "cna-reference-values.json";

    JsonWriter nonRenderingApis;
    nonRenderingApis.Add("Enums", DumpEnums());
    nonRenderingApis.Add("StatePresets", DumpStatePresets());

    JsonWriter root;
    root.Add("NonRenderingApis", nonRenderingApis);
    root.Add("PackedVector", DumpPackedVector());
    root.Add("Viewport", DumpViewport());
    root.Add("Vector2Edges", DumpVector2Edges());
    root.Add("Vector3Edges", DumpVector3Edges());
    root.Add("Vector4Edges", DumpVector4Edges());
    root.Add("QuaternionEdges", DumpQuaternionEdges());
    root.Add("MatrixEdges", DumpMatrixEdges());
    root.Add("GeometryEdges", DumpGeometryEdges());
    root.Add("PackingEdges", DumpPackingEdges());
    root.Add("CurveEdges", DumpCurveEdges());
    root.Add("MatrixPerspectiveEdges", DumpMatrixPerspectiveEdges());

    std::string json = root.ToString();

    std::ofstream file(outputPath);
    file << json;
    file.close();

    std::printf("Wrote %s\n", outputPath.c_str());
    return 0;
}
