#include "CNA/Internal/Renderers/OpenGL1/OpenGL1Renderer.hpp"
#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace CNA::Internal::Renderers;
namespace CNA::Internal::Renderers::OpenGL1 {
namespace {
GLenum Prim(PrimitiveType p){switch(p){case PrimitiveType::TriangleList:return GL_TRIANGLES;case PrimitiveType::TriangleStrip:return GL_TRIANGLE_STRIP;case PrimitiveType::LineList:return GL_LINES;case PrimitiveType::LineStrip:return GL_LINE_STRIP;default:return GL_POINTS;}}
int VertCount(PrimitiveType p,int n){switch(p){case PrimitiveType::TriangleList:return n*3;case PrimitiveType::TriangleStrip:return n+2;case PrimitiveType::LineList:return n*2;case PrimitiveType::LineStrip:return n+1;default:return n;}}
GLenum Cmp(int v){static const GLenum a[]={GL_ALWAYS,GL_NEVER,GL_LESS,GL_LEQUAL,GL_EQUAL,GL_GEQUAL,GL_GREATER,GL_NOTEQUAL};return (v>=0&&v<8)?a[v]:GL_ALWAYS;}
// plan_opengl1.md item 17 (EasyGL parity): indices 10/11 are Blend.BlendFactor/InverseBlendFactor
// (constant blend color) -- GL_ONE/GL_ZERO here is a fallback for a driver without extendedBlend
// (GL_CONSTANT_COLOR/GL_ONE_MINUS_CONSTANT_COLOR are themselves core-1.4-gated enum tokens, not
// valid to pass to glBlendFunc on an older driver), NOT the correct answer when it's available.
GLenum BlendF(int v,bool extendedBlend){static const GLenum a[]={GL_ONE,GL_ZERO,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA,GL_ONE,GL_ZERO,GL_SRC_ALPHA_SATURATE};
 if(extendedBlend){if(v==10)return GL_CONSTANT_COLOR;if(v==11)return GL_ONE_MINUS_CONSTANT_COLOR;}
 return(v>=0&&v<13)?a[v]:GL_ONE;}
// plan_opengl1.md item 18 (EasyGL parity): Blend.BlendFunction ordinals: Add=0,Subtract=1,
// ReverseSubtract=2,Max=3,Min=4.
GLenum BlendEq(int v){switch(v){case 1:return GL_FUNC_SUBTRACT;case 2:return GL_FUNC_REVERSE_SUBTRACT;case 3:return GL_MAX;case 4:return GL_MIN;default:return GL_FUNC_ADD;}}
GLenum StencilOp(int v){static const GLenum a[]={GL_KEEP,GL_ZERO,GL_REPLACE,GL_INCR,GL_DECR,GL_INCR,GL_DECR,GL_INVERT};return(v>=0&&v<8)?a[v]:GL_KEEP;}
// plan_opengl1.md item 16 (EasyGL parity): TextureAddressMode -> GL wrap mode. XNA ordinals:
// Wrap=0, Clamp=1, Mirror=2. GL_MIRRORED_REPEAT is core GL 1.4 (2002), same era as GL_CLAMP_TO_EDGE
// (core 1.2) already used unconditionally here -- no capability gate needed.
GLenum WrapMode(int v){switch(v){case 0:return GL_REPEAT;case 2:return GL_MIRRORED_REPEAT;default:return GL_CLAMP_TO_EDGE;}}
// plan_opengl1.md phase 6: TextureFilter -> GL min/mag filter, same mapping EasyGLRenderer
// already uses (its own ApplySamplerState) -- XNA ordinals: Linear=0, Point=1, Anisotropic=2,
// LinearMipPoint=3, PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
// MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8. `mip` selects whether the MIN filter
// uses a _MIPMAP_ variant at all -- the caller only passes true when the texture about to be
// sampled genuinely has more than one level (OpenGL1TextureRenderer::HasMips()), since requesting
// a mipmap MIN_FILTER against a texture with only level 0 makes it GL-incomplete.
void MinMagFilter(int filter,bool mip,GLenum&minF,GLenum&magF){
 switch(filter){
  case 1: minF=GL_NEAREST; magF=GL_NEAREST; break; // Point
  case 2: minF=mip?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR; magF=GL_LINEAR; break; // Anisotropic
  case 3: minF=mip?GL_LINEAR_MIPMAP_NEAREST:GL_LINEAR; magF=GL_LINEAR; break; // LinearMipPoint
  case 4: minF=mip?GL_NEAREST_MIPMAP_LINEAR:GL_NEAREST; magF=GL_NEAREST; break; // PointMipLinear
  case 5: minF=mip?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR; magF=GL_NEAREST; break; // MinLinearMagPointMipLinear
  case 6: minF=mip?GL_LINEAR_MIPMAP_NEAREST:GL_LINEAR; magF=GL_NEAREST; break; // MinLinearMagPointMipPoint
  case 7: minF=mip?GL_NEAREST_MIPMAP_LINEAR:GL_NEAREST; magF=GL_LINEAR; break; // MinPointMagLinearMipLinear
  case 8: minF=mip?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST; magF=GL_LINEAR; break; // MinPointMagLinearMipPoint
  default: minF=GL_LINEAR; magF=GL_LINEAR; break; // Linear
 }
}
void Mat(const Matrix&m,float o[16]){o[0]=m.M11;o[1]=m.M12;o[2]=m.M13;o[3]=m.M14;o[4]=m.M21;o[5]=m.M22;o[6]=m.M23;o[7]=m.M24;o[8]=m.M31;o[9]=m.M32;o[10]=m.M33;o[11]=m.M34;o[12]=m.M41;o[13]=m.M42;o[14]=m.M43;o[15]=m.M44;}
void Color4(const Color&c){glColor4ub((GLubyte)c.getRProperty(),(GLubyte)c.getGProperty(),(GLubyte)c.getBProperty(),(GLubyte)c.getAProperty());}
// plan_opengl1.md phase 3: ARB_multitexture/core-1.3 entry points for DualTextureEffect's
// second texture unit -- glActiveTexture/glMultiTexCoord2f are not part of the GL 1.1 core
// export table (notably on Windows' frozen opengl32.dll), so they need portable loading via
// SDL_GL_GetProcAddress, same reasoning as OpenGL1RenderTargetRenderer's FBO functions. Locally
// named typedefs (not the PFNGL...PROC ones SDL3's own GL headers declare) to sidestep any
// ambiguity in whether those are visible as typedefs vs. direct GL_VERSION_1_3 function
// declarations depending on this platform's exact header configuration.
typedef void (APIENTRY *CnaPFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRY *CnaPFNGLMULTITEXCOORD2FPROC)(GLenum target,GLfloat s,GLfloat t);
CnaPFNGLACTIVETEXTUREPROC glActiveTexture_=nullptr;
CnaPFNGLMULTITEXCOORD2FPROC glMultiTexCoord2f_=nullptr;
bool TryLoadMultitextureFunctions(){
 glActiveTexture_=reinterpret_cast<CnaPFNGLACTIVETEXTUREPROC>(SDL_GL_GetProcAddress("glActiveTexture"));
 glMultiTexCoord2f_=reinterpret_cast<CnaPFNGLMULTITEXCOORD2FPROC>(SDL_GL_GetProcAddress("glMultiTexCoord2f"));
 return glActiveTexture_&&glMultiTexCoord2f_;
}
// plan_opengl1.md phase 6: glGenerateMipmap is part of the same ARB_framebuffer_object/core-3.0
// entry-point family the FBO RenderTarget2D support (phase 2) already loads -- a separate,
// dedicated loader here rather than folding it into TryLoadOpenGL1FramebufferObjectFunctions()
// (OpenGL1RenderTargetRenderer.cpp) so a driver missing full FBO support but still exposing
// glGenerateMipmap (rare, but not impossible) isn't penalized, and so RenderTarget2D's own
// capability gating stays independent of texture mipmap generation's.
// plan_opengl1.md items 17/18/19 (EasyGL parity): glBlendColor/glBlendFuncSeparate/
// glBlendEquationSeparate are all core GL 1.4 (2002) -- the same core version bundles constant
// blend color, separate color/alpha blend factors, and blend equations beyond additive together,
// so a single capability flag (OpenGL1Capabilities::extendedBlend) and a single loader cover all
// three items. Same locally-named-typedef reasoning as the multitexture loader above.
typedef void (APIENTRY *CnaPFNGLBLENDCOLORPROC)(GLfloat,GLfloat,GLfloat,GLfloat);
typedef void (APIENTRY *CnaPFNGLBLENDFUNCSEPARATEPROC)(GLenum,GLenum,GLenum,GLenum);
typedef void (APIENTRY *CnaPFNGLBLENDEQUATIONSEPARATEPROC)(GLenum,GLenum);
CnaPFNGLBLENDCOLORPROC glBlendColor_=nullptr;
CnaPFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate_=nullptr;
CnaPFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate_=nullptr;
bool TryLoadBlendFunctions(){
 glBlendColor_=reinterpret_cast<CnaPFNGLBLENDCOLORPROC>(SDL_GL_GetProcAddress("glBlendColor"));
 glBlendFuncSeparate_=reinterpret_cast<CnaPFNGLBLENDFUNCSEPARATEPROC>(SDL_GL_GetProcAddress("glBlendFuncSeparate"));
 glBlendEquationSeparate_=reinterpret_cast<CnaPFNGLBLENDEQUATIONSEPARATEPROC>(SDL_GL_GetProcAddress("glBlendEquationSeparate"));
 return glBlendColor_&&glBlendFuncSeparate_&&glBlendEquationSeparate_;
}
typedef void (APIENTRY *CnaPFNGLGENERATEMIPMAPPROC)(GLenum target);
CnaPFNGLGENERATEMIPMAPPROC glGenerateMipmap_=nullptr;
bool TryLoadGenerateMipmapFunction(){
 glGenerateMipmap_=reinterpret_cast<CnaPFNGLGENERATEMIPMAPPROC>(SDL_GL_GetProcAddress("glGenerateMipmap"));
 return glGenerateMipmap_!=nullptr;
}
// CPU box-filter fallback for drivers with neither glGenerateMipmap nor GL_GENERATE_MIPMAP/
// SGIS_generate_mipmap (a strict GL 1.1-1.3 driver older than 1999's SGIS extension -- expected
// to be effectively unreachable on any real hardware/driver from this decade, but plan_opengl1.md
// phase 6 explicitly asks for a CPU fallback, not just "assume the extension exists"). 2x2 box
// average per level, clamped to source bounds so odd dimensions degrade gracefully instead of
// reading out of bounds.
void GenerateMipsCPU(GLenum target,int w,int h,const uint8_t*level0){
 std::vector<uint8_t> prev(level0,level0+(size_t)w*h*4);
 int level=1;
 while(w>1||h>1){
  int nw=std::max(1,w/2),nh=std::max(1,h/2);
  std::vector<uint8_t> next((size_t)nw*nh*4);
  for(int y=0;y<nh;++y)for(int x=0;x<nw;++x){
   int x0=std::min(x*2,w-1),x1=std::min(x*2+1,w-1);
   int y0=std::min(y*2,h-1),y1=std::min(y*2+1,h-1);
   for(int c=0;c<4;++c){
    const int sum=prev[((size_t)y0*w+x0)*4+c]+prev[((size_t)y0*w+x1)*4+c]+prev[((size_t)y1*w+x0)*4+c]+prev[((size_t)y1*w+x1)*4+c];
    next[((size_t)y*nw+x)*4+c]=(uint8_t)(sum/4);
   }
  }
  glTexImage2D(target,level,GL_RGBA,nw,nh,0,GL_RGBA,GL_UNSIGNED_BYTE,next.data());
  prev=std::move(next);w=nw;h=nh;++level;
 }
}
// plan_opengl1.md item 21 (EasyGL parity): a RenderTarget2D is never an OpenGL1TextureRenderer
// (separate class -- GPU-produced content, no CPU shadow), so the plain dynamic_cast<const
// OpenGL1TextureRenderer*> every sampling call site already used to gate mip-aware filtering
// always came back null for one, silently forcing mip=false even after
// OpenGL1RenderTargetRenderer::UnbindAsRenderTarget() genuinely generated a complete chain.
// Centralizes both checks so BasicEffect/DualTextureEffect 3D sampling and SpriteBatch::Draw all
// honor a render target's real HasMips() the same way they already honor a Texture2D's.
bool HasMipsFor(const ITextureRenderer*t){
 if(!t)return false;
 if(const auto*tex=dynamic_cast<const OpenGL1TextureRenderer*>(t))return tex->HasMips();
 if(const auto*rt=dynamic_cast<const OpenGL1RenderTargetRenderer*>(t))return rt->HasMips();
 return false;
}
}
void OpenGL1VertexBufferRenderer::SetData(const void*d,int c,std::size_t s){if(c<0||c>capacity_)throw std::runtime_error("OPENGL1 vertex count exceeds capacity");count_=c;stride_=s;data_.resize((size_t)c*s);if(d&&!data_.empty())std::memcpy(data_.data(),d,data_.size());}
void OpenGL1IndexBufferRenderer::SetData16(const void*d,int c){i32_=false;count_=c;data_.resize((size_t)c*2);if(d&&c)std::memcpy(data_.data(),d,data_.size());}
void OpenGL1IndexBufferRenderer::SetData32(const void*d,int c){i32_=true;count_=c;data_.resize((size_t)c*4);if(d&&c)std::memcpy(data_.data(),d,data_.size());}
OpenGL1TextureRenderer::OpenGL1TextureRenderer(const ImageData&d,OpenGL1ResourceRegistry*registry,bool generateMipmapCap):width_(d.width),height_(d.height),registry_(registry),mipMap_(d.mipLevels>1),generateMipmapCap_(generateMipmapCap){glGenTextures(1,&id_);glBindTexture(GL_TEXTURE_2D,id_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
// plan_opengl1.md phase 6: GL_GENERATE_MIPMAP must be set BEFORE the level-0 image is specified
// -- the driver then regenerates every level as a side effect of glTexImage2D/glTexSubImage2D on
// level 0. Only used when there is no glGenerateMipmap (an explicit call after upload, in
// RegenerateMips below, is preferred when available -- works identically on core-profile drivers
// where GL_GENERATE_MIPMAP itself was removed).
if(mipMap_&&!glGenerateMipmap_&&generateMipmapCap_)glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width_,height_,0,GL_RGBA,GL_UNSIGNED_BYTE,d.pixels.empty()?nullptr:d.pixels.data());
RegenerateMips(d.pixels.empty()?nullptr:d.pixels.data());
if(registry_)registry_->Add(this);}
// plan_opengl1.md phase 8: re-runs the exact same upload the constructor does, from whatever
// CPU pixel data Texture2D last shared via ShareCpuPixels() -- nullptr (a blank texture of the
// right size) only if context recovery was disabled and no CPU shadow was ever kept.
void OpenGL1TextureRenderer::RecreateGLResource(){glGenTextures(1,&id_);glBindTexture(GL_TEXTURE_2D,id_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);if(mipMap_&&!glGenerateMipmap_&&generateMipmapCap_)glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);const void*px=(cpuPixels_&&!cpuPixels_->empty())?cpuPixels_->data():nullptr;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width_,height_,0,GL_RGBA,GL_UNSIGNED_BYTE,px);RegenerateMips(static_cast<const uint8_t*>(px));}
OpenGL1TextureRenderer::~OpenGL1TextureRenderer(){if(registry_)registry_->Remove(this);if(id_)glDeleteTextures(1,&id_);}void OpenGL1TextureRenderer::BindGL(int /*unit*/)const{glBindTexture(GL_TEXTURE_2D,id_);}
// plan_opengl1.md phase 6: mip regeneration off a level-0 update. Priority: an explicit
// glGenerateMipmap call (works on core-profile drivers too) > the GL_GENERATE_MIPMAP texture
// parameter already set before the level-0 upload above (driver did it automatically as a side
// effect) > a CPU box-filter fallback from the just-uploaded level-0 pixels for a driver with
// neither mechanism.
void OpenGL1TextureRenderer::RegenerateMips(const uint8_t*level0){
if(!mipMap_){hasMips_=false;return;}
if(glGenerateMipmap_){glGenerateMipmap_(GL_TEXTURE_2D);hasMips_=true;return;}
if(generateMipmapCap_){hasMips_=true;return;}
if(level0){GenerateMipsCPU(GL_TEXTURE_2D,width_,height_,level0);hasMips_=true;}else hasMips_=false;
}
void OpenGL1TextureRenderer::UpdatePixels(const uint8_t*p,int stride){if(!p)return;glBindTexture(GL_TEXTURE_2D,id_);glPixelStorei(GL_UNPACK_ALIGNMENT,1);if(mipMap_&&!glGenerateMipmap_&&generateMipmapCap_)glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
if(stride==width_*4){glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width_,height_,GL_RGBA,GL_UNSIGNED_BYTE,p);RegenerateMips(p);}
else{for(int y=0;y<height_;++y)glTexSubImage2D(GL_TEXTURE_2D,0,0,y,width_,1,GL_RGBA,GL_UNSIGNED_BYTE,p+y*stride);
 // Non-contiguous source stride -- the CPU mip fallback needs a tightly-packed width_*height_*4
 // buffer, so repack once here rather than complicating GenerateMipsCPU with a stride parameter.
 if(mipMap_&&!glGenerateMipmap_&&!generateMipmapCap_){std::vector<uint8_t> packed((size_t)width_*height_*4);for(int y=0;y<height_;++y)std::memcpy(packed.data()+(size_t)y*width_*4,p+(size_t)y*stride,(size_t)width_*4);RegenerateMips(packed.data());}
 else RegenerateMips(nullptr);
}}
void OpenGL1TextureRenderer::UpdatePixelsLevel(int level,const uint8_t*p,int w,int h){glBindTexture(GL_TEXTURE_2D,id_);glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,p);}
// plan_opengl1.md phase 5: ARB_texture_cube_map/core-1.3 cube map renderer for EnvironmentMapEffect.
// GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z are consecutive enum values in exactly
// Microsoft::Xna::Framework::Graphics::CubeMapFace's own declaration order (PositiveX=0 ..
// NegativeZ=5, same convention EasyGLTextureCubeRenderer's kCubeFaceTargets table already relies
// on), so face->target is a plain offset, no lookup table needed. Every mip level is
// pre-allocated with a defined (if empty) image up front -- same reasoning as
// OpenGL1RenderTargetRenderer/EasyGLTextureCubeRenderer's own comments: glTexSubImage2D (used by
// SetData) requires the target level to already have a defined image, so skipping this loop
// would silently break SetData(level>0,...).
OpenGL1TextureCubeRenderer::OpenGL1TextureCubeRenderer(int size,bool mipMap,int /*surfaceFormat*/,OpenGL1ResourceRegistry*registry,bool generateMipmapCap):size_(size),mipMap_(mipMap),generateMipmapCap_(generateMipmapCap),registry_(registry){Build();if(registry_)registry_->Add(this);}
// Shared by the constructor and RecreateGLResource() -- every level of every face is always
// pre-allocated (glTexSubImage2D, used by SetData, requires the target level to already have a
// defined image), and level 0 is seeded from whatever cpuPixels_[face] currently holds (nullptr
// -- an empty pre-allocated level, same as before this phase -- on first construction; the real
// retained per-face pixel data on a post-context-loss rebuild).
void OpenGL1TextureCubeRenderer::Build(){
glGenTextures(1,&id_);glBindTexture(GL_TEXTURE_CUBE_MAP,id_);
int levels=1;if(mipMap_){int sz=size_;while(sz>1){sz/=2;levels++;}}
if(mipMap_&&!glGenerateMipmap_&&generateMipmapCap_)glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_GENERATE_MIPMAP,GL_TRUE);
for(int face=0;face<6;++face){
const void*px=cpuPixels_[face]?cpuPixels_[face]->data():nullptr;
glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,0,GL_RGBA,size_,size_,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
int levelSize=std::max(1,size_/2);
for(int lvl=1;lvl<levels;++lvl){glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,lvl,GL_RGBA,levelSize,levelSize,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);levelSize=std::max(1,levelSize/2);}
}
glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
RegenerateMips();
}
// Same 3-tier priority OpenGL1TextureRenderer::RegenerateMips() uses (glGenerateMipmap > the
// GL_GENERATE_MIPMAP texparam set in Build() above, already handled automatically by the driver
// as a side effect of any level-0 upload > a per-face CPU box-filter fallback), except
// glGenerateMipmap(GL_TEXTURE_CUBE_MAP) regenerates all 6 faces' mip chains in one call (unlike
// Texture2D, no need to loop faces for that path).
void OpenGL1TextureCubeRenderer::RegenerateMips(){
if(!mipMap_)return;
if(glGenerateMipmap_){glGenerateMipmap_(GL_TEXTURE_CUBE_MAP);return;}
if(generateMipmapCap_)return;
for(int face=0;face<6;++face)if(cpuPixels_[face])GenerateMipsCPU(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,size_,size_,cpuPixels_[face]->data());
}
OpenGL1TextureCubeRenderer::~OpenGL1TextureCubeRenderer(){if(registry_)registry_->Remove(this);if(id_)glDeleteTextures(1,&id_);}
void OpenGL1TextureCubeRenderer::RecreateGLResource(){Build();}
void OpenGL1TextureCubeRenderer::BindGL(int /*unit*/)const{glBindTexture(GL_TEXTURE_CUBE_MAP,id_);}
bool OpenGL1TextureCubeRenderer::SetData(int face,int level,int x,int y,int w,int h,const void*data,int /*dataLength*/){if(face<0||face>=6||level<0||!data||w<=0||h<=0)return false;glBindTexture(GL_TEXTURE_CUBE_MAP,id_);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data);if(level==0&&mipMap_)RegenerateMips();return true;}
// Desktop GL (unlike EasyGL's GLES3 target) has glGetTexImage, but it always reads the FULL
// face/level image -- no sub-rectangle readback exists at the GL API level -- so the requested
// [x,y,w,h] box is copied out of a full-image temporary rather than read directly.
bool OpenGL1TextureCubeRenderer::GetData(int face,int level,int x,int y,int w,int h,void*data,int /*dataLength*/)const{if(face<0||face>=6||level<0||!data||w<=0||h<=0)return false;glBindTexture(GL_TEXTURE_CUBE_MAP,id_);int levelSize=size_;for(int i=0;i<level;i++)levelSize=std::max(1,levelSize/2);std::vector<uint8_t>full((size_t)levelSize*levelSize*4);glPixelStorei(GL_PACK_ALIGNMENT,1);glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,GL_RGBA,GL_UNSIGNED_BYTE,full.data());uint8_t*dest=static_cast<uint8_t*>(data);for(int row=0;row<h;++row)std::memcpy(dest+(size_t)row*w*4,full.data()+((size_t)(y+row)*levelSize+x)*4,(size_t)w*4);return true;}
// plan_opengl1.md item 22: SDL_GL_MULTISAMPLEBUFFERS/SAMPLES only report what the driver
// GENUINELY granted for the visual the current GL context is bound to -- GLX can silently clamp
// or altogether refuse the request GraphicsDevice.cpp made before SDL_CreateWindow(), so this is
// read back empirically rather than trusted from GraphicsRendererCreateArgs::multiSampleCount.
void OpenGL1Renderer::DetectMultiSampleCount(){
int msBuffers=0,msSamples=0;SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS,&msBuffers);SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES,&msSamples);
multiSampleCount_=(msBuffers>0&&msSamples>1)?msSamples:0;
if(multiSampleCount_>1)glEnable(GL_MULTISAMPLE);
}
OpenGL1Renderer::OpenGL1Renderer(const GraphicsRendererCreateArgs&a):presentationMode_(static_cast<int>(a.presentationMode)),window_(a.window),virtualWidth_(a.virtualWidth),virtualHeight_(a.virtualHeight),contextRecoveryEnabled_(a.contextRecoveryEnabled){if(!window_)throw std::runtime_error("OPENGL1 requires SDL window");SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);glContext_=SDL_GL_CreateContext(window_);if(!glContext_)throw std::runtime_error(std::string("OPENGL1 SDL_GL_CreateContext failed: ")+SDL_GetError());SDL_GL_MakeCurrent(window_,glContext_);SDL_GL_SetSwapInterval(a.swapInterval);caps_=DetectOpenGL1Capabilities();if(caps_.framebufferObject)caps_.framebufferObject=TryLoadOpenGL1FramebufferObjectFunctions();if(caps_.multitexture)caps_.multitexture=TryLoadMultitextureFunctions();if(caps_.generateMipmap)TryLoadGenerateMipmapFunction();if(caps_.occlusionQuery)caps_.occlusionQuery=TryLoadOpenGL1OcclusionQueryFunctions();if(caps_.extendedBlend)caps_.extendedBlend=TryLoadBlendFunctions();DetectMultiSampleCount();
std::cout<<"CNA: OpenGL1 capabilities -- GL "<<caps_.versionMajor<<"."<<caps_.versionMinor
 <<"; framebuffer object: "<<(caps_.framebufferObject?"yes":"no")
 <<"; multitexture: "<<(caps_.multitexture?"yes":"no")
 <<"; texture cube map: "<<(caps_.textureCubeMap?"yes":"no")
 <<"; mipmap generation: "<<(caps_.generateMipmap?"yes":"no")
 <<"; anisotropic filtering: "<<(caps_.anisotropicFiltering?("supported, up to "+std::to_string((int)caps_.maxAnisotropy)+"x"):std::string("not supported"))
 <<"; MSAA: "<<(multiSampleCount_>1?(std::to_string(multiSampleCount_)+"x"):std::string("no"))
 <<std::endl;
