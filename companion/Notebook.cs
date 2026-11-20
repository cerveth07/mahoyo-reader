using System;
using System.IO;
using System.ComponentModel;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Text;
using System.Linq;
using System.Threading;
using System.Collections.Generic;
using System.Web.Script.Serialization;

namespace Mahoyo.Book {
    class Example {public int Row,A,B;public string Form;}
    class Note {
        public string Id,Base,Kana,Pos,Meaning,Signature;
        public bool Mastered;
        public int Row=-1,A,B;
        public string Form;
        public List<Example> Examples=new List<Example>();
        public bool Same(Selection s,bool local){
            return local?Row==s.Row&&A==s.A&&B==s.B&&Form==s.Form:Row<0&&(Signature==s.Signature||Signature==s.LiteralKey);
        }
        public void Remember(Selection s){
            if(!Examples.Any(e=>e.Row==s.Row&&e.A==s.A&&e.B==s.B))Examples.Add(new Example{Row=s.Row,A=s.A,B=s.B,Form=s.Form});
        }
    }
    class Hit {public int Row,A,B,Note;public string Reading;}
    class Notebook {
        public readonly string FileName;
        public readonly Corpus Data;
        static readonly JavaScriptSerializer Json=new JavaScriptSerializer{MaxJsonLength=32*1024*1024};
        public Notebook(string root,Corpus data){FileName=Path.Combine(root,"personal-words.dat");Data=data;}
        public List<Note> Read(out string revision){
            revision=Data.Digest;if(!File.Exists(FileName))return new List<Note>();
            using(var f=new FileStream(FileName,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete))
            using(var r=new BinaryReader(f)){
                r.Header("MHUSER01");revision=r.Text();int size=r.Count(32*1024*1024);
                var bytes=r.ReadBytes(size);if(bytes.Length!=size)throw new EndOfStreamException();
                var list=Json.Deserialize<List<Note>>(Wire.Utf8.GetString(bytes));
                if(list==null||list.Any(n=>n==null||String.IsNullOrEmpty(n.Id)||String.IsNullOrWhiteSpace(n.Meaning)||n.Examples==null))throw new InvalidDataException("生词本文件不完整。");
                return list;
            }
        }
        public List<Note> Read(){string rev;return Read(out rev);}
        public void Change(Action<List<Note>> edit){
            string name=Wire.Hash(Wire.Utf8.GetBytes(Path.GetFullPath(FileName).ToUpperInvariant()));
            using(var mutex=new Mutex(false,@"Local\MahoyoVocab-"+name)){
                try {if(!mutex.WaitOne(6000))throw new IOException("生词本正忙，请重试。");}
                catch(AbandonedMutexException){}
                try {var list=Read();edit(list);Write(list);} finally {mutex.ReleaseMutex();}
            }
        }
        public List<Note> EnsureEdition(){
            string revision;var notes=Read(out revision);
            if(revision!=Data.Digest)Change(latest=>notes=latest);
            return notes;
        }
        public Note Find(Selection s){return Find(Read(),s);}
        internal static Note Find(List<Note> notes,Selection s){
            return notes.FirstOrDefault(n=>n.Same(s,true))??notes.FirstOrDefault(n=>s.CanRepeat&&n.Same(s,false));
        }
        public void Save(Selection s,string meaning,bool local){
            meaning=meaning.Trim();if(meaning.Length==0||meaning.Length>4000)throw new ArgumentException("请输入 1 到 4000 字的释义。");
            local=local||!s.CanRepeat;
            Change(list=>{
                var n=list.FirstOrDefault(x=>x.Same(s,local));
                if(n==null){n=new Note{Id=Guid.NewGuid().ToString("N"),Base=s.Base,Kana=s.Kana,Pos=s.Pos,Signature=s.Signature,Row=local?s.Row:-1,A=s.A,B=s.B,Form=s.Form};list.Add(n);}
                n.Meaning=meaning;n.Remember(s);
            });
        }
        public void Remember(string id,Selection s){Change(list=>list.First(n=>n.Id==id).Remember(s));}
        public void Master(string id,bool value){Change(list=>list.First(n=>n.Id==id).Mastered=value);}
        public void Delete(string id){Remove(id);}
        public Note Remove(string id){
            Note removed=null;
            Change(list=>{removed=list.Find(n=>n.Id==id);if(removed!=null)list.Remove(removed);});
            return removed;
        }
        public void Restore(Note removed){
            Change(list=>{
                if(!list.Any(n=>n.Id==removed.Id||(removed.Row<0?n.Row<0&&n.Signature==removed.Signature:
                    n.Row==removed.Row&&n.A==removed.A&&n.B==removed.B&&n.Form==removed.Form)))list.Add(removed);
            });
        }
        public void Edit(string id,string value){if(String.IsNullOrWhiteSpace(value)||value.Length>4000)throw new ArgumentException("请输入 1 到 4000 字的释义。");Change(list=>list.First(n=>n.Id==id).Meaning=value.Trim());}
        public List<Hit> Hits(List<Note> list){
            var hits=new List<Hit>();if(list.Count==0)return hits;
            var first=new Dictionary<int,List<KeyValuePair<int,int[]>>>();
            var heads=Data.Lexemes.Select((l,i)=>new {l.Key,i}).ToDictionary(x=>x.Key,x=>x.i);
            var cards=new Dictionary<string,int>();
            var fixedForms=new Dictionary<char,List<int>>();
            var local=new Dictionary<int,List<int>>();
            for(int i=0;i<list.Count;i++){
                var n=list[i];if(n.Row>=0){if(!local.ContainsKey(n.Row))local[n.Row]=new List<int>();local[n.Row].Add(i);continue;}
                if(String.IsNullOrEmpty(n.Signature))continue;
                if(n.Signature[0]=='C'){cards[n.Signature]=i;continue;}
                if(n.Signature[0]=='F'&&!String.IsNullOrEmpty(n.Base)){
                    char initial=n.Base[0];if(!fixedForms.ContainsKey(initial))fixedForms[initial]=new List<int>();
                    fixedForms[initial].Add(i);continue;
                }
                var keys=n.Signature.Substring(1).Split('\u001e');if(keys.Any(k=>!heads.ContainsKey(k)))continue;
                var seq=keys.Select(k=>heads[k]).ToArray();
                if(!first.ContainsKey(seq[0]))first[seq[0]]=new List<KeyValuePair<int,int[]>>();
                first[seq[0]].Add(new KeyValuePair<int,int[]>(i,seq));
            }
            for(int id=0;id<Data.Rows.Length;id++){
                var r=Data.Rows[id];var where=new Dictionary<long,Hit>();
                Action<int,int,int,string> add=(a,b,n,y)=>{where[((long)a<<32)|(uint)b]=new Hit{Row=id,A=a,B=b,Note=n,Reading=y};};
                for(int k=0;k<r.Tokens.Length;k++){
                    var t=r.Tokens[k];
                    List<int> literal;
                    if(fixedForms.TryGetValue(r.Ja[t.A],out literal))foreach(int note in literal){
                        var n=list[note];int end=t.A+n.Base.Length;
                        if(end>r.Ja.Length||String.CompareOrdinal(r.Ja,t.A,n.Base,0,n.Base.Length)!=0)continue;
                        int j=k;string y=Data.Readings[t.Reading];
                        while(r.Tokens[j].B<end&&j+1<r.Tokens.Length&&r.Tokens[j].B==r.Tokens[j+1].A)y+=Data.Readings[r.Tokens[++j].Reading];
                        if(r.Tokens[j].B==end&&y==n.Kana)add(t.A,end,note,y);
                    }
                    List<KeyValuePair<int,int[]>> choices;if(!first.TryGetValue(t.Lex,out choices))continue;
                    foreach(var choice in choices){
                        var seq=choice.Value;int j=1;
                        while(j<seq.Length&&k+j<r.Tokens.Length&&r.Tokens[k+j].Lex==seq[j]&&r.Tokens[k+j-1].B==r.Tokens[k+j].A)j++;
                        if(j!=seq.Length)continue;
                        var last=Data.Lexemes[r.Tokens[k+j-1].Lex];
                        if(Corpus.Inflects(last)){
                            while(k+j<r.Tokens.Length&&r.Tokens[k+j-1].B==r.Tokens[k+j].A){
                                var next=Data.Lexemes[r.Tokens[k+j].Lex];
                                if(!Corpus.IsEnding(next))break;
                                j++;
                            }
                        }
                        string y=String.Concat(r.Tokens.Skip(k).Take(j).Select(x=>Data.Readings[x.Reading]));
                        add(t.A,r.Tokens[k+j-1].B,choice.Key,y);
                    }
                }
                if(cards.Count>0)foreach(var c in r.Cards){int n;if(cards.TryGetValue(Corpus.CardKey(c),out n))add(c.A,c.B,n,c.Kana);}
                List<int> own;if(local.TryGetValue(id,out own))foreach(int i in own){
                    var n=list[i];if(n.A>=0&&n.B>n.A&&n.B<=r.Ja.Length&&r.Ja.Substring(n.A,n.B-n.A)==n.Form){
                        var c=r.Cards.FirstOrDefault(x=>x.A==n.A&&x.B==n.B);add(n.A,n.B,i,c==null?n.Kana:c.Kana);
                    }
                }
                hits.AddRange(where.Values.OrderBy(h=>h.A).ThenBy(h=>h.B));
            }
            return hits;
        }
        // Hit.Note is an offset into the ordered note list. Snapshot matching fields by
        // value so a removal, reorder, or edit by another writer invalidates the index.
        object[] indexedRules;
        List<Hit> indexedHits;
        void Write(List<Note> list){
            var meta=Wire.Utf8.GetBytes(Json.Serialize(list));
            var rules=list.Select(n=>new {n.Id,n.Row,n.A,n.B,n.Form,n.Signature,n.Base,n.Kana}).ToArray();
            if(indexedRules==null||!rules.SequenceEqual<object>(indexedRules)){
                indexedHits=Hits(list);
                indexedRules=rules;
            }
            var hits=indexedHits;
            Wire.Atomic(FileName,f=>{using(var w=new BinaryWriter(f,Encoding.UTF8,true)){
                w.Write(Encoding.ASCII.GetBytes("MHUSER01"));w.Text(Data.Digest);w.Write(meta.Length);w.Write(meta);
                w.Write(list.Count);foreach(var n in list){w.Text(n.Base);w.Text(n.Kana);w.Text(n.Meaning);w.Write(n.Mastered?1:0);w.Write(n.Row>=0?1:0);}
                w.Write(hits.Count);foreach(var h in hits){w.Write(h.Row);w.Write(h.A);w.Write(h.B);w.Write(h.Note);w.Text(h.Reading);}
            }},true);
        }
    }
    class FavoriteLine {public int Row; public List<Example> Words=new List<Example>();}
    sealed class FavoritesStore {
        const string Header="mahoyo-favorites-v1";
        static readonly Regex Number=new Regex(@"\G[ \t\r\v\f]*([+-]?[0-9]+)");
        readonly string path;
        readonly Corpus data;

        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        static extern bool MoveFileEx(string source,string destination,uint flags);
        [DllImport("kernel32.dll")]static extern uint GetCurrentProcessId();

