param([Parameter(Mandatory=$true)][string]$PlayerZip)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Scope Local
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$root=Split-Path -Parent $PSScriptRoot
$pins=Get-Content -LiteralPath (Join-Path $root 'corpus/release-data.json') -Raw -Encoding UTF8 | ConvertFrom-Json

function Read-Member($Archive,[string]$Name){
    $entry=$Archive.GetEntry($Name)
    if(!$entry -or $entry.Length -gt 256MB){throw "Missing or oversized release member: $Name"}
    $inputStream=$entry.Open()
    $buffer=[IO.MemoryStream]::new()
    try {$inputStream.CopyTo($buffer);return ,$buffer.ToArray()}
    finally {$buffer.Dispose();$inputStream.Dispose()}
}

# The .NET Framework reflection-only context exposes the embedded ZIP without
# executing the installer. Only data pinned to this source revision is copied.
$outer=[IO.Compression.ZipFile]::OpenRead((Get-Item -LiteralPath $PlayerZip).FullName)
$ready=@{}
try {
    $assembly=[Reflection.Assembly]::ReflectionOnlyLoad([byte[]](Read-Member $outer '魔夜读本安装器.exe'))
    $resource=$assembly.GetManifestResourceStream('Mahoyo.Setup.FullGameZip')
    if(!$resource){throw 'The installer has no full-game payload.'}
    $payload=[IO.Compression.ZipArchive]::new($resource,[IO.Compression.ZipArchiveMode]::Read)
    try {
        foreach($pin in $pins.PSObject.Properties){
            $name=$pin.Name
            if($name -eq 'mahoyo-ja-zh-optional.zip'){
                $data=Read-Member $outer ('可选词典/'+$name)
                $dest=Join-Path $root ('dist/'+$name)
            } elseif($name -eq 'license-research.md'){
                $data=Read-Member $outer $name
                $dest=Join-Path $root ('package/'+$name)
            } else {
                $data=Read-Member $payload ('mahoyo-yomibito/mahoyo-learning/local/'+$name)
                $dest=Join-Path $root ('package/mahoyo-learning/local/'+$name)
            }
            $sha=[Security.Cryptography.SHA256]::Create()
            try {$actual=[BitConverter]::ToString($sha.ComputeHash($data)).Replace('-','')}
            finally {$sha.Dispose()}
            if($actual -ine $pin.Value){throw "Release data differs from this source revision: $name"}
            $ready[$dest]=$data
        }
    } finally {$payload.Dispose();$resource.Dispose()}
} finally {$outer.Dispose()}
foreach($dest in $ready.Keys){
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))
    $staged=$dest+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllBytes($staged,$ready[$dest])
        if([IO.File]::Exists($dest)){[IO.File]::Replace($staged,$dest,[NullString]::Value)}
        else{[IO.File]::Move($staged,$dest)}
    } finally {if([IO.File]::Exists($staged)){[IO.File]::Delete($staged)}}
}
Write-Output "Verified and prepared $($ready.Count) release data files."
