enum class StudyAction{None,Mode,Shelf,SaveSentence,SaveWord,Translation,EditWord,Orb,SelectRange,PickLine,CardSentence};
struct StudyTarget{StudyAction action=StudyAction::None;int id=-1;WordSpan span{-1,-1};};
struct StudyButton{mh::Box box;StudyTarget target;std::wstring text;};
struct StudyFrame{
    std::vector<StudyButton> buttons;
    std::vector<int> pageIds;
    std::vector<PersonalGlyph> glyphs;
    StudyTarget underPointer;
    int width=1,height=1;
    ULONGLONG tick=0;
};
std::mutex controlsMutex;
StudyFrame inputFrame;
std::atomic<unsigned> inputGeneration{0};
std::atomic<int> exceptionOpen{-1};
std::atomic<bool> closingWindow{false};
std::atomic<bool> choosingWord{false};
void toggleWordPicking(){
    choosingWord=!choosingWord.load();
    ++inputGeneration;
}
StudyTarget glyphAt(const StudyFrame& frame,int id,int x,int y){
    for(const auto& glyph:frame.glyphs){
        const auto& b=glyph.box;
        if(glyph.id!=id||x<b.l||x>=b.r||y<b.t||y>=b.b)continue;
        int offset=glyph.start+(x-b.l)*(glyph.end-glyph.start)/std::max(1,b.r-b.l);
        return {StudyAction::SelectRange,id,{offset,offset+1}};
    }
    return {};
}
bool cardHit(StudyAction a){return a==StudyAction::SelectRange||a==StudyAction::EditWord||a==StudyAction::CardSentence;}
bool lineEndHit(StudyAction a){return a==StudyAction::SaveSentence||a==StudyAction::Translation;}
bool invisibleHit(StudyAction a){return cardHit(a)||a==StudyAction::PickLine||lineEndHit(a);}
int debugRevealed=-1;
struct HoverDwell{
    POINT lastMouse{};
    uint64_t lastPage=0;
    unsigned generation=0;
    int pending=-1;
    ULONGLONG since=0;
    bool update(POINT mouse,int id,uint64_t page,unsigned input,ULONGLONG now){
        bool moved=mouse.x!=lastMouse.x||mouse.y!=lastMouse.y;
        lastMouse=mouse;
        if(page!=lastPage||input!=generation){lastPage=page;generation=input;pending=-1;return false;}
        if(id<0){pending=-1;return false;}
        if(id!=pending){pending=moved?id:-1;since=now;}
        return pending>=0&&now-since>=1000;
    }
} dwell;
constexpr ULONGLONG supplementalHoverMs=400;
struct SupplementalDwell{
    POINT previousMouse{};
    uint64_t page=0;
    unsigned generation=0;
    int record=-1;
    WordSpan span{-1,-1};
    ULONGLONG entered=0;
    bool waitingForMove=true;

