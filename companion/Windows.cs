using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Collections.Generic;

namespace Mahoyo.Book {
    class ReaderClose:Button {
        bool hot;
        public ReaderClose(){FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;TabStop=true;AccessibleName="关闭窗口";Cursor=Cursors.Hand;}
        protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);hot=true;Invalidate();}
        protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);hot=false;Invalidate();}
        protected override void OnPaint(PaintEventArgs e){
            e.Graphics.Clear(hot||Focused?ReaderStyle.CloseHoverFill:PanelForm.Ink);
            e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;int x=Width/2,y=Height/2,d=Math.Max(4,Height/7);
            using(var pen=new Pen(PanelForm.Body,1.4f)){e.Graphics.DrawLine(pen,x-d,y-d,x+d,y+d);e.Graphics.DrawLine(pen,x-d,y+d,x+d,y-d);}
            if(Focused)ControlPaint.DrawFocusRectangle(e.Graphics,Rectangle.Inflate(ClientRectangle,-2,-2));
        }
    }
    class ReaderButton:Button {
        public bool TextOnly,Pill;
        protected override void OnResize(EventArgs e){base.OnResize(e);Invalidate();}
        bool hot,pressed;
        protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);hot=true;Invalidate();}
        protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);hot=false;Invalidate();}
        protected override void OnMouseDown(MouseEventArgs e){base.OnMouseDown(e);pressed=e.Button==MouseButtons.Left;Invalidate();}
        protected override void OnMouseUp(MouseEventArgs e){base.OnMouseUp(e);pressed=false;Invalidate();}
        protected override void OnMouseCaptureChanged(EventArgs e){base.OnMouseCaptureChanged(e);if(!Capture)pressed=false;Invalidate();}
        protected override void OnPaint(PaintEventArgs e){
            var g=e.Graphics;var rect=ClientRectangle;if(rect.Width<2||rect.Height<2)return;
            Color fill=TextOnly?(Parent==null?BackColor:Parent.BackColor):Enabled?(hot?FlatAppearance.MouseOverBackColor:BackColor):ReaderStyle.DisabledButtonFill;
            if(Enabled&&pressed&&hot)fill=ControlPaint.Dark(fill,0.08f);
            g.Clear(Parent==null?PanelForm.Ink:Parent.BackColor);g.SmoothingMode=SmoothingMode.AntiAlias;
            if(!TextOnly)ReaderStyle.Surface(g,rect,Pill?rect.Height/2f:8*ReaderStyle.Scale(this),fill,
                Enabled?FlatAppearance.BorderColor:ReaderStyle.DisabledButtonBorder);
            TextRenderer.DrawText(g,Text,Font,rect,Enabled?(TextOnly&&hot?ReaderStyle.AccentHover:ForeColor):ReaderStyle.DisabledButtonText,
                TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.NoPrefix);
            if(Focused&&ShowFocusCues)ControlPaint.DrawFocusRectangle(g,Rectangle.Inflate(rect,-4,-4),ForeColor,fill);
        }
    }
    sealed class ReaderToggle:ReaderButton {
        readonly string label;
        public bool Checked{get;private set;}
        public ReaderToggle(string label,bool value){
            this.label=label;AccessibleName=label;AccessibleRole=AccessibleRole.CheckButton;
            Font=ReaderStyle.Hint;Pill=true;FlatStyle=FlatStyle.Flat;UseVisualStyleBackColor=false;
            BackColor=ReaderStyle.Ink;Cursor=Cursors.Hand;
            FlatAppearance.MouseOverBackColor=ReaderStyle.ButtonHoverFill;
            Checked=value;UpdateStyle();
        }
        void UpdateStyle(){
            Text=(Checked?"✓":"□")+" "+label;
            FlatAppearance.BorderColor=Checked?ReaderStyle.Accent:ReaderStyle.Line;
            ForeColor=Checked?ReaderStyle.Accent:ReaderStyle.Muted;
            AccessibleDescription=Checked?"已开启":"已关闭";Invalidate();
        }
        protected override void OnClick(EventArgs e){Checked=!Checked;UpdateStyle();base.OnClick(e);}
    }
    static class ReaderScroll {
        internal static void Thumb(Graphics g,int right,int top,int height,int total,int visible,int first,float scale){
            if(total<=visible||height<2)return;
            int width=Math.Max(2,(int)Math.Round(4*scale));
            int length=Math.Min(height,Math.Max(2*width,(int)((long)height*visible/total)));
            int offset=(int)((long)(height-length)*Math.Max(0,Math.Min(first,total-visible))/(total-visible));
            ReaderStyle.Surface(g,new Rectangle(right-width,top+offset,width,length),width/2f,ReaderStyle.Line,ReaderStyle.Line);
        }
        internal static int WheelLines(MouseEventArgs e,ref int remainder,int page){
            remainder+=e.Delta;
            const int wheelDelta=120;
            int detents=remainder/wheelDelta;
            remainder%=wheelDelta;
            int lines=SystemInformation.MouseWheelScrollLines;
            var handled=e as HandledMouseEventArgs;if(handled!=null)handled.Handled=true;
            return -detents*(lines<0?page:lines);
        }
        // Keep the panel's WinForms scroll range and focus scrolling while clipping its native bars.
        internal static void ClipBars(ScrollableControl panel){
            Size clipped=Size.Empty;
            EventHandler clip=(s,e)=>{
                var size=panel.ClientSize;
                if(size==clipped)return;
                clipped=size;
                var old=panel.Region;
                panel.Region=size==panel.Size?null:new Region(new Rectangle(Point.Empty,size));
                if(old!=null)old.Dispose();
                panel.Invalidate();if(panel.Parent!=null)panel.Parent.Invalidate(panel.Bounds);
            };
            panel.ClientSizeChanged+=clip;
            panel.Layout+=(s,e)=>clip(s,e);
            panel.Scroll+=(s,e)=>panel.Invalidate();
        }
        internal static void PanelThumb(ScrollableControl panel,Graphics g,int right,int top,int height){
            if(!panel.VerticalScroll.Visible)return;
            Thumb(g,right,top,height,panel.DisplayRectangle.Height,panel.ClientSize.Height,-panel.AutoScrollPosition.Y,ReaderStyle.Scale(panel));
        }
    }
    class ReaderListBox:ListBox {
        int wheel;
        protected override CreateParams CreateParams{
            get{var p=base.CreateParams;p.Style&=~0x00200000;return p;} // WS_VSCROLL
        }
        internal int VisibleRows{get{return Math.Max(1,ClientSize.Height/Math.Max(1,ItemHeight));}}
        protected override void OnMouseWheel(MouseEventArgs e){
            int visible=VisibleRows;
            if(Items.Count>visible){
                int lines=ReaderScroll.WheelLines(e,ref wheel,visible);
                if(lines!=0)TopIndex=Math.Max(0,Math.Min(Items.Count-visible,TopIndex+lines));
            }
            base.OnMouseWheel(e);
        }
        protected override void WndProc(ref Message m){
            base.WndProc(ref m);
            if(m.Msg==0x000f&&Parent!=null)Parent.Invalidate(new Rectangle(Right,Top,Parent.ClientSize.Width-Right,Height));
        }
    }
    sealed class ReaderTextBox:TextBox {
        [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr window,int message,IntPtr wParam,IntPtr lParam);
        int wheel;
        internal int VisibleLines{get{return Math.Max(1,ClientSize.Height/Font.Height);}}
        internal int LineCount{get{return 1+GetLineFromCharIndex(TextLength);}}
        internal int FirstLine{get{return SendMessage(Handle,0x00ce,IntPtr.Zero,IntPtr.Zero).ToInt32();}} // EM_GETFIRSTVISIBLELINE
        protected override void OnMouseWheel(MouseEventArgs e){
            int visible=VisibleLines;
            if(LineCount>visible){
                int lines=ReaderScroll.WheelLines(e,ref wheel,visible);
                if(lines!=0)SendMessage(Handle,0x00b6,IntPtr.Zero,(IntPtr)lines); // EM_LINESCROLL
            }
            base.OnMouseWheel(e);
        }
        protected override void WndProc(ref Message m){
            base.WndProc(ref m);
            if(m.Msg==0x000f&&Parent!=null)Parent.Invalidate(new Rectangle(Right,Top,Parent.ClientSize.Width-Right,Height));
        }
    }
    class PassageView:Control {
        readonly string line;readonly int start,end;
        const TextFormatFlags flags=TextFormatFlags.NoPadding|TextFormatFlags.NoPrefix|TextFormatFlags.SingleLine;
        public PassageView(string line,int start,int end){
            this.line=line;this.start=start;this.end=end;
            Font=ReaderStyle.JapaneseBody;Height=Math.Max(62,2*Font.Height+16);BackColor=ReaderStyle.Raised;
            AccessibleName="日文原句";AccessibleDescription=line;DoubleBuffered=true;
        }
        protected override void OnPaint(PaintEventArgs e){
            base.OnPaint(e);int pad=Math.Max(9,Font.Height/2),maxWidth=Math.Max(Font.Height*2,Width-2*pad);
            var at=StringInfo.ParseCombiningCharacters(line).Concat(new[]{line.Length}).ToArray();
            var starts=new List<int>{0};int width=0,selectedLine=0;
            for(int i=0;i<at.Length-1;i++){
                int advance=TextRenderer.MeasureText(e.Graphics,line.Substring(at[i],at[i+1]-at[i]),Font,Size.Empty,flags).Width;
                if(width>0&&width+advance>maxWidth){starts.Add(at[i]);width=0;}
                width+=advance;if(at[i]<=start)selectedLine=starts.Count-1;
            }
            starts.Add(line.Length);int count=starts.Count-1;
            int show=Math.Min(count,Math.Max(1,(Height-6)/Font.Height));
            int first=Math.Min(Math.Max(0,selectedLine-(show-1)/2),count-show);
            int top=(Height-show*Font.Height)/2;
            using(var pen=new Pen(ReaderStyle.PassageRule,2))e.Graphics.DrawLine(pen,1,8,1,Height-8);
            for(int i=first;i<first+show;i++,top+=Font.Height){
                int a=starts[i],b=starts[i+1],x=pad;
                foreach(var cut in new[]{Math.Max(a,Math.Min(start,b)),Math.Max(a,Math.Min(end,b)),b}){
                    if(cut<=a)continue;
                    var fragment=line.Substring(a,cut-a);var color=a>=start&&a<end?PanelForm.Accent:PanelForm.Muted;
                    TextRenderer.DrawText(e.Graphics,fragment,Font,new Point(x,top),color,flags);
                    x+=TextRenderer.MeasureText(e.Graphics,fragment,Font,Size.Empty,flags).Width;a=cut;
                }
            }
        }
    }
    class HeadwordLine:Control {
        readonly string kana;readonly Font small=ReaderStyle.JapaneseBody;
        const TextFormatFlags style=TextFormatFlags.NoPadding|TextFormatFlags.NoPrefix|TextFormatFlags.SingleLine;
        public HeadwordLine(string term,string reading){
            Text=term;kana=reading;Font=ReaderStyle.Headword;AccessibleName=term;AccessibleDescription=reading;DoubleBuffered=true;ResizeRedraw=true;SizeToWord();
        }
        bool Stacks(){return !String.IsNullOrEmpty(kana)&&TextRenderer.MeasureText(Text,Font,Size.Empty,style).Width+TextRenderer.MeasureText(kana,small,Size.Empty,style).Width+26>Width;}
        void SizeToWord(){Height=Font.Height+12+(Stacks()?small.Height+2:0);}
        protected override void OnSizeChanged(EventArgs e){base.OnSizeChanged(e);if(kana!=null)SizeToWord();}
        protected override void OnPaint(PaintEventArgs e){
            base.OnPaint(e);var top=new Rectangle(0,3,Width,Font.Height+4);
            TextRenderer.DrawText(e.Graphics,Text,Font,top,PanelForm.Body,style|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            if(String.IsNullOrEmpty(kana))return;
            int left=Stacks()?0:TextRenderer.MeasureText(e.Graphics,Text,Font,Size.Empty,style).Width+18;
            var r=new Rectangle(left,Stacks()?Font.Height+5:Math.Max(0,Font.Height-small.Height),Math.Max(1,Width-left),small.Height+6);
            TextRenderer.DrawText(e.Graphics,kana,small,r,PanelForm.Accent,style|TextFormatFlags.EndEllipsis);
        }
    }
    class ReaderList:ReaderListBox {
        public ReaderList(int height){
            Height=height;BorderStyle=BorderStyle.None;IntegralHeight=false;
            DrawMode=DrawMode.OwnerDrawFixed;ItemHeight=36;
            BackColor=PanelForm.Field;ForeColor=PanelForm.Body;
        }
        protected override void OnFontChanged(EventArgs e){base.OnFontChanged(e);ItemHeight=Math.Max(32,Font.Height+14);}
        protected override void OnDrawItem(DrawItemEventArgs e){
            if(e.Index<0||e.Index>=Items.Count)return;
            bool on=(e.State&DrawItemState.Selected)!=0;var r=e.Bounds;
            using(var fill=new SolidBrush(on?ReaderStyle.Raised:BackColor))e.Graphics.FillRectangle(fill,r);
            if(on)using(var pen=new SolidBrush(PanelForm.Accent))e.Graphics.FillRectangle(pen,r.Left,r.Top+5,2,Math.Max(2,r.Height-10));
            object item=Items[e.Index];var sense=item as Candidate;
            const TextFormatFlags fmt=TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis|TextFormatFlags.NoPrefix;
            var text=Rectangle.Inflate(r,-10,0);
            if(sense!=null){
                string source=sense.Source.Contains("統合辞書")?"小学馆":sense.Source=="mahoyo-ja-zh-optional"?"Tomoshi":sense.Source;
                int w=Math.Min(text.Width/3,TextRenderer.MeasureText(source,Font).Width+4);
                var end=new Rectangle(text.Right-w,text.Top,w,text.Height);
                TextRenderer.DrawText(e.Graphics,source,Font,end,PanelForm.Muted,fmt|TextFormatFlags.Right);
                text.Width=Math.Max(1,text.Width-w-12);
            }
            TextRenderer.DrawText(e.Graphics,sense==null?item.ToString():sense.Text,Font,text,PanelForm.Body,fmt);
            if((e.State&DrawItemState.Focus)!=0&&Focused)e.DrawFocusRectangle();
        }
    }
    class PanelForm:Form {
        internal static readonly Color Ink=ReaderStyle.Ink,Field=ReaderStyle.Field,Body=ReaderStyle.Body,Accent=ReaderStyle.Accent,Muted=ReaderStyle.Muted,Line=ReaderStyle.Line;
        readonly string stripTitle;
        readonly ReaderClose close=new ReaderClose();
        protected readonly FlowLayoutPanel Flow=new FlowLayoutPanel();
        readonly Panel foot=new Panel();
        readonly FlowLayoutPanel commands=new FlowLayoutPanel{WrapContents=true};
        bool fitting;
        Control trailingCommand;
        Control expandingRow;
        internal PanelForm(string title,int width,int height,bool singleTitle=false){
            Text="魔夜读本 · "+title;stripTitle=singleTitle?"魔夜读本":null;Font=ReaderStyle.BodyFont;BackColor=Ink;ForeColor=Body;
            AutoScaleMode=AutoScaleMode.Dpi;AutoScaleDimensions=new SizeF(96,96);ClientSize=new Size(width,height);
            MinimumSize=new Size(Math.Min(width,355),Math.Min(height,300));StartPosition=FormStartPosition.Manual;
            FormBorderStyle=FormBorderStyle.None;KeyPreview=true;ShowInTaskbar=true;DoubleBuffered=true;
            Padding=new Padding(1,36,1,1);ResizeRedraw=true;
            Flow.AutoScroll=true;ReaderScroll.ClipBars(Flow);
            Flow.Paint+=(s,e)=>ReaderScroll.PanelThumb(Flow,e.Graphics,Flow.ClientSize.Width-5,3,Flow.ClientSize.Height-6);
            Flow.MouseWheel+=(s,e)=>Flow.Invalidate();
            Flow.FlowDirection=FlowDirection.TopDown;Flow.WrapContents=false;Flow.Padding=new Padding(24,14,22,12);Controls.Add(Flow);
            foot.BackColor=ReaderStyle.Raised;commands.BackColor=foot.BackColor;foot.Controls.Add(commands);Controls.Add(foot);
            foot.Paint+=(s,e)=>{using(var pen=new Pen(ReaderStyle.Line))e.Graphics.DrawLine(pen,0,0,foot.Width,0);};
            Controls.Add(close);close.Click+=(s,e)=>Close();LayoutClose();
            Shown+=(s,e)=>{var work=Screen.FromPoint(Cursor.Position).WorkingArea;int x=Cursor.Position.X+16,y=Cursor.Position.Y+12;
                // Short laptop screens can trim the height while keeping enough width for the text.
                float scale=ReaderStyle.Scale(this);
                int margin=(int)Math.Round(8*scale);
                if(work.Width>2*margin+MinimumSize.Width&&work.Height>2*margin+MinimumSize.Height)work.Inflate(-margin,-margin);
                ClientSize=new Size(Math.Min((int)Math.Round(width*scale),work.Width),Math.Min((int)Math.Round(height*scale),work.Height));
                if(x+Width>work.Right)x=Cursor.Position.X-Width-12;if(y+Height>work.Bottom)y=work.Bottom-Height;
                Location=new Point(Math.Max(work.Left,Math.Min(x,work.Right-Width)),Math.Max(work.Top,y));RoundWindow();Fit();};
            SizeChanged+=(s,e)=>{RoundWindow();LayoutClose();Fit();};RoundWindow();
            KeyDown+=(s,e)=>{if(e.KeyCode==Keys.Escape&&!e.Handled){Close();e.Handled=true;}};
        }
        float CornerRadius{get{return Math.Max(7,Padding.Top/3f);}}
        void RoundWindow(){
            if(ClientSize.Width<2||ClientSize.Height<2)return;
            using(var outline=ReaderStyle.Rounded(ClientRectangle,CornerRadius)){
                var old=Region;Region=new Region(outline);if(old!=null)old.Dispose();
            }
        }
        void LayoutClose(){int side=Padding.Top-6;close.SetBounds(ClientSize.Width-side-7,3,side,side);close.BringToFront();}
        protected override void OnTextChanged(EventArgs e){
            base.OnTextChanged(e);Invalidate(new Rectangle(0,0,ClientSize.Width,Padding.Top));
        }
        protected override void WndProc(ref Message m){
            base.WndProc(ref m);
            if(m.Msg!=0x84||m.Result.ToInt32()!=1)return;
            long packed=m.LParam.ToInt64();var pt=PointToClient(new Point((short)packed,(short)(packed>>16)));
            const int edge=5;bool l=pt.X<edge,r=pt.X>=Width-edge,t=pt.Y<edge,b=pt.Y>=Height-edge;
            int target=t?(l?13:r?14:12):b?(l?16:r?17:15):l?10:r?11:pt.Y<Padding.Top&&!close.Bounds.Contains(pt)?2:1;
            m.Result=(IntPtr)target;
        }
        protected override void OnPaint(PaintEventArgs e){
            base.OnPaint(e);var g=e.Graphics;int band=Padding.Top;float s=band/36f;
            if(ClientSize.Width<2||ClientSize.Height<2)return;
            using(var shade=new LinearGradientBrush(new Rectangle(0,0,ClientSize.Width,band),ReaderStyle.TitleGradientStart,Ink,0f))
                g.FillRectangle(shade,0,0,ClientSize.Width,band);
            g.SmoothingMode=SmoothingMode.AntiAlias;
            using(var outline=ReaderStyle.Rounded(new RectangleF(0.5f,0.5f,ClientSize.Width-1,ClientSize.Height-1),CornerRadius))
            using(var border=new Pen(Line)){g.DrawPath(border,outline);g.DrawLine(border,1,band-1,Width-2,band-1);}
            using(var icon=new Pen(Accent,1.4f*s)){
                var state=g.Save();g.TranslateTransform(16*s,9*s);g.ScaleTransform(s,s);
                g.DrawBezier(icon,0,3,3,2,5,3,8,5);g.DrawBezier(icon,8,5,11,3,14,2,16,3);
                g.DrawLines(icon,new[]{new Point(0,3),new Point(0,13),new Point(4,13),new Point(8,15),new Point(12,13),new Point(16,13),new Point(16,3)});
                g.DrawLine(icon,8,5,8,15);g.Restore(state);
            }
            var title=new Rectangle((int)(40*s),1,Math.Max(20,Width-(int)(88*s)),band-2);
            TextRenderer.DrawText(g,stripTitle??Text,Font,title,Muted,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis|TextFormatFlags.NoPrefix);
        }
        protected void Fit(){
            if(fitting)return;
            fitting=true;
            try{
                int side=Flow.Padding.Left,inner=Math.Max(48,ClientSize.Width-2*side-2);
                int tail=trailingCommand!=null?trailingCommand.Width+8:0;
                int leftWidth=Math.Max(48,inner-tail);
                int pad=Math.Max(9,Font.Height/2),row=commands.Controls.Count==0?0:commands.GetPreferredSize(new Size(leftWidth,0)).Height;
                if(tail>0)row=Math.Max(row,trailingCommand.Height);
                int h=row==0?0:row+2*pad+1;
                foot.SetBounds(1,ClientSize.Height-1-h,ClientSize.Width-2,h);commands.SetBounds(side,pad+1,leftWidth,row);
                if(tail>0)trailingCommand.Location=new Point(foot.Width-side-trailingCommand.Width,pad+1);
                Flow.SetBounds(1,Padding.Top,ClientSize.Width-2,Math.Max(1,foot.Top-Padding.Top));
                for(int pass=0;pass<2;pass++){
                    int width=Math.Max(80,Flow.ClientSize.Width-Flow.Padding.Horizontal-3);
                    foreach(Control c in Flow.Controls){if(c is FlowLayoutPanel)c.MaximumSize=new Size(width,0);c.Width=width;}
                    if(expandingRow!=null && Flow.Visible){
                        int room=Flow.ClientSize.Height-Flow.Padding.Vertical-expandingRow.Margin.Vertical;
                        foreach(Control other in Flow.Controls)if(other!=expandingRow && other.Visible)room-=other.Height+other.Margin.Vertical;
                        expandingRow.Height=Math.Max(expandingRow.MinimumSize.Height,Math.Min(expandingRow.MaximumSize.Height,room));
                    }
                    Flow.PerformLayout();
                }
            }finally{fitting=false;}
        }
        protected void ExpandRow(Control row,int maxHeight){
            row.MinimumSize=new Size(0,row.Height);row.MaximumSize=new Size(0,maxHeight);expandingRow=row;Fit();
        }
        protected T Row<T>(T c) where T:Control{c.Margin=new Padding(1,3,1,3);Flow.Controls.Add(c);Fit();return c;}
        protected Label LabelRow(string text,int h){return Row(new Label{Text=text,Height=h,TextAlign=ContentAlignment.MiddleLeft,AutoEllipsis=true,ForeColor=Body});}
        protected Label Heading(string text){
            var label=LabelRow(text,42);label.Font=ReaderStyle.Heading;label.Height=Math.Max(42,label.Font.Height+9);return label;
        }
        protected Label Caption(string text){var label=LabelRow(text,Math.Max(23,Font.Height+2));label.ForeColor=Muted;return label;}
        protected void Rule(){var rule=Row(new Panel{Height=1,BackColor=ReaderStyle.Line});rule.Margin=new Padding(1,7,1,7);}
        protected FlowLayoutPanel Bar(){return Row(new FlowLayoutPanel{AutoSize=true,AutoSizeMode=AutoSizeMode.GrowAndShrink,WrapContents=true,FlowDirection=FlowDirection.LeftToRight,MinimumSize=new Size(1,36)});}
        protected FlowLayoutPanel Footer(){return commands;}
        protected Button ActionButton(FlowLayoutPanel bar,string text,Action action,bool primary=false){
            var b=MakeButton(text,action,primary);bar.Controls.Add(b);Fit();return b;
        }
        protected ReaderButton MakeButton(string text,Action action,bool primary=false){
            var b=new ReaderButton{Text=text,AutoSize=true,MinimumSize=new Size(70,33),Padding=new Padding(5,0,5,0),FlatStyle=FlatStyle.Flat,BackColor=primary?Accent:Ink,ForeColor=primary?Field:Body,Margin=new Padding(0,1,7,3),UseVisualStyleBackColor=false,Cursor=Cursors.Hand};
            b.FlatAppearance.BorderColor=primary?Accent:ReaderStyle.Line;
            b.FlatAppearance.MouseOverBackColor=primary?ReaderStyle.AccentHover:ReaderStyle.ButtonHoverFill;
            b.Click+=(s,e)=>Safe(action);return b;
        }
        protected ReaderButton TrailingButton(string text,Action action){
            var b=MakeButton(text,action);
            b.TextOnly=true;b.BackColor=foot.BackColor;b.ForeColor=ReaderStyle.Danger;
            trailingCommand=b;foot.Controls.Add(b);b.BringToFront();Fit();return b;
        }
        protected ReaderField Input(TextBox box){
            StyleInput(box);
            return Row(new ReaderField(box,box.Height));
        }
        protected static void StyleInput(TextBox box){
            box.BackColor=Field;box.ForeColor=Body;box.BorderStyle=BorderStyle.None;
        }
        protected void Safe(Action act){try{act();}catch(Exception e){MessageBox.Show(this,e.GetBaseException().Message,"魔夜读本",MessageBoxButtons.OK,MessageBoxIcon.Information);}}
        protected void SaveExport(List<string[]> rows,string name){
            using(var d=new SaveFileDialog{Title="导出"+name,FileName=name,Filter="CSV 表格 (*.csv)|*.csv|制表符文本 (*.tsv)|*.tsv",AddExtension=true,OverwritePrompt=true}){
                if(d.ShowDialog(this)==DialogResult.OK){Exports.Write(d.FileName,rows);MessageBox.Show(this,"已导出 "+(rows.Count-1)+" 行。\n"+d.FileName,"魔夜读本");}
            }
        }
    }
    class EditorForm:PanelForm {
        readonly Selection word;readonly Notebook book;readonly Dictionaries dict;readonly ReaderToggle local;readonly TextBox meaning;readonly ListBox options;
        readonly Label status;readonly Button save;readonly Note existing;readonly ReaderField optionField;
        public EditorForm(Notebook book,Dictionaries dict,Selection s):this(book,dict,s,book.Find(s)){}
        internal EditorForm(Notebook book,Dictionaries dict,Selection s,Note note):base("补释义",512,624){
            this.book=book;this.dict=dict;word=s;existing=note;
            if(existing!=null&&existing.Signature!=null&&existing.Signature.StartsWith("F")){s.Base=existing.Base;s.Kana=existing.Kana;s.Signature=existing.Signature;}
            Row(new HeadwordLine(s.Base,s.Kana));
            if(s.Form!=s.Base)Caption("原文词形　"+s.Form);
            Row(new PassageView(book.Data.Rows[s.Row].Ja,s.A,s.B));
            Rule();status=Caption("正在查询本机词典…");options=new ReaderList(82){AccessibleName="参考义项"};
            optionField=Row(new ReaderField(options,82){Visible=false});
            Caption("我的释义");meaning=new ReaderTextBox{Multiline=true,MaxLength=4000,Height=88,AccessibleName="我的释义",Text=existing==null?s.Hint:existing.Meaning};ExpandRow(Input(meaning),340);
            local=new ReaderToggle("仅记在本句",!s.CanRepeat||(existing!=null&&existing.Row>=0)){Enabled=s.CanRepeat,Size=new Size(138,30)};
            var scope=Row(new Panel{Height=local.Height});scope.Controls.Add(local);
            var actions=Footer();save=ActionButton(actions,"保存释义",()=>{book.Save(s,meaning.Text,local.Checked);Program.Changed();DialogResult=DialogResult.OK;Close();},true);
            save.Enabled=!String.IsNullOrWhiteSpace(meaning.Text);meaning.TextChanged+=(a,b)=>save.Enabled=!String.IsNullOrWhiteSpace(meaning.Text);
            if(existing!=null&&!existing.Examples.Any(e=>e.Row==s.Row&&e.A==s.A))ActionButton(actions,"记这条例句",()=>{book.Remember(existing.Id,s);Program.Changed();Close();});
            if(Program.HasGame)ActionButton(actions,"收藏词和原句",()=>{Program.MarkOriginal(s);Close();});
            options.SelectedIndexChanged+=(a,b)=>{var item=options.SelectedItem as Candidate;if(item!=null)meaning.Text=item.Text;};
            Shown+=(a,b)=>{meaning.Focus();meaning.Select(meaning.TextLength,0);LoadOptions();};
        }
        int query;
        async void LoadOptions(){
            int request=++query;
            try{
                string errors="";var list=await Task.Run(()=>dict.Lookup(word,out errors));
                if(IsDisposed||Disposing||request!=query)return;
                if(!String.IsNullOrWhiteSpace(word.Hint))list.Insert(0,new Candidate{Source="现有词卡",Text=word.Hint});
                options.Items.Clear();foreach(var item in list)options.Items.Add(item);
                optionField.Visible=list.Count>0;
                optionField.Height=options.ItemHeight*Math.Min(3,Math.Max(1,list.Count))+(int)Math.Round(12*ReaderStyle.Scale(optionField));Fit();
                status.Text=list.Count>0?"参考义项　"+list.Count+" 条　·　点选填入下方":String.IsNullOrWhiteSpace(errors)?"词典暂未收录，可直接填写":"词典无法读取，可手写或检查词典设置";
                if(!String.IsNullOrWhiteSpace(errors))new ToolTip().SetToolTip(status,errors);
            }catch(Exception ex){if(!IsDisposed&&!Disposing)status.Text=ex.GetBaseException().Message;}
        }
    }
    class PickerForm:PanelForm {
        readonly Corpus corpus;readonly Dictionaries dictionaries;readonly int key;
        readonly List<Selection> units;readonly TextBox original;readonly ListBox choices;
        readonly Label preview;readonly Button edit,left,right;Selection selected;int request;bool loading;
        public PickerForm(Notebook book,Dictionaries dict,int key):this(book,dict,key,-1,-1){}
        public PickerForm(Notebook book,Dictionaries dict,int key,int start,int end):base("选择词界",484,582){
            this.key=key;corpus=book.Data;dictionaries=dict;units=corpus.Units(key);
            Heading("选取原文");Caption("点击一个词，或拖选一段短语");
            original=new ReaderTextBox{Text=corpus.Rows[key].Ja,Font=ReaderStyle.JapaneseBody,Multiline=true,ReadOnly=true,
                HideSelection=false,Height=Math.Max(82,ReaderStyle.JapaneseBody.Height*3+8),AccessibleName="日文原句"};Input(original);
            original.MouseUp+=(a,b)=>FromText();
            original.KeyUp+=(a,b)=>{if(b.KeyCode==Keys.Left||b.KeyCode==Keys.Right||b.KeyCode==Keys.Home||b.KeyCode==Keys.End)FromText();};
            Caption("可选词界");choices=new ReaderList(92){AccessibleName="候选词和短语",Font=ReaderStyle.JapaneseBody};ExpandRow(Row(new ReaderField(choices,92)),245);
            choices.SelectedIndexChanged+=(a,b)=>{if(!loading&&choices.SelectedItem is Selection)ShowChoice((Selection)choices.SelectedItem);};
            var adjust=Bar();left=ActionButton(adjust,"向左扩一词",()=>Extend(true));right=ActionButton(adjust,"向右扩一词",()=>Extend(false));
            preview=Caption("");preview.Height=2*Font.Height+4;preview.AutoEllipsis=false;var actions=Footer();
            Action open=()=>{if(selected==null)return;using(var f=new EditorForm(book,dictionaries,selected)){if(f.ShowDialog(this)==DialogResult.OK)Close();}};
            edit=ActionButton(actions,"用这个词",open,true);ActionButton(actions,"取消",Close);choices.DoubleClick+=(a,b)=>Safe(open);
            Shown+=(a,b)=>{
                int at=start>=0?start:units.Count==0?0:units[0].A;
                FocusWord(at,end>start+1?end:-1);
            };
        }
        void ShowChoice(Selection s){
            selected=s;edit.Enabled=s!=null;
            left.Enabled=s!=null&&units.Any(u=>u.A<s.A&&u.B>=s.A);
            right.Enabled=s!=null&&units.Any(u=>u.B>s.B&&u.A<=s.B);
            if(s==null){preview.Text="请点选日文中的词。";return;}
            original.Select(s.A,s.B-s.A);original.ScrollToCaret();
            preview.Text=s.Form+(s.Form==s.Base?"":"　→　"+s.Base)+(s.Kana.Length==0?"":"　 "+s.Kana);
        }
        void FromText(){
            int a=original.SelectionStart,b=a+original.SelectionLength;
            if(b>a){++request;ShowChoice(corpus.Select(key,a,b));return;}
            FocusWord(a,-1);
        }
        void Extend(bool before){
            if(selected==null)return;
            var u=before?units.LastOrDefault(w=>w.A<selected.A&&w.B>=selected.A):units.FirstOrDefault(w=>w.B>selected.B&&w.A<=selected.B);
            if(u==null)return;++request;
            ShowChoice(corpus.Select(key,before?u.A:selected.A,before?selected.B:u.B));
        }
        async void FocusWord(int caret,int end){
            int current=++request;
            var options=corpus.ScopeOptions(key,caret,units);
            if(end>caret&&end<=corpus.Rows[key].Ja.Length&&!options.Any(s=>s.A==caret&&s.B==end))options.Insert(0,corpus.Select(key,caret,end));
            loading=true;choices.Items.Clear();foreach(var item in options)choices.Items.Add(item);loading=false;
            if(options.Count==0){ShowChoice(null);return;}
            int initial=options.FindIndex(s=>s.A==caret&&s.B==end);
            choices.SelectedIndex=Math.Max(0,initial);
            var spans=corpus.NearbyPhrases(key,caret,units);
            try{
                var matches=await Task.Run(()=>dictionaries.LongerEntries(spans));
                if(IsDisposed||Disposing||current!=request)return;
                foreach(var hit in matches){
                    var existing=choices.Items.Cast<Selection>().FirstOrDefault(s=>s.A==hit.A&&s.B==hit.B);
                    if(existing==null){choices.Items.Add(hit);continue;}
                    if(hit.Signature!=existing.Signature&&hit.Signature.StartsWith("F")){
                        bool active=selected==existing;
                        loading=true;choices.Items[choices.Items.IndexOf(existing)]=hit;loading=false;
                        if(active)ShowChoice(hit);
                    }
                }
            }catch(Exception){/* The original and manual word boundaries remain available offline. */}
        }
    }
    class SourcesForm:PanelForm {
        readonly Dictionaries dict;readonly ListBox list;readonly Label info;
        public SourcesForm(Dictionaries d):base("离线词典",680,408){
            dict=d;Heading("离线词典");Caption("已启用的词典");
            list=new ReaderList(90){AccessibleName="已启用的词典"};ExpandRow(Row(new ReaderField(list,90)),240);
            var management=Bar();detach=ActionButton(management,"移除关联",()=>{var s=list.SelectedItem as Source;if(s!=null){dict.Remove(s);RefreshSources();}});
            list.SelectedIndexChanged+=(s,e)=>{var source=list.SelectedItem as Source;detach.Enabled=source!=null&&source.Kind!="bundled";};
            info=LabelRow("「补释义」可直接查随读本附带的小学馆辞书。也可导入其他词表，或关联本机电子辞书。",48);
            var actions=Footer();ActionButton(actions,"导入词典",Import,true);ActionButton(actions,"关联本机词典",Link);
            RefreshSources();
        }
        readonly Button detach;
        void RefreshSources(){list.Items.Clear();foreach(var s in dict.All())list.Items.Add(s);detach.Enabled=false;}
        void Link(){
            using(var file=new OpenFileDialog{Title="选择已有的电子词典目录文件",Filter="电子辞书目录|CATALOG;CATALOGS;CATALOGS.ebz|全部文件|*.*"}){
                if(file.ShowDialog(this)!=DialogResult.OK)return;string reader=null;
                for(var d=Directory.GetParent(file.FileName);d!=null;d=d.Parent){var path=Path.Combine(d.FullName,"EBWin4.exe");if(File.Exists(path)){reader=path;break;}}
                if(reader==null)reader=dict.All().Where(x=>x.Kind=="eb"&&File.Exists(x.Reader)).Select(x=>x.Reader).FirstOrDefault();
                if(reader==null){using(var pick=new OpenFileDialog{Title="选择本机 EBWin4.exe",Filter="EBWin4|EBWin4.exe"}){if(pick.ShowDialog(this)!=DialogResult.OK)return;reader=pick.FileName;}}
                dict.Link(file.FileName,reader);RefreshSources();info.Text="本机词典已关联。";
            }
        }
        async void Import(){
            using(var pick=new OpenFileDialog{Title="导入离线词典",Filter="开放词库 / Yomitan / 三列 TSV|*.zip;*.tsv"}){
                if(pick.ShowDialog(this)!=DialogResult.OK)return;
                try{string path=pick.FileName;Enabled=false;info.Text="正在导入…";int n=await Task.Run(()=>dict.Import(path));if(!IsDisposed){RefreshSources();info.Text="已导入 "+n.ToString("N0")+" 条释义。";}}
                catch(Exception e){if(!IsDisposed)MessageBox.Show(this,e.GetBaseException().Message,"导入失败");}
                finally{if(!IsDisposed)Enabled=true;}
            }
        }
    }
    class LegendForm:PanelForm {
        public LegendForm():base("功能说明",420,600,true){
            Flow.Padding=new Padding(26,22,26,12);
            Heading("功能说明").Margin=new Padding(1,2,1,9);
            Row(new GuideRow("阅读模式 · F8","「对照」显示本页的行间中文。「原文」默认只读日文，在一句上停留约 1 秒可看译文。按 F8 或点悬浮球切换。",true));
            Row(new GuideRow("只看对应译文","在「对照」中，把鼠标移到日文上停留约 0.25 秒，只显示对应中文。能对齐到分句时，细金线会标出对应范围。移到正文外恢复整页；推进剧情后需重新移动鼠标。",true));
            Row(new GuideRow("读音与音调","指向普通汉字词约 0.4 秒可看假名。完整词卡还显示释义；有音调曲线时，从左到右读每一拍的高低，末端小圆表示后接助词的音高。",true));
            Row(new GuideRow("最近查过","词卡或读音气泡显示满 0.8 秒就会留下记录。打开生词本，切到「最近查过」，可把词加入生词本、收藏原句或导出。最近 500 条里，同一处的词只留最新一次。",true));
            Row(new GuideRow("补释义与点词","在词卡点「补释义」，从参考义项选一个或直接输入，再点「保存释义」。勾选「仅记在本句」可限制使用范围。碰到纯假名或词界不合适时，用悬浮球的「点词取词」选择范围。",true));
            Row(new GuideRow("管理离线词典","「补释义」会查询读本自带的小学馆辞书。悬浮球里的「离线词典」还可导入词典 ZIP、三列 TSV，或关联本机 EBWin4 词典。",true));
            Row(new GuideRow("收藏与批注","点行尾书签收藏整句；F9 收藏鼠标所指的词或句。按 F10 打开「收藏夹」，选一句写批注，停笔约 0.8 秒或离开输入框会自动保存。搜索可查批注，导出时批注在最后一列。",false));
            ActionButton(Footer(),"返回阅读",Close,true);
        }
    }
    sealed class ReaderField:Panel {
        readonly Control content;
        readonly bool magnifier;
        public ReaderField(Control content,int height,bool magnifier=false){
            this.content=content;this.magnifier=magnifier;Height=height;
            DoubleBuffered=true;ResizeRedraw=true;Controls.Add(content);
        }
        protected override void OnLayout(LayoutEventArgs e){
            base.OnLayout(e);if(content==null)return;
            float scale=ReaderStyle.Scale(this);
            int pad=(int)Math.Round(6*scale),left=magnifier?(int)Math.Round(33*scale):pad;
            var box=content as TextBox;
            if(box!=null&&!box.Multiline){
                box.SetBounds(left,Math.Max(1,(Height-box.PreferredHeight)/2),Math.Max(1,Width-left-pad),box.PreferredHeight);
            }else content.SetBounds(left,pad,Math.Max(1,Width-left-pad),Math.Max(1,Height-2*pad));
        }
        protected override void OnPaint(PaintEventArgs e){
            base.OnPaint(e);
            float scale=ReaderStyle.Scale(this);
            ReaderStyle.Surface(e.Graphics,ClientRectangle,8*scale,ReaderStyle.Field,ReaderStyle.FieldLine);
            int right=Width-Math.Max(2,(int)Math.Round(scale));
            var list=content as ReaderListBox;
            var box=content as ReaderTextBox;
            if(list!=null)ReaderScroll.Thumb(e.Graphics,right,list.Top,list.Height,list.Items.Count,list.VisibleRows,list.TopIndex,scale);
            if(box!=null&&box.IsHandleCreated)ReaderScroll.Thumb(e.Graphics,right,box.Top,box.Height,box.LineCount,box.VisibleLines,box.FirstLine,scale);
            var details=content as LibraryDetails;
            if(details!=null)ReaderScroll.PanelThumb(details,e.Graphics,right,details.Top,details.Height);
            if(magnifier)using(var pen=new Pen(ReaderStyle.Dim,scale)){
                float left=14*scale,top=Height/2f-5*scale,d=8*scale;
                e.Graphics.DrawEllipse(pen,left,top,d,d);
                e.Graphics.DrawLine(pen,left+7*scale,top+7*scale,left+11*scale,top+11*scale);
            }
        }
    }
    sealed class LibraryTab:Button {
        public int Count;
        public bool Active;
        public LibraryTab(string text){
            Text=text;FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;
            Cursor=Cursors.Hand;AccessibleName=text;ResizeRedraw=true;
        }
        protected override void OnPaint(PaintEventArgs e){
            var g=e.Graphics;g.Clear(ReaderStyle.Ink);
            float scale=ReaderStyle.Scale(this);
            int title=TextRenderer.MeasureText(g,Text,ReaderStyle.Tab,Size.Empty,TextFormatFlags.NoPrefix).Width;
            var flags=TextFormatFlags.VerticalCenter|TextFormatFlags.NoPrefix|TextFormatFlags.SingleLine;
            TextRenderer.DrawText(g,Text,ReaderStyle.Tab,new Rectangle(0,0,title+2,Height),Active?ReaderStyle.Body:ReaderStyle.Dim,flags);
            TextRenderer.DrawText(g,Count.ToString(),ReaderStyle.Small,new Rectangle(title,0,Math.Max(1,Width-title),Height),ReaderStyle.Dim,flags);
            if(Active)using(var fill=new SolidBrush(ReaderStyle.Accent))g.FillRectangle(fill,0,Height-2*scale,Width,2*scale);
            if(Focused&&ShowFocusCues)ControlPaint.DrawFocusRectangle(g,Rectangle.Inflate(ClientRectangle,-2,-4));
        }
    }
    sealed class LibraryEntry {
        public string Ja,Kana,Zh,Tag;
        public object Value;
        public bool AccentTag,HasNote;
        public override string ToString(){return Ja+" "+Kana+" "+Zh+" "+Tag;}
    }
    sealed class LibraryList:ReaderListBox {
        const TextFormatFlags Flags=TextFormatFlags.NoPrefix|TextFormatFlags.SingleLine|TextFormatFlags.EndEllipsis|TextFormatFlags.VerticalCenter;
        public LibraryList(){
            BorderStyle=BorderStyle.None;IntegralHeight=false;DrawMode=DrawMode.OwnerDrawFixed;
            BackColor=ReaderStyle.Field;AccessibleName="收藏条目";
        }
        protected override void OnSizeChanged(EventArgs e){
            base.OnSizeChanged(e);ItemHeight=Math.Min(255,Math.Max(1,(int)Math.Round(57*ReaderStyle.Scale(this))));
        }
        protected override void OnDrawItem(DrawItemEventArgs e){
            if(e.Index<0||e.Index>=Items.Count)return;
            var row=(LibraryEntry)Items[e.Index];var r=e.Bounds;var g=e.Graphics;
            float scale=ReaderStyle.Scale(this);
            int pad=(int)Math.Round(8*scale),left=r.Left+pad;
            bool selected=(e.State&DrawItemState.Selected)!=0;
            using(var fill=new SolidBrush(selected?ReaderStyle.Raised:ReaderStyle.Field))g.FillRectangle(fill,r);
            using(var rule=new Pen(ReaderStyle.FieldLine))g.DrawLine(rule,r.Left,r.Bottom-1,r.Right,r.Bottom-1);
            if(selected)using(var fill=new SolidBrush(ReaderStyle.Accent))
                g.FillRectangle(fill,r.Left,r.Top+pad,2*scale,Math.Max(1,r.Height-2*pad));
            int first=r.Top+pad,second=first+(int)Math.Round(21*scale);
            int right=r.Right-pad;
            if(!String.IsNullOrEmpty(row.Tag)){
                int w=Math.Min(r.Width/3,TextRenderer.MeasureText(g,row.Tag,ReaderStyle.Small,Size.Empty,Flags).Width+pad);
                var tag=new Rectangle(right-w,first,w,(int)Math.Round(18*scale));
                if(row.AccentTag)ReaderStyle.Surface(g,tag,tag.Height/2f,ReaderStyle.Field,ReaderStyle.Line);
                TextRenderer.DrawText(g,row.Tag,ReaderStyle.Small,tag,row.AccentTag?ReaderStyle.Accent:ReaderStyle.Dim,Flags|TextFormatFlags.HorizontalCenter);
                right=tag.Left-pad;
            }
            int width=Math.Max(1,right-left);
            int head=row.Kana.Length==0?width:Math.Min(width*2/3,TextRenderer.MeasureText(g,row.Ja,ReaderStyle.RowJapanese,Size.Empty,Flags).Width);
            TextRenderer.DrawText(g,row.Ja,ReaderStyle.RowJapanese,new Rectangle(left,first,head,second-first),ReaderStyle.Body,Flags);
            if(row.Kana.Length>0)TextRenderer.DrawText(g,row.Kana,ReaderStyle.RowJapanese,
                new Rectangle(left+head,first,Math.Max(1,width-head),second-first),ReaderStyle.Accent,Flags);
            int secondRight=r.Right-pad;
            if(row.HasNote){
                float x=secondRight-10*scale,y=second+7*scale;
                var state=g.Save();g.SmoothingMode=SmoothingMode.AntiAlias;
                using(var pen=new Pen(ReaderStyle.Accent,1.4f*scale)){
                    g.DrawPolygon(pen,new[]{new PointF(x-2*scale,y+5*scale),new PointF(x-scale,y+scale),
                        new PointF(x+5*scale,y-5*scale),new PointF(x+8*scale,y-2*scale),new PointF(x+2*scale,y+4*scale)});
                }
                g.Restore(state);secondRight-=(int)Math.Ceiling(17*scale);
            }
            TextRenderer.DrawText(g,row.Zh.Replace("\r"," ").Replace("\n"," "),row.Value is FavoriteLine?StoryTypeface.Small:ReaderStyle.Small,
                new Rectangle(left,second,Math.Max(1,secondRight-left),Math.Max(1,r.Bottom-second-pad/2)),ReaderStyle.Dim,Flags);
            if(Focused&&(e.State&DrawItemState.Focus)!=0)e.DrawFocusRectangle();
        }
    }
    sealed class LibraryDetails:Panel {
        static readonly TextFormatFlags BlockFlags=TextFormatFlags.NoPrefix|TextFormatFlags.WordBreak|TextFormatFlags.TextBoxControl;
        static readonly TextFormatFlags GlyphFlags=TextFormatFlags.NoPadding|TextFormatFlags.NoPrefix|TextFormatFlags.SingleLine;
        sealed class DetailPage:Panel {
            public DetailPage(){BackColor=ReaderStyle.Field;DoubleBuffered=true;ResizeRedraw=true;}
        }
        readonly Panel page=new DetailPage();
        internal readonly SentenceNoteEditor Annotation=new SentenceNoteEditor{Visible=false};
        Corpus data;List<Note> notes;object item;
        string copy="";
        bool arranging;
        public LibraryDetails(){
            AccessibleName="条目详情";AutoScroll=true;BackColor=ReaderStyle.Field;ReaderScroll.ClipBars(this);
            SetStyle(ControlStyles.Selectable,true);TabStop=true;
            Scroll+=(s,e)=>RefreshThumb();ClientSizeChanged+=(s,e)=>RefreshThumb();
            page.Controls.Add(Annotation);Controls.Add(page);page.Paint+=(s,e)=>Render(e.Graphics,page.Width,true);
            page.MouseDown+=(s,e)=>Focus();page.Resize+= (s,e)=>page.Invalidate();
        }
        public void ShowEntry(Corpus corpus,List<Note> words,object value){
            data=corpus;notes=words;item=value;
            AutoScrollPosition=Point.Empty;
            var line=value as FavoriteLine;var note=value as Note;
            var lookup=value as Lookup;
            copy=lookup!=null?Bilingual(data.Rows[lookup.Word.Row]):line!=null?Bilingual(data.Rows[line.Row]):note!=null?
                String.Join("\r\n\r\n",Examples(note).Select(x=>Bilingual(data.Rows[x.Row]))):"";
            if(note!=null&&copy.Length==0)copy=note.Base+"\r\n"+note.Meaning;
            AccessibleDescription=copy;Arrange();page.Invalidate();RefreshThumb();
        }
        void RefreshThumb(){if(Parent!=null)Parent.Invalidate(new Rectangle(Right,Top,Parent.ClientSize.Width-Right,Height));}
        protected override void OnMouseWheel(MouseEventArgs e){base.OnMouseWheel(e);RefreshThumb();}
        static string Bilingual(Sentence row){return row.Ja+"\r\n"+row.Zh;}
        IEnumerable<Example> Examples(Note note){return note.Examples.Where(x=>x.Row>=0&&x.Row<data.Rows.Length);}
        public bool CopyText(){
            if(String.IsNullOrEmpty(copy))return false;
            Clipboard.SetText(copy);return true;
        }
        protected override bool ProcessCmdKey(ref Message msg,Keys keyData){
            if(Annotation.ContainsFocus)return base.ProcessCmdKey(ref msg,keyData);
            if(keyData==(Keys.Control|Keys.C)&&CopyText())return true;
            if(VerticalScroll.Visible){
                int y=-AutoScrollPosition.Y,limit=Math.Max(0,DisplayRectangle.Height-ClientSize.Height);
                switch(keyData){
                    case Keys.Up:y-=Font.Height;break;
                    case Keys.Down:y+=Font.Height;break;
                    case Keys.PageUp:y-=ClientSize.Height;break;
                    case Keys.PageDown:y+=ClientSize.Height;break;
                    case Keys.Home:y=0;break;
                    case Keys.End:y=limit;break;
                    default:return base.ProcessCmdKey(ref msg,keyData);
                }
                AutoScrollPosition=new Point(0,Math.Max(0,Math.Min(limit,y)));RefreshThumb();
                return true;
            }
            return base.ProcessCmdKey(ref msg,keyData);
        }
        protected override void OnSizeChanged(EventArgs e){base.OnSizeChanged(e);Arrange();}
        void Arrange(){
            if(page==null||arranging||Width<1)return;
            arranging=true;
            try{
                using(var g=page.CreateGraphics()){
                    int w=Width,h=Render(g,w,false);
                    if(h>Height){
                        w=Math.Max(1,w-SystemInformation.VerticalScrollBarWidth);
                        h=Render(g,w,false);
                    }
                    page.Size=new Size(w,h);
                }
            }finally{arranging=false;}
        }
        static int Paragraph(Graphics g,string text,Font font,Color color,int left,int top,int width,bool draw){
            if(String.IsNullOrEmpty(text))return top;
            int height=TextRenderer.MeasureText(g,text,font,new Size(width,Int32.MaxValue),BlockFlags).Height;
            if(draw)TextRenderer.DrawText(g,text,font,new Rectangle(left,top,width,height),color,BlockFlags);
            return top+height;
        }
        static int Japanese(Graphics g,string text,Font font,IList<Example> marks,int left,int top,int width,int gap,bool draw){
            int x=left,y=top,height=TextRenderer.MeasureText(g,"あ",font,Size.Empty,GlyphFlags).Height;
            var chars=StringInfo.GetTextElementEnumerator(text);
            using(var pen=new Pen(ReaderStyle.Accent,Math.Max(1,gap/3f)))while(chars.MoveNext()){
                string glyph=chars.GetTextElement();int index=chars.ElementIndex;
                int advance=TextRenderer.MeasureText(g,glyph,font,Size.Empty,GlyphFlags).Width;
                if(x>left&&x+advance>left+width){x=left;y+=height+gap;}
                if(draw){
                    TextRenderer.DrawText(g,glyph,font,new Point(x,y),ReaderStyle.Body,GlyphFlags);
                    if(marks.Any(m=>m.A<index+glyph.Length&&m.B>index))
                        g.DrawLine(pen,x,y+height,x+advance,y+height);
                }
                x+=advance;
            }
            return y+height+gap;
        }
        int Render(Graphics g,int width,bool draw){
            float scale=ReaderStyle.Scale(this);
            int pad=(int)Math.Round(13*scale),gap=Math.Max(3,pad/3),top=pad;
            int w=Math.Max(1,width-2*pad);
            var line=item as FavoriteLine;var note=item as Note;
            if(line!=null){
                var row=data.Rows[line.Row];
                top=Japanese(g,row.Ja,ReaderStyle.DetailJapanese,line.Words,pad,top,w,gap,draw)+gap;
                top=Paragraph(g,row.Zh,StoryTypeface.Detail,ReaderStyle.Muted,pad,top,w,draw)+pad;
                if(line.Words.Count>0)top=Paragraph(g,"已标词语",ReaderStyle.Small,ReaderStyle.Dim,pad,top,w,draw)+gap;
                foreach(var word in line.Words){
                    var selection=data.Select(line.Row,word.A,word.B);
                    string kana=Exports.MarkedReading(data,row,word);
                    if(String.IsNullOrEmpty(kana))kana=selection.Kana;
                    var personal=notes.FirstOrDefault(x=>x.Same(selection,true))
                        ?? notes.FirstOrDefault(x=>selection.CanRepeat&&x.Same(selection,false));
                    string meaning=personal==null?selection.Hint:personal.Meaning;
                    top=Chip(g,word.Form,kana,meaning,pad,top,w,gap,draw)+gap;
                }
                int editorHeight=(int)Math.Round(118*scale);
                if(!draw)Annotation.SetBounds(pad,top,w,editorHeight);
                top+=editorHeight;
            }else if(item is Lookup){
                var hit=(Lookup)item;var s=hit.Word;var row=data.Rows[s.Row];
                top=Paragraph(g,s.Form,ReaderStyle.Headword,ReaderStyle.Body,pad,top,w,draw);
                top=Paragraph(g,hit.Kana,ReaderStyle.DetailJapanese,ReaderStyle.Accent,pad,top,w,draw)+gap;
                top=Paragraph(g,s.Hint,ReaderStyle.WordMeaning,ReaderStyle.Body,pad,top,w,draw)+pad;
                top=Japanese(g,row.Ja,ReaderStyle.RowJapanese,new[]{new Example{A=s.A,B=s.B}},pad,top,w,gap,draw);
                top=Paragraph(g,row.Zh,StoryTypeface.Small,ReaderStyle.Dim,pad,top,w,draw);
            }else if(note!=null){
                top=Paragraph(g,note.Base,ReaderStyle.Headword,ReaderStyle.Body,pad,top,w,draw);
                top=Paragraph(g,note.Kana,ReaderStyle.DetailJapanese,ReaderStyle.Accent,pad,top,w,draw)+gap;
                top=Paragraph(g,note.Meaning,ReaderStyle.WordMeaning,ReaderStyle.Body,pad,top,w,draw)+gap;
                top=Chip(g,note.Row<0?"下次遇到也显示":"仅本句","","",pad,top,w,gap,draw)+pad;
                foreach(var example in Examples(note)){
                    var row=data.Rows[example.Row];
                    top=Japanese(g,row.Ja,ReaderStyle.RowJapanese,new[]{example},pad,top,w,gap,draw);
                    top=Paragraph(g,row.Zh,StoryTypeface.Small,ReaderStyle.Dim,pad,top,w,draw)+pad;
                }
            }
            return top+pad;
        }
        static int Chip(Graphics g,string text,string kana,string meaning,int left,int top,int maxWidth,int pad,bool draw){
            var fields=new[]{text,kana,meaning};
            var colors=new[]{ReaderStyle.Body,ReaderStyle.Accent,ReaderStyle.Dim};
            var sizes=fields.Select(x=>String.IsNullOrEmpty(x)?Size.Empty:
                TextRenderer.MeasureText(g,x,ReaderStyle.Hint,Size.Empty,TextFormatFlags.NoPrefix)).ToArray();
            int width=Math.Min(maxWidth,sizes.Where(x=>x.Width>0).Sum(x=>x.Width+pad)+pad);
            int inner=Math.Max(1,width-2*pad);
            bool wrap=sizes.Sum(x=>x.Width)+pad*Math.Max(0,sizes.Count(x=>x.Width>0)-1)>inner||
                fields.Any(x=>x.IndexOf('\n')>=0);
            int y=top+pad, xPos=left+pad;
            int bottom=y+sizes.Max(x=>x.Height);
            if(wrap){
                bottom=y;
                foreach(string field in fields)bottom=Paragraph(g,field,ReaderStyle.Hint,ReaderStyle.Dim,left+pad,bottom,inner,false);
            }
            if(draw){
                ReaderStyle.Surface(g,new Rectangle(left,top,width,bottom+pad-top),2*pad,ReaderStyle.Field,ReaderStyle.Line);
                for(int i=0;i<fields.Length;i++){
                    if(sizes[i].Width==0)continue;
                    int next=Paragraph(g,fields[i],ReaderStyle.Hint,colors[i],xPos,y,wrap?inner:sizes[i].Width,true);
                    if(wrap)y=next;else xPos+=sizes[i].Width+pad;
                }
            }
            return bottom+pad;
        }
    }
    sealed class LibraryMessage:Panel {
        public bool Sentences,Recent,Loading;
        public string Error;
        public LibraryMessage(){DoubleBuffered=true;ResizeRedraw=true;AccessibleName="收藏列表状态";}
        protected override void OnPaint(PaintEventArgs e){
            base.OnPaint(e);var g=e.Graphics;float scale=ReaderStyle.Scale(this);
            if(Loading||Error!=null){
                ReaderStyle.Surface(g,ClientRectangle,8*scale,ReaderStyle.Field,ReaderStyle.FieldLine);
                TextRenderer.DrawText(g,Error??"正在载入…",ReaderStyle.Hint,
                    new Rectangle((int)(15*scale),(int)(14*scale),Math.Max(1,Width-(int)(30*scale)),Math.Max(1,Height-(int)(28*scale))),
                    ReaderStyle.Dim,TextFormatFlags.WordBreak|TextFormatFlags.NoPrefix);
                return;
            }
            ReaderStyle.Surface(g,ClientRectangle,8*scale,ReaderStyle.Ink,ReaderStyle.Line,true);
            float x=Width/2f,y=Height/2f-43*scale;
            var previous=g.Save();g.TranslateTransform(x,y);g.ScaleTransform(scale,scale);
            using(var pen=new Pen(ReaderStyle.PassageRule,2)){
                if(Sentences)g.DrawPolygon(pen,new[]{new Point(-9,0),new Point(9,0),new Point(9,30),new Point(0,23),new Point(-9,30)});
                else{
                    g.DrawBezier(pen,-20,0,-12,-3,-6,-2,0,4);g.DrawBezier(pen,0,4,6,-2,12,-3,20,0);
                    g.DrawLines(pen,new[]{new Point(-20,0),new Point(-20,28),new Point(-11,27),new Point(0,32),new Point(11,27),new Point(20,28),new Point(20,0)});
                    g.DrawLine(pen,0,4,0,32);
                }
            }
            g.Restore(previous);
            var flags=TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.WordBreak|TextFormatFlags.NoPrefix;
            TextRenderer.DrawText(g,Recent?"还没有查过的词":Sentences?"还没有收藏原句":"还没有生词",ReaderStyle.EmptyTitle,
                new Rectangle(0,(int)(y+40*scale),Width,(int)(28*scale)),ReaderStyle.Body,flags);
            TextRenderer.DrawText(g,Recent?"在正文里停留看词卡，会自动记在这里":Sentences?"在正文行尾点书签，或把鼠标停在句子上按 F9":"在正文词卡上点「补释义」或「加入生词本」",
                ReaderStyle.Hint,new Rectangle((int)(8*scale),(int)(y+70*scale),Math.Max(1,Width-(int)(16*scale)),(int)(35*scale)),ReaderStyle.Dim,flags);
        }
    }
    class LibraryForm:PanelForm {
        [DllImport("user32.dll",CharSet=CharSet.Unicode)]
        static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr wParam,string text);
        Notebook book;
        FavoritesStore favorites;
        readonly Dictionaries dict;
        readonly LibraryList list;
        readonly LibraryDetails details;
        readonly TextBox search;
        readonly LibraryTab wordTab,lineTab,recentTab;
        readonly ReaderToggle filter;
        readonly ReaderButton remove,undo;
        readonly Button export,edit,master,addLookup,saveLine,clearRecent;
        readonly Panel searchRow;
        readonly LibraryPane panes;
        readonly LibraryMessage message;
        readonly Panel content;
        readonly Timer undoTimer=new Timer{Interval=6500};
        readonly Timer recentRefresh=new Timer{Interval=1000};
        Action restore;
        string sentenceError;
        enum LibraryPage { Words, Sentences, Recent }
        LibraryPage page;
        bool phrases{get{return page==LibraryPage.Sentences;}}
        bool recent{get{return page==LibraryPage.Recent;}}
        bool refreshing;
        RecentStore history;
        SentenceNotes annotations;
        Dictionary<int,string> notesByRow=new Dictionary<int,string>();
        DateTime noteStamp;
        internal bool FlushNotes(){return details.Annotation.Flush();}
        List<Lookup> lookups;
        List<Note> words;
        List<FavoriteLine> lines;

        public LibraryForm(Dictionaries dict,bool sentences):base(sentences?"收藏夹":"生词本",760,510){
            this.dict=dict;page=sentences?LibraryPage.Sentences:LibraryPage.Words;
            Flow.Padding=new Padding(23,8,22,12);
            var tabs=Row(new Panel{Height=43});
            wordTab=new LibraryTab("生词本"){Enabled=false};
            lineTab=new LibraryTab("收藏夹"){Enabled=false};
            recentTab=new LibraryTab("最近查过"){Enabled=false};
            wordTab.Click+=(s,e)=>Safe(()=>Switch(LibraryPage.Words));
            lineTab.Click+=(s,e)=>Safe(()=>Switch(LibraryPage.Sentences));
            recentTab.Click+=(s,e)=>Safe(()=>Switch(LibraryPage.Recent));
            tabs.Controls.AddRange(new Control[]{wordTab,lineTab,recentTab});
            tabs.Paint+=(s,e)=>{using(var pen=new Pen(ReaderStyle.FieldLine))e.Graphics.DrawLine(pen,0,tabs.Height-1,tabs.Width,tabs.Height-1);};
            tabs.Layout+=(s,e)=>{
                float d=ReaderStyle.Scale(tabs);
                int rowHeight=(int)Math.Round(40*d),gap=(int)Math.Round(9*d),x=0,y=0;
                foreach(var tab in new[]{wordTab,lineTab,recentTab}){
                    int width=TextRenderer.MeasureText(tab.Text,ReaderStyle.Tab).Width+
                        TextRenderer.MeasureText(Convert.ToString(tab.Count),ReaderStyle.Small).Width+gap;
                    if(x>0&&x+width>tabs.Width){x=0;y+=rowHeight;}
                    tab.SetBounds(x,y,width,rowHeight);x+=width+gap;
                }
                tabs.Height=y+rowHeight;
            };

            search=new TextBox{Font=ReaderStyle.Hint,Enabled=false};StyleInput(search);
            searchRow=Row(new Panel{Height=34,Visible=false});
            var field=new ReaderField(search,34,true);
            filter=new ReaderToggle("只看有标词的句子",false);
            filter.Click+=(s,e)=>Safe(RefreshList);
            searchRow.Controls.AddRange(new Control[]{field,filter});
            searchRow.Layout+=(s,e)=>{
                float d=ReaderStyle.Scale(searchRow);
                int width=(int)Math.Round(150*d),h=(int)Math.Round(30*d);
                filter.SetBounds(Math.Max(0,searchRow.Width-width),Math.Max(0,(searchRow.Height-h)/2),width,h);
                field.SetBounds(0,0,Math.Max(1,searchRow.Width-(phrases?width+(int)(10*d):0)),searchRow.Height);
            };

            list=new LibraryList();details=new LibraryDetails();
            details.Annotation.Save=(key,value)=>{
                annotations.Save(key,value);
                if(value.Length==0)notesByRow.Remove(key);else notesByRow[key]=value;
                foreach(LibraryEntry entry in list.Items){
                    var line=entry.Value as FavoriteLine;
                    if(line!=null&&line.Row==key)entry.HasNote=value.Length>0;
                }
                list.Invalidate();Program.NotesChanged();
            };
            VisibleChanged+=(s,e)=>{if(!Visible)FlushNotes();};
            panes=new LibraryPane(new ReaderField(list,174),new ReaderField(details,174));
            message=new LibraryMessage{Loading=true,Sentences=phrases};
            content=Row(new Panel{Height=155});
            panes.Dock=message.Dock=DockStyle.Fill;
            panes.Visible=false;content.Controls.AddRange(new Control[]{panes,message});
            undo=MakeButton("已移除 · 撤销",()=>{if(restore!=null)restore();});
            undo.Font=ReaderStyle.Hint;undo.ForeColor=Accent;undo.Dock=DockStyle.Bottom;
            undo.Visible=false;content.Controls.Add(undo);undo.SendToBack();undoTimer.Tick+=(s,e)=>ClearUndo();

            var actions=Footer();
            addLookup=ActionButton(actions,"加入生词本",EditSelected,true);
            saveLine=ActionButton(actions,"收藏原句",()=>{
                var hit=Current() as Lookup;if(hit==null)return;
                favorites.Restore(new FavoriteLine{Row=hit.Word.Row});Program.Changed();Reload();
            });
            addLookup.Visible=saveLine.Visible=false;
            export=ActionButton(actions,phrases?"导出原句":"导出全部",ExportAll,true);
            edit=ActionButton(actions,phrases?"选词补释义":"编辑释义",EditSelected);
            master=ActionButton(actions,"标为已掌握",()=>{
                var n=Current() as Note;if(n==null)return;
                book.Master(n.Id,!n.Mastered);Program.Changed();Reload();
            });
            clearRecent=ActionButton(actions,"清空",()=>{
                if(MessageBox.Show(this,"清空最近查过的全部记录？","最近查过",MessageBoxButtons.YesNo,MessageBoxIcon.Question,MessageBoxDefaultButton.Button2)==DialogResult.Yes){
                    history.Remove(null);Reload();
                }
            });
            clearRecent.Visible=false;
            remove=TrailingButton(phrases?"移除这句":"移除这个词",RemoveSelected);
            export.Enabled=edit.Enabled=master.Enabled=remove.Enabled=false;
            edit.Visible=master.Visible=remove.Visible=false;
            recentRefresh.Tick+=(s,e)=>Safe(()=>{
                recentRefresh.Stop();
                if(RefreshFiles())RefreshList();
                recentRefresh.Enabled=recent&&Visible;
            });
            VisibleChanged+=(s,e)=>recentRefresh.Enabled=recent&&Visible&&book!=null;
            ExpandRow(content,1600);
            search.HandleCreated+=(s,e)=>SetCue();
            search.TextChanged+=(s,e)=>{if(!refreshing)RefreshList();};
            list.SelectedIndexChanged+=(s,e)=>{if(!refreshing)ShowDetails();};
            list.KeyDown+=(s,e)=>{
                if(e.Control&&e.KeyCode==Keys.C&&details.CopyText())e.SuppressKeyPress=true;
            };
            wordTab.Active=!phrases&&!recent;lineTab.Active=phrases;recentTab.Active=recent;
        }
        internal void LoadData(Notebook notebook,FavoritesStore store,List<Note> notes,List<FavoriteLine> sentences,string error){
            book=notebook;favorites=store;words=notes;lines=sentences;sentenceError=error;
            string root=Path.GetDirectoryName(book.FileName);
            annotations=new SentenceNotes(root);notesByRow=annotations.Read();
            history=new RecentStore(root,book.Data);lookups=history.Read();
            message.Loading=false;wordTab.Enabled=lineTab.Enabled=recentTab.Enabled=search.Enabled=true;
            RefreshList();
        }
        internal void LoadFailed(){message.Loading=false;message.Error="载入失败";message.Invalidate();}
        protected override void Dispose(bool disposing){
            if(disposing){recentRefresh.Dispose();undoTimer.Dispose();}
            base.Dispose(disposing);
        }
        void SetCue(){
            string cue=phrases?"搜索原文、译文或已标词语":"搜索词形、读音、原文或中文";
            search.AccessibleName=cue;
            if(search.IsHandleCreated)SendMessage(search.Handle,0x1501,IntPtr.Zero,cue);
        }
        DateTime wordStamp,lineStamp,recentStamp;
        bool RefreshFiles(){
            DateTime w=File.GetLastWriteTimeUtc(book.FileName),l=File.GetLastWriteTimeUtc(favorites.PathName),
                r=File.GetLastWriteTimeUtc(history.PathName),n=File.GetLastWriteTimeUtc(annotations.PathName);
            bool changed=false;
            if(n!=noteStamp){notesByRow=annotations.Read();noteStamp=n;changed=true;}
            if(w!=wordStamp){words=book.EnsureEdition();wordStamp=w;changed=true;}
            if(l!=lineStamp||sentenceError!=null){lines=favorites.TryRead(out sentenceError);lineStamp=l;changed=true;}
            if(r!=recentStamp){lookups=history.Read();recentStamp=r;changed=true;}
            return changed;
        }
        internal void OpenPage(Notebook notebook,bool sentences){
            bool first=book==null;
            if(first){
                string root=Path.GetDirectoryName(notebook.FileName);
                book=notebook;favorites=new FavoritesStore(root,book.Data);history=new RecentStore(root,book.Data);annotations=new SentenceNotes(root);
            }
            bool changed=RefreshFiles();
            var next=sentences?LibraryPage.Sentences:LibraryPage.Words;
            if(first){
                message.Loading=false;wordTab.Enabled=lineTab.Enabled=recentTab.Enabled=search.Enabled=true;
            }
            if(page!=next)Switch(next);
            else if(first||changed)RefreshList();
        }
        void Switch(LibraryPage next){
            if(page==next)return;
            page=next;Text="魔夜读本 · "+(recent?"最近查过":phrases?"收藏夹":"生词本");
            refreshing=true;search.Clear();refreshing=false;
            ClearUndo();RefreshFiles();RefreshList();
        }
        public void ExportAll(){
            if(book==null)return;
            if(recent){SaveExport(history.Export(),"最近查过");return;}
            if(phrases){if(!FlushNotes())return;var rows=favorites.Read();if(rows.Count>0)SaveExport(Exports.SentenceRows(rows,book.Data,annotations.Read()),"收藏夹");}
            else{var rows=book.Read();if(rows.Count>0)SaveExport(Exports.WordRows(rows,book.Data),"生词本");}
        }
        object Current(){var row=list.SelectedItem as LibraryEntry;return row==null?null:row.Value;}
        void Reload(){lookups=history.Read();words=book.Read();lines=favorites.TryRead(out sentenceError);RefreshList();}
        void EditSelected(){
            var hit=Current() as Lookup;
            if(hit!=null){using(var f=new EditorForm(book,dict,hit.Word,Notebook.Find(words,hit.Word)))f.ShowDialog(this);Reload();return;}
            var n=Current() as Note;
            if(n!=null){
                using(var f=new TextEdit(n.Meaning))if(f.ShowDialog(this)==DialogResult.OK){
                    book.Edit(n.Id,f.Value);Program.Changed();Reload();
                }
                return;
            }
            var line=Current() as FavoriteLine;
            if(line!=null){using(var f=new PickerForm(book,dict,line.Row))f.ShowDialog(this);Reload();}
        }
        void RemoveSelected(){
            var hit=Current() as Lookup;if(hit!=null){history.Remove(hit.Word);Reload();return;}
            var line=Current() as FavoriteLine;var note=Current() as Note;
            if(line!=null){
                var removed=favorites.Remove(line.Row);
                if(removed==null){Reload();return;}
                ShowUndo(()=>{if(favorites.Restore(removed))Program.Changed();ClearUndo();Reload();});
            }else if(note!=null){
                var removed=book.Remove(note.Id);
                if(removed==null){Reload();return;}
                ShowUndo(()=>{book.Restore(removed);Program.Changed();ClearUndo();Reload();});
            }else return;
            Program.Changed();Reload();
        }
        void ShowUndo(Action action){restore=action;undo.Visible=true;undoTimer.Stop();undoTimer.Start();}
        void ClearUndo(){undoTimer.Stop();undo.Visible=false;restore=null;content.PerformLayout();}
        void RefreshList(){
            if(book==null)return;
            string q=search.Text.Trim();object previous=Current();
            refreshing=true;list.BeginUpdate();
            try{
                list.Items.Clear();
                if(phrases){
                    foreach(var f in lines){
                        if(filter.Checked&&f.Words.Count==0)continue;
                        var r=book.Data.Rows[f.Row];
                        string note=SentenceNotes.Get(notesByRow,f.Row);
                        if(r.Ja.Contains(q)||r.Zh.Contains(q)||note.Contains(q)||f.Words.Any(w=>w.Form.Contains(q)||Exports.MarkedReading(book.Data,r,w).Contains(q)))
                            list.Items.Add(new LibraryEntry{Ja=r.Ja,Zh=r.Zh,Kana="",Value=f,HasNote=note.Length>0,AccentTag=true,Tag=f.Words.Count==0?"":f.Words.Count+" 词"});
                    }
                }else if(recent){
                    foreach(var hit in lookups){
                        var s=hit.Word;var row=book.Data.Rows[s.Row];
                        if(!(s.Form+hit.Kana+s.Hint+row.Ja+row.Zh).Contains(q))continue;
                        list.Items.Add(new LibraryEntry{Ja=s.Form,Kana=hit.Kana??"",Zh=String.IsNullOrEmpty(s.Hint)?
                            new StringInfo(row.Ja).SubstringByTextElements(0,Math.Min(20,new StringInfo(row.Ja).LengthInTextElements)):s.Hint,
                            Value=hit,Tag=Notebook.Find(words,s)!=null?"已记":"",AccentTag=true});
                    }
                }else foreach(var n in words.OrderBy(x=>x.Base)){
                    bool matches=(n.Base+n.Kana+n.Meaning).Contains(q)||n.Examples.Any(ex=>ex.Row>=0&&ex.Row<book.Data.Rows.Length&&
                        (book.Data.Rows[ex.Row].Ja.Contains(q)||book.Data.Rows[ex.Row].Zh.Contains(q)));
                    if(matches)list.Items.Add(new LibraryEntry{Ja=n.Base,Kana=n.Kana??"",Zh=n.Meaning,Value=n,Tag=n.Mastered?"已掌握":""});
                }
                if(list.Items.Count>0){
                    int index=0;
                    var oldLine=previous as FavoriteLine;var oldNote=previous as Note;
                    for(int i=0;i<list.Items.Count;i++){
                        object value=((LibraryEntry)list.Items[i]).Value;
                        if((phrases&&oldLine!=null&&oldLine.Row==((FavoriteLine)value).Row)||
                            (!phrases&&!recent&&oldNote!=null&&oldNote.Id==((Note)value).Id)||
                            (recent&&previous is Lookup&&((Lookup)value).Matches(((Lookup)previous).Word))){index=i;break;}
                    }
                    list.SelectedIndex=index;
                }
            }finally{list.EndUpdate();refreshing=false;}
            bool empty=recent?lookups.Count==0:phrases?lines.Count==0:words.Count==0;
            wordTab.Active=!phrases&&!recent;lineTab.Active=phrases;recentTab.Active=recent;
            wordTab.Count=words.Count;lineTab.Count=lines.Count;
            wordTab.AccessibleDescription=words.Count.ToString();lineTab.AccessibleDescription=lines.Count.ToString();
            recentTab.Count=lookups.Count;recentTab.AccessibleDescription=lookups.Count.ToString();
            wordTab.Invalidate();lineTab.Invalidate();recentTab.Invalidate();recentTab.Parent.PerformLayout();
            message.Recent=recent;message.Sentences=phrases;message.Error=phrases?sentenceError:null;message.Visible=empty;message.Invalidate();
            message.AccessibleDescription=message.Error??(empty?(recent?"还没有查过的词":phrases?"还没有收藏原句":"还没有生词"):"");
            panes.Visible=!empty;searchRow.Visible=!empty;filter.Visible=phrases;
            recentRefresh.Enabled=recent&&Visible;
            searchRow.PerformLayout();SetCue();
            export.Enabled=!empty;export.Text=phrases?"导出原句":"导出全部";
            edit.Text=phrases?"选词补释义":"编辑释义";edit.Visible=!recent&&!empty;
            master.Visible=!phrases&&!recent&&!empty;
            addLookup.Visible=saveLine.Visible=clearRecent.Visible=recent&&!empty;
            export.BackColor=recent?ReaderStyle.Ink:Accent;export.ForeColor=recent?Body:Field;
            export.FlatAppearance.MouseOverBackColor=recent?ReaderStyle.ButtonHoverFill:ReaderStyle.AccentHover;
            remove.Text=recent?"移除":phrases?"移除这句":"移除这个词";remove.Visible=!empty;
            ShowDetails();Fit();
        }
        void ShowDetails(){
            object item=Current();var note=item as Note;
            addLookup.Enabled=saveLine.Enabled=edit.Enabled=master.Enabled=remove.Enabled=item!=null;
            master.Text=note!=null&&note.Mastered?"标为生词":"标为已掌握";
            var sentence=item as FavoriteLine;
            details.Annotation.Bind(sentence==null?-1:sentence.Row,sentence==null?"":SentenceNotes.Get(notesByRow,sentence.Row));
            details.ShowEntry(book.Data,words,item);
        }
    }
    class TextEdit:PanelForm {
        readonly TextBox field;public string Value{get{return field.Text;}}
        public TextEdit(string value):base("编辑个人释义",480,342){
            Caption("个人释义");field=new ReaderTextBox{Text=value,Multiline=true,MaxLength=4000,Height=80,AccessibleName="个人释义"};ExpandRow(Input(field),900);
            var actions=Footer();ActionButton(actions,"保存",()=>{if(!String.IsNullOrWhiteSpace(Value)){DialogResult=DialogResult.OK;Close();}},true);ActionButton(actions,"取消",Close);
        }
    }
}
