@echo off
setlocal
cd /d "%~dp0"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
if not exist "%VCVARS%" if defined VS110COMNTOOLS set "VCVARS=%VS110COMNTOOLS%..\..\VC\vcvarsall.bat"
call "%VCVARS%" amd64
if errorlevel 1 goto error

rc.exe /nologo /fo app.res app.rc
if errorlevel 1 goto error

cl /nologo /EHsc /c BomModern.cpp /FoBomModern.obj
if errorlevel 1 goto error

cl /nologo /EHsc /c pipeline.cpp /Fopipeline.obj
if errorlevel 1 goto error

cl /nologo /EHsc /c xlsx_builder.cpp /Foxlsx_builder.obj
if errorlevel 1 goto error

cl /nologo /EHsc /c pipeline_test.cpp /Fopipeline_test.obj
if errorlevel 1 goto error

link /nologo BomModern.obj pipeline.obj xlsx_builder.obj app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:WINDOWS /OUT:"..\..\BomModern.tmp.exe"
if errorlevel 1 goto error

powershell -NoProfile -Command "Move-Item -Force '..\..\BomModern.tmp.exe' ('..\..\' + [char]0x5B9E + [char]0x4F53 + 'BOM' + [char]0x751F + [char]0x6210 + '.exe')"
if errorlevel 1 goto error

link /nologo pipeline_test.obj pipeline.obj xlsx_builder.obj app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:CONSOLE /OUT:"..\..\pipeline_test.exe"
if errorlevel 1 goto error

echo === Build Succeeded ===
exit /b 0

:error
echo === Build Failed ===
exit /b 1