        public string PathName{get{return path;}}
        public FavoritesStore(string root,Corpus data){
            path=Path.Combine(root,"favorites.tsv");this.data=data;
        }
        static bool AtEnd(string line,int offset){
            for(int i=offset;i<line.Length;i++)if(" \t\r\v\f".IndexOf(line[i])<0)return false;
            return true;
        }
        static bool ReadNumber(string line,ref int offset,out int value){
            var match=Number.Match(line,offset);value=0;
            if(!match.Success||!Int32.TryParse(match.Groups[1].Value,NumberStyles.AllowLeadingSign,CultureInfo.InvariantCulture,out value))return false;
            offset+=match.Length;return true;
        }
        List<FavoriteLine> Read(out byte[] recovery){
            recovery=null;var entries=new List<FavoriteLine>();byte[] bytes;
            try{
                using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete))
                using(var reader=new BinaryReader(file)){
                    long size=file.Length;
                    if(size>8*1024*1024)throw new IOException("收藏文件过大。");
                    bytes=reader.ReadBytes((int)size);
                    if(bytes.Length!=size)throw new EndOfStreamException("收藏文件读取不完整。");
                }
            }catch(FileNotFoundException){return entries;}
            // The native reader treats the file as bytes and permits an optional UTF-8 BOM.
            int bom=bytes.Length>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf?3:0;
            var rows=Encoding.GetEncoding(28591).GetString(bytes,bom,bytes.Length-bom).Split('\n');
            string header=rows[0];
            if(header.EndsWith("\r",StringComparison.Ordinal))header=header.Substring(0,header.Length-1);
            if(header!=Header)throw new InvalidDataException("无法识别收藏夹格式。");
            var seen=new HashSet<int>();
            for(int i=1;i<rows.Length;i++){
                string line=rows[i];int at=0,key;
                if(line.Trim(' ','\t','\r').Length==0)continue;
                if(!ReadNumber(line,ref at,out key)||key<0||key>=data.Rows.Length||seen.Contains(key)){
                    recovery=bytes;continue;
                }
                string ja=data.Rows[key].Ja;
                var entry=new FavoriteLine{Row=key};bool valid=true;
                while(!AtEnd(line,at)){
                    int a,b;
                    if(!ReadNumber(line,ref at,out a)||!ReadNumber(line,ref at,out b)||a<0||b<=a||b>ja.Length){valid=false;break;}
                    entry.Words.Add(new Example{Row=key,A=a,B=b,Form=ja.Substring(a,b-a)});
                }
                if(valid){seen.Add(key);entries.Add(entry);}else recovery=bytes;
            }
            return entries;
        }
        public List<FavoriteLine> Read(){byte[] recovery;return Read(out recovery);}
        public List<FavoriteLine> TryRead(out string error){
            error=null;
            try{return Read();}
            catch(InvalidDataException ex){error=ex.Message;}
            catch(IOException ex){error=ex.Message;}
            catch(UnauthorizedAccessException ex){error=ex.Message;}
            return new List<FavoriteLine>();
        }
        internal static void Write(string destination,byte[] bytes,bool replace){
            const uint replaceExisting=1,writeThrough=8;
            string temp=destination+"."+GetCurrentProcessId().ToString(CultureInfo.InvariantCulture)+".tmp";
            try{
                using(var file=new FileStream(temp,FileMode.Create,FileAccess.Write,FileShare.None)){
                    file.Write(bytes,0,bytes.Length);file.Flush(true);
                }
                if(!MoveFileEx(temp,destination,writeThrough|(replace?replaceExisting:0)))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }finally{if(File.Exists(temp))File.Delete(temp);}
        }
        internal static void WriteRecovery(string path,byte[] original){
            string backup=path+".bak";
            if(File.Exists(backup)||Directory.Exists(backup))
                backup+="."+Guid.NewGuid().ToString("N");
            Write(backup,original,false);
        }
        bool Change(Func<List<FavoriteLine>,bool> edit){
            using(var mutex=new Mutex(false,@"Local\MahoyoYomibitoFavoritesV1")){
                try{if(!mutex.WaitOne(250))throw new IOException("收藏夹正忙，请重试。");}
                catch(AbandonedMutexException){}
                try{
                    byte[] recovery;var entries=Read(out recovery);
                    if(!edit(entries))return false;
                    if(recovery!=null)WriteRecovery(path,recovery);
                    var output=new StringBuilder(Header).Append('\n');
                    foreach(var entry in entries){
                        output.Append(entry.Row.ToString(CultureInfo.InvariantCulture));
                        foreach(var word in entry.Words)output.Append('\t').Append(word.A.ToString(CultureInfo.InvariantCulture))
                            .Append('\t').Append(word.B.ToString(CultureInfo.InvariantCulture));
                        output.Append('\n');
                    }
                    Write(path,Wire.Utf8.GetBytes(output.ToString()),true);
                    return true;
                }finally{mutex.ReleaseMutex();}
            }
        }
        public FavoriteLine Remove(int row){
            FavoriteLine removed=null;
            Change(entries=>{
                removed=entries.Find(x=>x.Row==row);
                return removed!=null&&entries.Remove(removed);
            });
            return removed;
        }
        public bool Restore(FavoriteLine removed){
            return Change(entries=>{
                var entry=entries.Find(x=>x.Row==removed.Row);
                if(entry==null)entry=new FavoriteLine{Row=removed.Row};else entries.Remove(entry);
                foreach(var word in removed.Words)
                    if(!entry.Words.Any(x=>x.A==word.A&&x.B==word.B))entry.Words.Add(word);
                entries.Insert(0,entry);
                return true;
            });
        }
    }
    static class Exports {
        public static List<FavoriteLine> Sentences(string root,Corpus data){return new FavoritesStore(root,data).Read();}
        public static List<string[]> WordRows(IEnumerable<Note> notes,Corpus c){
            var rows=new List<string[]>{new[]{"词条","读音","词性","个人释义","范围","状态","剧情键","所选词形","日文例句","中文译文"}};
            foreach(var n in notes)foreach(var e in n.Examples.DefaultIfEmpty()){
                var r=e!=null&&e.Row>=0&&e.Row<c.Rows.Length?c.Rows[e.Row]:null;
                rows.Add(new[]{n.Base,n.Kana,n.Pos,n.Meaning,n.Row>=0?"仅本句":"全游戏",n.Mastered?"已掌握":"学习中",e==null?"":e.Row.ToString(),e==null?"":e.Form,r==null?"":r.Ja,r==null?"":r.Zh});
            }return rows;
        }
        public static string MarkedReading(Corpus corpus,Sentence row,Example word){
            var card=row.Cards.Where(v=>v.A==word.A&&v.B==word.B&&Corpus.Kana(v.Kana))
                .OrderByDescending(v=>v.Level=="原作").FirstOrDefault();
            if(card!=null)return card.Kana;
            var tokens=row.Tokens.Where(t=>t.A>=word.A&&t.B<=word.B).ToArray();
            if(tokens.Length==0||tokens.Sum(t=>t.B-t.A)!=word.B-word.A)return "";
            return String.Concat(tokens.Select(t=>corpus.Readings[t.Reading]));
        }
        public static List<string[]> SentenceRows(IEnumerable<FavoriteLine> list,Corpus c,IDictionary<int,string> notes=null){
            var rows=new List<string[]>{new[]{"剧情键","日文原句","中文译文","标记词","读音","批注"}};
            foreach(var f in list){
                var row=c.Rows[f.Row];
                var readings=f.Words.Select(word=>MarkedReading(c,row,word));
                rows.Add(new[]{f.Row.ToString(),row.Ja,row.Zh,String.Join(" / ",f.Words.Select(x=>x.Form)),String.Join(" / ",readings),SentenceNotes.Get(notes,f.Row)});
            }
            return rows;
        }
        public static void Write(string path,List<string[]> rows){
            Wire.Atomic(path,f=>{using(var w=new StreamWriter(f,new UTF8Encoding(true),4096,true)){
                bool tsv=String.Equals(Path.GetExtension(path),".tsv",StringComparison.OrdinalIgnoreCase);
                foreach(var row in rows)w.WriteLine(String.Join(tsv?"\t":",",row.Select(x=>tsv?(x??"").Replace("\t"," ").Replace("\r","").Replace("\n"," / "):"\""+(x??"").Replace("\"","\"\"")+"\"")));
            }},false);
        }
    }
}
