#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "learning_render.h"
#include "reader_palette.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <limits>
#include <utility>

namespace mh {
namespace {bool storyFontLoaded=false;std::wstring japaneseFaceOverride;}
bool loadReaderFont(const std::wstring& file){
    if(!storyFontLoaded)storyFontLoaded=AddFontResourceExW(file.c_str(),FR_PRIVATE,nullptr)>0;
    return storyFontLoaded;
}
const wchar_t* readerFace(){return storyFontLoaded?L"Noto Serif SC Medium":L"SimSun";}
namespace {

struct Dc {
    HDC value=CreateCompatibleDC(nullptr);
    Dc()=default;
    ~Dc(){ if(value) DeleteDC(value); }
    Dc(const Dc&)=delete;
    Dc& operator=(const Dc&)=delete;
};
struct GdiObject {
    HGDIOBJ value=nullptr;
    explicit GdiObject(HGDIOBJ object=nullptr):value(object){}
    ~GdiObject(){ if(value) DeleteObject(value); }
    GdiObject(const GdiObject&)=delete;
    GdiObject& operator=(const GdiObject&)=delete;
};
struct Select {
    HDC dc=nullptr; HGDIOBJ old=nullptr;
    Select(HDC dc_,HGDIOBJ object):dc(dc_) {
        if(dc && object) old=SelectObject(dc,object);
    }
    ~Select(){ if(dc && old && old!=HGDI_ERROR) SelectObject(dc,old); }
    Select(const Select&)=delete;
    Select& operator=(const Select&)=delete;
};
int CALLBACK foundFamily(const LOGFONTW*,const TEXTMETRICW*,DWORD,LPARAM value){
    *reinterpret_cast<bool*>(value)=true;
    return 0;
}
const wchar_t* installedFamily(std::initializer_list<const wchar_t*> names,const wchar_t* fallback){
    Dc dc;
    if(dc.value)for(const wchar_t* name:names){
        if(!*name||lstrlenW(name)>=LF_FACESIZE)continue;
        LOGFONTW query{};query.lfCharSet=DEFAULT_CHARSET;
        lstrcpynW(query.lfFaceName,name,LF_FACESIZE);
        bool found=false;
        EnumFontFamiliesExW(dc.value,&query,foundFamily,reinterpret_cast<LPARAM>(&found),0);
        if(found)return name;
    }
    return fallback;
}
HFONT font(int height,const wchar_t* face,int weight=FW_NORMAL) {
    return CreateFontW(-std::clamp(height,2,256),0,0,0,weight,FALSE,FALSE,
                       FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE,face?face:readerFace());
}
struct Measurer {
    Dc dc;
    GdiObject glyphFont;
    Select selected;
    explicit Measurer(int px,const wchar_t* face=readerFace(),int weight=FW_NORMAL)
        :glyphFont(font(px,face,weight)),selected(dc.value,glyphFont.value){}
    int width(const std::wstring& text) const {
        SIZE s{};
        if(!dc.value || text.empty()) return 0;
        if(text.size()>static_cast<std::size_t>(std::numeric_limits<int>::max())) return 0;
        return GetTextExtentPoint32W(dc.value,text.c_str(),static_cast<int>(text.size()),&s) ? s.cx : 0;
    }
};
bool closing(wchar_t c) {
    static const std::wstring chars=L"，。、！？：；）》】〉」』”’…‥―—ー％%!?;:,.)]}";
    return chars.find(c)!=std::wstring::npos;
}
bool opening(wchar_t c) {
    static const std::wstring chars=L"（《【〈「『“‘([{“";
    return chars.find(c)!=std::wstring::npos;
}
bool punctuationOnly(const std::wstring& text) {
    static const std::wstring marks=L"。，、！？：；・…‥―—ー「」『』（）“”‘’!?.,:;<>|\"'()-";
    for(auto c:text)if(c!=L' '&&c!=L'　'&&c!=L'\t'&&c!=L'\r'&&c!=L'\n'&&marks.find(c)==std::wstring::npos)return false;
    return true;
}
std::size_t step(const std::wstring& s,std::size_t i) {
    if(i<s.size() && s[i]>=0xd800 && s[i]<=0xdbff && i+1<s.size() &&
       s[i+1]>=0xdc00 && s[i+1]<=0xdfff) return i+2;
    return i+1;
}
std::size_t previousCharacter(const std::wstring& s,std::size_t i) {
    if(i==0)return 0;
    --i;
    if(i>0 && s[i]>=0xdc00 && s[i]<=0xdfff && s[i-1]>=0xd800 && s[i-1]<=0xdbff)--i;
    return i;
}
std::wstring oneLine(std::wstring s) {
    std::replace(s.begin(),s.end(),L'\r',L' ');
    std::replace(s.begin(),s.end(),L'\n',L' ');
    std::replace(s.begin(),s.end(),L'\t',L' ');
    return s;
}

// Return a maximal prefix which fits, with Chinese/Japanese closing marks kept
// off the next line and opening marks kept off this line's right edge.
std::size_t prefix(const std::wstring& s,std::size_t start,int width,
                   const Measurer& measure) {
    if(start>=s.size() || width<=0) return start;
    std::size_t end=start;
    for(std::size_t next=step(s,start); next<=s.size(); next=step(s,next)) {
        if(measure.width(s.substr(start,next-start))>width) break;
        end=next;
        if(next==s.size()) break;
    }
    if(end==start) return start;
    if(end<s.size() && closing(s[end])) {
        std::size_t candidate=end;
        // Make room for the closing mark by moving the preceding full character
        // to the following line. This retains punctuation without overhanging.
        while(candidate>start && closing(s[candidate]))candidate=previousCharacter(s,candidate);
        if(candidate>start) end=candidate;
        else return start;
    }
    // A whole script record may deliberately be just an opening quotation
    // mark (chapter record 1043). Kinsoku applies only when text continues on
    // the next visual line; it must not force such a record down to 6 pixels.
    if(end<s.size())while(end>start && opening(s[previousCharacter(s,end)]))end=previousCharacter(s,end);
    return end;
}
std::vector<std::wstring> wrap(const std::wstring& text,
                               const std::vector<int>& widths,
                               const Measurer& measure,bool& fits) {
    std::vector<std::wstring> lines;
    std::size_t pos=0;
    for(int width:widths) {
        if(pos>=text.size()) break;
        std::size_t end=prefix(text,pos,width,measure);
        if(end==pos) { fits=false; return {}; }
        lines.push_back(text.substr(pos,end-pos));
        pos=end;
    }
    fits=pos==text.size();
    return lines;
}

struct ClauseMap {
    std::vector<std::pair<std::size_t,std::size_t>> anchors;
    std::vector<std::size_t> rowEnds;
    bool ambiguous=false;
    bool reviewed=false;
    bool groupWholeRecord=false;
    std::size_t mergeTailFrom=std::numeric_limits<std::size_t>::max();
    std::vector<std::pair<std::size_t,std::size_t>> sourceBoundarySnaps;
};
std::uint64_t utf16Fingerprint(const std::wstring& text){
    std::uint64_t h=14695981039346656037ull;
    for(auto c:text){auto unit=std::uint16_t(c);h=(h^(unit&255))*1099511628211ull;h=(h^(unit>>8))*1099511628211ull;}
    return h;
}
bool reviewedClauses(const std::wstring& ja,const std::wstring& zh,ClauseMap& map,bool highlightOnly=false){
    const auto j=utf16Fingerprint(ja),c=utf16Fingerprint(zh);
    struct Plan {
        size_t jpLength, cnLength;
        uint64_t jpHash, cnHash;
        std::vector<std::pair<size_t,size_t>> anchors, snaps;
        bool whole;
        size_t mergeTail;
        bool highlightOnly;
    };
    static const Plan plans[]={
#include "../build/reviewed_plans.inc"
    };
    for(const auto& plan:plans){
        if(plan.highlightOnly!=highlightOnly||plan.jpLength!=ja.size() || plan.cnLength!=zh.size() ||
           plan.jpHash!=j || plan.cnHash!=c) continue;
        map.anchors=plan.anchors;
        map.sourceBoundarySnaps=plan.snaps;
        map.groupWholeRecord=plan.whole;
        map.mergeTailFrom=plan.mergeTail;
        map.reviewed=true;
        return true;
    }
    return false;
}
bool strongStop(wchar_t c){return std::wstring(L"。！？!?").find(c)!=std::wstring::npos;}
bool weakStop(wchar_t c){return std::wstring(L"、，,；;：:").find(c)!=std::wstring::npos;}
bool closingQuote(wchar_t c){return std::wstring(L"」』”’〉》】）)\"").find(c)!=std::wstring::npos;}
std::vector<std::size_t> stops(const std::wstring& text,bool strong,
                               std::size_t begin,std::size_t end) {
    std::vector<std::size_t> result;
    for(std::size_t p=begin;p<end;++p) {
        if(!(strong?strongStop(text[p]):weakStop(text[p])))continue;
        auto after=p+1;
        while(after<end && ((strong?strongStop(text[after]):weakStop(text[after])) || closingQuote(text[after])))++after;
        if(after<end)result.push_back(after);
        p=after-1;
    }
    return result;
}
ClauseMap clauseMap(const std::wstring& zh,const std::vector<std::wstring>& rows) {
    ClauseMap map;
    std::wstring ja;
    for(const auto& row:rows){ja+=oneLine(row);map.rowEnds.push_back(ja.size());}
    if(ja.empty() || zh.empty())return map;
    if(reviewedClauses(ja,zh,map))return map;
    map.anchors.push_back({0,0});
    auto js=stops(ja,true,0,ja.size()),cs=stops(zh,true,0,zh.size());
    if(js.size()!=cs.size()) {
        map.ambiguous=true;
        map.anchors.push_back({ja.size(),zh.size()});
        return map;
    }
    js.push_back(ja.size());cs.push_back(zh.size());
    std::size_t jb=0,cb=0;
    for(std::size_t part=0;part<js.size();++part) {
        auto jw=stops(ja,false,jb,js[part]),cw=stops(zh,false,cb,cs[part]);
        bool weakPairsSafe=jw.size()==cw.size();
        // Equal comma counts do not prove equal meanings: afterthoughts and
        // quoted speech are frequently reordered by the translator. Large
        // relative boundary shifts are kept as a whole-sentence pair instead.
        if(weakPairsSafe)for(std::size_t k=0;k<jw.size();++k){
            double j=double(jw[k]-jb)/std::max<std::size_t>(1,js[part]-jb);
            double c=double(cw[k]-cb)/std::max<std::size_t>(1,cs[part]-cb);
            if(std::abs(j-c)>0.20)weakPairsSafe=false;
        }
        if(weakPairsSafe)for(std::size_t k=0;k<jw.size();++k)map.anchors.push_back({jw[k],cw[k]});
        else map.ambiguous=true;
        map.anchors.push_back({js[part],cs[part]});jb=js[part];cb=cs[part];
    }
    return map;
}

bool rowAnchorCost(const ClauseMap* map,std::size_t row,std::size_t chineseEnd,
                    double& extra) {
    extra=0;
    if(!map || row>=map->rowEnds.size() || map->anchors.size()<2)return true;
    auto jp=map->rowEnds[row];
    // In the reviewed reporting clauses, an immediately following quotative
    // particle has no independent Chinese word. Keep the Chinese report whole
    // when native wrapping puts that one Japanese particle on the upper row.
    for(auto snap:map->sourceBoundarySnaps)if(jp==snap.first){jp=snap.second;break;}
    for(std::size_t i=1;i<map->anchors.size();++i) {
        auto left=map->anchors[i-1],right=map->anchors[i];
        if(jp>right.first)continue;
        // An actual Japanese row ending at a matched sentence/clause end must
        // show that clause's complete translation, not a similarly wide slice.
        if(jp==right.first)return chineseEnd==right.second;
        if(chineseEnd<left.second || chineseEnd>right.second)return false;
        double fraction=double(jp-left.first)/std::max<std::size_t>(1,right.first-left.first);
        double target=left.second+fraction*(right.second-left.second);
        double difference=(double(chineseEnd)-target)/std::max<std::size_t>(1,right.second-left.second);
        extra=4*difference*difference;
        return true;
    }
    return true;
}

// Keep the translation visually attached to every natural line of a long
// Japanese sentence. The optimal split uses the same count of rows and gives
// each row a proportional amount of Chinese, constrained by its true width.
// It changes no source wording and does not claim word-for-word alignment.
std::vector<std::wstring> balancedWrap(const std::wstring& text,
                                      const std::vector<int>& widths,
                                      const Measurer& measure,bool& fits,
                                      const ClauseMap* clauses=nullptr) {
    fits=false;
    if(widths.size()<2 || text.empty())return {};
    std::vector<std::size_t> boundaries{0};
    for(std::size_t p=0;p<text.size();) {p=step(text,p);boundaries.push_back(p);}
    const std::size_t count=boundaries.size()-1;
    const std::size_t rows=std::min(widths.size(),count);
    if(rows<2 || rows>64 || count>2048)return {};
    std::vector<int> advances(count+1,0);
    for(std::size_t i=1;i<=count;++i)advances[i]=measure.width(text.substr(0,boundaries[i]));
    int total=advances.back(), capacity=0;
    for(std::size_t i=0;i<rows;++i)capacity+=widths[i];
    if(total>capacity || total<=0)return {};
    const double infinity=std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> costs(rows+1,std::vector<double>(count+1,infinity));
    std::vector<std::vector<std::size_t>> previous(rows+1,std::vector<std::size_t>(count+1,0));
    costs[0][0]=0;
    const bool hasBody=!punctuationOnly(text);
    for(std::size_t row=1;row<=rows;++row) {
        for(std::size_t end=row;end<=count-(rows-row);++end) {
            if(end<count && opening(text[boundaries[end]-1]))continue;
            for(std::size_t start=row-1;start<end;++start) {
                if(!std::isfinite(costs[row-1][start]))continue;
                if(start>0 && closing(text[boundaries[start]]))continue;
                if(hasBody && punctuationOnly(text.substr(boundaries[start],boundaries[end]-boundaries[start])))continue;
                int segmentWidth=advances[end]-advances[start];
                if(segmentWidth>widths[row-1])continue;
                double anchorCost=0;
                if(!rowAnchorCost(clauses,row-1,boundaries[end],anchorCost))continue;
                double difference=double(segmentWidth)/std::max(1,total)-double(widths[row-1])/capacity;
                double cost=costs[row-1][start]+difference*difference+anchorCost;
                wchar_t tail=text[boundaries[end]-1];
                // Prefer a complete clause when both rows fit. Keeping a
                // connective with its clause matters more than equal width.
                if(tail==L'，' || tail==L'。' || tail==L'；' || tail==L'、')cost-=0.12;
                if(cost<costs[row][end]){costs[row][end]=cost;previous[row][end]=start;}
            }
        }
    }
    if(!std::isfinite(costs[rows][count]))return {};
    std::vector<std::wstring> out(rows);
    std::size_t end=count;
    for(std::size_t row=rows;row>0;--row) {
        auto start=previous[row][end];
        out[row-1]=text.substr(boundaries[start],boundaries[end]-boundaries[start]);
        if(measure.width(out[row-1])>widths[row-1])return {};
        end=start;
    }
    fits=end==0;
    return out;
}

bool leadingParticle(const std::wstring& text,std::size_t pos){
    // Allow the complete temporal word 之前 at the start of a row.
    return pos<text.size() && text[pos] && std::wcschr(L"的了着过地得吗呢吧啊们之",text[pos]) &&
        text.compare(pos,2,L"之前")!=0;
}
bool preferredStop(wchar_t c){return c&&std::wcschr(L"。！？；，、…」』”",c);}
void polishCaptionBreaks(std::vector<std::wstring>& lines,const std::vector<int>& widths,
                         const Measurer& measure,const ClauseMap* clauses){
    if(lines.size()<2||(clauses&&clauses->reviewed))return;
    std::vector<std::size_t> originalEnds;
    std::size_t offset=0;
    for(const auto& line:lines){offset+=line.size();originalEnds.push_back(offset);}
    offset=0;
    for(std::size_t row=0;row+1<lines.size();++row){
        auto pair=lines[row]+lines[row+1];
        const auto split=lines[row].size(),original=originalEnds[row]-offset;
        std::vector<std::size_t> boundaries{0};
        for(std::size_t p=0;p<pair.size();){p=step(pair,p);boundaries.push_back(p);}
        const auto center=std::lower_bound(boundaries.begin(),boundaries.end(),original)-boundaries.begin();
        const bool bad=leadingParticle(pair,split)||opening(pair[previousCharacter(pair,split)]);
        const int originalWidth=measure.width(pair.substr(0,original));
        int bestRank=bad?2:(preferredStop(pair[previousCharacter(pair,split)])?0:1),bestDistance=0;
        std::size_t best=split;
        for(std::ptrdiff_t i=1;i+1<std::ptrdiff_t(boundaries.size());++i){
            const auto end=boundaries[i],tail=previousCharacter(pair,end);
            if(end==split||leadingParticle(pair,end)||closing(pair[end])||opening(pair[tail]))continue;
            const int rank=preferredStop(pair[tail])?0:1;
            if(rank>bestRank||(rank==1&&!bad))continue;
            int width=measure.width(pair.substr(0,end)),distance=std::abs(width-originalWidth);
            if(std::abs(i-center)>4 && distance*100>widths[row]*15)continue;
            if(rank==bestRank&&distance>=bestDistance)continue;
            double anchorCost=0;
            if(!rowAnchorCost(clauses,row,offset+end,anchorCost))continue;
            auto right=pair.substr(end);
            if(width>widths[row]||measure.width(right)>widths[row+1]||
               punctuationOnly(pair.substr(0,end))||punctuationOnly(right))continue;
            best=end;bestRank=rank;bestDistance=distance;
        }
        if(best!=split){lines[row]=pair.substr(0,best);lines[row+1]=pair.substr(best);}
        offset+=best;
    }
}

Raster resizedWidth(const Raster& image,int width) {
    if(image.w<=0 || image.h<=0 || width<=0) return {};
    if(image.w<=width) return image;
    Raster out{width,image.h,std::vector<std::uint32_t>(static_cast<std::size_t>(width)*image.h)};
    // Area averaging in premultiplied space preserves the thin black outline
    // while producing a straight-alpha result for the destination texture.
    for(int y=0;y<out.h;++y) for(int x=0;x<out.w;++x) {
        double from=double(x)*image.w/out.w, to=double(x+1)*image.w/out.w;
        double a=0,r=0,g=0,b=0,weight=0;
        for(int sx=static_cast<int>(from);sx<std::min(image.w,static_cast<int>(std::ceil(to)));++sx) {
            double w=std::max(0.0,std::min(to,double(sx+1))-std::max(from,double(sx)));
            auto p=image.pixels[static_cast<std::size_t>(y)*image.w+sx];
            double pa=(p>>24)/255.0;
            a+=pa*w; r+=((p>>16)&255)*pa*w;
            g+=((p>>8)&255)*pa*w; b+=(p&255)*pa*w; weight+=w;
        }
        if(a>0 && weight>0) out.pixels[static_cast<std::size_t>(y)*out.w+x]=
            (std::uint32_t(std::clamp(std::lround(255*a/weight),0L,255L))<<24)|
            (std::uint32_t(std::clamp(std::lround(r/a),0L,255L))<<16)|
            (std::uint32_t(std::clamp(std::lround(g/a),0L,255L))<<8)|
            std::uint32_t(std::clamp(std::lround(b/a),0L,255L));
    }
    return out;
}

void over(Raster& canvas,const Raster& ink,int left,int top) {
    for(int y=std::max(0,-top);y<ink.h && y+top<canvas.h;++y)
        for(int x=std::max(0,-left);x<ink.w && x+left<canvas.w;++x) {
            auto src=ink.pixels[static_cast<std::size_t>(y)*ink.w+x];
            unsigned sa=src>>24;
            if(!sa) continue;
            auto& dst=canvas.pixels[static_cast<std::size_t>(y+top)*canvas.w+x+left];
            unsigned da=dst>>24;
            unsigned oa=sa+(da*(255-sa)+127)/255;
            if(!oa){dst=0;continue;}
            auto channel=[&](int shift) {
                unsigned sc=(src>>shift)&255,dc=(dst>>shift)&255;
                return std::min(255u,(sc*sa+((dc*da*(255-sa)+127)/255)+oa/2)/oa);
            };
            dst=(oa<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
        }
}
std::vector<std::wstring> uniformLines(const std::wstring& text,int width,
                                       int px,const wchar_t* face,int weight=FW_NORMAL) {
    Measurer m(px,face,weight);
    std::wstring clean=oneLine(text);
    std::vector<std::wstring> lines;
    for(std::size_t pos=0;pos<clean.size();) {
        auto end=prefix(clean,pos,width,m);
        if(end==pos) end=step(clean,pos);
        lines.push_back(clean.substr(pos,end-pos));
        pos=end;
    }
    return lines;
}
} // namespace

const wchar_t* uiFace(){
    static const auto face=installedFamily({
        L"SimSun",L"宋体",L"Noto Serif CJK SC",L"Source Han Serif SC",L"Noto Serif SC",
        L"Microsoft YaHei UI",L"Microsoft YaHei",L"Noto Sans SC",L"Microsoft JhengHei UI"},L"Segoe UI");
    return face;
}
void loadJapaneseUiFace(const std::wstring& ini){
    wchar_t family[LF_FACESIZE+1]{};
    GetPrivateProfileStringW(L"study",L"ui_japanese_face",L"",family,LF_FACESIZE+1,ini.c_str());
    japaneseFaceOverride=installedFamily({family},L"");
}
const wchar_t* japaneseUiFace(){
    if(!japaneseFaceOverride.empty())return japaneseFaceOverride.c_str();
    static const auto face=installedFamily({
        L"BIZ UDPMincho Medium",L"BIZ UDPMincho",L"Meiryo",L"Yu Gothic UI",L"Noto Sans JP"},uiFace());
    return face;
}

static Raster rasterTextWeighted(const std::wstring& text,int pixelHeight,
                                  std::uint32_t argb,const wchar_t* fontFace,
                                  int weight,int outlineRadius=1,int shadowOffset=0,
                                  bool caption=false,bool measureHeightOnly=false,
                                  int availableWidth=0,int* baseline=nullptr,std::vector<float>* edges=nullptr) {
    if(text.empty()) return {};
    Dc dc;
    GdiObject glyphFont(font(pixelHeight,fontFace?fontFace:readerFace(),weight));
    if(!dc.value || !glyphFont.value) return {};
    Select selectedFont(dc.value,glyphFont.value);
    TEXTMETRICW metric{};
    if(!GetTextMetricsW(dc.value,&metric)) return {};
    const int lineHeight=std::max(1,int(metric.tmHeight));
    std::vector<std::wstring> lines;
    std::size_t pos=0;
    do {
        auto end=text.find(L'\n',pos);
        auto line=text.substr(pos,end==std::wstring::npos?text.size()-pos:end-pos);
        if(!line.empty() && line.back()==L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if(end==std::wstring::npos) break;
        pos=end+1;
    } while(pos<=text.size());
    int contentWidth=0;
    for(const auto& line:lines) {
        SIZE size{};
        if(!line.empty() && !GetTextExtentPoint32W(dc.value,line.c_str(),static_cast<int>(line.size()),&size)) return {};
        contentWidth=std::max(contentWidth,int(size.cx));
    }
    // Add a little tracking only between neighboring Han characters in a
    // rendered caption. Keep numbers, punctuation, and Latin runs together.
    std::vector<std::size_t> breaks;
    std::vector<int> advances;
    int tracking=0;
    if(availableWidth>contentWidth && lines.size()==1) {
        const auto& line=lines.front();
        auto han=[](wchar_t c){return (c>=0x3400 && c<=0x4dbf) ||
                                      (c>=0x4e00 && c<=0x9fff) || (c>=0xf900 && c<=0xfaff);};
        for(std::size_t i=1;i<line.size();++i)
            if(han(line[i-1]) && han(line[i])) breaks.push_back(i);
        if(!breaks.empty()) {
            int room=availableWidth-contentWidth-2*outlineRadius-shadowOffset;
            tracking=std::min(std::clamp((pixelHeight+8)/16,1,3),room/int(breaks.size()));
            if(tracking>0) {
                advances.resize(line.size());
                SIZE size{};
                if(GetTextExtentExPointW(dc.value,line.data(),int(line.size()),availableWidth,nullptr,advances.data(),&size))
                    contentWidth+=int(breaks.size())*tracking;
                else tracking=0;
            }
        }
    }
    // Limit malformed/accidental input, rather than wrapping an allocation size.
    if(contentWidth>65528 || lines.size()>1024) return {};
    outlineRadius=std::clamp(outlineRadius,0,2);shadowOffset=std::clamp(shadowOffset,0,1);
    const int padding=outlineRadius+shadowOffset+2;
    int width=std::max(1,contentWidth)+padding*2;
    int height=lineHeight*static_cast<int>(lines.size())+padding*2;
    if(height>16384 || std::size_t(width)*height>64*1024*1024) return {};
    BITMAPINFO bi{};
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=width; bi.bmiHeader.biHeight=-height;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    GdiObject bitmap(CreateDIBSection(dc.value,&bi,DIB_RGB_COLORS,&bits,nullptr,0));
    if(!bitmap.value || !bits) return {};
    Select selectedBitmap(dc.value,bitmap.value);
    std::memset(bits,0,std::size_t(width)*height*4);
    SetBkMode(dc.value,TRANSPARENT); SetTextColor(dc.value,reader_palette::GlyphMask);
    if(tracking>0) {
        const auto& line=lines.front();
        breaks.push_back(line.size());
        std::size_t start=0;
        int x=padding;
        for(std::size_t i=0;i<breaks.size();++i) {
            auto end=breaks[i];
            TextOutW(dc.value,x,padding,line.data()+start,int(end-start));
            x=padding+advances[end-1]+int(i+1)*tracking;
            start=end;
        }
    } else {
        for(std::size_t i=0;i<lines.size();++i)
            if(!lines[i].empty()) TextOutW(dc.value,padding,padding+static_cast<int>(i)*lineHeight,
                                          lines[i].c_str(),static_cast<int>(lines[i].size()));
    }
    GdiFlush();
    const auto* raw=static_cast<const std::uint32_t*>(bits);
    std::vector<std::uint8_t> alpha(std::size_t(width)*height);
    int top=height,bottom=0,left=width,right=0;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        auto p=raw[std::size_t(y)*width+x];
        unsigned a=std::max({p&255,(p>>8)&255,(p>>16)&255});
        alpha[std::size_t(y)*width+x]=static_cast<std::uint8_t>(a);
        if(a){top=std::min(top,y);bottom=std::max(bottom,y+1);left=std::min(left,x);right=std::max(right,x+1);}
    }
    if(top>=bottom) return {width,std::max(1,pixelHeight),std::vector<std::uint32_t>(std::size_t(width)*std::max(1,pixelHeight))};
    top=std::max(0,top-outlineRadius);bottom=std::min(height,bottom+outlineRadius+shadowOffset);
    if(baseline)*baseline=padding+metric.tmAscent-top;
    if(measureHeightOnly)return {0,bottom-top,{}};
    // Caption bounds include every outline/shadow pixel, without empty leading
    // indentation. The native installer already trims those transparent edges.
    if(caption){left=std::max(0,left-outlineRadius);right=std::min(width,right+outlineRadius+shadowOffset);}
    else{left=0;right=width;}
    Raster out{right-left,bottom-top,std::vector<std::uint32_t>(std::size_t(right-left)*(bottom-top))};
    if(edges&&lines.size()==1){
        auto& line=lines.front();
        advances.resize(line.size());SIZE total{};
        if(!line.empty()&&GetTextExtentExPointW(dc.value,line.data(),int(line.size()),std::numeric_limits<int>::max(),nullptr,advances.data(),&total)){
            edges->assign(line.size()+1,0);int added=0;
            for(size_t i=1;i<line.size();++i){
                if(tracking>0&&std::binary_search(breaks.begin(),breaks.end(),i))added+=tracking;
                (*edges)[i]=std::clamp(float(padding+advances[i-1]+added-left)/out.w,0.f,1.f);
            }
            edges->back()=1;
        }
    }
    unsigned colorAlpha=argb>>24, cr=(argb>>16)&255,cg=(argb>>8)&255,cb=argb&255;
    for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
        unsigned fill=(unsigned(alpha[std::size_t(y)*width+x])*colorAlpha+127)/255;
        if(!outlineRadius){
            if(fill)out.pixels[std::size_t(y-top)*out.w+x-left]=(fill<<24)|(argb&0x00ffffff);
            continue;
        }
        unsigned outline=0;
        for(int dy=-outlineRadius;dy<=outlineRadius;++dy) for(int dx=-outlineRadius;dx<=outlineRadius;++dx) {
            int xx=x+dx, yy=y+dy;
            if(xx>=0 && xx<width && yy>=0 && yy<height)
                outline=std::max(outline,unsigned(alpha[std::size_t(yy)*width+xx]));
        }
        outline=outline*(caption?250:210)/255;
        unsigned shadow=0;
        if(shadowOffset){
            for(int dy=-outlineRadius;dy<=outlineRadius;++dy)for(int dx=-outlineRadius;dx<=outlineRadius;++dx){
                int xx=x-shadowOffset+dx,yy=y-shadowOffset+dy;
                if(xx>=0&&xx<width&&yy>=0&&yy<height)shadow=std::max(shadow,unsigned(alpha[std::size_t(yy)*width+xx]));
            }
            shadow=shadow*130/255;
        }
        unsigned dark=outline+(shadow*(255-outline)+127)/255;
        unsigned combined=fill+(dark*(255-fill)+127)/255;
        if(combined) out.pixels[std::size_t(y-top)*out.w+x-left]=(combined<<24)|
            (((cr*fill+combined/2)/combined)<<16)|
            (((cg*fill+combined/2)/combined)<<8)|((cb*fill+combined/2)/combined);
    }
    return out;
}

Raster rasterText(const std::wstring& text,int pixelHeight,std::uint32_t argb,
                  const wchar_t* fontFace) {
    return rasterTextWeighted(text,pixelHeight,argb,fontFace?fontFace:uiFace(),FW_NORMAL,0);
}

bool clauseAt(const ClauseAnchors& anchors,size_t offset,ClauseRange& range){
    if(anchors.size()<3||offset<anchors.front().first||offset>=anchors.back().first)return false;
    auto end=std::upper_bound(anchors.begin(),anchors.end(),offset,[](size_t pos,const auto& a){return pos<a.first;});
    auto begin=end-1;
    range={begin->first,end->first,begin->second,end->second};return true;
}

ChineseLayout planChinese(const std::wstring& zh,const std::vector<Box>& bodyLines,
                           const std::vector<std::wstring>& japaneseLines,int targetRubyHeight) {
    ChineseLayout plan;
    std::wstring text=oneLine(zh);
    if(text.empty()) return plan;
    struct InputRow{Box box;std::wstring ja;int source;};
    std::vector<InputRow> inputs;
    bool hasJapanese=japaneseLines.size()==bodyLines.size() && !japaneseLines.empty();
    for(std::size_t i=0;i<bodyLines.size();++i){auto b=bodyLines[i];if(b.r>b.l && b.b>b.t)inputs.push_back({b,hasJapanese?japaneseLines[i]:L"",int(i)});}
    if(inputs.empty())return plan;
    std::vector<int> sourceHeights;for(const auto& row:inputs)sourceHeights.push_back(row.box.b-row.box.t);
    std::sort(sourceHeights.begin(),sourceHeights.end());
    plan.targetRubyHeight=targetRubyHeight>0?std::clamp(targetRubyHeight,6,128):
        std::clamp(int(std::lround(sourceHeights[sourceHeights.size()/2]*0.45)),12,32);
    // Two native pixels at the game's normal 25px ruby height. At a smaller
    // coordinate scale the border follows that scale instead of eating glyphs.
    plan.outlinePixels=plan.targetRubyHeight>=22?2:1;
    plan.shadowOffset=plan.targetRubyHeight>=22?1:0;
    int opticalPx=std::min(32,plan.targetRubyHeight);
    while(opticalPx>6){
        auto sample=rasterTextWeighted(text,opticalPx,reader_palette::argb::Caption,readerFace(),FW_MEDIUM,
                                      plan.outlinePixels,plan.shadowOffset,true,true);
        if(sample.h>0&&sample.h<=plan.targetRubyHeight)break;
        --opticalPx;
    }
    const int captionMargin=2*plan.outlinePixels+plan.shadowOffset;
    std::stable_sort(inputs.begin(),inputs.end(),[](const auto& a,const auto& b){return a.box.t==b.box.t?a.box.l<b.box.l:a.box.t<b.box.t;});
    if(hasJapanese && inputs.size()>1){
        bool hasBody=std::any_of(inputs.begin(),inputs.end(),[](const auto& row){return !punctuationOnly(row.ja);});
        if(hasBody){
            std::vector<InputRow> active;std::wstring openingMarks;
            for(auto row:inputs){
                if(punctuationOnly(row.ja)){
                    if(active.empty())openingMarks+=row.ja;
                    else active.back().ja+=row.ja;
                }else{row.ja=openingMarks+row.ja;openingMarks.clear();active.push_back(std::move(row));}
            }
            inputs=std::move(active);
        }
    }
    std::vector<Box> boxes;std::vector<std::wstring> jaRows;
    for(const auto& row:inputs){boxes.push_back(row.box);jaRows.push_back(row.ja);}
    ClauseMap clauses;
    if(hasJapanese){
        clauses=clauseMap(text,jaRows);
        if(clauses.reviewed && clauses.mergeTailFrom!=std::numeric_limits<std::size_t>::max() && boxes.size()>1){
            std::size_t lastStart=0;for(std::size_t i=0;i+1<jaRows.size();++i)lastStart+=jaRows[i].size();
            if(lastStart>=clauses.mergeTailFrom){
                auto candidateJa=jaRows;candidateJa[candidateJa.size()-2]+=candidateJa.back();candidateJa.pop_back();
                auto candidateBoxes=boxes;candidateBoxes.pop_back();auto candidateMap=clauseMap(text,candidateJa);
                std::vector<int> hs,ws;for(auto b:boxes)hs.push_back(b.b-b.t);std::sort(hs.begin(),hs.end());
                int defaultPx=opticalPx;
                for(auto b:candidateBoxes)ws.push_back(std::max(1,b.r-b.l-captionMargin));
                Measurer measure(defaultPx,readerFace(),FW_MEDIUM);bool fits=false;
                if(candidateBoxes.size()==1)fits=measure.width(text)+captionMargin<=candidateBoxes.front().r-candidateBoxes.front().l;
                else balancedWrap(text,ws,measure,fits,&candidateMap);
                if(fits){boxes=std::move(candidateBoxes);jaRows=std::move(candidateJa);clauses=std::move(candidateMap);inputs.pop_back();}
                else plan.needsManualReview=true;
            }
        }
        if(clauses.groupWholeRecord && boxes.size()>1){
            std::vector<int> hs;for(auto b:boxes)hs.push_back(b.b-b.t);std::sort(hs.begin(),hs.end());
            int defaultPx=opticalPx;
            Measurer measure(defaultPx,readerFace(),FW_MEDIUM);
            if(measure.width(text)+captionMargin<=boxes.front().r-boxes.front().l){
                std::wstring whole;for(const auto& row:jaRows)whole+=row;
                boxes.resize(1);inputs.resize(1);jaRows={whole};
                clauses=clauseMap(text,jaRows);plan.groupedRecord=true;
            }else plan.needsManualReview=true;
        }
        // Native japaneseLines are consecutive clean.substr slices from offset zero, including
        // indent and quotes. oneLine replaces one UTF-16 unit with one unit. Only use the
        // planner's offsets if its visual row order still reconstructs that same source.
        std::wstring ordered,clean;
        for(const auto& row:jaRows)ordered+=oneLine(row);
        for(const auto& row:japaneseLines)clean+=oneLine(row);
        if(ordered==clean&&clauses.anchors.size()>2){
            if(clauses.reviewed||!clauses.ambiguous)plan.highlightAnchors=clauses.anchors;
            else{
                auto jaStart=clean.find_first_not_of(L" 　"),zhStart=text.find_first_not_of(L" 　");
                ClauseMap review;
                if(jaStart!=clean.npos&&zhStart!=text.npos&&reviewedClauses(clean.substr(jaStart),text.substr(zhStart),review,true)){
                    for(auto p:review.anchors)plan.highlightAnchors.emplace_back(p.first+jaStart,p.second+zhStart);
                    plan.highlightAnchors.front()={0,0};
                }
            }
        }
        plan.handReviewed=clauses.reviewed;
        plan.matchedInternalAnchors=clauses.anchors.size()>2?unsigned(clauses.anchors.size()-2):0;
        plan.punctuationAnchored=plan.matchedInternalAnchors>0;
        plan.needsManualReview=plan.needsManualReview || (boxes.size()>1 && clauses.ambiguous);
        plan.anchorConfidence=clauses.reviewed?0.95:(clauses.ambiguous?0.35:(plan.punctuationAnchored?0.75:0.55));
    } else if(!japaneseLines.empty())plan.needsManualReview=true;
    std::vector<int> heights,widths;
    for(auto b:boxes){heights.push_back(b.b-b.t);widths.push_back(std::max(1,b.r-b.l-captionMargin));}
    std::sort(heights.begin(),heights.end());
    int initialPx=opticalPx,px=initialPx;
    std::vector<std::wstring> lines;
    bool fits=false;
    if(hasJapanese && boxes.size()>1 && clauses.anchors.size()>=2){
        // Do not make a normal-size translation illegible solely to satisfy
        // an uncertain automatic punctuation map. Relax and report it instead.
        for(px=initialPx;px>=std::min(initialPx,12);--px){
            Measurer m(px,readerFace(),FW_MEDIUM);
            lines=balancedWrap(text,widths,m,fits,&clauses);
            if(fits)break;
        }
        if(!fits){plan.relaxedAnchors=true;plan.needsManualReview=true;plan.anchorConfidence=std::min(0.25,plan.anchorConfidence);}
    }
    if(!fits){
        for(px=initialPx;px>=6;--px){
            Measurer m(px,readerFace(),FW_MEDIUM);
            lines=balancedWrap(text,widths,m,fits);
            if(!fits)lines=wrap(text,widths,m,fits);
            if(fits)break;
        }
    }
    if(!fits) {
        // Guarantee that every code unit appears even when the supplied boxes
        // are implausibly narrow. Proportionally split, then compress the final
        // rasters; this case is deliberately visible as reduced readability.
        px=6; lines.clear();plan.horizontalCompression=true;plan.needsManualReview=true;
        std::size_t pos=0;
        int remainingWidth=0; for(int w:widths)remainingWidth+=w;
        for(std::size_t i=0;i<boxes.size() && pos<text.size();++i) {
            auto remaining=text.size()-pos;
            std::size_t count=i+1==boxes.size()?remaining:
                std::max<std::size_t>(1,(remaining*widths[i])/std::max(1,remainingWidth));
            auto end=std::min(text.size(),pos+count);
            if(end<text.size() && text[end-1]>=0xd800 && text[end-1]<=0xdbff) ++end;
            lines.push_back(text.substr(pos,end-pos));
            pos=end; remainingWidth-=widths[i];
        }
    }
    plan.pixelHeight=px;
    Measurer finalMeasure(px,readerFace(),FW_MEDIUM);
    if(fits)polishCaptionBreaks(lines,widths,finalMeasure,
        plan.handReviewed||(hasJapanese&&!plan.relaxedAnchors)?&clauses:nullptr);
    for(std::size_t i=0;i<lines.size();++i) {
        if(plan.handReviewed&&i>0&&(leadingParticle(lines[i],0)||opening(lines[i-1].back())))plan.needsManualReview=true;
        plan.rows.push_back({boxes[i],lines[i],inputs[i].source});
        if(finalMeasure.width(lines[i])+captionMargin>boxes[i].r-boxes[i].l)plan.horizontalCompression=true;
    }
    if(px<12)plan.needsManualReview=true;
    return plan;
}

// Windows ships ICU's Chinese dictionary break iterator. Keep closing marks
// with their word, and an opening quote with the following word.
static std::vector<size_t> chineseWordEnds(const std::wstring& text) {
    static HMODULE icu=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!icu)return {};
    using Open=void*(*)(int,const char*,const wchar_t*,int,int*);
    using Next=int(*)(void*);
    using Close=void(*)(void*);
    static auto open=reinterpret_cast<Open>(GetProcAddress(icu,"ubrk_open"));
    static auto next=reinterpret_cast<Next>(GetProcAddress(icu,"ubrk_next"));
    static auto close=reinterpret_cast<Close>(GetProcAddress(icu,"ubrk_close"));
    if(!open || !next || !close)return {};
    int error=0;
    void* iterator=open(1,"zh",text.data(),int(text.size()),&error);
    if(!iterator)return {};
    std::vector<size_t> ends;
    if(error<=0)for(int end=next(iterator);end>0;end=next(iterator)){
        if(size_t(end)==text.size() || (!closing(text[end]) && !opening(text[end-1])))
            ends.push_back(size_t(end));
    }
    close(iterator);
    return ends;
}

