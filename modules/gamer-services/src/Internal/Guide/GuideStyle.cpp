// SPDX-License-Identifier: MS-PL
#include "GuideStyle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <algorithm>
#include <cmath>

namespace CNA::Internal::GamerServices::GuideUi {
namespace Palette {
// Translucent colors are premultiplied, as SpriteBatch blends them.
Xna::Color dim(){return Xna::Color(4,6,9,168);}
Xna::Color panel(){return Xna::Color(21,27,38,255);}
Xna::Color surface(){return Xna::Color(31,39,53,255);}
Xna::Color rail(){return Xna::Color(15,20,29,255);}
Xna::Color accent(){return Xna::Color(255,128,82,255);}
Xna::Color focus(){return Xna::Color(62,31,20,62);}
Xna::Color text(){return Xna::Color(242,245,250,255);}
Xna::Color muted(){return Xna::Color(158,171,190,255);}
Xna::Color faint(){return Xna::Color(96,108,126,255);}
Xna::Color online(){return Xna::Color(92,206,122,255);}
Xna::Color away(){return Xna::Color(242,186,72,255);}
Xna::Color busy(){return Xna::Color(232,86,86,255);}
Xna::Color offline(){return Xna::Color(110,120,136,255);}
Xna::Color gold(){return Xna::Color(246,196,88,255);}
Xna::Color error(){return Xna::Color(236,94,94,255);}
}

namespace {
using Xna::Graphics::SpriteFont;
using Xna::Graphics::Texture2D;

// Reference pixel sizes (em) and stroke half-widths (units) of each text style.
struct FontSpec {float size;float radius;float tracking;};
constexpr std::array<FontSpec,static_cast<std::size_t>(Font::Count)> FontSpecs{{
    {17.0f,50.0f,0.8f},  // Caption
    {21.0f,47.0f,0.5f},  // Body
    {21.0f,66.0f,0.5f},  // BodyBold
    {27.0f,64.0f,0.3f},  // Heading
    {36.0f,62.0f,0.0f},  // Title
    {52.0f,58.0f,-0.5f}, // Display
}};
constexpr int DiscSize=128;
constexpr int IconSize=96;

std::vector<SharpRuntime::bytecs> premultiplied(const std::vector<float>& alpha)
{
    std::vector<SharpRuntime::bytecs> pixels(alpha.size()*4);
    for(std::size_t i=0;i<alpha.size();++i) {
        const auto value=static_cast<SharpRuntime::bytecs>(std::lround(std::clamp(alpha[i],0.0f,1.0f)*255.0f));
        pixels[i*4]=pixels[i*4+1]=pixels[i*4+2]=pixels[i*4+3]=value;
    }
    return pixels;
}

std::unique_ptr<SpriteFont> makeFont(Xna::Graphics::GraphicsDevice& device,float size,float radius,float tracking)
{
    // One row-packed atlas of the whole glyph set at this pixel size.
    const float scale=size/1000.0f;
    const int pad=static_cast<int>(std::ceil(radius*scale))+2;
    const int cellHeight=static_cast<int>(std::ceil(1060.0f*scale))+2*pad;
    const float top=800.0f+static_cast<float>(pad)/scale;
    const auto& glyphs=systemGlyphs();
    constexpr int AtlasWidth=1024;
    struct Cell {int x,y,w;StrokeImage image;};
    std::vector<Cell> cells;
    int x=0,y=0;
    for(const auto& glyph:glyphs) {
        const int w=static_cast<int>(std::ceil(glyph.advance*scale))+2*pad;
        if(x+w>AtlasWidth){x=0;y+=cellHeight;}
        cells.push_back({x,y,w,rasterizeStrokes(parseStrokes(glyph.strokes),scale,radius,-static_cast<float>(pad)/scale,top,w,cellHeight)});
        x+=w;
    }
    const int atlasHeight=y+cellHeight;
    std::vector<float> alpha(static_cast<std::size_t>(AtlasWidth)*atlasHeight,0.0f);
    for(const auto& cell:cells)
        for(int row=0;row<cell.image.height;++row)
            std::copy_n(cell.image.alpha.begin()+static_cast<std::ptrdiff_t>(row)*cell.image.width,cell.image.width,
                        alpha.begin()+static_cast<std::ptrdiff_t>(cell.y+row)*AtlasWidth+cell.x);
    auto atlas=Texture2D::CreateFromPixels(device,AtlasWidth,atlasHeight,premultiplied(alpha));
    std::vector<SharpRuntime::charcs> characters;
    std::vector<Xna::Rectangle> bounds,cropping;
    std::vector<Xna::Vector3> kerning;
    for(std::size_t i=0;i<glyphs.size();++i) {
        const auto& cell=cells[i];
        characters.push_back(glyphs[i].character);
        bounds.emplace_back(cell.x,cell.y,cell.w,cellHeight);
        cropping.emplace_back(0,0,cell.w,cellHeight);
        const float advance=glyphs[i].advance*scale+tracking;
        kerning.emplace_back(-static_cast<float>(pad),static_cast<float>(cell.w),advance-static_cast<float>(cell.w)+static_cast<float>(pad));
    }
    const int lineSpacing=static_cast<int>(std::lround(size*1.32f));
    return std::make_unique<SpriteFont>(atlas,bounds,cropping,characters,lineSpacing,0.0f,kerning,static_cast<SharpRuntime::charcs>('?'));
}

// Disc: anti-aliased; shadow: a smooth falloff from the middle to the edge.
std::vector<float> discAlpha(bool soft)
{
    std::vector<float> alpha(static_cast<std::size_t>(DiscSize)*DiscSize);
    const float r=DiscSize/2.0f;
    for(int y=0;y<DiscSize;++y)
        for(int x=0;x<DiscSize;++x) {
            const float dx=static_cast<float>(x)+0.5f-r,dy=static_cast<float>(y)+0.5f-r;
            const float d=std::sqrt(dx*dx+dy*dy);
            float a;
            if(soft) {const float t=std::clamp(1.0f-d/r,0.0f,1.0f);a=t*t*(3-2*t);}
            else a=std::clamp(r-d+0.5f,0.0f,1.0f);
            alpha[static_cast<std::size_t>(y)*DiscSize+x]=a;
        }
    return alpha;
}
}

struct Style::Resources {
    std::array<std::unique_ptr<SpriteFont>,static_cast<std::size_t>(Font::Count)> fonts;
    std::unique_ptr<Texture2D> white,disc,soft,ramp,icons;
};

Style::Style(Xna::Graphics::GraphicsDevice& device,int width,int height)
    : resources_(std::make_unique<Resources>()),device_(&device),width_(width),height_(height)
{
    scale_=std::max(0.5f,std::min(static_cast<float>(width)/1280.0f,static_cast<float>(height)/720.0f));
    for(std::size_t i=0;i<FontSpecs.size();++i)
        resources_->fonts[i]=makeFont(device,FontSpecs[i].size*scale_,FontSpecs[i].radius,FontSpecs[i].tracking*scale_);
    resources_->white=std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device,1,1,{255,255,255,255}));
    resources_->disc=std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device,DiscSize,DiscSize,premultiplied(discAlpha(false))));
    resources_->soft=std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device,DiscSize,DiscSize,premultiplied(discAlpha(true))));
    std::vector<float> ramp(256);
    for(int i=0;i<256;++i)ramp[static_cast<std::size_t>(i)]=static_cast<float>(i)/255.0f;
    resources_->ramp=std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device,1,256,premultiplied(ramp)));
    // Every icon at 96 px in one row.
    constexpr int Count=static_cast<int>(Icon::Count);
    std::vector<float> icons(static_cast<std::size_t>(IconSize)*Count*IconSize,0.0f);
    for(int index=0;index<Count;++index) {
        const auto image=rasterizeStrokes(parseStrokes(iconStrokes(static_cast<Icon>(index))),IconSize/1000.0f,58.0f,0.0f,1000.0f,IconSize,IconSize);
        for(int row=0;row<IconSize;++row)
            std::copy_n(image.alpha.begin()+static_cast<std::ptrdiff_t>(row)*IconSize,IconSize,
                        icons.begin()+static_cast<std::ptrdiff_t>(row)*IconSize*Count+index*IconSize);
    }
    resources_->icons=std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device,IconSize*Count,IconSize,premultiplied(icons)));
}

