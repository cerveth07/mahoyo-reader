// The renderer only queues a completed dwell. The worker owns all file I/O.
struct LookupDwell {
    int row=-1;
    WordSpan span{};
    uint64_t page=0,since=0;
    unsigned generation=0;
    bool recorded=false;
    bool update(int id,WordSpan word,uint64_t currentPage,unsigned epoch,uint64_t now){
        if(id<0||id!=row||word!=span||currentPage!=page||generation!=epoch||now<since){
            row=id;span=word;page=currentPage;generation=epoch;since=now;recorded=false;
        }
        if(row<0||recorded||now-since<800)return false;
        recorded=true;return true;
    }
};
struct RecentLookup {int row,a,b;int64_t time;};
using RecentList=std::vector<RecentLookup>;
void prependRecent(RecentList& list,const RecentLookup& entry){
    list.erase(std::remove_if(list.begin(),list.end(),[&](const RecentLookup& r){
        return r.row==entry.row&&r.a==entry.a&&r.b==entry.b;
    }),list.end());
    list.insert(list.begin(),entry);
    if(list.size()>500)list.resize(500);
}
bool mergeRecent(const RecentList& batch){
    const auto path=modRoot+L"\\recent-lookups.tsv";
    std::ifstream in(path.c_str(),std::ios::binary);
    RecentList existing;
    if(in){
        std::string line;
        if(!std::getline(in,line))return false;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        if(line!="mahoyo-recent-v1")return false;
        std::set<std::tuple<int,int,int>> seen;
        while(std::getline(in,line)&&existing.size()<500){
            std::istringstream fields(line); RecentLookup r{};char more;
            if((fields>>r.row>>r.a>>r.b>>r.time)&&!(fields>>more)&&r.row>=0&&r.a>=0&&r.b>r.a&&r.time>=0
                &&seen.emplace(r.row,r.a,r.b).second)existing.push_back(r);
        }
        if(in.bad())return false;
        in.close();
    }else if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES||GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
    for(auto it=batch.rbegin();it!=batch.rend();++it)prependRecent(existing,*it);
    std::ostringstream out;out<<"mahoyo-recent-v1\n";
    for(auto& r:existing)out<<r.row<<'\t'<<r.a<<'\t'<<r.b<<'\t'<<r.time<<'\n';
    return writeShelfFile(path,out.str(),true);
}
std::mutex recentQueueMutex;
std::condition_variable recentIdle;
RecentList pendingRecent;
bool recentWorker=false;
ULONGLONG nextRecentWrite=0;
DWORD WINAPI writeRecentWorker(void*){
    for(;;){
        RecentList batch;
        {
            std::lock_guard<std::mutex> lock(recentQueueMutex);
            if(pendingRecent.empty()){recentWorker=false;recentIdle.notify_all();return 0;}
        }
        auto now=GetTickCount64();
        if(now<nextRecentWrite)Sleep(DWORD(nextRecentWrite-now));
        {
            std::lock_guard<std::mutex> lock(recentQueueMutex);
            batch.swap(pendingRecent);
        }
        bool saved=false;
        HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\MahoyoYomibitoRecentV1");
        if(mutex){
            DWORD result=WaitForSingleObject(mutex,2000);
            if(result==WAIT_OBJECT_0||result==WAIT_ABANDONED){
                saved=mergeRecent(batch);ReleaseMutex(mutex);
            }
            CloseHandle(mutex);
        }
        nextRecentWrite=GetTickCount64()+2000;
        if(!saved){
            std::lock_guard<std::mutex> lock(recentQueueMutex);
            for(auto it=pendingRecent.rbegin();it!=pendingRecent.rend();++it)prependRecent(batch,*it);
            pendingRecent.swap(batch);
        }
    }
}
void queueRecent(int row,WordSpan span){
    FILETIME ft;GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER stamp{};stamp.LowPart=ft.dwLowDateTime;stamp.HighPart=ft.dwHighDateTime;
    int64_t seconds=int64_t(stamp.QuadPart/10000000)-11644473600LL;
    std::lock_guard<std::mutex> lock(recentQueueMutex);
    prependRecent(pendingRecent,{row,span.first,span.second,seconds});
    if(!recentWorker)recentWorker=QueueUserWorkItem(writeRecentWorker,nullptr,WT_EXECUTEDEFAULT)!=0;
}
// A normal game exit gives the throttled last batch time to finish off the render thread.
bool finishRecentWrites(){
    std::unique_lock<std::mutex> guard(recentQueueMutex);
    return recentIdle.wait_for(guard,std::chrono::seconds(5),[]{return !recentWorker&&pendingRecent.empty();});
}