static std::vector<Box> freeRubyIntervals(Box row,std::vector<Box> occupied,int gap) {
    std::sort(occupied.begin(),occupied.end(),[](Box a,Box b){return a.l<b.l;});
    std::vector<Box> free;
    int left=row.l;
    for(auto box:occupied){
        int stop=std::min(row.r,box.l-gap);
        if(stop>left)free.push_back({left,row.t,stop,row.b});
        left=std::max(left,box.r+gap);
    }
    if(left<row.r)free.push_back({left,row.t,row.r,row.b});
    return free;
}

std::vector<ProjectedCaption> projectChinese(const std::wstring& text,
    const std::vector<Box>& rows,const std::vector<std::wstring>& japanese,
    const std::vector<Box>& obstacles,int bodyHeight,bool speechBubble) {
    if(rows.empty()||rows.size()!=japanese.size()||bodyHeight<8)return {};
    const int target=std::clamp((bodyHeight*(speechBubble?54:44)+50)/100,12,32);
    const int gap=std::max(2,(bodyHeight+7)/14);
    auto plan=planChinese(text,rows,japanese,target);
    auto anchors=plan.highlightAnchors.empty()?nullptr:
        std::make_shared<const ClauseAnchors>(std::move(plan.highlightAnchors));
    const int minimum=std::max(11,(target*2+2)/3);
    for(int px=target;px>=minimum;--px){
        std::vector<ProjectedCaption> out;
        size_t offset=0;
        bool complete=true;
        for(const auto& row:plan.rows){
            auto first=row.text.find_first_not_of(L" 　\t");
            size_t start=offset;offset+=row.text.size();
            if(first==std::wstring::npos)continue;
            const auto content=row.text.substr(first);
            auto paint=[&](const std::wstring& part,CaptionSlice& slice){
                return rasterTextWeighted(part,px,speechBubble?reader_palette::argb::BubbleCaption:reader_palette::argb::Caption,
                    readerFace(),FW_MEDIUM,speechBubble?0:1,0,true,false,0,nullptr,anchors?&slice.edges:nullptr);
            };
            CaptionSlice slice{anchors,start+first,{}};
            auto ink=paint(content,slice);
            const auto box=row.body;
            const int bottom=box.t-gap,top=bottom-ink.h;
            // The first row may project a few pixels above the text texture.
            // The comic's top margin belongs to the balloon on screen.
            if(!ink.w||!ink.h||top < (speechBubble?-gap*3:0)){complete=false;break;}
            std::vector<Box> blocked;
            for(auto other:obstacles)
                if(other.t < bottom+1 && other.b > top-1)blocked.push_back(other);
            auto spans=freeRubyIntervals(box,blocked,gap);
            auto room=std::find_if(spans.begin(),spans.end(),[&](Box b){return b.r-b.l>=ink.w;});
            auto place=[&](Box space,Raster image,CaptionSlice part,bool initial){
                const int inset=initial&&space.l==box.l?std::min(px/2,space.r-space.l-image.w):0;
                out.push_back({{space.l+inset,bottom-image.h,std::move(image),std::move(part)},row.japaneseLine});
            };
            if(room!=spans.end()){place(*room,std::move(ink),std::move(slice),true);continue;}
            // An original reading may divide the available strip. Keep all
            // fragments on this physical row, in reading order, at word breaks.
            auto ends=chineseWordEnds(content);
            size_t from=0,next=0;
            Measurer measure(px,readerFace(),FW_MEDIUM);
            for(auto span:spans){
                const int width=span.r-span.l;
                size_t stop=next;
                while(stop<ends.size()&&measure.width(content.substr(from,ends[stop]-from))+
                      (speechBubble?0:2)<=width)++stop;
                if(stop==next)continue;
                const auto end=ends[stop-1];
                CaptionSlice part{anchors,start+first+from,{}};
                auto image=paint(content.substr(from,end-from),part);
                if(!image.w||image.w>width)continue;
                place(span,std::move(image),std::move(part),from==0);
                next=stop;from=end;
            }
            if(from!=content.size()){complete=false;break;}
        }
        if(complete&&!out.empty()&&offset==oneLine(text).size())return out;
    }
    return {};
}

