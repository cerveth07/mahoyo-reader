// The companion writes the metadata and its occurrence index in one atomic replacement.
struct PersonalNote {std::wstring base,kana,meaning;bool mastered=false,local=false;};
struct PersonalHit {int start,end;unsigned note;std::wstring reading;};
std::vector<PersonalNote> personalNotes;
std::map<int,std::vector<PersonalHit>> personalHits;
unsigned personalRevision=1;
FILETIME personalTime{};
std::wstring personalStoryHash;
UINT personalMessage(){static const UINT m=RegisterWindowMessageW(L"MahoyoYomibito.PersonalChanged.v1");return m;}
bool refreshPersonal(){
    const auto path=modRoot+L"\\personal-words.dat";
    WIN32_FILE_ATTRIBUTE_DATA file{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&file)){
        if(GetLastError()==ERROR_FILE_NOT_FOUND){
            std::lock_guard<std::recursive_mutex> lock(stateMutex);
            if(!personalHits.empty()||!personalNotes.empty()){personalHits.clear();personalNotes.clear();++personalRevision;}
            personalTime={};
        }return true;
    }
    if(CompareFileTime(&file.ftLastWriteTime,&personalTime)==0)return true;
    if(file.nFileSizeHigh||file.nFileSizeLow>256*1024*1024||file.nFileSizeLow<24)return false;
    try {
        Reader r;r.b.resize(file.nFileSizeLow);
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return false;
        DWORD got=0;bool readOk=ReadFile(f,r.b.data(),file.nFileSizeLow,&got,nullptr)&&got==file.nFileSizeLow;CloseHandle(f);
        if(!readOk||memcmp(r.b.data(),"MHUSER01",8))return false;r.p=8;
        if(personalStoryHash.empty()){
            std::array<uint8_t,32> bytes{};if(!sha256File((modRoot+L"\\local\\story.dat").c_str(),bytes))return false;
            const wchar_t* hex=L"0123456789abcdef";for(auto b:bytes){personalStoryHash+=hex[b>>4];personalStoryHash+=hex[b&15];}
        }
        if(r.s()!=personalStoryHash)return false;
        auto json=r.u();if(json>r.b.size()-r.p)return false;r.p+=json;
        auto count=r.u();if(count>100000)return false;
        std::vector<PersonalNote> notes;notes.reserve(count);
        for(unsigned i=0;i<count;++i){PersonalNote n{r.s(),r.s(),r.s(),r.u()!=0,r.u()!=0};if(n.base.empty()||n.meaning.empty())return false;notes.push_back(std::move(n));}
        std::map<int,std::vector<PersonalHit>> hits;count=r.u();if(count>2500000)return false;
        for(unsigned i=0;i<count;++i){
            int id=int(r.u());PersonalHit h{int(r.u()),int(r.u()),r.u(),r.s()};
            auto row=records.find(id);if(row==records.end()||h.start<0||h.end<=h.start||size_t(h.end)>row->second.clean.size()||h.note>=notes.size())return false;
            hits[id].push_back(std::move(h));
        }
        if(r.p!=r.b.size())return false;
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        personalNotes.swap(notes);personalHits.swap(hits);personalTime=file.ftLastWriteTime;++personalRevision;return true;
    }catch(...){return false;}
}
void updatePersonalBoxes(Annotation& a){
    if(a.personalVersion==personalRevision)return;
    a.personalVersion=personalRevision;a.personalBoxes.clear();
    if(personalHits.empty())return;
    std::set<int> rows;for(auto& row:a.sentenceRows)rows.insert(row.second);
    for(int id:rows){
        auto found=personalHits.find(id);if(found==personalHits.end())continue;
        const auto& record=records.at(id);
        for(const auto& hit:found->second){
            const auto& n=personalNotes[hit.note];std::map<int,mh::Box> boxes;
            for(const auto& g:a.personalGlyphs){
                if(g.id!=id||g.start>=hit.end||g.end<=hit.start)continue;
                auto b=g.box;const int w=b.r-b.l,len=g.end-g.start;
                b.r=b.l+(std::min(hit.end,g.end)-g.start)*w/len;
                b.l+=(std::max(hit.start,g.start)-g.start)*w/len;
                auto item=boxes.find(g.line);if(item==boxes.end())boxes.emplace(g.line,b);else item->second=unite(item->second,b);
            }
            if(boxes.empty())continue;
            Word word;word.start=hit.start;word.end=hit.end;word.term=record.clean.substr(hit.start,hit.end-hit.start);
            word.reading=hit.reading.empty()?n.kana:hit.reading;word.level=n.mastered?L"已掌握":L"生词本";word.pos=n.local?L"我的本句释义":L"我的释义";
            word.definition=n.meaning;
            if(n.base!=word.term&&!n.base.empty())word.definition+=L"\n辞書形："+n.base+(n.kana.empty()?L"":L"（"+n.kana+L"）");
            // The author's reading stays visible even where a personal entry covers a longer expression.
            for(const auto& orig:record.words){
                if(orig.level!=L"原作"||orig.reading.empty()||orig.start<word.start||orig.end>word.end)continue;
                if(orig.start==word.start&&orig.end==word.end)word.reading=orig.reading;
                else word.definition+=L"\n原作注音："+orig.term+L"（"+orig.reading+L"）";
            }
            for(const auto& b:boxes)a.personalBoxes.push_back({b.second,word,id});
        }
    }
}
void notice(const wchar_t* message);
bool launchBook(HWND owner,const wchar_t* action,int id=-1,int start=-1,int end=-1){
    auto app=modRoot+L"\\local\\StudyBook.exe";
    std::wstring command=L"\""+app+L"\" "+action+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(owner))+L" "+std::to_wstring(id)+L" "+std::to_wstring(start)+L" "+std::to_wstring(end);
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION process{};
    if(!CreateProcessW(app.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&process))return false;
    AllowSetForegroundWindow(process.dwProcessId);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    const wchar_t* label=L"正在打开生词本…";
    if(!lstrcmpW(action,L"sentences")||!lstrcmpW(action,L"export-sentences"))label=L"正在打开收藏夹…";
    else if(!lstrcmpW(action,L"dict"))label=L"正在打开离线词典…";
    else if(!lstrcmpW(action,L"legend"))label=L"正在打开功能说明…";
    else if(!lstrcmpW(action,L"pick"))label=L"正在打开选词窗口…";
    else if(!lstrcmpW(action,L"edit"))label=L"正在打开补释义…";
    notice(label);return true;
}
