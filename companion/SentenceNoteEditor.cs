using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Mahoyo.Book {
    sealed class SentenceNoteEditor:Panel {
        readonly TextBox input=new ReaderTextBox{Multiline=true,AcceptsReturn=true,AcceptsTab=true,AccessibleName="批注",
            BorderStyle=BorderStyle.None,BackColor=ReaderStyle.Field,ForeColor=ReaderStyle.Body,Font=ReaderStyle.Hint};
        readonly Label hint=new Label{Text="写下你的理解、联想或疑问…",AutoEllipsis=true,
            BackColor=ReaderStyle.Field,ForeColor=ReaderStyle.Dim,Font=ReaderStyle.Hint};
        readonly Label title=new Label{Text="批注",ForeColor=ReaderStyle.Dim,Font=ReaderStyle.Small};
        readonly Label status=new Label{AutoSize=false,TextAlign=ContentAlignment.MiddleRight,Font=ReaderStyle.Small};
        readonly ReaderField field;
        readonly Timer debounce=new Timer{Interval=800},fade=new Timer{Interval=50};
        readonly Stopwatch elapsed=new Stopwatch();
        readonly Dictionary<int,string> drafts=new Dictionary<int,string>();
        int row=-1;
        bool loading;
        public Action<int,string> Save;
        [DllImport("user32.dll")]static extern bool SystemParametersInfo(uint action,uint param,out int value,uint flags);
        public SentenceNoteEditor(){
            BackColor=ReaderStyle.Field;
            field=new ReaderField(input,70);field.Controls.Add(hint);hint.BringToFront();
            hint.Click+=(s,e)=>input.Focus();Controls.AddRange(new Control[]{title,status,field});
            input.TextChanged+=(s,e)=>{
                hint.Visible=input.TextLength==0&&!input.Focused;
                if(loading||row<0)return;
                drafts[row]=input.Text.Replace("\r\n","\n");status.Text="";fade.Stop();
                debounce.Stop();debounce.Start();
            };
            input.Enter+=(s,e)=>hint.Visible=false;
            input.Leave+=(s,e)=>{hint.Visible=input.TextLength==0;Flush();};
            debounce.Tick+=(s,e)=>Flush();
            fade.Tick+=(s,e)=>{
                long ms=elapsed.ElapsedMilliseconds;
                if(ms>=1500){status.Text="";fade.Stop();return;}
                if(ms<1250)return;
                int animations;double t=SystemParametersInfo(0x1042,0,out animations,0)&&animations==0?0:(ms-1250)/250.0;
                var a=ReaderStyle.Dim;var b=ReaderStyle.Field;
                status.ForeColor=Color.FromArgb((int)(a.R+(b.R-a.R)*t),(int)(a.G+(b.G-a.G)*t),(int)(a.B+(b.B-a.B)*t));
            };
        }
        protected override void OnLayout(LayoutEventArgs e){
            base.OnLayout(e);if(field==null)return;
            int h=(int)Math.Round(24*ReaderStyle.Scale(this));
            title.SetBounds(0,0,Width/2,h);status.SetBounds(Width/2,0,Width-Width/2,h);
            field.SetBounds(0,h,Width,Math.Max(1,Height-h));hint.Bounds=input.Bounds;
        }
        public void Bind(int key,string value){
            if(key!=row){Flush();fade.Stop();status.Text="";}
            row=key;Visible=key>=0;
            string draft;if(drafts.TryGetValue(key,out draft))value=draft;
            value=(value??"").Replace("\n","\r\n");
            loading=true;
            try{if(input.Text!=value)input.Text=value;}
            finally{loading=false;}
            hint.Visible=input.TextLength==0&&!input.Focused;
        }
        public bool Flush(){
            debounce.Stop();
            foreach(var pair in drafts.ToArray()){
                try{
                    if(Save==null)return false;
                    Save(pair.Key,pair.Value);drafts.Remove(pair.Key);
                    if(row==pair.Key){status.Text="已保存";status.ForeColor=ReaderStyle.Dim;elapsed.Restart();fade.Start();}
                }catch(Exception ex){
                    if(!(ex is IOException||ex is UnauthorizedAccessException||ex is Win32Exception||ex is InvalidDataException))throw;
                    status.Text="保存失败，等待重试";status.ForeColor=ReaderStyle.Danger;fade.Stop();debounce.Start();return false;
                }
            }
            return true;
        }
        protected override void Dispose(bool disposing){if(disposing){debounce.Dispose();fade.Dispose();}base.Dispose(disposing);}
    }
}
