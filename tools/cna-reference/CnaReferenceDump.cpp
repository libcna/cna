// SPDX-License-Identifier: MS-PL
// Task 479 (plans/plan_graphics.md Phase 53): CNA-side reference-value dump tool.
//
// Mirrors tools/fna-reference/{NonRenderingApiReference,PackedVectorReference,
// ViewportReference}.cs exactly -- same enum list, same state-preset list, same PackedVector
// input values, same Viewport cases -- so scripts/compare-fna-reference.py can diff this tool's
// JSON output against the real FNA harness's output key-for-key. None of these 4 categories need
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
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include "JsonWriter.hpp"

#include <cmath>
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

    std::string json = root.ToString();

    std::ofstream file(outputPath);
    file << json;
    file.close();

    std::printf("Wrote %s\n", outputPath.c_str());
    return 0;
}
