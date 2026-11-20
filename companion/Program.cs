using System;
using System.IO;
using System.Linq;
using System.Diagnostics;
using System.Globalization;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Mahoyo.Book {
    static class Program {
        [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern uint RegisterWindowMessage(string text);
        [DllImport("user32.dll")]static extern bool PostMessage(IntPtr h,uint msg,IntPtr a,IntPtr b);
        [DllImport("user32.dll")]static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32.dll")]static extern bool IsWindow(IntPtr h);
        [DllImport("user32.dll",EntryPoint="SetWindowLongPtrW")]static extern IntPtr Owner64(IntPtr window,int index,IntPtr value);
        [DllImport("user32.dll",EntryPoint="SetWindowLongW")]static extern int Owner32(IntPtr window,int index,int value);
        [DllImport("user32.dll")]static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")]static extern short GetAsyncKeyState(int key);
        [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
        [DllImport("user32.dll")]static extern bool AllowSetForegroundWindow(uint process);
        [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool SetProp(IntPtr window,string name,IntPtr value);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern uint GetPrivateProfileInt(string section,string key,int fallback,string path);
        static IntPtr owner;

        sealed class StartupTiming {
            readonly Stopwatch clock=Stopwatch.StartNew();
            readonly double beforeClock;
            readonly string mode,path;
            public StartupTiming(string root,string mode,bool cold){
                this.mode=(cold?"cold/":"warm/")+mode;path=Path.Combine(root,"book-timing.log");
                if(cold)using(var process=Process.GetCurrentProcess())beforeClock=(DateTime.UtcNow-process.StartTime.ToUniversalTime()).TotalMilliseconds-clock.Elapsed.TotalMilliseconds;
            }
            public void Stage(string stage){
                double elapsed=clock.Elapsed.TotalMilliseconds+beforeClock;
                string line=String.Format(CultureInfo.InvariantCulture,"{0:O} pid={1} mode={2} {3} {4:F1} ms{5}",
                    DateTime.UtcNow,ProcessId,mode,stage,elapsed,Environment.NewLine);
                try{File.AppendAllText(path,line);}
                catch(IOException){}
                catch(UnauthorizedAccessException){}
            }
            static readonly int ProcessId=ReadProcessId();
            static int ReadProcessId(){using(var process=Process.GetCurrentProcess())return process.Id;}
        }
        sealed class LoadingForm:PanelForm {
            public LoadingForm(string title,int width,int height):base(title,width,height){
                Row(new Label{Text="正在载入…",ForeColor=ReaderStyle.Dim,Font=ReaderStyle.Hint,Height=30});
            }
        }
        internal static string ModRoot{get;private set;}
        public static bool HasGame{get{return owner!=IntPtr.Zero&&IsWindow(owner);}}
        public static void MarkOriginal(Selection s){if(HasGame)PostMessage(owner,RegisterWindowMessage("MahoyoYomibito.MarkOriginal.v1"),new IntPtr(s.Row),new IntPtr(s.A|(s.B<<16)));}
        internal static void NotesChanged(){if(HasGame)PostMessage(owner,RegisterWindowMessage("MahoyoYomibito.SentenceNotesChanged.v1"),IntPtr.Zero,IntPtr.Zero);}
        public static void Changed(){if(owner!=IntPtr.Zero&&IsWindow(owner))PostMessage(owner,RegisterWindowMessage("MahoyoYomibito.PersonalChanged.v1"),IntPtr.Zero,IntPtr.Zero);}
        sealed class Resident:ApplicationContext,IMessageFilter {
            readonly string root,local;
            readonly Dictionaries dict;
            readonly Control dispatcher=new Control();
            readonly Timer lifetime=new Timer{Interval=2000},release=new Timer{Interval=15};
            readonly Stopwatch idle=Stopwatch.StartNew();
            readonly BookMailbox mailbox;
            Task<Notebook> loading;
            LibraryForm library;
            Form current;
            string[] pending;
            bool cold=true;
            int generation;
            public Resident(string path,Dictionaries dictionaries){
                root=path;local=Path.Combine(root,"local");dict=dictionaries;
                dispatcher.CreateControl();
                mailbox=new BookMailbox(root,Queue);
                release.Tick+=(s,e)=>{if((GetAsyncKeyState(1)&0x8000)==0){release.Stop();if(current!=null)current.Close();}};
                lifetime.Tick+=(s,e)=>{
                    if((owner!=IntPtr.Zero&&!IsWindow(owner))||idle.Elapsed>=TimeSpan.FromMinutes(30))ExitThread();
                };
                Application.AddMessageFilter(this);lifetime.Start();
            }
            public bool PreFilterMessage(ref Message m){
                if(m.Msg>=0x100&&m.Msg<=0x109||m.Msg>=0x200&&m.Msg<=0x20e)idle.Restart();
                return false;
            }
            public void Queue(string[] args){dispatcher.BeginInvoke(new Action(()=>Open(args)));}
            void Own(Form form){
                if(!HasGame)return;
                if(IntPtr.Size==8)Owner64(form.Handle,-8,owner);else Owner32(form.Handle,-8,owner.ToInt32());
                SetProp(form.Handle,"MahoyoYomibito.StudyBook.v1",owner);
            }
            T Prepare<T>(T form) where T:Form {
                form.HandleCreated+=(s,e)=>Own(form);
                form.Activated+=(s,e)=>{if(HasGame)PostMessage(owner,RegisterWindowMessage("MahoyoYomibito.UpdateReadingButtons.v1"),IntPtr.Zero,IntPtr.Zero);};
                form.FormClosing+=(s,e)=>{
                    if(current!=form||e.Cancel||e.CloseReason==CloseReason.WindowsShutDown||
                       e.CloseReason==CloseReason.ApplicationExitCall)return;
                    e.Cancel=true;
                    if(HasGame&&(GetAsyncKeyState(1)&0x8000)!=0){release.Start();return;}
                    if(form==library&&!library.FlushNotes())return;
                    ++generation;current=null;form.Hide();idle.Restart();
                    if(form!=library)dispatcher.BeginInvoke(new Action(form.Dispose));
                    if(pending!=null){var next=pending;pending=null;Queue(next);return;}
                    if(HasGame){
                        uint pid;GetWindowThreadProcessId(owner,out pid);AllowSetForegroundWindow(pid);
                        if(!PostMessage(owner,RegisterWindowMessage("MahoyoYomibito.BookClosed.v1"),IntPtr.Zero,IntPtr.Zero))SetForegroundWindow(owner);
                    }
                };
                return form;
            }
            void ShowForm(Form form){
                if(current!=null&&current!=form){
                    current.Hide();
                    if(current!=library)current.Dispose();
                }
                current=form;form.Show();Own(form);form.Activate();form.Refresh();
            }
            void ModalClosed(object sender,FormClosedEventArgs e){
                if(pending==null)return;
                var next=pending;pending=null;Queue(next);
            }
            async void Open(string[] args){
                if(args.Length==0)args=new[]{"words"};
                if(current!=null&&!current.Enabled){
                    pending=args;var modal=Form.ActiveForm;
                    if(modal!=null&&modal!=current){
                        modal.FormClosed-=ModalClosed;modal.FormClosed+=ModalClosed;modal.Activate();
                    }
                    return;
                }
                if(current is EditorForm||current is PickerForm){
                    pending=args;current.Activate();return;
                }
                idle.Restart();int request=++generation;
                long h;if(args.Length>1&&Int64.TryParse(args[1],out h))owner=new IntPtr(h);
                string mode=args[0];bool sentences=mode=="sentences"||mode=="export-sentences";
                bool pick=mode=="pick",edit=mode=="edit";
                var timing=GetPrivateProfileInt("study","log_enabled",0,Path.Combine(root,"settings.ini"))!=0?
                    new StartupTiming(root,mode,cold):null;
                cold=false;
                try{
                    if(mode=="dict"||mode=="legend"){
                        ShowForm(mode=="dict"?(Form)Prepare(new SourcesForm(dict)):Prepare(new LegendForm()));
                        if(timing!=null){timing.Stage("frame");timing.Stage("ready");}return;
                    }
                    if(!pick&&!edit){
                        if(library==null)library=Prepare(new LibraryForm(dict,sentences));
                        ShowForm(library);
                    }else if(loading==null||!loading.IsCompleted)
                        ShowForm(Prepare(new LoadingForm(pick?"选择词界":"补释义",pick?484:512,pick?582:624)));
                    if(timing!=null)timing.Stage("frame");
                    if(loading==null||loading.IsFaulted){
                        loading=Task.Run(()=>{
                            var data=new Corpus(local,timing==null?null:new Action<string>(timing.Stage));
                            var notebook=new Notebook(root,data);notebook.EnsureEdition();return notebook;
                        });
                    }
                    var book=await loading;
                    if(request!=generation||dispatcher.IsDisposed)return;
                    if(pick){ShowForm(Prepare(new PickerForm(book,dict,int.Parse(args[2]),args.Length>3?int.Parse(args[3]):-1,args.Length>4?int.Parse(args[4]):-1)));}
                    else if(edit){
                        var selection=book.Data.Select(int.Parse(args[2]),int.Parse(args[3]),int.Parse(args[4]));
                        ShowForm(Prepare(new EditorForm(book,dict,selection,book.Find(selection))));
                    }else{
                        library.OpenPage(book,sentences);library.Refresh();
                    }
                    if(timing!=null){timing.Stage("notebook");timing.Stage("ready");}
                    if(mode=="export-sentences")library.ExportAll();
                }catch(Exception ex){
                    if(request!=generation)return;
                    if(timing!=null)timing.Stage("failed/"+ex.GetBaseException().GetType().Name);
                    if(current==library&&library!=null)library.LoadFailed();
                    MessageBox.Show(current,ex.GetBaseException().Message,"魔夜读本",MessageBoxButtons.OK,MessageBoxIcon.Information);
                    if(current is LoadingForm)current.Close();
                }
            }
            protected override void Dispose(bool disposing){
                if(disposing){
                    ++generation;Application.RemoveMessageFilter(this);
                    release.Dispose();lifetime.Dispose();mailbox.Dispose();
                    if(current!=null&&current!=library)current.Dispose();
                    if(library!=null){library.FlushNotes();library.Dispose();}dict.Dispose();dispatcher.Dispose();
                }
                base.Dispose(disposing);
            }
        }
        internal static int Run(string[] args){
            if(args.Length==3&&args[0]=="--make-font"){GameType.Make(args[1],args[2]);return 0;}
            SetProcessDPIAware();Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            string local=AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
            StoryTypeface.Load(local);
            string root=ModRoot=Directory.GetParent(local).FullName;string mode=args.Length>0?args[0]:"words";
            var dictionaries=new Dictionaries(root);
            if(mode=="--link-local"&&args.Length==3){dictionaries.Link(args[1],args[2]);return 0;}
            if(mode=="--import"&&args.Length==2){dictionaries.Import(args[1]);return 0;}
            if(args.Length==0)args=new[]{"words"};
            if(BookMailbox.Forward(root,args))return 0;
            using(var mutex=new System.Threading.Mutex(false,BookMailbox.MutexName(root))){
                var wait=Stopwatch.StartNew();
                for(;;){
                    bool owns;
                    try{owns=mutex.WaitOne(0);}catch(System.Threading.AbandonedMutexException){owns=true;}
                    if(owns){
                        try{using(var resident=new Resident(root,dictionaries)){resident.Queue(args);Application.Run(resident);}}
                        finally{mutex.ReleaseMutex();}
                        return 0;
                    }
                    if(BookMailbox.Forward(root,args))return 0;
                    if(wait.ElapsedMilliseconds>=5000)throw new IOException("已有读本未响应，请稍后重试。");
                    System.Threading.Thread.Sleep(20);
                }
            }
        }
        [STAThread]static int Main(string[] args){
            // Keep the full dictionary hash in its filename even in a deeply nested game library.
            AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);
            AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
            try{return Run(args);}catch(Exception ex){
                if(!args.Any(a=>a.StartsWith("--")))MessageBox.Show(ex.GetBaseException().Message,"魔夜读本",MessageBoxButtons.OK,MessageBoxIcon.Information);
                else Console.Error.WriteLine(ex.GetBaseException().Message);
                return 1;
            }
        }
    }
}