std::vector<PlacedRaster> makeChinese(const std::wstring& zh,
                                      const std::vector<Box>& bodyLines,
                                      const std::vector<std::wstring>& japaneseLines,
                                      int targetRubyHeight,
                                      const std::vector<std::vector<Box>>& originalRuby) {
    auto plan=planChinese(zh,bodyLines,japaneseLines,targetRubyHeight);
    std::vector<PlacedRaster> out;
    auto anchors=plan.highlightAnchors.empty()?nullptr:std::make_shared<const ClauseAnchors>(std::move(plan.highlightAnchors));
    size_t zhEnd=0;
    for(const auto& row:plan.rows) {
        size_t zhStart=zhEnd;zhEnd+=row.text.size();
        int span=row.body.r-row.body.l;
        // A small optical inset belongs to the line start alone. Spend existing
        // whitespace; never compress the translation or push a word past ruby.
        const int inset=std::max(1,(plan.pixelHeight+1)/2);
        if(size_t(row.japaneseLine)<originalRuby.size() && !originalRuby[row.japaneseLine].empty()){
            auto text=row.text;
            size_t first=text.find_first_not_of(L" 　\t");
            if(first==text.npos)continue;
            text.erase(0,first);
            auto ends=chineseWordEnds(text);
            if(ends.empty() || ends.back()!=text.size())return {};
            auto intervals=freeRubyIntervals(row.body,originalRuby[row.japaneseLine],(plan.pixelHeight+1)/2);
            Measurer measure(plan.pixelHeight,readerFace(),FW_MEDIUM);
            size_t next=0,start=0;
            for(auto box:intervals){
                const int width=box.r-box.l;
                size_t firstWord=next;
                CaptionSlice slice{anchors,zhStart+first+start,{}};
                auto paint=[&](size_t end){
                    return rasterTextWeighted(text.substr(start,end-start),plan.pixelHeight,reader_palette::argb::Caption,
                        readerFace(),FW_MEDIUM,plan.outlinePixels,plan.shadowOffset,true,false,width,nullptr,anchors?&slice.edges:nullptr);
                };
                while(next<ends.size() && measure.width(text.substr(start,ends[next]-start))+
                      2*plan.outlinePixels+plan.shadowOffset<=width)++next;
                // A full-width comma has a long transparent right bearing. If
                // its visible outline fits, keep the clause together before ruby.
                Raster image;
                if(next<ends.size() && closing(text[ends[next]-1]) &&
                   measure.width(text.substr(start,ends[next]-start))<=width+plan.pixelHeight*2){
                    auto punctuated=paint(ends[next]);
                    if(punctuated.w>0 && punctuated.w<=width){image=std::move(punctuated);++next;}
                }
                if(next==firstWord)continue;
                size_t end=ends[next-1];
                if(!image.w)image=paint(end);
                if(!image.w || image.w>width)return {};
                // Closing punctuation occupies only the leading part of its last
                // character cell. Keep a third of an em of clear ink before ruby.
                const int punctuationRoom=box.r<row.body.r && closing(text[end-1])
                    ?std::max(0,(plan.pixelHeight+1)/2-std::max(5,plan.pixelHeight/3)):0;
                const int shift=start==0&&box.l==row.body.l?std::min(inset,width-image.w+punctuationRoom):0;
                out.push_back({box.l+shift,std::max(0,box.t-image.h-2),std::move(image),std::move(slice)});
                start=end;
                if(end==text.size())break;
            }
            if(start!=text.size())return {};
            continue;
        }
        CaptionSlice slice{anchors,zhStart,{}};
        auto image=rasterTextWeighted(row.text,plan.pixelHeight,reader_palette::argb::Caption,readerFace(),FW_MEDIUM,
                                      plan.outlinePixels,plan.shadowOffset,true,false,span,nullptr,anchors?&slice.edges:nullptr);
        if(image.w>span) image=resizedWidth(image,span);
        if(image.w<=0 || image.h<=0) continue;
        int x=row.body.l+std::min(inset,span-image.w);
        int y=row.body.t-image.h-2;
        // The first ruby row may begin exactly at texture y=0. Do not crop the
        // Chinese top: if there is insufficient original ruby space, overlap is
        // unavoidable and must be diagnosed from the caller's real bounds.
        y=std::max(0,y);
        out.push_back({x,y,std::move(image),std::move(slice)});
    }
    return out;
}

