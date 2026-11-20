// An owned button takes focus as the pointer enters it. The engine also polls the
// physical mouse, so swallowing only the game's window messages would still turn a page.
bool readerGuardActive();
HWND readerGuardWindow();
struct ReadingButtonWindow{
    HWND window{};
    StudyButton button;
    int fontSize=14;
    bool pressed=false;
    OrbDrag drag;
    OrbVisual orb;
    bool invisible=false;
};
std::vector<std::unique_ptr<ReadingButtonWindow>> buttonWindows;
constexpr wchar_t buttonClass[]=L"MahoyoYomibito.ReadingButton";
std::atomic<HWND> activeReadingButton{};
POINT pointerInScreen(HWND h,LPARAM param){POINT p{short(LOWORD(param)),short(HIWORD(param))};ClientToScreen(h,&p);return p;}
UINT buttonUpdateMessage(){static const UINT msg=RegisterWindowMessageW(L"MahoyoYomibito.UpdateReadingButtons.v1");return msg;}
void leaveReadingButton(HWND window){
    if(activeReadingButton==window){
        activeReadingButton=nullptr;
        if(GetForegroundWindow()==window)SetForegroundWindow(readerGuardActive()?readerGuardWindow():gameWindow);
        InvalidateRect(window,nullptr,FALSE);
    }
    KillTimer(window,1);
}
bool ownedStudyBookForeground(HWND foreground,HWND owner){
    // StudyBook stores this game's window handle in the property, so the check holds even if WinForms resets the owner.
    if(foreground&&owner&&GetPropW(foreground,L"MahoyoYomibito.StudyBook.v1")==reinterpret_cast<HANDLE>(owner))return true;
    bool studyBook=false;
    for(HWND h=foreground;h&&h!=owner;){
        if(GetPropW(h,L"MahoyoYomibito.StudyBook.v1"))studyBook=true;
        h=GetWindow(h,GW_OWNER);
        if(h==owner)return studyBook;
    }
    return false;
}
constexpr UINT_PTR orbTimer=2;
void stopOrb(HWND window,OrbVisual& orb){
    if(orb.interval)KillTimer(window,orbTimer);
    orb.interval=0;orb.alpha=-1;orb.bounds={};orb.proximity={};orb.hot={};orb.slide={};
}
bool refreshOrb(ReadingButtonWindow& entry){
    StudyFpScope fp;
    auto h=entry.window;auto& orb=entry.orb;
    RECT client{};POINT origin{},cursor{};GetClientRect(gameWindow,&client);ClientToScreen(gameWindow,&origin);
    auto area=orbArea(client.right,client.bottom),box=orbBox(client.right,client.bottom);
    const int size=box.r-box.l;const auto position=orbPosition();
    const auto foreground=GetForegroundWindow();
    const bool active=foreground&&(foreground==gameWindow||foreground==readerGuardWindow()||foreground==activeReadingButton.load()||
        (orb.menu&&foreground==activeOrbMenu.load())||ownedStudyBookForeground(foreground,gameWindow));
    if(size<1||!IsWindowVisible(gameWindow)||IsIconic(gameWindow)||!active){
        if(!orb.menu&&entry.pressed&&GetCapture()==h)ReleaseCapture();
        leaveReadingButton(h);stopOrb(h,orb);ShowWindow(h,SW_HIDE);return false;
    }
    const bool docked=orbDocked(position),left=position.x==0;
    const double scale=(area.b-area.t)/1080.0,radius=size*.5;
    const double restX=origin.x+(docked?(left?area.l:area.r):box.l+radius),centerY=origin.y+box.t+radius;
    const bool hasCursor=GetCursorPos(&cursor)!=FALSE;
    const bool wasHover=hasCursor&&orbContains(orb.bounds,orb.x,orb.y,orb.size,cursor);
    const bool nearPointer=orb.menu||entry.pressed||wasHover||
        (hasCursor&&std::hypot(cursor.x-restX,cursor.y-centerY)<=std::max(radius+1,80*scale));
    const auto now=GetTickCount64();const bool animate=readerAnimations().load();
    const float proximity=orb.proximity.update(nearPointer?1.f:0.f,now,animate);
    float slide=orb.slide.update(docked&&nearPointer?1.f:0.f,now,animate);
    int x=origin.x+box.l,y=origin.y+box.t;
    if(docked)x=origin.x+(left?area.l:area.r)+int(std::lround(-radius+(left?radius:-radius)*slide));
    const double shadow=std::clamp(6*scale,1.0,6.0);
    const int pad=int(std::ceil(std::max(shadow+2*scale,4*scale+.5)));
    RECT bounds{std::max<LONG>(origin.x+area.l,x-pad),std::max<LONG>(origin.y+area.t,y-pad),
                std::min<LONG>(origin.x+area.r,x+size+pad),std::min<LONG>(origin.y+area.b,y+size+pad)};
    const bool hover=hasCursor&&orbContains(bounds,x,y,size,cursor);
    if(!hover&&!entry.pressed&&!orb.menu)leaveReadingButton(h);
    const float hot=orb.hot.update(hover||orb.menu?1.f:0.f,now,animate);
    const bool rendered=updateOrbLayer(h,orb,bounds,x,y,size,int(std::lround(255*orbOpacity(proximity))),
                                       int(std::lround(255*hot)),int(std::lround(255*(.30+.25*proximity))),scale,shadow);
    if(rendered&&hover&&!wasHover&&!orb.menu&&foreground==gameWindow&&IsWindowVisible(h)&&
       !GetCapture()&&WindowFromPoint(cursor)==h){
        activeReadingButton=h;SetForegroundWindow(h);
    }
    const UINT interval=proximity!=orb.proximity.target||hot!=orb.hot.target||slide!=orb.slide.target?16:50;
    if(rendered&&orb.interval!=interval&&SetTimer(h,orbTimer,interval,nullptr))orb.interval=interval;
    return rendered;
}
LRESULT CALLBACK readingButtonProc(HWND h,UINT msg,WPARAM key,LPARAM param){
    if(msg==WM_NCCREATE){
        auto create=(CREATESTRUCTW*)param;
        SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)create->lpCreateParams);
    }
    auto* entry=(ReadingButtonWindow*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(!entry)return DefWindowProcW(h,msg,key,param);
    const bool isOrb=entry->button.target.action==StudyAction::Orb;
    switch(msg){
    case WM_NCHITTEST:
        if(!isOrb)break;
        {StudyFpScope fp;return orbContains(entry->orb.bounds,entry->orb.x,entry->orb.y,entry->orb.size,{short(LOWORD(param)),short(HIWORD(param))})?HTCLIENT:HTTRANSPARENT;}
    case WM_SETCURSOR:
        if(entry->button.target.action==StudyAction::PickLine){SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32513)));return TRUE;}
        break;
    case WM_MOUSEACTIVATE:return MA_ACTIVATE;
    case WM_MOUSEMOVE:
        if(!entry->pressed&&isOrb&&ownedStudyBookForeground(GetForegroundWindow(),gameWindow))return 0;
        if(entry->pressed&&isOrb){moveOrb(entry->drag,pointerInScreen(h,param),false);if(IsWindowVisible(h))refreshOrb(*entry);return 0;}
        if(activeReadingButton!=h){
            // The game polls the physical mouse. Give the orb the same hover
            // focus as the card actions before a press can advance the story.
            activeReadingButton=h;if(IsWindowVisible(h))SetForegroundWindow(h);
            if(isOrb)refreshOrb(*entry);else SetTimer(h,1,40,nullptr);
        }
        return 0;
    case WM_TIMER:{
        if(key==orbTimer){if(isOrb&&IsWindowVisible(h))refreshOrb(*entry);else stopOrb(h,entry->orb);return 0;}
        if(key!=1||isOrb)break;
        POINT point{};GetCursorPos(&point);RECT rect{};GetWindowRect(h,&rect);
        if(!entry->pressed&&!PtInRect(&rect,point))leaveReadingButton(h);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if(isOrb){
            entry->drag={};entry->drag.pointer=pointerInScreen(h,param);
            entry->drag.origin={entry->orb.x,entry->orb.y};entry->drag.diameter=entry->orb.size;
        }
        activeReadingButton=h;if(IsWindowVisible(h))SetForegroundWindow(h);if(!isOrb)SetTimer(h,1,40,nullptr);
        entry->pressed=true;SetCapture(h);return 0;
    case WM_LBUTTONUP:{
        if(!entry->pressed)return 0;
        if(isOrb){
            moveOrb(entry->drag,pointerInScreen(h,param),true);bool dragged=entry->drag.moved;
            entry->pressed=false;ReleaseCapture();
            if(!dragged){entry->orb.menu=true;refreshOrb(*entry);showOrbMenu(h);entry->orb.menu=false;}
            if(IsWindowVisible(h))refreshOrb(*entry);
            return 0;
        }
        entry->pressed=false;ReleaseCapture();
        RECT bounds{};GetClientRect(h,&bounds);POINT point{short(LOWORD(param)),short(HIWORD(param))};
        if(PtInRect(&bounds,point)){
            auto action=entry->button.target;
            if(action.action==StudyAction::PickLine){
                if(!choosingWord)return 0;
                ClientToScreen(h,&point);ScreenToClient(gameWindow,&point);RECT client{};GetClientRect(gameWindow,&client);
                {std::lock_guard<std::mutex> lock(controlsMutex);
                 action=glyphAt(inputFrame,action.id,MulDiv(point.x,inputFrame.width,std::max(1L,client.right)),MulDiv(point.y,inputFrame.height,std::max(1L,client.bottom)));}
                if(action.action==StudyAction::None)return 0;
                choosingWord=false;++inputGeneration;
            }
            if(action.action==StudyAction::Shelf)leaveReadingButton(h);
            runStudyAction(gameWindow,action);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if(entry->pressed&&isOrb&&entry->drag.moved){POINT cursor{};if(GetCursorPos(&cursor))moveOrb(entry->drag,cursor,true);else setOrbPosition(orbPosition(),true);}
        entry->pressed=false;return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:
        if(key==VK_ESCAPE){choosingWord=false;++inputGeneration;leaveReadingButton(h);return 0;}
        if(key==VK_F8||key==VK_F9||key==VK_F10){
            if(!(param&(1L<<30))){
                auto target=key==VK_F9?entry->button.target:StudyTarget{key==VK_F8?StudyAction::Mode:StudyAction::Shelf};
                if(key==VK_F9&&target.action==StudyAction::EditWord)target.action=StudyAction::SaveWord;
                if(target.action==StudyAction::Shelf)leaveReadingButton(h);
                runStudyAction(gameWindow,target);
            }
            return 0;
        }
        break;
    case WM_KILLFOCUS:if(activeReadingButton==h)activeReadingButton=nullptr;KillTimer(h,1);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps{};auto dc=BeginPaint(h,&ps);RECT rect{};GetClientRect(h,&rect);
        if(entry->invisible){FillRect(dc,&rect,(HBRUSH)GetStockObject(BLACK_BRUSH));EndPaint(h,&ps);return 0;}
        if(isOrb){EndPaint(h,&ps);return 0;}
        SetDCBrushColor(dc,reader_palette::Ink);FillRect(dc,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
        HFONT font=CreateFontW(-entry->fontSize,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,mh::uiFace());
        auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reader_palette::Body);
        DrawTextW(dc,entry->button.text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,old);DeleteObject(font);EndPaint(h,&ps);return 0;
    }
    case WM_NCDESTROY:stopOrb(h,entry->orb);leaveReadingButton(h);SetWindowLongPtrW(h,GWLP_USERDATA,0);break;
    }
    return DefWindowProcW(h,msg,key,param);
}
// All HWND creation, placement and focus transitions happen on the game's UI thread.
bool registerReadingButtons(){
    WNDCLASSW cls{};cls.hInstance=moduleHandle;cls.lpfnWndProc=readingButtonProc;cls.lpszClassName=buttonClass;
    cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32649));
    static const ATOM registered=RegisterClassW(&cls);
    return registered!=0;
}
void syncReadingButtons(HWND owner){
    if(activeOrbMenu)return;
    StudyFrame frame;
    {std::lock_guard<std::mutex> lock(controlsMutex);frame=inputFrame;}
    RECT client{};GetClientRect(owner,&client);POINT origin{};ClientToScreen(owner,&origin);
    const bool ownerVisible=IsWindowVisible(owner)&&!IsIconic(owner);
    HWND foreground=GetForegroundWindow();
    const bool bookForeground=ownedStudyBookForeground(foreground,owner);
    const bool show=ownerVisible&&foreground&&(foreground==owner||foreground==readerGuardWindow()||foreground==activeReadingButton.load()||bookForeground)&&frame.width>0&&frame.height>0;
    std::vector<StudyButton> controls;
    if(show)for(const auto& b:frame.buttons)
        if(b.target.action!=StudyAction::None&&(!(readerGuardActive()||bookForeground)||b.target.action==StudyAction::Orb))controls.push_back(b);
    if(!registerReadingButtons())return;
    size_t index=0;HWND orbOnTop=nullptr;bool hasPickLine=false;
    for(const auto& button:controls){
        if(index==buttonWindows.size()){
            auto item=std::make_unique<ReadingButtonWindow>();
            item->window=CreateWindowExW(WS_EX_TOOLWINDOW,buttonClass,L"",WS_POPUP,0,0,1,1,owner,nullptr,moduleHandle,item.get());
            if(!item->window)break;
            buttonWindows.push_back(std::move(item));
        }
        auto& entry=*buttonWindows[index++];
        if(button.target.action==StudyAction::Orb)orbOnTop=entry.window;
        if(button.target.action==StudyAction::PickLine)hasPickLine=true;
        if(entry.pressed||entry.orb.menu)continue;
        const bool wasOrb=entry.button.target.action==StudyAction::Orb,isOrb=button.target.action==StudyAction::Orb;
        const bool invisible=invisibleHit(button.target.action);
        if(wasOrb!=isOrb||invisible!=entry.invisible){
            leaveReadingButton(entry.window);stopOrb(entry.window,entry.orb);
            auto flags=GetWindowLongPtrW(entry.window,GWL_EXSTYLE)&~WS_EX_LAYERED;
            SetWindowLongPtrW(entry.window,GWL_EXSTYLE,flags);
            if(isOrb||invisible)SetWindowLongPtrW(entry.window,GWL_EXSTYLE,flags|WS_EX_LAYERED);
            if(invisible)SetLayeredWindowAttributes(entry.window,0,1,LWA_ALPHA);
            entry.invisible=invisible;
        }
        entry.button=button;
        if(isOrb){
            if(refreshOrb(entry))SetWindowPos(entry.window,HWND_TOP,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW|(bookForeground?SWP_NOZORDER:0));
            continue;
        }
        auto b=button.box;
        int x=MulDiv(b.l,client.right,frame.width),y=MulDiv(b.t,client.bottom,frame.height);
        int w=MulDiv(b.r,client.right,frame.width)-x,h=MulDiv(b.b,client.bottom,frame.height)-y;
        entry.fontSize=std::max(11,h*59/100);
        SetWindowTextW(entry.window,button.text.c_str());
        SetWindowPos(entry.window,HWND_TOP,origin.x+x,origin.y+y,std::max(1,w),std::max(1,h),SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(entry.window,nullptr,FALSE);
    }
    for(;index<buttonWindows.size();++index){
        auto& entry=*buttonWindows[index];if(entry.pressed)continue;
        stopOrb(entry.window,entry.orb);leaveReadingButton(entry.window);ShowWindow(entry.window,SW_HIDE);
    }
    if(orbOnTop&&!bookForeground&&(hasPickLine||readerGuardActive()))SetWindowPos(orbOnTop,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}

// Keep the game out of foreground while the close handoff is protected.
// An almost transparent owned window takes client clicks under the live orb.
HWND readerGuard{};
ULONGLONG readerGuardUntil=0;
HWND readerGuardWindow(){return readerGuard;}
bool readerGuardActive(){return readerGuardUntil!=0;}
void finishReaderGuard(HWND h){
    readerGuardUntil=0;readerGuard=nullptr;
    auto owner=GetWindow(h,GW_OWNER);
    if(GetForegroundWindow()==h&&IsWindowEnabled(owner))SetForegroundWindow(owner);
    DestroyWindow(h);
    if(IsWindow(owner))syncReadingButtons(owner);
}
LRESULT CALLBACK readerGuardProc(HWND h,UINT message,WPARAM key,LPARAM info){
    switch(message){
    case WM_MOUSEACTIVATE:return MA_ACTIVATEANDEAT;
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_LBUTTONDBLCLK:return 0;
    case WM_TIMER:
        if(GetTickCount64()>=readerGuardUntil&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000))finishReaderGuard(h);
        else if(GetForegroundWindow()==GetWindow(h,GW_OWNER))SetForegroundWindow(h);
        return 0;
    case WM_NCDESTROY:if(readerGuard==h){readerGuardUntil=0;readerGuard=nullptr;}break;
    }
    return DefWindowProcW(h,message,key,info);
}
void guardReaderReturn(HWND owner){
    RECT screen{};
    if(!IsWindowEnabled(owner)||!orbMenuClient(owner,screen))return;
    WNDCLASSW cls{};cls.hInstance=moduleHandle;cls.lpfnWndProc=readerGuardProc;
    cls.lpszClassName=L"MahoyoYomibito.ReaderReturn";
    cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    static const ATOM registered=RegisterClassW(&cls);
    if(registered&&!readerGuard)readerGuard=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,cls.lpszClassName,
        L"",WS_POPUP,screen.left,screen.top,screen.right-screen.left,screen.bottom-screen.top,owner,nullptr,moduleHandle,nullptr);
    if(!readerGuard||!SetLayeredWindowAttributes(readerGuard,0,1,LWA_ALPHA)||!SetTimer(readerGuard,1,15,nullptr)){
        if(readerGuard)finishReaderGuard(readerGuard);
        SetForegroundWindow(owner);syncReadingButtons(owner);return;
    }
    readerGuardUntil=GetTickCount64()+400;
    SetWindowPos(readerGuard,HWND_TOP,screen.left,screen.top,screen.right-screen.left,screen.bottom-screen.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    SetForegroundWindow(readerGuard);
    syncReadingButtons(owner);
}
