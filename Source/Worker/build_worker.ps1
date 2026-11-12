# build_worker.ps1 - Portable build & deployment script for CAA Standalone Worker
param(
    [string]$CatiaDir = "C:\Program Files\Dassault Systemes\B26",
    [string]$TckProfile = "V5_6R2016_B26",
    [string]$WorkspaceDir = ""
)

$ErrorActionPreference = "Stop"
if (-not $env:TEMP) { $env:TEMP = [System.IO.Path]::GetTempPath().TrimEnd('\') }
if (-not $env:TMP) { $env:TMP = $env:TEMP }

$srcDir = $PSScriptRoot
$repoRoot = (Resolve-Path "$srcDir\..\..").Path
if ([string]::IsNullOrWhiteSpace($WorkspaceDir)) {
    $WorkspaceDir = Join-Path $repoRoot "CAA_Build_Workspace"
}
$templateDir = Join-Path $srcDir "workspace_template"
$runtimeBin = Join-Path $repoRoot "Runtime\win_b64\code\bin"

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
if (-not (Test-Path $vcvars) -and $env:VS110COMNTOOLS) {
    $candidate = Join-Path $env:VS110COMNTOOLS "..\..\VC\vcvarsall.bat"
    if (Test-Path $candidate) { $vcvars = (Resolve-Path $candidate).Path }
}
if (-not (Test-Path $vcvars)) {
    throw "Visual Studio 2012 vcvarsall.bat not found."
}

$tckInit = Join-Path $CatiaDir "win_b64\code\command\tck_init.bat"
if (-not (Test-Path $tckInit)) {
    throw "CATIA TCK script not found at: $tckInit"
}

Write-Host "=== [1/4] Preparing CAA Build Workspace at $WorkspaceDir ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Path "$WorkspaceDir\CAAStandalonePart.edu\IdentityCard" -Force | Out-Null
New-Item -ItemType Directory -Path "$WorkspaceDir\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src" -Force | Out-Null
New-Item -ItemType Directory -Path $runtimeBin -Force | Out-Null

Copy-Item -Path "$templateDir\CATIAV5Level.lvl" -Destination "$WorkspaceDir\" -Force
Copy-Item -Path "$templateDir\Install_config_win_b64" -Destination "$WorkspaceDir\" -Force
Copy-Item -Path "$srcDir\IdentityCard.xml" -Destination "$WorkspaceDir\CAAStandalonePart.edu\IdentityCard\" -Force
Copy-Item -Path "$srcDir\Imakefile.mk" -Destination "$WorkspaceDir\CAAStandalonePart.edu\CAAStandalonePartWorker.m\" -Force
Copy-Item -Path "$srcDir\Imakefile.mk" -Destination "$WorkspaceDir\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src\" -Force
Copy-Item -Path "$srcDir\CAAStandalonePartWorker.cpp" -Destination "$WorkspaceDir\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src\" -Force

$builtExe = "$WorkspaceDir\win_b64\code\bin\CAAStandalonePartWorker.exe"
if (Test-Path $builtExe) { Remove-Item $builtExe -Force }

Write-Host "=== [2/4] Running mkmk build ===" -ForegroundColor Cyan
$cmdBatch = @"
@echo off
call "$vcvars" amd64
call "$tckInit"
call tck_profile $TckProfile
cd /d "$WorkspaceDir"
call mkGetPreq -p "$CatiaDir"
call mkmk -a -u -w -g
"@

$tmpBat = Join-Path $env:TEMP ("build_mkmk_" + [guid]::NewGuid().ToString("N") + ".bat")
Set-Content -Path $tmpBat -Value $cmdBatch -Encoding ASCII

$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$buildOutput = & cmd.exe /c $tmpBat 2>&1
$ErrorActionPreference = $prevEAP
Remove-Item $tmpBat -Force -ErrorAction SilentlyContinue

$buildOutput | ForEach-Object { Write-Host $_ }

Write-Host "=== [3/4] Verifying Build Artifact ===" -ForegroundColor Cyan
$hasMakeError = $buildOutput | Select-String "# make-ERROR:|# mkmk-FATAL:"
if ($hasMakeError) {
    throw "Build contains errors: $($hasMakeError -join '; ')"
}
if (-not (Test-Path $builtExe)) {
    throw "Target executable not generated at $builtExe"
}

Write-Host "=== [4/4] Deploying to Runtime Directory ===" -ForegroundColor Cyan
Copy-Item -Path $builtExe -Destination "$runtimeBin\CAAStandalonePartWorker.exe" -Force
$netapi = Join-Path $CatiaDir "win_b64\code\bin\netapi32.dll"
if (Test-Path $netapi) {
    Copy-Item -Path $netapi -Destination "$runtimeBin\netapi32.dll" -Force
}

Write-Host "Deployment complete: $runtimeBin\CAAStandalonePartWorker.exe" -ForegroundColor Green
