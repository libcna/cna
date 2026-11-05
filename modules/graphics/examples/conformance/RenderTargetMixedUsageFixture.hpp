// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Tests::MixedUsage
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    inline constexpr std::array usages{RenderTargetUsage::DiscardContents,
        RenderTargetUsage::PreserveContents, RenderTargetUsage::PlatformContents};

    template<class Target, class Check>
    void Expect2D(Target& target, const Color& want, Check check, const std::string& label)
    {
        std::array<Color, 16> pixels{};
        target.GetData(pixels.data(), 16);
        for (std::size_t i = 0; i < pixels.size(); ++i)
            check(pixels[i].getPackedValueProperty() == want.getPackedValueProperty(),
                  label + " pixel=" + std::to_string(i));
    }

    template<class Check>
    void FourSlots(GraphicsDevice& device, Check check, int samples = 0)
    {
        // Refresh SetRenderTargets IL_0393-03b8 and ClearDirtyBuffers IL_0052-00f0:
        // each Discard slot becomes exactly (68,34,136,255); Preserve and Platform retain
        // their independently painted RGBA bytes. 3^4=81 includes every ordered usage set.
        const std::array painted{Color(33,67,101,255), Color(50,79,113,255),
                                 Color(71,91,127,255), Color(89,109,149,255)};
        for (int row = 0; row < 81; ++row)
        {
            int digits = row;
            std::array<RenderTargetUsage,4> chosen{};
            std::array<std::unique_ptr<RenderTarget2D>,4> targets;
            std::vector<RenderTargetBinding> bindings;
            for (int slot = 0; slot < 4; ++slot)
            {
                chosen[slot] = usages[digits % 3]; digits /= 3;
                targets[slot] = std::make_unique<RenderTarget2D>(device,4,4,false,
                    SurfaceFormat::Color,DepthFormat::None,samples,chosen[slot]);
                check(targets[slot]->getMultiSampleCountProperty() == samples,
                      "matrix applied samples");
                device.SetRenderTarget(targets[slot].get());
                device.Clear(painted[slot]);
                bindings.emplace_back(targets[slot].get());
            }
            device.SetRenderTargets({});
            device.SetRenderTargets(bindings);
            const auto bound = device.GetRenderTargets();
            check(bound.size() == 4, "four bindings remain active after selective discard");
            if (bound.size() == 4)
                for (int slot = 0; slot < 4; ++slot)
                    check(bound[slot].getRenderTargetProperty() == targets[slot].get(),
                          "binding order survives selective discard");
            device.SetRenderTargets({});
            for (int slot = 0; slot < 4; ++slot)
                Expect2D(*targets[slot], chosen[slot] == RenderTargetUsage::DiscardContents
                    ? Color(68,34,136,255) : painted[slot], check,
                    "usage row=" + std::to_string(row) + " slot=" + std::to_string(slot));
        }
    }

    template<class Check>
    void CubeFaces(GraphicsDevice& device, Check check, int samples = 0)
    {
        // The same per-slot oracle; ClearDirtyBuffers IL_0068-007b selects the BOUND
        // cube face, not face +X or the entire cube. Exercise cube in every slot.
        for (auto cubeUsage : usages) for (auto flatUsage : usages) for (int cubeSlot=0; cubeSlot<4; ++cubeSlot)
        {
            RenderTargetCube cube(device,4,false,SurfaceFormat::Color,DepthFormat::None,samples,cubeUsage);
            RenderTarget2D flat(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,samples,flatUsage);
            check(cube.getMultiSampleCountProperty()==samples && flat.getMultiSampleCountProperty()==samples,
                  "cube pair applied samples");
            for (int face=0; face<6; ++face)
            {
                device.SetRenderTarget(&cube,static_cast<CubeMapFace>(face));
                device.Clear(Color(17+face,83+face,149+face,255));
            }
            device.SetRenderTarget(&flat); device.Clear(Color(33,67,101,255));
            device.SetRenderTargets({});
            std::vector<RenderTargetBinding> bindings{RenderTargetBinding(&cube,CubeMapFace::NegativeZ),
                                                       RenderTargetBinding(&flat)};
            RenderTarget2D extra1(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,samples,flatUsage);
            RenderTarget2D extra2(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,samples,flatUsage);
            for(auto* extra : {&extra1,&extra2})
            { device.SetRenderTarget(extra); device.Clear(Color(33,67,101,255)); }
            device.SetRenderTargets({});
            bindings.emplace_back(&extra1); bindings.emplace_back(&extra2);
            std::swap(bindings[0],bindings[cubeSlot]);
            device.SetRenderTargets(bindings); device.SetRenderTargets({});
            Expect2D(flat,flatUsage==RenderTargetUsage::DiscardContents
                ? Color(68,34,136,255) : Color(33,67,101,255),check,"mixed cube neighbor");
            for(auto* extra : {&extra1,&extra2})
                Expect2D(*extra,flatUsage==RenderTargetUsage::DiscardContents
                    ? Color(68,34,136,255) : Color(33,67,101,255),check,"extra mixed cube neighbor");
            for (int face=0; face<6; ++face)
            {
                std::array<Color,16> pixels{};
                cube.GetData(static_cast<CubeMapFace>(face),pixels.data(),16);
                const Color want=face==5 && cubeUsage==RenderTargetUsage::DiscardContents
                    ? Color(68,34,136,255) : Color(17+face,83+face,149+face,255);
                for (const auto pixel : pixels)
                    check(pixel.getPackedValueProperty()==want.getPackedValueProperty(),
                          "mixed cube face="+std::to_string(face)+" slot="+std::to_string(cubeSlot));
            }
        }
    }

    template<class Check>
    void PublicClearAndEqualBinding(GraphicsDevice& device, Check check)
    {
        RenderTarget2D a(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,0,usages[0]);
        RenderTarget2D b(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,0,usages[1]);
        const std::vector<RenderTargetBinding> set{RenderTargetBinding(&a),RenderTargetBinding(&b)};
        device.SetRenderTargets(set);
        // Public Clear still clears BOTH colors, then an equal binding is a complete no-op
        // (Refresh SetRenderTargets IL_0015-0083). Neither operation may reapply discard.
        device.Clear(ClearOptions::Target,Color(23,47,199,255),1.0f,0);
        device.SetRenderTargets(set); device.SetRenderTargets({});
        Expect2D(a,Color(23,47,199,255),check,"public clear discard slot");
        Expect2D(b,Color(23,47,199,255),check,"public clear preserve slot");
    }

    inline void DrawProbe(GraphicsDevice& device, float z, const Color& color, bool configureState=true)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled=true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const VertexPositionColor quad[]{
            {Vector3(-1,1,z),color},{Vector3(-1,-1,z),color},{Vector3(1,-1,z),color},
            {Vector3(-1,1,z),color},{Vector3(1,-1,z),color},{Vector3(1,1,z),color}};
        if(configureState)
        {
            device.setBlendStateProperty(BlendState::Opaque);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
        }
        device.DrawUserPrimitives(PrimitiveType::TriangleList,quad,0,2);
    }

    template<class Check>
    void DepthStencilOwnership(GraphicsDevice& device, Check check)
    {
        // Refresh binds pDepthSurface only from slot 0 (SetRenderTargets IL_0370-0392).
        // Only a discarding slot 0 sets depth/stencil flags (IL_03ad-03b8);
        // ClearDirtyBuffers IL_00fe-0114 clears those aspects once to depth=1, stencil=0.
        for (auto firstUsage : usages) for (auto secondUsage : usages)
        for (bool stencilProbe : {false,true})
        {
            RenderTarget2D a(device,4,4,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,0,firstUsage);
            RenderTarget2D b(device,4,4,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,0,secondUsage);
            device.SetRenderTarget(&a); device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,Color(33,67,101,255),0.25f,9);
            device.SetRenderTarget(&b); device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,Color(50,79,113,255),0.75f,13);
            device.SetRenderTargets({});
            device.SetRenderTargets({RenderTargetBinding(&a),RenderTargetBinding(&b)});
            // A depth-only clear resets neither color. The probe writes output 0 only.
            DepthStencilState state;
            state.setDepthBufferEnableProperty(!stencilProbe);
            state.setDepthBufferWriteEnableProperty(false);
            state.setDepthBufferFunctionProperty(CompareFunction::Less);
            state.setStencilEnableProperty(stencilProbe);
            state.setStencilFunctionProperty(CompareFunction::Equal);
            state.setReferenceStencilProperty(0);
            state.setStencilPassProperty(StencilOperation::Keep);
            device.setDepthStencilStateProperty(state);
            DrawProbe(device,0.5f,Color(0,255,0,255));
            device.SetRenderTargets({});
            // 0.5<1 passes, 0.5<0.25 fails; stencil 0==0 passes, 9==0 fails.
            const Color want=firstUsage==RenderTargetUsage::DiscardContents ? Color(0,255,0,255)
                : Color(33,67,101,255);
            Expect2D(a,want,check,stencilProbe ? "slot0 stencil ownership" : "slot0 depth ownership");
            Expect2D(b,secondUsage==RenderTargetUsage::DiscardContents ? Color(68,34,136,255)
                : Color(50,79,113,255),check,"probe leaves slot1 color intact");
        }
        device.setDepthStencilStateProperty(DepthStencilState::Default);
    }

    template<class Check>
    void MaskedState(GraphicsDevice& device, Check check)
    {
        RenderTarget2D a(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,0,usages[0]);
        RenderTarget2D b(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,0,usages[1]);
        device.SetRenderTarget(&b); device.Clear(Color(33,67,101,255));
        device.SetRenderTarget(&a); device.Clear(Color(50,79,113,255));
        BlendState masked;
        masked.setColorWriteChannelsProperty(ColorWriteChannels::None);
        masked.setColorWriteChannels1Property(ColorWriteChannels::None);
        masked.setColorWriteChannels2Property(ColorWriteChannels::None);
        masked.setColorWriteChannels3Property(ColorWriteChannels::None);
        device.setBlendStateProperty(masked);
        RasterizerState clipped;
        clipped.setScissorTestEnableProperty(true);
        clipped.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(clipped);
        device.setScissorRectangleProperty(Rectangle(1,1,1,1));
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawProbe(device,0.5f,Color(0,255,0,255),false); // Installs real native restricted masks.
        device.SetRenderTargets({RenderTargetBinding(&a),RenderTargetBinding(&b)});
        check(device.getBlendStateProperty().getColorWriteChannelsProperty()==ColorWriteChannels::None,
              "discard preserves caller blend mask");
        check(device.getRasterizerStateProperty().getScissorTestEnableProperty(),
              "discard preserves caller scissor-enable");
        const auto viewport=device.getViewportProperty();
        check(viewport.getXProperty()==0 && viewport.getYProperty()==0 &&
              viewport.getWidthProperty()==4 && viewport.getHeightProperty()==4,
              "discard preserves the post-bind viewport");
        DrawProbe(device,0.5f,Color(0,255,0,255),false); // Native masks must still suppress this draw.
        device.SetRenderTargets({});
        // Clear ignores ColorWriteChannels and covers the whole surface (Refresh's ColorFill,
        // ClearDirtyBuffers IL_00bc-00d1). Viewport reset: SetRenderTargets IL_04a6-04d1.
        // Do not assert FNA's scissor-rectangle reset as a Refresh requirement.
        Expect2D(a,Color(68,34,136,255),check,"masked discard covers all channels and pixels");
        Expect2D(b,Color(33,67,101,255),check,"masked discard preserves neighbor");
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    template<class Check>
    void CubeDepthStencilOwnership(GraphicsDevice& device, Check check)
    {
        // Same Refresh slot 0-only depth/stencil oracle as DepthStencilOwnership, now both
        // descriptor arms (2D first / cube first). All probe depths are exact binary32 values.
        for(auto firstUsage : usages) for(auto secondUsage : usages)
        for(bool cubeFirst : {false,true}) for(bool stencilProbe : {false,true})
        {
            RenderTargetCube cube(device,4,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,
                                  0,cubeFirst?firstUsage:secondUsage);
            RenderTarget2D flat(device,4,4,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,
                                0,cubeFirst?secondUsage:firstUsage);
            device.SetRenderTarget(&cube,CubeMapFace::NegativeZ);
            device.Clear(ClearOptions::Target|ClearOptions::DepthBuffer|ClearOptions::Stencil,
                cubeFirst?Color(33,67,101,255):Color(50,79,113,255),cubeFirst?0.25f:0.75f,cubeFirst?9:13);
            device.SetRenderTarget(&flat);
            device.Clear(ClearOptions::Target|ClearOptions::DepthBuffer|ClearOptions::Stencil,
                cubeFirst?Color(50,79,113,255):Color(33,67,101,255),cubeFirst?0.75f:0.25f,cubeFirst?13:9);
            device.SetRenderTargets({});
            std::vector<RenderTargetBinding> set{RenderTargetBinding(&cube,CubeMapFace::NegativeZ),
                                                RenderTargetBinding(&flat)};
            if(!cubeFirst) std::swap(set[0],set[1]);
            device.SetRenderTargets(set);
            DepthStencilState state;
            state.setDepthBufferEnableProperty(!stencilProbe);
            state.setDepthBufferWriteEnableProperty(false);
            state.setDepthBufferFunctionProperty(CompareFunction::Less);
            state.setStencilEnableProperty(stencilProbe);
            state.setStencilFunctionProperty(CompareFunction::Equal);
            state.setReferenceStencilProperty(0);
            state.setStencilPassProperty(StencilOperation::Keep);
            device.setDepthStencilStateProperty(state);
            DrawProbe(device,0.5f,Color(0,255,0,255));
            device.SetRenderTargets({});
            const Color first=firstUsage==RenderTargetUsage::DiscardContents ? Color(0,255,0,255)
                                                                           : Color(33,67,101,255);
            const Color second=secondUsage==RenderTargetUsage::DiscardContents ? Color(68,34,136,255)
                                                                             : Color(50,79,113,255);
            Expect2D(flat,cubeFirst?second:first,check,"mixed depth flat");
            std::array<Color,16> pixels{};
            cube.GetData(CubeMapFace::NegativeZ,pixels.data(),16);
            for(const auto pixel:pixels)
                check(pixel.getPackedValueProperty()==(cubeFirst?first:second).getPackedValueProperty(),
                      "mixed depth cube");
        }
        device.setDepthStencilStateProperty(DepthStencilState::Default);
    }
}
