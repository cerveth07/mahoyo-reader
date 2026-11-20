// Exercise production compositing and hit testing against a real off-screen D3D9 target.
bool readingFrameRegression(){
    LookupDwell recent;
    auto dwellAt=[&](int id,int page,uint64_t now){return recent.update(id,{2,4},page,0,now);};
    if(dwellAt(4,1,0)||dwellAt(4,1,799)||!dwellAt(4,1,800)||dwellAt(4,1,2000)||dwellAt(-1,1,2001)||
       dwellAt(4,1,2010)||dwellAt(4,2,2809)||!dwellAt(4,2,3609))return false;
    modRoot=L"package\\mahoyo-learning";
    if(!mh::loadReaderFont(modRoot+L"\\local\\NotoSerifSC-Medium.ttf"))return false;
    if(!loadData())return false;
    if(!robinEpisode(19667)||!robinEpisode(20466)||robinEpisode(19666)||robinEpisode(20467)||
       !glossOnly(5150)||glossOnly(85)||detailedTranslation(records.at(85)).find(L"杀死")==std::wstring::npos||
       detailedTranslation(records.at(5150))!=L"我活在睡梦之中。孤独地在土中腐朽。"||
       records.at(19792).zh!=L"咦！？"||records.at(14412).zh!=L"嗯哼。”"||
       records.at(23570).zh.find(L"忍了下来")==std::wstring::npos)return false;
    // Japanese ink measured on the first episode's three-line studio balloon.
    const std::vector<mh::Box> bubble{{184,122,1002,167},{184,202,1338,248},{186,284,1192,329}};
    for(int i=0;i<3;++i){
        const auto& record=records.at(19680+i);
        auto lines=mh::projectChinese(record.zh,{bubble[i]},{record.clean},bubble,56,true);
        if(lines.size()!=1||lines.front().japaneseLine!=0)return false;
        const auto& r=lines.front().raster;
        mh::Box box{r.x,r.y,r.x+r.image.w,r.y+r.image.h};
        if(box.b!=bubble[i].t-4||box.r>bubble[i].r||
           std::any_of(bubble.begin(),bubble.end(),[&](mh::Box other){return intersects(box,other);}))return false;
    }
    auto ruby=bubble;
    ruby.push_back({180,82,1040,122});
    if(!mh::projectChinese(records.at(19680).zh,{bubble.front()},{records.at(19680).clean},ruby,56,true).empty())return false;
    const auto& familiars=records.at(19734);
    const mh::Box familiarRow{84,110,1089,156};
    const std::vector<mh::Box> marked{{84,29,890,75},familiarRow,{445,82,555,107},{817,82,927,107}};
    auto split=mh::projectChinese(familiars.zh,{familiarRow},{familiars.clean},marked,56,true);
    if(split.size()<2)return false;
    for(const auto& row:split){
        const auto& p=row.raster;
        mh::Box b{p.x,p.y,p.x+p.image.w,p.y+p.image.h};
        if(std::any_of(marked.begin(),marked.end(),[&](mh::Box other){return intersects(other,b);}))return false;
    }
    const auto& dialog=records.at(5012);
    const auto ja=dialog.clean,zh=plain(dialog.zh);
    const auto wrap=ja.find(L"前のも");
    if(wrap==ja.npos)return false;
    auto plan=mh::planChinese(zh,{{24,50,1000,90},{24,125,800,165}},{ja.substr(0,wrap),ja.substr(wrap)},25);
    auto aligned=[&](const wchar_t* word,const std::wstring& chinese){
        mh::ClauseRange range;
        return mh::clauseAt(plan.highlightAnchors,ja.find(word),range)&&zh.substr(range.zhStart,range.zhEnd-range.zhStart)==chinese;
    };
    if(!aligned(L"信じられない",zh.substr(0,zh.find(L"之前")))||!aligned(L"この",L"之前的是这样，")||
       !aligned(L"前のも",L"之前的是这样，")||!aligned(L"今のヤツもね",L"今天的这个也是一样。”"))return false;
    auto fragments=mh::makeChinese(zh,{{24,50,1000,90},{24,125,800,165}},{ja.substr(0,wrap),ja.substr(wrap)},25);
    size_t covered=0;
    for(auto& ink:fragments){
        const auto& slice=ink.slice;
        if(!slice.anchors||slice.zhStart!=covered||slice.edges.size()<2||slice.edges.front()!=0||slice.edges.back()!=1||
           !std::is_sorted(slice.edges.begin(),slice.edges.end()))return false;
        covered+=slice.edges.size()-1;
    }
    if(covered!=zh.size())return false;
    auto ambiguous=mh::planChinese(L"开始，因为要走。然后回去。",{{0,50,800,90}},{L"始まる。帰る。"},25);
    mh::ClauseRange ignored;
    if(!ambiguous.highlightAnchors.empty()||mh::clauseAt({{0,0},{4,5}},1,ignored))return false;
    ParallelFocus testFocus;
    FocusClause selected{5012,{1,6,1,8}};
    POINT still{100,100},over{101,100};
    testFocus.update(selected,still,1,0,0,true,true);testFocus.update(selected,still,1,0,999,true,true);
    if(!testFocus.fades.empty())return false;
    testFocus.update(selected,over,1,0,1000,true,true);testFocus.update(selected,over,1,0,1249,true,true);
    if(!testFocus.fades.empty())return false;
    testFocus.update(selected,over,1,0,1250,true,true);testFocus.update(selected,over,1,0,1370,true,true);
    if(testFocus.fades.size()!=1||testFocus.fades.front().value!=1)return false;
    testFocus.update({},still,1,0,1380,true,true);testFocus.update({},still,1,0,1500,true,true);
    if(!testFocus.fades.empty())return false;
    testFocus.update(selected,still,1,0,1510,true,true);testFocus.update(selected,still,1,0,2000,true,true);
    if(!testFocus.fades.empty())return false;
    HWND window=CreateWindowExW(0,L"STATIC",L"Reading frame regression",WS_POPUP,0,0,720,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return false;
    gameWindow=window;
    using CreateD3D=IDirect3D9*(WINAPI*)(UINT);
    auto create=(CreateD3D)systemD3D("Direct3DCreate9");
    auto* api=create?create(D3D_SDK_VERSION):nullptr;
    IDirect3DDevice9* device=nullptr;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferWidth=720;pp.BackBufferHeight=360;
    if(!api || FAILED(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)))return false;
    originalDrawUP=(DrawUPFn)(*(void***)device)[83];
    IDirect3DSurface9 *target=nullptr,*copy=nullptr;
    device->GetRenderTarget(0,&target);
    if(!target||FAILED(device->CreateOffscreenPlainSurface(720,360,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr)))return false;
    auto& layer=layers[window];
    layer.native=window;layer.gpuW=720;layer.gpuH=360;layer.mapping={720,0,0,0,360,0,true};layer.drawn=frameNo;
    // Geometry observed in the game's Japanese layout for story key 4930.
    const int id=4930;const auto& r=records.at(id);
    auto supplemental=std::find_if(r.words.begin(),r.words.end(),[](const Word& w){
        return w.term==L"蒼崎"&&w.reading==L"あおざき"&&w.level==L"补注";
    });
    if(supplemental==r.words.end())return false;
    mh::Box row{74,23,426,79};
    Annotation annotation;annotation.body={row};annotation.sentenceRows={{row,id}};annotation.visibleRight={row.r};annotation.firstId=id;
    auto source=std::make_shared<std::vector<ReadingInk>>();
    for(auto& ink:mh::makeChinese(r.zh,{row},{r.clean},22))source->push_back({std::move(ink),id});
    if(source->empty())return false;
    annotation.readingInk=source;
    Word word=*supplemental;word.level=L"N1";
    annotation.words.push_back({{118,23,180,79},word,id});
    layer.pageAnnotations={annotation};
    const RECT client{0,0,720,360};
    int wordBaselineInk=0;
    auto frame=[&](int x,int y,ULONGLONG tick,D3DCOLOR backdrop=0xff000000){
        device->Clear(0,nullptr,D3DCLEAR_TARGET,backdrop,1.f,0);
        if(FAILED(device->BeginScene()))return -1;
        drawReadingFrame(device,{x,y},client,true,tick);
        device->EndScene();
        if(FAILED(device->GetRenderTargetData(target,copy)))return -1;
        D3DLOCKED_RECT lock{};if(FAILED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return -1;
        int lit=0;
        for(int yy=0;yy<row.t;yy++)for(int xx=row.l;xx<row.r;xx++)
            if(((uint32_t*)((uint8_t*)lock.pBits+yy*lock.Pitch))[xx]&0x00ffffff)++lit;
        wordBaselineInk=0;
        for(int yy=row.b-6;yy<row.b+5;yy++)for(int xx=118;xx<180;xx++)
            if(((uint32_t*)((uint8_t*)lock.pBits+yy*lock.Pitch))[xx]&0x00ffffff)++wordBaselineInk;
        copy->UnlockRect();return lit;
    };
    // Screenshots 4930/4944 place a short Chinese line and author marks immediately above the body.
    const mh::Box anchor{200,140,240,164};
    const std::vector<mh::Box> crowded{{0,105,480,107},{0,173,480,175},{0,140,480,142},
                                       {0,300,480,315},{0,12,480,14}};
    auto nearby=placeTooltip(anchor,64,32,480,400,8,false,crowded);
    auto above=placeTooltip(anchor,64,32,480,400,8,true,crowded);
    auto below=placeTooltip({200,12,240,36},64,32,480,400,8,true,{});
    auto clear=[&](mh::Box b,const std::vector<mh::Box>& ink){
        return b.r>b.l&&std::none_of(ink.begin(),ink.end(),[&](mh::Box r){return intersects(b,r);});
    };
    bool ok=clear(nearby,crowded)&&clear(above,crowded)&&below.t==44;
    std::vector<mh::Box> shot{{221,123,555,174},{249,97,417,122},{435,97,492,120},{598,123,650,160}};
    for(bool compact:{false,true}){
        for(int scale:{100,150}){
            auto body=compact?mh::makeReadingBubble(L"意志",L"いし",scale):
                mh::makeTooltip(L"合意",L"ごうい",L"名・自動3",L"同意；双方意见一致",L"N1",L"⓪",420*scale/100,scale);
            mh::Box edit{},range{},save{};
            auto card=mh::withWordActions(body,compact?L"补释义":L"加入生词本",scale,edit,range,save,compact);
            auto safe=placeTooltip({250,123,346,174},card.w,card.h,1806,945,10,compact,shot);
            ok&=clear(safe,shot)&&safe.l>=0&&safe.t>=0&&safe.r<=1806&&safe.b<=945;
        }
    }
    ok&=placeTooltip(anchor,320,160,340,180,8,true,{{0,0,340,180}}).r==0;
    const auto& concerned=records.at(4948);
    auto head=std::find_if(concerned.words.begin(),concerned.words.end(),[](const Word& w){return w.term==L"気遣っ";});
    if(head==concerned.words.end())return false;
    auto te=displayedForm(concerned,*head);
    ok&=te.term==L"気遣って"&&te.reading==L"きづかって"&&te.end==10&&wordActionLabel(te)==L"加入生词本";
    auto start=GetTickCount64();
    // Sample the actual D3D sampling and blend state on an opaque two-cell caption.
    auto sample=[&](int x,int y){
        D3DLOCKED_RECT lock{};if(FAILED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return -1;
        int value=((uint32_t*)((uint8_t*)lock.pBits+y*lock.Pitch))[x]&255;
        copy->UnlockRect();return value;
    };
    auto& testRow=layer.pageAnnotations.front();
    auto simple=std::make_shared<std::vector<ReadingInk>>();
    mh::CaptionSlice slice{std::make_shared<mh::ClauseAnchors>(mh::ClauseAnchors{{0,0},{1,1},{2,2}}),0,{0,.5f,1}};
    simple->push_back({{100,5,{100,10,std::vector<uint32_t>(1000,0xffffffff)},slice},id});
    testRow.readingInk=simple;testRow.words.clear();
    testRow.personalGlyphs={{{110,25,150,75},id,0,0,1},{{150,25,190,75},id,0,1,2}};
    inlineTranslation=true;
    frame(500,210,start);frame(130,50,start+5);frame(130,50,start+375);frame(130,50,start+495);
    ok&=sample(125,10)==255&&sample(175,10)==0&&sample(128,75)>0;
    frame(172,50,start+500);frame(172,50,start+750);frame(172,50,start+870);
    ok&=sample(175,10)==255&&sample(125,10)==0;
    frame(500,210,start+900);frame(500,210,start+1020);
    ok&=sample(125,10)==255&&sample(175,10)==255&&sample(128,75)==0;
    // Replay the reported page. 5022 and 5023 share one native line; 5023 wraps.
    // A strip has white fill, opaque black outline and a half-covered black edge.
    std::vector<uint8_t> pixels(720*360*4,255);
    layer.locked=pixels.data();layer.w=720;layer.h=360;layer.pitch=720*4;
    layer.pageAnnotations.clear();
    auto addRecord=[&](int key,std::vector<mh::Box> rows){
        Annotation a;a.firstId=key;a.body=rows;
        auto ink=std::make_shared<std::vector<ReadingInk>>();
        for(auto box:rows){
            a.sentenceRows.emplace_back(box,key);
            mh::Raster strip{48,6,std::vector<uint32_t>(288,0xffffffff)};
            for(int y=0;y<strip.h;++y){
                std::fill_n(strip.pixels.begin()+y*strip.w+18,5,0xff000000u);
                std::fill_n(strip.pixels.begin()+y*strip.w+28,5,0x80000000u);
            }
            ink->push_back({{box.l,box.t-10,std::move(strip),key==5020?slice:mh::CaptionSlice{}},key});
        }
        if(key==5020)a.personalGlyphs={{{70,30,125,55},key,0,0,1},{{125,30,180,55},key,0,1,2}};
        a.readingInk=ink;updateReadingPage(layer,a);
    };
    addRecord(5020,{{70,30,180,55}});
    addRecord(5021,{{70,90,600,115}});
    addRecord(5022,{{70,145,215,170}});
    addRecord(5023,{{215,145,610,170},{70,185,160,210}});
    addRecord(5024,{{70,235,590,260},{70,275,490,300},{70,315,200,340}});
    ok&=layer.pageAnnotations.size()==5;
    auto withinOne=[](int pixel,int expected){return std::abs(pixel-expected)<=1;};
    const int resting=std::lround(255*kPreviousCaptionOpacity);
    auto allCaptions=[&](){return withinOne(sample(80,22),resting)&&withinOne(sample(80,82),resting)&&
        withinOne(sample(80,137),resting)&&withinOne(sample(225,137),resting)&&withinOne(sample(80,177),resting)&&
        sample(80,227)==255&&sample(80,267)==255&&sample(80,307)==255;};
    frame(230,155,start+1050);frame(230,155,start+1500);
    ok&=allCaptions(); // A sentence arriving under a parked mouse never engages focus.
    frame(231,155,start+1510);frame(231,155,start+1759);ok&=allCaptions();
    frame(231,155,start+1760);frame(231,155,start+1880);
    ok&=sample(225,137)==255&&sample(80,177)==255&&
        sample(80,137)==0&&sample(80,22)==0&&sample(80,227)==0;
    frame(680,350,start+1890);frame(680,350,start+2010);ok&=allCaptions();
    constexpr auto white=0xffffffffu;
    frame(680,350,start+2020,white);
    ok&=withinOne(sample(90,22),255-resting)&&withinOne(sample(100,22),255-resting/2)&&sample(90,227)==0;
    frame(90,42,start+2030,white);frame(90,42,start+2280,white);
    frame(90,42,start+2340,white);
    if(readerAnimations().load())ok&=sample(90,22)>0&&sample(90,22)<255-resting&&sample(90,227)>0&&sample(90,227)<255;
    frame(90,42,start+2400,white);
    ok&=sample(90,22)==0&&sample(100,22)==255&&sample(90,227)==255;
    // The selected second half keeps the source glyph coverage and the scene's own fade.
    frame(145,42,start+2410,white);frame(145,42,start+2660,white);frame(145,42,start+2780,white);
    ok&=sample(90,22)==255&&withinOne(sample(100,22),127);
    layer.drawnAlpha=128;
    frame(145,42,start+2790,0xff4050a0);
    ok&=sample(90,22)==160&&withinOne(sample(100,22),120)&&sample(90,227)==160;
    layer.drawnAlpha=255;
    frame(680,350,start+2800,white);frame(680,350,start+2920,white);
    ok&=withinOne(sample(90,22),255-resting)&&withinOne(sample(100,22),255-resting/2)&&sample(90,227)==0;
    // An actual new page starts back at the left margin, so none of the old rows survive.
    addRecord(5025,{{70,30,320,55}});
    ok&=layer.pageAnnotations.size()==1&&layer.pageAnnotations.front().firstId==5025;
    addRecord(5026,{{72,30,450,55}});
    ok&=layer.pageAnnotations.size()==1&&layer.pageAnnotations.front().firstId==5026;
    layer.locked=nullptr;layer.pageAnnotations={annotation};start+=3100;
    ok&=frame(500,210,start)>10 && wordBaselineInk==0;
    inlineTranslation=false;
    ok&=frame(260,51,start+100)==0&&frame(263,51,start+1099)==0;
    int firstCaption=frame(263,51,start+1100);
    ok&=debugRevealed==id&&(readerAnimations().load()?firstCaption==0:firstCaption>10);
    ok&=frame(150,51,start+1200)==0&&debugHoverStart==1&&inputFrame.underPointer.action==StudyAction::SaveWord;
    ok&=std::any_of(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::EditWord;});
    ok&=!inputFrame.buttons.empty()&&inputFrame.buttons.front().target.action==StudyAction::Orb;
    layer.pageAnnotations[0].sentenceRows[0].second=4931;
    frame(263,51,start+1300);frame(263,51,start+3000);
    ok&=debugRevealed==-1;
    layer.pageAnnotations[0].sentenceRows[0].second=id;
    layer.pageAnnotations[0].words[0].word=*supplemental;
    frame(260,51,start+3100);
    frame(150,51,start+3200);
    ok&=debugHoverStart==-1&&inputFrame.underPointer.action==StudyAction::SaveWord;
    frame(150,51,start+3599);
    ok&=debugHoverStart==-1&&debugRevealed==-1;
    frame(151,51,start+3600);
    ok&=debugHoverStart==1&&debugRevealed==-1&&tooltipKey.find(L"あおざき")!=tooltipKey.npos;
    auto action=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::EditWord;});
    auto adjust=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::SelectRange;});
    auto keep=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::CardSentence;});
    if(action==inputFrame.buttons.end()||adjust==inputFrame.buttons.end()||keep==inputFrame.buttons.end())return false;
    mh::Box card{action->box.l-editInCard.l,action->box.t-editInCard.t,0,0};card.r=card.l+tooltipW;card.b=card.t+tooltipH;
    ok&=action->box.b<card.b&&keep->box.r<card.r&&!intersects(action->box,adjust->box)&&!intersects(adjust->box,keep->box)&&!intersects(card,row);
    auto bookmark=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::SaveSentence;});
    const auto testStage=orbArea(720,360);
    const int afterFeather=row.r+MulDiv(115,MulDiv(40,testStage.b-testStage.t,1071),100)+7;
    ok&=bookmark!=inputFrame.buttons.end()&&bookmark->target.id==id&&bookmark->box.l==afterFeather
        &&bookmark->box.r-bookmark->box.l==16&&invisibleHit(bookmark->target.action)&&!intersects(card,bookmark->box);
    const auto rangeButton=adjust->box;
    const auto& ink=source->front().raster;
    ok&=!intersects(card,{ink.x,ink.y,ink.x+ink.image.w,ink.y+ink.image.h});
    frame(150,51,start+4199);
    ok&=debugRevealed==-1;
    frame(150,51,start+4200);
    ok&=debugRevealed==id&&debugHoverStart==1;
    ok&=frame(150,51,start+4350)>10;
    frame((rangeButton.l+rangeButton.r)/2,(rangeButton.t+rangeButton.b)/2,start+4360);ok&=debugHoverStart==1;
    layer.pageAnnotations[0].readingInk.reset();
    layer.pageAnnotations[0].companionCaption=true;inlineTranslation=true;
    frame(500,210,start+4400);
    auto translation=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::Translation;});
    ok&=translation!=inputFrame.buttons.end()&&translation->box.l==afterFeather;
    frame(260,51,start+4500);
    auto save=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::SaveSentence;});
    if(save==inputFrame.buttons.end())ok=false;
    else {
        const StudyButton target=*save;
        const int y=(target.box.t+target.box.b)/2;
        frame(afterFeather-2,y,start+4510);
        ok&=std::any_of(inputFrame.buttons.begin(),inputFrame.buttons.end(),[&](const auto& b){return sameButton(target,b);});
        frame((target.box.l+target.box.r)/2,y,start+4520);
        ok&=inputFrame.underPointer.action==StudyAction::SaveSentence&&inputFrame.underPointer.id==id
            &&lineEndTextures.count({LineEndImage::BookmarkHover,60})&&lineEndTextures.count({LineEndImage::Hint,60});
        save=std::find_if(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::SaveSentence;});
    }
    auto scratch=std::filesystem::temp_directory_path()/(L"mahoyo-reading-window-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(start));
    if(!std::filesystem::create_directory(scratch))return false;
    modRoot=scratch.wstring();ready=true;observeStudyKeys(window);
    const auto recentPath=scratch/L"recent-lookups.tsv";
    for(const std::string header:{"mahoyo-recent-v1\n","mahoyo-recent-v1\r\n","\xef\xbb\xbfmahoyo-recent-v1\r\n"}){
        {std::ofstream fixture(recentPath,std::ios::binary);fixture<<header<<"7\t1\t2\t42\r\n";}
        ok&=mergeRecent({{8,1,3,43}});
        std::ifstream result(recentPath,std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(result)),{});
        ok&=contents=="mahoyo-recent-v1\n8\t1\t3\t43\n7\t1\t2\t42\n";
    }
    std::filesystem::remove(recentPath);
    queueRecent(id,{1,3});ok&=finishRecentWrites();
    queueRecent(id,{4,5});queueRecent(id,{1,3});ok&=finishRecentWrites();
    {
        std::ifstream recentFile(scratch/L"recent-lookups.tsv",std::ios::binary);
        std::string header,first,second,extra;
        ok&=std::getline(recentFile,header)&&header=="mahoyo-recent-v1"&&
            std::getline(recentFile,first)&&first.find(std::to_string(id)+"\t1\t3\t")==0&&
            std::getline(recentFile,second)&&second.find(std::to_string(id)+"\t4\t5\t")==0&&!std::getline(recentFile,extra);
    }
    std::filesystem::remove(scratch/L"recent-lookups.tsv");
    ok&=save!=inputFrame.buttons.end();
    if(save!=inputFrame.buttons.end()){
        ReadingButtonWindow button;button.button=*save;button.invisible=invisibleHit(save->target.action);
        if(!registerReadingButtons())return false;
        button.window=CreateWindowExW(WS_EX_TOOLWINDOW,buttonClass,L"",WS_POPUP,0,0,60,24,window,nullptr,moduleHandle,&button);
        if(!button.window)return false;
        unsigned before=inputGeneration.load();
        SendMessageW(button.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(8,9));
        SendMessageW(button.window,WM_LBUTTONUP,0,MAKELPARAM(8,9));
        DestroyWindow(button.window);
        ok&=inputGeneration==before && favorites.size()==1 && favorites.front().id==id&&collectedIds.count(id);
    }
    frame(150,51,GetTickCount64());SendMessageW(window,WM_KEYDOWN,VK_F9,0);
    ok&=favorites.size()==1&&favorites.front().words==std::vector<WordSpan>{{1,3}};
    // F10 now opens the study book's sentence page; this scratch root has no StudyBook.exe, so it only reports.
    SendMessageW(window,WM_SYSKEYDOWN,VK_F10,0);
    {std::lock_guard<std::mutex> lock(controlsMutex);ok&=!shelfWindow&&shelfNotice==L"读本未能打开，请检查安装包";}
    // The native shelf stays compiled; keep exercising it directly.
    openShelf(window);
    ok&=shelfWindow&&SendMessageW(shelfList,LB_GETCOUNT,0,0)==1;
    if(shelfFilter){
        // The hidden fixture does not get the first-show WM_SIZE from Windows.
        RECT clientArea{};GetClientRect(shelfWindow,&clientArea);
        SendMessageW(shelfWindow,WM_SIZE,SIZE_RESTORED,MAKELPARAM(clientArea.right,clientArea.bottom));
        RECT filter{};GetClientRect(shelfFilter,&filter);
        wchar_t caption[64]{};int length=GetWindowTextW(shelfFilter,caption,64);
        HDC dc=GetDC(shelfFilter);auto old=SelectObject(dc,shelfFont);SIZE ink{};
        const bool measured=length>0&&GetTextExtentPoint32W(dc,caption,length,&ink);
        const int needed=ink.cx+MulDiv(24,readerWindowDpi(shelfFilter),96);
        if(!measured||needed>filter.right){printf("Shelf filter needs %d px, available %ld px\n",needed,filter.right);ok=false;}
        SelectObject(dc,old);ReleaseDC(shelfFilter,dc);
        const int dpi=readerWindowDpi(shelfWindow);
        ok&=SendMessageW(shelfList,LB_GETITEMHEIGHT,0,0)==MulDiv(32,dpi,96)
            &&SendMessageW(shelfList,LB_GETTEXTLEN,0,0)>0;
        for(int width:{420,820}){
            SendMessageW(shelfWindow,WM_SIZE,SIZE_RESTORED,MAKELPARAM(MulDiv(width,dpi,96),MulDiv(440,dpi,96)));
            RECT list{},empty{},detail{};GetWindowRect(shelfList,&list);GetWindowRect(shelfEmpty,&empty);GetWindowRect(shelfDetails,&detail);
            ok&=EqualRect(&list,&empty)&&!IsRectEmpty(&list)&&!IsRectEmpty(&detail)&&
                (width==420?list.bottom<detail.top&&list.right==detail.right:list.right<detail.left&&list.bottom==detail.bottom);
        }
        SendMessageW(shelfWindow,WM_SIZE,SIZE_RESTORED,MAKELPARAM(clientArea.right,clientArea.bottom));
    }
    SetWindowTextW(shelfSearch,L"no hit");ok&=SendMessageW(shelfList,LB_GETCOUNT,0,0)==0;
    SetWindowTextW(shelfSearch,r.zh.c_str());ok&=SendMessageW(shelfList,LB_GETCOUNT,0,0)==1;
    SendMessageW(shelfWindow,WM_COMMAND,DeleteID,0);ok&=favorites.empty();
    SendMessageW(shelfWindow,WM_COMMAND,UndoID,0);ok&=favorites.size()==1;
    if(shelfWindow)DestroyWindow(shelfWindow);
    SendMessageW(window,WM_KEYDOWN,VK_F8,0);
    ok&=!inlineTranslation&&GetPrivateProfileIntW(L"study",L"inline_translation",1,(modRoot+L"\\settings.ini").c_str())==0;
    // Drive the real drag window procedure using client coordinates, without moving the desktop cursor.
    ReadingButtonWindow orb;orb.button.target.action=StudyAction::Orb;
    orb.orb.x=80;orb.orb.y=50;orb.orb.size=40;
    orb.window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,buttonClass,L"",WS_POPUP,80,50,40,40,window,nullptr,moduleHandle,&orb);
    if(!orb.window)return false;
    SendMessageW(orb.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(8,8));
    SendMessageW(orb.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(9,8));ok&=!orb.drag.moved;
    SendMessageW(orb.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(128,68));ok&=orb.drag.moved;
    SendMessageW(orb.window,WM_LBUTTONUP,0,MAKELPARAM(128,68));auto savedPosition=orbPosition();
    setOrbPosition({0,0},false);loadOrbPosition();auto loadedPosition=orbPosition();
    ok&=savedPosition.x>0&&savedPosition.y>0&&savedPosition.x==loadedPosition.x&&savedPosition.y==loadedPosition.y;
    ok&=!orbDocked(loadedPosition);
    for(int edge:{0,10000}){
        const auto stage=orbArea(client.right,client.bottom);
        const int left=edge==0?stage.l+8:stage.r-orb.orb.size-8;
        SendMessageW(orb.window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(8,8));
        SendMessageW(orb.window,WM_LBUTTONUP,0,MAKELPARAM(8+left-orb.orb.x,8));
        ok&=orbPosition().x==edge;
        setOrbPosition({5000,5000},false);loadOrbPosition();
        ok&=orbPosition().x==edge&&orbDocked(orbPosition());
    }
    OrbMotion opacity;
    ok&=orbOpacity(opacity.update(1.f,start,true))==.4f;
    ok&=orbOpacity(opacity.sample(start+75,true))>.4f&&orbOpacity(opacity.sample(start+150,true))==1.f;
    ok&=orbOpacity(opacity.update(0.f,start+150,false))==.4f;
    setOrbPosition(savedPosition,false);
    auto ultrawide=orbBox(3440,1440),area=orbArea(3440,1440);ok&=ultrawide.l>=area.l&&ultrawide.r<=area.r;
    DestroyWindow(orb.window);
    const RECT menuClient{0,0,1904,1071},leftOrb{0,1005,48,1053},rightOrb{1856,1005,1904,1053};
    for(int dpi:{96,144})for(bool left:{false,true}){
        const auto m=layoutOrbMenu(menuClient,menuClient,left?rightOrb:leftOrb,dpi,100);
        auto inside=[&](POINT p,int w,int h){return p.x-m.margin>=0&&p.y-m.margin>=0&&
            p.x+w+m.margin<=menuClient.right&&p.y+h+m.margin+m.px(2)<=menuClient.bottom;};
        ok&=m.left==left&&inside(m.main,m.w,m.h)&&inside(m.sub,m.subW,m.subH)&&m.visible>0&&m.visible<100;
        ok&=(left?m.main.x+m.w<rightOrb.left:m.main.x>leftOrb.right);
        ok&=(left?m.sub.x+m.subW<m.main.x:m.sub.x>m.main.x+m.w);
        ok&=m.hitMain({m.w/2-1,m.pad})==0&&m.hitMain({m.w/2,m.pad})==1&&m.hitMain({m.w/2,m.px(40)})==-1;
        ok&=m.hitMain({m.pad,m.px(45)})==-1&&m.hitMain({m.pad,m.px(50)})==2&&m.hitMain({m.pad,m.px(226)})==-1&&m.items[8].bottom<=m.h-m.pad+1;
    }
    ok&=orbMenuLabel(L"　 \t「独り言よ。」")==L"「独り言よ。」"&&orbMenuLabel(L"　 \t").empty();
    const std::wstring prefix(27,L'あ');
    ok&=orbMenuLabel(L"　"+prefix+L"\U00020000い")==prefix+L"\U00020000…";
    // Consume the exact file written by the managed production editor in its independent regression.
    std::filesystem::create_directory(scratch/L"local");
    std::filesystem::copy_file(L"package/mahoyo-learning/local/story.dat",scratch/L"local/story.dat");
    std::filesystem::copy_file(L"build/book-qa-personal.dat",scratch/L"personal-words.dat");
    {std::lock_guard<std::recursive_mutex> guard(stateMutex);collectedIds.clear();}
    {std::ofstream notes(scratch/L"sentence-notes.tsv",std::ios::binary);
     notes<<"mahoyo-notes-v1\n"<<id<<"\tfirst\\nsecond\n77\t\nbad\n88\tbad\\q\n";}
    SendMessageW(window,RegisterWindowMessageW(L"MahoyoYomibito.SentenceNotesChanged.v1"),0,0);
    ok&=annotatedIds.size()==1&&annotatedIds.count(id);
    std::filesystem::remove(scratch/L"sentence-notes.tsv");
    SendMessageW(window,personalMessage(),0,0);
    ok&=collectedIds.count(id)!=0;
    const int kanaId=1877;
    const int offset=int(records.at(kanaId).clean.find(L"ふと"));
    auto& current=layer.pageAnnotations[0];current.sentenceRows={{row,kanaId}};current.words.clear();current.readingInk.reset();
    current.personalGlyphs={{{118,23,149,79},kanaId,0,offset,offset+1},{{149,23,180,79},kanaId,0,offset+1,offset+2}};
    frame(150,51,start+5700);
    ok&=offset>=0&&debugHoverStart==offset&&tooltipKey.find(L"不经意间")!=tooltipKey.npos&&personalHits.count(3293)>0;
    toggleWordPicking();frame(151,51,start+5720);
    auto picked=glyphAt(inputFrame,kanaId,151,51);
    ok&=picked.action==StudyAction::SelectRange&&picked.span.first==offset+1&&debugHoverStart==-1;
    ok&=std::any_of(inputFrame.buttons.begin(),inputFrame.buttons.end(),[](const auto& b){return b.target.action==StudyAction::PickLine;});
    ok&=glyphAt(inputFrame,kanaId,310,51).action==StudyAction::None;
    SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);ok&=!choosingWord;
    std::filesystem::remove(scratch/L"personal-words.dat");std::filesystem::remove(scratch/L"local/story.dat");std::filesystem::remove(scratch/L"local");refreshPersonal();
    std::filesystem::remove(scratch/L"favorites.tsv");std::filesystem::remove(scratch/L"settings.ini");reloadCollectedIds();std::filesystem::remove(scratch);
    shelfNotice.clear();noticeStart=noticeUntil=0;
    layer.bitmapEmpty=true;frame(260,51,start+6000);ok&=inputFrame.buttons.empty();
    clearReadingTextures();releaseTooltip();layers.clear();copy->Release();target->Release();device->Release();api->Release();DestroyWindow(window);
    puts(ok?"D3D9 word and caption dwell, personal kana hover from managed storage, orb drag/persistence and menu layout, F8/F9/F10 and cleanup passed.":"Reading frame test FAILED");
    return ok;
}