std::vector<PlacedRaster> makeChinese(const std::wstring& zh,
                                      const std::vector<Box>& bodyLines,
                                      const std::vector<std::wstring>& japaneseLines) {
    return makeChinese(zh,bodyLines,japaneseLines,0);
}

std::vector<PlacedRaster> makeChinese(const std::wstring& zh,
                                      const std::vector<Box>& bodyLines) {
    return makeChinese(zh,bodyLines,{});
}

static Raster roundedControl(int width,int height,double radius,double stroke,
                             std::uint32_t fill,std::uint32_t edge){
    radius=std::min({radius,width*.5,height*.5});
    Raster image{width,height,std::vector<std::uint32_t>(std::size_t(width)*height)};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        const double dx=x+.5-std::clamp(x+.5,radius,width-radius);
        const double dy=y+.5-std::clamp(y+.5,radius,height-radius);
        const double distance=std::hypot(dx,dy);
        const double outer=std::clamp(radius+.5-distance,0.0,1.0);
        const double inner=std::min(outer,std::clamp(.5+std::min({x+.5,y+.5,width-x-.5,height-y-.5,radius-distance})-stroke,0.0,1.0));
        const double f=(fill>>24)*inner,e=(edge>>24)*(outer-inner),alpha=f+e;
        if(alpha<.5)continue;
        auto channel=[&](int shift){return std::uint32_t(std::lround((((fill>>shift)&255)*f+((edge>>shift)&255)*e)/alpha));};
        image.pixels[std::size_t(y)*width+x]=(std::uint32_t(std::lround(alpha))<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
    }
    return image;
}

