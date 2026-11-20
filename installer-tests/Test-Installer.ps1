param([string]$Workspace=(Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$script:expectedGame='7a718cfca3fccafd9d7984e41206473a6152098d6e0f7374c52abf1581b6ad9a'
$script:sourcePackage=Join-Path $Workspace 'package'
$script:sourceExe=Join-Path $Workspace 'backups\WoH.exe'
$script:testRoot=Join-Path $PSScriptRoot ('run-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $script:testRoot | Out-Null
$script:results=New-Object 'System.Collections.Generic.List[object]'

function Assert-Test($Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Sha([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
Assert-Test ((Sha $script:sourceExe) -eq $script:expectedGame) 'Original backup EXE identity changed.'

# Load only exact production function ASTs; do not invoke its default game path.
# This enables deterministic failures at manifest commit without a production test flag.
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:sourcePackage 'Install.ps1'),[ref]$tokens,[ref]$parseErrors)
Assert-Test ($parseErrors.Count -eq 0) 'Install.ps1 parse errors.'
foreach ($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)) {
    . ([ScriptBlock]::Create($definition.Extent.Text))
}
foreach ($name in @('Uninstall.ps1')) {
    $tokens=$null;$parseErrors=$null
    [Management.Automation.Language.Parser]::ParseFile((Join-Path $script:sourcePackage $name),[ref]$tokens,[ref]$parseErrors) | Out-Null
    Assert-Test ($parseErrors.Count -eq 0) ($name+' parse errors.')
}
$script:originalOwnership=(Get-Item Function:\Write-StudyOwnership).ScriptBlock
$script:originalAtomic=(Get-Item Function:\Set-StudyAtomicFile).ScriptBlock
$script:originalGameGuard=(Get-Item Function:\Assert-StudyGameClosed).ScriptBlock

function New-IntegrityManifest([string]$PackageRoot) {
    $entries=@()
    foreach ($relative in @('d3d9.dll','mahoyo-learning/local/story.dat','mahoyo-learning/local/provenance.json','mahoyo-learning/local/StudyBook.exe','mahoyo-learning/local/word-index.dat','mahoyo-learning/local/NotoSerifSC-Medium.ttf','mahoyo-learning/local/NotoSerifSC-OFL.txt','mahoyo-learning/local/shogakukan.zip')) {
        $file=Join-Path $PackageRoot $relative.Replace('/','\')
        $entries += [ordered]@{path=$relative;sha256=(Sha $file);bytes=(Get-Item -LiteralPath $file).Length}
    }
    [ordered]@{format='mahoyo-learning-package-v1';files=$entries} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $PackageRoot 'package-manifest.json') -Encoding UTF8
}
function New-Fixture([string]$Name,[switch]$Owned,[switch]$Unknown) {
    $root=Join-Path $script:testRoot $Name
    $game=Join-Path $root 'game'
    $package=Join-Path $root 'package'
    New-Item -ItemType Directory -Path $game,(Join-Path $game 'UserData'),(Join-Path $game 'Archive'),(Join-Path $package 'mahoyo-learning\local') -Force | Out-Null
    Copy-Item -LiteralPath $script:sourceExe -Destination (Join-Path $game 'WoH.exe')
    [IO.File]::WriteAllText((Join-Path $game 'UserData\save-slot.dat'),'do-not-touch-save')
    [IO.File]::WriteAllText((Join-Path $game 'Archive\data00100.hfa'),'do-not-touch-archive')
    foreach ($relative in @('d3d9.dll','mahoyo-learning\local\story.dat','mahoyo-learning\local\provenance.json','mahoyo-learning\settings.ini','mahoyo-learning\local\StudyBook.exe','mahoyo-learning\local\word-index.dat','mahoyo-learning\local\NotoSerifSC-Medium.ttf','mahoyo-learning\local\NotoSerifSC-OFL.txt','mahoyo-learning\local\shogakukan.zip')) {
        Copy-Item -LiteralPath (Join-Path $script:sourcePackage $relative) -Destination (Join-Path $package $relative)
    }
    New-IntegrityManifest $package
    if ($Owned) {
        New-Item -ItemType Directory -Path (Join-Path $game 'mahoyo-learning\local') -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $game 'd3d9.dll'),'old-owned-study-proxy')
        [IO.File]::WriteAllText((Join-Path $game 'mahoyo-learning\local\story.dat'),'old-study-data')
        [IO.File]::WriteAllText((Join-Path $game 'mahoyo-learning\local\provenance.json'),'old-study-provenance')
        [IO.File]::WriteAllText((Join-Path $game 'mahoyo-learning\settings.ini'),'user-preferences-must-survive')
        [ordered]@{sample='chapter01';dll_sha256=(Sha (Join-Path $game 'd3d9.dll'));game_sha256=$script:expectedGame;installed_at='legacy'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $game 'mahoyo-learning\installed.json') -Encoding UTF8
    }
    if ($Unknown) { [IO.File]::WriteAllText((Join-Path $game 'd3d9.dll'),'unrelated-proxy-do-not-overwrite') }
    return @{game=$game;package=$package;root=$root}
}
function Snapshot([string]$Root) {
    $result=@{}
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File) { $result[$file.FullName.Substring($Root.Length)]=Sha $file.FullName }
    return $result
}
function Assert-SnapshotEqual($Before,[string]$Root) {
    $after=Snapshot $Root
    Assert-Test ($Before.Count -eq $after.Count) 'File inventory changed after failed installation.'
    foreach ($key in $Before.Keys) { Assert-Test ($after[$key] -eq $Before[$key]) ('Previous file was not restored: '+$key) }
}
function Expect-Failure([ScriptBlock]$Action,[string]$Pattern) {
    $message=$null
    try { & $Action | Out-Null } catch { $message=$_.Exception.Message }
    Assert-Test ([bool]$message) 'Expected failure did not occur.'
    Assert-Test ($message -like ('*'+$Pattern+'*')) ('Unexpected failure: '+$message)
    return $message
}
function Reset-Faults {
    Set-Item Function:\Write-StudyOwnership $script:originalOwnership
    Set-Item Function:\Set-StudyAtomicFile $script:originalAtomic
    Set-Item Function:\Assert-StudyGameClosed $script:originalGameGuard
}
function Run-Test([string]$Name,[ScriptBlock]$Action) {
    try { & $Action; $script:results.Add(@{name=$Name;passed=$true}); Write-Output ('PASS '+$Name) }
    catch { $script:results.Add(@{name=$Name;passed=$false;error=$_.Exception.Message}); Write-Output ('FAIL '+$Name+': '+$_.Exception.Message) }
    finally { Reset-Faults }
}
function Run-Uninstall([string]$Game) {
    $output=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:sourcePackage 'Uninstall.ps1') -GamePath $Game 2>&1
    Assert-Test ($LASTEXITCODE -eq 0) ('Uninstall failed: '+($output -join ' '))
}
function Run-CmdWrapper([string]$Script,[string]$Game,[switch]$Dropped) {
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$env:ComSpec
    $option=if ($Dropped) { ' ' } else { ' -GamePath ' }
    $start.Arguments='/d /c ""'+$Script+'"'+$option+'"'+$Game+'""'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process
    $process.StartInfo=$start
    [void]$process.Start();$process.StandardInput.WriteLine('');$process.StandardInput.Close()
    if (-not $process.WaitForExit(20000)) { $process.Kill();throw 'CMD wrapper timed out.' }
    $output=$process.StandardOutput.ReadToEnd()+$process.StandardError.ReadToEnd()
    $code=$process.ExitCode;$process.Dispose()
    return @{code=$code;output=$output}
}

