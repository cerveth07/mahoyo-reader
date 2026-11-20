using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Mahoyo.Book {
    sealed class BookMailbox:NativeWindow,IDisposable {
        const string ClassName="MahoyoYomibito.Book.Mailbox.v1";
        const int Signature=0x4d594201,CopyData=0x4a;
        static readonly IntPtr MessageOnly=new IntPtr(-3);
        [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
        struct WindowClass {
            public uint style;
            public IntPtr procedure;
            public int classExtra,windowExtra;
            public IntPtr instance,icon,cursor,background;
            public string menu,name;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct Packet {public IntPtr signature;public int bytes;public IntPtr text;}
        [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern ushort RegisterClass(ref WindowClass cls);
        [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern IntPtr CreateWindowEx(int ex,string cls,string title,int style,int x,int y,int w,int h,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr param);
        [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
        [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr SendMessageTimeout(IntPtr h,int message,IntPtr sender,ref Packet data,uint flags,uint timeout,out IntPtr result);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true)]static extern IntPtr GetProcAddress(IntPtr module,string name);
        [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
        [DllImport("user32.dll")]static extern bool AllowSetForegroundWindow(uint process);
        readonly Action<string[]> receive;
        public static string MutexName(string root){
            return @"Local\MahoyoYomibitoBookV1-"+Wire.Hash(Wire.Utf8.GetBytes(root.ToUpperInvariant()));
        }
        public BookMailbox(string root,Action<string[]> accept){
            receive=accept;
            var wc=new WindowClass{name=ClassName,instance=GetModuleHandle(null),procedure=GetProcAddress(GetModuleHandle("user32.dll"),"DefWindowProcW")};
            if(RegisterClass(ref wc)==0&&Marshal.GetLastWin32Error()!=1410)throw new System.ComponentModel.Win32Exception();
            IntPtr hwnd=CreateWindowEx(0,ClassName,root,0,0,0,0,0,MessageOnly,IntPtr.Zero,wc.instance,IntPtr.Zero);
            if(hwnd==IntPtr.Zero)throw new System.ComponentModel.Win32Exception();
            AssignHandle(hwnd);
        }
        public static bool Forward(string root,string[] args){
            IntPtr target=FindWindowEx(MessageOnly,IntPtr.Zero,ClassName,root);
            if(target==IntPtr.Zero)return false;
            uint pid;GetWindowThreadProcessId(target,out pid);AllowSetForegroundWindow(pid);
            string value=String.Join("\n",args);
            var packet=new Packet{signature=new IntPtr(Signature),bytes=2*value.Length,text=Marshal.StringToHGlobalUni(value)};
            try{
                IntPtr result;
                return SendMessageTimeout(target,CopyData,IntPtr.Zero,ref packet,2,3000,out result)!=IntPtr.Zero&&result==new IntPtr(1);
            }finally{Marshal.FreeHGlobal(packet.text);}
        }
        protected override void WndProc(ref Message m){
            if(m.Msg!=CopyData){base.WndProc(ref m);return;}
            if(m.LParam==IntPtr.Zero)return;
            var packet=(Packet)Marshal.PtrToStructure(m.LParam,typeof(Packet));
            if(packet.signature!=new IntPtr(Signature)||packet.text==IntPtr.Zero||packet.bytes<2||packet.bytes>512||(packet.bytes&1)!=0)return;
            string[] args=Marshal.PtrToStringUni(packet.text,packet.bytes/2).Split('\n');
            long number;
            if(args.Length>5||!new[]{"words","sentences","export-sentences","dict","legend","pick","edit"}.Contains(args[0])
                ||args.Skip(1).Any(x=>!Int64.TryParse(x,out number)))return;
            receive(args);m.Result=new IntPtr(1);
        }
        public void Dispose(){DestroyHandle();}
    }
}
