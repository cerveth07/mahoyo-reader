using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml.Linq;
using System.Threading;
using System.Net;
using System.Reflection;
using System.Collections;
using System.Collections.Generic;
using System.IO.Compression;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace Mahoyo.Book {
    class Source {
        public string Name,Path,Reader,Kind;
        public override string ToString(){return Name+"   ·   "+(Kind=="bundled"?"读本自带":Kind=="eb"?"EBWin4":"日中词表");}
    }
    class Candidate {
        public string Source,Text;
        public override string ToString(){return Text+"　〔"+Source+"〕";}
    }
    class Dictionaries:IDisposable {
        sealed class CachedSource {
            public DateTime Modified;
            public string Reader;
            public EbReader Eb;
            public TsvIndex Index;
        }
        readonly Dictionary<string,CachedSource> cache=new Dictionary<string,CachedSource>(StringComparer.OrdinalIgnoreCase);
        CachedSource Cached(Source source){
            DateTime modified=File.GetLastWriteTimeUtc(source.Path);
            CachedSource entry;
            if(cache.TryGetValue(source.Path,out entry)){
                if(entry.Modified==modified&&entry.Reader==source.Reader)return entry;
                if(entry.Eb!=null)entry.Eb.Dispose();cache.Remove(source.Path);
            }
            entry=new CachedSource{Modified=modified,Reader=source.Reader};
            if(source.Kind=="eb")entry.Eb=new EbReader(source,root);
            else entry.Index=new TsvIndex(source);
            cache.Add(source.Path,entry);return entry;
        }
        public void Dispose(){
            lock(EbReader.Gate){
                foreach(var entry in cache.Values)if(entry.Eb!=null)entry.Eb.Dispose();
                cache.Clear();
            }
        }
        readonly string root,config;
        public Dictionaries(string root){this.root=root;config=Path.Combine(root,"dictionary-sources.xml");}
        public List<Source> All(){
            var list=Configured();
            string path=Path.Combine(root,"local","shogakukan.zip");
            if(File.Exists(path))list.Insert(0,new Source{Kind="bundled",Name="小学馆《中日・日中統合辞書》",Path=path,Reader=""});
            return list;
        }
        List<Source> Configured(){
            if(!File.Exists(config))return new List<Source>();
            XDocument doc;using(var f=new FileStream(config,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))doc=XDocument.Load(f);
            if(doc.Root.Name!="dictionaries"||(string)doc.Root.Attribute("version")!="1")throw new InvalidDataException("无法识别词典配置。");
            return doc.Root.Elements("source").Select(x=>new Source{Name=(string)x.Attribute("name"),Path=(string)x.Attribute("path"),Reader=(string)x.Attribute("reader")??"",Kind=(string)x.Attribute("kind")}).ToList();
        }
        void Change(Action<List<Source>> fn){
            using(var guard=new Mutex(false,@"Local\MahoyoDictionary-"+Wire.Hash(Wire.Utf8.GetBytes(config.ToUpperInvariant())))){
                try{if(!guard.WaitOne(5000))throw new IOException("词典配置正忙。");}catch(AbandonedMutexException){}
                try{
                    var list=Configured();fn(list);var xml=new XDocument(new XElement("dictionaries",new XAttribute("version","1"),list.Select(x=>new XElement("source",new XAttribute("name",x.Name),new XAttribute("path",x.Path),new XAttribute("kind",x.Kind),new XAttribute("reader",x.Reader??"")))));
                    Wire.Atomic(config,f=>xml.Save(f),true);
                }finally{guard.ReleaseMutex();}
            }
        }
        public void Remove(Source s){Change(list=>list.RemoveAll(x=>String.Equals(x.Path,s.Path,StringComparison.OrdinalIgnoreCase)));}
        void Add(Source s){Change(list=>{list.RemoveAll(x=>String.Equals(x.Path,s.Path,StringComparison.OrdinalIgnoreCase));list.Add(s);});}
        public void Link(string catalog,string reader){
            catalog=Path.GetFullPath(catalog);reader=Path.GetFullPath(reader);
            var s=new Source{Kind="eb",Path=catalog,Reader=reader,Name=new DirectoryInfo(Path.GetDirectoryName(catalog)).Name};
            lock(EbReader.Gate)using(var dict=new EbReader(s,root)){} Add(s);
        }
        static string Clean(string value){return Regex.Replace(value??"",@"\s+"," ").Trim();}
        static void Tsv(Stream stream,StreamWriter output,ref int n){
            using(var r=new StreamReader(stream,Wire.Utf8,true)){
                string line;int lineNo=0;while((line=r.ReadLine())!=null){
                    lineNo++;if(String.IsNullOrWhiteSpace(line)||line.StartsWith("#"))continue;
                    var cells=line.Split('\t');
                    if(cells.Length>=3&&((cells[0]=="词条"&&cells[1]=="读音")||(cells[0]=="term"&&cells[1]=="reading")))continue;
                    if(cells.Length!=3||String.IsNullOrWhiteSpace(cells[0])||String.IsNullOrWhiteSpace(cells[2]))throw new InvalidDataException("TSV 第 "+lineNo+" 行需要词条、读音、释义三列。");
                    output.WriteLine(Clean(cells[0])+"\t"+Clean(cells[1])+"\t"+Clean(cells[2]));n++;
                }
            }
        }
        static string Rich(object value){
            if(value is string)return (string)value;
            var obj=value as Dictionary<string,object>;
            if(obj!=null){object content;if(obj.TryGetValue("content",out content))return Rich(content);if(obj.TryGetValue("text",out content))return Rich(content);return "";}
            var items=value as object[];return items==null?"":String.Join(" ",items.Select(Rich).Where(s=>s.Length!=0));
        }
        public int Import(string path){
            string dir=Path.GetFullPath(Path.Combine(root,"dictionaries"));
            // File.Move on .NET Framework still calls Win32 with the supplied path.
            // A SHA-256 filename can cross MAX_PATH even when the game itself fits.
            if(dir.Length+1+64+".tsv".Length>=260&&!dir.StartsWith(@"\\?\",StringComparison.Ordinal))
                dir=dir.StartsWith(@"\\",StringComparison.Ordinal)?@"\\?\UNC\"+dir.Substring(2):@"\\?\"+dir;
            Directory.CreateDirectory(dir);
            string temp=Path.Combine(dir,Guid.NewGuid().ToString("N")+".tmp");string title=Path.GetFileNameWithoutExtension(path);int n=0;
            try{
                using(var f=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None))
                using(var w=new StreamWriter(f,Wire.Utf8)){
                    if(Path.GetExtension(path).Equals(".tsv",StringComparison.OrdinalIgnoreCase)){
                        using(var input=File.OpenRead(path))Tsv(input,w,ref n);
                    }else using(var zip=ZipFile.OpenRead(path)){
                        if(zip.Entries.Sum(e=>e.Length)>1024L*1024*1024)throw new InvalidDataException("词典超过 1 GiB。");
                        var table=zip.GetEntry("dictionary.tsv");
                        if(table!=null){using(var input=table.Open())Tsv(input,w,ref n);}
                        else{
                            var json=new JavaScriptSerializer{MaxJsonLength=100*1024*1024};
                            var index=zip.GetEntry("index.json");
                            if(index==null)throw new InvalidDataException("ZIP 中需要 Yomitan 的 index.json 或 dictionary.tsv。");
                            using(var r=new StreamReader(index.Open(),Wire.Utf8)){
                                var meta=json.Deserialize<Dictionary<string,object>>(r.ReadToEnd());if(meta.ContainsKey("title"))title=Convert.ToString(meta["title"]);
                            }
                            foreach(var bank in zip.Entries.Where(e=>Regex.IsMatch(e.FullName,@"^term_bank_[0-9]+\.json$"))){
                                if(bank.Length>100*1024*1024)throw new InvalidDataException("单个词库分卷过大。");
                                using(var r=new StreamReader(bank.Open(),Wire.Utf8)){
                                    foreach(var row in json.Deserialize<object[][]>(r.ReadToEnd())){
                                        if(row.Length<6||!(row[0] is string)||!(row[5] is object[]))throw new InvalidDataException("Yomitan 词条结构无效。");
                                        var term=Clean((string)row[0]);var kana=Clean(row[1] as string);
                                        foreach(object sense in (object[])row[5]){var def=Clean(Rich(sense));if(term.Length!=0&&def.Length!=0){w.WriteLine(term+"\t"+kana+"\t"+def);n++;}}
                                    }
                                }
                            }
                        }
                    }
                }
                if(n==0)throw new InvalidDataException("没有可导入的释义。");
                string digest;using(var h=System.Security.Cryptography.SHA256.Create())using(var file=File.OpenRead(temp))digest=BitConverter.ToString(h.ComputeHash(file)).Replace("-","").ToLowerInvariant();
                var final=Path.Combine(dir,digest+".tsv");
                if(!File.Exists(final)){try{File.Move(temp,final);}catch(IOException){if(!File.Exists(final))throw;}}
                Add(new Source{Kind="tsv",Name=title,Path=final});return n;
            }finally{if(File.Exists(temp))File.Delete(temp);}
        }
        TsvIndex IndexFor(Source source){lock(EbReader.Gate)return Cached(source).Index;}
        sealed class TsvEntry {
            public string Term,Reading,Definition;
            public int Order;
        }
        sealed class TsvIndex {
            readonly ILookup<string,TsvEntry> headings;
            readonly bool bundled;
            public TsvIndex(Source source){
                bundled=source.Kind=="bundled";
                if(bundled){
                    using(var zip=ZipFile.OpenRead(source.Path))
                    using(var input=new StreamReader(zip.GetEntry("lookup.tsv").Open(),Wire.Utf8))
                        headings=Read(input,true).ToLookup(e=>Hira(e.Term),StringComparer.Ordinal);
                }else{
                    using(var input=new StreamReader(source.Path,Wire.Utf8))
                        headings=Read(input,false).ToLookup(e=>Hira(e.Term),StringComparer.Ordinal);
                }
            }
            static IEnumerable<TsvEntry> Read(StreamReader input,bool packed){
                int order=0;string line;
                while((line=input.ReadLine())!=null){
                    string[] cells=line.Split('\t');
                    if(cells.Length!=3)continue;
                    string kana=Hira(cells[1]);
                    var terms=packed?cells[0].Split('\u001f'):new[]{cells[0]};
                    foreach(string term in terms)yield return new TsvEntry{Term=term,Reading=kana,Definition=cells[2],Order=order};
                    order++;
                }
            }
            public IEnumerable<string> ExactReadings(string term){
                return headings[Hira(term)].Where(entry=>entry.Term==term&&(!bundled||entry.Reading.Length!=0)).Select(entry=>entry.Reading);
            }
            public IEnumerable<string> Lookup(Selection word){
                string reading=Hira(word.Kana),lemma=Hira(word.Base),form=Hira(word.Form);
                var entries=headings[lemma];
                if(form!=lemma)entries=entries.Concat(headings[form]).OrderBy(entry=>entry.Order);
                var matched=entries.Where(entry=>entry.Term==word.Base||entry.Term==word.Form||
                        (lemma==reading&&Hira(entry.Term)==reading));
                if(bundled){
                    // In a Japanese sentence, 得体 (えたい) must not pick the Chinese
                    // headword 得体. An unvoiced Chinese-only key still resolves.
                    var japanese=matched.Where(e=>e.Reading.Length!=0&&(reading.Length==0||e.Reading==reading)).Take(60).ToList();
                    if(japanese.Count!=0||reading.Length!=0)matched=japanese;
                    else matched=matched.Where(e=>e.Reading.Length==0);
                    return matched.Take(60).SelectMany(e=>e.Definition.Split('\u001f'));
                }
                return matched.Where(e=>e.Reading.Length==0||reading.Length==0||e.Reading==reading)
                    .Take(60).Select(e=>e.Definition);
            }
        }
        static string Hira(string s){return new string((s??"").Select(c=>c>='ァ'&&c<='ヶ'?(char)(c-0x60):c).ToArray());}
        // Called only when the reader explicitly picks a word. A dictionary
        // must attest the whole spelling before suggesting a longer expression.
        public List<Selection> LongerEntries(List<Selection> spans){
            var candidates=spans.Where(s=>!String.IsNullOrEmpty(s.SurfaceKana)).ToList();
            var wanted=new HashSet<string>(candidates.SelectMany(s=>new[]{s.Form,s.Base}),StringComparer.Ordinal);
            var matched=new HashSet<string>(StringComparer.Ordinal);
            if(wanted.Count==0)return new List<Selection>();
            foreach(var src in All()){
                try{
                    if(src.Kind=="eb"){
                        lock(EbReader.Gate)foreach(string key in wanted)
                            if(Cached(src).Eb.HasExactHeading(key))matched.Add(key+"\t");
                    }else{
                        var index=IndexFor(src);
                        foreach(string term in wanted)foreach(string reading in index.ExactReadings(term))
                            matched.Add(term+"\t"+reading);
                    }
                }catch(IOException){}catch(UnauthorizedAccessException){}catch(TargetInvocationException){}
            }
            Func<string,string,bool> has=(term,kana)=>matched.Contains(term+"\t")||matched.Contains(term+"\t"+Hira(kana));
            var result=new List<Selection>();
            foreach(var s in candidates){
                if(has(s.Base,s.Kana)){result.Add(s);continue;}
                if(!has(s.Form,s.SurfaceKana))continue;
                s.Base=s.Form;s.Kana=s.SurfaceKana;s.Signature=s.LiteralKey;result.Add(s);
            }
            return result;
        }
        public List<Candidate> Lookup(Selection s,out string status){
            var results=new List<Candidate>();var errors=new List<string>();var used=new HashSet<string>();
            foreach(var src in All()){
                try{
                    IEnumerable<string> defs;
                    if(src.Kind=="eb"){lock(EbReader.Gate)defs=Cached(src).Eb.Lookup(s).ToArray();}
                    else defs=IndexFor(src).Lookup(s);
                    foreach(var d in defs)if(!String.IsNullOrWhiteSpace(d)&&used.Add(d))results.Add(new Candidate{Source=src.Name,Text=d});
                }catch(Exception ex){errors.Add(src.Name+" · "+ex.GetBaseException().Message);}
            }
            status=String.Join("\n",errors);return results;
        }
    }
    // Use the player's installed EBWin4 reader. The reader renders gaiji files into its data
    // directory, so give it an isolated writable cache instead of the dictionary's own files.
    class EbReader:IDisposable {
        readonly Type type;object reader;internal static readonly object Gate=new object();
        object Call(string name,params object[] args){return type.InvokeMember(name,BindingFlags.InvokeMethod,null,reader,args);}
        public EbReader(Source src,string root){
            try{
                if(!File.Exists(src.Reader)||!File.Exists(src.Path))throw new FileNotFoundException("找不到本机词典或 EBWin4。");
                string home=Path.GetDirectoryName(src.Reader),dll=Path.Combine(home,"EBLibCLR.dll");
                type=Assembly.LoadFrom(dll).GetType("EBLibCLR.Dictionary",true);reader=Activator.CreateInstance(type);
                Call("InitializeDictionary");
                string cache=Path.Combine(root,"dictionary-cache","ebwin");Directory.CreateDirectory(Path.Combine(cache,"files","000"));
                foreach(var name in new[]{"profile.ini","phonetic.ini","alternate.ini"}){string srcFile=Path.Combine(home,"res",name),target=Path.Combine(cache,name);if(File.Exists(srcFile)&&!File.Exists(target))File.Copy(srcFile,target);}
                Call("SetDataPath",cache);Call("AddCatalog",src.Path);
                if((int)Call("GetDictionaryCount")<1)throw new InvalidDataException("EBWin4 未能读取这个 CATALOG。");
                Call("SelectDictionary",0);Call("SetUseUnicode",1);Call("SetMaxHit",12);
            }catch{Dispose();throw;}
        }
        static string Plain(string html){return WebUtility.HtmlDecode(Regex.Replace(html,"<[^>]*>"," ")).Trim();}
        public bool HasExactHeading(string spelling){
            Call("SearchWord",spelling);int hits=(int)Call("GetCount");
            for(int i=0;i<Math.Min(8,hits);i++){
                Call("SelectItem",i);
                string html=(string)Call("GetText");
                var head=Regex.Match(html,"<span class=\"key\">(.*?)</span>",RegexOptions.Singleline);
                if(head.Success&&Plain(head.Groups[1].Value)==spelling)return true;
            }
            return false;
        }
        public List<string> Lookup(Selection s){
            var found=new List<string>();
            foreach(var query in new[]{s.Kana,s.Base}.Where(v=>!String.IsNullOrWhiteSpace(v)).Distinct()){
                Call("SearchWord",query);int hits=(int)Call("GetCount");
                for(int i=0;i<Math.Min(12,hits);i++){
                    Call("SelectItem",i);string html=(string)Call("GetText");
                    var head=Regex.Match(html,"<span class=\"key\">(.*?)</span>",RegexOptions.Singleline);
                    if(head.Success&&Plain(head.Groups[1].Value)!=query&&Plain(head.Groups[1].Value)!=s.Base)continue;
                    string lead=Regex.Split(html,@"<br\s*/?>",RegexOptions.IgnoreCase)[0];
                    if(hits>1&&!String.IsNullOrEmpty(s.Base)&&s.Base!=query&&!Plain(lead).Contains(s.Base))continue;
                    int previousCount=found.Count;
                    if(html.Contains("ZHONG_RI")){
                        var parts=Regex.Split(html,@"<b>\s*[０-９0-9①-⑳]+\s*</b>",RegexOptions.IgnoreCase);
                        foreach(string part in parts.Skip(parts.Length>1?1:0)){
                            var before=part.Split('¶')[0];
                            var alternatives=new List<string>();
                            foreach(string piece in Regex.Split(Regex.Replace(before,"<[^>]*>",""),@"[,，；.。]")){
                                var chars=Regex.Matches(Regex.Replace(piece,"<[^>]*>",""),"&#[0-9]+;").Cast<Match>().Select(m=>WebUtility.HtmlDecode(m.Value)).Where(c=>c.Length==1&&c[0]>='㐀'&&c[0]<='鿿');
                                string v=String.Concat(chars);if(v.Length>0&&v.Length<=100)alternatives.Add(v);
                            }
                            if(alternatives.Count>0)found.Add(String.Join("，",alternatives.Distinct()));
                        }
                    }
                    if(found.Count==previousCount){string raw=Regex.Replace(Plain(html),@"\s+"," ").Trim();if(raw.Length>0)found.Add(raw.Length>350?raw.Substring(0,350)+"…":raw);}
                }
                if(found.Count>0)break;
            }return found.Distinct().ToList();
        }
        public void Dispose(){
            if(reader!=null){try{Call("FinalizeDictionary");}finally{var d=reader as IDisposable;if(d!=null)d.Dispose();reader=null;}}
        }
    }
}