Raster makeControlLabel(const std::wstring& text,int textPx,int radius,int scale){
    const double zoom=std::clamp(scale,50,250)/100.0;
    auto px=[zoom](int n){return std::max(1,int(std::lround(n*zoom)));};
    auto label=rasterText(text,px(textPx),reader_palette::argb::Body);
    if(label.w<1||label.h<1)return {};
    auto image=roundedControl(label.w+2*px(6),std::max(label.h+2*px(4),px(22)),
        radius*zoom,zoom,reader_palette::argb::Card,reader_palette::argb::Line);
    over(image,label,(image.w-label.w)/2,(image.h-label.h)/2);
    return image;
}

Raster makeBookmark(bool collected,bool hovered,int scale,bool annotated){
    const double zoom=std::clamp(scale,50,250)/100.0;
    const int size=int(std::lround(26*zoom));
    Raster image=hovered?roundedControl(size,size,7*zoom,zoom,
        reader_palette::argb::fromColorRef(reader_palette::Card,224),
        reader_palette::argb::fromColorRef(reader_palette::Accent,153)):
        Raster{size,size,std::vector<std::uint32_t>(std::size_t(size)*size)};
    Raster ribbon{size,size,std::vector<std::uint32_t>(std::size_t(size)*size)};
    const unsigned alpha=collected||hovered?255:158;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        unsigned coverage=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
            const double signedX=(x+(sx+.5)/4-size*.5)/zoom;
            const double dx=std::abs(signedX);
            const double dy=(y+(sy+.5)/4-size*.5)/zoom;
            const double bottom=4.5+dx*(2.0/3);
            bool dot=collected&&annotated&&std::hypot(signedX-7,dy+8.5)<=1.5;
            if(dot||(dx<=4.5&&dy>=-7.5&&dy<=bottom&&
               (collected||dx>=3.25||dy<=-6.25||bottom-dy<=1.5)))++coverage;
        }
        if(coverage)ribbon.pixels[std::size_t(y)*size+x]=
            reader_palette::argb::fromColorRef(reader_palette::Accent,(alpha*coverage+8)/16);
    }
    over(image,ribbon,0,0);
    return image;
}

