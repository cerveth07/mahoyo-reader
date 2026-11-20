using System;
using System.IO;
using System.Linq;
using System.Globalization;
using System.Collections.Generic;
using System.Text;
using System.Threading;

namespace Mahoyo.Book {
    // Both processes read atomic snapshots. Writers take the named mutex before re-reading.
    abstract class StudyTsv {
        public readonly string PathName;
        readonly string mutexName,header;
        protected StudyTsv(string root,string file,string signature,string mutex){
            PathName=Path.Combine(root,file);header=signature;mutexName=mutex;
        }
        protected string[] ReadLines(){
            try{
                using(var file=new FileStream(PathName,FileMode.Open,FileAccess.Read,FileShare.Read|FileShare.Delete))
                using(var reader=new StreamReader(file,Wire.Utf8)){
                    if(reader.ReadLine()!=header)throw new InvalidDataException("无法识别 "+Path.GetFileName(PathName)+" 格式。");
                    return reader.ReadToEnd().Split('\n');
                }
            }catch(FileNotFoundException){return new string[0];}
        }
        protected void Rewrite(Func<IEnumerable<string>> change){
            using(var mutex=new Mutex(false,mutexName)){
                try{if(!mutex.WaitOne(2000))throw new IOException("文件正忙，请重试。");}
                catch(AbandonedMutexException){}
                try{
                    var text=new StringBuilder(header).Append('\n');
                    foreach(var row in change())text.Append(row).Append('\n');
                    FavoritesStore.Write(PathName,Wire.Utf8.GetBytes(text.ToString()),true);
                }finally{mutex.ReleaseMutex();}
            }
        }
    }
    sealed class SentenceNotes:StudyTsv {
        public SentenceNotes(string root):base(root,"sentence-notes.tsv","mahoyo-notes-v1",@"Local\MahoyoYomibitoNotesV1"){}
        static string Escape(string text){return text.Replace("\\","\\\\").Replace("\t","\\t").Replace("\n","\\n");}
        static bool Decode(string encoded,out string value){
            var result=new StringBuilder();value="";
            for(int i=0;i<encoded.Length;i++){
                char c=encoded[i];
                if(c=='\t')return false;
                if(c!='\\'){result.Append(c);continue;}
                if(++i==encoded.Length)return false;
                switch(encoded[i]){
                    case 'n':result.Append('\n');break;
                    case 't':result.Append('\t');break;
                    case '\\':result.Append('\\');break;
                    default:return false;
                }
            }
            value=result.ToString();return true;
        }
        public Dictionary<int,string> Read(){
            bool malformed;
            return Read(out malformed);
        }
        Dictionary<int,string> Read(out bool malformed){
            malformed=false;
            var notes=new Dictionary<int,string>();
            foreach(string raw in ReadLines()){
                string line=raw.TrimEnd('\r');int tab=line.IndexOf('\t'),key;string text;
                if(tab>0&&Int32.TryParse(line.Substring(0,tab),NumberStyles.None,CultureInfo.InvariantCulture,out key)
                    &&Decode(line.Substring(tab+1),out text)&&text.Length>0){
                    if(notes.ContainsKey(key))malformed=true;
                    notes[key]=text;
                }else if(line.Length!=0)malformed=true;
            }
            return notes;
        }
        public void Save(int key,string value){
            if(key<0)throw new ArgumentOutOfRangeException("key");
            Rewrite(()=>{
                bool malformed;
                var notes=Read(out malformed);
                if(malformed)FavoritesStore.WriteRecovery(PathName,File.ReadAllBytes(PathName));
                value=value.Replace("\r\n","\n").Replace('\r','\n');
                if(value.Length==0)notes.Remove(key);else notes[key]=value;
                return notes.OrderBy(pair=>pair.Key).Select(pair=>pair.Key.ToString(CultureInfo.InvariantCulture)+"\t"+Escape(pair.Value));
            });
        }
        public static string Get(IDictionary<int,string> values,int key){
            string text;return values!=null&&values.TryGetValue(key,out text)?text:"";
        }
    }
    sealed class Lookup {
        public Selection Word;
        public long Time;
        public string Kana{get{return String.IsNullOrEmpty(Word.SurfaceKana)?Word.Kana:Word.SurfaceKana;}}
        public bool Matches(Selection s){return Word.Row==s.Row&&Word.A==s.A&&Word.B==s.B;}
    }
    sealed class RecentStore:StudyTsv {
        readonly Corpus corpus;
        public RecentStore(string root,Corpus data):base(root,"recent-lookups.tsv","mahoyo-recent-v1",@"Local\MahoyoYomibitoRecentV1"){corpus=data;}
        public List<Lookup> Read(){
            var entries=new List<Lookup>();
            var seen=new HashSet<Tuple<int,int,int>>();
            foreach(string line in ReadLines()){
                string[] f=line.Split('\t');int row,a,b;long time;
                if(f.Length!=4||!Int32.TryParse(f[0],out row)||!Int32.TryParse(f[1],out a)||!Int32.TryParse(f[2],out b)
                    ||!Int64.TryParse(f[3],out time)||time<0||time>253402300799L||row<0||row>=corpus.Rows.Length
                    ||a<0||b<=a||b>corpus.Rows[row].Ja.Length)continue;
                entries.Add(new Lookup{Word=corpus.Select(row,a,b),Time=time});
            }
            return entries.OrderByDescending(x=>x.Time).Where(x=>seen.Add(Tuple.Create(x.Word.Row,x.Word.A,x.Word.B))).Take(500).ToList();
        }
        public void Remove(Selection word){
            Rewrite(()=>Read().Where(x=>word!=null&&!x.Matches(word)).Select(x=>String.Format(CultureInfo.InvariantCulture,
                "{0}\t{1}\t{2}\t{3}",x.Word.Row,x.Word.A,x.Word.B,x.Time)));
        }
        public List<string[]> Export(){
            var rows=new List<string[]>{new[]{"查看时间","词条","读音","释义","剧情键","日文原句","中文译文"}};
            foreach(var hit in Read()){
                var s=hit.Word;var line=corpus.Rows[s.Row];
                var time=new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddSeconds(hit.Time);
                rows.Add(new[]{time.ToString("u"),s.Form,hit.Kana,s.Hint,s.Row.ToString(),line.Ja,line.Zh});
            }
            return rows;
        }
    }
}
