@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat" amd64
cd /d "%~dp0"
rc.exe /nologo /fo app.res app.rc
cl /nologo /EHsc /c BomModern.cpp /FoBomModern.obj
cl /nologo /EHsc /c pipeline.cpp /Fopipeline.obj
cl /nologo /EHsc /c pipeline_test.cpp /Fopipeline_test.obj
link /nologo BomModern.obj pipeline.obj app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:WINDOWS /OUT:"..\..\实体BOM生成.exe"
link /nologo pipeline_test.obj pipeline.obj app.res user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib /SUBSYSTEM:CONSOLE /OUT:"..\..\pipeline_test.exe"
echo === Build Succeeded ===
