using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace Mahoyo.Book {
    static class Wire {
        internal static readonly Encoding Utf8 = new UTF8Encoding(false,true);
        internal static string Text(this BinaryReader r) {
            int n=r.ReadInt32();
            if(n<0 || n>1000000) throw new InvalidDataException("词库字段长度无效。");
            byte[] b=r.ReadBytes(checked(n*2));
            if(b.Length!=n*2) throw new EndOfStreamException();
            return Encoding.Unicode.GetString(b);
        }
        internal static void Text(this BinaryWriter w,string s) {
            w.Write(s.Length); w.Write(Encoding.Unicode.GetBytes(s));
        }
        internal static void Header(this BinaryReader r,string tag) {
            if(Encoding.ASCII.GetString(r.ReadBytes(8))!=tag) throw new InvalidDataException("无法识别数据格式。");
        }
        internal static int Count(this BinaryReader r,int max) {
            int n=r.ReadInt32(); if(n<0 || n>max)throw new InvalidDataException("文件记录数无效。"); return n;
        }
        internal static string Hash(byte[] bytes) {
            using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();
        }
        internal static void Atomic(string file,Action<FileStream> write,bool backup) {
            string tmp=file+"."+Guid.NewGuid().ToString("N")+".tmp";
            try {
                using(var f=new FileStream(tmp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){write(f);f.Flush(true);}
                if(File.Exists(file)) File.Replace(tmp,file,backup?file+".bak":null);
                else File.Move(tmp,file);
            } finally {if(File.Exists(tmp))File.Delete(tmp);}
        }
    }
    class Card { public int A,B; public string Term,Kana,Pos,Meaning,Level; }
    struct Token { public int A,B,Lex,Reading; }
    class Sentence { public string Ja,Zh; public Card[] Cards; public Token[] Tokens; }
    class Lex { public string Base,Kana,Pos,Key; }
    class Selection {
        public int Row,A,B; public string Form,Base,Kana,Pos,Signature,Hint,SurfaceKana;
        public bool CanRepeat {get{return !String.IsNullOrEmpty(Signature)&&!String.IsNullOrEmpty(Kana);}}
        public override string ToString(){return Form+(Form==Base?"":"　→　"+Base);}
        public string LiteralKey {get{return String.IsNullOrEmpty(SurfaceKana)?"":"F"+Form+"\u001f"+SurfaceKana;}}
    }
    class Corpus {
        public Sentence[] Rows; public Lex[] Lexemes; public string[] Readings; public string Digest;
        static readonly Regex Ruby=new Regex(@"<([^<>|]+)\|[^<>]+>",RegexOptions.Compiled);
        static readonly Regex Control=new Regex(@"<[^<>]*>|@o|\^|\r|\n",RegexOptions.Compiled);
        public Corpus(string local,Action<string> stage=null) {
            var data=File.ReadAllBytes(Path.Combine(local,"story.dat"));Digest=Wire.Hash(data);
            using(var r=new BinaryReader(new MemoryStream(data))) {
                r.Header("MHJL0002");Rows=new Sentence[r.Count(100000)];
                for(int i=0;i<Rows.Length;i++){
                    if(r.ReadInt32()!=i)throw new InvalidDataException("剧情索引无效。");
                    var s=new Sentence{Ja=Control.Replace(Ruby.Replace(r.Text(),"$1"),""),Zh=r.Text()};
                    s.Cards=new Card[r.Count(4000)];
                    for(int j=0;j<s.Cards.Length;j++){
                        s.Cards[j]=new Card{A=r.ReadInt32(),B=r.ReadInt32(),Term=r.Text(),Kana=r.Text(),Pos=r.Text(),Meaning=r.Text(),Level=r.Text()};r.Text();
                    }
                    Rows[i]=s;
                }
            }
            if(stage!=null)stage("corpus");
            using(var r=new BinaryReader(File.OpenRead(Path.Combine(local,"word-index.dat")))) {
                r.Header("MHLEX001");if(r.Text()!=Digest)throw new InvalidDataException("剧情和选词索引版本不匹配。");
                Lexemes=new Lex[r.Count(200000)];
                for(int i=0;i<Lexemes.Length;i++){var l=new Lex{Base=r.Text(),Kana=r.Text(),Pos=r.Text()};l.Key=l.Base+"\u001f"+l.Kana+"\u001f"+l.Pos;Lexemes[i]=l;}
                Readings=new string[r.Count(200000)];for(int i=0;i<Readings.Length;i++)Readings[i]=r.Text();
                if(r.ReadInt32()!=Rows.Length)throw new InvalidDataException();
                foreach(var s in Rows){
                    s.Tokens=new Token[r.Count(5000)];int previous=0;
                    for(int j=0;j<s.Tokens.Length;j++){
                        var t=new Token{A=r.ReadUInt16(),B=r.ReadUInt16(),Lex=r.ReadInt32(),Reading=r.ReadInt32()};
                        if(t.A<previous||t.B<=t.A||t.B>s.Ja.Length||t.Lex<0||t.Lex>=Lexemes.Length||t.Reading<0||t.Reading>=Readings.Length)throw new InvalidDataException("选词位置无效。");
                        s.Tokens[j]=t;previous=t.B;
                    }
                }
                if(r.BaseStream.Position!=r.BaseStream.Length)throw new InvalidDataException();
            }
            if(stage!=null)stage("word-index");
        }
        internal static string Gloss(string value) {
            return string.Join("\n",value.Split('\n').Where(s=>!s.StartsWith("辞書形：")&&!s.StartsWith("原作注音：")&&!String.IsNullOrWhiteSpace(s))).Trim();
        }
        internal static bool Kana(string s){return !String.IsNullOrEmpty(s)&&Regex.IsMatch(s,@"^[ぁ-ゖァ-ヺー]+$");}
        internal Selection Select(int id,int a,int b) {
            if(id<0||id>=Rows.Length)throw new ArgumentOutOfRangeException("id");
            var row=Rows[id];
            if(a<0||b<=a||b>row.Ja.Length)throw new ArgumentException("请在日文原句中选中一个词。");
            var s=new Selection{Row=id,A=a,B=b,Form=row.Ja.Substring(a,b-a),Hint="",Pos="",Kana="",Signature="",SurfaceKana=""};s.Base=s.Form;
            var card=row.Cards.Where(c=>c.A==a&&c.B==b).OrderByDescending(c=>!String.IsNullOrEmpty(Gloss(c.Meaning))).FirstOrDefault();
            var ts=row.Tokens.Where(t=>t.A>=a&&t.B<=b).ToList();
            bool aligned=ts.Count>0&&ts[0].A==a&&ts[ts.Count-1].B==b;
            for(int j=1;j<ts.Count;j++)if(ts[j-1].B!=ts[j].A)aligned=false;
            if(aligned){
                s.SurfaceKana=String.Concat(ts.Select(t=>Readings[t.Reading]));
                int headCount=ts.Count;
                while(headCount>1&&IsEnding(Lexemes[ts[headCount-1].Lex]))headCount--;
                if(headCount<ts.Count&&Inflects(Lexemes[ts[headCount-1].Lex]))ts.RemoveRange(headCount,ts.Count-headCount);
                var last=Lexemes[ts.Last().Lex];var head=ts[0];
                s.Base=row.Ja.Substring(head.A,ts.Last().A-head.A)+last.Base;
                s.Kana=String.Concat(ts.Take(ts.Count-1).Select(t=>Readings[t.Reading]))+last.Kana;
                s.Pos=ts.Count==1?last.Pos:"短语";
                if(Kana(s.Kana))s.Signature="T"+String.Join("\u001e",ts.Select(t=>Lexemes[t.Lex].Key));
                if(card==null&&ts.Count==1)card=row.Cards.FirstOrDefault(c=>c.A==a&&c.B==ts[0].B&&(c.Level=="N1"||c.Level=="N2"));
            }
            if(card!=null){
                s.Hint=Gloss(card.Meaning);
                if(Kana(card.Kana)&&(!s.CanRepeat||(card.A==a&&card.B==b&&card.Kana!=s.SurfaceKana&&(card.Pos.Contains("人名")||card.Level=="原作")))){
                    s.Base=s.Form;s.Kana=card.Kana;s.SurfaceKana=card.Kana;s.Pos=card.Pos;s.Signature=CardKey(card);
                }
            }
            return s;
        }
        internal List<Selection> Units(int id){
            var row=Rows[id];var units=new List<Selection>();
            for(int i=0;i<row.Tokens.Length;i++){
                var head=row.Tokens[i];
                int end=head.B;
                if(Inflects(Lexemes[head.Lex])){
                    while(i+1<row.Tokens.Length){
                        var next=row.Tokens[i+1];var lex=Lexemes[next.Lex];
                        if(next.A!=end||!IsEnding(lex))break;
                        end=next.B;i++;
                    }
                }
                units.Add(Select(id,head.A,end));
            }
            return units;
        }
        internal static bool Inflects(Lex lex){return lex.Pos=="動詞"||lex.Pos=="形容詞";}
        internal static bool IsEnding(Lex lex){return lex.Pos=="助動詞"||lex.Pos=="助詞"&&(lex.Base=="て"||lex.Base=="で");}
        internal List<Selection> ScopeOptions(int id,int caret,List<Selection> units){
            var choices=new List<Selection>();int index=units.FindIndex(u=>u.A<=caret&&caret<u.B);
            if(index<0)return choices;
            var unit=units[index];
            foreach(var card in Rows[id].Cards.Where(c=>c.A<=caret&&caret<c.B&&c.B-c.A>unit.B-unit.A).OrderByDescending(c=>c.B-c.A))
                if(!choices.Any(s=>s.A==card.A&&s.B==card.B))choices.Add(Select(id,card.A,card.B));
            choices.Add(unit);
            // A noun + case particle + verb is a useful *optional* selection.
            // The reader decides whether this construction belongs on one card.
            for(int j=Math.Max(0,index-2);j<=index&&j+2<units.Count;j++){
                var left=units[j];var particle=units[j+1];var right=units[j+2];
                if(left.B==particle.A&&particle.B==right.A&&(left.Pos=="名詞"||left.Pos=="代名詞")&&
                   particle.Pos=="助詞"&&("をにへとで".Contains(particle.Form))&&right.Pos=="動詞")
                    choices.Add(Select(id,left.A,right.B));
            }
            return choices;
        }
        internal List<Selection> NearbyPhrases(int id,int caret,List<Selection> units){
            var output=new List<Selection>();int at=units.FindIndex(u=>u.A<=caret&&caret<u.B);
            if(at<0)return output;
            for(int left=Math.Max(0,at-4);left<=at;left++){
                for(int right=left+1;right<units.Count&&right<left+5;right++){
                    if(units[right-1].B!=units[right].A||units[right].B-units[left].A>22)break;
                    if(right>=at)output.Add(Select(id,units[left].A,units[right].B));
                }
            }
            return output.OrderByDescending(s=>s.B-s.A).ToList();
        }
        internal static string CardKey(Card c){return "C"+c.Term+"\u001f"+c.Kana+"\u001f"+c.Pos;}
    }
}
