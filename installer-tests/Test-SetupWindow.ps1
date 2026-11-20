param([Parameter(Mandatory=$true)][string]$GameExe,[string]$SetupPath,[string]$Evidence)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if (!$SetupPath) {$SetupPath=Join-Path $project 'dist/魔夜读本安装器.exe'}
if (!$Evidence) {$Evidence=Join-Path $project 'build/setup-window-evidence'}
[void][IO.Directory]::CreateDirectory($Evidence)
$original=(Get-Item -LiteralPath $GameExe).FullName
$fixture=Join-Path $project ('build/setup-ui-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixture)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class BookSetupScreen {
  [StructLayout(LayoutKind.Sequential)] public struct Bounds { public int Left,Top,Right,Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Bounds rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd,IntPtr dc,uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr insert,int x,int y,int w,int h,uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr w,IntPtr l);
  delegate bool Child(IntPtr h,IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(Child callback,IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h,Child callback,IntPtr p);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int length);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
  public static IntPtr FindSetupWindow(uint pid,string caption) {
      IntPtr found=IntPtr.Zero;
      EnumWindows(delegate(IntPtr h,IntPtr p) {
          uint owner;GetWindowThreadProcessId(h,out owner);
          if(owner!=pid||!IsWindowVisible(h))return true;
          var text=new StringBuilder(256);GetWindowText(h,text,text.Capacity);
          if(text.ToString()!=caption)return true;
          found=h;return false;
      },IntPtr.Zero);
      return found;
  }
  public static IntPtr FindVisibleControl(IntPtr root,string name) {
      IntPtr found=IntPtr.Zero;
      EnumChildWindows(root,delegate(IntPtr h,IntPtr p) {
          if(!IsWindowVisible(h))return true;
          var text=new StringBuilder(256);GetWindowText(h,text,text.Capacity);
          if(text.ToString()!=name)return true;
          found=h;return false;
      },IntPtr.Zero);
      return found;
  }
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder name,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,StringBuilder text);
  public static string ReadPath(IntPtr root) {
      string result=null;
      EnumChildWindows(root,delegate(IntPtr h,IntPtr p) {
          var type=new StringBuilder(256);GetClassName(h,type,type.Capacity);
          if(type.ToString().IndexOf(".EDIT.",StringComparison.OrdinalIgnoreCase)<0)return true;
          var text=new StringBuilder(2048);SendMessage(h,0x000D,(IntPtr)text.Capacity,text);result=text.ToString();return false;
      },IntPtr.Zero);
      return result;
  }
}
'@
[void][BookSetupScreen]::SetProcessDPIAware()
$script:setupProcess=$null
$results=@()
function Expect($condition,$message) {if(!$condition){throw $message}}
function HashFile($path) { return (Get-FileHash -LiteralPath $path).Hash }
function Launch([string]$folder) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=(Get-Item -LiteralPath $SetupPath).FullName
    $info.UseShellExecute=$false
    if($folder) {$info.Arguments='"'+$folder+'"'}
    $script:setupProcess=[Diagnostics.Process]::Start($info)
    $until=[DateTime]::UtcNow.AddSeconds(18)
    do {
        Start-Sleep -Milliseconds 90
        $script:setupWindow=[BookSetupScreen]::FindSetupWindow($script:setupProcess.Id,'魔夜读本 · 安装与卸载')
    } until ($script:setupWindow -ne [IntPtr]::Zero -or $script:setupProcess.HasExited -or [DateTime]::UtcNow -gt $until)
    Expect ($script:setupWindow -ne [IntPtr]::Zero) 'Installer did not open.'
    Start-Sleep -Milliseconds 450
}
function Close-Setup {
    if($script:setupProcess -and !$script:setupProcess.HasExited) {
        [void][BookSetupScreen]::PostMessage($script:setupWindow,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
        if(!$script:setupProcess.WaitForExit(9000)){throw 'The installer did not close after the operation.'}
    }
    $script:setupProcess=$null
}
function Element([string]$name) {
    $handle=[BookSetupScreen]::FindVisibleControl($script:setupWindow,$name)
    if($handle -ne [IntPtr]::Zero){return $handle}
}
function Await-Label([string]$name,[scriptblock]$ready) {
    $until=[DateTime]::UtcNow.AddSeconds(45)
    do {
        $found=Element $name
        if($found -and (!$ready -or (& $ready))){return}
        if($name -like '*完成' -and $name -notlike '*未完成' -and ((Element '安装未完成') -or (Element '卸载未完成'))) {throw "Unexpected failure awaiting $name"}
        Start-Sleep -Milliseconds 120
    } while([DateTime]::UtcNow -lt $until)
    throw "Missing result: $name"
}
function Invoke-Button([string]$name) {
    $handle=Element $name
    Expect ($handle -and [BookSetupScreen]::IsWindowEnabled($handle)) "Disabled or missing button: $name"
    [uint32]$ownerPid=0
    [void][BookSetupScreen]::GetWindowThreadProcessId($handle,[ref]$ownerPid)
    Expect ($ownerPid -eq $script:setupProcess.Id) 'Button does not belong to the test window.'
    Expect ([BookSetupScreen]::PostMessage($handle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)) 'Button click was not delivered.'
}
function Capture([string]$name) {
    $h=$script:setupWindow
    $rect=New-Object BookSetupScreen+Bounds
    [void][BookSetupScreen]::GetWindowRect($h,[ref]$rect)
    $image=New-Object System.Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
    $g=[Drawing.Graphics]::FromImage($image);$dc=$g.GetHdc()
    try {Expect ([BookSetupScreen]::PrintWindow($h,$dc,2)) 'Window capture failed.'}
    finally {$g.ReleaseHdc($dc);$g.Dispose()}
    try {$image.Save((Join-Path $Evidence $name),[Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
}
try {
    Launch ''
    $autoPath=[BookSetupScreen]::ReadPath($script:setupWindow)
    Expect ($null -ne $autoPath) 'Default launch did not expose the game directory.'
    if ($autoPath -is [string] -and $autoPath.Length -gt 0) {
        Expect (Test-Path -LiteralPath (Join-Path $autoPath 'WoH.exe')) 'The detected Steam game is missing.'
        $results+='default launch locates a real Steam game (read-only)'
    }
    Capture '默认启动.png';Close-Setup
    $valid=Join-Path $fixture "阅读 & Alice's 游戏"
    [void][IO.Directory]::CreateDirectory($valid)
    Copy-Item -LiteralPath $original -Destination (Join-Path $valid 'WoH.exe')
    $archive=Join-Path ([IO.Path]::GetDirectoryName($original)) 'data00100.hfa'
    if(Test-Path -LiteralPath $archive) {Copy-Item -LiteralPath $archive -Destination (Join-Path $valid 'data00100.hfa')}
    $dll=Join-Path $valid 'd3d9.dll';$mod=Join-Path $valid 'mahoyo-learning'
    Launch $valid;Await-Label '可安装'
    Capture '可安装.png'
    Invoke-Button '安装读本';Await-Label '安装完成'
    Expect ((HashFile $dll) -eq (HashFile (Join-Path $project 'package/d3d9.dll'))) 'Installed DLL hash differs from the package.'
    Expect (Test-Path -LiteralPath (Join-Path $mod 'local/story.dat')) 'Story data missing.'
    Expect (Test-Path -LiteralPath (Join-Path $mod 'local/NotoSerifSC-Medium.ttf')) 'The bundled Medium story font was not installed.'
    Expect ((HashFile (Join-Path $mod 'local/shogakukan.zip')) -eq (HashFile (Join-Path $project 'package/mahoyo-learning/local/shogakukan.zip'))) 'Built-in Shogakukan data differs from the package.'
    Capture '安装完成.png'
    $results+='clean install and Chinese path with shell metacharacters'
    $markers=@{}
    foreach($name in @('personal-words.dat','favorites.tsv','sentence-notes.tsv','recent-lookups.tsv')) {
        $f=Join-Path $mod $name
        [IO.File]::WriteAllText($f,('test-'+$name));$markers[$name]=HashFile $f
    }
    $markers['settings.ini']=HashFile (Join-Path $mod 'settings.ini')
    $receipt=Join-Path $mod 'installed.json'
    $previous=(Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json).installed_at
    Invoke-Button '重新安装'
    Await-Label '安装完成' {
        $now=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
        $now.state -eq 'installed' -and $now.installed_at -ne $previous
    }
    foreach($name in $markers.Keys){Expect ((HashFile (Join-Path $mod $name)) -eq $markers[$name]) ('Reinstall changed '+$name)}
    $results+='reinstall and exact preservation of all five study files'
    Invoke-Button '卸载读本';Await-Label '卸载完成'
    Expect (!(Test-Path -LiteralPath $dll)) 'DLL still active.'
    Expect (@(Get-ChildItem -LiteralPath $valid -Filter 'd3d9.mahoyo-learning.*.disabled').Count -eq 1) 'Expected one disabled backup.'
    foreach($name in $markers.Keys){Expect ((HashFile (Join-Path $mod $name)) -eq $markers[$name]) ('Uninstall changed '+$name)}
    Capture '卸载完成.png'
    Expect ((HashFile (Join-Path $valid 'WoH.exe')) -eq (HashFile $original)) 'Game executable changed.'
    $results+='uninstall and preservation of executable, notes, favourites, history and settings'
    Close-Setup;Launch (Join-Path $valid 'WoH.exe');Await-Label '已卸载'
    Invoke-Button '安装读本';Await-Label '安装完成'
    $results+='reopen from WoH.exe and re-enable'
    $h=$script:setupWindow
    [void][BookSetupScreen]::SetWindowPos($h,[IntPtr]::Zero,0,0,450,475,6)
    Start-Sleep -Milliseconds 240;Capture '窄窗口.png';Close-Setup
    $foreign=Join-Path $fixture '其他组件';[void][IO.Directory]::CreateDirectory($foreign)
    Copy-Item -LiteralPath $original -Destination (Join-Path $foreign 'WoH.exe')
    $otherDll=Join-Path $foreign 'd3d9.dll';[IO.File]::WriteAllText($otherDll,'another component')
    $foreignHash=HashFile $otherDll
    Launch $foreign;Await-Label '游戏目录中已有其他组件'
    Expect (![BookSetupScreen]::IsWindowEnabled((Element '安装读本')) -and ![BookSetupScreen]::IsWindowEnabled((Element '卸载读本'))) 'Conflict actions should be disabled.'
    Expect ((HashFile $otherDll) -eq $foreignHash) 'A foreign DLL changed.';Close-Setup
    $results+='foreign component detected and protected'
    $bad=Join-Path $fixture '其他游戏版本';[void][IO.Directory]::CreateDirectory($bad)
    [IO.File]::WriteAllText((Join-Path $bad 'WoH.exe'),'unsupported build test')
    Launch $bad;Await-Label '可安装';Invoke-Button '安装读本';Await-Label '安装未完成'
    Expect (!(Test-Path -LiteralPath (Join-Path $bad 'd3d9.dll'))) 'Unsupported build was changed.'
    Expect (Element '运行详情') 'Missing failure details.';Capture '版本不匹配.png';Close-Setup
    $results+='incompatible build reported in Chinese without game writes'
    (@{passed=$results;setup_sha256=(HashFile $SetupPath);fixture=$fixture} | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $Evidence '实测.json') -Encoding utf8
    $results | Write-Output
} finally { Close-Setup }

