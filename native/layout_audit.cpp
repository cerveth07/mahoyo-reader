#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "learning_render.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
std::string utf8(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),int(s.size()),nullptr,0,nullptr,nullptr);std::string o(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.c_str(),int(s.size()),o.data(),n,nullptr,nullptr);return o;}
std::string json(const std::wstring& s){auto u=utf8(s);std::string o="\"";for(unsigned char c:u){if(c=='"'||c=='\\'){o+='\\';o+=c;}else if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else if(c=='\t')o+="\\t";else if(c<32){char b[8];std::snprintf(b,sizeof(b),"\\u%04x",c);o+=b;}else o+=c;}return o+'"';}
bool stop(wchar_t c,bool strong){return std::wstring(strong?L"。！？!?":L"、，,；;：:").find(c)!=std::wstring::npos;}
bool quote(wchar_t c){return std::wstring(L"」』”’〉》】）)\"").find(c)!=std::wstring::npos;}
std::vector<std::size_t> stops(const std::wstring& s,bool strong,std::size_t a,std::size_t b){std::vector<std::size_t> o;for(std::size_t i=a;i<b;++i)if(stop(s[i],strong)){auto e=i+1;while(e<b&&(stop(s[e],strong)||quote(s[e])))++e;if(e<b)o.push_back(e);i=e-1;}return o;}
std::vector<std::pair<std::size_t,std::size_t>> anchors(const std::wstring& ja,const std::wstring& zh){
    auto j=stops(ja,true,0,ja.size()),z=stops(zh,true,0,zh.size());
    std::vector<std::pair<std::size_t,std::size_t>> a;
    if(j.size()!=z.size())return a;
    j.push_back(ja.size());z.push_back(zh.size());std::size_t jb=0,zb=0;
    for(std::size_t i=0;i<j.size();++i){
        auto jw=stops(ja,false,jb,j[i]),zw=stops(zh,false,zb,z[i]);
        bool safe=jw.size()==zw.size();
        if(safe)for(std::size_t k=0;k<jw.size();++k)if(std::abs(double(jw[k]-jb)/std::max<std::size_t>(1,j[i]-jb)-double(zw[k]-zb)/std::max<std::size_t>(1,z[i]-zb))>0.20)safe=false;
        if(safe)for(std::size_t k=0;k<jw.size();++k)a.push_back({jw[k],zw[k]});
        if(i+1<j.size())a.push_back({j[i],z[i]});jb=j[i];zb=z[i];
    }
    return a;
}
bool punctuationOnly(const std::wstring& s){for(auto c:s)if(!iswspace(c) && c!=L'　' && std::wstring(L"。，、！？：；・…‥―—ー「」『』（）“”‘’!?.,:;<>|\"'()-").find(c)==std::wstring::npos)return false;return true;}
struct Measure{HDC dc=CreateCompatibleDC(nullptr);HFONT f{};HGDIOBJ old{};explicit Measure(int px){f=CreateFontW(-px,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Noto Serif JP");old=SelectObject(dc,f);}~Measure(){SelectObject(dc,old);DeleteObject(f);DeleteDC(dc);}int width(const std::wstring&s){SIZE z{};GetTextExtentPoint32W(dc,s.c_str(),int(s.size()),&z);return z.cx;}};
struct Profile{const char*name;int body,column,ruby,pitch;};
std::vector<std::wstring> wrapJa(const std::wstring& s,int column,Measure& measure){
    std::vector<std::wstring> lines;std::size_t pos=0;
    while(pos<s.size()){
        std::size_t end=pos;
        for(std::size_t e=pos+1;e<=s.size();++e){if(measure.width(s.substr(pos,e-pos))>column)break;end=e;}
        if(end==pos)end=pos+1;
        if(end<s.size() && std::wstring(L"、。，．！？）」』”’").find(s[end])!=std::wstring::npos && end>pos+1)--end;
        while(end>pos+1 && std::wstring(L"（「『“‘").find(s[end-1])!=std::wstring::npos)--end;
        lines.push_back(s.substr(pos,end-pos));pos=end;
    }
    return lines;
}
struct Reader{std::ifstream in;explicit Reader(const char*p):in(p,std::ios::binary){}std::uint32_t u(){std::uint32_t v{};if(!in.read(reinterpret_cast<char*>(&v),4))throw std::runtime_error("truncated input");return v;}std::wstring s(){auto n=u();if(n>65536)throw std::runtime_error("bad string");std::wstring o(n,L'\0');if(!in.read(reinterpret_cast<char*>(o.data()),n*2))throw std::runtime_error("truncated UTF16");return o;}};
int inkHeight(const mh::Raster&r){int top=r.h,bottom=0;for(int y=0;y<r.h;++y)for(int x=0;x<r.w;++x)if(r.pixels[std::size_t(y)*r.w+x]>>24){top=std::min(top,y);bottom=std::max(bottom,y+1);}return std::max(1,bottom-top);}
}
int main(int argc,char**argv){
    if(argc!=3 && argc!=4){std::cerr<<"usage: layout_audit input output.jsonl\n";return 2;}
    Reader input(argv[1]);char magic[8];input.in.read(magic,8);bool nativeInput=std::string(magic,8)=="MHLAY002";if(!nativeInput&&std::string(magic,8)!="MHLAY001")return 3;
    auto count=input.u();if(!count || count>100000)return 4;std::ofstream output(argv[2]);
    const Profile profiles[]={{"wide",40,1120,20,64},{"standard",32,928,16,52},{"narrow",32,640,16,52}};
    for(std::uint32_t i=0;i<count;++i){auto id=input.u();auto ja=input.s(),zh=input.s();
        std::vector<mh::Box> nativeBoxes;std::vector<std::wstring> nativeJapanese;int nativeRuby=25;bool exactNativeJapanese=false;
        if(nativeInput){auto rows=input.u();nativeRuby=int(input.u());if(rows>128 || nativeRuby<1 || nativeRuby>256)return 5;std::wstring joined;for(std::uint32_t r=0;r<rows;++r){mh::Box b;b.l=int(input.u());b.t=int(input.u());b.r=int(input.u());b.b=int(input.u());nativeBoxes.push_back(b);nativeJapanese.push_back(input.s());joined+=nativeJapanese.back();}exactNativeJapanese=joined==ja;}
        for(int profileIndex=0;profileIndex<(nativeInput?1:3);++profileIndex){
            auto profile=nativeInput?Profile{"native",56,0,nativeRuby,81}:profiles[profileIndex];
            std::vector<std::wstring> japanese;std::vector<mh::Box> boxes;
            if(nativeInput){japanese=nativeJapanese;boxes=nativeBoxes;}else{
                Measure measure(profile.body);japanese=wrapJa(ja,profile.column,measure);
                for(std::size_t n=0;n<japanese.size();++n)boxes.push_back({0,profile.ruby+4+int(n)*profile.pitch,std::max(1,measure.width(japanese[n])),profile.ruby+4+int(n)*profile.pitch+profile.body});
            }
            for(int mode=(argc==4?1:0);mode<(nativeInput&&!exactNativeJapanese?1:2);++mode){auto plan=mh::planChinese(zh,boxes,mode?japanese:std::vector<std::wstring>{},profile.ruby);auto images=mh::makeChinese(zh,boxes,mode?japanese:std::vector<std::wstring>{},profile.ruby);
                std::wstring joined;bool overflow=false,fragment=false;double effective=plan.pixelHeight;int maxInkHeight=0;
                for(std::size_t row=0;row<plan.rows.size();++row){joined+=plan.rows[row].text;fragment|=punctuationOnly(plan.rows[row].text)&&!punctuationOnly(zh);if(row<images.size()){auto&r=images[row];overflow|=r.x<plan.rows[row].body.l||r.x+r.image.w>plan.rows[row].body.r;int ink=inkHeight(r.image);maxInkHeight=std::max(maxInkHeight,ink);effective=std::min(effective,double(plan.pixelHeight)*std::min(1.0,double(profile.ruby)/ink));}}
                auto paired=nativeInput&&!exactNativeJapanese?std::vector<std::pair<std::size_t,std::size_t>>{}:anchors(ja,zh);int violations=0;std::size_t jpEnd=0,cnEnd=0;
                for(std::size_t row=0;row+1<japanese.size();++row){jpEnd+=japanese[row].size();for(const auto& r:plan.rows)if(r.japaneseLine==int(row))cnEnd+=r.text.size();for(auto a:paired)if(a.first==jpEnd && a.second!=cnEnd)++violations;}
                output<<"{\"index\":"<<id<<",\"profile\":\""<<profile.name<<"\",\"mode\":\""<<(mode?"anchored":"legacy")<<"\",\"pixel_height\":"<<plan.pixelHeight<<",\"effective_px\":"<<std::fixed<<std::setprecision(3)<<effective<<",\"complete\":"<<(joined==zh?"true":"false")<<",\"overflow\":"<<(overflow?"true":"false")<<",\"compressed\":"<<(plan.horizontalCompression?"true":"false")<<",\"punctuation_fragment\":"<<(fragment?"true":"false")<<",\"needs_review\":"<<(plan.needsManualReview?"true":"false")<<",\"relaxed\":"<<(plan.relaxedAnchors?"true":"false")<<",\"hand_reviewed\":"<<(plan.handReviewed?"true":"false")<<",\"grouped_record\":"<<(plan.groupedRecord?"true":"false")<<",\"matched_anchors\":"<<plan.matchedInternalAnchors<<",\"anchor_violations\":"<<violations<<",\"anchor_confidence\":"<<plan.anchorConfidence<<",\"ja\":"<<json(ja)<<",\"zh\":"<<json(zh)<<",\"ja_lines\":[";
                for(std::size_t row=0;row<japanese.size();++row){if(row)output<<',';output<<json(japanese[row]);}output<<"],\"cn_lines\":[";
                for(std::size_t row=0;row<plan.rows.size();++row){if(row)output<<',';output<<json(plan.rows[row].text);}output<<"],\"native_geometry\":"<<(nativeInput?"true":"false")<<",\"exact_ja_lines\":"<<(!nativeInput||exactNativeJapanese?"true":"false")<<",\"ruby_height\":"<<profile.ruby<<",\"caption_max_ink_height\":"<<maxInkHeight<<",\"height_fits_without_resampling\":"<<(maxInkHeight<=profile.ruby?"true":"false")<<",\"outline_pixels\":"<<plan.outlinePixels<<",\"shadow_offset\":"<<plan.shadowOffset<<",\"body_boxes\":[";
                for(std::size_t row=0;row<boxes.size();++row){if(row)output<<',';auto b=boxes[row];output<<'['<<b.l<<','<<b.t<<','<<b.r<<','<<b.b<<']';}output<<"]}\n";
            }
        }
        if((i+1)%200==0)std::cout<<"Audited "<<i+1<<" records\n";
    }
    std::cout<<"Completed "<<count<<" records, "<<(nativeInput?"actual native geometry":"3 metric profiles")<<", legacy vs anchored where source line text is available.\n";
}
