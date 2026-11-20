// Copyright (c) 2026. MIT. Game text and personal Anki data are separate local inputs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <d3d9.h>
#include <stdint.h>
#include <cstdio>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <tuple>
#include <fstream>
#ifdef MAHOYO_QA_CORPUS_REPLAY
#include <filesystem>
#endif
#include <sstream>
#include <cstring>
#include <climits>
#include <cfenv>
#include <xmmintrin.h>
#include <bcrypt.h>
#include <array>
#include <memory>
#include "MinHook.h"
#include "learning_render.h"
#include "reader_palette.h"

extern "C" __declspec(dllexport) const char* WINAPI MahoyoStudyDebugJson();
namespace {
// D3D9/game callbacks may enter with FP exceptions unmasked. In particular,
// Clang vectorizes quad coordinates using packed SSE lanes whose unused upper
// values are unspecified by the Windows float-argument ABI. Keep our drawing
// arithmetic non-trapping, then restore the caller's complete FP environment.
class StudyFpScope{
    fenv_t saved{};unsigned mxcsr;bool held;
public:
    StudyFpScope()noexcept:mxcsr(_mm_getcsr()),held(feholdexcept(&saved)==0){_mm_setcsr((mxcsr|0x1f80u)&~0x3fu);}
    ~StudyFpScope(){if(held)fesetenv(&saved);_mm_setcsr(mxcsr);}
    StudyFpScope(const StudyFpScope&)=delete;StudyFpScope&operator=(const StudyFpScope&)=delete;
};
HMODULE moduleHandle{}, realD3D{};
uintptr_t gameBase{};
HWND gameWindow{};
std::wstring modRoot;
std::recursive_mutex stateMutex;
bool ready=false, initialized=false, loggedLanguage=false, painting=false;
const char*disabledReason="not_initialized";
uint64_t frameNo=1;
std::atomic<bool> inlineTranslation{true};
bool recordLookups=false,parallelHighlight=true;
bool deviceHooked=false;
int includeN2=0, reserveSpace=1, auditOriginalLayout=0, debugDrawCapture=0, auditLayouts=0;
FILE* logFile{},*auditFile{};
void writeRuntimeAudit(bool force=false){
    if(!auditLayouts||modRoot.empty())return;
    static ULONGLONG last=0;auto now=GetTickCount64();if(!force&&now-last<2000)return;last=now;
    auto path=modRoot+L"\\runtime-state.json",temp=path+L".tmp";
    if(FILE*f=_wfopen(temp.c_str(),L"wb")){const char*json=MahoyoStudyDebugJson();fwrite(json,1,strlen(json),f);fclose(f);MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);}
}
void log(const char* msg){if(logFile){fprintf(logFile,"%llu %s\n",GetTickCount64(),msg);fflush(logFile);}}
// Build 13060638 ends exactly at the end of its last section. Its original PE
// checksum and certificate-directory fields are zero. All other bytes, including
// every other PE header field, must hash to the original build fingerprint.
bool hashGameImage(HANDLE file,BCRYPT_HASH_HANDLE hash,decltype(&BCryptHashData) update){
    constexpr DWORD imageBytes=6237464,checksumOffset=392,certificateDirectory=472;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<imageBytes||size.QuadPart>imageBytes+32*1024*1024)return false;
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read=0;
    if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())return false;
    auto u32=[&](size_t pos){uint32_t n;memcpy(&n,bytes.data()+pos,sizeof(n));return n;};
    const auto tail=bytes.size()-imageBytes;
    if(u32(certificateDirectory)!=(tail?imageBytes:0)||u32(certificateDirectory+4)!=tail)return false;
    for(size_t pos=imageBytes;pos<bytes.size();){
        const auto remaining=bytes.size()-pos;
        if(remaining<8)return false;
        const size_t length=u32(pos);
        // WIN_CERT_REVISION_2_0, WIN_CERT_TYPE_PKCS_SIGNED_DATA, padded to 8 bytes.
        if(length<=8||length>remaining||u32(pos+4)!=0x00020200)return false;
        const size_t padded=(length+7)&~size_t(7);
        if(padded>remaining)return false;
        pos+=padded;
    }
    std::fill_n(bytes.data()+checksumOffset,4,0);
    std::fill_n(bytes.data()+certificateDirectory,8,0);
    return update(hash,bytes.data(),imageBytes,0)>=0;
}
bool sha256File(const wchar_t*path,std::array<uint8_t,32>&digest,bool gameImage=false){
    HMODULE dll=LoadLibraryExW(L"bcrypt.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll)return false;
    auto open=(decltype(&BCryptOpenAlgorithmProvider))GetProcAddress(dll,"BCryptOpenAlgorithmProvider");auto close=(decltype(&BCryptCloseAlgorithmProvider))GetProcAddress(dll,"BCryptCloseAlgorithmProvider");
    auto property=(decltype(&BCryptGetProperty))GetProcAddress(dll,"BCryptGetProperty");auto create=(decltype(&BCryptCreateHash))GetProcAddress(dll,"BCryptCreateHash");auto update=(decltype(&BCryptHashData))GetProcAddress(dll,"BCryptHashData");auto finish=(decltype(&BCryptFinishHash))GetProcAddress(dll,"BCryptFinishHash");auto destroy=(decltype(&BCryptDestroyHash))GetProcAddress(dll,"BCryptDestroyHash");
    bool ok=false;BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};HANDLE file=INVALID_HANDLE_VALUE;
    if(open&&close&&property&&create&&update&&finish&&destroy&&open(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0){DWORD size=0,received=0;if(property(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof(size),&received,0)>=0&&size&&size<=1024*1024){std::vector<uint8_t>object(size);if(create(algorithm,&hash,object.data(),size,nullptr,0,0)>=0){file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){if(gameImage)ok=hashGameImage(file,hash,update);else{std::array<uint8_t,65536>buffer{};ok=true;for(;;){DWORD count=0;if(!ReadFile(file,buffer.data(),(DWORD)buffer.size(),&count,nullptr)){ok=false;break;}if(!count)break;if(update(hash,buffer.data(),count,0)<0){ok=false;break;}}}if(ok)ok=finish(hash,digest.data(),(ULONG)digest.size(),0)>=0;}destroy(hash);hash=nullptr;}}}
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(algorithm)close(algorithm,0);FreeLibrary(dll);return ok;
}
bool matchesSupportedBuild(const wchar_t*path){
    std::array<uint8_t,32>digest{};if(!sha256File(path,digest,true))return false;
    constexpr char expected[]="7a718cfca3fccafd9d7984e41206473a6152098d6e0f7374c52abf1581b6ad9a";const char hex[]="0123456789abcdef";std::string actual;for(auto b:digest){actual+=hex[b>>4];actual+=hex[b&15];}return actual==expected;
}
bool supportedExecutable(){
    wchar_t path[32768];DWORD length=GetModuleFileNameW(nullptr,path,32768);
    return length&&length<32768&&matchesSupportedBuild(path);
}
bool readable(const void* address,size_t n){
    if(!address||!n)return false;
    uintptr_t p=(uintptr_t)address, end=p+n;
    if(end<p)return false;
    while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery((void*)p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;auto next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=p)return false;p=next;}
    return true;
}
template<class T>T read(const void* p,size_t off=0,T fallback={}){T v{};const auto q=(const uint8_t*)p+off;if(readable(q,sizeof(v)))memcpy(&v,q,sizeof(v));else v=fallback;return v;}
std::wstring nativeString(const void* p){
    auto len=read<size_t>(p,16),cap=read<size_t>(p,24);
    if(len>8192||cap<len)return {};
    auto s=cap<8?(const wchar_t*)p:read<const wchar_t*>(p);
    if(!readable(s,(len+1)*2))return {};
    return std::wstring(s,len);
}
std::wstring plain(const std::wstring& s){
    std::wstring o;
    for(size_t p=0;p<s.size();){
        if(s[p]==L'<'){auto bar=s.find(L'|',p+1),end=s.find(L'>',p+1);if(bar!=s.npos&&end!=s.npos&&bar<end){o.append(s,p+1,bar-p-1);p=end+1;continue;}}
        if(s[p]==L'<'){auto end=s.find(L'>',p+1);if(end!=s.npos){p=end+1;continue;}}
        if(s[p]==L'@'&&p+1<s.size()&&s[p+1]==L'o'){p+=2;continue;}
        if(s[p]==L'^'||s[p]==L'\r'||s[p]==L'\n'){++p;continue;}
        o+=s[p++];
    }return o;
}
bool whitespace(wchar_t c){return c==L' '||c==L'\t'||c==L'\r'||c==L'\n'||c==L'　';}
std::set<std::wstring>nativeRubyTags={L"<r　>"};
std::wstring reserved(const std::wstring& text){
    std::wstring o;
    // The engine renders author ruby itself. Add caption slots only on the
    // unannotated parts; in particular, never nest a spacer in an author span.
    for(size_t i=0;i<text.size();i++){auto c=text[i];
        if(c==L'<'){auto end=text.find(L'>',i+1);if(end!=text.npos){
            auto tag=text.substr(i,end-i+1);auto bar=tag.find(L'|');
            if(bar!=tag.npos){o+=L"<r"+tag.substr(bar+1);o+=tag.substr(1,bar-1)+L"</r>";}
            else if(nativeRubyTags.count(tag)){
                auto close=text.find(L"</r>",end+1);
                if(close!=text.npos){o.append(text,i,close+4-i);i=close+3;continue;}
                o+=tag;
            }else o+=tag;
            i=end;continue;
        }}
        if(c==L'@'&&i+1<text.size()){o+=c;o+=text[++i];continue;}
        if(whitespace(c)||c==L'<'||c==L'>'||c==L'|'||c==L'@'||c==L'^')o+=c;
        else{o+=L"<r　>";o+=c;if(c>=0xd800&&c<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)o+=text[++i];o+=L"</r>";}
    }return o;
}
std::wstring normalized(std::wstring s){for(auto&c:s)if(c>=0xff01&&c<=0xff5e)c-=0xfee0;return s;}
struct Word{uint32_t start{},end{};std::wstring term,reading,pos,definition,level,pitch;};
bool containsHan(const std::wstring& text){
    for(size_t i=0;i<text.size();++i){
        unsigned c=text[i];
        if(c>=0xd800&&c<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)
            c=0x10000+((c-0xd800)<<10)+(text[++i]-0xdc00);
        if((c>=0x3400&&c<=0x9fff)||(c>=0xf900&&c<=0xfaff)||(c>=0x20000&&c<=0x323af)||c==0x3005)return true;
    }
    return false;
}
struct Record{int id{};std::wstring ja,zh,clean;std::vector<Word> words;};
struct SentenceGloss{const wchar_t* text;bool withCaption;};
const std::map<int,SentenceGloss> sentenceGlosses={
#include "../build/sentence_glosses.inc"
};
bool glossOnly(int id){
    auto gloss=sentenceGlosses.find(id);
    return gloss!=sentenceGlosses.end()&&!gloss->second.withCaption;
}
std::wstring detailedTranslation(const Record& record){
    auto gloss=sentenceGlosses.find(record.id);
    if(gloss==sentenceGlosses.end())return record.zh;
    if(gloss->second.withCaption)return record.zh+L"\n"+gloss->second.text;
    return gloss->second.text;
}
std::map<int,Record> records;
struct Reader{
    std::vector<uint8_t>b;size_t p{};
    uint32_t u(){if(p>b.size()||b.size()-p<4)throw 1;uint32_t v;memcpy(&v,b.data()+p,4);p+=4;return v;}
    std::wstring s(){auto n=u();if(n>100000||p>b.size()||n>(b.size()-p)/2)throw 2;std::wstring s(n,L'\0');memcpy(s.data(),b.data()+p,n*2);p+=n*2;
        for(size_t i=0;i<s.size();i++){auto c=s[i];if(!c)throw 2;if(c>=0xd800&&c<=0xdbff){if(++i>=s.size()||s[i]<0xdc00||s[i]>0xdfff)throw 2;}else if(c>=0xdc00&&c<=0xdfff)throw 2;}return s;}
};
#include "word_forms.inc"
bool loadData(){
    try{
        FILE*f=_wfopen((modRoot+L"\\local\\story.dat").c_str(),L"rb");if(!f){log("Missing local/story.dat");return false;}
        fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);if(n<12||n>16*1024*1024){fclose(f);return false;}
        Reader r;r.b.resize(n);size_t got=fread(r.b.data(),1,n,f);fclose(f);if(got!=(size_t)n||memcmp(r.b.data(),"MHJL0002",8))return false;r.p=8;
        auto count=r.u();if(!count || count>100000)throw 3;std::map<int,Record> parsed;
        for(uint32_t i=0;i<count;i++){Record e;auto id=r.u();if(id>=count)throw 3;e.id=(int)id;e.ja=r.s();e.zh=r.s();e.clean=plain(e.ja);if(e.clean.empty()||e.zh.empty())throw 3;auto wc=r.u();if(wc>2000)throw 3;for(uint32_t j=0;j<wc;j++){Word w;w.start=r.u();w.end=r.u();w.term=r.s();w.reading=r.s();w.pos=r.s();w.definition=r.s();w.level=r.s();w.pitch=r.s();if(w.start>=w.end||w.end>e.clean.size()||w.term.empty()||e.clean.substr(w.start,w.end-w.start)!=w.term)throw 3;e.words.push_back(std::move(w));}if(!parsed.emplace(e.id,std::move(e)).second)throw 3;}
        if(r.p!=r.b.size())return false;
        std::set<std::wstring>rubyTags={L"<r　>"};for(auto&[_,record]:parsed)for(size_t p=0;(p=record.ja.find(L'<',p))!=record.ja.npos;){auto end=record.ja.find(L'>',p+1),bar=record.ja.find(L'|',p+1);if(end==record.ja.npos)break;if(bar!=record.ja.npos&&bar<end)rubyTags.insert(L"<r"+record.ja.substr(bar+1,end-bar-1)+L">");p=end+1;}
        loadInflections(parsed);
        nativeRubyTags.swap(rubyTags);records.swap(parsed);log("Loaded validated full-game Japanese/Chinese text and study notes");return true;
    }catch(...){log("Invalid local data; native game remains unchanged");return false;}
}
struct Glyph{mh::Box box;int line{};size_t start{},end{};void*node{};};
struct WordBox{mh::Box box;Word word;int recordId=-1;};
struct PersonalGlyph{mh::Box box;int id,line,start,end;};
struct ReadingInk{mh::PlacedRaster raster;int recordId;int sentenceRow=-1;};
struct Annotation{std::vector<mh::PlacedRaster> chinese;std::shared_ptr<const std::vector<ReadingInk>> readingInk;std::vector<WordBox> words;std::vector<mh::Box> body;std::vector<int> visibleRight;std::vector<std::pair<mh::Box,int>> sentenceRows;int firstId{};bool nativeCaptions=false, companionCaption=false;void*owner{};std::vector<PersonalGlyph> personalGlyphs;std::vector<WordBox> personalBoxes;unsigned personalVersion=0;std::vector<mh::Box> authorMarks;};
struct CaptionSource{int recordId{},line{};size_t start{},end{};};
struct RubySlot{void*node{};int x{},y{},w{},h{},line{};size_t source{};bool used=false;};
struct PartAudit{int id{};size_t expected{},matched{};std::vector<mh::Box> body;std::vector<std::pair<size_t,uint32_t>> unmapped;std::vector<std::wstring> japaneseLines;int targetRubyHeight=0;std::vector<mh::Box> inkRows;};
struct Snapshot{std::wstring input;std::vector<std::pair<Record*,size_t>> parts;Annotation annotation;std::vector<PartAudit> audit;std::vector<mh::Box> unmappedNativeBody,occupiedRuby;std::vector<CaptionSource> captionSources;std::vector<RubySlot> rubySlots;std::vector<size_t>chosenSlots;std::vector<std::string>slotDiagnostics;const char*failureStage="";bool nonRubyNodesUnchanged=false;};
std::map<void*,std::shared_ptr<Snapshot>> snapshots;
std::set<int> layoutSeen,nativeSeen,layoutIncomplete;uint64_t layoutAttempts=0,layoutSkipped=0;
thread_local std::shared_ptr<Snapshot> currentText;
struct Affine{float a{},b{},c{},d{},e{},f{};bool valid=false;};
struct Layer{
    void* native{};IDirect3DTexture9* gpu{};int w{},h{},gpuW{},gpuH{};D3DFORMAT format{};
    uint8_t*locked{};int pitch{};std::shared_ptr<Snapshot> incoming;
    std::vector<Annotation> pageAnnotations;Affine mapping;uint64_t drawn{},lastUsed{},occludedFrame{};UINT drawnAlpha=255;bool bitmapEmpty=false;
    std::vector<IDirect3DTexture9*> ownedTextures;
    std::map<IDirect3DSurface9*,std::pair<Affine,uint64_t>> surfaces;
    std::map<IDirect3DTexture9*,std::pair<Affine,uint64_t>> composites;
    std::map<IDirect3DSurface9*,UINT>surfaceAlpha;
    std::map<IDirect3DTexture9*,UINT>compositeAlpha;
};
std::map<void*,Layer> layers;
std::map<IDirect3DTexture9*,void*> gpuLayers;
std::map<IDirect3DTexture9*,void*> compositeLayers;
void forgetLayerTextures(Layer&l){for(auto i=gpuLayers.begin();i!=gpuLayers.end();)if(i->second==l.native)i=gpuLayers.erase(i);else ++i;for(auto*t:l.ownedTextures)t->Release();l.ownedTextures.clear();l.gpu=nullptr;}
void forgetDefaultTextures(Layer&l){for(auto i=l.ownedTextures.begin();i!=l.ownedTextures.end();){D3DSURFACE_DESC desc{};auto*t=*i;if(FAILED(t->GetLevelDesc(0,&desc))||desc.Pool==D3DPOOL_DEFAULT){auto owner=gpuLayers.find(t);if(owner!=gpuLayers.end()&&owner->second==l.native)gpuLayers.erase(owner);if(l.gpu==t)l.gpu=nullptr;t->Release();i=l.ownedTextures.erase(i);}else ++i;}}
void rememberTexture(Layer&l,IDirect3DTexture9*t){if(!t)return;if(std::find(l.ownedTextures.begin(),l.ownedTextures.end(),t)==l.ownedTextures.end()){t->AddRef();l.ownedTextures.push_back(t);}gpuLayers[t]=l.native;}
mh::Box unite(mh::Box a,mh::Box b){return {std::min(a.l,b.l),std::min(a.t,b.t),std::max(a.r,b.r),std::max(a.b,b.b)};}
bool intersects(mh::Box a,mh::Box b){return a.l<b.r&&b.l<a.r&&a.t<b.b&&b.t<a.b;}
bool contains(mh::Box b,float x,float y){return x>=b.l&&x<=b.r&&y>=b.t&&y<=b.b;}
std::vector<int> idsIn(const std::wstring& key){
    std::vector<int> ids;
    for(size_t p=0;p+7<=key.size();p++)if(key[p]==L'$'){
        int n=0,k=0;for(;k<6;k++){auto c=key[p+1+k];if(c<L'0'||c>L'9')break;n=n*10+c-L'0';}
        if(k==6&&records.count(n)){ids.push_back(n);p+=6;}
    }return ids;
}
bool blankNativeBitmap(void*bitmap){
    int w=read<int>(bitmap,0x84),h=read<int>(bitmap,0x88),pitch=read<int>(bitmap,0x98);auto*p=read<const uint8_t*>(bitmap,0x90);
    if(w<1||w>2048||h<1||h>256||pitch<w*4||!readable(p,(size_t)pitch*h))return false;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(p[(size_t)y*pitch+x*4+3])return false;return true;
}
bool rubyBelongsToBase(const Glyph&g,int x,int y,int w,int h,int tallest){
    int center=2*x+w;return center>=2*g.box.l-2&&center<=2*g.box.r+2&&y<=g.box.t&&y+h<=g.box.b&&g.box.t-(y+h)<=tallest;
}
mh::Box originalGlyphInk(const Glyph& glyph){
    auto bitmap=static_cast<const uint8_t*>(glyph.node)+0x30;
    const int w=glyph.box.r-glyph.box.l,h=glyph.box.b-glyph.box.t;
    const int pitch=read<int>(bitmap,0x98);
    auto pixels=read<const uint8_t*>(bitmap,0x90);
    if(pitch<w*4||pitch>32768||!readable(pixels,size_t(pitch)*h))return glyph.box;
    mh::Box ink{w,h,0,0};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        if(pixels[size_t(y)*pitch+x*4+3]<12)continue;
        ink.l=std::min(ink.l,x);ink.r=std::max(ink.r,x+1);
        ink.t=std::min(ink.t,y);ink.b=std::max(ink.b,y+1);
    }
    if(ink.r<=ink.l)return glyph.box;
    return {glyph.box.l+ink.l,glyph.box.t+ink.t,glyph.box.l+ink.r,glyph.box.t+ink.b};
}
void makeSnapshot(void* self,Snapshot& s,bool prepareCaptions=true,bool mapOriginal=false){
    StudyFpScope fp;
    auto sentinel=read<void*>(self,0x338);auto node=read<void*>(sentinel);
    auto count=read<size_t>(self,0x340);if(count>6000)return;
    std::vector<Glyph> glyphs;std::vector<mh::Box> anonymousNodes;size_t cursor=0;int tallest=0;
    std::vector<std::pair<Glyph,std::wstring>> namedNodes,originalNodes;
    for(size_t k=0;k<count&&node&&node!=sentinel;k++,node=read<void*>(node)){
        if(!readable(node,0x1b0))break;auto token=nativeString((uint8_t*)node+0x170);
        int x=read<int>(node,0x20),y=read<int>(node,0x24),w=read<int>(node,0x30+0x84),h=read<int>(node,0x30+0x88),line=read<int>(node,0x19c);
        if(w<=0||w>2048||h<=0||h>1024||x< -4096||x>8192||y< -4096||y>8192)continue;
        tallest=std::max(tallest,h);
        if(mapOriginal)originalNodes.push_back({{{x,y,x+w,y+h},line,0,0,node},token});
        if(token.empty()){anonymousNodes.push_back({x,y,x+w,y+h});continue;}
        if(std::all_of(token.begin(),token.end(),whitespace))continue;
        namedNodes.push_back({{{x,y,x+w,y+h},line,0,0,node},std::move(token)});
    }
    for(auto& [glyph,token]:namedNodes){
        if(glyph.box.b-glyph.box.t<=tallest*3/4)continue;
        auto at=s.input.find(token,cursor);if(at==s.input.npos)continue;cursor=at+token.size();
        glyph.start=at;glyph.end=cursor;glyphs.push_back(glyph);
    }
    for(auto b:anonymousNodes)if(b.b-b.t>tallest*3/4)s.unmappedNativeBody.push_back(b);
    // Plain balloon text is a sequence of anonymous glyphs. The full-size
    // atlas nodes include indentation, independently of the script's line breaks.
    // Accept the positional match only if it consumes the entire source and all
    // named anchors agree. A missing glyph must never shift a later sentence.
    if(mapOriginal&&!s.unmappedNativeBody.empty()){
        std::vector<Glyph> original;size_t at=0;bool aligned=true;
        for(auto& [g,token]:originalNodes){
            if(g.box.b-g.box.t<=tallest*3/4)continue;
            if(at>=s.input.size()||(!token.empty()&&token[0]!=s.input[at])){aligned=false;break;}
            g.start=at++;
            if(s.input[g.start]>=0xd800&&s.input[g.start]<=0xdbff&&at<s.input.size())++at;
            g.end=at;
            if(!whitespace(s.input[g.start]))original.push_back(g);
        }
        if(aligned&&at==s.input.size()){glyphs=std::move(original);s.unmappedNativeBody.clear();}
    }
    auto& occupiedRuby=s.occupiedRuby;
    node=read<void*>(sentinel);std::map<void*,const Glyph*>baseNodes;for(auto&g:glyphs)baseNodes[g.node]=&g;const Glyph*precedingBase=nullptr;
    for(size_t k=0;k<count&&node&&node!=sentinel;k++,node=read<void*>(node)){
        auto knownBase=baseNodes.find(node);
        if(knownBase!=baseNodes.end() && knownBase->second->box.b-knownBase->second->box.t>tallest*3/4){precedingBase=knownBase->second;continue;}
        if(!readable(node,0x1b0))break;int x=read<int>(node,0x20),y=read<int>(node,0x24),w=read<int>(node,0xb4),h=read<int>(node,0xb8),line=read<int>(node,0x19c);
        if(w<1||w>2048||h<1||h>tallest*3/4)continue;
        bool blank1=blankNativeBitmap((uint8_t*)node+0x30),blank2=blankNativeBitmap((uint8_t*)node+0xd0);
        if(auditLayouts){std::ostringstream info;info<<"{\"slot\":["<<x<<','<<y<<','<<w<<','<<h<<"],\"line\":"<<line<<",\"blank\":["<<blank1<<','<<blank2<<"]";if(precedingBase){auto&g=*precedingBase;info<<",\"base\":["<<g.box.l<<','<<g.box.t<<','<<g.box.r<<','<<g.box.b<<"],\"baseLine\":"<<g.line<<",\"source\":"<<g.start;}info<<'}';s.slotDiagnostics.push_back(info.str());}
        if(!blank1||!blank2){occupiedRuby.push_back({x,y,x+w,y+h});continue;}
        if(!nativeString((uint8_t*)node+0x170).empty())continue;
        // Observed native order is base glyph, then its empty ruby node.
        // Validate position, then inherit the base glyph's line and source.
        // The ruby node's own 0x19c stays zero even on later body rows (observed
        // in records 4/5); it is not the parent paragraph's line index.
        // Never steal another record's same-Y slot based on position alone.
        if(precedingBase){auto&g=*precedingBase;if(rubyBelongsToBase(g,x,y,w,h,tallest))s.rubySlots.push_back({node,x,y,w,h,g.line,g.start,false});}
    }
    s.annotation.authorMarks=occupiedRuby;
    for(auto [record,start]:s.parts){
        std::map<int,mh::Box> lineBoxes;std::map<int,std::pair<size_t,size_t>> lineSpans;std::vector<Glyph> own;
        size_t end=start+record->clean.size();
        PartAudit audit;audit.id=record->id;std::vector<bool> covered(record->clean.size());
        for(size_t i=0;i<record->clean.size();i++)if(!whitespace(record->clean[i]))audit.expected++;
        for(auto& g:glyphs)if(g.start<end&&g.end>start){own.push_back(g);auto a=std::max(start,g.start),b=std::min(end,g.end);for(auto i=a;i<b;i++)covered[i-start]=true;if(lineBoxes.count(g.line)){lineBoxes[g.line]=unite(lineBoxes[g.line],g.box);auto&span=lineSpans[g.line];span.first=std::min(span.first,a);span.second=std::max(span.second,b);}else{lineBoxes[g.line]=g.box;lineSpans[g.line]={a,b};}}
        for(size_t i=0;i<covered.size();i++)if(!whitespace(record->clean[i])){if(covered[i])audit.matched++;else audit.unmapped.push_back({i,(uint32_t)record->clean[i]});}
        // Native opening quotes can have an empty node token even though their
        // full-size bitmap is present. Keep those characters (and indentation)
        // in the alignment source instead of dropping the prefix of each row.
        // Node coverage remains explicitly incomplete until directly observed.
        std::vector<mh::Box> lines;std::vector<int> lineNumbers;std::vector<std::wstring> japaneseLines;size_t sliceStart=0,lineIndex=0;
        for(auto [line,b]:lineBoxes){lines.push_back(b);lineNumbers.push_back(line);s.annotation.body.push_back(b);s.annotation.sentenceRows.emplace_back(b,record->id);auto span=lineSpans[line];size_t sliceEnd=++lineIndex==lineBoxes.size()?record->clean.size():span.second-start;japaneseLines.push_back(record->clean.substr(sliceStart,sliceEnd-sliceStart));sliceStart=sliceEnd;}
        if(mapOriginal){
            std::map<int,mh::Box> ink;
            for(const auto& g:own){
                const auto rect=originalGlyphInk(g);
                auto [it,first]=ink.emplace(g.line,rect);
                if(!first)it->second=unite(it->second,rect);
            }
            for(int n:lineNumbers)audit.inkRows.push_back(ink.at(n));
        }
        audit.body=lines;audit.japaneseLines=japaneseLines;s.audit.push_back(std::move(audit));
        if(lines.empty())continue;
        for(const auto& g:own){
            int a=int(g.start)-int(start),b=int(g.end)-int(start);
            if(b>a)s.annotation.personalGlyphs.push_back({g.box,record->id,g.line,a,b});
        }
        int rubyHeight=INT_MAX;for(auto&slot:s.rubySlots)if(slot.source>=start&&slot.source<end)rubyHeight=std::min(rubyHeight,slot.h);
        s.audit.back().targetRubyHeight=rubyHeight==INT_MAX?0:rubyHeight;
        if(prepareCaptions){
            std::vector<std::vector<mh::Box>> protectedByRow(lines.size());
            for(size_t i=0;i<lines.size();++i){
                for(const auto& slot:s.rubySlots){
                    if(slot.source<start || slot.source>=end || slot.line!=lineNumbers[i])continue;
                    for(auto box:occupiedRuby)
                        if(box.t<slot.y+slot.h && box.b>slot.y && box.l<lines[i].r && box.r>lines[i].l)
                            protectedByRow[i].push_back(box);
                    break;
                }
            }
            auto captions=mh::makeChinese(plain(record->zh),lines,japaneseLines,rubyHeight==INT_MAX?0:rubyHeight,protectedByRow);
            if(captions.empty() && !std::all_of(record->zh.begin(),record->zh.end(),whitespace)){
                s.failureStage="caption-does-not-fit";s.annotation.chinese.clear();s.captionSources.clear();return;
            }
            for(auto& p:captions){size_t row=0;int distance=INT_MAX;int bodyAnchor=p.y+p.image.h+2;for(size_t j=0;j<lines.size();j++){int d=std::abs(lines[j].t-bodyAnchor);if(d<distance){distance=d;row=j;}}s.captionSources.push_back({record->id,lineNumbers[row],start,end});s.annotation.chinese.push_back(std::move(p));}
        }
        for(const auto& sourceWord:record->words){
            auto word=displayedForm(*record,sourceWord);
            const bool readingOnly=!includeN2&&word.level.find(L"N2")!=word.level.npos&&word.level.find(L"N1")==word.level.npos;
            if(readingOnly&&(!containsHan(word.term)||word.reading.empty()))continue;
            size_t a=start+word.start,b=start+word.end;
            std::map<int,mh::Box> spans;
            for(auto& g:own)if(g.start<b&&g.end>a){
                auto r=g.box;float n=float(g.end-g.start);if(n>1){r.l=g.box.l+int((std::max(a,g.start)-g.start)*(g.box.r-g.box.l)/n);r.r=g.box.l+int((std::min(b,g.end)-g.start)*(g.box.r-g.box.l)/n);}
                if(spans.count(g.line))spans[g.line]=unite(spans[g.line],r);else spans[g.line]=r;
            }
            for(auto [line,r]:spans){
                WordBox box{r,word,record->id};
                if(readingOnly){
                    box.word.level=L"补注";
                    box.word.definition.clear();
                    box.word.pitch.clear();
                }
                s.annotation.words.push_back(std::move(box));
            }
        }
    }
}