Style::~Style()=default;

bool Style::matches(const Xna::Graphics::GraphicsDevice& device,int width,int height) const
{
    return &device==device_&&width==width_&&height==height_;
}

SpriteFont& Style::font(Font font){return *resources_->fonts[static_cast<std::size_t>(font)];}
Texture2D& Style::white(){return *resources_->white;}

Xna::Vector2 Style::measure(Font font,const std::string& text)
{
    return text.empty()?Xna::Vector2(0,static_cast<float>(this->font(font).getLineSpacingProperty())):this->font(font).MeasureString(text);
}

std::string Style::fit(Font font,const std::string& text,float width)
{
    if(measure(font,text).X<=width)return text;
    std::string cut=text;
    while(!cut.empty()&&measure(font,cut+"\xe2\x80\xa6").X>width) {
        cut.pop_back();
        while(!cut.empty()&&(static_cast<unsigned char>(cut.back())&0xC0)==0x80)cut.pop_back();
    }
    while(!cut.empty()&&cut.back()==' ')cut.pop_back();
    return cut+"\xe2\x80\xa6";
}

std::vector<std::string> Style::wrap(Font font,const std::string& text,float width)
{
    std::vector<std::string> lines;
    std::size_t start=0;
    while(start<=text.size()) {
        const auto newline=text.find('\n',start);
        const auto paragraph=text.substr(start,newline==std::string::npos?std::string::npos:newline-start);
        std::string line;
        std::size_t at=0;
        while(at<paragraph.size()) {
            auto end=paragraph.find(' ',at);
            if(end==std::string::npos)end=paragraph.size();
            const auto word=paragraph.substr(at,end-at);
            const auto candidate=line.empty()?word:line+" "+word;
            if(measure(font,candidate).X<=width||line.empty()&&measure(font,word).X<=width) {
                line=candidate;
            } else if(line.empty()) {
                // A word longer than the line is broken where it must be.
                std::string part;
                for(char c:word) {
                    if(measure(font,part+c).X>width&&!part.empty()){lines.push_back(part);part.clear();}
                    part+=c;
                }
                line=part;
            } else {
                lines.push_back(line);
                line=word;
                if(measure(font,line).X>width){at=end-word.size();line.clear();continue;}
            }
            at=end+1;
        }
        lines.push_back(line);
        if(newline==std::string::npos)break;
        start=newline+1;
    }
    return lines;
}

