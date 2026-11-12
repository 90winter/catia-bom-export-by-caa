Set-Location $PSScriptRoot

if (-not $env:TEMP) { $env:TEMP = [System.IO.Path]::GetTempPath().TrimEnd('\') }
if (-not $env:TMP) { $env:TMP = $env:TEMP }

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
if (-not (Test-Path $vcvars)) {
    if ($env:VS110COMNTOOLS -and (Test-Path "$env:VS110COMNTOOLS\..\..\VC\vcvarsall.bat")) {
        $vcvars = (Resolve-Path "$env:VS110COMNTOOLS\..\..\VC\vcvarsall.bat").Path
    }
}

if (-not (Test-Path $vcvars)) {
    Write-Host "Error: Visual Studio 2012 vcvarsall.bat not found!" -ForegroundColor Red
    exit 1
}

Write-Host "=== 1. Setup VS 2012 64-bit environment & Compile ==="
$cmd = "call `"$vcvars`" amd64 && " + `
       'rc.exe /nologo /fo app.res app.rc && ' + `
       'cl /nologo /EHsc /c BomModern.cpp /FoBomModern.obj && ' + `
       'cl /nologo /EHsc /c pipeline.cpp /Fopipeline.obj && ' + `
       'cl /nologo /EHsc /c xlsx_builder.cpp /Foxlsx_builder.obj && ' + `
       'cl /nologo /EHsc /c pipeline_test.cpp /Fopipeline_test.obj && ' + `
       'link /nologo BomModern.obj pipeline.obj xlsx_builder.obj app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:WINDOWS /OUT:"..\..\实体BOM生成.exe" && ' + `
       'link /nologo pipeline_test.obj pipeline.obj xlsx_builder.obj app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:CONSOLE /OUT:"..\..\pipeline_test.exe"'

cmd /c $cmd
if ($LASTEXITCODE -eq 0) {
    Write-Host "=== Build Succeeded! ===" -ForegroundColor Green
} else {
    Write-Host "=== Build Failed with code $LASTEXITCODE ===" -ForegroundColor Red
    exit $LASTEXITCODE
}
