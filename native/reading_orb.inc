struct OrbPosition {int x=10000,y=8600;};
bool orbDocked(OrbPosition p){return p.x==0||p.x==10000;}
std::atomic<uint32_t> orbPacked{(10000u<<16)|8600u};
OrbPosition orbPosition(){const auto p=orbPacked.load();return {int(p>>16),int(p&65535)};}
void setOrbPosition(OrbPosition p,bool persist){
    p.x=std::clamp(p.x,0,10000);p.y=std::clamp(p.y,0,10000);orbPacked=(uint32_t(p.x)<<16)|uint32_t(p.y);
    if(persist){
        auto value=std::to_wstring(p.x)+L","+std::to_wstring(p.y);
        if(!WritePrivateProfileStringW(L"study",L"orb_position",value.c_str(),(modRoot+L"\\settings.ini").c_str()))notice(L"位置已调整，本次未能写入设置");
    }
}
void loadOrbPosition(){
    wchar_t value[40]{};GetPrivateProfileStringW(L"study",L"orb_position",L"10000,8600",value,40,(modRoot+L"\\settings.ini").c_str());
    int x=10000,y=8600;wchar_t extra=0;if(swscanf(value,L"%d,%d%lc",&x,&y,&extra)==2)setOrbPosition({x,y},false);
}
// Fit the virtual 16:9 stage and keep the full target inside it after resolution changes.
mh::Box orbArea(int w,int h){int sw=std::min(w,int((int64_t)h*16/9)),sh=std::min(h,int((int64_t)w*9/16));return {(w-sw)/2,(h-sh)/2,(w+sw)/2,(h+sh)/2};}
mh::Box orbBox(int w,int h){
    auto a=orbArea(w,h);int size=std::clamp((a.b-a.t)*48/900,32,112);
    size=std::min({size,a.r-a.l,a.b-a.t});const auto p=orbPosition();
    int x=a.l+MulDiv(std::max(0,a.r-a.l-size),p.x,10000),y=a.t+MulDiv(std::max(0,a.b-a.t-size),p.y,10000);
    return {x,y,x+size,y+size};
}
struct OrbDrag {POINT pointer{},origin{};int diameter=0;bool moved=false;};
void moveOrb(OrbDrag& drag,POINT cursor,bool finish){
    int dx=cursor.x-drag.pointer.x,dy=cursor.y-drag.pointer.y;
    if(!drag.moved&&std::abs(dx)<std::max(4,GetSystemMetrics(SM_CXDRAG))&&std::abs(dy)<std::max(4,GetSystemMetrics(SM_CYDRAG)))return;
    drag.moved=true;
    RECT client{};GetClientRect(gameWindow,&client);POINT zero{};ClientToScreen(gameWindow,&zero);
    auto area=orbArea(client.right,client.bottom);
    const int size=drag.diameter,rangeX=std::max(1,area.r-area.l-size),rangeY=std::max(1,area.b-area.t-size);
    int x=std::clamp(int(drag.origin.x+dx-zero.x-area.l),0,rangeX),y=std::clamp(int(drag.origin.y+dy-zero.y-area.t),0,rangeY);
    int savedX=std::clamp(MulDiv(x,10000,rangeX),1,9999);
    if(finish){
        const int left4=4*x+2*size,right4=4*(area.r-area.l)-left4;
        if(std::min(left4,right4)<=3*size)savedX=left4<=right4?0:10000;
    }
    setOrbPosition({savedX,MulDiv(y,10000,rangeY)},finish);
}
struct OrbDib {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    uint32_t* pixels{};
    OrbDib(int w,int h){
        if(!dc)return;
        BITMAPINFO info{};auto& header=info.bmiHeader;
        header.biSize=sizeof(header);header.biWidth=w;header.biHeight=-h;header.biPlanes=1;header.biBitCount=32;header.biCompression=BI_RGB;
        void* bits=nullptr;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(bitmap){previous=SelectObject(dc,bitmap);if(previous&&previous!=HGDI_ERROR)pixels=static_cast<uint32_t*>(bits);}
    }
    ~OrbDib(){if(pixels)SelectObject(dc,previous);if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);}
};
bool presentOrbLayer(HWND window,const OrbDib& image,POINT dest,SIZE dimensions,BYTE alpha){
    POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,alpha,AC_SRC_ALPHA};
    return UpdateLayeredWindow(window,nullptr,&dest,&dimensions,image.dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
}
bool makeOrbGlyph(int size,std::vector<uint8_t>& glyph){
    constexpr int samples=4;
    const int stride=size*samples;
    OrbDib mask(stride,stride);if(!mask.pixels)return false;
    HPEN outline=CreatePen(PS_SOLID,std::max(1,MulDiv(stride,3,100)),reader_palette::GlyphMask);
    if(!outline)return false;
    auto oldPen=SelectObject(mask.dc,outline);
    if(!oldPen||oldPen==HGDI_ERROR){DeleteObject(outline);return false;}
    PatBlt(mask.dc,0,0,stride,stride,BLACKNESS);
    auto point=[&](int x,int y)->POINT{return {MulDiv(x,stride,100),MulDiv(y,stride,100)};};
    POINT left[]={point(50,40),point(43,34),point(33,32),point(24,33),point(24,41),point(24,52),point(24,61),point(34,60),point(43,64),point(50,69)};
    POINT right[]={point(50,40),point(57,34),point(67,32),point(76,33),point(76,41),point(76,52),point(76,61),point(66,60),point(57,64),point(50,69)};
    POINT spine[]={point(50,40),point(50,68)},ribbon[]={point(63,34),point(63,50),point(67,46),point(70,49),point(70,34)};
    PolyBezier(mask.dc,left,10);PolyBezier(mask.dc,right,10);Polyline(mask.dc,spine,2);Polyline(mask.dc,ribbon,5);
    GdiFlush();SelectObject(mask.dc,oldPen);DeleteObject(outline);
    glyph.resize(size_t(size)*size);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        unsigned sum=0;
        for(int sy=0;sy<samples;++sy)for(int sx=0;sx<samples;++sx)sum+=mask.pixels[(y*samples+sy)*stride+x*samples+sx]&255;
        glyph[y*size+x]=uint8_t((sum+8)/16);
    }
    return true;
}
constexpr float orbOpacity(float proximity){return .4f+.6f*proximity;}
struct OrbMotion{
    float from=0,target=0;
    ULONGLONG since=0;
    float sample(ULONGLONG now,bool animate)const{return from+(target-from)*motionProgress(now,since,animate?150:0);}
    float update(float next,ULONGLONG now,bool animate){
        if(target!=next){from=sample(now,animate);target=next;since=now;}
        return sample(now,animate);
    }
};
struct OrbVisual{
    OrbMotion proximity,hot,slide;
    RECT bounds{};
    int x=0,y=0,size=0,glyphSize=0,alpha=-1,ring=-1,idleRing=-1;
    UINT interval=0;
    bool menu=false;
    std::vector<uint8_t> glyph;
};
bool orbContains(const RECT& bounds,int x,int y,int size,POINT p){
    if(!PtInRect(&bounds,p))return false;
    const double r=size*.5;
    return std::hypot(p.x+.5-x-r,p.y+.5-y-r)<=r;
}
bool updateOrbLayer(HWND window,OrbVisual& orb,RECT bounds,int x,int y,int size,int alpha,int ring,int idleRing,
                    double scale,double shadow){
    if(EqualRect(&bounds,&orb.bounds)&&orb.x==x&&orb.y==y&&orb.size==size&&orb.alpha==alpha&&orb.ring==ring&&orb.idleRing==idleRing)return true;
    if(orb.glyphSize!=size){if(!makeOrbGlyph(size,orb.glyph))return false;orb.glyphSize=size;}
    const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    OrbDib image(w,h);if(!image.pixels)return false;
    const double radius=size*.5,hot=ring/255.0;
    for(int py=0;py<h;++py)for(int px=0;px<w;++px){
        const int gx=bounds.left+px-x,gy=bounds.top+py-y;
        const double dx=gx+.5-radius,dy=gy+.5-radius;
        const double distance=std::hypot(dx,dy),coverage=std::clamp(radius+.5-distance,0.0,1.0);
        double a=0,r=0,g=0,b=0;
        auto over=[&](COLORREF color,double opacity){
            const double keep=1-opacity;
            a=opacity+a*keep;r=GetRValue(color)*opacity+r*keep;g=GetGValue(color)*opacity+g*keep;b=GetBValue(color)*opacity+b*keep;
        };
        double falloff=std::clamp((radius+shadow-std::hypot(dx,dy-2*scale))/shadow,0.0,1.0);
        over(reader_palette::Shadow,.35*falloff*falloff);
        over(reader_palette::Accent,.14*hot*(std::clamp(radius+4*scale+.5-distance,0.0,1.0)-coverage));
        over(reader_palette::Ink,.92*coverage);
        double light=std::clamp(1-std::hypot(dx,dy+radius*.65)/radius,0.0,1.0);
        over(reader_palette::Raised,.55*light*coverage);
        double inset=std::clamp(radius-(1+.5*hot)*scale+.5-distance,0.0,1.0);
        over(reader_palette::Accent,(coverage-inset)*(hot+(1-hot)*idleRing/255.0));
        if(gx>=0&&gx<size&&gy>=0&&gy<size)over(reader_palette::OrbIcon,orb.glyph[gy*size+gx]/255.0);
        image.pixels[py*w+px]=(uint32_t(std::lround(a*255))<<24)|(uint32_t(std::lround(r))<<16)|(uint32_t(std::lround(g))<<8)|uint32_t(std::lround(b));
    }
    if(!presentOrbLayer(window,image,{bounds.left,bounds.top},{w,h},BYTE(alpha)))return false;
    orb.bounds=bounds;orb.x=x;orb.y=y;orb.size=size;orb.alpha=alpha;orb.ring=ring;orb.idleRing=idleRing;
    return true;
}
int menuPx(HWND h,int n){
    return MulDiv(n,readerWindowDpi(h),96);
}
constexpr int orbMenuCommands[]={1,2,8,3,4,5,7,9,6};
constexpr int orbMenuPicker=6;
constexpr UINT_PTR orbMenuTimer=3;
constexpr wchar_t orbMenuClass[]=L"MahoyoYomibito.OrbMenu";
std::atomic<HWND> activeOrbMenu{};
bool orbMenuOpensLeft(RECT orb,RECT stage){return orb.left+orb.right>=stage.left+stage.right;}
std::wstring orbMenuLabel(const std::wstring& text){
    size_t start=text.find_first_not_of(L" \t\n\r\f\v\x0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000");
    if(start==text.npos)return {};
    size_t end=start;
    for(int chars=0;chars<28&&end<text.size();++chars){
        const auto c=text[end++];
        if(c>=0xd800&&c<=0xdbff&&end<text.size()&&text[end]>=0xdc00&&text[end]<=0xdfff)++end;
    }
    return text.substr(start,end-start)+(end<text.size()?L"…":L"");
}
struct OrbMenuLayout{
    int unit=96,w=0,h=0,subW=0,subH=0,pad=0,margin=0,step=0,visible=0;
    bool left=false;
    POINT main{},sub{};
    RECT items[9]{};
    int px(int n)const{return MulDiv(n,unit,96);}
    RECT subItem(int row)const{return {pad,pad+row*step,subW-pad,pad+(row+1)*step};}
    int hitMain(POINT p)const{
        for(int i=0;i<9;++i)if(PtInRect(&items[i],p))return i;
        return -1;
    }
};
OrbMenuLayout layoutOrbMenu(RECT client,RECT stage,RECT orb,int dpi,int lines){
    OrbMenuLayout m;
    const int width=client.right-client.left,height=client.bottom-client.top;
    m.unit=std::max(1,std::min({dpi,width*96/(lines?610:332),height*96/343}));
    m.left=orbMenuOpensLeft(orb,stage);
    m.w=m.px(268);m.h=m.px(309);m.subW=m.px(300);m.pad=m.px(8);m.margin=m.px(16);m.step=std::max(1,m.px(34));
    const int gap=m.px(6),slide=m.px(2),inset=m.margin;
    const int minX=client.left+inset,maxX=client.right-inset;
    const int pair=m.w+m.subW+gap;
    int lo=minX,hi=maxX-m.w;
    if(lines&&pair<=maxX-minX){if(m.left)lo+=m.subW+gap;else hi-=m.subW+gap;}
    m.main.x=std::clamp(int(m.left?orb.left-gap-m.w:orb.right+gap),lo,hi);
    m.main.y=std::clamp(int(orb.top),int(client.top)+inset,int(client.bottom)-inset-m.h-slide);
    const int middle=m.w/2;
    m.items[0]={m.pad,m.pad,middle,m.px(40)};
    m.items[1]={middle,m.pad,m.w-m.pad,m.px(40)};
    // Distribute DPI rounding across the rows so the bottom padding stays fixed.
    for(int i=0;i<7;++i){const int y=50+i*34+(i>=5?13:0);m.items[i+2]={m.pad,m.px(y),m.w-m.pad,m.px(y+34)};}
    m.visible=std::min(lines,std::max(0,(height-2*inset-slide-2*m.pad)/m.step));
    m.subH=2*m.pad+m.visible*m.step;
    m.sub.x=std::clamp(int(m.left?m.main.x-gap-m.subW:m.main.x+m.w+gap),minX,maxX-m.subW);
    m.sub.y=std::clamp(int(m.main.y)+int(m.items[orbMenuPicker].top)-m.pad,int(client.top)+inset,int(client.bottom)-inset-m.subH-slide);
    return m;
}
struct OrbMenuPopup{
    HWND owner{},window{},submenu{};
    RECT client{};
    OrbMenuLayout layout;
    std::vector<std::pair<int,std::wstring>> lines;
    std::unique_ptr<OrbDib> mainImage,subImage;
    bool dual=false,picking=false,animate=false,open=false,closing=false,done=false,activated=false;
    int hot=-1,subHot=-1,first=0,pressed=-1,chosen=0,record=-1;
    bool mainFading=false,subFading=false;
    ULONGLONG started=0,subStarted=0;
};
void orbMenuOver(uint32_t& pixel,COLORREF color,double alpha){
    const double keep=1-alpha;
    auto channel=[&](int shift,unsigned value){return uint32_t(std::lround(value*alpha+((pixel>>shift)&255)*keep))<<shift;};
    pixel=channel(24,255)|channel(16,GetRValue(color))|channel(8,GetGValue(color))|channel(0,GetBValue(color));
}
double orbMenuDistance(double x,double y,const RECT& box,double radius){
    const double dx=std::abs(x-(box.left+box.right)*.5)-(box.right-box.left)*.5+radius;
    const double dy=std::abs(y-(box.top+box.bottom)*.5)-(box.bottom-box.top)*.5+radius;
    return std::hypot(std::max(0.,dx),std::max(0.,dy))+std::min(0.,std::max(dx,dy))-radius;
}
bool paintOrbMenu(OrbMenuPopup& menu,bool sub){
    StudyFpScope fp;
    using namespace reader_palette;
    auto& m=menu.layout;
    const int width=sub?m.subW:m.w,height=sub?m.subH:m.h,margin=m.margin;
    const int w=width+2*margin,h=height+2*margin;
    auto& image=sub?menu.subImage:menu.mainImage;
    if(!image)image=std::make_unique<OrbDib>(w,h);
    OrbDib mask(w,h);
    if(!image->pixels||!mask.pixels)return false;
    auto px=[&](int n){return m.px(n);};
    const RECT panel{0,0,width,height};
    const double radius=px(12),border=std::max(1,px(1)),blur=std::max(1,px(36));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const double sx=x-margin+.5,sy=y-margin+.5;
        const double edge=orbMenuDistance(sx,sy,panel,radius);
        const double shadowEdge=orbMenuDistance(sx,sy-px(14),panel,radius);
        const double falloff=std::clamp(1-std::max(0.,shadowEdge)/blur,0.,1.);
        const double trim=std::clamp(double(std::min({x,y,w-1-x,h-1-y}))/std::max(1,px(9)),0.,1.);
        auto& p=image->pixels[y*w+x];p=0;
        orbMenuOver(p,Shadow,.55*falloff*falloff*trim);
        const double outer=std::clamp(.5-edge,0.,1.),inner=std::clamp(.5-edge-border,0.,1.);
        if(outer>0){orbMenuOver(p,Card,.97*outer);orbMenuOver(p,Line,.85*(outer-inner));}
    }
    auto shape=[&](RECT r,int radius,COLORREF fill,double alpha,COLORREF edge=0,double edgeAlpha=0){
        for(int y=int(r.top);y<r.bottom;++y)for(int x=int(r.left);x<r.right;++x){
            const double d=orbMenuDistance(x+.5,y+.5,r,radius);
            const double outer=std::clamp(.5-d,0.,1.),inner=std::clamp(.5-d-border,0.,1.);
            auto& p=image->pixels[(y+margin)*w+x+margin];
            orbMenuOver(p,fill,alpha*outer);
            if(edgeAlpha)orbMenuOver(p,edge,edgeAlpha*(outer-inner));
        }
    };
    SetBkMode(mask.dc,TRANSPARENT);SetTextColor(mask.dc,GlyphMask);
    auto ink=[&](RECT r,COLORREF color,auto draw){
        OffsetRect(&r,margin,margin);
        PatBlt(mask.dc,r.left,r.top,r.right-r.left,r.bottom-r.top,BLACKNESS);
        draw(r);GdiFlush();
        for(int y=int(r.top);y<r.bottom;++y)for(int x=int(r.left);x<r.right;++x){
            const auto a=mask.pixels[y*w+x]&255;
            if(a)orbMenuOver(image->pixels[y*w+x],color,a/255.);
        }
    };
    auto label=[&](const std::wstring& value,RECT r,int size,COLORREF color,UINT align=DT_LEFT,const wchar_t* face=mh::uiFace()){
        if(value.empty()||r.right<=r.left)return;
        HFONT font=CreateFontW(-std::max(1,px(size)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,face);
        if(!font)return;
        auto previous=SelectObject(mask.dc,font);
        if(previous&&previous!=HGDI_ERROR){
            ink(r,color,[&](RECT box){DrawTextW(mask.dc,value.c_str(),-1,&box,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);});
            SelectObject(mask.dc,previous);
        }
        DeleteObject(font);
    };
    if(sub){
        for(int i=0;i<m.visible;++i){
            auto r=m.subItem(i);const int index=menu.first+i;const bool hot=index==menu.subHot;
            if(hot)shape(r,px(8),HoverTint,.10);
            if(index==int(menu.lines.size())-1){
                const int x=int(r.left)+px(7),y=int(r.top+r.bottom)/2;
                shape({x,y-px(2),x+px(5),y-px(2)+px(5)},px(5)/2,Accent,1);
            }
            r.left+=px(22);r.right-=px(7);
            label(menu.lines[index].second,r,14,hot?GlyphMask:Body,DT_LEFT,mh::japaneseUiFace());
        }
        return true;
    }
    shape({m.pad,m.pad,m.w-m.pad,px(40)},px(9),Field,1,Line,.70);
    for(int i=0;i<2;++i){
        auto r=m.items[i];InflateRect(&r,-px(3),-px(3));
        const bool active=menu.dual==(i==0);
        if(active)shape(r,px(7),menu.hot==i?AccentHover:Accent,1);
        else if(menu.hot==i)shape(r,px(7),HoverTint,.10);
        label(i==0?L"对照":L"原文",r,15,active?Ink:menu.hot==i?GlyphMask:Muted,DT_CENTER);
    }
    for(int y:{45,226})shape({px(16),px(y),m.w-px(16),px(y)+std::max(1,px(1))},0,Line,.75);
    const std::wstring labels[]={menu.picking?L"取消点词":L"点词取词",L"生词本",L"收藏夹",L"离线词典",L"在本页选词",L"功能说明",L"复位悬浮球"};
    const std::wstring hints[]={menu.picking?L"Esc":L"",L"",L"F10",L"",std::to_wstring(menu.lines.size())+L" 句",L"",L""};
    for(int i=0;i<7;++i){
        auto r=m.items[i+2];const bool hot=menu.hot==i+2||(i+2==orbMenuPicker&&menu.open);
        if(hot)shape(r,px(8),HoverTint,.10);
        const auto icon=hot||(i==0&&menu.picking)?Accent:Muted;
        const int top=int(r.top+r.bottom)/2-px(8);
        RECT iconBox{px(17),top,px(35),top+px(17)};
        ink(iconBox,icon,[&](RECT box){
            auto pen=SelectObject(mask.dc,GetStockObject(DC_PEN)),brush=SelectObject(mask.dc,GetStockObject(NULL_BRUSH));
            SetDCPenColor(mask.dc,GlyphMask);
            auto x=[&](int n){return box.left+px(n);};auto y=[&](int n){return box.top+px(n);};
            auto path=[&](std::initializer_list<POINT> points){
                POINT scaled[10];int n=0;
                for(auto p:points)scaled[n++]={x(p.x),y(p.y)};
                Polyline(mask.dc,scaled,n);
            };
            switch(i){
            case 0:path({{3,1},{14,10},{9,10},{10,15},{7,16},{5,11},{3,13},{3,1}});break;
            case 1:path({{8,4},{5,2},{1,2},{1,13},{5,13},{8,15},{11,13},{15,13},{15,2},{11,2}});path({{11,2},{8,4},{8,15}});break;
            case 2:path({{4,2},{12,2},{12,15},{8,11},{4,15},{4,2}});break;
            case 3:RoundRect(mask.dc,x(0),y(0),x(17),y(17),px(4),px(4));break;
            case 4:if(menu.lines.empty())break;
                if(m.left)path({{10,4},{6,8},{10,12}});else path({{6,4},{10,8},{6,12}});break;
            case 5:Ellipse(mask.dc,x(0),y(0),x(17),y(17));break;
            case 6:Ellipse(mask.dc,x(3),y(3),x(14),y(14));
                path({{8,0},{8,6}});path({{8,11},{8,17}});path({{0,8},{6,8}});path({{11,8},{17,8}});break;
            }
            SelectObject(mask.dc,brush);SelectObject(mask.dc,pen);
        });
        if(i==3||i==5)label(i==3?L"辞":L"?",iconBox,11,icon,DT_CENTER);
        label(labels[i],{px(44),r.top,m.w-px(65),r.bottom},15,hot?GlyphMask:Body);
        label(hints[i],{m.w-px(64),r.top,m.w-px(17),r.bottom},12,Dim,DT_RIGHT);
    }
    return true;
}
bool presentOrbMenu(OrbMenuPopup& menu,bool sub,ULONGLONG now){
    StudyFpScope fp;
    auto& m=menu.layout;auto& image=sub?menu.subImage:menu.mainImage;
    auto position=sub?m.sub:m.main;
    const float progress=motionProgress(now,sub?menu.subStarted:menu.started,menu.animate?120:0);
    position.x-=m.margin;position.y+=int(std::lround((1-progress)*m.px(2)))-m.margin;
    SIZE size{(sub?m.subW:m.w)+2*m.margin,(sub?m.subH:m.h)+2*m.margin};
    return presentOrbLayer(sub?menu.submenu:menu.window,*image,position,size,BYTE(std::max(1,int(std::lround(255*progress)))));
}
bool orbMenuClient(HWND owner,RECT& screen){
    RECT client{};POINT origin{};
    if(!IsWindowVisible(owner)||IsIconic(owner)||!GetClientRect(owner,&client)||!ClientToScreen(owner,&origin))return false;
    OffsetRect(&client,origin.x,origin.y);screen=client;
    return client.right-client.left>=32&&client.bottom-client.top>=32;
}
void closeOrbMenu(OrbMenuPopup& menu,int command=0){
    menu.closing=true;menu.chosen=command;
    // The timer keeps the foreground and capture until the physical release.
    if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000))menu.done=true;
}
void redrawOrbMenu(OrbMenuPopup& menu,bool sub){
    if(!paintOrbMenu(menu,sub)||!presentOrbMenu(menu,sub,GetTickCount64()))closeOrbMenu(menu);
}
void showOrbSubmenu(OrbMenuPopup& menu,bool show){
    if(show==menu.open)return;
    menu.open=show;menu.subHot=-1;
    if(show){
        menu.subStarted=GetTickCount64();menu.subFading=menu.animate;
        redrawOrbMenu(menu,true);
        if(!menu.closing)SetWindowPos(menu.submenu,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }else ShowWindow(menu.submenu,SW_HIDE);
    redrawOrbMenu(menu,false);
}
int orbMenuHit(const OrbMenuPopup& menu,POINT point){
    const auto& m=menu.layout;
    auto local=[&](bool sub){
        const auto origin=sub?m.sub:m.main;
        RECT bounds{};
        if(GetWindowRect(sub?menu.submenu:menu.window,&bounds))
            return POINT{point.x-bounds.left-m.margin,point.y-bounds.top-m.margin};
        return POINT{point.x-origin.x,point.y-origin.y};
    };
    const POINT main=local(false);
    if(menu.open){
        const POINT sub=local(true);
        for(int i=0;i<m.visible;++i){auto r=m.subItem(i);if(PtInRect(&r,sub))return 100+menu.first+i;}
        if(sub.x>=0&&sub.x<m.subW&&sub.y>=0&&sub.y<m.subH)return -2;
    }
    if(main.x>=0&&main.x<m.w&&main.y>=0&&main.y<m.h){
        int hit=m.hitMain(main);return hit<0?-2:hit;
    }
    return -1;
}
void selectOrbSubmenu(OrbMenuPopup& menu,int index){
    menu.subHot=index;
    menu.first=std::clamp(menu.first,index-menu.layout.visible+1,index);
    redrawOrbMenu(menu,true);
}
void activateOrbMenuItem(OrbMenuPopup& menu,int index){
    if(index>=100){menu.record=menu.lines[index-100].first;closeOrbMenu(menu,100);}
    else if(index==orbMenuPicker){
        if(menu.layout.visible)showOrbSubmenu(menu,true);
    }else if(index>=0)closeOrbMenu(menu,orbMenuCommands[index]);
}
LRESULT CALLBACK orbMenuProc(HWND h,UINT msg,WPARAM key,LPARAM param){
    if(msg==WM_NCCREATE)SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)param)->lpCreateParams);
    auto* state=reinterpret_cast<OrbMenuPopup*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(!state)return DefWindowProcW(h,msg,key,param);
    auto& m=*state;
    if(h==m.submenu){
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(msg==WM_CLOSE)return 0;
        return DefWindowProcW(h,msg,key,param);
    }
    switch(msg){
    case WM_ACTIVATE:
        if(LOWORD(key)==WA_INACTIVE&&m.activated){closeOrbMenu(m);return 0;}
        break;
    case WM_ACTIVATEAPP:if(!key&&m.activated)closeOrbMenu(m);break;
    case WM_CANCELMODE:case WM_CLOSE:closeOrbMenu(m);return 0;
    case WM_CAPTURECHANGED:if(m.activated)closeOrbMenu(m);return 0;
    case WM_TIMER:{
        if(key!=orbMenuTimer)break;
        RECT client{};
        if(!orbMenuClient(m.owner,client)){m.chosen=0;m.done=true;return 0;}
        if(closingWindow||!EqualRect(&client,&m.client)||GetForegroundWindow()!=h)closeOrbMenu(m);
        if(m.closing){if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000))m.done=true;return 0;}
        const auto now=GetTickCount64();
        if(m.mainFading){
            m.mainFading=now-m.started<120;
            if(!presentOrbMenu(m,false,now))closeOrbMenu(m);
        }
        if(m.open&&m.subFading){
            m.subFading=now-m.subStarted<120;
            if(!presentOrbMenu(m,true,now))closeOrbMenu(m);
        }
        return 0;
    }
    case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:case WM_XBUTTONDOWN:{
        if(m.closing)return 0;
        POINT point{short(LOWORD(param)),short(HIWORD(param))};ClientToScreen(h,&point);
        const int hit=orbMenuHit(m,point);
        if(msg==WM_MOUSEMOVE){
            if(hit>=100){if(m.subHot!=hit-100)selectOrbSubmenu(m,hit-100);}
            else if(hit>=0){
                const bool open=hit==orbMenuPicker&&m.layout.visible;
                if(m.open!=open){m.hot=hit;showOrbSubmenu(m,open);}
                else if(m.hot!=hit||m.subHot!=-1){
                    m.hot=hit;m.subHot=-1;
                    if(m.open)redrawOrbMenu(m,true);
                    redrawOrbMenu(m,false);
                }
            }else if(!m.open&&m.hot!=-1){m.hot=-1;redrawOrbMenu(m,false);}
        }else if(msg==WM_LBUTTONUP){
            if(hit>=0&&hit==m.pressed)activateOrbMenuItem(m,hit);
            m.pressed=-1;
        }else if(hit==-1)closeOrbMenu(m);
        else if(msg==WM_LBUTTONDOWN)m.pressed=hit;
        return 0;
    }
    case WM_MOUSEWHEEL:
        if(!m.closing&&m.open&&m.layout.visible<int(m.lines.size())){
            m.first=std::clamp(m.first+(short(HIWORD(key))>0?-1:1),0,int(m.lines.size())-m.layout.visible);
            m.subHot=-1;redrawOrbMenu(m,true);
        }
        return 0;
    case WM_KEYDOWN:{
        if(m.closing)return 0;
        const auto inward=m.layout.left?VK_LEFT:VK_RIGHT,outward=m.layout.left?VK_RIGHT:VK_LEFT;
        if(key==VK_ESCAPE)closeOrbMenu(m);
        else if(key==VK_UP||key==VK_DOWN){
            const int delta=key==VK_DOWN?1:-1;
            if(m.open){
                const int count=int(m.lines.size());
                selectOrbSubmenu(m,m.subHot<0?(delta>0?0:count-1):(m.subHot+delta+count)%count);
            }else{
                m.hot=m.hot<0?(delta>0?0:8):(m.hot+delta+9)%9;
                redrawOrbMenu(m,false);
            }
        }else if(key==inward&&m.hot==orbMenuPicker&&m.layout.visible){
            showOrbSubmenu(m,true);selectOrbSubmenu(m,m.first);
        }else if(key==outward&&m.open){m.hot=orbMenuPicker;showOrbSubmenu(m,false);}
        else if(key==VK_RETURN||key==VK_SPACE){
            if(m.open){activateOrbMenuItem(m,100+(m.subHot<0?m.first:m.subHot));}
            else if(m.hot==orbMenuPicker&&m.layout.visible){showOrbSubmenu(m,true);selectOrbSubmenu(m,m.first);}
            else activateOrbMenuItem(m,m.hot);
        }
        return 0;
    }
    case WM_DESTROY:
        m.chosen=0;m.done=true;
        // A synchronous owner destroy can run inside GetMessage and remove its timer.
        if(m.activated)PostThreadMessageW(GetCurrentThreadId(),WM_NULL,0,0);
        return 0;
    }
    return DefWindowProcW(h,msg,key,param);
}
void showOrbMenu(HWND h){
    if(activeOrbMenu)return;
    OrbMenuPopup menu;menu.owner=gameWindow;
    RECT orb{};if(!orbMenuClient(menu.owner,menu.client)||!GetWindowRect(h,&orb))return;
    WNDCLASSW cls{};cls.hInstance=moduleHandle;cls.lpfnWndProc=orbMenuProc;cls.lpszClassName=orbMenuClass;
    cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    static const ATOM registered=RegisterClassW(&cls);
    if(!registered)return;
    StudyFrame frame;{std::lock_guard<std::mutex> lock(controlsMutex);frame=inputFrame;}
    for(int id:frame.pageIds){auto it=records.find(id);if(it!=records.end())menu.lines.emplace_back(id,orbMenuLabel(it->second.clean));}
    const auto area=orbArea(menu.client.right-menu.client.left,menu.client.bottom-menu.client.top);
    const RECT stage{menu.client.left+area.l,menu.client.top+area.t,menu.client.left+area.r,menu.client.top+area.b};
    menu.layout=layoutOrbMenu(menu.client,stage,orb,menuPx(h,96),int(menu.lines.size()));
    menu.dual=inlineTranslation.load();menu.picking=choosingWord.load();menu.animate=readerAnimations().load();
    constexpr DWORD flags=WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST;
    menu.window=CreateWindowExW(flags,orbMenuClass,L"",WS_POPUP,0,0,0,0,menu.owner,nullptr,moduleHandle,&menu);
    if(!menu.window)return;
    if(menu.layout.visible)menu.submenu=CreateWindowExW(flags|WS_EX_NOACTIVATE,orbMenuClass,L"",WS_POPUP,0,0,0,0,
        menu.owner,nullptr,moduleHandle,&menu);
    if(!menu.submenu)menu.layout.visible=0;
    menu.started=GetTickCount64();menu.mainFading=menu.animate;
    const auto timer=SetTimer(menu.window,orbMenuTimer,16,nullptr);
    bool ownerDisabled=false;
    if(timer&&paintOrbMenu(menu,false)&&presentOrbMenu(menu,false,menu.started)){
        activeOrbMenu=menu.window;
        ShowWindow(menu.window,SW_SHOWNOACTIVATE);
        SetForegroundWindow(menu.window);
        if(GetForegroundWindow()==menu.window){
            menu.activated=true;
            ownerDisabled=IsWindowEnabled(menu.owner)!=FALSE;
            if(ownerDisabled)EnableWindow(menu.owner,FALSE);
            SetCapture(menu.window);
            if(GetCapture()!=menu.window||GetForegroundWindow()!=menu.window)closeOrbMenu(menu);
            MSG msg{};
            while(!menu.done){
                const BOOL result=GetMessageW(&msg,nullptr,0,0);
                if(result<=0){menu.chosen=0;if(result==0)PostQuitMessage(int(msg.wParam));break;}
                TranslateMessage(&msg);DispatchMessageW(&msg);
            }
        }
    }
    const int chosen=menu.chosen;const bool dual=menu.dual;
    const bool restore=GetForegroundWindow()==menu.window;
    menu.activated=false;
    if(GetCapture()==menu.window)ReleaseCapture();
    if(menu.submenu&&IsWindow(menu.submenu))DestroyWindow(menu.submenu);
    if(IsWindow(menu.window))KillTimer(menu.window,orbMenuTimer);
    if(ownerDisabled&&IsWindow(menu.owner))EnableWindow(menu.owner,TRUE);
    activeOrbMenu=nullptr;
    if(restore)guardReaderReturn(menu.owner);
    if(IsWindow(menu.window))DestroyWindow(menu.window);
    if(!IsWindowVisible(menu.owner)||IsIconic(menu.owner)||(GetAsyncKeyState(VK_LBUTTON)&0x8000))return;
    if(chosen==1||chosen==2){if(dual!=(chosen==1))runStudyAction(gameWindow,{StudyAction::Mode});}
    if(chosen==4)runStudyAction(gameWindow,{StudyAction::Shelf});
    if(chosen==3||chosen==5){if(!launchBook(gameWindow,chosen==3?L"words":L"dict"))notice(L"读本未能打开，请检查安装包");}
    if(chosen==6)setOrbPosition({},true);
    if(chosen==8){toggleWordPicking();notice(choosingWord?L"请点正文里的词 · Esc 取消":L"已退出点词");}
    if(chosen==9&&!launchBook(gameWindow,L"legend"))notice(L"功能说明未能打开");
    if(chosen==100&&!launchBook(gameWindow,L"pick",menu.record))notice(L"选词窗口未能打开");
}