static Raster tooltipSurface(int width,int height){
    Raster card{width,height,std::vector<std::uint32_t>(std::size_t(width)*height,reader_palette::argb::Card)};
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        if(x==0 || y==0 || x==width-1 || y==height-1)card.pixels[std::size_t(y)*width+x]=reader_palette::argb::CardEdge;
    return card;
}
static void headwordBar(Raster& card,int width,int top,int bottom){
    for(int y=std::max(1,top);y<std::min(card.h-1,bottom);++y)
        for(int x=1;x<=std::min(width,card.w-2);++x)
            card.pixels[std::size_t(y)*card.w+x]=reader_palette::argb::CardAccentBar;
}

void roundCardCorners(Raster& card,int scale){
    const int radius=std::min({std::max(3,(10*scale+50)/100),card.w/2,card.h/2});
    constexpr std::uint32_t edge=reader_palette::argb::CardEdge;
    for(int y=0;y<radius;++y)for(int x=0;x<radius;++x){
        double distance=std::hypot(radius-x-0.5,radius-y-0.5);
        double outer=std::clamp(radius+0.5-distance,0.0,1.0);
        double inner=std::clamp(radius-0.5-distance,0.0,1.0);
        if(inner==1.0)continue;
        for(int row:{y,card.h-1-y})for(int col:{x,card.w-1-x}){
            auto& pixel=card.pixels[std::size_t(row)*card.w+col];
            double fill=(pixel>>24)*inner,stroke=(edge>>24)*(outer-inner),alpha=fill+stroke;
            if(alpha<0.5){pixel=0;continue;}
            auto channel=[&](int shift){return std::uint32_t(std::lround((((pixel>>shift)&255)*fill+((edge>>shift)&255)*stroke)/alpha));};
            pixel=(std::uint32_t(std::lround(alpha))<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
        }
    }
}

Raster makeToast(const std::wstring& text,int scale){
    auto px=[scale](int n){return std::max(1,(n*scale+50)/100);};
    const auto split=text.find(L" · ");
    const auto main=rasterText(text.substr(0,split),px(15),reader_palette::argb::Body);
    const auto detail=split==text.npos?Raster{}:rasterText(L"· "+text.substr(split+3),px(13),reader_palette::argb::Dim);
    const int h=px(34),dotX=px(17),textX=px(29),detailX=textX+main.w+px(5);
    const int w=std::max(h,(detail.w?detailX+detail.w:textX+main.w)+px(17));
    const double radius=h*.5;
    Raster pill{w,h,std::vector<std::uint32_t>(std::size_t(w)*h)};
    constexpr auto fill=reader_palette::argb::ToastFill,edge=reader_palette::argb::ToastEdge;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const double dx=x+.5-std::clamp(x+.5,radius,w-radius),dy=y+.5-radius;
        const double distance=std::hypot(dx,dy);
        const double outer=std::clamp(radius+.5-distance,0.0,1.0);
        const double inner=std::clamp(radius+.5-scale/100.0-distance,0.0,1.0);
        const double f=(fill>>24)*inner,e=(edge>>24)*(outer-inner),alpha=f+e;
        if(alpha<.5)continue;
        auto channel=[&](int shift){return std::uint32_t(std::lround((((fill>>shift)&255)*f+((edge>>shift)&255)*e)/alpha));};
        pill.pixels[std::size_t(y)*w+x]=(std::uint32_t(std::lround(alpha))<<24)|(channel(16)<<16)|(channel(8)<<8)|channel(0);
    }
    const int diameter=px(6);
    Raster dot{diameter,diameter,std::vector<std::uint32_t>(std::size_t(diameter)*diameter)};
    for(int y=0;y<diameter;++y)for(int x=0;x<diameter;++x){
        const double r=diameter*.5;
        const double coverage=std::clamp(r+.5-std::hypot(x+.5-r,y+.5-r),0.0,1.0);
        dot.pixels[y*diameter+x]=reader_palette::argb::fromColorRef(reader_palette::Accent,std::uint32_t(std::lround(255*coverage)));
    }
    over(pill,dot,dotX-diameter/2,(h-diameter)/2);
    over(pill,main,textX,(h-main.h)/2);
    over(pill,detail,detailX,(h-detail.h)/2);
    return pill;
}

