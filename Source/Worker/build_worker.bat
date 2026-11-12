@echo off
setlocal enabledelayedexpansion

set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"
for %%I in ("%SRC_DIR%\..\..") do set "REPO_ROOT=%%~fI"

if not defined CATIA_DIR set "CATIA_DIR=C:\Program Files\Dassault Systemes\B26"
if not defined TCK_PROFILE set "TCK_PROFILE=V5_6R2016_B26"
set "WS_DIR=%REPO_ROOT%\CAA_Build_Workspace"
set "TEMPLATE_DIR=%SRC_DIR%\workspace_template"
set "RUNTIME_BIN=%REPO_ROOT%\Runtime\win_b64\code\bin"

echo ===================================================
echo [1/4] Preparing CAA Build Workspace
echo ===================================================

if not exist "%WS_DIR%\CAAStandalonePart.edu\IdentityCard" mkdir "%WS_DIR%\CAAStandalonePart.edu\IdentityCard"
if not exist "%WS_DIR%\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src" mkdir "%WS_DIR%\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src"
if not exist "%RUNTIME_BIN%" mkdir "%RUNTIME_BIN%"

copy /y "%TEMPLATE_DIR%\CATIAV5Level.lvl" "%WS_DIR%\" >nul
copy /y "%TEMPLATE_DIR%\Install_config_win_b64" "%WS_DIR%\" >nul
copy /y "%SRC_DIR%\IdentityCard.xml" "%WS_DIR%\CAAStandalonePart.edu\IdentityCard\" >nul
copy /y "%SRC_DIR%\Imakefile.mk" "%WS_DIR%\CAAStandalonePart.edu\CAAStandalonePartWorker.m\" >nul
copy /y "%SRC_DIR%\Imakefile.mk" "%WS_DIR%\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src\" >nul
copy /y "%SRC_DIR%\CAAStandalonePartWorker.cpp" "%WS_DIR%\CAAStandalonePart.edu\CAAStandalonePartWorker.m\src\" >nul

if exist "%WS_DIR%\win_b64\code\bin\CAAStandalonePartWorker.exe" (
    del /f /q "%WS_DIR%\win_b64\code\bin\CAAStandalonePartWorker.exe"
)

echo ===================================================
echo [2/4] Setting Up Build Tools and Building with mkmk
echo ===================================================

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\vcvarsall.bat"
if not exist "%VCVARS%" if defined VS110COMNTOOLS set "VCVARS=%VS110COMNTOOLS%..\..\VC\vcvarsall.bat"
call "%VCVARS%" amd64
if errorlevel 1 (
    echo [ERROR] Failed to run vcvarsall.bat
    exit /b 1
)

call "%CATIA_DIR%\win_b64\code\command\tck_init.bat"
call tck_profile %TCK_PROFILE%

cd /d "%WS_DIR%"
call mkGetPreq -p "%CATIA_DIR%"
call mkmk -a -u -w -g

echo ===================================================
echo [3/4] Verifying Build Artifact
echo ===================================================

if not exist "%WS_DIR%\win_b64\code\bin\CAAStandalonePartWorker.exe" (
    echo [ERROR] Build failed: CAAStandalonePartWorker.exe not found!
    exit /b 1
)

echo ===================================================
echo [4/4] Deploying to Runtime
echo ===================================================

copy /y "%WS_DIR%\win_b64\code\bin\CAAStandalonePartWorker.exe" "%RUNTIME_BIN%\" >nul
if exist "%CATIA_DIR%\win_b64\code\bin\netapi32.dll" (
    copy /y "%CATIA_DIR%\win_b64\code\bin\netapi32.dll" "%RUNTIME_BIN%\" >nul
)

echo [SUCCESS] CAA Worker deployed to %RUNTIME_BIN%.
exit /b 0
