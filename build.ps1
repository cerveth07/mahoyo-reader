param([Parameter(Mandatory=$true)][string]$ZigPath,[string]$Python="python",[switch]$LayoutTools,[switch]$PreparedData)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$compiler=(Resolve-Path -LiteralPath $ZigPath).Path
$mhRoot=Join-Path $taskRoot 'vendor\minhook'
$buildRoot=Join-Path $taskRoot 'build'
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $buildRoot 'zig-global'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $buildRoot 'zig-local'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$local = Join-Path $taskRoot 'package/mahoyo-learning/local'
New-Item -ItemType Directory -Path $local -Force | Out-Null
$planArgs=@()
if($PreparedData) {
    $planArgs+='--released-data'
} else {
    Copy-Item -LiteralPath (Join-Path $taskRoot 'assets/dictionaries/shogakukan.zip') -Destination $local -Force
    Copy-Item -LiteralPath (Join-Path $taskRoot 'assets/fonts/NotoSerifSC-Medium.ttf') -Destination $local -Force
    Copy-Item -LiteralPath (Join-Path $taskRoot 'licenses/NotoSerifSC-OFL.txt') -Destination $local -Force
}
& $Python (Join-Path $taskRoot 'tools/compile_plans.py') @planArgs
if($LASTEXITCODE){throw 'Reviewed plan generation failed'}
$objects=@()
foreach($source in @('buffer.c','hook.c','trampoline.c','hde\hde64.c')) {
    $object=Join-Path $buildRoot ([IO.Path]::GetFileNameWithoutExtension($source)+'.o')
    & $compiler cc -target x86_64-windows-gnu -O2 -c (Join-Path $mhRoot "src\$source") -o $object
    if($LASTEXITCODE) {throw "Compilation failed: $source"}
    $objects+=$object
}
& $compiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -s -shared -static -I (Join-Path $mhRoot 'include') (Join-Path $taskRoot 'native\mod.cpp') (Join-Path $taskRoot 'native\learning_render.cpp') @objects -luser32 -lgdi32 -o (Join-Path $buildRoot 'd3d9.dll')
if($LASTEXITCODE) {throw 'Mod compilation failed'}
& $compiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -static (Join-Path $taskRoot 'native\render_test.cpp') (Join-Path $taskRoot 'native\learning_render.cpp') -luser32 -lgdi32 -o (Join-Path $buildRoot 'render_test.exe')
if($LASTEXITCODE) {throw 'Renderer test compilation failed'}
& $compiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -static -I (Join-Path $mhRoot 'include') (Join-Path $taskRoot 'native\fp_regression.cpp') (Join-Path $taskRoot 'native\learning_render.cpp') @objects -luser32 -lgdi32 -o (Join-Path $buildRoot 'fp_regression.exe')
if($LASTEXITCODE) {throw 'Floating-point regression compilation failed'}
if($LayoutTools) {
    & $compiler c++ -DMAHOYO_QA_CORPUS_REPLAY -target x86_64-windows-gnu -std=c++17 -O2 -shared -static -I (Join-Path $mhRoot 'include') (Join-Path $taskRoot 'native\mod.cpp') (Join-Path $taskRoot 'native\learning_render.cpp') @objects -luser32 -lgdi32 -o (Join-Path $buildRoot 'layout_probe.dll')
    if($LASTEXITCODE){throw 'Corpus replay compilation failed'}
    & $compiler c++ -target x86_64-windows-gnu -std=c++17 -O2 -static (Join-Path $taskRoot 'native/layout_audit.cpp') (Join-Path $taskRoot 'native/learning_render.cpp') -luser32 -lgdi32 -o (Join-Path $buildRoot 'layout_audit.exe')
    if($LASTEXITCODE){throw 'Layout audit compilation failed'}
}
Write-Output 'Built build/d3d9.dll, build/render_test.exe, and build/fp_regression.exe. Tested compiler: Zig 0.13.0.'
