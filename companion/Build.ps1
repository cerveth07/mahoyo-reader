param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$references = @('/nologo','/codepage:65001','/optimize+','/platform:anycpu','/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll',
    '/reference:System.Xml.Linq.dll','/reference:System.Web.Extensions.dll','/reference:System.IO.Compression.dll','/reference:System.IO.Compression.FileSystem.dll')
$references += '/resource:'+(Join-Path $PSScriptRoot 'game-type.recipe.gz')+',Mahoyo.FontRecipe'
$sources = @('Corpus.cs','Notebook.cs','StudyHistory.cs','Dictionaries.cs','GameType.cs','TypeContours.cs','TypeTables.cs','ReaderStyle.cs','Windows.cs','SentenceNoteEditor.cs','BookMailbox.cs','Program.cs') | ForEach-Object { Join-Path $PSScriptRoot $_ }
$dest = Join-Path $project 'package/mahoyo-learning/local/StudyBook.exe'
& $compiler @references /target:winexe "/out:$dest" @sources
if ($LASTEXITCODE) { throw 'StudyBook compilation failed.' }
if (!$SkipTests) {
    $testExe = Join-Path $project 'build/StudyBookTests.exe'
    & $compiler @references /target:exe /main:Mahoyo.Book.BookTests "/out:$testExe" @sources (Join-Path $PSScriptRoot 'BookTests.cs')
    if ($LASTEXITCODE) { throw 'StudyBook test compilation failed.' }
    & $testExe (Split-Path $dest) (Join-Path $project 'dist/mahoyo-ja-zh-optional.zip')
    if ($LASTEXITCODE) { throw 'StudyBook regression failed.' }
}