mh::Box rasterInkBounds(const mh::Raster& input){
    int l=input.w,t=input.h,r=0,b=0;
    for(int y=0;y<input.h;y++)for(int x=0;x<input.w;x++)if(input.pixels[y*input.w+x]>>24){l=std::min(l,x);t=std::min(t,y);r=std::max(r,x+1);b=std::max(b,y+1);}
    return {l,t,r,b};
}
mh::Raster tightRaster(const mh::Raster& input,int maxHeight,float commonScale=1.f){
    auto box=rasterInkBounds(input);int l=box.l,t=box.t,r=box.r,b=box.b;if(r<=l||b<=t||maxHeight<1)return {};
    double factor=std::min({1.,double(commonScale),double(maxHeight)/(b-t)});mh::Raster out;out.w=std::max(1,int((r-l)*factor));out.h=std::max(1,int((b-t)*factor));out.pixels.resize((size_t)out.w*out.h);
    // Area integration in premultiplied space preserves thin strokes when the
    // native ruby slot requires a fractional downscale. Return straight alpha.
    for(int y=0;y<out.h;y++)for(int x=0;x<out.w;x++){
        double x0=l+double(x)*(r-l)/out.w,x1=l+double(x+1)*(r-l)/out.w,y0=t+double(y)*(b-t)/out.h,y1=t+double(y+1)*(b-t)/out.h;
        double alpha=0,red=0,green=0,blue=0;
        for(int sy=(int)y0;sy<(int)ceil(y1);sy++)for(int sx=(int)x0;sx<(int)ceil(x1);sx++){double weight=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)));uint32_t c=input.pixels[sy*input.w+sx];double a=(c>>24)*weight;alpha+=a;red+=((c>>16)&255)*a;green+=((c>>8)&255)*a;blue+=(c&255)*a;}
        auto byte=[](double value){return (uint32_t)std::max(0.,std::min(255.,floor(value+.5)));};uint32_t a=byte(alpha/((x1-x0)*(y1-y0)));
        out.pixels[y*out.w+x]=alpha>0?(a<<24)|(byte(red/alpha)<<16)|(byte(green/alpha)<<8)|byte(blue/alpha):0;
    }
    return out;
}
bool nativeBitmap(void* bitmap,const mh::Raster& image){
    if(image.w<1||image.h<1||image.w>4096||image.h>256||image.pixels.size()!=(size_t)image.w*image.h)return false;
    auto v=read<void**>(bitmap);if(!readable(v,16))return false;
    using Create=int(__fastcall*)(void*,uint32_t,uint32_t,uint32_t,uint32_t);
    auto create=(Create)read<void*>(v,8);if((uintptr_t)create!=gameBase+0x52780)return false;
    if(create(bitmap,image.w,image.h,2,1))return false;
    auto p=read<uint8_t*>(bitmap,0x90);int pitch=read<int>(bitmap,0x98);
    if(!p||pitch<image.w*4||!readable(p,(size_t)pitch*image.h))return false;
    for(int y=0;y<image.h;y++)memcpy(p+y*pitch,image.pixels.data()+y*image.w,image.w*4);
    return true;
}
size_t captionSlot(const std::vector<RubySlot>&slots,const CaptionSource&source){
    size_t best=slots.size();for(size_t j=0;j<slots.size();j++){auto&slot=slots[j];if(slot.used||slot.line!=source.line||slot.source<source.start||slot.source>=source.end)continue;if(best==slots.size()||slot.source<slots[best].source)best=j;}return best;
}
bool installNativeCaptions(void* self,Snapshot& s,bool embed=true){
    StudyFpScope fp;
    auto&slots=s.rubySlots;if(slots.size()<s.annotation.chinese.size()||s.annotation.chinese.empty()||s.captionSources.size()!=s.annotation.chinese.size()){s.failureStage="slot-count-or-caption-contract";return false;}
    struct NodeState{void*node;int x,y,w,h,w2,h2;};std::vector<NodeState>protectedNodes;std::set<void*>slotNodes;
    for(auto&slot:slots)slotNodes.insert(slot.node);
    auto head=read<void*>(self,0x338);auto n=read<void*>(head);auto count=read<size_t>(self,0x340);if(count>6000)return false;
    for(size_t i=0;i<count&&n&&n!=head;i++,n=read<void*>(n))if(!slotNodes.count(n))protectedNodes.push_back({n,read<int>(n,0x20),read<int>(n,0x24),read<int>(n,0xb4),read<int>(n,0xb8),read<int>(n,0x154),read<int>(n,0x158)});
    std::vector<size_t>selected;std::map<int,float>factors;
    for(size_t i=0;i<s.annotation.chinese.size();i++){
        auto source=s.captionSources[i];size_t best=captionSlot(slots,source);
        if(best==slots.size()){s.failureStage="slot-owner-or-line";return false;}slots[best].used=true;selected.push_back(best);s.chosenSlots=selected;
        auto ink=rasterInkBounds(s.annotation.chinese[i].image);if(ink.b<=ink.t){s.failureStage="empty-caption-ink";return false;}float factor=std::min(1.f,float(slots[best].h)/(ink.b-ink.t));auto it=factors.find(source.recordId);if(it==factors.end())factors[source.recordId]=factor;else it->second=std::min(it->second,factor);
    }
    for(size_t i=0;i<selected.size();i++){
        auto&placed=s.annotation.chinese[i];auto&slot=slots[selected[i]];auto image=tightRaster(placed.image,slot.h,factors[s.captionSources[i].recordId]);if(image.pixels.empty())return false;
        int x=placed.x,y=slot.y+slot.h-image.h;
        mh::Box caption{x,y,x+image.w,y+image.h};
        if(std::any_of(s.occupiedRuby.begin(),s.occupiedRuby.end(),[&](mh::Box mark){return intersects(caption,mark);})){
            s.failureStage="caption-over-author-ruby";return false;
        }
        if(embed){
            if(!nativeBitmap((uint8_t*)slot.node+0x30,image)||!nativeBitmap((uint8_t*)slot.node+0xd0,image)){s.failureStage="native-bitmap-write";return false;}
            memcpy((uint8_t*)slot.node+0x20,&x,4);memcpy((uint8_t*)slot.node+0x24,&y,4);
        }
        if(!placed.slice.edges.empty()){
            auto ink=rasterInkBounds(placed.image);
            for(auto& u:placed.slice.edges)u=std::clamp((u*placed.image.w-ink.l)/float(ink.r-ink.l),0.f,1.f);
        }
        placed.x=x;placed.y=y;placed.image=std::move(image);
    }
    s.chosenSlots=selected;
    // Empty placeholder glyphs must not subsequently erase parts of the caption.
    // Keep their native timing/list ownership; clip only their empty bitmap draw.
    if(embed)for(auto&slot:slots)if(!slot.used){int clipped=-64;memcpy((uint8_t*)slot.node+0x20,&clipped,4);memcpy((uint8_t*)slot.node+0x24,&clipped,4);}
    s.nonRubyNodesUnchanged=true;for(auto old:protectedNodes)if(read<int>(old.node,0x20)!=old.x||read<int>(old.node,0x24)!=old.y||read<int>(old.node,0xb4)!=old.w||read<int>(old.node,0xb8)!=old.h||read<int>(old.node,0x154)!=old.w2||read<int>(old.node,0x158)!=old.h2)s.nonRubyNodesUnchanged=false;
    if(!s.nonRubyNodesUnchanged){s.failureStage="non-ruby-node-mutated";return false;}s.annotation.nativeCaptions=true;return true;
}

