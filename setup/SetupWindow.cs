using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Reflection;

[assembly: AssemblyTitle("魔夜读本安装器")]
[assembly: AssemblyProduct("魔夜读本")]

namespace Mahoyo.Book {
    static class SetupProgram {
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

        [STAThread]
        static void Main(string[] args) {
            SetProcessDPIAware();
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            string target="";
            try {target=args.Length==0 ? SetupPackage.FindGame() : SetupPackage.GameFolder(args[0]);}
            catch(Exception) { /* The user can select a game manually. */ }
            Application.Run(new SetupWindow(target));
        }
    }

    sealed class SetupWindow : PanelForm {
        readonly TextBox pathBox = new TextBox();
        readonly Button browse, install, uninstall, details;
        readonly Label status, message;
        readonly Timer debounce = new Timer{ Interval=240 };
        bool working;
        string lastError="";
        string target="";
        SetupState state;

        internal SetupWindow(string initial) : base("安装与卸载", 610, 350, true) {
            Text="魔夜读本 · 安装与卸载";
            Icon=System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            Heading("安装与卸载");
            Caption("《魔法使之夜》双语阅读 MOD");
            Rule();
            var folderLabel=LabelRow("游戏目录",25);
            folderLabel.Font=ReaderStyle.Label;
            pathBox.AccessibleName="游戏目录";
            pathBox.Name="GamePath";
            pathBox.Font=ReaderStyle.BodyFont;
            StyleInput(pathBox);
            var field=new ReaderField(pathBox,32) {Dock=DockStyle.Fill,Margin=new Padding(0,0,8,0)};
            browse=MakeButton("浏览…",Browse);
            browse.Name="Browse"; browse.Dock=DockStyle.Fill;
            browse.Margin=Padding.Empty;
            var paths=new TableLayoutPanel{Height=39,ColumnCount=2,RowCount=1,Padding=Padding.Empty,Margin=Padding.Empty};
            paths.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
            paths.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            paths.RowStyles.Add(new RowStyle(SizeType.Percent,100));
            paths.Controls.Add(field,0,0);paths.Controls.Add(browse,1,0); Row(paths);
            status=LabelRow("",30);status.Name="State";status.Font=ReaderStyle.Label;
            message=Row(new Label{Height=55, TextAlign=ContentAlignment.TopLeft,ForeColor=ReaderStyle.Muted,UseMnemonic=false});
            message.Name="Result";
            message.SizeChanged+=(sender,e)=>FitMessage();
            message.TextChanged+=(sender,e)=>FitMessage();
            var bar=Footer();
            install=ActionButton(bar,"安装读本",()=>StartAction(true),true);install.Name="Install";
            uninstall=ActionButton(bar,"卸载读本",()=>StartAction(false));uninstall.Name="Uninstall";
            details=TrailingButton("运行详情",ShowDetails);details.Name="Details";details.Visible=false;

            pathBox.TextChanged+=(sender,e)=>{debounce.Stop();debounce.Start();};
            debounce.Tick+=(sender,e)=>{debounce.Stop();if (!working) RefreshTarget();};
            pathBox.KeyDown+=(sender,e)=>{if(e.KeyCode==Keys.Enter){e.SuppressKeyPress=true;debounce.Stop();RefreshTarget();}};
            pathBox.Text=initial;
            Shown+=(sender,e)=>{
                CenterToScreen();debounce.Stop();RefreshTarget();
                pathBox.Select(0,0);pathBox.ScrollToCaret();
                if (install.Enabled) install.Focus(); else browse.Focus();
            };
            FormClosing+=(sender,e)=>{if(working)e.Cancel=true;};
            FormClosed+=(sender,e)=>debounce.Dispose();
        }

        void Browse() {
            using(var dialog=new OpenFileDialog{Title="选择《魔法使之夜》的 WoH.exe",Filter="游戏主程序 (WoH.exe)|WoH.exe",FileName="WoH.exe",CheckFileExists=true}) {
                if (Directory.Exists(target)) dialog.InitialDirectory=target;
                if (dialog.ShowDialog(this)==DialogResult.OK) {
                    pathBox.Text=Path.GetDirectoryName(dialog.FileName);
                    debounce.Stop();RefreshTarget();
                }
            }
        }

