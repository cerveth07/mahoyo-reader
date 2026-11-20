// Test build only. Exercise the existing caption pipeline with each source key
// in one known story text object. The caller then lays out the real story line.
// This checks a real font atlas and layout profile, not each scene's staging.
void probeCorpusLayout(void* self,uint32_t mode){
    static bool started=false;
    if(started)return;
    started=true;
    std::ofstream out(std::filesystem::path(modRoot)/L"corpus-layout.tsv");
    if(!out)return;
    out<<"key\tresult\trows\tcaptions\tgeometry_same\tslots\tauthor_bitmap_count\tfinal_ruby_same\tcaption_overlaps\n";
    for(auto& [id,record]:records){
        if(record.words.empty()&&(record.clean==record.zh ||
            std::all_of(record.clean.begin(),record.clean.end(),whitespace))){
            out<<id<<"\tpass-through\t0\t0\t1\t0\t0\t1\t0\n";
            continue;
        }
        // In the live text layer the script's <base|reading> is encoded as
        // <rREADING>base</r>. Preserve other native control/timing characters.
        std::wstring original;
        for(size_t p=0;p<record.ja.size();){
            if(record.ja[p]==L'<'){
                auto bar=record.ja.find(L'|',p+1),end=record.ja.find(L'>',p+1);
                if(bar!=std::wstring::npos && end!=std::wstring::npos && bar<end){
                    original+=L"<r"+record.ja.substr(bar+1,end-bar-1)+L">";
                    original+=record.ja.substr(p+1,bar-p-1)+L"</r>";
                    p=end+1;continue;
                }
            }
            original+=record.ja[p++];
        }
        if(!nativeLayout(self,original.c_str(),mode)){
            out<<id<<"\toriginal-layout\t0\t0\t0\t0\n";continue;
        }
        auto before=nativeBodyRows(self);
        auto wrapped=reserved(original);
        Snapshot study;
        study.parts.emplace_back(&record,0);
        study.input=record.clean;
        auto originalRuby=originalRubyGeometry(self,study);
        if(!nativeLayout(self,wrapped.c_str(),mode)){
            out<<id<<"\treserved-layout\t0\t0\t0\t0\n";continue;
        }
        makeSnapshot(self,study);
        bool rubySame=originalRuby.empty() || sameBodyRows(originalRuby,study.occupiedRuby);
        bool ok=rubySame && installNativeCaptions(self,study);
        int overlaps=0;
        if(ok)for(const auto& caption:study.annotation.chinese){
            mh::Box rect{caption.x,caption.y,caption.x+caption.image.w,caption.y+caption.image.h};
            for(auto mark:study.occupiedRuby)if(intersects(rect,mark))++overlaps;
        }
        auto after=nativeBodyRows(self);
        bool same=before.size()==after.size();
        for(size_t i=0;same&&i<before.size();++i){
            auto a=before[i],b=after[i];
            same=a.l==b.l && a.r==b.r && a.t==b.t && a.b==b.b;
        }
        std::string status="inline";
        if(!ok || !same){
            if(!nativeLayout(self,original.c_str(),mode)){
                out<<id<<"\trestore-failed\t0\t0\t0\t0\n";continue;
            }
            Snapshot fallback;
            fallback.parts.emplace_back(&record,0);
            fallback.input=record.clean;
            prepareOriginalStudy(self,fallback,true);
            same=sameBodyRows(before,nativeBodyRows(self));
            rubySame=originalRuby.empty() || sameBodyRows(originalRuby,fallback.occupiedRuby);
            if(!fallback.annotation.sentenceRows.empty() && same)
                status=ok?"companion-geometry":"companion-slot";
            else status="fallback-failed";
        }
        out<<id<<'\t'<<status<<'\t'<<after.size()<<'\t'
           <<study.annotation.chinese.size()<<'\t'<<same<<'\t'<<study.rubySlots.size()<<'\t'
           <<originalRuby.size()<<'\t'<<rubySame<<'\t'<<overlaps<<'\n';
        if(id%100==0)out.flush();
    }
    out<<"end\n";
}