using LayoutFn=int(__fastcall*)(void*,const wchar_t*,uint32_t);
using ComposeFn=int(__fastcall*)(void*);
using LockFn=int(__fastcall*)(void*,uint8_t**,int*);
using UnlockFn=int(__fastcall*)(void*);
LayoutFn nativeLayout{};ComposeFn nativeCompose{};LockFn nativeLock{};UnlockFn nativeUnlock{};
std::vector<mh::Box> nativeBodyRows(void*self){
    // Static layout evidence: +0x19c is native line; ruby bitmaps are shorter
    // than the corresponding base glyphs. Used for diagnostics, never reflow.
    auto head=read<void*>(self,0x338),node=read<void*>(head);auto count=read<size_t>(self,0x340);if(count>6000)return {};
    struct Item{mh::Box b;int line;};std::vector<Item> items;int tallest=0;
    for(size_t i=0;i<count&&node&&node!=head;i++,node=read<void*>(node)){if(!readable(node,0x1b0))break;int x=read<int>(node,0x20),y=read<int>(node,0x24),w=read<int>(node,0xb4),h=read<int>(node,0xb8);if(w>0&&w<=4096&&h>0&&h<=256&&x>=-4096&&x<=8192&&y>=-4096&&y<=8192){items.push_back({{x,y,x+w,y+h},read<int>(node,0x19c)});tallest=std::max(tallest,h);}}
    std::map<int,mh::Box> grouped;for(auto item:items)if(item.b.b-item.b.t>tallest*3/4){auto it=grouped.find(item.line);if(it==grouped.end())grouped.emplace(item.line,item.b);else it->second=unite(it->second,item.b);}
    std::vector<mh::Box> rows;for(auto [_,b]:grouped)rows.push_back(b);return rows;
}
bool sameBodyRows(const std::vector<mh::Box>& a,const std::vector<mh::Box>& b){
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),
        [](mh::Box x,mh::Box y){return x.l==y.l&&x.t==y.t&&x.r==y.r&&x.b==y.b;});
}
void prepareOriginalStudy(void* self,Snapshot& original,bool companionCaption){
    makeSnapshot(self,original,false,true);
    original.annotation.companionCaption=companionCaption;
    // A single opening quote can be an anonymous native bitmap with no
    // text token. Its record identity is still known from the script key.
    if(original.annotation.body.empty() && original.parts.size()==1){
        original.annotation.body=nativeBodyRows(self);
        for(auto box:original.annotation.body)
            original.annotation.sentenceRows.emplace_back(box,original.parts.front().first->id);
    }
}
bool robinEpisode(int id){return id>=19667&&id<=20466;} // nz1.chs through nz6.chs

