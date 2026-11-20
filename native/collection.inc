// Favorites contain only stable story IDs and UTF-16 word spans. Player text stays in story.dat.
using WordSpan=std::pair<int,int>;
struct Favorite {int id;std::vector<WordSpan> words;};
using Favorites=std::vector<Favorite>;
enum class ShelfEdit {Save,Remove,Restore};
Favorites favorites;
std::set<int> collectedIds; // Guarded by stateMutex.
Favorite removedFavorite{-1,{}};
std::wstring shelfNotice;
ULONGLONG noticeStart=0,noticeUntil=0;

enum class ShelfResult {Ok, Recovered, ReadFailed, UnknownFormat, BackupFailed, WriteFailed};
struct ShelfRead {
    Favorites entries;
    std::string original;
    size_t skipped=0;
};

bool shelfSucceeded(ShelfResult result){
    return result==ShelfResult::Ok||result==ShelfResult::Recovered;
}
const wchar_t* shelfError(ShelfResult result){
    switch(result){
    case ShelfResult::ReadFailed:return L"无法读取 favorites.tsv，未修改收藏。";
    case ShelfResult::UnknownFormat:return L"无法识别 favorites.tsv 的格式，未修改收藏。";
    case ShelfResult::BackupFailed:return L"无法备份含坏行的收藏文件，未修改原文件。";
    default:return L"收藏文件写入失败，未修改原文件。";
    }
}

ShelfResult readFavorites(ShelfRead& result){
    result={};
    const auto path=modRoot+L"\\favorites.tsv";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)
        return GetLastError()==ERROR_FILE_NOT_FOUND?ShelfResult::Ok:ShelfResult::ReadFailed;
    LARGE_INTEGER size{};
    bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>=0&&size.QuadPart<=8*1024*1024;
    std::string data;
    if(ok){
        data.resize(size_t(size.QuadPart));
        DWORD got=0;
        ok=ReadFile(file,data.data(),DWORD(data.size()),&got,nullptr)&&got==data.size();
    }
    CloseHandle(file);
    if(!ok)return ShelfResult::ReadFailed;

    std::istringstream input(data);
    std::string line;
    if(!std::getline(input,line))return ShelfResult::UnknownFormat;
    if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!="mahoyo-favorites-v1")return ShelfResult::UnknownFormat;

    std::set<int> seen;
    while(std::getline(input,line)){
        if(line.find_first_not_of(" \t\r")==std::string::npos)continue;
        std::istringstream row(line);
        Favorite entry{};
        if(!(row>>entry.id)||!records.count(entry.id)||seen.count(entry.id)){
            ++result.skipped;
            continue;
        }
        const size_t length=records.at(entry.id).clean.size();
        bool valid=true;
        while(row>>std::ws && !row.eof()){
            int start,end;
            if(!(row>>start>>end)||start<0||end<=start||size_t(end)>length){
                valid=false;
                break;
            }
            entry.words.emplace_back(start,end);
        }
        if(!valid){
            ++result.skipped;
            continue;
        }
        seen.insert(entry.id);
        result.entries.push_back(std::move(entry));
    }
    if(result.skipped)result.original=std::move(data);
    return ShelfResult::Ok;
}

