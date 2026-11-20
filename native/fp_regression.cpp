#include <iterator>
// Native regression for WER c0000093 in quad(), RVA 0x13701 of the 23:05 DLL.
// Compile this translation unit instead of mod.cpp, linking the same renderer
// and MinHook objects. It calls the actual quad with a tiny fake D3D device;
// it neither launches nor attaches to the game and creates no GUI windows.
#include "mod.cpp"

namespace {
volatile LONG observedFaults=0;
DWORD lastFault=0;
DVertex capturedQuad[4]{};
int capturedCalls=0;
LONG CALLBACK catchFpProbe(EXCEPTION_POINTERS*p){
    auto code=p->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_FLT_UNDERFLOW&&code!=EXCEPTION_FLT_INVALID_OPERATION)return EXCEPTION_CONTINUE_SEARCH;
    ++observedFaults;lastFault=code;
    p->ContextRecord->MxCsr=(p->ContextRecord->MxCsr|0x1f80u)&~0x3fu;
    return EXCEPTION_CONTINUE_EXECUTION;
}
__declspec(noinline) void underflowProbe(){
    float inputs[4]={1e-20f,1e-20f,1e-20f,1e-20f};
    __m128 a=_mm_loadu_ps(inputs);
    __asm__ __volatile__("mulps %0,%0":"+x"(a));
    volatile float result=_mm_cvtss_f32(a);(void)result;
}
HRESULT WINAPI fakeState(IDirect3DDevice9*,DWORD,DWORD,DWORD){return D3D_OK;}
HRESULT WINAPI captureQuad(IDirect3DDevice9*,D3DPRIMITIVETYPE type,UINT count,const void*vertices,UINT stride){
    if(type!=D3DPT_TRIANGLESTRIP||count!=2||stride!=sizeof(DVertex))return E_FAIL;
    memcpy(capturedQuad,vertices,sizeof(capturedQuad));++capturedCalls;return D3D_OK;
}
struct FakeDevice{void**vtable;};
struct FakeTexture{void**vtable;D3DPOOL pool;ULONG references=1;};
ULONG WINAPI fakeTextureAddRef(IDirect3DTexture9*p){return ++((FakeTexture*)p)->references;}
ULONG WINAPI fakeTextureRelease(IDirect3DTexture9*p){return --((FakeTexture*)p)->references;}
HRESULT WINAPI fakeTextureDesc(IDirect3DTexture9*p,UINT,D3DSURFACE_DESC*out){*out={};out->Pool=((FakeTexture*)p)->pool;return S_OK;}
HRESULT WINAPI fakeVertexDesc(IDirect3DVertexBuffer9*p,D3DVERTEXBUFFER_DESC*out){*out={};out->Pool=((FakeTexture*)p)->pool;return S_OK;}
HRESULT WINAPI noTextureContainer(IDirect3DSurface9*,REFIID,void**out){*out=nullptr;return E_NOINTERFACE;}
bool naturalWrapSlotRegression(){
    // Native observations from records 4/5: ruby node line=0 in both rows,
    // while its preceding base glyph has line=1 after the natural wrap.
    for(int secondY:{347,509}){
        std::array<uint8_t,0x350>self{};std::array<uint8_t,16>sentinel{};std::array<std::array<uint8_t,0x1b0>,4>nodes{};std::vector<uint8_t>blank(24*25*4);
        auto put=[](auto&buffer,size_t offset,auto value){memcpy(buffer.data()+offset,&value,sizeof(value));};
        put(self,0x338,(void*)sentinel.data());put(self,0x340,size_t(4));put(sentinel,0,(void*)nodes[0].data());
        for(size_t i=0;i<nodes.size();i++){
            auto&n=nodes[i];put(n,0,i+1<nodes.size()?(void*)nodes[i+1].data():(void*)sentinel.data());put(n,0x188,size_t(7));bool ruby=i%2;int row=int(i/2),baseY=secondY-(1-row)*81;
            put(n,0x20,ruby?41:24);put(n,0x24,ruby?baseY-22:baseY);put(n,0x19c,ruby?0:row);
            if(!ruby){put(n,0x170,wchar_t(L'A'+row));put(n,0x180,size_t(1));}
            for(size_t bitmap:{size_t(0x30),size_t(0xd0)}){put(n,bitmap+0x84,ruby?24:55);put(n,bitmap+0x88,ruby?25:56);if(ruby){put(n,bitmap+0x90,(void*)blank.data());put(n,bitmap+0x98,96);}}
        }
        Snapshot snapshot;snapshot.input=std::wstring(26,L'x')+L"AB";makeSnapshot(self.data(),snapshot);
        if(snapshot.rubySlots.size()!=2||snapshot.rubySlots[0].line!=0||snapshot.rubySlots[1].line!=1||snapshot.rubySlots[1].source!=27||captionSlot(snapshot.rubySlots,{4,1,26,28})!=1)return false;
    }return true;
}
bool boundedVertexCacheRegression(void** table){
    const auto savedFrame=frameNo;
    std::vector<FakeTexture> items(3*vbEntryLimit+3,{table,D3DPOOL_MANAGED});
    auto vb=[&](size_t i){return reinterpret_cast<IDirect3DVertexBuffer9*>(&items[i]);};
    auto clear=[](){for(auto& item:vertexBuffers)item.first->Release();vertexBuffers.clear();};
    for(size_t i=0;i<vbEntryLimit;++i)trackVertexBuffer(vb(i),112);
    trackVertexBuffer(vb(vbEntryLimit),112); // All current; decline without acquiring ownership.
    bool ok=vertexBuffers.size()==vbEntryLimit&&items[vbEntryLimit].references==1;
    vertexBuffers.at(vb(0)).locked=&items[0];
    vertexBuffers.at(vb(1)).bytes[9]=77;
    for(size_t i=vbEntryLimit;i<items.size();++i){
        frameNo+=3;vertexBuffers.at(vb(1)).used=frameNo-2;
        trackVertexBuffer(vb(i),112);
        ok&=vertexBuffers.size()==vbEntryLimit&&items[i].references==2&&items[0].references==2&&items[1].references==2;
    }
    for(size_t i=2;i<vbEntryLimit;++i)ok&=items[i].references==1;
    forgetDefaultVertexBuffers();ok&=vertexBuffers.at(vb(1)).bytes[9]==77;
    clear();for(const auto& i:items)ok&=i.references==1;
    for(size_t i=0;i<5;++i){frameNo+=3;trackVertexBuffer(vb(i),8u*1024*1024);}
    ok&=vertexBuffers.size()==4&&items[0].references==1&&items[4].references==2;
    clear();for(const auto& i:items)ok&=i.references==1;frameNo=savedFrame;return ok;
}
bool reviewRegressions(){
    // SHA-256 must inspect real file bytes and reject the regression executable
    // as an unsupported game rather than accepting a familiar PE prologue.
    wchar_t tempDir[MAX_PATH],tempFile[MAX_PATH];if(!GetTempPathW(MAX_PATH,tempDir)||!GetTempFileNameW(tempDir,L"mhf",0,tempFile))return false;
    FILE*f=_wfopen(tempFile,L"wb");if(!f){DeleteFileW(tempFile);return false;}fwrite("abc",1,3,f);fclose(f);std::array<uint8_t,32>digest{};bool hashed=sha256File(tempFile,digest);DeleteFileW(tempFile);
    const uint8_t expected[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    bool hash=hashed&&memcmp(digest.data(),expected,32)==0&&!supportedExecutable();
    // Reset must discard only DEFAULT resources. UpdateTexture still needs the
    // SYSTEMMEM source identity when no text Lock/re-layout follows Reset.
    void* textureTable[23]{};textureTable[1]=(void*)fakeTextureAddRef;textureTable[2]=(void*)fakeTextureRelease;textureTable[17]=(void*)fakeTextureDesc;
    FakeTexture system{textureTable,D3DPOOL_SYSTEMMEM},managed{textureTable,D3DPOOL_MANAGED},device{textureTable,D3DPOOL_DEFAULT};
    Layer layer;layer.native=&layer;layer.gpu=(IDirect3DTexture9*)&system;rememberTexture(layer,(IDirect3DTexture9*)&system);rememberTexture(layer,(IDirect3DTexture9*)&managed);rememberTexture(layer,(IDirect3DTexture9*)&device);forgetDefaultTextures(layer);
    bool reset=layer.ownedTextures.size()==2&&layer.gpu==(IDirect3DTexture9*)&system&&gpuLayers.count((IDirect3DTexture9*)&system)&&gpuLayers.count((IDirect3DTexture9*)&managed)&&!gpuLayers.count((IDirect3DTexture9*)&device)&&device.references==1;forgetLayerTextures(layer);reset&=system.references==1&&managed.references==1;
    void*vertexTable[14]{};vertexTable[1]=(void*)fakeTextureAddRef;vertexTable[2]=(void*)fakeTextureRelease;vertexTable[13]=(void*)fakeVertexDesc;
    FakeTexture vbManaged{vertexTable,D3DPOOL_MANAGED},vbDefault{vertexTable,D3DPOOL_DEFAULT};trackVertexBuffer((IDirect3DVertexBuffer9*)&vbManaged,128);trackVertexBuffer((IDirect3DVertexBuffer9*)&vbDefault,128);vertexBuffers[(IDirect3DVertexBuffer9*)&vbManaged].bytes[17]=73;forgetDefaultVertexBuffers();bool vbReset=vertexBuffers.size()==1&&vertexBuffers.count((IDirect3DVertexBuffer9*)&vbManaged)&&vertexBuffers[(IDirect3DVertexBuffer9*)&vbManaged].bytes[17]==73&&vbDefault.references==1;for(auto&[vb,_]:vertexBuffers)vb->Release();vertexBuffers.clear();vbReset&=vbManaged.references==1;
    std::vector<uint8_t>pixels(16*16*4);layer.w=layer.h=16;layer.pitch=64;layer.locked=pixels.data();Annotation annotation;annotation.body={{0,0,16,16}};
    bool ink=!bodyInkPresent(annotation,layer);pixels[3]=9;pixels[2*4+3]=9;ink&=bodyInkPresent(annotation,layer);std::fill(pixels.begin(),pixels.end(),0);ink&=!bodyInkPresent(annotation,layer);
    bool zero=bitmapAllZero(layer);pixels[2]=1;zero&=!bitmapAllZero(layer);pixels[2]=0;
    // The next record shares Y/line with the first record: its caption must
    // select its own first base offset instead of the first free same-Y slot.
    std::vector<RubySlot>slots={{nullptr,0,0,24,25,0,1,false},{nullptr,50,0,24,25,0,2,false},{nullptr,100,0,24,25,0,10,false},{nullptr,150,0,24,25,1,11,false}};
    bool ownership=captionSlot(slots,{2,0,10,20})==2&&captionSlot(slots,{2,1,10,20})==3&&captionSlot(slots,{2,2,10,20})==slots.size();
    mh::Raster small;small.w=small.h=2;small.pixels={0xffffffff,0x00ff0000,0x00ff0000,0xff000000};auto down=tightRaster(small,1);
    bool area=down.w==1&&down.h==1&&down.pixels[0]==0x80808080u;
    void*ownerA=(void*)0x10000,*ownerB=(void*)0x20000;Layer isolation;isolation.mapping.valid=true;Annotation a,b;a.owner=ownerA;b.owner=ownerB;isolation.pageAnnotations={a,b};layers[&isolation]=isolation;snapshots[ownerA]=std::make_shared<Snapshot>();snapshots[ownerB]=std::make_shared<Snapshot>();discardSnapshot(ownerA,true);
    bool isolated=layers[&isolation].pageAnnotations.size()==1&&layers[&isolation].pageAnnotations.front().owner==ownerB&&layers[&isolation].mapping.valid&&!snapshots.count(ownerA)&&snapshots.count(ownerB);layers.erase(&isolation);snapshots.erase(ownerB);
    void*surfaceTable[13]{};surfaceTable[11]=(void*)noTextureContainer;FakeDevice surface{surfaceTable};Layer alphaLayer;Affine matrix{1,0,0,0,1,0,true};surfaceMapping(alphaLayer,(IDirect3DSurface9*)&surface,matrix,true,0);bool alpha=alphaLayer.drawnAlpha==0;surfaceMapping(alphaLayer,(IDirect3DSurface9*)&surface,matrix,true,255);alpha&=alphaLayer.drawnAlpha==255;
    auto marked=reserved(L"<rgb=white>漢</rgb>");bool tags=marked.find(L"<rgb=white>")!=marked.npos&&marked.find(L"</rgb>")!=marked.npos;
    nativeRubyTags.insert(L"<rしず>");nativeRubyTags.insert(L"<r・>");
    auto author=reserved(L"<rしず>厳</r>か<r・>朝</r>");
    tags=tags && author==L"<rしず>厳</r><r　>か</r><r・>朝</r>" &&
        reserved(L"<厳|しず>か")==L"<rしず>厳</r><r　>か</r>";
    bool naturalWrap=naturalWrapSlotRegression();
    printf("review_sha256=%s; reset_surviving_sources=%s; gray_ink_and_clear=%s; same_row_slot_ownership=%s; premultiplied_area_filter=%s; self_only_discard=%s; final_alpha=%s; exact_ruby_tags=%s; observed_natural_wrap_slots=%s\n",hash?"ok":"FAIL",reset?"ok":"FAIL",ink?"ok":"FAIL",ownership?"ok":"FAIL",area?"ok":"FAIL",isolated?"ok":"FAIL",alpha?"ok":"FAIL",tags?"ok":"FAIL",naturalWrap?"ok":"FAIL");
    printf("reset_managed_vertex_shadow=%s; full_bitmap_clear_not_rgb_ink=%s\n",vbReset?"ok":"FAIL",zero?"ok":"FAIL");
    bool cache=boundedVertexCacheRegression(vertexTable);printf("bounded_vertex_cache_lru_and_refcounts=%s\n",cache?"ok":"FAIL");
    return cache&&hash&&reset&&ink&&ownership&&area&&isolated&&alpha&&tags&&naturalWrap&&vbReset&&zero;
}
__declspec(noinline) void poisonedQuadCall(IDirect3DDevice9*device){
    // Windows x64 passes x/y/w in the low lanes of XMM1/2/3; the other
    // lanes have no ABI-defined value. Deliberately supply signaling NaNs.
    alignas(16) uint32_t x[4]={0x42c80000u,0x7f800001u,0x7f800001u,0x7f800001u}; // 100
    alignas(16) uint32_t y[4]={0x43480000u,0x7f800001u,0x7f800001u,0x7f800001u}; // 200
    alignas(16) uint32_t w[4]={0x43d20000u,0x7f800001u,0x7f800001u,0x7f800001u}; // 420
    auto fn=&quad;
    __asm__ __volatile__(
        "movups %[x],%%xmm1\n\t"
        "movups %[y],%%xmm2\n\t"
        "movups %[w],%%xmm3\n\t"
        "movq %[device],%%rcx\n\t"
        "movq %[fn],%%rax\n\t"
        "movq %%rsp,%%r12\n\t"
        "andq $-16,%%rsp\n\t"
        "subq $64,%%rsp\n\t"
        "movl $0x42c80000,32(%%rsp)\n\t" // h = 100
        "movl $0xffffffff,40(%%rsp)\n\t"
        "movq $0,48(%%rsp)\n\t"
        "callq *%%rax\n\t"
        "movq %%r12,%%rsp\n\t"
        : : [x]"m"(x),[y]"m"(y),[w]"m"(w),[device]"r"(device),[fn]"r"(fn)
        : "rax","rcx","rdx","r8","r9","r10","r11","r12",
          "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","memory","cc");
}
}