// The balloon supplies its own line spacing. Project onto the final text layer
// using the unmodified Japanese layout, including when no empty ruby node exists.
void projectOriginalCaptions(Snapshot& original){
    std::vector<mh::Box> occupied=original.occupiedRuby;
    occupied.insert(occupied.end(),original.unmappedNativeBody.begin(),original.unmappedNativeBody.end());
    for(const auto& part:original.audit)
        occupied.insert(occupied.end(),part.inkRows.begin(),part.inkRows.end());
    auto ink=std::make_shared<std::vector<ReadingInk>>();
    int firstRow=0;
    for(const auto& part:original.audit){
        const auto& record=records.at(part.id);
        if(part.body.empty())continue;
        if(glossOnly(part.id)||part.expected!=part.matched){firstRow+=int(part.body.size());continue;}
        auto rows=mh::projectChinese(plain(record.zh),part.inkRows,part.japaneseLines,
            occupied,part.body.front().b-part.body.front().t,robinEpisode(part.id));
        for(auto& row:rows){
            const auto& r=row.raster;
            occupied.push_back({r.x,r.y,r.x+r.image.w,r.y+r.image.h});
            ink->push_back({std::move(row.raster),part.id,firstRow+row.japaneseLine});
        }
        firstRow+=int(part.body.size());
    }
    if(!ink->empty())original.annotation.readingInk=std::move(ink);
}
std::vector<mh::Box> originalRubyGeometry(void* self,const Snapshot& study){
    if(std::none_of(study.parts.begin(),study.parts.end(),[](const auto& part){
        return part.first->ja.find(L'|')!=std::wstring::npos;
    }))return {};
    Snapshot original;
    makeSnapshot(self,original,false);
    return original.occupiedRuby;
}
void writeBoxes(std::ostream&out,const std::vector<mh::Box>&boxes){out<<'[';for(size_t i=0;i<boxes.size();i++){if(i)out<<',';auto b=boxes[i];out<<'['<<b.l<<','<<b.t<<','<<b.r<<','<<b.b<<']';}out<<']';}
void writeJsonString(std::ostream&out,const std::wstring&value){
    // Escape UTF-16 code units directly: JSON preserves surrogate pairs and the
    // audit is independent of the process locale / Windows console code page.
    static const char hex[]="0123456789abcdef";out<<'"';for(uint32_t c:value){if(c=='"'||c=='\\'){out<<'\\'<<(char)c;}else if(c>=0x20&&c<0x7f)out<<(char)c;else{out<<"\\u"<<hex[(c>>12)&15]<<hex[(c>>8)&15]<<hex[(c>>4)&15]<<hex[c&15];}}out<<'"';
}
void auditLayout(const Snapshot&s,const char*status,const std::vector<mh::Box>&before={},const std::vector<mh::Box>&after={}){
    std::lock_guard<std::recursive_mutex>guard(stateMutex);for(auto&a:s.audit){layoutSeen.insert(a.id);if(s.annotation.nativeCaptions)nativeSeen.insert(a.id);if(a.expected!=a.matched)layoutIncomplete.insert(a.id);}if(!auditFile)return;std::ostringstream out;
    out<<"{\"tick\":"<<GetTickCount64()<<",\"frame\":"<<frameNo<<",\"status\":\""<<status<<"\",\"native\":"<<(s.annotation.nativeCaptions?"true":"false")<<",\"projected\":[";
    if(s.annotation.readingInk)for(size_t i=0;i<s.annotation.readingInk->size();++i){
        const auto& ink=(*s.annotation.readingInk)[i];const auto& p=ink.raster;
        if(i)out<<',';
        out<<"{\"id\":"<<ink.recordId<<",\"row\":"<<ink.sentenceRow<<",\"rect\":["<<p.x<<','<<p.y<<','<<p.x+p.image.w<<','<<p.y+p.image.h<<"]}";
    }
    out<<"],\"sourceLength\":"<<s.input.size()<<",\"originalInk\":[";
    for(size_t i=0;i<s.audit.size();++i){if(i)out<<',';writeBoxes(out,s.audit[i].inkRows);}
    out<<"],\"parts\":[";
    for(size_t i=0;i<s.audit.size();i++){auto&a=s.audit[i];if(i)out<<',';out<<"{\"id\":"<<a.id<<",\"expected\":"<<a.expected<<",\"matched\":"<<a.matched<<",\"targetRubyHeight\":"<<a.targetRubyHeight<<",\"body\":";writeBoxes(out,a.body);out<<",\"unmapped\":[";for(size_t j=0;j<a.unmapped.size();j++){if(j)out<<',';out<<"{\"offset\":"<<a.unmapped[j].first<<",\"codepoint\":"<<a.unmapped[j].second<<'}';}out<<"],\"japaneseLines\":[";for(size_t j=0;j<a.japaneseLines.size();j++){if(j)out<<',';writeJsonString(out,a.japaneseLines[j]);}out<<"]}";}
    out<<"],\"captions\":[";for(size_t i=0;i<s.annotation.chinese.size();i++){if(i)out<<',';auto&p=s.annotation.chinese[i];out<<'['<<p.x<<','<<p.y<<','<<p.image.w<<','<<p.image.h<<']';}out<<"],\"originalBody\":";writeBoxes(out,before);out<<",\"studyBody\":";writeBoxes(out,after);out<<",\"unmappedNativeBody\":";writeBoxes(out,s.unmappedNativeBody);out<<",\"nonRubyNodesUnchanged\":"<<(s.nonRubyNodesUnchanged?"true":"false")<<",\"rubySources\":[";for(size_t i=0;i<s.chosenSlots.size();i++){auto&slot=s.rubySlots[s.chosenSlots[i]];auto&source=s.captionSources[i];if(i)out<<',';out<<"{\"caption\":"<<i<<",\"recordId\":"<<source.recordId<<",\"recordStart\":"<<source.start<<",\"recordEnd\":"<<source.end<<",\"offset\":"<<slot.source<<",\"line\":"<<slot.line<<",\"height\":"<<slot.h<<'}';}out<<"]}";
    if(!s.annotation.nativeCaptions){auto current=out.str();if(!current.empty())current.pop_back();out.str("");out.clear();out<<current<<",\"failureStage\":\""<<s.failureStage<<"\",\"eligibleSlots\":"<<s.rubySlots.size()<<",\"requestedSources\":[";for(size_t i=0;i<s.captionSources.size();i++){if(i)out<<',';auto&source=s.captionSources[i];out<<"{\"record\":"<<source.recordId<<",\"line\":"<<source.line<<",\"start\":"<<source.start<<",\"end\":"<<source.end<<'}';}out<<"],\"slotCandidates\":[";for(size_t i=0;i<s.slotDiagnostics.size();i++){if(i)out<<',';out<<s.slotDiagnostics[i];}out<<"]}";}
    if(auditFile){auto line=out.str();fwrite(line.data(),1,line.size(),auditFile);fputc('\n',auditFile);fflush(auditFile);}
}
#ifdef MAHOYO_QA_CORPUS_REPLAY
#include "corpus_layout_probe.inc"
#endif
void discardSnapshot(void*self,bool clearPage=false){std::lock_guard<std::recursive_mutex>guard(stateMutex);snapshots.erase(self);if(clearPage)for(auto&[_,l]:layers){l.pageAnnotations.erase(std::remove_if(l.pageAnnotations.begin(),l.pageAnnotations.end(),[&](const Annotation&a){return a.owner==self;}),l.pageAnnotations.end());if(l.pageAnnotations.empty())l.mapping.valid=false;}}
int __fastcall onLayout(void* self,const wchar_t* text,uint32_t mode){
    if(!ready||!text||!readable(text,2))return nativeLayout(self,text,mode);
    auto caller=(uintptr_t)__builtin_return_address(0)-gameBase;
    if((caller!=0x836d0&&caller!=0x83b62)||(uintptr_t)read<void*>(self)!=gameBase+0x229de8)return nativeLayout(self,text,mode);
    auto key=nativeString((uint8_t*)self+0x98);auto ids=idsIn(key);if(ids.empty()){discardSnapshot(self,key.find(L'$')!=key.npos);return nativeLayout(self,text,mode);}
    layoutAttempts++;std::wstring input;bool terminated=false;for(size_t i=0;i<16384&&readable(text+i,2);i++){if(!text[i]){terminated=true;break;}input+=text[i];}if(!terminated){layoutSkipped++;discardSnapshot(self,true);return nativeLayout(self,text,mode);}
    auto clean=plain(input);Snapshot next;next.input=clean;next.annotation.owner=self;size_t cursor=0;
    for(int id:ids){auto& r=records.at(id);auto pos=clean.find(r.clean,cursor);if(pos==clean.npos)pos=normalized(clean).find(normalized(r.clean),cursor);if(pos==clean.npos)continue;next.parts.push_back({&r,pos});cursor=pos+r.clean.size();}
    if(next.parts.size()!=ids.size()){
        layoutSkipped++;discardSnapshot(self,true);
        if(!loggedLanguage){log("Native text did not match local Japanese records; study layout skipped");loggedLanguage=true;}
        for(int id:ids)next.audit.push_back({id,records.at(id).clean.size(),0,{}});auditLayout(next,"record-match-failed");
        return nativeLayout(self,text,mode);
    }
    // Spacers and identical title cards keep the original composition and timing.
    if(std::all_of(next.parts.begin(),next.parts.end(),[](const auto& part){
        const auto& record=*part.first;
        return !sentenceGlosses.count(record.id) && record.words.empty() && (record.clean==record.zh ||
            std::all_of(record.clean.begin(),record.clean.end(),whitespace));
    })){
        discardSnapshot(self,true);
        return nativeLayout(self,text,mode);
    }
    next.annotation.firstId=next.parts.front().first->id;std::vector<mh::Box> before;
    auto keepOriginalWithStudy=[&](){
        discardSnapshot(self,false);
        int result=nativeLayout(self,text,mode);
        if(!result){discardSnapshot(self,true);return result;}
        Snapshot original;
        original.input=next.input;
        original.parts=next.parts;
        original.annotation.firstId=next.annotation.firstId;
        original.annotation.owner=self;
        prepareOriginalStudy(self,original,true);
        if(deviceHooked)projectOriginalCaptions(original);
        if(auditLayouts)auditLayout(original,"original-study");
        if(!original.annotation.sentenceRows.empty()){
            std::lock_guard<std::recursive_mutex> guard(stateMutex);
            snapshots[self]=std::make_shared<Snapshot>(std::move(original));
        }else discardSnapshot(self,true);
        return result;
    };
#ifdef MAHOYO_QA_CORPUS_REPLAY
    probeCorpusLayout(self,mode);
#endif
    if(!deviceHooked && !inlineTranslation)return keepOriginalWithStudy();
    if(std::any_of(ids.begin(),ids.end(),glossOnly)||
       (deviceHooked&&std::all_of(ids.begin(),ids.end(),robinEpisode)))return keepOriginalWithStudy();
    if(!nativeLayout(self,text,mode))return keepOriginalWithStudy();
    before=nativeBodyRows(self);
    auto originalRuby=originalRubyGeometry(self,next);
    auto wrapped=reserveSpace?reserved(input):input;
    int result=nativeLayout(self,wrapped.c_str(),mode);
    if(result){
        if(!sameBodyRows(before,nativeBodyRows(self))){
            log("Original geometry preserved; displaying a separate translation");
            return keepOriginalWithStudy();
        }
        makeSnapshot(self,next);
        if(!originalRuby.empty() && !sameBodyRows(originalRuby,next.occupiedRuby))return keepOriginalWithStudy();
        if(!installNativeCaptions(self,next,!deviceHooked)){layoutSkipped++;auditLayout(next,"caption-install-failed",before,nativeBodyRows(self));log("Caption installation incomplete; retaining on-demand translation");return keepOriginalWithStudy();}
        auto after=nativeBodyRows(self);
        if(!sameBodyRows(before,after))return keepOriginalWithStudy();
        auditLayout(next,"ok",before,after);char status[240];snprintf(status,sizeof(status),"Layout id=%d parts=%zu CN-lines=%zu words=%zu native=%d",next.annotation.firstId,next.parts.size(),next.annotation.chinese.size(),next.annotation.words.size(),next.annotation.nativeCaptions?1:0);log(status);
        auto ink=std::make_shared<std::vector<ReadingInk>>();
        for(size_t i=0;i<next.annotation.chinese.size();++i)
            ink->push_back({std::move(next.annotation.chinese[i]),next.captionSources[i].recordId});
        next.annotation.chinese.clear();
        next.annotation.readingInk=std::move(ink);
        std::lock_guard<std::recursive_mutex>guard(stateMutex);snapshots[self]=std::make_shared<Snapshot>(std::move(next));
    }else{layoutSkipped++;log("Reserved layout failed; retaining on-demand translation");return keepOriginalWithStudy();}
    return result;
}
int __fastcall onCompose(void* self){
    auto old=currentText;
    {std::lock_guard<std::recursive_mutex>guard(stateMutex);auto it=snapshots.find(self);currentText=it==snapshots.end()?nullptr:it->second;}
    int ret=nativeCompose(self);currentText=old;return ret;
}
bool bodyInkPresent(const Annotation&a,const Layer&l){
    if(!l.locked||l.pitch<l.w*4)return false;int ink=0;
    for(auto b:a.body)for(int y=std::max(0,b.t);y<std::min(l.h,b.b);y+=2)for(int x=std::max(0,b.l);x<std::min(l.w,b.r);x+=2)if(l.locked[(size_t)y*l.pitch+x*4+3]>8&&++ink>=2)return true;
    return false;
}
bool bitmapAllZero(const Layer&l){
    if(!l.locked||l.pitch<l.w*4||l.w<1||l.h<1)return false;
    for(int y=0;y<l.h;y++)for(int x=0;x<l.w;x++){uint32_t pixel;memcpy(&pixel,l.locked+(size_t)y*l.pitch+x*4,4);if(pixel)return false;}
    return true;
}
void auditInk(const Layer&l){
    if(!auditLayouts||!l.incoming||l.pageAnnotations.empty())return;
    static std::map<void*,int>last;int id=l.incoming->annotation.firstId;if(last.count(l.native)&&last[l.native]==id)return;last[l.native]=id;
    FILE*f=_wfopen((modRoot+L"\\ink-audit.jsonl").c_str(),L"ab");if(!f)return;
    fprintf(f,"{\"session\":%lu,\"frame\":%llu,\"incoming\":%d,\"previous\":[",GetCurrentProcessId(),frameNo,id);bool comma=false;
    for(auto&a:l.pageAnnotations){size_t colored=0,alpha=0;int maxAlpha=0;for(auto b:a.body)for(int y=std::max(0,b.t);y<std::min(l.h,b.b);y+=2)for(int x=std::max(0,b.l);x<std::min(l.w,b.r);x+=2){uint32_t p;memcpy(&p,l.locked+(size_t)y*l.pitch+x*4,4);colored+=bool(p);alpha+=(p>>24)>8;maxAlpha=std::max(maxAlpha,int(p>>24));}if(comma)fputc(',',f);comma=true;fprintf(f,"{\"id\":%d,\"rgbaSamples\":%zu,\"alphaSamples\":%zu,\"maxAlpha\":%d}",a.firstId,colored,alpha,maxAlpha);}
    fputs("]}\n",f);fclose(f);
}
int __fastcall onLock(void*self,uint8_t**pixels,int*pitch){
    bool targetLock=(uintptr_t)__builtin_return_address(0)==gameBase+0x81056;
    int ret=nativeLock(self,pixels,pitch);
    if(ret||!pixels||!pitch||!*pixels)return ret;
    std::lock_guard<std::recursive_mutex>guard(stateMutex);
    if((!currentText||!targetLock)&&!layers.count(self))return ret;
    auto gpu=read<IDirect3DTexture9*>(self,0xb8);D3DSURFACE_DESC desc{};
    if(!gpu||!readable(gpu,sizeof(void*))||FAILED(gpu->GetLevelDesc(0,&desc))||desc.Format!=D3DFMT_A8R8G8B8)return ret;
    int w=read<int>(self,0x84),h=read<int>(self,0x88);
    if(w<1||h<1||w>8192||h>8192||*pitch<w*4||!readable(*pixels,(size_t)*pitch*h))return ret;
    auto&l=layers[self];if(l.gpu&&l.gpu!=gpu){forgetLayerTextures(l);l.pageAnnotations.clear();l.surfaces.clear();l.composites.clear();l.mapping.valid=false;}
    l.native=self;l.gpu=gpu;l.w=w;l.h=h;l.gpuW=desc.Width;l.gpuH=desc.Height;l.format=desc.Format;l.locked=*pixels;l.pitch=*pitch;l.incoming=targetLock?currentText:nullptr;l.lastUsed=frameNo;rememberTexture(l,gpu);
    // The engine uploads the lockable system-memory texture to this GPU mirror.
    auto mirror=read<IDirect3DTexture9*>(self,0xc8);if(mirror&&readable(mirror,sizeof(void*)))rememberTexture(l,mirror);
    return ret;
}
// Consecutive records can continue on the same native body line (5022 -> 5023).
// Compare the next opening with the previous ending, including horizontal position.
bool beginsNewReadingPage(const Annotation& previous,const Annotation& next){
    if(next.firstId==previous.firstId)return false;
    if(next.firstId<previous.firstId)return true;
    const auto& last=previous.body.back();
    const auto& first=next.body.front();
    const int rowHeight=std::min(last.b-last.t,first.b-first.t);
    const int centerDelta=(first.t+first.b)-(last.t+last.b);
    if(std::abs(centerDelta)>=rowHeight)return centerDelta<0;
    // Glyph bitmaps can overlap slightly at a shared character boundary.
    return first.l<last.r-std::max(1,rowHeight/5);
}
void updateReadingPage(Layer& l,const Annotation& a){
    if(!l.pageAnnotations.empty()&&beginsNewReadingPage(l.pageAnnotations.back(),a))
        l.pageAnnotations.clear();
    auto previous=std::find_if(l.pageAnnotations.begin(),l.pageAnnotations.end(),[&](const Annotation& old){return old.firstId==a.firstId;});
    Annotation visible=a;
    visible.visibleRight.reserve(a.sentenceRows.size());
    for(size_t i=0;i<a.sentenceRows.size();++i){
        auto box=a.sentenceRows[i].first;
        int right=box.l;
        if(previous!=l.pageAnnotations.end()&&previous->readingInk==a.readingInk&&i<previous->visibleRight.size())right=previous->visibleRight[i];
        // Track the advancing native glyphs on partial text uploads.
        // A row that has finished needs no more bitmap scans.
        for(int x=std::min(l.w,box.r)-1;x>=std::max({0,box.l,right});x-=2){
            bool ink=false;
            for(int y=std::max(0,box.t);y<std::min(l.h,box.b);y+=3)
                if(l.locked[size_t(y)*l.pitch+4*x+3]>24){ink=true;break;}
            if(ink){right=std::min(box.r,x+std::max(3,(box.b-box.t)/3));break;}
        }
        visible.visibleRight.push_back(right);
    }
    if(previous!=l.pageAnnotations.end())l.pageAnnotations.erase(previous);
    l.pageAnnotations.push_back(std::move(visible));
}
int __fastcall onUnlock(void* self){
    {std::lock_guard<std::recursive_mutex>guard(stateMutex);auto it=layers.find(self);
        if(it!=layers.end()&&it->second.locked){auto&l=it->second;
            // A current-text compose restores/repaints a local region, not a
            // completed page. Its transient missing old rows cannot retire the
            // visible gray page history. Observe a full zero bitmap outside
            // that compose instead, suppressing interaction without destroying
            // history needed if native content is subsequently restored.
            auditInk(l);l.bitmapEmpty=!l.incoming&&bitmapAllZero(l);
            if(l.incoming&&!l.incoming->annotation.body.empty())
                updateReadingPage(l,l.incoming->annotation);
            if(l.pageAnnotations.empty())l.mapping.valid=false;
            l.locked=nullptr;l.incoming=nullptr;
        }
    }
    return nativeUnlock(self);
}

