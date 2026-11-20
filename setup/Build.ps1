param([string]$PackagePath,[string]$Output)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if (!$PackagePath) { $PackagePath=Join-Path $project 'dist/mahoyo-yomibito-full.zip' }
if (!$Output) { $Output=Join-Path $project 'dist/魔夜读本安装器.exe' }
$package=(Get-Item -LiteralPath $PackagePath).FullName
$published=[IO.Path]::GetFullPath($Output)
$dest=[IO.Path]::GetDirectoryName($published)
[void][IO.Directory]::CreateDirectory($dest)
$staging=Join-Path $dest ('.mahoyo-setup-'+[Guid]::NewGuid().ToString('N')+'.exe')
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$references=@('/nologo','/codepage:65001','/optimize+','/platform:x64','/target:winexe','/main:Mahoyo.Book.SetupProgram',
    '/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Xml.Linq.dll',
    '/reference:System.Web.Extensions.dll','/reference:System.IO.Compression.dll','/reference:System.IO.Compression.FileSystem.dll')
# The installer uses the same PanelForm, controls, fonts and palette as the reading companion.
$uiSources=@(Get-ChildItem -LiteralPath (Join-Path $project 'companion') -Filter '*.cs' -File |
    Where-Object {$_.Name -notlike '*Tests.cs'} | Select-Object -ExpandProperty FullName)
$setupSources=@(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cs' -File | Select-Object -ExpandProperty FullName)
try {
    $icon=Join-Path $PSScriptRoot 'Book.ico'
    & $compiler @references "/win32icon:$icon" "/resource:$package,Mahoyo.Setup.FullGameZip" "/out:$staging" @uiSources @setupSources
    if($LASTEXITCODE) {throw 'Installer compilation failed.'}
    if([IO.File]::Exists($published)) { [IO.File]::Replace($staging,$published,[NullString]::Value) }
    else { [IO.File]::Move($staging,$published) }
} finally {
    if([IO.File]::Exists($staging)) { [IO.File]::Delete($staging) }
}
Write-Output "Ready: $published"
