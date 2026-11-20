param([Parameter(Mandatory=$true)][string]$ZigPath,[string]$Python="python",[string]$PlayerZip)
$ErrorActionPreference='Stop'
$zig=(Resolve-Path -LiteralPath $ZigPath).Path
if($PlayerZip){$PlayerZip=(Get-Item -LiteralPath $PlayerZip).FullName}
Push-Location $PSScriptRoot
try {
    if($PlayerZip){
        $windowsPS=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
        & $windowsPS -NoProfile -NonInteractive -File (Join-Path $PSScriptRoot 'tools/Prepare-ReleaseData.ps1') -PlayerZip $PlayerZip
        if($LASTEXITCODE){throw 'Release data preparation failed'}
    }
    & .\build.ps1 -ZigPath $zig -Python $Python -PreparedData:([bool]$PlayerZip)
    Push-Location build
    try {
        & .\fp_regression.exe; if($LASTEXITCODE){throw 'Native regression failed'}
        & .\render_test.exe; if($LASTEXITCODE){throw 'Renderer regression failed'}
    } finally {Pop-Location}
    if(!$PlayerZip){
        & $Python tools/full_game.py; if($LASTEXITCODE){throw 'Full story data build failed'}
        & $Python tools/build_word_index.py; if($LASTEXITCODE){throw 'Word index build failed'}
        & $Python tools/validate_full_game.py; if($LASTEXITCODE){throw 'Full story data validation failed'}
    }
    & .\companion\Build.ps1; if($LASTEXITCODE){throw 'Study book build failed'}
    & .\build\fp_regression.exe --full-data; if($LASTEXITCODE){throw 'Full native data regression failed'}
    & .\build\fp_regression.exe --reading-frame; if($LASTEXITCODE){throw 'D3D9 reading frame regression failed'}
    Copy-Item -Path installer/* -Destination package -Force
    Copy-Item -LiteralPath build/d3d9.dll -Destination package/d3d9.dll -Force
    Copy-Item -LiteralPath config/settings.ini -Destination package/mahoyo-learning/settings.ini -Force
    & $Python write_package_manifest.py; if($LASTEXITCODE){throw 'Manifest build failed'}
    Copy-Item docs/player-guide.md package/README.md -Force
    Copy-Item LICENSE,license-research.md package -Force
    New-Item -ItemType Directory -Force package/licenses | Out-Null
    Copy-Item licenses/MinHook-LICENSE.txt,licenses/NotoSerifSC-OFL.txt package/licenses -Force
    & $Python tools/pack_full_game.py; if($LASTEXITCODE){throw 'ZIP creation failed'}
    & .\setup\Build.ps1
    & $Python tools/pack_full_game.py --release; if($LASTEXITCODE){throw 'Player ZIP creation failed'}
} finally {Pop-Location}
