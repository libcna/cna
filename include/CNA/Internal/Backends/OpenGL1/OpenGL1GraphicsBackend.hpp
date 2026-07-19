#pragma once
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>


namespace CNA::Internal::Backends::OpenGL1 {
class OpenGL1VertexBufferBackend final : public IVertexBufferBackend {
public:
 explicit OpenGL1VertexBufferBackend(int capacity):capacity_(capacity){}
 void SetData(const void*,int,std::size_t) override;
 void SetDataWithOptions(const void* d,int c,std::size_t s,SetDataOptions) override {SetData(d,c,s);} 
 int GetVertexCount() const override{return count_;}
 const std::vector<std::uint8_t>& Data() const{return data_;} std::size_t Stride()const{return stride_;}
private:int capacity_=0,count_=0;std::size_t stride_=0;std::vector<std::uint8_t> data_;
};
class OpenGL1IndexBufferBackend final : public IIndexBufferBackend {
public: explicit OpenGL1IndexBufferBackend(bool i32):i32_(i32){}
 void SetData16(const void*,int) override; void SetData32(const void*,int) override;
 int GetIndexCount()const override{return count_;} bool IsThirtyTwoBit()const override{return i32_;}
 const std::vector<std::uint8_t>& Data()const{return data_;}
private:bool i32_=false;int count_=0;std::vector<std::uint8_t> data_;
};
class OpenGL1TextureBackend final : public ITextureBackend {
public: explicit OpenGL1TextureBackend(const ImageData&); ~OpenGL1TextureBackend() override;
 int GetWidth()const override{return width_;} int GetHeight()const override{return height_;} SDL_Texture* GetNativeTexture()const override{return nullptr;}
 void UpdatePixels(const uint8_t*,int) override; void UpdatePixelsLevel(int,const uint8_t*,int,int) override; void BindGL()const override;
 unsigned int Id()const{return id_;}
private:unsigned int id_=0;int width_=0,height_=0;
};
class OpenGL1SpriteBatchBackend final : public ISpriteBatchBackend {
public: explicit OpenGL1SpriteBatchBackend(class OpenGL1GraphicsBackend& o):owner_(o){}
 void Begin()override;void End()override;void SetTransformMatrix(const Matrix& m)override{transform_=m;}
 void SetSamplerFilter(int f)override{filter_=f;} void SetSamplerAddressMode(int u,int v)override{u_=u;v_=v;}
 void Draw(const ITextureBackend&,float,float)override;
 void Draw(const ITextureBackend&,const Rectangle&,const Rectangle&,const Color&)override;
 void Draw(const ITextureBackend&,const Rectangle&,const Rectangle&,const Color&,float,const Vector2&,SpriteEffects,float)override;
private:OpenGL1GraphicsBackend& owner_;bool begun_=false;Matrix transform_=Matrix::getIdentityProperty();int filter_=0,u_=1,v_=1;
};
class OpenGL1GraphicsBackend final : public IGraphicsBackend {
public: explicit OpenGL1GraphicsBackend(const GraphicsBackendCreateArgs&);~OpenGL1GraphicsBackend()override;
 void Clear(float,float,float,float)override;void Present()override;void GetViewportSize(int&,int&)override;void SetVirtualResolution(int,int)override;void SetPresentationMode(int)override;
 SDL_Window* GetWindowInternal()const override{return window_;} SDL_Renderer* GetRendererInternal()const override{return nullptr;}
 std::unique_ptr<ITextureBackend>CreateTexture(const ImageData&)override;std::unique_ptr<ISpriteBatchBackend>CreateSpriteBatch()override;
 void ApplyBlendState(int,int,int,int,int,int)override;void ApplyDepthStencilState(bool,bool,int,bool,int,int,int,int,int,int,int,bool,int,int,int,int)override;
 void ApplyRasterizerState(int,int,bool,float,float)override;void ApplySamplerState(int,int,int,int,int)override;void SetBlendFactor(float,float,float,float)override;void SetReferenceStencil(int)override;
 void SetScissorRect(int,int,int,int)override;void SetViewport(int,int,int,int,float,float)override;
 void ClearColorAndDepth(float,float,float,float,float)override;void ClearDepth(float)override;void ClearStencil(int)override;void ClearDepthAndStencil(float,int)override;void ClearColorAndStencil(float,float,float,float,int)override;void ClearColorDepthAndStencil(float,float,float,float,float,int)override;
 void SetDepthTestEnabled(bool)override;void SetBlendEnabled(bool)override;void SetDepthWriteEnabled(bool)override;
 std::unique_ptr<IVertexBufferBackend>CreateVertexBuffer(int)override;std::unique_ptr<IIndexBufferBackend>CreateIndexBuffer16(int)override;std::unique_ptr<IIndexBufferBackend>CreateIndexBuffer32(int)override;
 void DrawColoredPrimitives(const IVertexBufferBackend&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int)override;
 void DrawIndexedColoredPrimitives(const IVertexBufferBackend&,const IIndexBufferBackend&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int)override;
 void DrawPrimitivesEx(const IVertexBufferBackend&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int,const GpuDrawParams&)override;
 void DrawIndexedPrimitivesEx(const IVertexBufferBackend&,const IIndexBufferBackend&,const Matrix&,const Matrix&,const Matrix&,PrimitiveType,int,const GpuDrawParams&)override;
 bool SupportsCapability(CNA::GraphicsCapability capability)const override;
 int VirtualWidth()const{return virtualWidth_;}int VirtualHeight()const{return virtualHeight_;}
private:void SetupMatrices(const Matrix&,const Matrix&,const Matrix&);void DrawInternal(const OpenGL1VertexBufferBackend&,const OpenGL1IndexBufferBackend*,PrimitiveType,int,const GpuDrawParams*);
 SDL_Window* window_=nullptr;void* glContext_=nullptr;int virtualWidth_=0,virtualHeight_=0;int stencilRef_=0;
};
}
