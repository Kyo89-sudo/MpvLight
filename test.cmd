@echo off
rem Engine integration tests: test.cmd C:\path\tracks-subs.mkv  (see TESTING.md)
setlocal EnableExtensions
cd /d "%~dp0"

rem --- Visual Studio: use the developer environment if already active, otherwise find it with vswhere.
where cl >nul 2>nul
if errorlevel 1 (
  set "VSDIR="
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
  )
  if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
)
where cl >nul 2>nul
if errorlevel 1 (
  if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERRORE: Visual Studio con gli strumenti C++ non trovato.
    exit /b 1
  )
  call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
  if errorlevel 1 exit /b 1
)

if not exist build mkdir build
set "DLL=%~dp0portable\libmpv-2.dll"
if not exist "%DLL%" set "DLL=%LOCALAPPDATA%\Programs\MpvLight\libmpv-2.dll"
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /utf-8 tests\integration.cpp /Fe:build\integration.exe /Fo:build\integration.obj /link user32.lib
if errorlevel 1 exit /b 1
build\integration.exe "%DLL%" "%~1"