bool hookAt(uintptr_t rva,void*replacement,void**original,const uint8_t*sig,size_t n){
    void*address=(void*)(gameBase+rva);if(!readable(address,n)||memcmp(address,sig,n)){log("Game code signature differs: study hooks disabled");return false;}
    return MH_CreateHook(address,replacement,original)==MH_OK;
}
#include "notebook.inc"
void loadOrbPosition();
void reloadCollectedIds();
void reloadAnnotatedIds();
void initialize(){
    if(initialized)return;initialized=true;gameBase=(uintptr_t)GetModuleHandleW(nullptr);
    wchar_t path[32768];GetModuleFileNameW(moduleHandle,path,32768);std::wstring p=path;modRoot=p.substr(0,p.find_last_of(L"\\/"))+L"\\mahoyo-learning";
    auto ini=modRoot+L"\\settings.ini";includeN2=GetPrivateProfileIntW(L"study",L"include_n2",0,ini.c_str());reserveSpace=GetPrivateProfileIntW(L"study",L"reserve_ruby_space",1,ini.c_str());auditOriginalLayout=GetPrivateProfileIntW(L"study",L"audit_original_layout",0,ini.c_str());
    debugDrawCapture=GetPrivateProfileIntW(L"study",L"debug_draws",0,ini.c_str());auditLayouts=GetPrivateProfileIntW(L"study",L"audit_layout",auditOriginalLayout?1:0,ini.c_str());
    if(GetPrivateProfileIntW(L"study",L"log_enabled",0,ini.c_str()))logFile=_wfopen((modRoot+L"\\learning.log").c_str(),L"ab");log("Loading full-game study edition");
    if(mh::loadReaderFont(modRoot+L"\\local\\NotoSerifSC-Medium.ttf"))log("Loaded private Noto Serif SC Medium for story translations");
    parallelHighlight=GetPrivateProfileIntW(L"study",L"parallel_highlight",1,ini.c_str())!=0;
    recordLookups=GetPrivateProfileIntW(L"study",L"record_lookups",1,ini.c_str())!=0;
    inlineTranslation=GetPrivateProfileIntW(L"study",L"inline_translation",1,ini.c_str());
    if(auditLayouts){auditFile=_wfopen((modRoot+L"\\layout-audit.jsonl").c_str(),L"ab");if(auditFile){fprintf(auditFile,"{\"session\":%lu,\"tick\":%llu}\n",GetCurrentProcessId(),GetTickCount64());fflush(auditFile);}}
    if(!reserveSpace){reserveSpace=1;log("reserve_ruby_space=0 is deprecated; required native caption slots remain enabled");}
    if(!GetPrivateProfileIntW(L"study",L"enabled",1,ini.c_str())){disabledReason="disabled_by_setting";return;}
    if(!supportedExecutable()){disabledReason="unsupported_executable";log("Executable SHA-256 differs or could not be read; native study hooks disabled");return;}
    const uint8_t lockCall[]={0x49,0x8b,0xce,0xff,0x50,0x28};if(!readable((void*)(gameBase+0x81050),sizeof(lockCall))||memcmp((void*)(gameBase+0x81050),lockCall,sizeof(lockCall))){disabledReason="lock_call_signature";return;}
    if(!loadData()){disabledReason="invalid_local_data";return;}
    mh::loadJapaneseUiFace(ini);loadOrbPosition();refreshPersonal();reloadCollectedIds();reloadAnnotatedIds();
    if(MH_Initialize()!=MH_OK)return;
    const uint8_t a[]={0x4c,0x8b,0xdc,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x78};
    const uint8_t b[]={0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    const uint8_t c[]={0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x70};
    const uint8_t d[]={0x40,0x53,0x48,0x83,0xec,0x20,0x83,0x79,0x40,0x00};
    auto removeNativeHooks=[](){for(uintptr_t rva:{0x7f920,0x80f40,0x99c00,0x99d10}){MH_DisableHook((void*)(gameBase+rva));MH_RemoveHook((void*)(gameBase+rva));}};
    if(!hookAt(0x7f920,(void*)onLayout,(void**)&nativeLayout,a,sizeof(a))||!hookAt(0x80f40,(void*)onCompose,(void**)&nativeCompose,b,sizeof(b))||!hookAt(0x99c00,(void*)onLock,(void**)&nativeLock,c,sizeof(c))||!hookAt(0x99d10,(void*)onUnlock,(void**)&nativeUnlock,d,sizeof(d))){removeNativeHooks();disabledReason="native_hook_signature";return;}
    if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){removeNativeHooks();disabledReason="native_hook_enable";log("Native study hooks could not be enabled; game remains unchanged");return;}ready=true;disabledReason="";log("Native study hooks installed; waiting for Japanese story text");
}