void setCollectedIds(const Favorites& list){
    collectedIds.clear();
    for(const auto& entry:list)collectedIds.insert(entry.id);
}
std::set<int> annotatedIds; // Guarded by stateMutex, like collectedIds.
void reloadAnnotatedIds(){
    auto path=modRoot+L"\\sentence-notes.tsv";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    std::set<int> ids;
    if(file==INVALID_HANDLE_VALUE){
        if(GetLastError()!=ERROR_FILE_NOT_FOUND)return;
    }else{
        // Only the presence of a note is rendered in game. The body stays in StudyBook.
        char buffer[4096];std::string line;bool first=true,valid=true;
        auto consume=[&]{
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(first){
                if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
                valid=line=="mahoyo-notes-v1";first=false;
            }else{
                auto tab=line.find('\t');
                if(tab>0&&tab!=line.npos&&tab+1<line.size()){
                    int key=0;
                    auto keyEnd=line.data()+tab;
                    auto number=std::from_chars(line.data(),keyEnd,key);
                    bool noteValid=key>=0&&number.ec==std::errc{}&&number.ptr==keyEnd;
                    for(size_t i=tab+1;noteValid&&i<line.size();++i){
                        if(line[i]=='\t')noteValid=false;
                        if(line[i]=='\\')noteValid=++i<line.size()&&(line[i]=='\\'||line[i]=='t'||line[i]=='n');
                    }
                    if(noteValid)ids.insert(key);
                }
            }
            line.clear();
        };
        DWORD count;
        while(valid){
            if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)){valid=false;break;}
            if(!count)break;
            for(DWORD i=0;i<count;++i){if(buffer[i]=='\n')consume();else line+=buffer[i];}
        }
        CloseHandle(file);
        if(valid&&!line.empty())consume();
        if(!valid||first)return;
    }
    std::lock_guard<std::recursive_mutex> guard(stateMutex);annotatedIds.swap(ids);
}
void reloadCollectedIds(){
    std::lock_guard<std::recursive_mutex> guard(stateMutex);
    ShelfRead loaded;
    if(readFavorites(loaded)==ShelfResult::Ok)setCollectedIds(loaded.entries);
}