struct WordActionLabels{
    int margin,band,inset;
    Raster main,range,sentence;
    int width() const{return main.w+range.w+sentence.w+6*margin+2*inset;}
};
static WordActionLabels wordActionLabels(const std::wstring& primary,int scale){
    auto px=[scale](int n){return std::max(1,(n*scale+50)/100);};
    const int labelPx=std::max(15,px(18));
    WordActionLabels labels{px(12),std::max(28,px(37)),px(3),
        rasterText(primary,labelPx,reader_palette::argb::Accent),
        rasterText(L"调整词界",labelPx,reader_palette::argb::Muted),
        rasterText(L"收藏原句",labelPx,reader_palette::argb::Muted)};
    const int space=420*scale/100-6*labels.margin-2*labels.inset;
    const int ink=labels.main.w+labels.range.w+labels.sentence.w;
    if(ink>space){
        labels.main=resizedWidth(labels.main,space*labels.main.w/ink);
        labels.range=resizedWidth(labels.range,space*labels.range.w/ink);
        labels.sentence=resizedWidth(labels.sentence,space*labels.sentence.w/ink);
    }
    return labels;
}
Raster withWordActions(const Raster& body,const std::wstring& primary,int scale,Box& edit,Box& select,Box& save,bool compact){
    auto labels=wordActionLabels(primary,scale);
    if(!compact)labels.band+=(6*scale+50)/100;
    const auto& [margin,band,inset,main,range,sentence]=labels;
    const int rangeW=range.w+2*margin,saveW=sentence.w+2*margin;
    const int w=std::max(body.w,labels.width()),h=body.h+band;
    auto card=tooltipSurface(w,h);
    for(int y=1;y<body.h-1;++y)
        std::copy_n(body.pixels.begin()+std::size_t(y)*body.w+1,body.w-2,card.pixels.begin()+std::size_t(y)*w+1);
    const int split=w-inset-saveW-rangeW;
    for(int y=body.h;y<h-1;++y)for(int x=1;x<w-1;++x){
        card.pixels[std::size_t(y)*w+x]=(y==body.h&&x>=margin&&x<w-margin)?reader_palette::argb::CardActionDivider:reader_palette::argb::RaisedActionFill;
    }
    edit={inset,body.h+1,split,h-2};select={split,body.h+1,split+rangeW,h-2};save={select.r,body.h+1,w-inset,h-2};
    over(card,main,edit.l+margin,body.h+(band-main.h)/2);
    over(card,range,select.r-margin-range.w,body.h+(band-range.h)/2);
    over(card,sentence,save.r-margin-sentence.w,body.h+(band-sentence.h)/2);
    return card;
}

Raster makeReadingBubble(const std::wstring& term,const std::wstring& reading,int scale){
    scale=std::clamp(scale,50,250);
    auto px=[scale](int value){return std::max(1,(value*scale+50)/100);};
    const int pad=px(10),gap=px(7),maxWidth=px(420);
    int titleBaseline=0,kanaBaseline=0;
    auto title=rasterTextWeighted(term,px(22),reader_palette::argb::Body,japaneseUiFace(),
        FW_NORMAL,0,0,false,false,0,&titleBaseline);
    auto kana=rasterTextWeighted(reading,px(18),reader_palette::argb::Accent,japaneseUiFace(),
        FW_NORMAL,0,0,false,false,0,&kanaBaseline);
    const int inner=maxWidth-2*pad-gap;
    if(title.w+kana.w>inner){
        const int titleWidth=std::clamp(int(static_cast<long long>(inner)*title.w/(title.w+kana.w)),1,inner-1);
        title=resizedWidth(title,titleWidth);
        kana=resizedWidth(kana,inner-titleWidth);
    }
    const int baseline=std::max(titleBaseline,kanaBaseline);
    const int titleY=pad+baseline-titleBaseline,kanaY=pad+baseline-kanaBaseline;
    const int bottom=std::max(titleY+title.h,kanaY+kana.h);
    auto card=tooltipSurface(title.w+gap+kana.w+2*pad,bottom+pad);
    headwordBar(card,px(2),pad,bottom);
    over(card,title,pad,titleY);
    over(card,kana,pad+title.w+gap,kanaY);
    return card;
}

std::vector<std::wstring> splitMora(const std::wstring& reading){
    std::vector<std::wstring> mora;
    const std::wstring small=L"ゃゅょぁぃぅぇぉゎャュョァィゥェォヮ";
    for(wchar_t c:reading){
        if(!((c>=L'ぁ'&&c<=L'ゖ')||(c>=L'ァ'&&c<=L'ヺ')||c==L'ー'))return {};
        if(small.find(c)!=small.npos&&!mora.empty())mora.back()+=c;
        else mora.emplace_back(1,c);
    }
    return mora;
}
bool pitchPattern(const std::wstring& reading,const std::wstring& pitch,std::vector<bool>& high,bool& particleHigh){
    high.clear();particleHigh=false;
    if(pitch.find(L'+')!=pitch.npos||pitch.compare(0,4,L"辞书重音")==0)return false;
    int n=-1;
    for(wchar_t c:pitch){
        if(c==L'⓪'){n=0;break;}
        if(c>=L'①'&&c<=L'⑳'){n=int(c-L'①')+1;break;}
    }
    auto mora=splitMora(reading);
    if(mora.empty()||n<0||size_t(n)>mora.size())return false;
    particleHigh=n==0;
    for(size_t i=0;i<mora.size();++i)high.push_back(n==1?i==0:i>0&&(n==0||i<size_t(n)));
    return true;
}
// The kana raster has two transparent pixels before its first GDI cell.
static Raster pitchBand(const std::wstring& line,const std::vector<std::wstring>& mora,
                        const std::vector<bool>& high,bool particleHigh,int px,int scale,int limit){
    Measurer measure(px,japaneseUiFace());
    SIZE bounds{};std::vector<int> widths(line.size());
    if(line.empty()||!GetTextExtentExPointW(measure.dc.value,line.data(),int(line.size()),std::numeric_limits<int>::max(),nullptr,widths.data(),&bounds))return {};
    double radius=px*.12,stroke=1.5*scale/100.;
    int height=std::max(3,int(std::lround(px*.42)));
    double top=stroke/2+(particleHigh?radius:0),bottom=height-stroke/2-(particleHigh?0:radius);
    struct Segment{double x1,y1,x2,y2;};
    std::vector<Segment> path;
    size_t end=0;double x=2,previous=0,cell=0;
    bool full=false;double ringX=0,ringY=0;
    for(size_t i=0;i<mora.size()&&end<line.size();++i){
        bool wholeMora=end+mora[i].size()<=line.size();
        end=std::min(line.size(),end+mora[i].size());
        double next=2+widths[end-1],y=high[i]?top:bottom;
        if(i&&previous!=y)path.push_back({x,previous,x,y});
        path.push_back({x,y,next,y});cell=next-x;x=next;previous=y;
        full=wholeMora&&i+1==mora.size();
    }
    if(full){
        ringY=particleHigh?top:bottom;
        if(previous!=ringY)path.push_back({x,previous,x,ringY});
        ringX=x+std::max(2*radius,cell*.45);
        path.push_back({x,ringY,ringX-radius,ringY});
    }
    Raster image;image.w=std::min(limit,int(std::ceil((full?ringX+radius:x)+stroke+1)));
    image.h=height;image.pixels.resize(size_t(image.w)*image.h);
    for(int y=0;y<image.h;++y)for(int i=0;i<image.w;++i){
        double cx=i+.5,cy=y+.5;
        double distance=full?std::abs(std::hypot(cx-ringX,cy-ringY)-radius):1e6;
        for(auto& part:path){
            double dx=cx-std::clamp(cx,std::min(part.x1,part.x2),std::max(part.x1,part.x2));
            double dy=cy-std::clamp(cy,std::min(part.y1,part.y2),std::max(part.y1,part.y2));
            distance=std::min(distance,std::hypot(dx,dy));
        }
        auto alpha=unsigned(std::lround(204*std::clamp(.5+stroke/2-distance,0.,1.)));
        image.pixels[size_t(y)*image.w+i]=reader_palette::argb::fromColorRef(reader_palette::Accent,alpha);
    }
    return image;
}

