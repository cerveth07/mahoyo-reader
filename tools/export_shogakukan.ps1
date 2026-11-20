param([Parameter(Mandatory=$true)][string]$Catalog,
      [Parameter(Mandatory=$true)][string]$EBWin,
      [Parameter(Mandatory=$true)][string]$Output,
      [Parameter(Mandatory=$true)][string]$Positions,
      [Parameter(Mandatory=$true)][string]$Cache)
$ErrorActionPreference='Stop'
# This is a build tool. The installed MOD reads its own index without EBWin4.
$readerHome=Split-Path -Parent (Get-Item -LiteralPath $EBWin).FullName
[void][Reflection.Assembly]::LoadFrom((Join-Path $readerHome 'EBLibCLR.dll'))
[void][IO.Directory]::CreateDirectory((Join-Path $Cache 'files/000'))
foreach($name in @('profile.ini','phonetic.ini','alternate.ini')) {
    Copy-Item -LiteralPath (Join-Path $readerHome "res/$name") -Destination $Cache -Force
}
$book=New-Object EBLibCLR.Dictionary
$book.InitializeDictionary()
try {
    [void]$book.SetDataPath($Cache)
    [void]$book.AddCatalog((Get-Item -LiteralPath $Catalog).FullName)
    if($book.GetDictionaryCount() -ne 1){throw 'Expected one integrated dictionary.'}
    [void]$book.SelectDictionary(0)
    [void]$book.SetUseUnicode(1)
    $file=[IO.File]::Create($Output)
    try {
        $gzip=[IO.Compression.GZipStream]::new($file,[IO.Compression.CompressionLevel]::Fastest)
        $writer=[IO.StreamWriter]::new($gzip,[Text.UTF8Encoding]::new($false))
        try {
            $count=0
            foreach($address in [IO.File]::ReadLines($Positions)){
                $pair=$address.Split([char]9)
                $page=[uint32]$pair[0];$offset=[int]$pair[1]
                $html=$book.GetTextByAddr(0,$page,$offset)
                if(!$html -or $book.GetPage() -ne $page -or $book.GetOffs() -ne $offset){throw "Unreadable entry at $address"}
                $writer.WriteLine(('{0}{3}{1}{3}{2}' -f $page,$offset,([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($html))),[char]9))
                $count++
            }
        } finally {$writer.Dispose()}
    } finally {$file.Dispose()}
    Write-Output "Read $count distinct indexed text addresses."
} finally {$book.FinalizeDictionary();$book.Dispose()}