        void RefreshTarget() {
            lastError="";details.Visible=false;
            try {target=SetupPackage.GameFolder(pathBox.Text);state=SetupPackage.Inspect(target);}
            catch(Exception ex) {target="";state=SetupState.Missing;lastError=ex.Message;}
            switch(state) {
                case SetupState.Installed:
                    Say("已安装","可以重新安装或卸载读本。卸载后，生词、收藏、批注和设置都会保留。",false); break;
                case SetupState.Pending:
                    Say("上次安装尚未完成","可重新安装以恢复读本，或卸载已登记的版本。",false); break;
                case SetupState.Disabled:
                    Say("已卸载","可以重新安装读本，继续使用原有的学习记录。",false); break;
                case SetupState.Ready:
                    Say("可安装","请先退出游戏，再点「安装读本」。",false); break;
                case SetupState.Conflict:
                    Say("游戏目录中已有其他组件","d3d9.dll 的归属无法确认，请检查该目录中已安装的 MOD。",true); break;
                default:
                    Say("选择游戏目录","点「浏览…」选择 WoH.exe，或在上方填入游戏目录。",false); break;
            }
            ControlsForState();
        }

        void Say(string headline,string body,bool error) {
            status.Text=headline;status.ForeColor=error?ReaderStyle.Danger:ReaderStyle.Accent;
            message.Text=body;
        }

        void FitMessage() {
            if (message.Width<50 || String.IsNullOrEmpty(message.Text)) return;
            var flags=TextFormatFlags.WordBreak|TextFormatFlags.TextBoxControl|TextFormatFlags.NoPrefix;
            int h=TextRenderer.MeasureText(message.Text,message.Font,new Size(message.Width-5,Int32.MaxValue),flags).Height+8;
            if (message.Height!=h) message.Height=h;
        }

        void ControlsForState() {
            browse.Enabled=!working;pathBox.ReadOnly=working;
            bool installed=state==SetupState.Installed || state==SetupState.Pending;
            install.Text=installed?"重新安装":"安装读本";
            install.Enabled=!working && state!=SetupState.Missing && state!=SetupState.Conflict;
            uninstall.Enabled=!working && installed;
            details.Visible=lastError.Length>0; Fit();
        }

        async void StartAction(bool apply) {
            if (working) return;
            debounce.Stop();RefreshTarget();
            if ((apply && !install.Enabled) || (!apply && !uninstall.Enabled)) return;
            string game=target;
            working=true;ControlsForState();
            Say(apply?"正在安装…":"正在卸载…",apply?"正在校验并安装读本文件。":"正在禁用读本，保留学习记录。",false);
            try {
                await Task.Run(()=>SetupPackage.Run(game,apply));
                working=false;RefreshTarget();
                bool finished=apply ? state==SetupState.Installed : state==SetupState.Disabled || state==SetupState.Ready;
                if (!finished) throw new IOException("安装器运行结束，所选目录的状态未能核实。");
                Say(apply?"安装完成":"卸载完成",apply
                    ?"从 Steam 启动游戏，在游戏设置中选择「日本語」。进入剧情后，点右侧的悬浮球即可开始阅读。"
                    :"下次启动即为原版。生词、收藏、批注和设置已保留。",false);
            } catch(Exception ex) {
                working=false;RefreshTarget();lastError=ex.GetBaseException().Message;
                string explain=lastError.StartsWith("另一个安装器",StringComparison.Ordinal)?lastError:SetupPackage.FriendlyError(lastError);
                Say(apply?"安装未完成":"卸载未完成",explain,true);
            } finally {working=false;ControlsForState();}
        }

        void ShowDetails() {
            if(lastError.Length==0)return;
            MessageBox.Show(this,lastError,"魔夜读本 · 运行详情",MessageBoxButtons.OK,MessageBoxIcon.Information);
        }
    }
}
