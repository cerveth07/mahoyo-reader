#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "learning_render.h"
#include "reader_palette.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <cstdlib>

// Keep every test live in release/NDEBUG builds; a failure is console-only.
static void requireTest(bool condition,const char* expression,int line){if(!condition){std::fprintf(stderr,"CHECK failed at line %d: %s\n",line,expression);std::exit(2);}}
#undef assert
#define assert(condition) requireTest(bool(condition),#condition,__LINE__)

static bool validUtf16(const std::wstring& text){for(std::size_t i=0;i<text.size();++i){auto c=text[i];if(c>=0xd800&&c<=0xdbff){if(i+1>=text.size()||text[i+1]<0xdc00||text[i+1]>0xdfff)return false;++i;}else if(c>=0xdc00&&c<=0xdfff)return false;}return true;}
static int captionAdvance(const std::wstring& text,int pixelHeight){
    HDC dc=CreateCompatibleDC(nullptr);HFONT f=CreateFontW(-pixelHeight,0,0,0,FW_MEDIUM,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,mh::readerFace());
    auto old=SelectObject(dc,f);SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);
    SelectObject(dc,old);DeleteObject(f);DeleteDC(dc);return size.cx;
}

static void over(mh::Raster& canvas,const mh::Raster& ink,int left,int top) {
    for(int y=0;y<ink.h;++y)for(int x=0;x<ink.w;++x) {
        if(x+left<0 || x+left>=canvas.w || y+top<0 || y+top>=canvas.h)continue;
        auto src=ink.pixels[std::size_t(y)*ink.w+x];
        auto& dst=canvas.pixels[std::size_t(y+top)*canvas.w+x+left];
        unsigned a=src>>24;
        auto c=[&](int shift){return ((((src>>shift)&255)*a+((dst>>shift)&255)*(255-a)+127)/255)<<shift;};
        dst=0xff000000|c(16)|c(8)|c(0);
    }
}
static void bmp(const char* path,const mh::Raster& image) {
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);
    file.bfSize=file.bfOffBits+image.w*image.h*4;
    info.biSize=sizeof(info);info.biWidth=image.w;info.biHeight=-image.h;
    info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file),sizeof(file));
    out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    out.write(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size()*4);
}
int main() {
    std::vector<bool> high;bool particle=false;
    auto pattern=[&](const wchar_t* kana,const wchar_t* pitch,std::vector<bool> expected,bool after){
        return mh::pitchPattern(kana,pitch,high,particle)&&high==expected&&after==particle;
    };
    assert((mh::splitMora(L"しょうどう")==std::vector<std::wstring>{L"しょ",L"う",L"ど",L"う"}));
    assert((mh::splitMora(L"きゅうけつき")==std::vector<std::wstring>{L"きゅ",L"う",L"け",L"つ",L"き"}));
    assert(pattern(L"しょうどう",L"⓪",{false,true,true,true},true));
    assert(pattern(L"いし",L"①",{true,false},false));
    assert(pattern(L"はな",L"②",{false,true},false));
    assert(pattern(L"きゅうけつき",L"④③",{false,true,true,true,false},false));
    assert(!mh::pitchPattern(L"げんじつ",L"辞书重音 ⓪",high,particle));
    assert(!mh::pitchPattern(L"だいさんしゃ",L"①+①",high,particle));
    assert(!mh::pitchPattern(L"しょうどう",L"⑨",high,particle));
    assert(!mh::pitchPattern(L"イマ（原作）",L"①",high,particle));
    assert((mh::splitMora(L"キャットーン")==std::vector<std::wstring>{L"キャ",L"ッ",L"ト",L"ー",L"ン"}));
    assert(mh::loadReaderFont(L"../package/mahoyo-learning/local/NotoSerifSC-Medium.ttf"));
    assert(std::wstring(mh::readerFace())==L"Noto Serif SC Medium");
    mh::Raster canvas{1140,820,std::vector<std::uint32_t>(1140*820,0xff141c22)};
    over(canvas,mh::rasterText(L"同一纹理：中文小字 / 日文原文",24,0xffc3b28e,L"Noto Sans SC"),40,20);
    auto japanese=[&](const wchar_t* s,int x,int y){auto image=mh::rasterText(s,38,0xffeee7db,L"Noto Serif JP");mh::Box box{x,y,x+image.w,y+image.h};over(canvas,image,x,y);return box;};
    // Synthetic author-owned fixtures: no chapter text or private card content
    // is needed in the future public source distribution.
    auto b1=japanese(L"彼の町は丘の中腹に広がっていた。",48,104);
    auto c1=mh::makeChinese(L"他的城镇坐落在半山腰。",{b1},{},25);
    for(auto& p:c1)over(canvas,p.image,p.x,p.y);
    auto b2=japanese(L"隣町に住む友人との約束を忘れてしまい、",48,240);
    auto b3=japanese(L"学校へ行くのも、店に出かけるのも大変な一日だった。",48,324);
    auto c2=mh::makeChinese(L"他忘记了与住在邻镇的朋友的约定，去学校或去商店都成了辛苦的事。",{b2,b3},{},25);
    for(auto& p:c2)over(canvas,p.image,p.x,p.y);
    for(int scale:{100,150}){
        const auto outline=mh::makeBookmark(false,false,scale);
        const auto filled=mh::makeBookmark(true,false,scale);
        const auto hover=mh::makeBookmark(false,true,scale);
        auto marked=mh::makeBookmark(true,false,scale,true);
        assert(marked.pixels!=filled.pixels&&mh::makeBookmark(false,false,scale,true).pixels==outline.pixels);
        const int size=26*scale/100;
        assert(outline.w==size&&outline.h==size&&hover.w==size&&hover.h==size);
        const int center=(size/2)*size+size/2;
        assert(outline.pixels[center]==0&&(filled.pixels[center]>>24)==255&&(hover.pixels[center]>>24)==224);
        assert(hover.pixels.front()==0&&std::any_of(outline.pixels.begin(),outline.pixels.end(),
            [](auto pixel){return (pixel>>24)>0&&(pixel>>24)<158;}));
    }
    auto tip=mh::makeTooltip(L"中腹",L"ちゅうふく",L"名",L"半山腰",L"N1",L"⓪");
    auto wordCard=mh::makeTooltip(L"译文",L"",L"",L"青子没有回答。",L"",L"");
    auto translatedCard=mh::makeTooltip(L"译文",L"",L"",L"青子没有回答。",L"",L"",420,100,mh::CardContent::Translation);
    assert(wordCard.pixels!=translatedCard.pixels);
    // The final definition row ends exactly one 22px inset above the action bar.
    const int pad=22;
    assert(tip.h>pad+1);
    const auto lastDefinitionRow=tip.pixels.end()-(pad+1)*tip.w;
    const auto firstPaddingRow=lastDefinitionRow+tip.w;
    assert(std::any_of(lastDefinitionRow+1,firstPaddingRow-1,[](auto p){return p!=reader_palette::argb::Card;}));
    assert(std::all_of(firstPaddingRow+1,firstPaddingRow+tip.w-1,[](auto p){return p==reader_palette::argb::Card;}));
    mh::roundCardCorners(tip,100);
    assert((tip.pixels.front()>>24)==0&&(tip.pixels[tip.w/2]>>24)>200);
    assert(std::any_of(tip.pixels.begin(),tip.pixels.begin()+tip.w/2,[](auto p){auto alpha=p>>24;return alpha>0&&alpha<200;}));
    over(canvas,tip,48,430);
    auto longtip=mh::makeTooltip(L"安らぎ",L"やすらぎ",L"名",L"身心不受打扰的平静状态；也可以形容让人放松、恢复精神的感受。",L"N1",L"⓪④");
    over(canvas,longtip,520,430);
    auto tiny=mh::makeChinese(L"这是用于验证极端长译文仍完整绘制的压力样本，并非游戏新增台词。",{{48,735,180,770}});
    over(canvas,mh::rasterText(L"窄边界压力样本：完整压缩显示，无新增行",15,0xffafa99b,L"Noto Sans SC"),48,676);
    for(auto& p:tiny)over(canvas,p.image,p.x,p.y);
    assert(!c1.empty() && !c2.empty() && !tiny.empty());
    const mh::Box rubyRow{42,60,800,110};
    auto flow=[&](const std::wstring& cn,std::vector<mh::Box> ruby){
        return mh::makeChinese(cn,{rubyRow},{},25,{ruby});
    };
    auto left=flow(L"那是",{{400,35,425,60}});
    int inset=(mh::planChinese(L"那是",{rubyRow},{},25).pixelHeight+1)/2;
    assert(left.size()==1 && left.front().x==rubyRow.l+inset);
    auto crossing=flow(L"那是在她初次接触到生命的肃穆早晨发生的事。",{{185,35,209,60},{300,35,335,60}});
    assert(crossing.size()>=2 && crossing.front().x>=rubyRow.l && crossing.front().x<=rubyRow.l+inset);
    for(auto& p:crossing){
        assert(p.x+p.image.w<=rubyRow.r);
        for(auto mark:std::vector<mh::Box>{{185,35,209,60},{300,35,335,60}})
            assert(p.x+p.image.w<=mark.l-5 || p.x>=mark.r+5);
    }
    assert(flow(L"从头读起。",{{41,35,90,60}}).front().x>90);
    assert(flow(L"译文必须完整。",{{41,35,801,60}}).empty());
    const std::wstring clause=L"青子没有回答，";
    auto clausePlan=mh::planChinese(clause,{rubyRow},{},25);
    int freeWidth=captionAdvance(clause,clausePlan.pixelHeight)+2*clausePlan.outlinePixels+clausePlan.shadowOffset-1;
    int markLeft=rubyRow.l+freeWidth+(clausePlan.pixelHeight+1)/2;
    auto beforeMark=flow(clause,{{markLeft,35,markLeft+32,60}});
    assert(beforeMark.size()==1 && beforeMark[0].x+beforeMark[0].image.w<=markLeft-std::max(5,clausePlan.pixelHeight/3));
    // Only the three Han-to-Han boundaries get extra space; the punctuation
    // and digits form the same runs in a tight caption and a generous one.
    const std::wstring trackingText=L"春风又来，12。";
    auto trackingPlan=mh::planChinese(trackingText,{{0,40,900,80}},{},25);
    int naturalSpan=captionAdvance(trackingText,trackingPlan.pixelHeight)
                    +2*trackingPlan.outlinePixels+trackingPlan.shadowOffset;
    auto tight=mh::makeChinese(trackingText,{{0,40,naturalSpan,80}},{},25);
    auto roomy=mh::makeChinese(trackingText,{{0,40,900,80}},{},25);
    assert(tight.size()==1 && roomy.size()==1);
    int firstInset=std::max(1,(trackingPlan.pixelHeight+1)/2);
    assert(roomy[0].x==firstInset && tight[0].x==std::min(firstInset,naturalSpan-tight[0].image.w));
    int tracking=std::clamp((trackingPlan.pixelHeight+8)/16,1,3);
    assert(roomy[0].image.w==tight[0].image.w+3*tracking);
    assert(roomy[0].image.h==tight[0].image.h);
    auto tightLatin=mh::makeChinese(L"AB12",{{0,40,120,80}},{},25);
    auto roomyLatin=mh::makeChinese(L"AB12",{{0,40,900,80}},{},25);
    assert(tightLatin.size()==1 && roomyLatin.size()==1 && tightLatin[0].image.pixels==roomyLatin[0].image.pixels);
    // Cards fit their content between the 260px minimum and the 420px cap; long meanings wrap within the cap.
    assert(tip.w==260 && tip.h<=180 && longtip.w>tip.w && longtip.w<=420 && longtip.h<=300);
    for(auto p:tiny)assert(p.image.w<=132);
    auto fitted=mh::planChinese(L"光线明亮时，较深的描边仍然围绕文字。",{b2,b3},{},25);
    auto fittedImages=mh::makeChinese(L"光线明亮时，较深的描边仍然围绕文字。",{b2,b3},{},25);
    assert(fitted.outlinePixels==2 && fitted.shadowOffset==1 && fitted.pixelHeight>=12);
    for(const auto& p:fittedImages)assert(p.image.h<=25);
    // A known source/translation clause pair must follow the Japanese clause
    // end, even when the first Japanese box is disproportionately wide.
    auto clauses=mh::planChinese(L"上学很辛苦，外出游玩也很不方便。",
        {{0,30,900,68},{0,98,500,136}},
        {L"学校に通うのは大変で、",L"遊びに出かけるのも不便だった。"});
    assert(clauses.punctuationAnchored && !clauses.relaxedAnchors);
    assert(clauses.rows.size()==2 && clauses.rows[0].text==L"上学很辛苦，");
    auto key5012=mh::planChinese(L"虽然你可能无法相信，但那真的是以自己意志行动的人偶。之前的是这样，今天的这个也是一样。”",
        {{0,30,800,68},{0,98,800,136}},
        {L"信じられないだろうけど自分の意志で動く人形なの。この",L"前のも、今のヤツもね」"},25);
    assert(key5012.rows.size()==2 && key5012.rows[0].text==L"虽然你可能无法相信，但那真的是以自己意志行动的人偶。" &&
        key5012.rows[1].text==L"之前的是这样，今天的这个也是一样。”");
    auto opener=mh::planChinese(L"“",{{0,30,96,62}});
    assert(opener.pixelHeight>=12 && !opener.horizontalCompression);
    auto punctuationTail=mh::planChinese(L"他就是个普通人―――",
        {{0,30,700,62},{0,82,64,114}},{L"普通のひとだった―",L"――"});
    assert(punctuationTail.rows.size()==1 && punctuationTail.rows[0].text==L"他就是个普通人―――");
    // Three rows make the balanced splitter reject a punctuation-only third
    // fragment, exercising prefix's kinsoku fallback with an extension-B pair.
    const std::wstring supplementary=L"A\U0002000B。";
    auto basePlan=mh::planChinese(supplementary,{{0,30,600,86}}, {},25);
    int firstSpan=captionAdvance(L"A\U0002000B",basePlan.pixelHeight)+2*basePlan.outlinePixels+basePlan.shadowOffset;
    auto surrogatePlan=mh::planChinese(supplementary,{{0,30,firstSpan,86},{0,111,600,167},{0,192,600,248}}, {},25);
    std::wstring joined;for(const auto& row:surrogatePlan.rows){assert(validUtf16(row.text));joined+=row.text;}
    assert(joined==supplementary && surrogatePlan.rows.size()==2);
    assert(surrogatePlan.rows.front().text==L"A");
    // Validate actual BGRA pitch clipping and guard memory, including a negative
    // origin and repeated writes to the same locked texture rectangle.
    constexpr int width=32,height=18,pitch=width*4+24,guard=64;
    std::vector<std::uint8_t> memory(guard+height*pitch+guard,0xa7);
    auto cropped=mh::PlacedRaster{-11,-4,tip};
    mh::paint(memory.data()+guard,width,height,pitch,cropped);
    auto once=memory;
    mh::paint(memory.data()+guard,width,height,pitch,cropped);
    assert(memory==once);
    assert(std::all_of(memory.begin(),memory.begin()+guard,[](auto b){return b==0xa7;}));
    assert(std::all_of(memory.end()-guard,memory.end(),[](auto b){return b==0xa7;}));
    for(int y=0;y<height;++y)assert(std::all_of(memory.begin()+guard+y*pitch+width*4,memory.begin()+guard+(y+1)*pitch,[](auto b){return b==0xa7;}));
    // Warm GDI font caches before checking that every selected bitmap, font and
    // private DC is released. Parallel rendering uses no shared GDI objects.
    for(int i=0;i<3;++i)mh::rasterText(L"字体资源释放",20,0xffffffff);
    DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    std::vector<std::thread> threads;
    for(int thread=0;thread<4;++thread)threads.emplace_back([&]{for(int i=0;i<80;++i){auto a=mh::makeChinese(L"他的城镇坐落在半山腰。",{b1});auto b=mh::makeTooltip(L"安らぎ",L"やすらぎ",L"名",L"平静、放松的状态",L"N1",L"⓪④");assert(!a.empty() && !b.pixels.empty());}});
    for(auto& thread:threads)thread.join();
    GdiFlush();
    DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    assert(after<=before+1);
    bmp("render-test.bmp",canvas);
    std::printf("GDI renderer passed: 320 parallel tooltip/translation cycles; handles %lu -> %lu; tooltip %dx%d; long %dx%d; clipping and idempotence passed.\n",before,after,tip.w,tip.h,longtip.w,longtip.h);
}