void Style::text(Xna::Graphics::SpriteBatch& batch,Font font,const std::string& text,Xna::Vector2 position,Xna::Color color,Align align)
{
    if(text.empty())return;
    if(align!=Align::Left) {
        const float w=measure(font,text).X;
        position.X-=align==Align::Center?w/2.0f:w;
    }
    batch.DrawString(this->font(font),text,Xna::Vector2(std::round(position.X),std::round(position.Y)),color);
}

void Style::fill(Xna::Graphics::SpriteBatch& batch,const Box& box,Xna::Color color)
{
    if(box.w<=0||box.h<=0)return;
    batch.Draw(white(),Xna::Vector2(box.x,box.y),std::nullopt,color,0.0f,Xna::Vector2::Zero,Xna::Vector2(box.w,box.h),
        Xna::Graphics::SpriteEffects::None,0.0f);
}

namespace {
// Nine pieces of a disc texture stretched over a rounded box.
void nineSlice(Xna::Graphics::SpriteBatch& batch,Texture2D& texture,Texture2D& white,const Box& box,float radius,Xna::Color color)
{
    radius=std::min({radius,box.w/2.0f,box.h/2.0f});
    if(radius<=0.5f) {
        batch.Draw(white,Xna::Vector2(box.x,box.y),std::nullopt,color,0.0f,Xna::Vector2::Zero,Xna::Vector2(box.w,box.h),
            Xna::Graphics::SpriteEffects::None,0.0f);
        return;
    }
    const int half=DiscSize/2;
    const float s=radius/static_cast<float>(half);
    auto piece=[&](int sx,int sy,int sw,int sh,float x,float y,float w,float h) {
        if(w<=0||h<=0)return;
        batch.Draw(texture,Xna::Vector2(x,y),Xna::Rectangle(sx,sy,sw,sh),color,0.0f,Xna::Vector2::Zero,
            Xna::Vector2(w/static_cast<float>(sw),h/static_cast<float>(sh)),Xna::Graphics::SpriteEffects::None,0.0f);
    };
    const float inner=box.w-2*radius,innerH=box.h-2*radius;
    piece(0,0,half,half,box.x,box.y,radius,radius);
    piece(half,0,half,half,box.right()-radius,box.y,radius,radius);
    piece(0,half,half,half,box.x,box.bottom()-radius,radius,radius);
    piece(half,half,half,half,box.right()-radius,box.bottom()-radius,radius,radius);
    // Edges and middle from the texture's centre column/row, which are uniform across.
    piece(half-1,0,2,half,box.x+radius,box.y,inner,radius);
    piece(half-1,half,2,half,box.x+radius,box.bottom()-radius,inner,radius);
    piece(0,half-1,half,2,box.x,box.y+radius,radius,innerH);
    piece(half,half-1,half,2,box.right()-radius,box.y+radius,radius,innerH);
    piece(half-1,half-1,2,2,box.x+radius,box.y+radius,inner,innerH);
    (void)s;
}
}