    bool update(const Word* word,int id,POINT mouse,bool active,uint64_t currentPage,unsigned input,ULONGLONG now){
        const bool moved=mouse.x!=previousMouse.x||mouse.y!=previousMouse.y;
        previousMouse=mouse;
        if(!active||page!=currentPage||generation!=input){
            page=currentPage;generation=input;record=-1;waitingForMove=true;
            return false;
        }
        if(moved)waitingForMove=false;
        if(waitingForMove||!word||word->level!=L"补注"){
            record=-1;
            return false;
        }
        WordSpan current{int(word->start),int(word->end)};
        if(record!=id||span!=current){record=id;span=current;entered=now;}
        return now-entered>=supplementalHoverMs;
    }
} supplementalDwell;
UINT buttonUpdateMessage();
bool sameButton(const StudyButton& a,const StudyButton& b){
    return a.box.l==b.box.l&&a.box.t==b.box.t&&a.box.r==b.box.r&&a.box.b==b.box.b&&a.text==b.text&&
        a.target.action==b.target.action&&a.target.id==b.target.id&&a.target.span==b.target.span;
}
void publishControls(StudyFrame frame){
    bool changed;
    {
        std::lock_guard<std::mutex> lock(controlsMutex);
        changed=frame.width!=inputFrame.width||frame.height!=inputFrame.height||
            frame.buttons.size()!=inputFrame.buttons.size()||
            !std::equal(frame.buttons.begin(),frame.buttons.end(),inputFrame.buttons.begin(),sameButton);
        inputFrame=std::move(frame);
    }
    if(changed&&gameWindow)PostMessageW(gameWindow,buttonUpdateMessage(),0,0);
}
StudyTarget pointedTarget(){
    std::lock_guard<std::mutex> lock(controlsMutex);
    return GetTickCount64()-inputFrame.tick<=400?inputFrame.underPointer:StudyTarget{};
}
bool readAnimationSetting(){
    BOOL enabled=TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0)||enabled;
}
std::atomic<bool>& readerAnimations(){static std::atomic<bool> enabled{readAnimationSetting()};return enabled;}
float motionProgress(ULONGLONG now,ULONGLONG since,ULONGLONG duration){
    if(!duration)return 1.f;
    const float t=now>=since?std::min(1.f,float(now-since)/duration):0.f;
    return 1.f-(1.f-t)*(1.f-t);
}
D3DCOLOR withOpacity(D3DCOLOR color,float opacity){
    return (color&0xffffffu)|(DWORD(std::lround((color>>24)*opacity))<<24);
}
void notice(const wchar_t* message){
    std::lock_guard<std::mutex> lock(controlsMutex);shelfNotice=message;noticeStart=GetTickCount64();noticeUntil=noticeStart+(readerAnimations().load()?2000:1600);
}
void runStudyAction(HWND window,StudyTarget target){
    switch(target.action){
        case StudyAction::SelectRange:
            if(!launchBook(window,L"pick",target.id,target.span.first,target.span.second))notice(L"选词窗口未能打开");
            break;
        case StudyAction::EditWord:
            if(!launchBook(window,L"edit",target.id,target.span.first,target.span.second))notice(L"读本未能打开，请检查安装包");
            break;
        case StudyAction::Mode:{
            bool dual=!inlineTranslation.load();inlineTranslation=dual;
            bool saved=WritePrivateProfileStringW(L"study",L"inline_translation",dual?L"1":L"0",(modRoot+L"\\settings.ini").c_str());
            notice(saved?(dual?L"对照 · F8":L"原文 · 悬停 1 秒看中文"):L"模式已切换，本次设置未能写入文件");
            ++inputGeneration;exceptionOpen=-1;break;
        }
        case StudyAction::Shelf:
            ++inputGeneration;
            if(!launchBook(window,L"sentences"))notice(L"读本未能打开，请检查安装包");
            break;
        case StudyAction::SaveWord:
        case StudyAction::CardSentence:
        case StudyAction::SaveSentence:{
            bool word=target.action==StudyAction::SaveWord;
            auto result=editFavorite(ShelfEdit::Save,target.id,word?target.span:WordSpan{-1,-1});
            if(result==ShelfResult::Recovered)notice(L"已收藏，原收藏文件已备份");
            else notice(result==ShelfResult::Ok?(word?L"已收藏词语和原句":L"已收藏整句"):shelfError(result));
            break;
        }
        case StudyAction::Translation:exceptionOpen=exceptionOpen==target.id?-1:target.id;break;
        default:break;
    }
}
struct StudyTexture{IDirect3DTexture9* texture{};int w=0,h=0;};
StudyTexture uploadStudyImage(IDirect3DDevice9* device,const mh::Raster& r){
    StudyTexture item;
    if(r.w<1||r.h<1||r.pixels.size()!=size_t(r.w)*r.h)return item;
    if(FAILED(device->CreateTexture(r.w,r.h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&item.texture,nullptr)))return {};
    D3DLOCKED_RECT lock{};
    if(FAILED(item.texture->LockRect(0,&lock,nullptr,0))){item.texture->Release();return {};}
    for(int y=0;y<r.h;++y)memcpy((uint8_t*)lock.pBits+y*lock.Pitch,r.pixels.data()+y*r.w,r.w*4);
    item.texture->UnlockRect(0);item.w=r.w;item.h=r.h;return item;
}
struct CachedInk{std::weak_ptr<const std::vector<ReadingInk>> owner;StudyTexture image;};
std::map<const ReadingInk*,CachedInk> inkTextures;
std::map<std::pair<std::wstring,int>,StudyTexture> toastTextures;
enum class LineEndImage{Bookmark,Collected,BookmarkHover,CollectedHover,Annotated,AnnotatedHover,ShowTranslation,HideTranslation,Hint};
std::map<std::pair<LineEndImage,int>,StudyTexture> lineEndTextures;
const StudyTexture& lineEndTexture(IDirect3DDevice9* d,LineEndImage state,int scale){
    auto& image=lineEndTextures[{state,scale}];
    if(!image.texture){
        mh::Raster raster;
        if(state==LineEndImage::Hint)raster=mh::makeControlLabel(L"收藏本句 · F9",12,6,scale);
        else if(state==LineEndImage::ShowTranslation||state==LineEndImage::HideTranslation)
            raster=mh::makeControlLabel(state==LineEndImage::ShowTranslation?L"查看译文":L"收起译文",13,6,scale);
        else{
            bool annotated=state==LineEndImage::Annotated||state==LineEndImage::AnnotatedHover;
            raster=mh::makeBookmark(annotated||state==LineEndImage::Collected||state==LineEndImage::CollectedHover,
                state==LineEndImage::BookmarkHover||state==LineEndImage::CollectedHover||state==LineEndImage::AnnotatedHover,scale,annotated);
        }
        image=uploadStudyImage(d,raster);
    }
    return image;
}
void clearReadingTextures(){
    for(auto& [key,entry]:inkTextures)if(entry.image.texture)entry.image.texture->Release();
    for(auto& [key,image]:toastTextures)if(image.texture)image.texture->Release();
    for(auto& [key,image]:lineEndTextures)if(image.texture)image.texture->Release();
    lineEndTextures.clear();toastTextures.clear();inkTextures.clear();
}
void trimInkTextures(){
    for(auto it=inkTextures.begin();it!=inkTextures.end();){
        if(it->second.owner.expired()){if(it->second.image.texture)it->second.image.texture->Release();it=inkTextures.erase(it);}
        else ++it;
    }
}
struct VisibleInk{
    std::shared_ptr<const std::vector<ReadingInk>> owner;
    const ReadingInk* source;
    mh::Box box;
    float restingOpacity;
    UINT alpha;
    float progress;
};
struct FocusClause {
    int id=-1;
    mh::ClauseRange range{};
    bool wholeSentence()const{return range.jaEnd==0;}
    bool operator==(const FocusClause& other)const{return id==other.id&&range==other.range;}
};
struct ClauseFade {
    FocusClause clause;
    float from=0,to=0,value=0;
    ULONGLONG start=0;
};
struct ParallelFocus {
    FocusClause pending,target;
    ULONGLONG since=0;
    uint64_t page=0;
    unsigned generation=0;
    std::vector<ClauseFade> fades;
    POINT lastMouse{};
    bool waitingForMove=true;
    void update(FocusClause pointed,POINT mouse,uint64_t currentPage,unsigned epoch,ULONGLONG now,bool animate,bool enabled){
        const bool moved=mouse.x!=lastMouse.x||mouse.y!=lastMouse.y;
        lastMouse=mouse;
        if(page!=currentPage||epoch!=generation||!enabled){
            fades.clear();pending={};target={};page=currentPage;generation=epoch;waitingForMove=true;
            return;
        }
        if(moved)waitingForMove=false;
        if(waitingForMove)return;
        // Typewriter reveals and moving hit boxes must not start a new hover by themselves.
        if(pointed.id>=0&&!(pointed==pending)&&!moved)pointed={};
        if(!(pointed==pending)){pending=pointed;since=now;}
        for(auto& f:fades)f.value=f.from+(f.to-f.from)*motionProgress(now,f.start,animate?120:0);
        FocusClause next=target;
        if(pointed.id<0)next={};
        else if(now-since>=250)next=pointed;
        else if(pointed.id!=target.id)next={};
        if(!(next==target)){
            target=next;bool found=false;
            for(auto& f:fades){
                bool active=f.clause==target;found|=active;
                f.from=f.value;f.to=active?1.f:0.f;f.start=now;if(!animate)f.value=f.to;
            }
            if(!found&&target.id>=0)fades.push_back({target,0,1,animate?0.f:1.f,now});
        }
        fades.erase(std::remove_if(fades.begin(),fades.end(),[](auto& f){return f.to==0&&f.value<=0;}),fades.end());
    }
};

