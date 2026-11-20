param([string]$GamePath,[string]$DictionaryPack,[switch]$CheckVersion)
# External launchers may pass a different PowerShell edition's module search path.
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

function Get-StudySha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-StudyGameClosed {
    if (Get-Process -Name 'WoH' -ErrorAction SilentlyContinue) {
        throw 'Close the game before installing the sample.'
    }
}

function Get-StudyOwnerHashes($Owner, [string]$ExpectedGameHash) {
    if ($Owner.sample -notin @('chapter01','full-game') -or $Owner.game_sha256 -ne $ExpectedGameHash -or
        [string]$Owner.dll_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'The installed ownership record is invalid; no DLL was overwritten.'
    }
    if ($Owner.format -and $Owner.format -ne 'mahoyo-learning-installed-v2') {
        throw 'Unsupported installed ownership record; no DLL was overwritten.'
    }
    $state = [string]$Owner.state
    if ($state -and $state -notin @('installed','installing')) {
        throw 'Unsupported installation state; no DLL was overwritten.'
    }
    $hashes = @(([string]$Owner.dll_sha256).ToLowerInvariant())
    if ($state -eq 'installing' -and $Owner.previous_dll_sha256) {
        if ([string]$Owner.previous_dll_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw 'The pending ownership record is invalid; no DLL was overwritten.'
        }
        $hashes += ([string]$Owner.previous_dll_sha256).ToLowerInvariant()
    }
    return $hashes
}

function Read-StudyPackage([string]$PackageRoot) {
    $manifestPath = Join-Path $PackageRoot 'package-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'package-manifest.json is missing. Use a complete sample package.'
    }
    $document = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $required = @('d3d9.dll','mahoyo-learning/local/story.dat','mahoyo-learning/local/provenance.json','mahoyo-learning/local/StudyBook.exe','mahoyo-learning/local/word-index.dat','mahoyo-learning/local/NotoSerifSC-Medium.ttf','mahoyo-learning/local/NotoSerifSC-OFL.txt','mahoyo-learning/local/shogakukan.zip')
    if ($document.format -ne 'mahoyo-learning-package-v1' -or @($document.files).Count -ne $required.Count) {
        throw 'The package integrity manifest has an unsupported format or file list.'
    }
    $files = @{}
    foreach ($entry in $document.files) {
        $relative = [string]$entry.path
        $bytes = [long]0
        if ($required -cnotcontains $relative -or $files.ContainsKey($relative) -or
            [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            -not [long]::TryParse([string]$entry.bytes, [ref]$bytes) -or $bytes -le 0) {
            throw 'The package integrity manifest contains an invalid file entry.'
        }
        $source = Join-Path $PackageRoot $relative.Replace('/','\')
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw ('Required package file is missing: ' + $relative)
        }
        $hash = ([string]$entry.sha256).ToLowerInvariant()
        if ((Get-Item -LiteralPath $source).Length -ne $bytes -or (Get-StudySha256 $source) -ne $hash) {
            throw ('Package integrity check failed: ' + $relative + '. No game files were changed.')
        }
        $files[$relative] = @{source=$source; sha256=$hash; bytes=$bytes}
    }
    $provenance = Get-Content -LiteralPath $files['mahoyo-learning/local/provenance.json'].source -Raw | ConvertFrom-Json
    $data = $files['mahoyo-learning/local/story.dat']
    if ($provenance.sha256 -ne $data.sha256 -or $provenance.bytes -ne $data.bytes -or $provenance.records -ne 24134 -or $provenance.scope -ne 'full-game') {
        throw 'The chapter data and provenance do not describe the same full-game edition. No game files were changed.'
    }
    return @{files=$files; manifest_sha256=(Get-StudySha256 $manifestPath)}
}

function Set-StudyAtomicFile([string]$Source, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        # Windows PowerShell 5.1 otherwise coerces $null to an empty path.
        [IO.File]::Replace($Source, $Destination, [NullString]::Value)
    } else {
        [IO.File]::Move($Source, $Destination)
    }
}