int readerWindowDpi(HWND window){
    using GetWindowDpi=UINT(WINAPI*)(HWND);
    static const auto getDpi=(GetWindowDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    if(getDpi){const UINT dpi=getDpi(window);if(dpi)return int(dpi);}
    HDC dc=GetDC(window);
    if(!dc)return 96;
    const int dpi=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(window,dc);
    return dpi>0?dpi:96;
}

void guardReaderReturn(HWND owner);
#include "collection.inc"
#include "recent_lookups.inc"
#include "dx9_fragment.inc"
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){moduleHandle=h;DisableThreadLibraryCalls(h);}return TRUE;}

// Read-only diagnostics for this sample's native integration tests.
extern "C" __declspec(dllexport) const char* WINAPI MahoyoStudyDebugJson(){
    StudyFpScope fp;
    static thread_local std::string result;
    std::lock_guard<std::recursive_mutex>guard(stateMutex);std::ostringstream out;
    out<<"{\"ready\":"<<(ready?"true":"false")<<",\"disabledReason\":\""<<disabledReason<<"\",\"debugDrawsEnabled\":"<<(debugDrawCapture?"true":"false")<<",\"frame\":"<<frameNo<<",\"dualMode\":"<<(inlineTranslation?"true":"false")<<",\"revealedId\":"<<debugRevealed<<",\"snapshots\":"<<snapshots.size()<<",\"overlayCalls\":"<<debugOverlayCalls<<",\"hoverStart\":"<<debugHoverStart<<",\"quadHR\":"<<debugQuadHR<<",\"beginHR\":"<<debugBeginHR<<",\"popupW\":"<<tooltipW<<",\"popupH\":"<<tooltipH<<",\"coverage\":{\"attempts\":"<<layoutAttempts<<",\"skipped\":"<<layoutSkipped<<",\"originalLayoutAudit\":"<<(auditOriginalLayout?"true":"false")<<",\"seen\":[";
    bool comma=false;for(int id:layoutSeen){if(comma)out<<',';comma=true;out<<id;}out<<"],\"native\":[";comma=false;for(int id:nativeSeen){if(comma)out<<',';comma=true;out<<id;}out<<"],\"incomplete\":[";comma=false;for(int id:layoutIncomplete){if(comma)out<<',';comma=true;out<<id;}out<<"]},\"layers\":[";comma=false;
    for(auto&[p,l]:layers){if(comma)out<<',';comma=true;out<<"{\"native\":\""<<p<<"\",\"w\":"<<l.w<<",\"h\":"<<l.h<<",\"mapped\":"<<(l.mapping.valid?"true":"false")<<",\"drawn\":"<<l.drawn<<",\"drawnAlpha\":"<<l.drawnAlpha<<",\"occludedFrame\":"<<l.occludedFrame<<",\"retainedTextures\":"<<l.ownedTextures.size()<<",\"surfaces\":"<<l.surfaces.size()<<",\"composites\":"<<l.composites.size()<<",\"matrix\":["<<l.mapping.a<<','<<l.mapping.b<<','<<l.mapping.c<<','<<l.mapping.d<<','<<l.mapping.e<<','<<l.mapping.f<<"],\"records\":[";bool ca=false;D3DSURFACE_DESC desc{};if(l.mapping.valid&&l.drawn+1>=frameNo){desc.Width=l.gpuW;desc.Height=l.gpuH;}
        for(auto&a:l.pageAnnotations){if(ca)out<<',';ca=true;out<<"{\"id\":"<<a.firstId<<",\"captions\":"<<(a.readingInk?a.readingInk->size():a.chinese.size())<<",\"native\":"<<(a.nativeCaptions?"true":"false")<<",\"words\":[";bool cw=false;
            for(auto&w:a.words){if(cw)out<<',';cw=true;auto b=w.box;if(desc.Width&&desc.Height)b=mapBox(l.mapping,b,desc.Width,desc.Height);out<<"{\"start\":"<<w.word.start<<",\"end\":"<<w.word.end<<",\"box\":["<<b.l<<','<<b.t<<','<<b.r<<','<<b.b<<"]}";}
            out<<"]}";
        }out<<"]}";
    }out<<"],\"drawFrame\":"<<debugCompleteFrame<<",\"drawsTruncated\":"<<(debugCompleteTruncated?"true":"false")<<",\"draws\":[";for(size_t i=0;i<debugCompleteDraws.size();i++){if(i)out<<',';out<<debugCompleteDraws[i];}out<<"],\"vertexBuffers\":[";comma=false;for(auto&[vb,shadow]:vertexBuffers){if(comma)out<<',';comma=true;D3DVERTEXBUFFER_DESC d{};HRESULT hr=vb->GetDesc(&d);out<<"{\"native\":\""<<vb<<"\",\"bytes\":"<<shadow.bytes.size()<<",\"pool\":"<<(SUCCEEDED(hr)?int(d.Pool):-1)<<'}';}out<<"],\"bitmapVisibility\":[";comma=false;for(auto&[p,l]:layers){if(comma)out<<',';comma=true;out<<"{\"native\":\""<<p<<"\",\"empty\":"<<(l.bitmapEmpty?"true":"false")<<'}';}out<<"]}";result=out.str();return result.c_str();
}