#include "reading_frame_test.inc"
bool favoritesRegression(){
    const auto dir=std::filesystem::temp_directory_path()/(L"mahoyo-favorites-test-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!std::filesystem::create_directory(dir))return false;
    modRoot=dir.wstring();
    records[42].clean=L"彼女の影";records[17].clean=L"朝";
    auto path=dir/L"favorites.tsv",backup=dir/L"favorites.tsv.bak";
    auto write=[&](const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;return bool(file);};
    auto bytes=[](const std::filesystem::path& file){
        std::ifstream input(file,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
    };
    ShelfRead loaded;
    bool ok=editFavorite(ShelfEdit::Save,42,{3,4})==ShelfResult::Ok&&
        editFavorite(ShelfEdit::Save,42,{3,4})==ShelfResult::Ok&&editFavorite(ShelfEdit::Save,42)==ShelfResult::Ok&&
        editFavorite(ShelfEdit::Save,17)==ShelfResult::Ok&&readFavorites(loaded)==ShelfResult::Ok&&
        loaded.entries.size()==2&&loaded.entries[0].id==17&&loaded.entries[1].words==std::vector<WordSpan>{{3,4}}&&
        editFavorite(ShelfEdit::Remove,42)==ShelfResult::Ok&&favorites.size()==1&&
        editFavorite(ShelfEdit::Restore,42)==ShelfResult::Ok&&favorites.front().words==std::vector<WordSpan>{{3,4}};
    const std::string header="\xef\xbb\xbf" "mahoyo-favorites-v1\r\n";
    ok&=write(header+"42\t3\t4\r\n")&&readFavorites(loaded)==ShelfResult::Ok&&loaded.entries.size()==1&&
        loaded.entries.front().words==std::vector<WordSpan>{{3,4}}&&loaded.skipped==0;
    const std::string damaged=header+"42\t0\t99\r\nnot a record\r\n42\t3\t4\r\n17\r\n";
    ok&=write(damaged)&&readFavorites(loaded)==ShelfResult::Ok&&loaded.entries.size()==2&&
        loaded.entries.front().words==std::vector<WordSpan>{{3,4}}&&loaded.skipped==2&&!std::filesystem::exists(backup);
    const auto backupBlock=dir/(L"favorites.tsv.bak."+std::to_wstring(GetCurrentProcessId())+L".tmp");
    ok&=std::filesystem::create_directory(backupBlock);
    ok&=editFavorite(ShelfEdit::Save,42)==ShelfResult::BackupFailed&&bytes(path)==damaged;
    std::filesystem::remove(backupBlock);
    ok&=editFavorite(ShelfEdit::Save,42)==ShelfResult::Recovered&&bytes(backup)==damaged&&
        readFavorites(loaded)==ShelfResult::Ok&&loaded.skipped==0&&loaded.entries.size()==2&&
        loaded.entries.front().words==std::vector<WordSpan>{{3,4}};
    const auto canonical=bytes(path);
    const auto writeBlock=dir/(L"favorites.tsv."+std::to_wstring(GetCurrentProcessId())+L".tmp");
    ok&=std::filesystem::create_directory(writeBlock);
    ok&=editFavorite(ShelfEdit::Remove,42)==ShelfResult::WriteFailed&&bytes(path)==canonical;
    std::filesystem::remove(writeBlock);
    const auto laterDamage=header+"42\t3\t4\r\n99\r\n";
    ok&=write(laterDamage)&&editFavorite(ShelfEdit::Save,17)==ShelfResult::Recovered&&bytes(backup)==damaged;
    bool laterBackup=false;
    for(const auto& item:std::filesystem::directory_iterator(dir)){
        if(item.path().filename().wstring().rfind(L"favorites.tsv.bak.",0)==0){
            laterBackup=bytes(item.path())==laterDamage;
            std::filesystem::remove(item.path());
        }
    }
    ok&=laterBackup;
    const std::string foreign="different format\r\n42\r\n";
    ok&=write(foreign)&&editFavorite(ShelfEdit::Save,42)==ShelfResult::UnknownFormat&&bytes(path)==foreign;
    HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock==INVALID_HANDLE_VALUE)ok=false;
    else {
        ok&=editFavorite(ShelfEdit::Save,42)==ShelfResult::ReadFailed;
        CloseHandle(lock);
    }
    ok&=bytes(path)==foreign;
    std::filesystem::remove(path);std::filesystem::remove(backup);std::filesystem::remove(dir);
    records.clear();favorites.clear();modRoot.clear();
    return ok;
}

int main(int argc,char**argv){
    if(argc==2 && std::string(argv[1])=="--check-game-build")return matchesSupportedBuild(L"WoH.exe")?0:1;
    if(argc==2 && std::string(argv[1])=="--reading-frame")return readingFrameRegression()?0:14;
    if(argc==2 && std::string(argv[1])=="--full-data"){
        modRoot=L"package\\mahoyo-learning";
        if(!loadData() || records.size()!=24134 || records.rbegin()->first!=24133) return 10;
        for(const auto& [key,rec]:records){
            if(plain(reserved(rec.ja))!=rec.clean){
                fprintf(stderr,"Native control round trip failed at %d\n",key);
                return 11;
            }
        }
        puts("All 24134 records passed native decoding, word spans and control round trip.");
        return 0;
    }
    HoverDwell hover;
    auto step=[&](int x,int id,int page,int input,ULONGLONG ms){return hover.update({x,50},id,page,input,ms);};
    if(step(50,1,1,0,100)||step(51,1,1,0,120)||step(52,1,1,0,1119)||!step(52,1,1,0,1120)||
       step(52,2,2,0,1200)||step(52,2,2,0,2500)||step(53,2,2,0,2501)||step(54,-1,2,0,3501)||
       step(55,2,2,0,4000)||!step(55,2,2,0,5000)||step(55,2,2,1,5001)||step(55,2,2,1,6100))return 12;
    if(!favoritesRegression())return 13;
    puts("Reading dwell and favorites persist, merge, undo, BOM/CRLF, recovery backup and read/write protection passed.");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    auto handler=AddVectoredExceptionHandler(1,catchFpProbe);if(!handler)return 2;
    const unsigned initial=_mm_getcsr();
    const unsigned unmasked=((initial|0x1f80u)&~0x3fu)&~(1u<<11); // underflow enabled
    _mm_setcsr(unmasked);underflowProbe();
    const LONG baselineFaults=observedFaults;const DWORD baselineCode=lastFault;
    if(baselineFaults!=1||baselineCode!=EXCEPTION_FLT_UNDERFLOW){_mm_setcsr(initial);printf("FAIL: underflow positive control faults=%ld code=%08lx\n",baselineFaults,baselineCode);return 3;}
    observedFaults=0;_mm_setcsr(unmasked);
    {StudyFpScope scope;underflowProbe();}
    if(observedFaults||_mm_getcsr()!=unmasked){_mm_setcsr(initial);puts("FAIL: guard faulted or did not restore MXCSR");return 4;}
    void* vtable[137];for(auto&slot:vtable)slot=(void*)fakeState;FakeDevice fake{vtable};originalDrawUP=captureQuad;
    // Repeatedly mimic reset/driver changes to the exception masks and rounding.
    bool restored=true;for(unsigned i=0;i<256;i++){
        unsigned incoming=(unmasked&~(1u<<7)&~(3u<<13))|((i%4)<<13);
        _mm_setcsr(incoming);poisonedQuadCall((IDirect3DDevice9*)&fake);restored&=_mm_getcsr()==incoming;
    }
    _mm_setcsr(initial);RemoveVectoredExceptionHandler(handler);
    const bool positions=capturedQuad[0].x==99.5f&&capturedQuad[0].y==199.5f&&capturedQuad[3].x==519.5f&&capturedQuad[3].y==299.5f;
    // Captured id5 geometry: the exit confirmation covered 71-78% of several
    // rows, but only ~56% of the entire page. Small decorative draws must not
    // activate the new per-row path, even when moved directly onto a text row.
    const std::vector<mh::Box> rows={{323,157,1024,214},{323,319,924,376},{323,400,1619,457},{273,481,1470,538},{323,562,1619,619},{273,643,875,700}};
    const bool occlusion=occludesText(rows,{495,287,1424,624})&&occludesText(rows,{0,0,1920,1080})
        &&!occludesText(rows,{323,319,414,410})&&!occludesText(rows,{323,319,404,345})
        &&!occludesText(rows,{323,319,384,365})&&!occludesText(rows,{323,374,924,376})
        &&!occludesText(rows,{1200,750,1800,1000});
    printf("baseline_underflow=%ld code=%08lx; guarded_faults=%ld; quad_calls=%d; positions=%s; exact_mxcsr_restore=%s\n",baselineFaults,baselineCode,observedFaults,capturedCalls,positions?"ok":"FAIL",restored?"ok":"FAIL");
    const bool stageAlphaOK=stageAlpha(D3DTOP_MODULATE,-1,0)==0 &&
        stageAlpha(D3DTOP_MODULATE2X,0,-1)==0 && stageAlpha(D3DTOP_MODULATE,255,-1)==-1 &&
        stageAlpha(D3DTOP_SELECTARG2,0,255)==255 && stageAlpha(D3DTOP_SELECTARG1,0,255)==0 &&
        stageAlpha(D3DTOP_ADD,0,0)==-1;
    printf("fixed_function_text_alpha=%s\n",stageAlphaOK?"ok":"FAIL");
    printf("modal_and_small_decoration_geometry=%s\n",occlusion?"ok":"FAIL");
    bool review=reviewRegressions();
    return observedFaults||capturedCalls!=256||!positions||!restored||!occlusion||!review||!stageAlphaOK?5:0;
}