function Write-StudyOwnership([string]$Path, $Record) {
    $temporary = $Path + '.new.' + [Guid]::NewGuid().ToString('N')
    try {
        $json = $Record | ConvertTo-Json -Depth 8
        [IO.File]::WriteAllText($temporary, $json, (New-Object Text.UTF8Encoding($false)))
        Set-StudyAtomicFile $temporary $Path
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Remove-StudyTransaction([string]$TransactionRoot, [string]$GameRoot) {
    # Only a generated child of this explicit game directory may be removed.
    $resolved = [IO.Path]::GetFullPath($TransactionRoot)
    $prefix = [IO.Path]::GetFullPath($GameRoot).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike '.mahoyo-learning-install-*') {
        throw 'Refusing to clean a transaction outside the selected game directory.'
    }
    if (Test-Path -LiteralPath $resolved -PathType Container) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

function Get-StudyGameImageHash([string]$Exe) {
    # Fixed offsets for Build 13060638 only. Its checksum and certificate entry
    # were zero and its last section ended at EOF. Hash every other original byte.
    $imageBytes=6237464
    $file=[IO.File]::OpenRead($Exe)
    try {
        if ($file.Length -lt $imageBytes -or $file.Length -gt ($imageBytes+32MB)) { return }
        $reader=New-Object IO.BinaryReader($file)
        $bytes=$reader.ReadBytes([int]$file.Length)
        if ($bytes.Length -ne $file.Length) { return }
    } finally { $file.Dispose() }
    $tail=$bytes.Length-$imageBytes
    $offset=if ($tail) { $imageBytes } else { 0 }
    if ([BitConverter]::ToUInt32($bytes,472) -ne $offset -or [BitConverter]::ToUInt32($bytes,476) -ne $tail) { return }
    for ($pos=$imageBytes; $pos -lt $bytes.Length; $pos+=$padded) {
        $remaining=$bytes.Length-$pos
        if ($remaining -lt 8) { return }
        $length=[long][BitConverter]::ToUInt32($bytes,$pos)
        if ($length -le 8 -or $length -gt $remaining -or [BitConverter]::ToUInt32($bytes,$pos+4) -ne 0x00020200) { return }
        $padded=($length+7) -band (-8L)
        if ($padded -gt $remaining) { return }
    }
    [Array]::Clear($bytes,392,4)
    [Array]::Clear($bytes,472,8)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($bytes,0,$imageBytes)).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Assert-StudyGameVersion([string]$GameRoot) {
    $exe=Join-Path $GameRoot 'WoH.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'WoH.exe was not found in the selected game directory.' }
    $imageHash=Get-StudyGameImageHash $exe
    if ($imageHash -ne '7a718cfca3fccafd9d7984e41206473a6152098d6e0f7374c52abf1581b6ad9a') {
        $actual=Get-StudySha256 $exe
        throw "This package targets a different game executable. WoH.exe SHA-256: $actual. Verified build: 13060638."
    }
    return $imageHash
}
function Install-StudyMod([string]$SelectedGamePath, [string]$PackageRoot) {
    $gameRoot = [IO.Path]::GetFullPath($SelectedGamePath)
    if ($gameRoot.Length -gt [IO.Path]::GetPathRoot($gameRoot).Length) { $gameRoot = $gameRoot.TrimEnd('\') }
    $expected = Assert-StudyGameVersion $gameRoot
    Assert-StudyGameClosed
    $package = Read-StudyPackage $PackageRoot
    $destination = Join-Path $gameRoot 'd3d9.dll'
    $dataRoot = Join-Path $gameRoot 'mahoyo-learning'
    $localRoot = Join-Path $dataRoot 'local'
    $manifest = Join-Path $dataRoot 'installed.json'
    $settings = Join-Path $dataRoot 'settings.ini'
    foreach ($directory in @($dataRoot,$localRoot)) {
        if ((Test-Path -LiteralPath $directory) -and -not (Test-Path -LiteralPath $directory -PathType Container)) {
            throw ('The target data directory is occupied by a file: ' + $directory)
        }
    }
    foreach ($file in @($destination,$manifest,$settings)) {
        if ((Test-Path -LiteralPath $file) -and -not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw ('The target file path is occupied by a directory: ' + $file)
        }
    }
    $previousHash = $null
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Another d3d9.dll is already installed. It was not overwritten.' }
        $old = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
        $accepted = @(Get-StudyOwnerHashes $old $expected)
        $previousHash = Get-StudySha256 $destination
        if ($accepted -notcontains $previousHash) { throw 'The installed DLL was changed by another program. It was not overwritten.' }
    }
    $targets = @(
        @{relative='mahoyo-learning/local/story.dat'; destination=(Join-Path $localRoot 'story.dat')},
        @{relative='mahoyo-learning/local/provenance.json'; destination=(Join-Path $localRoot 'provenance.json')},
        @{relative='mahoyo-learning/local/StudyBook.exe'; destination=(Join-Path $localRoot 'StudyBook.exe')},
        @{relative='mahoyo-learning/local/word-index.dat'; destination=(Join-Path $localRoot 'word-index.dat')},
        @{relative='mahoyo-learning/local/shogakukan.zip'; destination=(Join-Path $localRoot 'shogakukan.zip')},
        @{relative='mahoyo-learning/local/NotoSerifSC-Medium.ttf'; destination=(Join-Path $localRoot 'NotoSerifSC-Medium.ttf')},
        @{relative='mahoyo-learning/local/NotoSerifSC-OFL.txt'; destination=(Join-Path $localRoot 'NotoSerifSC-OFL.txt')}
    )
    if (-not (Test-Path -LiteralPath $settings -PathType Leaf)) {
        $settingsSource = Join-Path $PackageRoot 'mahoyo-learning\settings.ini'
        if (-not (Test-Path -LiteralPath $settingsSource -PathType Leaf)) { throw 'The sample settings.ini is missing.' }
        $targets += @{relative='mahoyo-learning/settings.ini'; destination=$settings; source=$settingsSource}
    }
    # Install the proxy last; the write-ahead ownership record already recognizes it.
    $targets += @{relative='d3d9.dll'; destination=$destination}
    foreach ($target in $targets) {
        if ((Test-Path -LiteralPath $target.destination) -and -not (Test-Path -LiteralPath $target.destination -PathType Leaf)) {
            throw ('The target file path is occupied by a directory: ' + $target.destination)
        }
    }

    $transactionRoot = Join-Path $gameRoot ('.mahoyo-learning-install-' + [Guid]::NewGuid().ToString('N'))
    $changed = New-Object 'System.Collections.Generic.List[object]'
    $ownershipWritten = $false
    $complete = $false
    $rollbackComplete = $false
    $manifestExisted = Test-Path -LiteralPath $manifest -PathType Leaf
    try {
        New-Item -ItemType Directory -Path $transactionRoot | Out-Null
        $manifestBackup = Join-Path $transactionRoot 'ownership.backup'
        if ($manifestExisted) { Copy-Item -LiteralPath $manifest -Destination $manifestBackup }
        $number = 0
        foreach ($target in $targets) {
            $target.stage = Join-Path $transactionRoot ('new-' + $number)
            $target.backup = Join-Path $transactionRoot ('old-' + $number)
            $target.existed = Test-Path -LiteralPath $target.destination -PathType Leaf
            if (-not $target.source) { $target.source = $package.files[$target.relative].source }
            Copy-Item -LiteralPath $target.source -Destination $target.stage
            if ($package.files.ContainsKey($target.relative)) {
                $entry = $package.files[$target.relative]
                if ((Get-Item -LiteralPath $target.stage).Length -ne $entry.bytes -or (Get-StudySha256 $target.stage) -ne $entry.sha256) {
                    throw ('Staged package integrity check failed: ' + $target.relative)
                }
            }
            if ($target.existed) { Copy-Item -LiteralPath $target.destination -Destination $target.backup }
            $number++
        }
        New-Item -ItemType Directory -Path $localRoot -Force | Out-Null
        Assert-StudyGameClosed
        $record = [ordered]@{
            format='mahoyo-learning-installed-v2'; sample='full-game'; state='installing'
            dll_sha256=$package.files['d3d9.dll'].sha256; game_sha256=$expected
            package_manifest_sha256=$package.manifest_sha256; installed_at=(Get-Date).ToString('o')
        }
        if ($previousHash) { $record.previous_dll_sha256 = $previousHash }
        # If this write fails, no payload has been replaced. A crash after it still
        # leaves enough ownership information to disable either old or new DLL.
        Write-StudyOwnership $manifest $record
        $ownershipWritten = $true
        foreach ($target in $targets) {
            Set-StudyAtomicFile $target.stage $target.destination
            $changed.Add($target)
        }
        $record.state = 'installed'
        $record.Remove('previous_dll_sha256')
        Write-StudyOwnership $manifest $record
        $complete = $true
    } catch {
        $failure = $_.Exception.Message
        $rollbackErrors = New-Object 'System.Collections.Generic.List[string]'
        for ($i = $changed.Count - 1; $i -ge 0; $i--) {
            $target = $changed[$i]
            try {
                if ($target.existed) { Set-StudyAtomicFile $target.backup $target.destination }
                elseif (Test-Path -LiteralPath $target.destination -PathType Leaf) { Remove-Item -LiteralPath $target.destination -Force }
            } catch { $rollbackErrors.Add($_.Exception.Message) }
        }
        if ($rollbackErrors.Count -eq 0 -and $ownershipWritten) {
            try {
                if ($manifestExisted) { Set-StudyAtomicFile $manifestBackup $manifest }
                elseif (Test-Path -LiteralPath $manifest -PathType Leaf) { Remove-Item -LiteralPath $manifest -Force }
            } catch { $rollbackErrors.Add($_.Exception.Message) }
        }
        $rollbackComplete = $rollbackErrors.Count -eq 0
        if (-not $rollbackComplete) {
            throw ('Installation failed: ' + $failure + '. Recovery backups were retained at ' + $transactionRoot +
                '. The pending ownership record recognizes the old/new study DLL; close the game and rerun installation or uninstall. Rollback errors: ' + ($rollbackErrors -join '; '))
        }
        throw ('Installation failed and previous files were restored: ' + $failure)
    } finally {
        if ($complete -or $rollbackComplete) {
            try { Remove-StudyTransaction $transactionRoot $gameRoot }
            catch { Write-Warning ('Temporary installation backups were retained at ' + $transactionRoot + ': ' + $_.Exception.Message) }
        }
    }
    Write-Output 'Installed. Select Japanese in the game. Full-game text coverage: 24,134 keys.'
    Write-Output 'Existing settings were preserved. The original executable, game archives, and saves were not replaced.'
}

if ($CheckVersion) {
    $hash=Assert-StudyGameVersion $GamePath
    $actual=Get-StudySha256 (Join-Path $GamePath 'WoH.exe')
    Write-Output "WoH.exe matches the verified Windows build 13060638. File SHA-256: $actual"
    if ($actual -ne $hash) { Write-Output "Same game image with different PE metadata. Build fingerprint: $hash" }
    return
}
if ($DictionaryPack) {
    $dictionaryFile=(Get-Item -LiteralPath $DictionaryPack -ErrorAction Stop).FullName
    if (-not (Test-Path -LiteralPath $dictionaryFile -PathType Leaf)) { throw 'Select a dictionary ZIP or TSV file.' }
}
Install-StudyMod -SelectedGamePath $GamePath -PackageRoot $PSScriptRoot
if ($DictionaryPack) {
    $book=Join-Path $GamePath 'mahoyo-learning/local/StudyBook.exe'
    $process=Start-Process -FilePath $book -ArgumentList @('--import',('"'+$dictionaryFile+'"')) -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode) { throw 'The MOD was installed. The optional dictionary import failed. Use the dictionary menu to retry.' }
    Write-Output 'Optional offline dictionary imported.'
}
