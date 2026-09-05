param([string]$Config = "Release", [string]$Target = "ALL")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "build"

Write-Host "== Configuring (Visual Studio 18 2026, x64) ==" -ForegroundColor Cyan
cmake -S $root -B $build -G "Visual Studio 18 2026" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== Building ($Config) ==" -ForegroundColor Cyan
cmake --build $build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== Running DSP tests ==" -ForegroundColor Cyan
$exe = Join-Path $build "tests\$Config\KleinMeshTests.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $build "$Config\KleinMeshTests.exe" }
if (Test-Path $exe) { & $exe; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
else { Write-Warning "test executable not found at $exe" }

Write-Host "== Artifacts ==" -ForegroundColor Green
Get-ChildItem $build -Recurse -Include *.vst3, *.exe, *.pdb |
    Where-Object { $_.FullName -match "\\$Config\\" -and $_.FullName -notmatch "KleinMeshTests|juceaide" } |
    ForEach-Object { Write-Host $_.FullName }