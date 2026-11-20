using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;
using System.Collections.Generic;

namespace Mahoyo.Book {
    static class BookTests {
        static void Check(bool ok,string name){if(!ok)throw new Exception(name);}
        static Selection Word(Corpus c,int row,string text){int a=c.Rows[row].Ja.IndexOf(text,StringComparison.Ordinal);Check(a>=0,"fixture "+text);return c.Select(row,a,a+text.Length);}
        static void Entry(ZipArchive z,string name,string value){using(var w=new StreamWriter(z.CreateEntry(name).Open(),Wire.Utf8))w.Write(value);}
        static void FavoritesRoundTrip(string root,Corpus data,Selection word){
            string path=Path.Combine(root,"favorites.tsv");
            var store=new FavoritesStore(root,data);
            byte[] original=File.ReadAllBytes(path);
            var removed=store.Remove(word.Row);
            Check(removed!=null&&removed.Words.Single().Form==word.Form&&store.Read().Single().Row==3293,"remove marked favourite");
            Check(File.ReadAllBytes(path+".bak").SequenceEqual(original),"first backup keeps BOM, CRLF and malformed rows");
            Check(store.Restore(removed)&&store.Read().First().Words.Single().Form==word.Form,"restore marked favourite");
            Check(File.ReadAllText(path)=="mahoyo-favorites-v1\n138\t"+word.A+"\t"+word.B+"\n3293\n","native favourite wire format");

            File.AppendAllText(path,"138\n3293\t0\t99999\n");
            original=File.ReadAllBytes(path);
            removed=store.Remove(word.Row);
            // Windows also matches the plain ".bak" with "bak.*", so keep only numbered backups.
            var backups=Directory.GetFiles(root,"favorites.tsv.bak.*").Where(f=>Path.GetFileName(f)!="favorites.tsv.bak").ToArray();
            Check(backups.Length==1&&File.ReadAllBytes(backups[0]).SequenceEqual(original),"numbered backup preserves later malformed rows");

            // An independently owned handle represents the game's mutex on another thread.
            using(var held=new Mutex(false,@"Local\MahoyoYomibitoFavoritesV1")){
                Check(held.WaitOne(2000),"acquire native favourites mutex");
                Exception failure=null;bool finished;
                byte[] before=File.ReadAllBytes(path);
                var attempt=new Thread(()=>{
                    try{store.Restore(removed);}
                    catch(Exception ex){failure=ex;}
                }){IsBackground=true};
                try{attempt.Start();finished=attempt.Join(2500);}
                finally{held.ReleaseMutex();}
                Check(finished&&failure is IOException&&File.ReadAllBytes(path).SequenceEqual(before),"managed writer times out while native mutex is held");
            }
            Check(store.Restore(removed)&&store.Read().Count==2,"restore succeeds after mutex release");
            // A concurrent game-side re-save must survive undo and deduplicate shared marks.
            removed=store.Remove(word.Row);
            int other=word.A==0&&word.B==1?1:0;
            File.AppendAllText(path,"138\t"+other+"\t"+(other+1)+"\t"+word.A+"\t"+word.B+"\n");
            Check(store.Restore(removed)&&store.Read().First().Words.Count==2,"restore merges new game marks");
        }
        [STAThread] public static int Main(string[] args){
            string temp=Path.Combine(Path.GetTempPath(),"mahoyo-book-qa-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(temp);
            try{
                Check(StoryTypeface.Load(args[0]),"registered bundled story font");
                Check(StoryTypeface.Small.Name.IndexOf("Noto Serif SC",StringComparison.OrdinalIgnoreCase)>=0 &&
                    StoryTypeface.Detail.Name==StoryTypeface.Small.Name,"Noto Serif SC Medium on both Chinese story sizes");
                string[] forwarded=null;
                Check(BookMailbox.MutexName(temp)==BookMailbox.MutexName(temp.ToUpperInvariant())&&
                    BookMailbox.MutexName(temp)!=BookMailbox.MutexName(Path.Combine(temp,"other")),
                    "resident lock follows the case-insensitive installation path");
                using(var mailbox=new BookMailbox(temp,values=>forwarded=values)){
                    string[] request={"edit","0","138","1","3"};
                    Check(BookMailbox.Forward(temp,request)&&request.SequenceEqual(forwarded),"signed message-only window transfers editor selection");
                    Check(!BookMailbox.Forward(temp,new[]{"--import","0"}),"interactive mailbox accepts only reader actions");
                }
                var c=new Corpus(args[0]);Check(c.Rows.Length==24134,"full script");var book=new Notebook(temp,c);
                var mi=Word(c,138,"見上げ");var again=Word(c,3293,"見上げる");
                Check(mi.Signature==again.Signature&&mi.Kana=="みあげる","verb lemma and reading");
                Check(Word(c,1894,"見上げて").Signature==mi.Signature,"editing the full te-form reopens the same word");
                var line=c.Units(4948);var lastKana=c.ScopeOptions(4948,9,line)[0];
                Check(lastKana.Form=="気遣って"&&lastKana.Base=="気遣う"&&lastKana.Kana=="きづかう"&&lastKana.Hint.Contains("担心"),"clicking the ending selects the full Anki verb and keeps its source gloss");
                var noun=c.Rows[4948].Ja.IndexOf("声");var combo=c.ScopeOptions(4948,noun,line).First(x=>x.Form=="声をかける");
                var pastVoice=Word(c,4454,"声をかけた");Check(combo.Signature==pastVoice.Signature,"multi-word selection normalizes the inflected final verb");
                Check(c.ScopeOptions(4948,c.Rows[4948].Ja.IndexOf("。"),line).Count==0,"punctuation is not a proposed word");
                Check(c.ScopeOptions(4944,5,c.Units(4944))[0].Kana=="みけん"&&c.ScopeOptions(4930,2,c.Units(4930))[0].Kana=="あおざき","screenshot readings");
                Check(Word(c,1787,"頷い").Signature==Word(c,7859,"うなずい").Signature,"kana and kanji variants");
                var futo=Word(c,1877,"ふと");Check(futo.CanRepeat,"unmarked kana selectable");
                Check(c.ScopeOptions(1877,futo.B-1,c.Units(1877))[0].Form=="ふと","explicit click in an unmarked kana word selects its boundaries");
                Check(Word(c,4224,"取り戻した").Base=="取り戻す","compound verb and past tense");
                var phrase=Word(c,1877,"目を向けれ");Check(phrase.CanRepeat,"phrase token boundaries");
                book.Save(mi,"抬头看，仰望",false);book.Save(again,"本句里看天花板",true);
                book.Save(futo,"不经意间",false);book.Save(phrase,"把目光转向",false);
                var reopened=new Notebook(temp,c);var list=reopened.Read();var hits=book.Hits(list);
                Check(list.Count==4&&reopened.Find(again).Row==3293,"separate scoped and global entries after restart");
                Check(hits.Any(h=>h.Row==1894&&list[h.Note].Base=="見上げる"),"recurrence on third page");
                Check(hits.Any(h=>h.Row==1877&&list[h.Note].Base=="ふと"),"saved kana word found on page");
                // A second writer changes the order and membership behind the first writer's cache.
                var localNote=reopened.Remove(reopened.Find(again).Id);
                var global=book.Find(mi);
                book.Master(global.Id,true);
                var indexed=File.ReadAllBytes(book.FileName);
                new Notebook(temp,c).Master(global.Id,true);
                Check(indexed.SequenceEqual(File.ReadAllBytes(book.FileName)),"cached index matches full rebuild after external removal");
                reopened.Restore(localNote);
                var mine=book.Find(mi);book.Remember(mine.Id,again);book.Remember(mine.Id,again);book.Master(mine.Id,true);
                Check(book.Find(mi).Mastered&&book.Find(mi).Examples.Count==2,"deduplicate examples and master");
                book.Master(mine.Id,false);book.Edit(mine.Id,"抬头看，仰望");File.Copy(book.FileName,Path.Combine(Path.GetDirectoryName(typeof(BookTests).Assembly.Location),"book-qa-personal.dat"),true);
                File.WriteAllText(Path.Combine(temp,"favorites.tsv"),"mahoyo-favorites-v1\r\n138\t"+mi.A+"\t"+mi.B+"\r\nbad row\r\n3293\r\n",new UTF8Encoding(true));
                var sentences=Exports.Sentences(temp,c);Check(sentences.Count==2&&sentences[0].Words[0].Form==mi.Form,"existing BOM CRLF favorites");
                var csv=Path.Combine(temp,"词句.csv");Exports.Write(csv,Exports.SentenceRows(sentences,c));Check(File.ReadAllText(csv).Contains(c.Rows[138].Zh),"sentence export bilingual");
                FavoritesRoundTrip(temp,c,mi);
                var annotations=new SentenceNotes(temp);string noteText="第一行\t义项\\n\n第二行\\末尾,\"例\"";
                annotations.Save(mi.Row,noteText);annotations.Save(3293,"另一条");
                var savedNotes=new SentenceNotes(temp).Read();
                Check(savedNotes[mi.Row]==noteText&&savedNotes[3293]=="另一条","notes escape round trip");
                Check(File.ReadAllLines(annotations.PathName).Length==3&&!File.ReadAllText(annotations.PathName).Contains("\r"),"one physical LF row per note");
                File.AppendAllText(annotations.PathName,"9\tbad\\x\r\n");
                var damagedNotes=File.ReadAllBytes(annotations.PathName);
                annotations.Save(3293,"另一条");
                Check(File.ReadAllBytes(annotations.PathName+".bak").SequenceEqual(damagedNotes)
                    &&annotations.Read()[mi.Row]==noteText,"note save preserves malformed original and other valid notes");
                var favStore=new FavoritesStore(temp,c);var removedLine=favStore.Remove(mi.Row);
                Check(removedLine!=null&&annotations.Read()[mi.Row]==noteText&&favStore.Restore(removedLine),"unfavorite preserves notes for undo");
                var noteRows=Exports.SentenceRows(favStore.Read(),c,savedNotes);
                Check(noteRows[0].SequenceEqual(new[]{"剧情键","日文原句","中文译文","标记词","读音","批注"})&&noteRows[1][5]==noteText,"append notes after original columns");
                string noteCsv=Path.Combine(temp,"notes.csv"),noteTsv=Path.Combine(temp,"notes.tsv");
                Exports.Write(noteCsv,noteRows);Exports.Write(noteTsv,noteRows);
                Check(File.ReadAllText(noteCsv).Contains("\""+noteText.Replace("\"","\"\"")+"\"")&&File.ReadAllLines(noteTsv)[1].Split('\t').Length==6,"escaped CSV and single-line TSV notes");
                annotations.Save(mi.Row,"");Check(!annotations.Read().ContainsKey(mi.Row)&&annotations.Read()[3293]=="另一条","clear one note independently");
                using(var editor=new SentenceNoteEditor()){
                    editor.Save=annotations.Save;editor.Bind(mi.Row,"");
                    var noteInput=editor.Controls.OfType<ReaderField>().Single().Controls.OfType<System.Windows.Forms.TextBox>().Single();
                    noteInput.Text="800 毫秒后保存";
                    var clock=System.Diagnostics.Stopwatch.StartNew();
                    while(clock.ElapsedMilliseconds<1600&&!annotations.Read().ContainsKey(mi.Row)){
                        System.Windows.Forms.Application.DoEvents();Thread.Sleep(15);
                    }
                    Check(annotations.Read()[mi.Row]==noteInput.Text,"note editor autosaves after typing stops");
                    noteInput.Text="换句时保存";editor.Bind(3293,"另一条");
                    Check(annotations.Read()[mi.Row]=="换句时保存"&&noteInput.Text=="另一条","switching notes flushes the old row");
                }
                var history=new RecentStore(temp,c);
                var valid=Enumerable.Range(0,c.Rows.Length).Where(i=>c.Rows[i].Ja.Length>0).Take(505).ToArray();
                File.WriteAllText(history.PathName,"mahoyo-recent-v1\nbad\n-1\t0\t1\t1\n"+
                    String.Join("\n",valid.Select(i=>i+"\t0\t1\t"+i))+"\n"+valid[0]+"\t0\t1\t90000\n",Wire.Utf8);
                var recent=history.Read();
                Check(recent.Count==500&&recent[0].Time==90000&&recent.Select(x=>x.Word.Row).Distinct().Count()==500,"recent cap, latest duplicate, malformed rows");
                history.Remove(recent[0].Word);
                Check(history.Export().Count==500&&history.Read().All(x=>x.Word.Row!=valid[0]),"recent atomic removal and export");
                history.Remove(null);Check(history.Read().Count==0,"clear recent history");
                var markedKana=new FavoriteLine{Row=futo.Row};markedKana.Words.Add(new Example{Row=futo.Row,A=futo.A,B=futo.B,Form=futo.Form});
                Check(Exports.SentenceRows(new[]{markedKana},c)[1][4]=="ふと","reading export for a marked kana word without an existing card");
                book.Edit(mine.Id,"逗号,引号\"与\n换行");Exports.Write(csv,Exports.WordRows(book.Read(),c));
                Check(File.ReadAllText(csv).Contains("\"逗号,引号\"\"与\n换行\""),"CSV quoting and multiline");
                var tsv=Path.ChangeExtension(csv,".tsv");Exports.Write(tsv,Exports.WordRows(book.Read(),c));Check(File.ReadAllText(tsv).Contains("与 / 换行"),"TSV one record per line");
                string bundledRoot=Path.Combine(temp,"shogakukan-player");
                Directory.CreateDirectory(Path.Combine(bundledRoot,"local"));
                File.Copy(Path.Combine(args[0],"shogakukan.zip"),Path.Combine(bundledRoot,"local","shogakukan.zip"));
                using(var bundled=new Dictionaries(bundledRoot)){
                    string message;
                    Check(bundled.All().Single().Kind=="bundled","included dictionary available before any configuration");
                    Check(bundled.Lookup(mi,out message).Any(g=>g.Text.Contains("抬头看"))&&message=="","built-in 見上げる lookup");
                    var etai=new Selection{Base="得体",Form="得体",Kana="えたい"};
                    var etaiSenses=bundled.Lookup(etai,out message);
                    Check(etaiSenses.First().Text.Contains("本来面目")&&!etaiSenses.Any(g=>g.Text.Contains("適切")),"Japanese 得体 excludes the homographic Chinese headword");
                    Check(bundled.Lookup(new Selection{Base="苹果",Form="苹果",Kana=""},out message).Any(g=>g.Text.Contains("リンゴ")),"Chinese-Japanese side of included dictionary");
                    var bridge=new Selection{Base="橋",Form="橋",Kana="はし"};
                    var chopsticks=new Selection{Base="箸",Form="箸",Kana="はし"};
                    Check(bundled.Lookup(bridge,out message).Any(g=>g.Text.Contains("桥"))&&bundled.Lookup(chopsticks,out message).Any(g=>g.Text.Contains("筷")),"orthography distinguishes homophones");
                    Check(!File.Exists(Path.Combine(bundledRoot,"dictionary-sources.xml")),"built-in lookup needs no local dictionary configuration");
                }
                Console.WriteLine("Bundled Shogakukan lookup passed without an external reader or user dictionary configuration.");
                var d=new Dictionaries(temp);Check(d.Import(args[1])>1000,"optional dictionary");
                string status;Check(d.Lookup(mi,out status).Any(g=>g.Text.Contains("仰")||g.Text.Contains("抬头")),"actual optional gloss for 見上げる");
                var y=Path.Combine(temp,"mock.zip");using(var z=ZipFile.Open(y,ZipArchiveMode.Create)){Entry(z,"index.json","{\"title\":\"自用词典\",\"format\":3}");Entry(z,"term_bank_1.json","[[\"見上げる\",\"みあげる\",\"\",\"\",1,[{\"type\":\"structured-content\",\"content\":{\"tag\":\"span\",\"content\":\"自订义项\"}}],1,\"\"]]");}
                Check(d.Import(y)==1&&d.Lookup(mi,out status).Any(g=>g.Text=="自订义项"),"Yomitan structured gloss");
                var input=Path.Combine(temp,"自用词.tsv");File.WriteAllText(input,"词条\t读音\t释义\n見上げる\tみあげる\t向上看\n",new UTF8Encoding(true));
                Check(d.Import(input)==1&&d.Lookup(mi,out status).Any(g=>g.Text=="向上看"),"custom TSV");int before=d.All().Count;
                string expression=Path.Combine(temp,"expressions.tsv");
                File.WriteAllText(expression,"なんとなく\tなんとなく\t总觉得\n",new UTF8Encoding(true));
                var other=Path.Combine(temp,"scope-dictionary");Directory.CreateDirectory(other);
                var phraseDictionary=new Dictionaries(other);phraseDictionary.Import(expression);
                int middle=c.Rows[632].Ja.IndexOf("なんとなく")+2;
                var longer=phraseDictionary.LongerEntries(c.NearbyPhrases(632,middle,c.Units(632))).Single(s=>s.Form=="なんとなく");
                Check(longer.Base=="なんとなく"&&longer.Kana=="なんとなく"&&longer.Signature.StartsWith("F"),"dictionary-attested adverb across several tokens keeps its own spelling and reading");
                var phraseBook=new Notebook(other,c);phraseBook.Save(longer,"总觉得",false);
                File.WriteAllText(expression,"ミル\tみる\tvariant\n表形\t\tform\nみる\tみる\tlemma\nみる\tべつ\twrong\n",Wire.Utf8);
                phraseDictionary.Import(expression);
                var senses=phraseDictionary.Lookup(new Selection{Base="みる",Form="表形",Kana="ミル"},out status);
                Check(senses.Select(x=>x.Text).SequenceEqual(new[]{"variant","form","lemma"}),"indexed lookup preserves TSV order, kana aliases and reading filters");
                var marked=phraseBook.Hits(phraseBook.Read());
                Check(marked.Count>1&&marked.All(h=>c.Rows[h.Row].Ja.Substring(h.A,h.B-h.A)=="なんとなく")&&phraseBook.Find(Word(c,632,"なんとなく"))!=null,"literal expression recurs only as a complete word and reopens");
                File.WriteAllText(input,"broken");try{d.Import(input);}catch(InvalidDataException){}Check(d.All().Count==before,"failed import atomic");
                string eb=Environment.GetEnvironmentVariable("MAHOYO_TEST_EBWIN"),cat=Environment.GetEnvironmentVariable("MAHOYO_TEST_CATALOG");
                if(!String.IsNullOrEmpty(eb)&&File.Exists(cat)){
                    d.Link(cat,eb);var actual=d.Lookup(mi,out status);Check(actual.Any(g=>g.Source.Contains("統合")&&g.Text.Contains("抬头看")&&g.Text.Length<70),"local bilingual dictionary: "+status+" "+String.Join("/",actual.Select(g=>g.Text)));
                    Console.WriteLine("Local EBWin4 Japanese-Chinese lookup passed.");
                }
                using(var f=new EditorForm(book,d,mi)){
                    var panel=f.Controls.OfType<System.Windows.Forms.FlowLayoutPanel>().Single();var title=panel.Controls[0];
                    Check(f.Font.Name==ReaderStyle.BodyFont.Name&&title.Font.Name==ReaderStyle.Headword.Name,"separate Chinese interface and Japanese headword faces");
                    Check(title.Height>=System.Windows.Forms.TextRenderer.MeasureText(title.Text,title.Font).Height+8,"headword has room to render the Japanese typeface");
                    Check(panel.Controls.OfType<PassageView>().Single().AccessibleDescription==c.Rows[mi.Row].Ja,"editor preserves the original sentence");
                    var foot=f.Controls.OfType<System.Windows.Forms.Panel>().Single(x=>x.Controls.OfType<System.Windows.Forms.FlowLayoutPanel>().Any());
                    var editorCommands=foot.Controls.OfType<System.Windows.Forms.FlowLayoutPanel>().Single().Controls.OfType<System.Windows.Forms.Button>().Select(x=>x.Text);
                    Check(editorCommands.Contains("保存释义")&&!editorCommands.Any(x=>x.Contains("词典")),"definition editor offers saving without a dictionary management button");
                    f.ClientSize=new System.Drawing.Size(365,327);
                    Check(foot.Bottom<=f.ClientSize.Height&&foot.Top>=panel.Bottom&&foot.Height>=38,"saving stays visible above the window edge at laptop size");
                }
                using(var f=new PickerForm(book,d,1877)){}using(var f=new SourcesForm(d)){}
                foreach(bool sentenceTab in new[]{false,true})using(var f=new LibraryForm(d,sentenceTab)){
                    var favorites=new FavoritesStore(temp,c);
                    f.LoadData(book,favorites,book.Read(),favorites.Read(),null);
                    var tabs=f.Controls.OfType<System.Windows.Forms.FlowLayoutPanel>().Single().Controls.OfType<System.Windows.Forms.Panel>()
                        .Single(p=>p.Controls.OfType<LibraryTab>().Count()==3);
                    foreach(int width in new[]{305,700}){
                        tabs.Width=width;tabs.PerformLayout();
                        var cells=tabs.Controls.Cast<System.Windows.Forms.Control>().ToList();
                        Check(cells.Select(x=>x.Text).SequenceEqual(new[]{"生词本","收藏夹","最近查过"}),"reader navigation contains only the three library tabs");
                        Check(cells.All(x=>tabs.ClientRectangle.Contains(x.Bounds))&&cells.All(x=>cells.All(otherCell=>
                            x==otherCell||!x.Bounds.IntersectsWith(otherCell.Bounds))),"all three tabs fit the reader");
                    }
                    var panes=f.Controls.OfType<System.Windows.Forms.FlowLayoutPanel>().Single()
                        .Controls.OfType<System.Windows.Forms.Panel>().SelectMany(p=>p.Controls.OfType<LibraryPane>()).Single();
                    var entries=panes.Controls.OfType<ReaderField>().Single(p=>p.Controls.OfType<LibraryList>().Any());
                    var detail=panes.Controls.OfType<ReaderField>().Single(p=>p.Controls.OfType<LibraryDetails>().Any());
                    panes.Dock=System.Windows.Forms.DockStyle.None;
                    foreach(int width in new[]{390,770}){
                        panes.Width=(int)(width*ReaderStyle.Scale(panes));panes.PerformLayout();
                        Check(panes.ClientRectangle.Contains(entries.Bounds)&&panes.ClientRectangle.Contains(detail.Bounds)&&
                              (width<620?entries.Bottom<detail.Top:entries.Right<detail.Left),"library fits list and details in narrow and wide layouts");
                    }
                }
                using(var f=new LegendForm()){
                    Check(f.Text.EndsWith("功能说明")&&f.ClientSize.Height>=1.35*f.ClientSize.Width,"guide opens in portrait");
                    Check(f.Region!=null&&!f.Region.IsVisible(0,0)&&f.Region.IsVisible(f.ClientSize.Width/2,2),"rounded guide corners");
                    var rows=f.Controls.OfType<System.Windows.Forms.FlowLayoutPanel>().Single().Controls.OfType<GuideRow>().ToList();
                    Check(rows.Select(x=>x.AccessibleName).SequenceEqual(new[]{"阅读模式 · F8","只看对应译文","读音与音调","最近查过","补释义与点词","管理离线词典","收藏与批注"}),"guide covers reading, lookup, dictionary management and personal notes");
                    foreach(int width in new[]{365,630})foreach(var row in rows){
                        row.Width=(int)(width*ReaderStyle.Scale(row));row.PerformLayout();
                        var labels=row.Controls.OfType<System.Windows.Forms.Label>().ToArray();
                        Check(labels.All(x=>!x.AutoEllipsis&&row.ClientRectangle.Contains(x.Bounds))&&
                              (width<520?labels[0].Bottom<=labels[1].Top:labels[0].Right<=labels[1].Left),"guide instructions fit portrait and wide layouts without ellipses");
                    }
                }
                book.Delete(mine.Id);Check(!book.Read().Any(n=>n.Id==mine.Id),"remove only selected word");
                Console.WriteLine("PASS all 24134 indexed rows, kana point selection, inflected and fixed compound matching, scope, persistence, both exports, ZIP/Yomitan/TSV import and five Windows views.");
                return 0;
            }catch(Exception e){Console.Error.WriteLine(e.ToString());return 1;}
            finally{if(Path.GetDirectoryName(temp)==Path.GetTempPath().TrimEnd(Path.DirectorySeparatorChar)&&Path.GetFileName(temp).StartsWith("mahoyo-book-qa-"))Directory.Delete(temp,true);}
        }
    }
}
