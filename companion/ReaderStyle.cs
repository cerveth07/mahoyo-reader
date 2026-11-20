using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace Mahoyo.Book {
    // Both WinForms TextRenderer (GDI) and System.Drawing (GDI+) need the private
    // font. Keep its collection and Font objects for the lifetime of the reader.
    static class StoryTypeface {
        [DllImport("gdi32.dll", CharSet=CharSet.Unicode)]
        static extern int AddFontResourceEx(string file,uint flags,IntPtr reserved);
        static readonly System.Drawing.Text.PrivateFontCollection fonts=new System.Drawing.Text.PrivateFontCollection();
        static Font small,detail;
        internal static Font Small {get{return small??ReaderStyle.Small;}}
        internal static Font Detail {get{return detail??ReaderStyle.DetailChinese;}}
        internal static bool Load(string directory){
            if(small!=null)return true;
            string file=Path.Combine(directory,"NotoSerifSC-Medium.ttf");
            if(!File.Exists(file)||AddFontResourceEx(file,0x10,IntPtr.Zero)==0)return false;
            fonts.AddFontFile(file);
            var family=fonts.Families[0];
            small=new Font(family,12*0.75f,FontStyle.Regular);
            detail=new Font(family,15*0.75f,FontStyle.Regular);
            return true;
        }
    }

    static class ReaderStyle {
        internal static readonly Color Ink=Color.FromArgb(23,36,47);
        internal static readonly Color Card=Color.FromArgb(245,20,31,40);
        internal static readonly Color Field=Color.FromArgb(16,29,38);
        internal static readonly Color Raised=Color.FromArgb(27,41,50);
        internal static readonly Color Line=Color.FromArgb(62,80,88);
        internal static readonly Color Body=Color.FromArgb(238,236,224);
        internal static readonly Color Muted=Color.FromArgb(183,198,197);
        internal static readonly Color Dim=Color.FromArgb(143,163,163);
        internal static readonly Color Accent=Color.FromArgb(223,200,154);
        internal static readonly Color AccentHover=Color.FromArgb(235,218,182);
        internal static readonly Color Danger=Color.FromArgb(196,163,160);
        internal static readonly Color HoverTint=Color.FromArgb(26,223,200,154);
        internal static readonly Color Caption=Color.FromArgb(240,236,227);

        internal static readonly Color CloseHoverFill=Color.FromArgb(60,69,71); // Close control hover and keyboard focus.
        internal static readonly Color DisabledButtonFill=Color.FromArgb(33,45,52); // Disabled button surface.
        internal static readonly Color DisabledButtonBorder=Color.FromArgb(63,76,80); // Disabled button outline.
        internal static readonly Color DisabledButtonText=Color.FromArgb(155,166,166); // Disabled button label.
        internal static readonly Color PassageRule=Color.FromArgb(151,139,106); // Muted gold rule beside the original passage.
        internal static readonly Color TitleGradientStart=Color.FromArgb(29,49,62); // Lighter end of the custom title gradient.
        internal static readonly Color ButtonHoverFill=Color.FromArgb(44,62,71); // Opaque hover fill for secondary buttons.

        [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]
        static extern uint GetPrivateProfileString(string section,string key,string fallback,StringBuilder value,uint size,string path);
        static string JapaneseOverride(){
            if(String.IsNullOrEmpty(Program.ModRoot))return "";
            var value=new StringBuilder(33);
            GetPrivateProfileString("study","ui_japanese_face","",value,(uint)value.Capacity,Path.Combine(Program.ModRoot,"settings.ini"));
            return value.Length<32?value.ToString():"";
        }
        static FontFamily Installed(FontFamily fallback,params string[] families){
            foreach(string family in families){
                if(String.IsNullOrEmpty(family))continue;
                try{return new FontFamily(family);}catch(ArgumentException){}
            }
            return fallback;
        }
        // Share these application-lifetime fonts across the auxiliary windows.
        static readonly FontFamily Chinese=Installed(FontFamily.GenericSansSerif,
            "SimSun","宋体","Noto Serif CJK SC","Source Han Serif SC","Noto Serif SC",
            "Microsoft YaHei UI","Microsoft YaHei","Noto Sans SC","Microsoft JhengHei UI");
        static readonly FontFamily Japanese=Installed(Chinese,JapaneseOverride(),"BIZ UDPMincho Medium","BIZ UDPMincho","Meiryo","Yu Gothic UI","Noto Sans JP");
        // Defer field initialization until Program has resolved the mod root.
        static ReaderStyle(){}
        static Font JapaneseFont(float size,FontStyle preferred){
            if(Japanese.IsStyleAvailable(preferred))return new Font(Japanese,size,preferred);
            foreach(FontStyle style in new[]{FontStyle.Regular,FontStyle.Bold,FontStyle.Italic,FontStyle.Bold|FontStyle.Italic})
                if(Japanese.IsStyleAvailable(style))return new Font(Japanese,size,style);
            return new Font(Chinese,size,preferred);
        }
        internal static readonly Font BodyFont=new Font(Chinese,12);
        internal static readonly Font Heading=new Font(Chinese,21);
        internal static readonly Font Label=new Font(Chinese,11,FontStyle.Bold);
        internal static readonly Font JapaneseBody=JapaneseFont(12,FontStyle.Regular);
        internal static readonly Font Headword=JapaneseFont(21,FontStyle.Bold);

        // Point equivalents keep the mock's logical pixel sizes under DPI scaling.
        internal static readonly Font RowJapanese=JapaneseFont(15*0.75f,FontStyle.Regular);
        internal static readonly Font DetailJapanese=JapaneseFont(21*0.75f,FontStyle.Regular);
        internal static readonly Font Small=new Font(Chinese,12*0.75f);
        internal static readonly Font Hint=new Font(Chinese,13*0.75f);
        internal static readonly Font WordMeaning=new Font(Chinese,17);
        internal static readonly Font DetailChinese=new Font(Chinese,15*0.75f);
        internal static readonly Font Tab=new Font(Chinese,19*0.75f);
        internal static readonly Font EmptyTitle=new Font(Chinese,18*0.75f);
        internal static readonly Color FieldLine=Color.FromArgb(39,55,63);

        internal static void Surface(Graphics g,Rectangle r,float radius,Color fill,Color border,bool dashed=false){
            if(r.Width<2||r.Height<2)return;
            g.SmoothingMode=SmoothingMode.AntiAlias;
            using(var path=Rounded(new RectangleF(r.X+0.5f,r.Y+0.5f,r.Width-1,r.Height-1),radius))
            using(var brush=new SolidBrush(fill))
            using(var pen=new Pen(border)){
                if(dashed)pen.DashStyle=DashStyle.Dash;
                g.FillPath(brush,path);g.DrawPath(pen,path);
            }
        }

        internal static float Scale(Control control){
            using(var dc=control.IsHandleCreated?control.CreateGraphics():Graphics.FromHwnd(IntPtr.Zero))return dc.DpiX/96f;
        }

        internal static GraphicsPath Rounded(RectangleF rect,float radius){
            var path=new GraphicsPath();
            float d=Math.Min(2*radius,Math.Min(rect.Width,rect.Height));
            if(d<1){path.AddRectangle(rect);return path;}
            path.AddArc(rect.Left,rect.Top,d,d,180,90);
            path.AddArc(rect.Right-d,rect.Top,d,d,270,90);
            path.AddArc(rect.Right-d,rect.Bottom-d,d,d,0,90);
            path.AddArc(rect.Left,rect.Bottom-d,d,d,90,90);
            path.CloseFigure();return path;
        }
    }

    // A portrait guide reads from each heading to its paragraph. A wider guide can use columns.
    sealed class GuideRow:Panel {
        readonly Label name,description;
        readonly bool rule;
        public GuideRow(string title,string text,bool drawRule){
            rule=drawRule;
            name=new Label{Text=title,Font=ReaderStyle.Label,ForeColor=PanelForm.Accent,TextAlign=ContentAlignment.TopLeft};
            description=new Label{Text=text,ForeColor=PanelForm.Body,TextAlign=ContentAlignment.TopLeft};
            Controls.AddRange(new Control[]{name,description});AccessibleName=title;ResizeRedraw=true;
        }
        protected override void OnLayout(LayoutEventArgs e){
            base.OnLayout(e);if(description==null||ClientSize.Width<1)return;
            float scale=ReaderStyle.Scale(this);
            bool stacked=Width<520*scale;
            int pad=(int)Math.Round((stacked?20:10)*scale);
            int column=stacked?0:(int)Math.Round(130*scale);
            int bodyWidth=Math.Max(1,Width-column);
            var flags=TextFormatFlags.WordBreak|TextFormatFlags.NoPrefix|TextFormatFlags.TextBoxControl;
            int bodyHeight=TextRenderer.MeasureText(description.Text,Font,new Size(bodyWidth,Int32.MaxValue),flags).Height+2;
            int top=stacked?pad+name.Font.Height+(int)Math.Round(8*scale):pad;
            name.SetBounds(0,pad,stacked?Width:column,name.Font.Height+2);
            description.SetBounds(column,top,bodyWidth,bodyHeight);
            int needed=Math.Max(name.Bottom,description.Bottom)+pad+2;
            if(Height!=needed)Height=needed;
        }
        protected override void OnPaint(PaintEventArgs e){
            base.OnPaint(e);
            if(rule)using(var pen=new Pen(PanelForm.Line))e.Graphics.DrawLine(pen,0,Height-1,Width,Height-1);
        }
    }

    sealed class LibraryPane:Panel {
        readonly Control entries,details;
        public LibraryPane(Control entries,Control details){
            this.entries=entries;this.details=details;Height=174;
            Controls.AddRange(new[]{entries,details});
        }
        protected override void OnLayout(LayoutEventArgs e){
            base.OnLayout(e);
            if(details==null||Width<2||Height<2)return;
            float scale=ReaderStyle.Scale(this);
            bool wide=ClientSize.Width>=620*scale;
            int length=wide?ClientSize.Width:ClientSize.Height;
            int gap=Math.Min(length-2,(int)Math.Round(14*scale));
            int first=(length-gap)*41/100;
            if(wide){
                entries.SetBounds(0,0,first,Height);
                details.SetBounds(first+gap,0,Width-first-gap,Height);
            }else{
                entries.SetBounds(0,0,Width,first);
                details.SetBounds(0,first+gap,Width,Height-first-gap);
            }
        }
    }
}
