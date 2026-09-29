// SPDX-License-Identifier: MS-PL
#include "GuideStrokes.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>

namespace CNA::Internal::GamerServices::GuideUi {
namespace {
constexpr float Pi=3.14159265f;

std::vector<float> numbers(std::string_view text)
{
    std::vector<float> values;
    std::size_t at=0;
    while(at<text.size()) {
        while(at<text.size()&&text[at]==' ')++at;
        if(at>=text.size())break;
        float value=0;
        const auto [end,error]=std::from_chars(text.data()+at,text.data()+text.size(),value);
        if(error!=std::errc())break;
        values.push_back(value);
        at=static_cast<std::size_t>(end-text.data());
    }
    return values;
}

std::vector<StrokePoint> arc(float cx,float cy,float rx,float ry,float a0,float a1)
{
    const int steps=std::max(2,static_cast<int>(std::ceil(std::fabs(a1-a0)/4.0f)));
    std::vector<StrokePoint> points;
    for(int i=0;i<=steps;++i) {
        const float a=(a0+(a1-a0)*static_cast<float>(i)/static_cast<float>(steps))*Pi/180.0f;
        points.push_back({cx+rx*std::cos(a),cy+ry*std::sin(a)});
    }
    return points;
}

float segmentDistance(StrokePoint p,StrokePoint a,StrokePoint b)
{
    const float dx=b.x-a.x,dy=b.y-a.y;
    const float length=dx*dx+dy*dy;
    float t=length>0?((p.x-a.x)*dx+(p.y-a.y)*dy)/length:0.0f;
    t=std::clamp(t,0.0f,1.0f);
    const float ex=a.x+dx*t-p.x,ey=a.y+dy*t-p.y;
    return std::sqrt(ex*ex+ey*ey);
}
}

StrokeShape parseStrokes(std::string_view text)
{
    StrokeShape shape;
    std::size_t start=0;
    while(start<text.size()) {
        auto end=text.find(';',start);
        if(end==std::string_view::npos)end=text.size();
        auto command=text.substr(start,end-start);
        start=end+1;
        while(!command.empty()&&command.front()==' ')command.remove_prefix(1);
        if(command.empty())continue;
        const char kind=command.front();
        const auto v=numbers(command.substr(1));
        std::vector<StrokePoint> points;
        switch(kind) {
        case 'L':
        case 'P':
            for(std::size_t i=0;i+1<v.size();i+=2)points.push_back({v[i],v[i+1]});
            (kind=='L'?shape.lines:shape.fills).push_back(std::move(points));
            break;
        case 'A':
            if(v.size()==6)shape.lines.push_back(arc(v[0],v[1],v[2],v[3],v[4],v[5]));
            break;
        case 'D':
            if(v.size()==2)shape.lines.push_back({{v[0],v[1]}});
            break;
        case 'C':
            if(v.size()==3)shape.fills.push_back(arc(v[0],v[1],v[2],v[2],0,360));
            break;
        default:
            break;
        }
    }
    return shape;
}

StrokeImage rasterizeStrokes(const StrokeShape& shape,float scale,float radius,float originX,float originY,int width,int height)
{
    StrokeImage image{width,height,std::vector<float>(static_cast<std::size_t>(width)*height,0.0f)};
    const float pixel=1.0f/scale;
    for(int y=0;y<height;++y)
        for(int x=0;x<width;++x) {
            const StrokePoint p{originX+(static_cast<float>(x)+0.5f)*pixel,originY-(static_cast<float>(y)+0.5f)*pixel};
            float best=1e9f;
            for(const auto& line:shape.lines) {
                if(line.size()==1)best=std::min(best,segmentDistance(p,line[0],line[0]));
                for(std::size_t i=0;i+1<line.size();++i) {
                    const auto& a=line[i];const auto& b=line[i+1];
                    // Only segments that can reach this pixel.
                    if(p.x<std::min(a.x,b.x)-radius-pixel||p.x>std::max(a.x,b.x)+radius+pixel||
                       p.y<std::min(a.y,b.y)-radius-pixel||p.y>std::max(a.y,b.y)+radius+pixel)continue;
                    best=std::min(best,segmentDistance(p,a,b));
                }
            }
            float coverage=std::clamp((radius-best)*scale+0.5f,0.0f,1.0f);
            for(const auto& fill:shape.fills) {
                bool inside=false;
                float edge=1e9f;
                for(std::size_t i=0,j=fill.size()-1;i<fill.size();j=i++) {
                    const auto& a=fill[i];const auto& b=fill[j];
                    if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
                    edge=std::min(edge,segmentDistance(p,a,b));
                }
                const float signedEdge=inside?edge:-edge;
                coverage=std::max(coverage,std::clamp(signedEdge*scale+0.5f,0.0f,1.0f));
            }
            image.alpha[static_cast<std::size_t>(y)*width+x]=coverage;
        }
    return image;
}

const std::vector<Glyph>& systemGlyphs()
{
    // The CNA system typeface: a rounded monoline sans on a 1000-unit em (x-height 520, capitals
    // and figures 720, ascenders 760, descenders 210). Each glyph is its skeleton; the stroke width
    // is chosen when it is rasterized (regular and bold).
    static const std::vector<Glyph> glyphs{
        {u' ',260,""},
        {u'!',220,"L 110 720 110 240;D 110 45"},
        {u'"',350,"L 110 720 110 520;L 240 720 240 520"},
        {u'#',680,"L 190 0 270 720;L 430 0 510 720;L 70 250 590 250;L 100 470 620 470"},
        {u'$',600,"A 300 510 190 140 28 270;A 300 230 205 140 90 -155;L 300 760 300 -40"},
        {u'%',680,"A 180 570 110 140 0 360;A 500 150 110 140 0 360;L 90 0 590 720"},
        {u'&',660,"A 290 570 125 150 215 -35;L 392 484 110 215;A 285 190 180 190 175 330;L 196 484 620 0"},
        {u'\'',220,"L 110 720 110 520"},
        {u'(',340,"A 350 330 230 470 118 242"},
        {u')',340,"A -10 330 230 470 62 -62"},
        {u'*',440,"L 220 720 220 450;L 100 655 340 515;L 340 655 100 515"},
        {u'+',520,"L 70 300 450 300;L 260 110 260 490"},
        {u',',220,"L 120 60 80 -110"},
        {u'-',420,"L 70 290 350 290"},
        {u'.',220,"D 110 45"},
        {u'/',430,"L 50 -60 380 760"},
        {u'0',600,"A 300 360 230 360 0 360"},
        {u'1',600,"L 150 560 340 720 340 0"},
        {u'2',600,"A 300 520 215 200 162 -32;L 482 414 90 0 530 0"},
        {u'3',600,"A 295 545 205 175 155 -90;A 295 185 225 185 90 -160"},
        {u'4',600,"L 430 0 430 720 70 230 560 230"},
        {u'5',600,"L 500 720 135 720 121 370;A 305 235 225 235 145 -150"},
        {u'6',600,"A 300 225 225 225 0 360;A 330 290 255 420 70 180;L 75 290 75 225"},
        {u'7',600,"L 70 720 530 720 230 0"},
        {u'8',600,"A 300 550 190 170 0 360;A 300 190 220 190 0 360"},
        {u'9',600,"A 300 495 225 225 0 360;A 270 430 255 420 0 -110;L 525 495 525 430"},
        {u':',220,"D 110 45;D 110 450"},
        {u';',220,"D 110 450;L 120 60 80 -110"},
        {u'<',520,"L 450 560 80 320 450 80"},
        {u'=',520,"L 70 380 450 380;L 70 210 450 210"},
        {u'>',520,"L 70 560 440 320 70 80"},
        {u'?',520,"A 270 545 190 175 160 -70;L 335 381 270 300 270 220;D 270 45"},
        {u'@',760,"A 380 300 310 330 -30 330;A 370 300 115 135 0 360;L 485 300 485 180 560 110"},
        {u'A',660,"L 40 0 330 720 620 0;L 150 250 510 250"},
        {u'B',640,"L 90 0 90 720 360 720;A 360 545 175 175 90 -90;L 90 370 380 370;A 380 185 185 185 90 -90;L 380 0 90 0"},
        {u'C',700,"A 395 360 320 360 45 315"},
        {u'D',720,"L 90 0 90 720 310 720;A 310 360 300 360 90 -90;L 310 0 90 0"},
        {u'E',590,"L 520 720 90 720 90 0 520 0;L 90 370 460 370"},
        {u'F',560,"L 520 720 90 720 90 0;L 90 370 460 370"},
        {u'G',770,"A 395 360 320 360 45 360;L 715 360 450 360"},
        {u'H',660,"L 90 0 90 720;L 570 0 570 720;L 90 370 570 370"},
        {u'I',210,"L 105 0 105 720"},
        {u'J',490,"L 400 720 400 230;A 235 230 165 230 0 -180"},
        {u'K',620,"L 90 0 90 720;L 570 720 90 250;L 260 420 590 0"},
        {u'L',540,"L 90 720 90 0 500 0"},
        {u'M',780,"L 80 0 110 720 390 140 670 720 700 0"},
        {u'N',660,"L 90 0 90 720 570 0 570 720"},
        {u'O',790,"A 395 360 325 360 0 360"},
        {u'P',610,"L 90 0 90 720 350 720;A 350 530 190 190 90 -90;L 350 340 90 340"},
        {u'Q',790,"A 395 360 325 360 0 360;L 470 180 700 -40"},
        {u'R',630,"L 90 0 90 720 350 720;A 350 530 190 190 90 -90;L 350 340 90 340;L 340 340 570 0"},
        {u'S',660,"A 330 545 235 175 28 270;A 330 175 250 175 90 -155"},
        {u'T',640,"L 40 720 600 720;L 320 720 320 0"},
        {u'U',660,"L 90 720 90 270;A 330 270 240 270 180 360;L 570 270 570 720"},
        {u'V',660,"L 30 720 330 0 630 720"},
        {u'W',920,"L 30 720 230 0 460 620 690 0 890 720"},
        {u'X',630,"L 60 720 570 0;L 570 720 60 0"},
        {u'Y',640,"L 40 720 320 360 600 720;L 320 360 320 0"},
        {u'Z',640,"L 70 720 570 720 70 0 580 0"},
        {u'[',320,"L 250 780 100 780 100 -120 250 -120"},
        {u'\\',430,"L 50 760 380 -60"},
        {u']',320,"L 70 780 220 780 220 -120 70 -120"},
        {u'^',520,"L 90 480 260 720 430 480"},
        {u'_',500,"L 30 -90 470 -90"},
        {u'`',280,"L 90 760 190 640"},
        {u'a',640,"A 300 260 225 260 0 360;L 525 520 525 0"},
        {u'b',640,"L 80 760 80 0;A 305 260 225 260 0 360"},
        {u'c',580,"A 305 260 230 260 45 315"},
        {u'd',640,"A 295 260 225 260 0 360;L 520 760 520 0"},
        {u'e',610,"L 80 260 530 260;A 305 260 225 260 0 320"},
        {u'f',400,"L 190 0 190 600;A 320 600 130 140 180 20;L 60 500 350 500"},
        {u'g',640,"A 295 260 225 260 0 360;L 520 520 520 -60;A 290 -60 230 150 0 -165"},
        {u'h',620,"L 80 760 80 0;A 305 290 225 230 158 0;L 530 290 530 0"},
        {u'i',210,"L 105 520 105 0;D 105 690"},
        {u'j',250,"L 160 520 160 -60;A 60 -60 100 150 0 -140;D 160 690"},
        {u'k',520,"L 80 760 80 0;L 460 520 80 210;L 200 320 480 0"},
        {u'l',210,"L 105 760 105 0"},
        {u'm',770,"L 80 520 80 0;A 230 330 150 190 150 0;L 380 330 380 0;A 530 330 150 190 150 0;L 680 330 680 0"},
        {u'n',620,"L 80 520 80 0;A 305 290 225 230 158 0;L 530 290 530 0"},
        {u'o',610,"A 305 260 235 260 0 360"},
        {u'p',640,"L 80 520 80 -210;A 305 260 225 260 0 360"},
        {u'q',640,"A 295 260 225 260 0 360;L 520 520 520 -210"},
        {u'r',420,"L 80 520 80 0;A 290 290 210 230 158 60"},
        {u's',560,"A 285 390 185 130 25 270;A 285 130 200 130 90 -155"},
        {u't',420,"L 190 700 190 110;A 300 110 110 110 180 300;L 60 520 380 520"},
        {u'u',620,"L 80 520 80 230;A 305 230 225 230 180 338;L 530 520 530 0"},
        {u'v',580,"L 50 520 290 0 530 520"},
        {u'w',760,"L 40 520 210 0 380 460 550 0 720 520"},
        {u'x',550,"L 70 520 480 0;L 480 520 70 0"},
        {u'y',580,"L 50 520 335 30;L 530 520 240 -210"},
        {u'z',560,"L 80 520 470 520 80 0 480 0"},
        {u'{',360,"L 270 780 230 780 190 740 190 400 120 330 190 260 190 -80 230 -120 270 -120"},
        {u'|',220,"L 110 780 110 -120"},
        {u'}',360,"L 90 780 130 780 170 740 170 400 240 330 170 260 170 -80 130 -120 90 -120"},
        {u'~',520,"A 165 300 95 70 180 0;A 355 300 95 70 180 360"},
        {u'×',500,"L 90 110 410 430;L 410 110 90 430"},
        {u'\u2018',220,"L 130 520 90 700"},
        {u'\u2019',220,"L 130 700 90 520"},
        {u'\u201c',350,"L 130 520 90 700;L 260 520 220 700"},
        {u'\u201d',350,"L 130 700 90 520;L 260 700 220 520"},
        {u'\u2013',520,"L 70 290 450 290"},
        {u'\u2014',800,"L 70 290 730 290"},
        {u'•',300,"C 150 290 80"},
        {u'…',660,"D 110 45;D 330 45;D 550 45"},
    };
    return glyphs;
}

std::string_view iconStrokes(Icon icon)
{
    // The CNA icon family, in the typeface's monoline style (1000-unit box).
    static const std::array<std::string,static_cast<std::size_t>(Icon::Count)> icons=[] {
        std::array<std::string,static_cast<std::size_t>(Icon::Count)> table;
        auto set=[&](Icon icon,std::string text){table[static_cast<std::size_t>(icon)]=std::move(text);};
        set(Icon::Person,"A 500 690 150 150 0 360;A 500 170 280 280 20 160");
        set(Icon::People,"A 400 660 140 140 0 360;A 400 160 260 260 20 160;A 700 720 110 110 0 360;A 700 330 200 170 35 145");
        set(Icon::Party,"A 500 700 120 120 0 360;A 210 610 95 95 0 360;A 790 610 95 95 0 360;A 500 210 230 230 25 155;A 210 200 170 170 40 150;A 790 200 170 170 30 140");
        set(Icon::Message,"L 120 220 880 220 880 780 120 780 120 220;L 120 780 500 460 880 780");
        set(Icon::Invite,"L 90 180 690 180 690 620 90 620 90 180;L 90 620 390 400 690 620;L 810 580 810 900;L 660 740 960 740");
        set(Icon::Trophy,"A 500 640 230 330 180 360;L 270 640 730 640;A 270 540 110 110 90 270;A 730 540 110 110 90 -90;L 500 310 500 200;L 380 200 620 200;L 320 130 680 130");
        set(Icon::Controller,"L 290 690 710 690;A 710 500 190 190 90 -40;L 855 378 760 250 640 330 360 330 240 250 145 378;A 290 500 190 190 220 90;L 290 440 290 600;L 210 520 370 520;D 680 560;D 760 480");
        set(Icon::Store,"L 180 690 820 690 760 120 240 120 180 690;A 500 690 150 170 0 180");
        {
            std::string gear="A 500 500 220 220 0 360;A 500 500 85 85 0 360";
            for(int tooth=0;tooth<8;++tooth) {
                const float a=static_cast<float>(tooth)*Pi/4.0f;
                gear+=";L "+std::to_string(500+280*std::cos(a))+" "+std::to_string(500+280*std::sin(a))+" "+
                      std::to_string(500+380*std::cos(a))+" "+std::to_string(500+380*std::sin(a));
            }
            set(Icon::Settings,gear);
        }
        std::string star,outline="L";
        for(int point=0;point<10;++point) {
            const float a=(90.0f+36.0f*static_cast<float>(point))*Pi/180.0f;
            const float r=point%2?170.0f:400.0f;
            const auto x=std::to_string(500+r*std::cos(a)),y=std::to_string(480+r*std::sin(a));
            star+=" "+x+" "+y;
            outline+=" "+x+" "+y;
        }
        outline+=" 500 880";
        set(Icon::Star,"P"+star);
        set(Icon::StarOutline,outline);
        set(Icon::Check,"L 180 500 400 280 820 720");
        set(Icon::Cross,"L 230 230 770 770;L 770 230 230 770");
        set(Icon::Plus,"L 500 180 500 820;L 180 500 820 500");
        set(Icon::ChevronRight,"L 380 820 700 500 380 180");
        set(Icon::ChevronLeft,"L 620 820 300 500 620 180");
        set(Icon::Lock,"L 220 120 780 120 780 520 220 520 220 120;A 500 560 190 230 0 180;D 500 320");
        set(Icon::Globe,"A 500 500 360 360 0 360;A 500 500 150 360 0 360;L 140 500 860 500;L 210 710 790 710;L 210 290 790 290");
        set(Icon::Moon,"A 480 500 330 330 60 300;A 680 500 240 330 125 235;L 645 786 542 770;L 645 214 542 230");
        set(Icon::Busy,"A 500 500 330 330 0 360;L 320 500 680 500");
        set(Icon::Crown,"L 170 250 170 700 350 480 500 760 650 480 830 700 830 250 170 250");
        set(Icon::Speaker,"L 150 380 320 380 540 180 540 820 320 620 150 620 150 380;A 560 500 180 220 -50 50;A 560 500 320 360 -50 50");
        set(Icon::Clock,"A 500 500 360 360 0 360;L 500 740 500 500 660 400");
        set(Icon::Search,"A 420 580 240 240 0 360;L 600 400 850 150");
        set(Icon::PersonAdd,"A 400 690 140 140 0 360;A 400 170 260 260 20 160;L 800 460 800 760;L 650 610 950 610");
        set(Icon::SignOut,"L 560 150 200 150 200 850 560 850;L 440 500 900 500;L 760 640 900 500 760 360");
        set(Icon::Pencil,"L 220 180 250 350 720 820 850 690 380 220 220 180;L 640 740 770 610");
        set(Icon::Download,"L 500 850 500 330;L 330 500 500 330 670 500;L 180 180 820 180");
        set(Icon::Info,"A 500 500 380 380 0 360;L 500 560 500 280;D 500 700");
        set(Icon::Leaderboard,"L 150 150 150 450 380 450 380 150;L 380 150 380 700 620 700 620 150;L 620 150 620 350 850 350 850 150;L 100 150 900 150");
        set(Icon::Home,"L 150 480 500 820 850 480;L 250 580 250 150 750 150 750 580;L 430 150 430 380 570 380 570 150");
        set(Icon::Warning,"L 500 870 90 150 910 150 500 870;L 500 620 500 380;D 500 250");
        set(Icon::Error,"A 500 500 380 380 0 360;L 360 360 640 640;L 640 360 360 640");
        set(Icon::Question,"A 500 500 380 380 0 360;A 500 600 120 110 160 -70;L 541 497 500 440 500 400;D 500 270");
        return table;
    }();
    return icons[static_cast<std::size_t>(icon)];
}
}