void drawInk(IDirect3DDevice9* d,const VisibleInk& ink,float opacity,const ParallelFocus& focus){
    auto& entry=inkTextures[ink.source];
    if(entry.owner.expired()||!entry.image.texture){
        if(entry.image.texture)entry.image.texture->Release();
        entry.image=uploadStudyImage(d,ink.source->raster.image);entry.owner=ink.owner;
    }
    if(!entry.image.texture||ink.progress<=0)return;
    auto b=ink.box;
    const auto color=D3DCOLOR_ARGB(ink.alpha,255,255,255);
    auto draw=[&](float left,float right,float strength){
        right=std::min(right,ink.progress);
        if(strength>0&&right>left)texturedQuad(d,b.l+(b.r-b.l)*left,float(b.t),(b.r-b.l)*(right-left),float(b.b-b.t),
            withOpacity(color,opacity*strength),entry.image.texture,right,left);
    };
    if(focus.fades.empty()){draw(0,1,ink.restingOpacity);return;}
    const auto& slice=ink.source->raster.slice;
    // Disjoint texture intervals preserve the original glyph alpha even on transparent edges.
    struct HighlightPart{float left,right,weight;};
    std::vector<HighlightPart> parts;
    std::vector<float> boundaries{0,1};
    float background=1;
    for(const auto& f:focus.fades){
        background-=f.value;
        if(f.clause.id!=ink.source->recordId)continue;
        float left=0,right=1;
        if(!f.clause.wholeSentence()){
            if(slice.edges.empty())continue;
            auto boundary=[&](size_t offset){return slice.edges[offset<=slice.zhStart?0:std::min(offset-slice.zhStart,slice.edges.size()-1)];};
            left=boundary(f.clause.range.zhStart);right=boundary(f.clause.range.zhEnd);
        }
        if(left>=right)continue;
        parts.push_back({left,right,f.value});
        boundaries.push_back(left);boundaries.push_back(right);
    }
    // Unfocused ink blends into the CG, including its outline. A focused clause
    // returns to the original texture opacity even when it belongs to an older line.
    background=std::clamp(background,0.f,1.f)*ink.restingOpacity;
    if(parts.empty()){draw(0,1,background);return;}
    std::sort(boundaries.begin(),boundaries.end());
    for(size_t i=1;i<boundaries.size();++i){
        float left=boundaries[i-1],right=boundaries[i],strength=background;
        if(right<=left)continue;
        for(const auto& part:parts)if(part.left<=left&&right<=part.right)strength+=part.weight;
        draw(left,right,std::clamp(strength,0.f,1.f));
    }
}
void drawToast(IDirect3DDevice9* d,const std::wstring& message,int scale,int width,int bottom,float opacity){
    if(width<=8)return;
    auto& image=toastTextures[{message,scale}];
    if(!image.texture)image=uploadStudyImage(d,mh::makeToast(message,scale));
    if(!image.texture)return;
    float fit=std::min(1.f,float(width-8)/image.w);
    float w=image.w*fit,h=image.h*fit;
    quad(d,(width-w)*.5f,bottom-h,w,h,withOpacity(reader_palette::argb::TextureModulation,opacity),image.texture);
}