// Backups and regular saves use the same durable write, with distinct replace rules.
bool writeShelfFile(const std::wstring& path,const std::string& bytes,bool replace){
    auto temp=path+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0));
    if(!ok)DeleteFileW(temp.c_str());
    return ok;
}
bool backupFavorites(const std::wstring& path,const std::string& bytes){
    auto backup=path+L".bak";
    if(GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES){
        // Keep every recovery source, including the original first backup.
        backup+=L"."+std::to_wstring(GetTickCount64())+L"."+std::to_wstring(GetCurrentProcessId());
    }
    return writeShelfFile(backup,bytes,false);
}
bool writeFavorites(const std::wstring& path,const Favorites& list){
    std::ostringstream out;
    out<<"mahoyo-favorites-v1\n";
    for(const auto& f:list){
        out<<f.id;
        for(auto [start,end]:f.words)out<<'\t'<<start<<'\t'<<end;
        out<<'\n';
    }
    return writeShelfFile(path,out.str(),true);
}
ShelfResult editFavorite(ShelfEdit edit,int id,WordSpan span={-1,-1}){
    if(!records.count(id))return ShelfResult::WriteFailed;
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\MahoyoYomibitoFavoritesV1");
    if(!mutex)return ShelfResult::WriteFailed;
    auto acquired=WaitForSingleObject(mutex,250);
    if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){CloseHandle(mutex);return ShelfResult::WriteFailed;}

    ShelfRead loaded;
    auto result=readFavorites(loaded);
    if(result==ShelfResult::Ok){
        auto& next=loaded.entries;
        Favorite changed{id,{}};
        auto it=std::find_if(next.begin(),next.end(),[id](const auto& item){return item.id==id;});
        if(it!=next.end()){changed=*it;next.erase(it);}
        if(edit!=ShelfEdit::Remove){
            auto add=[&](WordSpan w){if(std::find(changed.words.begin(),changed.words.end(),w)==changed.words.end())changed.words.push_back(w);};
            if(edit==ShelfEdit::Restore)for(auto w:removedFavorite.words)add(w);
            if(edit==ShelfEdit::Save&&span.first>=0)add(span);
            next.insert(next.begin(),changed);
        }
        const auto path=modRoot+L"\\favorites.tsv";
        if(loaded.skipped&&!backupFavorites(path,loaded.original))result=ShelfResult::BackupFailed;
        else if(!writeFavorites(path,next))result=ShelfResult::WriteFailed;
        else {
            favorites.swap(next);
            {
                std::lock_guard<std::recursive_mutex> guard(stateMutex);
                setCollectedIds(favorites);
            }
            if(edit==ShelfEdit::Remove)removedFavorite=std::move(changed);
            if(edit==ShelfEdit::Restore)removedFavorite={-1,{}};
            if(loaded.skipped)result=ShelfResult::Recovered;
        }
    }
    ReleaseMutex(mutex);CloseHandle(mutex);
    return result;
}
std::wstring wordSummary(const Favorite& f){
    const auto& r=records.at(f.id);
    std::wstring text;
    for(auto [start,end]:f.words){
        if(!text.empty())text+=L"\r\n";
        text+=r.clean.substr(start,end-start);
        for(const auto& source:r.words){
            if(int(source.start)!=start)continue;
            Word word=int(source.end)==end?source:displayedForm(r,source);
            if(int(word.end)!=end)continue;
            if(!word.reading.empty())text+=L"  "+word.reading;
            if(!word.definition.empty())text+=L"  "+word.definition;
        }
    }
    return text;
}
HWND shelfWindow{},shelfSearch{},shelfList{},shelfDetails{},shelfFilter{},shelfDelete{},shelfUndo{},shelfExport{},shelfBook{};
HWND shelfSearchLabel{},shelfEmpty{};
HWND shelfPausedOwner{};
HBRUSH shelfPaper{},shelfField{};HFONT shelfFont{};
enum {SearchID=201,FilterID,ListID,DeleteID,UndoID,ExportID,BookID};
ShelfResult shelfReadResult=ShelfResult::Ok;
size_t shelfSkippedRows=0;
int selectedFavorite(){
    auto index=SendMessageW(shelfList,LB_GETCURSEL,0,0);
    return index==LB_ERR?-1:int(SendMessageW(shelfList,LB_GETITEMDATA,index,0));
}
void fitShelfScrollbar(){
    if(!shelfDetails)return;
    RECT content{};SendMessageW(shelfDetails,EM_GETRECT,0,(LPARAM)&content);
    HDC dc=GetDC(shelfDetails);if(!dc)return;
    auto previous=SelectObject(dc,shelfFont);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
    SelectObject(dc,previous);ReleaseDC(shelfDetails,dc);
    auto rows=SendMessageW(shelfDetails,EM_GETLINECOUNT,0,0);
    ShowScrollBar(shelfDetails,SB_VERT,rows*metrics.tmHeight>content.bottom-content.top);
}
void releaseShelfOwner(){
    if(shelfPausedOwner&&IsWindow(shelfPausedOwner))EnableWindow(shelfPausedOwner,TRUE);
    shelfPausedOwner=nullptr;
}
void showShelfDetails(){
    int id=selectedFavorite();
    std::wstring detail;
    for(const auto& entry:favorites)if(entry.id==id){
        const auto& r=records.at(id);
        detail=r.clean+L"\r\n\r\n"+r.zh;
        if(!entry.words.empty())detail+=L"\r\n\r\n已标词语\r\n"+wordSummary(entry);
        break;
    }
    if(detail.empty())detail=favorites.empty()?L"在词卡内点「收藏原句」，或点正文行尾的书签。":L"可更换搜索词，或取消筛选。";
    if(!shelfSucceeded(shelfReadResult))detail=std::wstring(shelfError(shelfReadResult))+L"\r\n\r\n"+detail;
    else if(shelfSkippedRows)detail=L"已跳过 "+std::to_wstring(shelfSkippedRows)+L" 行异常记录。首次修改时会备份原文件。\r\n\r\n"+detail;
    else if(shelfReadResult==ShelfResult::Recovered)detail=L"原收藏文件已备份。\r\n\r\n"+detail;
    SetWindowTextW(shelfDetails,detail.c_str());
    fitShelfScrollbar();
    int rows=int(SendMessageW(shelfList,LB_GETCOUNT,0,0));
    SetWindowTextW(shelfEmpty,favorites.empty()?L"还没有收藏原句":L"没有匹配的原句");
    ShowWindow(shelfEmpty,rows==0?SW_SHOWNA:SW_HIDE);
    const bool canEdit=shelfSucceeded(shelfReadResult);
    EnableWindow(shelfDelete,canEdit&&id>=0);EnableWindow(shelfUndo,canEdit&&removedFavorite.id>=0);
}
void refreshShelf(){
    const int selected=selectedFavorite();
    std::wstring query(GetWindowTextLengthW(shelfSearch)+1,L'\0');
    GetWindowTextW(shelfSearch,query.data(),(int)query.size());query.resize(wcslen(query.c_str()));
    bool wordsOnly=SendMessageW(shelfFilter,BM_GETCHECK,0,0)==BST_CHECKED;
    SendMessageW(shelfList,WM_SETREDRAW,FALSE,0);SendMessageW(shelfList,LB_RESETCONTENT,0,0);
    int select=0;
    for(const auto& f:favorites){
        if(wordsOnly&&f.words.empty())continue;
        const auto& r=records.at(f.id);
        if(!query.empty()&&r.clean.find(query)==r.clean.npos&&r.zh.find(query)==r.zh.npos&&wordSummary(f).find(query)==std::wstring::npos)continue;
        auto label=r.clean;
        if(!f.words.empty())label=L"◆ "+label;
        auto i=SendMessageW(shelfList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        if(i>=0){SendMessageW(shelfList,LB_SETITEMDATA,i,f.id);if(f.id==selected)select=(int)i;}
    }
    SendMessageW(shelfList,LB_SETCURSEL,select,0);SendMessageW(shelfList,WM_SETREDRAW,TRUE,0);
    InvalidateRect(shelfList,nullptr,TRUE);showShelfDetails();
}
void drawShelfEntry(const DRAWITEMSTRUCT& item){
    auto dc=item.hDC;
    const int state=SaveDC(dc),dpi=readerWindowDpi(item.hwndItem);
    const bool selected=(item.itemState&ODS_SELECTED)!=0;
    const auto brush=(HBRUSH)GetStockObject(DC_BRUSH);
    SetDCBrushColor(dc,selected?reader_palette::Raised:reader_palette::Field);
    FillRect(dc,&item.rcItem,brush);
    if(selected){
        RECT mark=item.rcItem;
        InflateRect(&mark,0,-MulDiv(5,dpi,96));mark.right=mark.left+MulDiv(2,dpi,96);
        SetDCBrushColor(dc,reader_palette::Accent);FillRect(dc,&mark,brush);
    }
    if(item.itemID!=UINT(-1)){
        auto length=SendMessageW(item.hwndItem,LB_GETTEXTLEN,item.itemID,0);
        if(length>0){
            std::wstring label(size_t(length)+1,L'\0');
            length=SendMessageW(item.hwndItem,LB_GETTEXT,item.itemID,(LPARAM)label.data());
            if(length>0){
                RECT text=item.rcItem;InflateRect(&text,-MulDiv(10,dpi,96),0);
                SelectObject(dc,shelfFont);SetTextColor(dc,reader_palette::Body);SetBkMode(dc,TRANSPARENT);
                DrawTextW(dc,label.data(),int(length),&text,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        }
    }
    if((item.itemState&(ODS_FOCUS|ODS_NOFOCUSRECT))==ODS_FOCUS){
        RECT focus=item.rcItem;InflateRect(&focus,-2,-2);DrawFocusRect(dc,&focus);
    }
    RestoreDC(dc,state);
}
LRESULT CALLBACK shelfProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:{
        shelfPaper=CreateSolidBrush(reader_palette::Ink);shelfField=CreateSolidBrush(reader_palette::Field);
        const int dpi=readerWindowDpi(h);
        shelfFont=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,mh::uiFace());
        if(auto dwm=LoadLibraryW(L"dwmapi.dll")){
            using SetAttribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
            if(auto attr=(SetAttribute)GetProcAddress(dwm,"DwmSetWindowAttribute")){
                BOOL dark=TRUE;COLORREF color=reader_palette::Ink;attr(h,20,&dark,sizeof(dark));attr(h,35,&color,sizeof(color));
                const DWORD roundCorners=2;attr(h,33,&roundCorners,sizeof(roundCorners));
            }
            FreeLibrary(dwm);
        }
        auto make=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int id,DWORD edge=0){
            HWND child=CreateWindowExW(edge,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,(HMENU)(INT_PTR)id,moduleHandle,nullptr);
            SendMessageW(child,WM_SETFONT,(WPARAM)shelfFont,TRUE);return child;
        };
        shelfSearchLabel=make(L"STATIC",L"搜索原文、译文或已标词语",SS_LEFT,0);
        shelfSearch=make(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP|WS_BORDER,SearchID);
        SendMessageW(shelfSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(6,6));
        shelfFilter=make(L"BUTTON",L"只看有标词的句子",BS_AUTOCHECKBOX|WS_TABSTOP,FilterID);
        shelfList=make(L"LISTBOX",L"",WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_TABSTOP,ListID);
        SendMessageW(shelfList,LB_SETITEMHEIGHT,0,MulDiv(32,dpi,96));
        shelfEmpty=make(L"STATIC",L"",SS_CENTER|SS_CENTERIMAGE,0);
        shelfDetails=make(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP,0);
        SendMessageW(shelfDetails,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
        shelfDelete=make(L"BUTTON",L"移除所选",WS_TABSTOP|BS_OWNERDRAW,DeleteID);
        shelfUndo=make(L"BUTTON",L"撤销移除",WS_TABSTOP|BS_OWNERDRAW,UndoID);
        shelfExport=make(L"BUTTON",L"导出原句",WS_TABSTOP|BS_OWNERDRAW,ExportID);
        shelfBook=make(L"BUTTON",L"生词本",WS_TABSTOP|BS_OWNERDRAW,BookID);
        return 0;
    }
    case WM_ERASEBKGND:{RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,shelfPaper);return 1;}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{
        bool field=(HWND)l==shelfDetails||(HWND)l==shelfEmpty||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX;
        bool secondary=(HWND)l==shelfEmpty||(HWND)l==shelfSearchLabel;
        SetTextColor((HDC)w,secondary?reader_palette::Muted:reader_palette::Body);SetBkColor((HDC)w,field?reader_palette::Field:reader_palette::Ink);
        return (LRESULT)(field?shelfField:shelfPaper);
    }
    case WM_DRAWITEM:{
        auto& d=*(DRAWITEMSTRUCT*)l;
        if(d.CtlType==ODT_LISTBOX&&d.CtlID==ListID){drawShelfEntry(d);return TRUE;}
        if(d.CtlType!=ODT_BUTTON)break;
        bool primary=d.CtlID==ExportID,down=d.itemState&ODS_SELECTED,disabled=d.itemState&ODS_DISABLED;
        FillRect(d.hDC,&d.rcItem,shelfPaper);
        SetDCBrushColor(d.hDC,down?reader_palette::ShelfPressedFill:primary?reader_palette::Accent:reader_palette::Raised);
        auto oldPen=SelectObject(d.hDC,GetStockObject(NULL_PEN)),oldBrush=SelectObject(d.hDC,GetStockObject(DC_BRUSH));
        RoundRect(d.hDC,d.rcItem.left,d.rcItem.top,d.rcItem.right,d.rcItem.bottom,9,9);
        SelectObject(d.hDC,oldBrush);SelectObject(d.hDC,oldPen);
        SetTextColor(d.hDC,disabled?reader_palette::Dim:primary?reader_palette::Field:reader_palette::Body);SetBkMode(d.hDC,TRANSPARENT);
        wchar_t name[48]{};GetWindowTextW(d.hwndItem,name,48);auto old=SelectObject(d.hDC,shelfFont);
        DrawTextW(d.hDC,name,-1,&d.rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(d.hDC,old);if(d.itemState&ODS_FOCUS){auto r=d.rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}return TRUE;
    }
    case WM_GETMINMAXINFO:{
        const int dpi=readerWindowDpi(h);
        ((MINMAXINFO*)l)->ptMinTrackSize={MulDiv(400,dpi,96),MulDiv(330,dpi,96)};return 0;
    }
    case WM_SIZE:{
        const int dpi=readerWindowDpi(h);
        const int width=MulDiv(LOWORD(l),96,dpi)-48,height=MulDiv(HIWORD(l),96,dpi);
        auto place=[dpi](HWND child,int x,int y,int w,int height){
            MoveWindow(child,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(height,dpi,96),TRUE);
        };
        place(shelfSearchLabel,24,12,width,23);
        const int filterWidth=184;
        place(shelfSearch,24,38,std::max(85,width-filterWidth-12),30);
        place(shelfFilter,24+width-filterWidth,38,filterWidth,30);
        const int panelHeight=std::max(84,height-144),gap=14;
        const bool wide=width>=620;
        const int listWidth=wide?(width-gap)*41/100:width;
        const int listHeight=wide?panelHeight:(panelHeight-gap)*41/100;
        place(shelfList,24,80,listWidth,listHeight);place(shelfEmpty,24,80,listWidth,listHeight);
        if(wide)place(shelfDetails,24+listWidth+gap,80,width-listWidth-gap,panelHeight);
        else place(shelfDetails,24,80+listHeight+gap,width,panelHeight-listHeight-gap);
        fitShelfScrollbar();
        int cell=std::min(110,std::max(62,(width-18)/4));
        HWND buttons[]={shelfExport,shelfBook,shelfDelete,shelfUndo};
        for(int i=0;i<4;i++)place(buttons[i],24+i*(cell+6),height-49,cell,33);return 0;
    }
    case WM_COMMAND:{
        int id=LOWORD(w),event=HIWORD(w);
        if((id==SearchID&&event==EN_CHANGE)||id==FilterID)refreshShelf();
        if(id==ListID&&event==LBN_SELCHANGE)showShelfDetails();
        if(id==ExportID||id==BookID){
            if(!launchBook(gameWindow,id==ExportID?L"export-sentences":L"words"))SetWindowTextW(shelfDetails,L"读本未能打开，请检查安装包。");
        }
        if(id==DeleteID||id==UndoID){
            bool undo=id==UndoID;
            auto result=editFavorite(undo?ShelfEdit::Restore:ShelfEdit::Remove,undo?removedFavorite.id:selectedFavorite());
            if(shelfSucceeded(result)){
                shelfReadResult=result;shelfSkippedRows=0;
                refreshShelf();
            }else SetWindowTextW(shelfDetails,shelfError(result));
        }
        return 0;
    }
    case WM_CLOSE:
        // Keep this window active through the click release. The game also
        // samples the physical mouse as soon as its reading window regains focus.
        if(SetTimer(h,1,100,nullptr))return 0;
        releaseShelfOwner();guardReaderReturn(gameWindow);ShowWindow(h,SW_HIDE);return 0;
    case WM_TIMER:
        if(w!=1)break;
        if(GetAsyncKeyState(VK_LBUTTON)&0x8000)return 0;
        KillTimer(h,1);releaseShelfOwner();guardReaderReturn(gameWindow);ShowWindow(h,SW_HIDE);return 0;
    case WM_DESTROY:releaseShelfOwner();shelfWindow=nullptr;return 0;
    case WM_NCDESTROY:DeleteObject(shelfFont);DeleteObject(shelfField);DeleteObject(shelfPaper);shelfFont=nullptr;shelfField=shelfPaper=nullptr;break;
    }
    return DefWindowProcW(h,m,w,l);
}
// Retained for the native shelf implementation; F10 and the menu now share StudyBook.
void openShelf(HWND owner){
    if(!shelfWindow){
        WNDCLASSW cls{};cls.lpfnWndProc=shelfProc;cls.hInstance=moduleHandle;cls.lpszClassName=L"MahoyoYomibito.Collection";
        cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        RegisterClassW(&cls);
        RECT parent{};GetWindowRect(owner,&parent);
        const int dpi=readerWindowDpi(owner);
        int width=std::clamp(int(parent.right-parent.left),MulDiv(400,dpi,96),MulDiv(832,dpi,96));
        int height=std::clamp(int(parent.bottom-parent.top),MulDiv(340,dpi,96),MulDiv(480,dpi,96));
        shelfWindow=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"魔夜读本 · 收藏夹",
            WS_OVERLAPPEDWINDOW, parent.left+(parent.right-parent.left-width)/2,parent.top+(parent.bottom-parent.top-height)/2,
            width,height,owner,nullptr,moduleHandle,nullptr);
    }
    if(!shelfWindow)return;
    ShelfRead fromDisk;
    shelfReadResult=readFavorites(fromDisk);
    shelfSkippedRows=fromDisk.skipped;
    if(shelfSucceeded(shelfReadResult))favorites.swap(fromDisk.entries);
    refreshShelf();
    if(IsWindowVisible(owner)){
        ShowWindow(shelfWindow,SW_SHOWNORMAL);SetForegroundWindow(shelfWindow);SetFocus(shelfSearch);
        // Reading resumes on close. A click dispatched to the shelf, including
        // its system close button, cannot activate the underlying game client.
        if(IsWindowEnabled(owner)){shelfPausedOwner=owner;EnableWindow(owner,FALSE);}
    }
}
