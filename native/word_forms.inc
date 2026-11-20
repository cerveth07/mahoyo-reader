// Display the complete inflection of a studied verb when the corpus tokenizer
// identifies its contiguous ending. Original annotation offsets stay intact.
struct InflectedSpan {uint32_t stemEnd,end;std::wstring suffixReading;};
using Inflections=std::map<std::pair<int,uint32_t>,InflectedSpan>;
Inflections inflections;
enum class PartRole {Other,Inflects,Ending};
bool loadInflections(const std::map<int,Record>& story){
    inflections.clear();
    try{
        const auto local=modRoot+L"\\local\\";
        auto file=std::ifstream(std::filesystem::path(local+L"word-index.dat"),std::ios::binary|std::ios::ate);
        const auto size=file.tellg();
        if(!file||size<16||size>32*1024*1024)return false;
        Reader r;r.b.resize(size_t(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(r.b.data()),size)||memcmp(r.b.data(),"MHLEX001",8))return false;
        r.p=8;auto digest=r.s();std::array<uint8_t,32> hash{};
        if(!sha256File((local+L"story.dat").c_str(),hash)||digest.size()!=64)return false;
        const wchar_t* hex=L"0123456789abcdef";
        for(size_t j=0;j<hash.size();++j)if(digest[2*j]!=hex[hash[j]>>4]||digest[2*j+1]!=hex[hash[j]&15])return false;
        unsigned n=r.u();if(n>200000)return false;
        std::vector<PartRole> roles;roles.reserve(n);
        for(unsigned j=0;j<n;++j){
            auto base=r.s();r.s();auto pos=r.s();
            bool ending=pos==L"助動詞"||(pos==L"助詞"&&(base==L"て"||base==L"で"));
            roles.push_back(ending?PartRole::Ending:(pos==L"動詞"||pos==L"形容詞")?PartRole::Inflects:PartRole::Other);
        }
        n=r.u();if(n>200000)return false;
        std::vector<std::wstring> readings;readings.reserve(n);
        for(unsigned j=0;j<n;++j)readings.push_back(r.s());
        if(r.u()!=story.size())return false;
        Inflections parsed;
        struct Token{uint32_t a,b,lex,reading;};
        for(const auto& [id,row]:story){
            n=r.u();if(n>5000)return false;
            std::vector<Token> tokens;tokens.reserve(n);
            for(unsigned j=0;j<n;++j){
                auto pair=r.u();Token t{pair&65535,pair>>16,r.u(),r.u()};
                if(t.b<=t.a||t.b>row.clean.size()||t.lex>=roles.size()||t.reading>=readings.size()||(!tokens.empty()&&tokens.back().b>t.a))return false;
                tokens.push_back(t);
            }
            for(size_t j=0;j+1<tokens.size();++j){
                auto head=tokens[j];
                if(roles[head.lex]!=PartRole::Inflects||tokens[j+1].a!=head.b||roles[tokens[j+1].lex]!=PartRole::Ending)continue;
                auto study=std::find_if(row.words.begin(),row.words.end(),[&](const Word& w){
                    return w.start==head.a&&w.end==head.b&&(w.level==L"N1"||w.level==L"N2");
                });
                if(study==row.words.end())continue;
                InflectedSpan span{head.b,head.b,{}};
                for(size_t k=j+1;k<tokens.size();++k){
                    auto t=tokens[k];if(t.a!=span.end||roles[t.lex]!=PartRole::Ending)break;
                    if(std::any_of(row.words.begin(),row.words.end(),[&](const Word& w){
                        return w.start<t.b&&w.end>t.a&&w.level!=L"补注";
                    }))break;
                    span.end=t.b;span.suffixReading+=readings[t.reading];
                }
                if(span.end>head.b)parsed.emplace(std::make_pair(id,head.a),std::move(span));
            }
        }
        if(r.p!=r.b.size())return false;
        inflections.swap(parsed);return true;
    }catch(...){return false;}
}
Word displayedForm(const Record& row,const Word& word){
    if(word.level!=L"N1"&&word.level!=L"N2")return word;
    auto hit=inflections.find({row.id,word.start});
    if(hit==inflections.end()||hit->second.stemEnd!=word.end)return word;
    Word display=word;display.end=hit->second.end;
    display.term=row.clean.substr(word.start,display.end-word.start);
    display.reading+=hit->second.suffixReading;
    return display;
}
