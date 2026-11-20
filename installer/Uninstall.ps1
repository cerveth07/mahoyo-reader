param([string]$GamePath)
# Use the hashing and JSON commands shipped with the running PowerShell edition.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Scope Local -ErrorAction Stop
if (-not $GamePath) {
    $steam=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { throw 'Game not found. Drag WoH.exe onto the CMD launcher, or pass -GamePath with its folder.' }
    $libraries=@($steam)
    $config=Join-Path $steam 'steamapps/libraryfolders.vdf'
    if (Test-Path -LiteralPath $config) {
        foreach ($line in Get-Content -LiteralPath $config) {
            if ($line -match '^\s*"path"\s+"([^"]+)"') { $libraries += $Matches[1].Replace('\\','\') }
        }
    }
    foreach ($library in $libraries) {
        $candidate=Join-Path $library 'steamapps/common/WITCH ON THE HOLY NIGHT'
        if (Test-Path -LiteralPath (Join-Path $candidate 'WoH.exe')) { $GamePath=$candidate; break }
    }
    if (-not $GamePath) { throw 'Game not found. Drag WoH.exe onto the CMD launcher, or pass -GamePath with its folder.' }
}
$ErrorActionPreference='Stop'
if ((Test-Path -LiteralPath $GamePath -PathType Leaf) -and ([IO.Path]::GetFileName($GamePath) -ieq 'WoH.exe')) {
    $GamePath = (Get-Item -LiteralPath $GamePath).DirectoryName
}
$gameRoot=[IO.Path]::GetFullPath($GamePath)
if ($gameRoot.Length -gt [IO.Path]::GetPathRoot($gameRoot).Length) { $gameRoot=$gameRoot.TrimEnd('\') }
if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'WoH.exe') -PathType Leaf)) { throw 'Invalid game directory.' }
if (Get-Process -Name 'WoH' -ErrorAction SilentlyContinue) { throw 'Close the game before uninstalling the sample.' }
$manifest=Join-Path $gameRoot 'mahoyo-learning\installed.json'
$dll=Join-Path $gameRoot 'd3d9.dll'
if (-not (Test-Path -LiteralPath $dll)) { Write-Output 'The study DLL is already disabled.'; exit }
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw 'The DLL path is not a file; no file was changed.' }
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'No study-mod ownership record; no file was changed.' }
$owner=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$expectedGame='7a718cfca3fccafd9d7984e41206473a6152098d6e0f7374c52abf1581b6ad9a'
if ($owner.sample -notin @('chapter01','full-game') -or $owner.game_sha256 -ne $expectedGame -or
    [string]$owner.dll_sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid ownership record; no file was changed.' }
if ($owner.format -and $owner.format -ne 'mahoyo-learning-installed-v2') { throw 'Unsupported ownership record; no file was changed.' }
$state=[string]$owner.state
if ($state -and $state -notin @('installed','installing')) { throw 'Unsupported installation state; no file was changed.' }
$accepted=@(([string]$owner.dll_sha256).ToLowerInvariant())
if ($state -eq 'installing' -and $owner.previous_dll_sha256) {
    if ([string]$owner.previous_dll_sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid pending ownership record; no file was changed.' }
    $accepted+=([string]$owner.previous_dll_sha256).ToLowerInvariant()
}
if ($accepted -notcontains (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'DLL ownership check failed; no file was changed.' }
# Keep the intentional disabled backup, including after a pending installation.
# A unique suffix avoids collisions when uninstalling twice in the same second.
$disabled=Join-Path $gameRoot ('d3d9.mahoyo-learning.'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'.'+[Guid]::NewGuid().ToString('N').Substring(0,8)+'.disabled')
Move-Item -LiteralPath $dll -Destination $disabled
Write-Output 'Study mod disabled. Local notes, settings, logs, and the disabled DLL backup were retained.'
Write-Output 'The next launch uses the original game.'