void Style::rounded(Xna::Graphics::SpriteBatch& batch,const Box& box,float radius,Xna::Color color)
{
    nineSlice(batch,*resources_->disc,white(),box,radius,color);
}

void Style::shadow(Xna::Graphics::SpriteBatch& batch,const Box& box,float radius,float spread,Xna::Color color)
{
    nineSlice(batch,*resources_->soft,white(),Box{box.x-spread,box.y-spread+spread*0.35f,box.w+2*spread,box.h+2*spread},radius+spread,color);
}

void Style::gradient(Xna::Graphics::SpriteBatch& batch,const Box& box,Xna::Color top,Xna::Color bottom)
{
    fill(batch,box,top);
    // The ramp's alpha rises downwards; drawn in the bottom color it blends top into bottom.
    batch.Draw(*resources_->ramp,Xna::Vector2(box.x,box.y),std::nullopt,bottom,0.0f,Xna::Vector2::Zero,Xna::Vector2(box.w,box.h/256.0f),
        Xna::Graphics::SpriteEffects::None,0.0f);
}

void Style::disc(Xna::Graphics::SpriteBatch& batch,Xna::Vector2 center,float radius,Xna::Color color)
{
    batch.Draw(*resources_->disc,Xna::Vector2(center.X-radius,center.Y-radius),std::nullopt,color,0.0f,Xna::Vector2::Zero,
        Xna::Vector2(2*radius/DiscSize,2*radius/DiscSize),Xna::Graphics::SpriteEffects::None,0.0f);
}

void Style::icon(Xna::Graphics::SpriteBatch& batch,Icon icon,const Box& box,Xna::Color color)
{
    batch.Draw(*resources_->icons,Xna::Vector2(box.x,box.y),Xna::Rectangle(static_cast<int>(icon)*IconSize,0,IconSize,IconSize),color,0.0f,
        Xna::Vector2::Zero,Xna::Vector2(box.w/IconSize,box.h/IconSize),Xna::Graphics::SpriteEffects::None,0.0f);
}

void Style::pad(Xna::Graphics::SpriteBatch& batch,PadButton button,Xna::Vector2 center,float size)
{
    struct Look {const char* label;Xna::Color color;bool round;};
    static const Look looks[]{
        {"A",Xna::Color(94,186,86),true},{"B",Xna::Color(222,78,70),true},{"X",Xna::Color(66,128,222),true},
        {"Y",Xna::Color(238,184,54),true},{"LB",Xna::Color(70,80,96),false},{"RB",Xna::Color(70,80,96),false},
        {"START",Xna::Color(70,80,96),false},{"BACK",Xna::Color(70,80,96),false}};
    const auto& look=looks[static_cast<int>(button)];
    if(look.round) {
        disc(batch,center,size/2,look.color);
        const float h=measure(Font::Caption,look.label).Y;
        text(batch,Font::Caption,look.label,Xna::Vector2(center.X,center.Y-h*0.52f),Xna::Color(255,255,255),Align::Center);
    } else {
        const float w=std::max(size*1.4f,measure(Font::Caption,look.label).X+size*0.6f);
        rounded(batch,Box{center.X-w/2,center.Y-size*0.42f,w,size*0.84f},size*0.3f,look.color);
        const float h=measure(Font::Caption,look.label).Y;
        text(batch,Font::Caption,look.label,Xna::Vector2(center.X,center.Y-h*0.52f),Palette::text(),Align::Center);
    }
}

float Style::key(Xna::Graphics::SpriteBatch& batch,const std::string& label,Xna::Vector2 position,float height)
{
    const auto size=measure(Font::Caption,label);
    const float w=std::max(height,size.X+height*0.7f);
    rounded(batch,Box{position.X,position.Y-height/2,w,height},height*0.25f,Xna::Color(200,208,222));
    rounded(batch,Box{position.X+1,position.Y-height/2+1,w-2,height-3},height*0.22f,Xna::Color(48,56,72));
    text(batch,Font::Caption,label,Xna::Vector2(position.X+w/2,position.Y-size.Y*0.55f),Palette::text(),Align::Center);
    return w;
}
}