glEnable(GL_TEXTURE_2D);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glShadeModel(GL_SMOOTH);glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_NICEST);IGraphicsRenderer::RegisterForWindow(window_,this);}
OpenGL1Renderer::~OpenGL1Renderer(){IGraphicsRenderer::UnregisterForWindow(window_);if(glContext_)SDL_GL_DestroyContext(glContext_);}void OpenGL1Renderer::Clear(float r,float g,float b,float a){glClearColor(r,g,b,a);glClear(GL_COLOR_BUFFER_BIT);}void OpenGL1Renderer::Present(){SDL_GL_SwapWindow(window_);}void OpenGL1Renderer::GetViewportSize(int&w,int&h){SDL_GetWindowSizeInPixels(window_,&w,&h);}void OpenGL1Renderer::SetVirtualResolution(int w,int h){virtualWidth_=w;virtualHeight_=h;}void OpenGL1Renderer::SetPresentationMode(int mode){presentationMode_=mode;}
// plan_opengl1.md item 13 (EasyGL parity): see EffectiveWidth()/EffectiveHeight()'s own doc
// comment in the header for the full rationale of which modes recompute and which don't.
void OpenGL1Renderer::ComputeLogicalSize(int&outW,int&outH)const{
if(presentationMode_!=4){outW=virtualWidth_;outH=virtualHeight_;return;} // Only FixedHeightDynamicWidth recomputes.
int physW=0,physH=0;SDL_GetWindowSizeInPixels(window_,&physW,&physH);
if(physW<=0||physH<=0||virtualHeight_<=0){outW=virtualWidth_;outH=virtualHeight_;return;}
outH=virtualHeight_;
outW=(int)(((long long)physW*virtualHeight_+physH/2)/physH); // round(physW*preferredH/physH)
}
bool OpenGL1Renderer::TransformWindowToLogical(float windowX,float windowY,float&logX,float&logY)const{
if(presentationMode_!=4)return false;
int physW=0,physH=0;SDL_GetWindowSizeInPixels(window_,&physW,&physH);int logW,logH;ComputeLogicalSize(logW,logH);
if(physH<=0||logH<=0)return false;
const float scale=(float)logH/(float)physH; // uniform -- see the header's own doc comment for why width/height scale are identical here.
logX=windowX*scale;logY=windowY*scale;return true;
}
bool OpenGL1Renderer::TransformLogicalToWindow(float logX,float logY,float&windowX,float&windowY)const{
if(presentationMode_!=4)return false;
int physW=0,physH=0;SDL_GetWindowSizeInPixels(window_,&physW,&physH);int logW,logH;ComputeLogicalSize(logW,logH);
if(logH<=0||physH<=0)return false;
const float scale=(float)physH/(float)logH;
windowX=logX*scale;windowY=logY*scale;return true;
}
void OpenGL1Renderer::SetSwapInterval(int interval){SDL_GL_SetSwapInterval(interval);}
void OpenGL1Renderer::ReadBackbuffer(int x,int y,int w,int h,uint8_t*pixels){int W,H;GetViewportSize(W,H);glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);const int glY=H-y-h;glReadPixels(x,glY,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);const int rowBytes=w*4;std::vector<uint8_t>tmp(rowBytes);for(int i=0;i<h/2;++i){uint8_t*top=pixels+i*rowBytes;uint8_t*bot=pixels+(h-1-i)*rowBytes;std::copy(top,top+rowBytes,tmp.data());std::copy(bot,bot+rowBytes,top);std::copy(tmp.begin(),tmp.end(),bot);}}
std::unique_ptr<ITextureRenderer>OpenGL1Renderer::CreateTexture(const ImageData&d){return std::make_unique<OpenGL1TextureRenderer>(d,RegistryIfEnabled(),caps_.generateMipmap);}std::unique_ptr<ISpriteBatchRenderer>OpenGL1Renderer::CreateSpriteBatch(){return std::make_unique<OpenGL1SpriteBatchRenderer>(*this);}std::unique_ptr<IVertexBufferRenderer>OpenGL1Renderer::CreateVertexBuffer(int c){return std::make_unique<OpenGL1VertexBufferRenderer>(c);}std::unique_ptr<IIndexBufferRenderer>OpenGL1Renderer::CreateIndexBuffer16(int){return std::make_unique<OpenGL1IndexBufferRenderer>(false);}std::unique_ptr<IIndexBufferRenderer>OpenGL1Renderer::CreateIndexBuffer32(int){return std::make_unique<OpenGL1IndexBufferRenderer>(true);}
std::unique_ptr<IRenderTargetRenderer>OpenGL1Renderer::CreateRenderTarget2D(int w,int h,int depthFormat,bool,bool mipMap,int multiSampleCount){if(!caps_.framebufferObject)return nullptr;try{return std::make_unique<OpenGL1RenderTargetRenderer>(w,h,depthFormat,mipMap,multiSampleCount,RegistryIfEnabled());}catch(const std::exception&){return nullptr;}}
// plan_opengl1.md phase 8: matches EasyGLRenderer's own desktop DebugSimulateContextLoss()/
// DebugRestoreContext() pattern (destroy+recreate is one atomic operation on desktop -- there is
// no genuine asynchronous lost/restored pair the way WebGL has), implemented independently with
// no EasyGL/metagl dependency. registry_.NotifyContextLost() runs while the OLD context is still
// current (so resources can drop handles cleanly); the new context's capabilities/extension
// entry points are re-detected/re-loaded exactly as the constructor does, then
// registry_.NotifyContextRestored() rebuilds every tracked resource against the new context.
void OpenGL1Renderer::SetContextRecoveryEnabled(bool enabled){contextRecoveryEnabled_=enabled;}
void OpenGL1Renderer::DebugSimulateContextLoss(){
std::cout<<"CNA: OpenGL1 simulating desktop GL context loss + immediate recreate"<<std::endl;
registry_.NotifyContextLost();
if(glContext_){SDL_GL_MakeCurrent(window_,nullptr);SDL_GL_DestroyContext(glContext_);glContext_=nullptr;}
glContext_=SDL_GL_CreateContext(window_);
if(!glContext_)throw std::runtime_error(std::string("OPENGL1 SDL_GL_CreateContext failed during debug context loss: ")+SDL_GetError());
SDL_GL_MakeCurrent(window_,glContext_);
caps_=DetectOpenGL1Capabilities();if(caps_.framebufferObject)caps_.framebufferObject=TryLoadOpenGL1FramebufferObjectFunctions();if(caps_.multitexture)caps_.multitexture=TryLoadMultitextureFunctions();if(caps_.generateMipmap)TryLoadGenerateMipmapFunction();if(caps_.occlusionQuery)caps_.occlusionQuery=TryLoadOpenGL1OcclusionQueryFunctions();if(caps_.extendedBlend)caps_.extendedBlend=TryLoadBlendFunctions();DetectMultiSampleCount();
glEnable(GL_TEXTURE_2D);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glShadeModel(GL_SMOOTH);glHint(GL_PERSPECTIVE_CORRECTION_HINT,GL_NICEST);
registry_.NotifyContextRestored();
// The new context defaults to FBO 0 (the backbuffer) regardless of what was bound before the
// loss -- if a RenderTarget2D was the active render target at the moment of loss, its rebuilt
// FBO (just recreated above, via NotifyContextRestored -> OpenGL1RenderTargetRenderer::
// RecreateGLResource) needs to be explicitly re-bound, or every subsequent draw silently lands
// on the backbuffer instead while SetViewport/SetScissorRect keep computing against the RT's
// own dimensions (currentRt_ is still non-null) -- a viewport/scissor mismatch on top of the
// wrong target.
if(currentRt_)currentRt_->BindAsRenderTarget();
// plan_opengl1.md item 24: same rebind reasoning as currentRt_ above, for a cube-face target.
if(currentCubeRt_)currentCubeRt_->BindAsRenderTargetFace(currentCubeFace_);
std::cout<<"CNA: OpenGL1 desktop GL context recreated and all tracked resources restored"<<std::endl;
}
void OpenGL1Renderer::DebugRestoreContext(){DebugSimulateContextLoss();}
std::unique_ptr<ITextureCubeRenderer>OpenGL1Renderer::CreateTextureCube(int size,bool mipMap,int surfaceFormat){if(!caps_.textureCubeMap)return nullptr;return std::make_unique<OpenGL1TextureCubeRenderer>(size,mipMap,surfaceFormat,RegistryIfEnabled(),caps_.generateMipmap);}
std::unique_ptr<IRenderTargetCubeRenderer>OpenGL1Renderer::CreateRenderTargetCube(int size,int depthFormat,bool /*preserveContents*/,bool mipMap,int){if(!caps_.framebufferObject||!caps_.textureCubeMap)return nullptr;try{return std::make_unique<OpenGL1RenderTargetCubeRenderer>(size,depthFormat,mipMap,RegistryIfEnabled());}catch(const std::exception&){return nullptr;}}
std::unique_ptr<IOcclusionQueryRenderer>OpenGL1Renderer::CreateOcclusionQuery(){if(!caps_.occlusionQuery)return nullptr;return std::make_unique<OpenGL1OcclusionQueryRenderer>();}
void OpenGL1Renderer::SetRenderTarget2D(IRenderTargetRenderer*rt){if(currentCubeRt_){currentCubeRt_->UnbindAsRenderTarget();currentCubeRt_=nullptr;currentCubeFace_=-1;}if(currentRt_&&currentRt_!=rt)currentRt_->UnbindAsRenderTarget();currentRt_=rt;if(rt)rt->BindAsRenderTarget();}
// plan_opengl1.md item 24: mirrors SetRenderTarget2D's own clear-the-other-kind's-stale-pointer
// symmetry -- switching TO a cube face must also unbind/clear whatever 2D currentRt_ was active.
void OpenGL1Renderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer*rt,int face){if(currentRt_){currentRt_->UnbindAsRenderTarget();currentRt_=nullptr;}if(currentCubeRt_&&currentCubeRt_!=rt)currentCubeRt_->UnbindAsRenderTarget();currentCubeRt_=rt;currentCubeFace_=rt?face:-1;if(rt)rt->BindAsRenderTargetFace(face);}
// Post-audit descriptor route (see the header's own comment). SetRenderTarget2D(nullptr) already
// unbinds and clears an active cube face, so the restore path needs no second call.
void OpenGL1Renderer::SetRenderTargets(const RenderTargetBindingDescriptor*renderTargets,int count){
 if(!renderTargets||count<=0){SetRenderTarget2D(nullptr);return;}
 if(count>1)throw System::NotSupportedException("OpenGL1: multiple simultaneous render targets are not available on this fixed-function renderer -- SupportsCapability(MultipleRenderTargets) reports false here.");
 if(renderTargets[0].IsRenderTargetCubeFace()){
  auto*cube=dynamic_cast<OpenGL1RenderTargetCubeRenderer*>(renderTargets[0].GetRenderTargetCube());
  if(!cube)throw System::NotSupportedException("OpenGL1: this render-target cube was not created by the OpenGL1 renderer.");
  SetRenderTargetCubeFace(cube,renderTargets[0].GetCubeFace());
  return;
 }
 SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
}
void OpenGL1Renderer::SetDepthTestEnabled(bool e){e?glEnable(GL_DEPTH_TEST):glDisable(GL_DEPTH_TEST);}void OpenGL1Renderer::SetBlendEnabled(bool e){e?glEnable(GL_BLEND):glDisable(GL_BLEND);}void OpenGL1Renderer::SetDepthWriteEnabled(bool e){glDepthMask(e?GL_TRUE:GL_FALSE);}void OpenGL1Renderer::ClearColorAndDepth(float r,float g,float b,float a,float d){glClearColor(r,g,b,a);glClearDepth(d);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}void OpenGL1Renderer::ClearDepth(float d){glClearDepth(d);glClear(GL_DEPTH_BUFFER_BIT);}void OpenGL1Renderer::ClearStencil(int s){glClearStencil(s);glClear(GL_STENCIL_BUFFER_BIT);}void OpenGL1Renderer::ClearDepthAndStencil(float d,int s){glClearDepth(d);glClearStencil(s);glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);}void OpenGL1Renderer::ClearColorAndStencil(float r,float g,float b,float a,int s){glClearColor(r,g,b,a);glClearStencil(s);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);}void OpenGL1Renderer::ClearColorDepthAndStencil(float r,float g,float b,float a,float d,int s){glClearColor(r,g,b,a);glClearDepth(d);glClearStencil(s);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);}
// plan_opengl1.md items 18/19 (EasyGL parity): colorBlendFunc/alphaBlendFunc (blend equations
// beyond additive) and alphaSrcBlend/alphaDstBlend (separate alpha blend factors) used to be
// dropped entirely -- glBlendFunc always implied GL_FUNC_ADD for both channels and reused the
// color factors for alpha too. glBlendFuncSeparate/glBlendEquationSeparate (both core GL 1.4,
// same TryLoadBlendFunctions() loader as item 17's glBlendColor) fix both; falls back to the
// old single-value glBlendFunc/implicit-Add behavior when the driver genuinely lacks them.
void OpenGL1Renderer::ApplyBlendState(int cs,int as,int cd,int ad,int cbf,int abf,const BlendWriteState&ws){
// REMED-GFX-077: slot 0's ColorWriteChannels are real on desktop GL (glColorMask, core 1.0),
// applied before anything else because the mask is independent of whether blending is enabled.
// Slots 1..3 have no subject without MRT support here; BlendState.MultiSampleMask has no
// fixed-function GL 1.x equivalent (GL_SAMPLE_MASK is GL 3.2+) -- the same documented gap as
// EasyGL's, reported rather than silently approximated.
const int cwc=ws.colorWriteChannels[0];
glColorMask(ColorWriteHasRed(cwc)?GL_TRUE:GL_FALSE,ColorWriteHasGreen(cwc)?GL_TRUE:GL_FALSE,ColorWriteHasBlue(cwc)?GL_TRUE:GL_FALSE,ColorWriteHasAlpha(cwc)?GL_TRUE:GL_FALSE);
glEnable(GL_BLEND);
if(caps_.extendedBlend){glBlendFuncSeparate_(BlendF(cs,true),BlendF(cd,true),BlendF(as,true),BlendF(ad,true));glBlendEquationSeparate_(BlendEq(cbf),BlendEq(abf));}
else glBlendFunc(BlendF(cs,false),BlendF(cd,false));}void OpenGL1Renderer::ApplyDepthStencilState(bool de,bool dw,int df,bool se,int sf,int sp,int sfa,int sdf,int sm,int sw,int ref,bool,int,int,int,int){de?glEnable(GL_DEPTH_TEST):glDisable(GL_DEPTH_TEST);glDepthMask(dw);glDepthFunc(Cmp(df));se?glEnable(GL_STENCIL_TEST):glDisable(GL_STENCIL_TEST);stencilRef_=ref;glStencilFunc(Cmp(sf),ref,(GLuint)sm);glStencilMask((GLuint)sw);glStencilOp(StencilOp(sfa),StencilOp(sdf),StencilOp(sp));}
void OpenGL1Renderer::ApplyRasterizerState(int c,int f,bool sc,float db,float slope){if(c==0)glDisable(GL_CULL_FACE);else{glEnable(GL_CULL_FACE);glFrontFace(GL_CCW);glCullFace(c==1?GL_BACK:GL_FRONT);}glPolygonMode(GL_FRONT_AND_BACK,f==1?GL_LINE:GL_FILL);sc?glEnable(GL_SCISSOR_TEST):glDisable(GL_SCISSOR_TEST);if(db!=0||slope!=0){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(slope,db);}else glDisable(GL_POLYGON_OFFSET_FILL);}// Pure bookkeeping now -- see the header's own comment on ApplySamplerFilterAndWrap for why this
// no longer issues any gl* calls directly (fixes a real ordering + ignored-slot bug found while
// implementing phase 6's mip-aware filtering).
void OpenGL1Renderer::ApplySamplerState(int slot,int filter,int u,int v,int maxAniso){if(slot<0||slot>=2)return;samplerSlot_[slot].filter=filter;samplerSlot_[slot].addrU=u;samplerSlot_[slot].addrV=v;samplerSlot_[slot].maxAniso=maxAniso;}
// Applies samplerSlot_[slot]'s cached filter/wrap/anisotropy to whatever texture is bound on the
// CURRENTLY ACTIVE texture unit -- callers must glActiveTexture() + BindGL() the right texture
// first. `hasMips` gates whether the min filter is allowed to request a _MIPMAP_ variant (see
// OpenGL1TextureRenderer::HasMips()'s own doc comment for why).
void OpenGL1Renderer::ApplySamplerFilterAndWrap(int slot,bool hasMips){if(slot<0||slot>=2)return;const GL1SamplerParams&s=samplerSlot_[slot];GLenum minF,magF;MinMagFilter(s.filter,hasMips,minF,magF);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,minF);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,magF);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,WrapMode(s.addrU));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,WrapMode(s.addrV));if(caps_.anisotropicFiltering){float req=(s.filter==2)?(float)s.maxAniso:1.0f;if(req<1.0f)req=1.0f;if(req>caps_.maxAnisotropy)req=caps_.maxAnisotropy;glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,req);}}// plan_opengl1.md item 17 (EasyGL parity): was a no-op -- Blend.BlendFactor/InverseBlendFactor
// mapped to GL_ONE/GL_ZERO instead (BlendF above), meaning a game using GraphicsDevice.
// BlendFactor got a silently WRONG constant color, not just a degraded/ignored one.
void OpenGL1Renderer::SetBlendFactor(float r,float g,float b,float a){if(caps_.extendedBlend)glBlendColor_(r,g,b,a);}void OpenGL1Renderer::SetReferenceStencil(int v){stencilRef_=v;}void OpenGL1Renderer::SetScissorRect(int x,int y,int w,int h){int H;if(currentRt_)H=currentRt_->GetHeight();else if(currentCubeRt_)H=currentCubeRt_->GetSize();else{int W;GetViewportSize(W,H);}glScissor(x,H-y-h,w,h);}void OpenGL1Renderer::SetViewport(int x,int y,int w,int h,float mn,float mx){int H;if(currentRt_)H=currentRt_->GetHeight();else if(currentCubeRt_)H=currentCubeRt_->GetSize();else{int W;GetViewportSize(W,H);}glViewport(x,H-y-h,w,h);glDepthRange(mn,mx);}
void OpenGL1Renderer::SetupMatrices(const Matrix&w,const Matrix&v,const Matrix&p){float a[16];glMatrixMode(GL_PROJECTION);Mat(p,a);glLoadMatrixf(a);glMatrixMode(GL_MODELVIEW);Mat(v,a);glLoadMatrixf(a);Mat(w,a);glMultMatrixf(a);}
// REMED-GFX-010 replaced GpuDrawParams' scalar fogStart/fogEnd with FNA's fog vector, which
// every shader renderer dots against the object-space position. A fixed-function pipeline has no
// such dot product -- glFog wants the two scalars back. They are recovered exactly rather than
// approximated: the vector is built from those scalars and from the same world*view matrix
// SetupMatrices() just loaded into GL_MODELVIEW, so projecting fogVector.xyz back onto that
// matrix's own eye-Z row recovers the scale, and the w term then yields fogStart and fogEnd.
// All-zero is FNA's own "fog disabled" encoding (honoured even if the flag disagrees) and
// {0,0,0,1} is FNA's degenerate fogStart==fogEnd encoding (fully fogged).
static void ApplyFogFromVector(const GpuDrawParams*params){
 if(!params||!params->fogEnabled){glDisable(GL_FOG);return;}
 if(params->fogVector[0]==0.0f&&params->fogVector[1]==0.0f&&params->fogVector[2]==0.0f&&params->fogVector[3]==0.0f){glDisable(GL_FOG);return;}
 glEnable(GL_FOG);glFogi(GL_FOG_MODE,GL_LINEAR);
 float mv[16];glGetFloatv(GL_MODELVIEW_MATRIX,mv);
 // Eye-space Z row of world*view: z_view = zx*x + zy*y + zz*z + zw.
 const float zx=mv[2],zy=mv[6],zz=mv[10],zw=mv[14];
 const float denom=zx*zx+zy*zy+zz*zz;
 const float numer=params->fogVector[0]*zx+params->fogVector[1]*zy+params->fogVector[2]*zz;
 const float scale=denom>0.0f?numer/denom:0.0f;
 if(scale==0.0f){
  // fogStart == fogEnd (or a world*view that collapses the eye-Z axis entirely): the whole draw
  // is fogged. GL's visibility factor is (end - z)/(end - start), clamped to [0,1], with z the
  // eye distance (never negative); start=-1, end=0 gives -z, <= 0 for every z including z == 0 --
  // fully fogged everywhere. A near-zero-width ramp would NOT do: geometry sitting exactly at
  // the eye would still come out unfogged.
  glFogf(GL_FOG_START,-1.0f);glFogf(GL_FOG_END,0.0f);
 }else{
  const float fogStart=params->fogVector[3]/scale-zw;
  const float fogEnd=fogStart-1.0f/scale;
  glFogf(GL_FOG_START,fogStart);glFogf(GL_FOG_END,fogEnd);
 }
 float fc[4]={params->fogColor[0],params->fogColor[1],params->fogColor[2],1};glFogfv(GL_FOG_COLOR,fc);
}
void OpenGL1Renderer::DrawInternal(const OpenGL1VertexBufferRenderer&vb,const OpenGL1IndexBufferRenderer*ib,PrimitiveType prim,int pc,const GpuDrawParams*params){const auto&s=vb.Data();const size_t st=vb.Stride();if(st<12||s.empty())return;bool tex=params&&params->texture0&&(st==20||st==24||st==32);bool normal=(st==32);
bool dual=tex&&params->dualTexture&&params->texture1&&glActiveTexture_&&glMultiTexCoord2f_;
// plan_opengl1.md phase 5: EnvironmentMapEffect's fixed-function reflection-mapping subset.
// Needs a normal (stride 32), a real ARB_texture_cube_map cube texture and a second texture
// unit (the cube map rides unit 1, exactly like DualTextureEffect's unit 1 -- the two are
// mutually exclusive per draw, XNA has no effect that is both dual-textured and env-mapped).
bool envMap=params&&params->envMapping&&params->envMap&&normal&&caps_.textureCubeMap&&glActiveTexture_;
// Unit 1 previously left in GL_TEXTURE_CUBE_MAP/GL_TEXTURE_GEN_* state by an earlier envMap draw
// would otherwise silently corrupt a later DualTextureEffect or plain textured draw (per-unit
// enables persist across draw calls here -- there is no glPushAttrib/glPopAttrib bracketing
// DrawInternal the way SpriteBatch::Begin()/End() has). Reset unconditionally whenever this
// draw is not itself doing env mapping.
auto resetUnit1EnvGen=[&](){if(glActiveTexture_){glActiveTexture_(GL_TEXTURE1);glDisable(GL_TEXTURE_GEN_S);glDisable(GL_TEXTURE_GEN_T);glDisable(GL_TEXTURE_GEN_R);if(caps_.textureCubeMap)glDisable(GL_TEXTURE_CUBE_MAP);glActiveTexture_(GL_TEXTURE0);}};
if(tex||envMap){
if(tex){glEnable(GL_TEXTURE_2D);params->texture0->BindGL();
ApplySamplerFilterAndWrap(0,HasMipsFor(params->texture0));
// Render targets sample bottom-up relative to every CPU-uploaded texture's top-down convention
// (see OpenGL1SpriteBatchRenderer::Draw's identical fix/comment) -- flip V via the legacy texture
// matrix here since these texcoords are baked into the vertex buffer, not computed per-draw.
glMatrixMode(GL_TEXTURE);
if(dynamic_cast<const IRenderTargetRenderer*>(params->texture0)){glLoadIdentity();glScalef(1.0f,-1.0f,1.0f);glTranslatef(0.0f,-1.0f,0.0f);}else glLoadIdentity();
glMatrixMode(GL_MODELVIEW);
}else glDisable(GL_TEXTURE_2D);
// plan_opengl1.md phase 3: DualTextureEffect's fixed-function equivalent, matching this
// project's own shader renderers' "base.rgb*=2.0; FragColor=base*texture(uTexture2,vUV)*
// uDiffuseColor" formula exactly via GL_COMBINE texture environment chaining -- unit 0
// modulates texture0 by the vertex/material color (GL_PRIMARY_COLOR) with an RGB_SCALE of 2
// (the classic "modulate2x" lightmap headroom XNA's own DualTextureEffect.fx uses), then unit 1
// modulates that result (GL_PREVIOUS) by texture1. Both units sample the SAME per-vertex UV
// (XNA's own DualTextureEffect has no second texcoord channel either -- see FillGpuDrawParams).
if(dual){resetUnit1EnvGen();glActiveTexture_(GL_TEXTURE1);glEnable(GL_TEXTURE_2D);params->texture1->BindGL();
ApplySamplerFilterAndWrap(1,HasMipsFor(params->texture1));
glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_MODULATE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_PREVIOUS);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_MODULATE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_TEXTURE);
glActiveTexture_(GL_TEXTURE0);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_MODULATE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PRIMARY_COLOR);glTexEnvf(GL_TEXTURE_ENV,GL_RGB_SCALE,2.0f);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_MODULATE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_PRIMARY_COLOR);glTexEnvf(GL_TEXTURE_ENV,GL_ALPHA_SCALE,1.0f);
}else if(envMap){
// Unit 1 samples the cube map via GL_REFLECTION_MAP texture-coordinate generation off the
// eye-space normal/vertex (hence GL_NORMALIZE below, since a non-uniform World scale can leave
// normals non-unit-length) and GL_INTERPOLATEs it against unit 0's result (the lit/textured
// base color, or the bare vertex-lit primary color when there is no diffuse Texture) using
// GL_CONSTANT.a = EnvironmentMapAmount -- i.e. exactly FNA's own real
// `mix(baseColor,envColor,Amount)` blend (docs/environmentmapeffect-support.md Task 394).
// Deliberately NOT attempted: Fresnel edge-weighting and EnvironmentMapSpecular's alpha-scaled
// specular term are both inherently per-pixel/view-angle-dependent and cannot be expressed with
// fixed-function texture combiners -- an honest, documented limitation (plan_opengl1.md), not a
// silent wrong answer. OPENGL1 always behaves as if FresnelEnabled=false/EnvironmentMapSpecular=0.
glActiveTexture_(GL_TEXTURE1);glEnable(GL_TEXTURE_CUBE_MAP);glDisable(GL_TEXTURE_2D);params->envMap->BindGL();
glTexGeni(GL_S,GL_TEXTURE_GEN_MODE,GL_REFLECTION_MAP);glTexGeni(GL_T,GL_TEXTURE_GEN_MODE,GL_REFLECTION_MAP);glTexGeni(GL_R,GL_TEXTURE_GEN_MODE,GL_REFLECTION_MAP);
glEnable(GL_TEXTURE_GEN_S);glEnable(GL_TEXTURE_GEN_T);glEnable(GL_TEXTURE_GEN_R);
float envConst[4]={0,0,0,params->envMapAmount};
glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_INTERPOLATE);
glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_RGB,GL_SRC_COLOR);
glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PREVIOUS);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND1_RGB,GL_SRC_COLOR);
glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_CONSTANT);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND2_RGB,GL_SRC_ALPHA);
glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_REPLACE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_PREVIOUS);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND0_ALPHA,GL_SRC_ALPHA);
glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,envConst);
glActiveTexture_(GL_TEXTURE0);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
}else{resetUnit1EnvGen();glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);if(glActiveTexture_){glActiveTexture_(GL_TEXTURE1);glDisable(GL_TEXTURE_2D);glActiveTexture_(GL_TEXTURE0);}}
}else{glDisable(GL_TEXTURE_2D);resetUnit1EnvGen();if(glActiveTexture_){glActiveTexture_(GL_TEXTURE1);glDisable(GL_TEXTURE_2D);glActiveTexture_(GL_TEXTURE0);}}
normal?glEnable(GL_NORMALIZE):glDisable(GL_NORMALIZE);
if(params&&params->lightingEnabled&&normal){glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);float amb[4]={params->ambientColor[0],params->ambientColor[1],params->ambientColor[2],1};float dif[4]={params->light0Diffuse[0],params->light0Diffuse[1],params->light0Diffuse[2],1};float pos[4]={-params->light0Dir[0],-params->light0Dir[1],-params->light0Dir[2],0};glLightModelfv(GL_LIGHT_MODEL_AMBIENT,amb);glLightfv(GL_LIGHT0,GL_DIFFUSE,dif);glLightfv(GL_LIGHT0,GL_POSITION,pos);
// plan_opengl1.md item 14 (EasyGL parity): BasicEffect.DirectionalLight1/DirectionalLight2 via
// GL_LIGHT1/GL_LIGHT2, the same mechanism GL_LIGHT0 already uses -- GpuDrawParams::light1Diffuse/
// light2Diffuse are zeroed by FillGpuDrawParams when a light is Enabled=false (mirrors FNA's own
// DirectionalLight.Enabled setter), so unconditionally enabling both here is safe: a disabled
// light's zero diffuse contributes nothing, matching every other lit-path field's own convention.
float dif1[4]={params->light1Diffuse[0],params->light1Diffuse[1],params->light1Diffuse[2],1};float pos1[4]={-params->light1Dir[0],-params->light1Dir[1],-params->light1Dir[2],0};glEnable(GL_LIGHT1);glLightfv(GL_LIGHT1,GL_DIFFUSE,dif1);glLightfv(GL_LIGHT1,GL_POSITION,pos1);
float dif2[4]={params->light2Diffuse[0],params->light2Diffuse[1],params->light2Diffuse[2],1};float pos2[4]={-params->light2Dir[0],-params->light2Dir[1],-params->light2Dir[2],0};glEnable(GL_LIGHT2);glLightfv(GL_LIGHT2,GL_DIFFUSE,dif2);glLightfv(GL_LIGHT2,GL_POSITION,pos2);
// plan_opengl1.md item 15 (EasyGL parity): BasicEffect specular highlights via GL_SPECULAR
// material/light state. GL_SEPARATE_SPECULAR_COLOR (core GL 1.2) adds the specular term AFTER
// texture modulation instead of folding it into the modulated primary color -- matches BasicEffect
// .fx's own "texture*(ambient+diffuse) + specular" formula rather than darkening/distorting the
// highlight through the texture. GL_LIGHT_MODEL_LOCAL_VIEWER makes GL compute the eye vector from
// the real (eye-space) viewer position instead of an infinite viewer along -Z; since MODELVIEW
// already holds View*World (SetupMatrices above) whenever lights/vertices are specified here, this
// is GL's own automatic equivalent of BasicEffect's explicit `eyePositionWorld` half-vector term --
// no separate GpuDrawParams::eyePositionWorld read needed for this fixed-function path.
glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER,GL_TRUE);glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL,GL_SEPARATE_SPECULAR_COLOR);
float matSpec[4]={params->specularColor[0],params->specularColor[1],params->specularColor[2],1};
float shininess=params->specularPower;if(shininess<0.0f)shininess=0.0f;if(shininess>128.0f)shininess=128.0f; // GL_SHININESS valid range.
glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,matSpec);glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,shininess);
float spec0[4]={params->light0Specular[0],params->light0Specular[1],params->light0Specular[2],1};glLightfv(GL_LIGHT0,GL_SPECULAR,spec0);
float spec1[4]={params->light1Specular[0],params->light1Specular[1],params->light1Specular[2],1};glLightfv(GL_LIGHT1,GL_SPECULAR,spec1);
float spec2[4]={params->light2Specular[0],params->light2Specular[1],params->light2Specular[2],1};glLightfv(GL_LIGHT2,GL_SPECULAR,spec2);
glEnable(GL_COLOR_MATERIAL);glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
// plan_opengl1.md phase 5 finding: the lit (stride 32) path never set GL_EMISSION, so
// EnvironmentMapEffect's EmissiveColor/AmbientLightColor contribution (pre-combined into
// GpuDrawParams::emissiveColor by FillGpuDrawParams -- EnvironmentMapEffect does not populate
// ambientColor at all, unlike BasicEffect) was silently dropped entirely. Real XNA/GL fixed-
// function lighting sums material emission flatly regardless of any light; also benefits
// BasicEffect's own EmissiveColor for the same lit path.
float emis[4]={params->emissiveColor[0],params->emissiveColor[1],params->emissiveColor[2],0};
glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,emis);
}else glDisable(GL_LIGHTING);ApplyFogFromVector(params);if(params&&params->alphaTest[3]<0){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GEQUAL,params->alphaTest[0]);}else glDisable(GL_ALPHA_TEST);
 auto emitTexCoord=[&](float u,float v){if(dual){glMultiTexCoord2f_(GL_TEXTURE0,u,v);glMultiTexCoord2f_(GL_TEXTURE1,u,v);}else if(envMap){glMultiTexCoord2f_(GL_TEXTURE0,u,v);}else glTexCoord2f(u,v);};
 // plan_opengl1.md phase 4: matches FNA's real BasicEffect.fx combine -- when VertexColorEnabled,
 // the vertex color is multiplied by the material DiffuseColor (Task/GpuDrawParams convention:
 // diffuseColor already carries DiffuseColor*Alpha), not used alone. Stride 20/32 already applied
 // diffuseColor (no vertex-color channel to combine with there); this closes the same gap for the
 // two vertex-color-carrying strides.
 auto emitVertexColor=[&](const uint8_t*b){const uint32_t c=*(const uint32_t*)(b+12);const float r=(c&255)/255.0f,g=((c>>8)&255)/255.0f,bl=((c>>16)&255)/255.0f,a=((c>>24)&255)/255.0f;if(params)glColor4f(r*params->diffuseColor[0],g*params->diffuseColor[1],bl*params->diffuseColor[2],a*params->diffuseColor[3]);else glColor4f(r,g,bl,a);};
 auto emit=[&](uint32_t i){if(i>=(uint32_t)vb.GetVertexCount())return;const uint8_t*b=s.data()+i*st;const float*f=(const float*)b;if(st==16){emitVertexColor(b);}else if(st==24){emitVertexColor(b);const float*t=(const float*)(b+16);emitTexCoord(t[0],t[1]);}else if(st==20){const float*t=(const float*)(b+12);if(params)glColor4fv(params->diffuseColor);else glColor4f(1,1,1,1);emitTexCoord(t[0],t[1]);}else if(st==32){const float*n=(const float*)(b+12);const float*t=(const float*)(b+24);glNormal3f(n[0],n[1],n[2]);emitTexCoord(t[0],t[1]);if(params)glColor4fv(params->diffuseColor);}else glColor4f(1,1,1,1);glVertex3f(f[0],f[1],f[2]);};
 int count=VertCount(prim,pc);glBegin(Prim(prim));if(ib){count=std::min(count,ib->GetIndexCount());for(int k=0;k<count;k++){uint32_t i=ib->IsThirtyTwoBit()?((const uint32_t*)ib->Data().data())[k]:((const uint16_t*)ib->Data().data())[k];emit(i);}}else{count=std::min(count,vb.GetVertexCount());for(int i=0;i<count;i++)emit((uint32_t)i);}glEnd();glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);}