static FARPROC systemD3D(const char* name){
    if(!realD3D){wchar_t p[MAX_PATH];GetSystemDirectoryW(p,MAX_PATH);std::wstring s=p;s+=L"\\d3d9.dll";realD3D=LoadLibraryW(s.c_str());}
    return realD3D?GetProcAddress(realD3D,name):nullptr;
}
extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT version){
    auto f=(IDirect3D9*(WINAPI*)(UINT))systemD3D("Direct3DCreate9");if(!f)return nullptr;auto d=f(version);initialize();if(d&&ready)installCreateDevice(d);return d;
}
extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT version,IDirect3D9Ex**out){auto f=(HRESULT(WINAPI*)(UINT,IDirect3D9Ex**))systemD3D("Direct3DCreate9Ex");return f?f(version,out):E_FAIL;}
extern "C" __declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR c,LPCWSTR s){auto f=(int(WINAPI*)(D3DCOLOR,LPCWSTR))systemD3D("D3DPERF_BeginEvent");return f?f(c,s):0;}
extern "C" __declspec(dllexport) int WINAPI D3DPERF_EndEvent(){auto f=(int(WINAPI*)())systemD3D("D3DPERF_EndEvent");return f?f():0;}
extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR c,LPCWSTR s){auto f=(void(WINAPI*)(D3DCOLOR,LPCWSTR))systemD3D("D3DPERF_SetMarker");if(f)f(c,s);}
extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR c,LPCWSTR s){auto f=(void(WINAPI*)(D3DCOLOR,LPCWSTR))systemD3D("D3DPERF_SetRegion");if(f)f(c,s);}
extern "C" __declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame(){auto f=(BOOL(WINAPI*)())systemD3D("D3DPERF_QueryRepeatFrame");return f?f():FALSE;}
extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD o){auto f=(void(WINAPI*)(DWORD))systemD3D("D3DPERF_SetOptions");if(f)f(o);}
extern "C" __declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus(){auto f=(DWORD(WINAPI*)())systemD3D("D3DPERF_GetStatus");return f?f():0;}