Run-Test 'fresh install and uninstall preserve game archives and saves' {
    $f=New-Fixture 'fresh';$before=Snapshot $f.game
    Install-StudyMod $f.game $f.package | Out-Null
    $owner=Get-Content -LiteralPath (Join-Path $f.game 'mahoyo-learning\installed.json') -Raw | ConvertFrom-Json
    Assert-Test ($owner.state -eq 'installed' -and $owner.format -eq 'mahoyo-learning-installed-v2') 'Missing committed ownership.'
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $f.game 'mahoyo-learning\dictionary-sources.xml'))) 'Base install wrote the player dictionary configuration.'
    Assert-Test ((Sha (Join-Path $f.game 'd3d9.dll')) -eq (Sha (Join-Path $f.package 'd3d9.dll'))) 'DLL copy mismatch.'
    foreach ($fontItem in @('NotoSerifSC-Medium.ttf','NotoSerifSC-OFL.txt','shogakukan.zip')) {
        $relative='mahoyo-learning/local/'+$fontItem
        Assert-Test ((Sha (Join-Path $f.game $relative)) -eq (Sha (Join-Path $f.package $relative))) ('Installed data mismatch: '+$fontItem)
    }
    foreach ($key in $before.Keys) { Assert-Test ((Sha ($f.game+$key)) -eq $before[$key]) ('Game/save file changed: '+$key) }
    Run-Uninstall $f.game
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll'))) 'Uninstall left active DLL.'
    Assert-Test (@(Get-ChildItem -LiteralPath $f.game -Filter '*.disabled').Count -eq 1) 'Disabled safety backup missing.'
}
Run-Test 'optional dictionary installs from the PowerShell entry with a content path beyond MAX_PATH' {
    $f=New-Fixture 'optional-dictionary'
    $game=$f.game
    $example=Join-Path $game ('mahoyo-learning\dictionaries\'+('a'*64)+'.tsv')
    if($example.Length -lt 261){
        $game=Join-Path $game ('library'.PadRight([Math]::Max(7,260-$example.Length),'x'))
        [void][IO.Directory]::CreateDirectory($game)
        Copy-Item -LiteralPath (Join-Path $f.game 'WoH.exe') -Destination $game
    }
    $dictionaryPack=Join-Path $Workspace 'dist\mahoyo-ja-zh-optional.zip'
    $result=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:sourcePackage 'Install.ps1') -GamePath $game -DictionaryPack $dictionaryPack 2>&1
    Assert-Test ($LASTEXITCODE -eq 0) ('Optional install failed: '+($result -join ' '))
    $sources=Join-Path $game 'mahoyo-learning\dictionary-sources.xml'
    Assert-Test (Test-Path -LiteralPath $sources) 'Optional dictionary configuration missing.'
    # Enumerate names directly: the Windows PowerShell 5.1 provider tries to
    # stat the long filename with the legacy path limit when it wraps it.
    $tables=[IO.Directory]::GetFiles((Join-Path $game 'mahoyo-learning\dictionaries'),'*.tsv')
    Assert-Test ($tables.Count -eq 1 -and $tables[0].Length -ge 261) 'Expected one optional word list beyond MAX_PATH.'
}
Run-Test 'legacy upgrade preserves settings, personal words, favorites and dictionaries' {
    $f=New-Fixture 'upgrade' -Owned;$settings=Sha (Join-Path $f.game 'mahoyo-learning\settings.ini')
    $personal=@('personal-words.dat','favorites.tsv','dictionary-sources.xml') | ForEach-Object {
        $file=Join-Path (Join-Path $f.game 'mahoyo-learning') $_
        [IO.File]::WriteAllText($file,'existing player data '+$_)
        [PSCustomObject]@{Path=$file;Hash=(Sha $file)}
    }
    Install-StudyMod $f.game $f.package | Out-Null
    foreach($item in $personal){Assert-Test ((Sha $item.Path) -eq $item.Hash) ('Player data changed: '+$item.Path)}
    Assert-Test ((Sha (Join-Path $f.game 'mahoyo-learning\settings.ini')) -eq $settings) 'Settings overwritten.'
    Assert-Test ((Sha (Join-Path $f.game 'd3d9.dll')) -eq (Sha (Join-Path $f.package 'd3d9.dll'))) 'Upgrade DLL mismatch.'
}
Run-Test 'unknown DLL without ownership is rejected unchanged' {
    $f=New-Fixture 'unknown' -Unknown;$before=Snapshot $f.game
    Expect-Failure { Install-StudyMod $f.game $f.package } 'Another d3d9.dll' | Out-Null
    Assert-SnapshotEqual $before $f.game
}
Run-Test 'changed DLL with legacy ownership is rejected unchanged' {
    $f=New-Fixture 'changed' -Owned
    [IO.File]::WriteAllText((Join-Path $f.game 'd3d9.dll'),'changed-by-unrelated-program')
    $before=Snapshot $f.game
    Expect-Failure { Install-StudyMod $f.game $f.package } 'changed by another program' | Out-Null
    Assert-SnapshotEqual $before $f.game
}
foreach ($relative in @('d3d9.dll','mahoyo-learning\local\story.dat','mahoyo-learning\local\provenance.json','mahoyo-learning\local\StudyBook.exe','mahoyo-learning\local\word-index.dat','mahoyo-learning\local\NotoSerifSC-Medium.ttf','mahoyo-learning\local\NotoSerifSC-OFL.txt','mahoyo-learning\local\shogakukan.zip')) {
    Run-Test ('bad package hash rejected: '+$relative) {
        $f=New-Fixture ('bad-hash-'+[Guid]::NewGuid().ToString('N'));$before=Snapshot $f.game
        [IO.File]::AppendAllText((Join-Path $f.package $relative),'corruption')
        Expect-Failure { Install-StudyMod $f.game $f.package } 'Package integrity check failed' | Out-Null
        Assert-SnapshotEqual $before $f.game
    }
}
Run-Test 'provenance-data mismatch rejected even with valid package hashes' {
    $f=New-Fixture 'mismatched-provenance';$before=Snapshot $f.game
    $path=Join-Path $f.package 'mahoyo-learning\local\provenance.json'
    $p=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json;$p.sha256='0'*64
    $p | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
    New-IntegrityManifest $f.package
    Expect-Failure { Install-StudyMod $f.game $f.package } 'provenance do not describe' | Out-Null
    Assert-SnapshotEqual $before $f.game
}
Run-Test 'wrong executable rejected before modification' {
    $f=New-Fixture 'wrong-exe';[IO.File]::WriteAllText((Join-Path $f.game 'WoH.exe'),'wrong-build');$before=Snapshot $f.game
    Expect-Failure { Install-StudyMod $f.game $f.package } 'different game executable' | Out-Null
    Assert-SnapshotEqual $before $f.game
}
Run-Test 'running game guard prevents modification' {
    $f=New-Fixture 'running-guard';$before=Snapshot $f.game
    Set-Item Function:\Assert-StudyGameClosed { throw 'Close the game before installing the sample.' }
    Expect-Failure { Install-StudyMod $f.game $f.package } 'Close the game' | Out-Null
    Assert-SnapshotEqual $before $f.game
}
Run-Test 'real OS lock on ownership manifest leaves old files unchanged' {
    $f=New-Fixture 'manifest-locked' -Owned;$before=Snapshot $f.game
    $handle=[IO.File]::Open((Join-Path $f.game 'mahoyo-learning\installed.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try { Expect-Failure { Install-StudyMod $f.game $f.package } 'Installation failed and previous files were restored' | Out-Null } finally { $handle.Dispose() }
    Assert-SnapshotEqual $before $f.game
    Install-StudyMod $f.game $f.package | Out-Null
    Assert-Test ((Sha (Join-Path $f.game 'd3d9.dll')) -eq (Sha (Join-Path $f.package 'd3d9.dll'))) 'Retry after releasing the lock failed.'
}
foreach ($upgrade in @($false,$true)) {
    Run-Test ('final manifest write failure rolls back; upgrade='+$upgrade) {
        $f=New-Fixture ('commit-failure-'+$upgrade) -Owned:$upgrade;$before=Snapshot $f.game
        Set-Item Function:\Write-StudyOwnership {
            param([string]$Path,$Record)
            if ($Record.state -eq 'installed') { throw 'Injected final ownership commit failure' }
            & $script:originalOwnership $Path $Record
        }
        Expect-Failure { Install-StudyMod $f.game $f.package } 'Injected final ownership commit failure' | Out-Null
        Assert-SnapshotEqual $before $f.game
    }
}
Run-Test 'rollback failure retains pending ownership and recoverable backup; uninstall works' {
    $f=New-Fixture 'rollback-failure' -Owned
    $oldHash=Sha (Join-Path $f.game 'd3d9.dll')
    Set-Item Function:\Write-StudyOwnership {
        param([string]$Path,$Record)
        if ($Record.state -eq 'installed') { throw 'Injected final ownership commit failure' }
        & $script:originalOwnership $Path $Record
    }
    Set-Item Function:\Set-StudyAtomicFile {
        param([string]$Source,[string]$Destination)
        if ([IO.Path]::GetFileName($Source) -like 'old-*' -and [IO.Path]::GetFileName($Destination) -eq 'd3d9.dll') { throw 'Injected locked DLL during rollback' }
        & $script:originalAtomic $Source $Destination
    }
    Expect-Failure { Install-StudyMod $f.game $f.package } 'Recovery backups were retained' | Out-Null
    $owner=Get-Content -LiteralPath (Join-Path $f.game 'mahoyo-learning\installed.json') -Raw | ConvertFrom-Json
    Assert-Test ($owner.state -eq 'installing' -and $owner.previous_dll_sha256 -eq $oldHash) 'Pending old/new ownership lost.'
    Assert-Test ((@(Get-StudyOwnerHashes $owner $script:expectedGame)) -contains (Sha (Join-Path $f.game 'd3d9.dll'))) 'Active DLL became unowned.'
    Assert-Test (@(Get-ChildItem -LiteralPath $f.game -Directory -Filter '.mahoyo-learning-install-*').Count -eq 1) 'Recovery backups were removed.'
    Run-Uninstall $f.game
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll'))) 'Pending-install DLL could not be disabled.'
}
Run-Test 'pending ownership also permits disabling the old DLL before replacement' {
    $f=New-Fixture 'pending-old' -Owned
    $path=Join-Path $f.game 'mahoyo-learning\installed.json'
    [ordered]@{format='mahoyo-learning-installed-v2';sample='chapter01';state='installing';dll_sha256=(Sha (Join-Path $f.package 'd3d9.dll'));previous_dll_sha256=(Sha (Join-Path $f.game 'd3d9.dll'));game_sha256=$script:expectedGame} | ConvertTo-Json | Set-Content -LiteralPath $path -Encoding UTF8
    Run-Uninstall $f.game
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll'))) 'Old pending DLL could not be disabled.'
}
Run-Test 'build fingerprint: metadata-only variants, upgrade and rejection of image changes' {
    $f=New-Fixture 'game-image'
    $exe=Join-Path $f.game 'WoH.exe'
    $original=[IO.File]::ReadAllBytes($exe)
    $fixture=[byte[]]$original.Clone()
    function Check-BuildFixture([byte[]]$Bytes,[bool]$Accept) {
        [IO.File]::WriteAllBytes($exe,$Bytes)
        if ($Accept) { Assert-Test ((Assert-StudyGameVersion $f.game) -eq $script:expectedGame) 'Incorrect build fingerprint.' }
        else { Expect-Failure { Assert-StudyGameVersion $f.game } 'different game executable' | Out-Null }
        $probe=Join-Path $Workspace 'build/fp_regression.exe'
        if (Test-Path -LiteralPath $probe) {
            Push-Location -LiteralPath $f.game
            try { & $probe --check-game-build; Assert-Test (($LASTEXITCODE -eq 0) -eq $Accept) 'Native verifier disagrees.' }
            finally { Pop-Location }
        }
    }
    Check-BuildFixture $original $true
    $fixture[392]=0x42
    Check-BuildFixture $fixture $true
    Install-StudyMod $f.game $f.package | Out-Null
    # A synthetic certificate table checks file layout only. No game process is started.
    $size=$original.Length
    $extended=New-Object byte[] ($size+16)
    $fixture.CopyTo($extended,0)
    $fixture=$extended
    [BitConverter]::GetBytes([uint32]$size).CopyTo($fixture,472)
    [BitConverter]::GetBytes([uint32]16).CopyTo($fixture,476)
    [BitConverter]::GetBytes([uint32]16).CopyTo($fixture,$size)
    [BitConverter]::GetBytes([uint32]0x00020200).CopyTo($fixture,($size+4))
    Check-BuildFixture $fixture $true
    Install-StudyMod $f.game $f.package | Out-Null
    Run-Uninstall $f.game
    $fixture[1024]=$fixture[1024] -bxor 1
    Check-BuildFixture $fixture $false
    $fixture[1024]=$original[1024]
    $fixture[$size]=17
    Check-BuildFixture $fixture $false
    $fixture[$size]=16
    $fixture[472]=$fixture[472] -bxor 8
    Check-BuildFixture $fixture $false
    $fixture=New-Object byte[] ($size+1)
    $original.CopyTo($fixture,0)
    Check-BuildFixture $fixture $false
}
Run-Test 'standalone executable path: read-only version check and both drag-and-drop wrappers' {
    $f=New-Fixture 'outside Steam [standalone]'
    $exe=Join-Path $f.game 'WoH.exe'
    $install=Join-Path $script:sourcePackage 'Install.ps1'
    $before=Snapshot $f.game
    $output=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $install -GamePath $exe -CheckVersion
    Assert-Test ($LASTEXITCODE -eq 0 -and "$output" -match $script:expectedGame) 'Recognized EXE check failed.'
    Assert-SnapshotEqual $before $f.game
    $run=Run-CmdWrapper (Join-Path $script:sourcePackage ([string][char]0x5b89+[char]0x88c5+'.cmd')) $exe -Dropped
    Assert-Test ($run.code -eq 0 -and (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll'))) ('Dropped install failed: '+$run.output)
    $run=Run-CmdWrapper (Join-Path $script:sourcePackage ([string][char]0x5378+[char]0x8f7d+'.cmd')) $exe -Dropped
    Assert-Test ($run.code -eq 0 -and -not (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll'))) ('Dropped uninstall failed: '+$run.output)
    [IO.File]::WriteAllText($exe,'different fixture build')
    $before=Snapshot $f.game
    $output=& cmd.exe /d /c "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$install`" -GamePath `"$exe`" -CheckVersion 2>&1"
    Assert-Test ($LASTEXITCODE -ne 0 -and (($output -join '') -replace '\s','') -match (Sha $exe)) 'Unrecognized EXE check should report its fingerprint.'
    Assert-SnapshotEqual $before $f.game
}
Run-Test 'actual PowerShell entry and both CMD wrappers preserve exit codes' {
    $f=New-Fixture 'cmd-wrappers'
    foreach ($name in @('Install.ps1','Uninstall.ps1',([string][char]0x5b89+[char]0x88c5+'.cmd'),([string][char]0x5378+[char]0x8f7d+'.cmd'))) {
        Copy-Item -LiteralPath (Join-Path $script:sourcePackage $name) -Destination (Join-Path $f.package $name)
    }
    $install=Join-Path $f.package ([string][char]0x5b89+[char]0x88c5+'.cmd')
    $uninstall=Join-Path $f.package ([string][char]0x5378+[char]0x8f7d+'.cmd')
    $run=Run-CmdWrapper $install $f.game
    Assert-Test ($run.code -eq 0) ('Install CMD failed: '+$run.output)
    Assert-Test (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll')) 'Install CMD did not install.'
    $run=Run-CmdWrapper $uninstall $f.game
    Assert-Test ($run.code -eq 0) ('Uninstall CMD failed: '+$run.output)
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $f.game 'd3d9.dll'))) 'Uninstall CMD did not disable.'
    [IO.File]::AppendAllText((Join-Path $f.package 'd3d9.dll'),'corruption')
    $before=Snapshot $f.game
    $run=Run-CmdWrapper $install $f.game
    Assert-Test ($run.code -ne 0) 'Install CMD swallowed failure exit code.'
    Assert-SnapshotEqual $before $f.game
}

$failed=@($script:results | Where-Object { -not $_.passed })
$report=[ordered]@{powershell=$PSVersionTable.PSVersion.ToString();test_root=$script:testRoot;production_exe_never_executed=$true;installed_game_not_modified=$true;tests=$script:results.ToArray();passed=$script:results.Count-$failed.Count;failed=$failed.Count;installer_sha256=(Sha (Join-Path $script:sourcePackage 'Install.ps1'));uninstaller_sha256=(Sha (Join-Path $script:sourcePackage 'Uninstall.ps1'))}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'test-results.json') -Encoding UTF8
Write-Output ('RESULT '+$report.passed+' passed, '+$report.failed+' failed; report: '+(Join-Path $PSScriptRoot 'test-results.json'))
if ($failed.Count) { exit 1 }