Raster makeTooltip(const std::wstring& term,const std::wstring& reading,
                   const std::wstring& pos,const std::wstring& definition,
                   const std::wstring& level,const std::wstring& pitch,
                   int width,int scale,CardContent content,const std::wstring& primaryAction) {
    scale=std::clamp(scale,50,250); width=std::clamp(width,180,1600);
    auto sp=[scale](int n){return std::max(1,(n*scale+50)/100);};
    const int pad=sp(18), gap=sp(9), maxHeight=sp(300);
    const int inner=std::max(1,width-2*pad);
    const bool translation=content==CardContent::Translation;
    const auto titleFace=translation?uiFace():japaneseUiFace();
    const auto bodyFace=translation?readerFace():uiFace();
    struct Piece {int x,y;Raster image;};
    std::vector<Piece> pieces;
    int used=0,headingBottom=0,contentWidth=0;
    std::size_t badgePiece=0;
    const int detailPx=std::max(13,sp(14));
    int kanaPx=sp(translation?21:18);
    std::vector<bool> high;bool particleHigh=false;
    bool hasPitch=!translation&&pitchPattern(reading,pitch,high,particleHigh);
    // Leave room for the post-word particle without moving the kana relative to the curve.
    int kanaWidth=hasPitch?std::max(1,inner-kanaPx):inner;
    auto kanaLines=uniformLines(reading,kanaWidth,kanaPx,japaneseUiFace());
    Raster curve;
    if(hasPitch&&!kanaLines.empty())curve=pitchBand(kanaLines.front(),splitMora(reading),high,particleHigh,kanaPx,scale,inner);
    auto layout=[&](int definitionPx) {
        pieces.clear(); contentWidth=0; badgePiece=0; int y=translation?pad:pad+sp(3);
        const bool outlined=level==L"N1"||level==L"N2";
        auto badge=rasterText(level,sp(15),reader_palette::argb::Accent,outlined?L"Segoe UI":uiFace());
        if(outlined){
            auto pill=roundedControl(badge.w+2*(sp(4)+1),badge.h+2*(sp(3)+1),sp(4),scale/100.0,0,
                reader_palette::argb::fromColorRef(reader_palette::Accent,128));
            over(pill,badge,(pill.w-badge.w)/2,(pill.h-badge.h)/2);
            badge=std::move(pill);
        }
        const int badgeWidth=badge.w?badge.w+gap:0;
        int termPx=sp(translation?26:27);
        auto headings=uniformLines(term,std::max(1,inner-badgeWidth),termPx,titleFace);
        int firstY=y;
        for(const auto& line:headings){auto img=rasterText(line,termPx,reader_palette::argb::Body,titleFace); img=resizedWidth(img,std::max(1,inner-badgeWidth)); int h=img.h;pieces.push_back({pad,y,std::move(img)});y+=h+sp(3);}
        if(badge.w){
            const int x=width-pad-badge.w;
            const int badgeY=firstY+std::max(0,(headings.empty()?0:pieces.front().image.h-badge.h)/2);
            y=std::max(y,badgeY+badge.h);
            badgePiece=pieces.size()+1;
            pieces.push_back({x,badgeY,std::move(badge)});
        }
        if(!reading.empty()) {
            y+=sp(translation?2:6);
            if(curve.h){pieces.push_back({pad,y,curve});y+=curve.h;}
            for(const auto& line:kanaLines){
                auto img=rasterText(line,kanaPx,reader_palette::argb::Accent,japaneseUiFace());img=resizedWidth(img,inner);int h=img.h;pieces.push_back({pad,y,std::move(img)});y+=h+sp(3);
            }
        }
        headingBottom=y-sp(3);
        if(!translation&&(!pos.empty()||!pitch.empty())){
            y+=sp(9);
            const int pitchPx=std::max(14,sp(16));
            auto posLines=uniformLines(pos,inner,detailPx,japaneseUiFace());
            auto pitchLines=uniformLines(pitch,inner,pitchPx,japaneseUiFace());
            auto label=rasterText(pos,detailPx,reader_palette::argb::CardDetail,japaneseUiFace());
            auto accent=rasterText(pitch,pitchPx,reader_palette::argb::Muted,japaneseUiFace());
            if(label.w&&accent.w&&label.w+sp(14)+accent.w<=inner){
                int h=std::max(label.h,accent.h),x=pad+label.w+sp(14);
                pieces.push_back({pad,y+(h-label.h)/2,std::move(label)});
                pieces.push_back({x,y+(h-accent.h)/2,std::move(accent)});
                y+=h+sp(3);
            }else{
                auto addLines=[&](const std::vector<std::wstring>& lines,int px,std::uint32_t color){
                    for(const auto& line:lines){
                        auto image=rasterText(line,px,color,japaneseUiFace());
                        int h=image.h;pieces.push_back({pad,y,std::move(image)});y+=h+sp(3);
                    }
                };
                addLines(posLines,detailPx,reader_palette::argb::CardDetail);
                addLines(pitchLines,pitchPx,reader_palette::argb::Muted);
            }
        }
        std::wstring detail=pos;
        if(!pitch.empty()){if(!detail.empty())detail+=L"   ";detail+=pitch;}
        if(translation&&!detail.empty()) {
            y+=sp(3);
            for(const auto& line:uniformLines(detail,inner,detailPx,uiFace())){
                auto img=rasterText(line,detailPx,reader_palette::argb::CardDetail);img=resizedWidth(img,inner);int h=img.h;pieces.push_back({pad,y,std::move(img)});y+=h+sp(3);
            }
        }
        y+=gap+sp(translation?2:10);
        const int weight=translation?FW_MEDIUM:FW_NORMAL;
        for(const auto& line:uniformLines(definition,inner,definitionPx,bodyFace,weight)) {
            auto img=rasterTextWeighted(line,definitionPx,reader_palette::argb::Body,bodyFace,weight,0);img=resizedWidth(img,inner);int h=img.h;pieces.push_back({pad,y,std::move(img)});y+=h+sp(5);
        }
        int bottom=0;
        for(std::size_t i=0;i<pieces.size();++i){
            const auto& piece=pieces[i];
            bottom=std::max(bottom,piece.y+piece.image.h);
            if(i+1!=badgePiece)
                contentWidth=std::max(contentWidth,piece.x-pad+piece.image.w+(i==0&&!headings.empty()?badgeWidth:0));
        }
        contentWidth=std::max(contentWidth,badgeWidth);
        return (translation?y:bottom+sp(4))+pad;
    };
    const int floor=translation?sp(10):detailPx;
    for(int px=std::max(floor,sp(22));px>=floor;--px){used=layout(px);if(used<=maxHeight)break;}
    if(!translation){
        const int actionWidth=primaryAction.empty()?0:wordActionLabels(primaryAction,scale).width();
        const int fitted=std::min(width,std::max({sp(260),contentWidth+2*pad,actionWidth}));
        if(badgePiece)pieces[badgePiece-1].x-=width-fitted;
        width=fitted;
    }
    // Current user dictionary definitions are <=32 Chinese characters. For
    // unexpected long data, preserve all content by scaling the complete card
    // vertically to its height budget instead of clipping or eliding a meaning.
    int height=used;
    const int minimum=sp(115);
    if(height<minimum&&(translation||height<makeReadingBubble(term,reading,scale).h))height=minimum;
    auto card=tooltipSurface(width,height);
    headwordBar(card,sp(2),sp(10),headingBottom);
    for(const auto& piece:pieces)over(card,piece.image,piece.x,piece.y);
    if(height<=maxHeight)return card;
    Raster compact{width,maxHeight,std::vector<std::uint32_t>(std::size_t(width)*maxHeight)};
    for(int y=0;y<maxHeight;++y) {
        int sy=int((static_cast<long long>(y)*height)/maxHeight);
        std::memcpy(compact.pixels.data()+std::size_t(y)*width,card.pixels.data()+std::size_t(sy)*width,std::size_t(width)*4);
    }
    return compact;
}

void paint(std::uint8_t* dst,int w,int h,int pitch,const PlacedRaster& p) {
    if(!dst || w<=0 || h<=0 || pitch<static_cast<long long>(w)*4 ||
       p.image.w<=0 || p.image.h<=0 || p.image.pixels.size()<std::size_t(p.image.w)*p.image.h) return;
    long long left=std::max(0LL,static_cast<long long>(p.x));
    long long top=std::max(0LL,static_cast<long long>(p.y));
    long long right=std::min(static_cast<long long>(w),static_cast<long long>(p.x)+p.image.w);
    long long bottom=std::min(static_cast<long long>(h),static_cast<long long>(p.y)+p.image.h);
    if(left>=right || top>=bottom)return;
    for(long long y=top;y<bottom;++y) {
        const auto* src=p.image.pixels.data()+std::size_t(y-p.y)*p.image.w+std::size_t(left-p.x);
        auto* row=dst+std::size_t(y)*pitch+std::size_t(left)*4;
        std::memcpy(row,src,std::size_t(right-left)*4);
    }
}
} // namespace mh