void OpenGL1Renderer::DrawColoredPrimitives(const IVertexBufferRenderer&v,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pr,int c){RequireFaithfulDeclarationEXT(v,"colored-nonindexed");SetupMatrices(w,vi,p);DrawInternal(dynamic_cast<const OpenGL1VertexBufferRenderer&>(v),nullptr,pr,c,nullptr);}void OpenGL1Renderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&v,const IIndexBufferRenderer&i,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pr,int c){RequireFaithfulDeclarationEXT(v,"colored-indexed");SetupMatrices(w,vi,p);DrawInternal(dynamic_cast<const OpenGL1VertexBufferRenderer&>(v),&dynamic_cast<const OpenGL1IndexBufferRenderer&>(i),pr,c,nullptr);}void OpenGL1Renderer::DrawPrimitivesEx(const IVertexBufferRenderer&v,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pr,int c,const GpuDrawParams&gp){RequireFaithfulDeclarationEXT(v,"ordinary-nonindexed");SetupMatrices(w,vi,p);DrawInternal(dynamic_cast<const OpenGL1VertexBufferRenderer&>(v),nullptr,pr,c,&gp);}void OpenGL1Renderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer&v,const IIndexBufferRenderer&i,const Matrix&w,const Matrix&vi,const Matrix&p,PrimitiveType pr,int c,const GpuDrawParams&gp){RequireFaithfulDeclarationEXT(v,"ordinary-indexed");SetupMatrices(w,vi,p);DrawInternal(dynamic_cast<const OpenGL1VertexBufferRenderer&>(v),&dynamic_cast<const OpenGL1IndexBufferRenderer&>(i),pr,c,&gp);}bool OpenGL1Renderer::SupportsCapability(CNA::GraphicsCapability capability)const{
 // Exhaustive over the CURRENT GraphicsCapability enum with no default case, so a future member
 // surfaces as a -Wswitch warning rather than an inherited wrong answer (the hazard every prior
 // GL-family lane of this campaign hit: a fork-era switch answering for members it never knew).
 switch(capability){
  // Real fixed-function 3D pipeline, real 24/8 depth/stencil, real glPolygonMode(GL_LINE).
  case CNA::GraphicsCapability::ThreeD:
  case CNA::GraphicsCapability::DepthStencilBuffer:
  case CNA::GraphicsCapability::WireFrame:
  case CNA::GraphicsCapability::StencilBuffer:
  case CNA::GraphicsCapability::AdditiveBlending: return true;
  // Runtime-detected GL_EXT_texture_filter_anisotropic (phase 1).
  case CNA::GraphicsCapability::AnisotropicFiltering: return caps_.anisotropicFiltering;
  // What the driver GENUINELY granted for the window's visual (item 22), not what was requested.
  case CNA::GraphicsCapability::MultiSampleAntiAliasing: return multiSampleCount_>1;
  // Runtime-detected ARB_occlusion_query/core-1.5 (item 23).
  case CNA::GraphicsCapability::OcclusionQuery: return caps_.occlusionQuery;
  // One colour attachment, no shader pipeline, no volume textures, one vertex stream, no
  // instancing (immediate-mode emission has no per-instance advancement of any kind): all
  // five are structural properties of this fixed-function renderer, not missing detection.
  case CNA::GraphicsCapability::MultipleRenderTargets:
  case CNA::GraphicsCapability::CustomEffects:
  case CNA::GraphicsCapability::Texture3D:
  case CNA::GraphicsCapability::MultiStreamVertexInput:
  case CNA::GraphicsCapability::Instancing: return false;
 }
 // Unreachable for current members; a future member lands here (after the -Wswitch warning
 // above) and is reported unsupported until this renderer explicitly claims it.
 return false;
}
void OpenGL1SpriteBatchRenderer::Begin(){if(begun_)return;begun_=true;glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_TEXTURE_BIT);glDisable(GL_DEPTH_TEST);
// Real XNA SpriteBatch is never subject to 3D face culling -- a game's RasterizerState.CullMode
// (set for its own 3D rendering) must not make sprites disappear depending on winding. Restored
// by the GL_ENABLE_BIT push/pop above, so this is fully self-contained to the sprite batch.
glDisable(GL_CULL_FACE);
// SpriteBatch only ever touches texture unit 0 -- glPushAttrib above saves whatever unit 1's
// state currently is (it does not reset it), so a prior DualTextureEffect 3D draw's still-bound,
// still-GL_COMBINE-enabled unit 1 would otherwise bleed into every sprite drawn here.
if(glActiveTexture_){glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glActiveTexture_(GL_TEXTURE1);glDisable(GL_TEXTURE_2D);glActiveTexture_(GL_TEXTURE0);}
glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
// Use the bound render target's own size, not the window's -- SpriteBatch draws in pixel space
// relative to whatever's currently bound, and an RT's dimensions can differ from the window.
glOrtho(0,owner_.EffectiveWidth(),owner_.EffectiveHeight(),0,-1,1);
glMatrixMode(GL_MODELVIEW);glPushMatrix();float m[16];Mat(transform_,m);glLoadMatrixf(m);}void OpenGL1SpriteBatchRenderer::End(){if(!begun_)return;glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glPopAttrib();begun_=false;}void OpenGL1SpriteBatchRenderer::Draw(const ITextureRenderer&t,float x,float y){Rectangle d((int)x,(int)y,t.GetWidth(),t.GetHeight()),s(0,0,t.GetWidth(),t.GetHeight());Draw(t,d,s,Color::White);}void OpenGL1SpriteBatchRenderer::Draw(const ITextureRenderer&t,const Rectangle&d,const Rectangle&s,const Color&c){Draw(t,d,s,c,0,Vector2::Zero,SpriteEffects::None,0);}void OpenGL1SpriteBatchRenderer::Draw(const ITextureRenderer&t,const Rectangle&d,const Rectangle&s,const Color&c,float rot,const Vector2&o,SpriteEffects e,float z){if(!begun_)Begin();t.BindGL();
// plan_opengl1.md phase 6 finding: SetSamplerAddressMode()'s u_/v_ used to be stored and never
// actually applied (GL_TEXTURE_WRAP_S/T stayed whatever the texture's own constructor set,
// always GL_CLAMP_TO_EDGE) -- harmless for the common default (Clamp=1, same result), a real gap
// for a game requesting Wrap/tiling. Also now uses the same mip-aware min/mag mapping the 3D
// path does, gated on this specific texture's own HasMips().
{GLenum minF,magF;MinMagFilter(filter_,HasMipsFor(&t),minF,magF);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,minF);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,magF);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,WrapMode(u_));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,WrapMode(v_));}
float u0=(float)s.X/t.GetWidth(),v0=(float)s.Y/t.GetHeight(),u1=(float)(s.X+s.Width)/t.GetWidth(),v1=(float)(s.Y+s.Height)/t.GetHeight();if((int)e&1)std::swap(u0,u1);if((int)e&2)std::swap(v0,v1);
// A render target's color texture is written by the GPU rasterizer, whose row 0 is the BOTTOM
// of what was drawn into it (standard GL framebuffer convention) -- the opposite of every
// CPU-uploaded texture, whose row 0 is whatever was given first (this project's own convention:
// the TOP, since images are uploaded top-down). OpenGL1RenderTargetRenderer::GetData() already
// compensates for this on the CPU-readback path (see its own comment); this is the equivalent
// compensation for the GPU-sampling path (drawing a render target as a regular sprite/texture).
if(dynamic_cast<const IRenderTargetRenderer*>(&t))std::swap(v0,v1);
glPushMatrix();glTranslatef((float)d.X,(float)d.Y,z);glRotatef(rot*57.2957795f,0,0,1);glTranslatef(-o.X,-o.Y,0);Color4(c);glBegin(GL_QUADS);glTexCoord2f(u0,v0);glVertex2f(0,0);glTexCoord2f(u1,v0);glVertex2f((float)d.Width,0);glTexCoord2f(u1,v1);glVertex2f((float)d.Width,(float)d.Height);glTexCoord2f(u0,v1);glVertex2f(0,(float)d.Height);glEnd();glPopMatrix();}
}
#ifdef CNA_RENDERER_OPENGL1
namespace CNA::Internal::Renderers
{
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace OpenGL1
    {
        std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(
            const GraphicsRendererCreateArgs& args)
        {
            return std::make_unique<OpenGL1Renderer>(args);
        }
    }
}
#endif
