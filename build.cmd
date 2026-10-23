@echo off
rem MpvLight build: decoder 7z (LZMA SDK) -> portable\MpvLight.exe -> dist\ (setup, ZIP portatile, SHA256SUMS.txt)
setlocal EnableExtensions
set "VERSION=0.5.1"
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

if not exist portable mkdir portable
if not exist build\lzma mkdir build\lzma
if not exist dist mkdir dist

set "CXXFLAGS=/nologo /std:c++17 /EHsc /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX"
set "LIBS=user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib shlwapi.lib dwmapi.lib uxtheme.lib gdiplus.lib winhttp.lib bcrypt.lib"
set "LZMA=7zAlloc 7zArcIn 7zBuf 7zCrc 7zCrcOpt 7zDec 7zStream Bcj2 Bra Bra86 BraIA64 CpuArch Delta Lzma2Dec LzmaDec Ppmd7 Ppmd7Dec"

rem --- 7z decoder (public domain, third_party\lzma)
set "LZMAOBJ="
for %%f in (%LZMA%) do (
  cl /nologo /c /O2 /MT /W3 /DZ7_PPMD_SUPPORT /DZ7_EXTRACT_ONLY /Fo:build\lzma\%%f.obj third_party\lzma\%%f.c
  if errorlevel 1 (
    echo ERRORE: compilazione di third_party\lzma\%%f.c
    exit /b 1
  )
  call set "LZMAOBJ=%%LZMAOBJ%% build\lzma\%%f.obj"
)

rem --- MpvLight
pushd src
rc /nologo /c65001 /fo ..\build\app.res app.rc
if errorlevel 1 (
  popd
  exit /b 1
)
popd
cl %CXXFLAGS% src\main.cpp %LZMAOBJ% build\app.res /Fe:portable\MpvLight.exe /Fo:build\main.obj /link /SUBSYSTEM:WINDOWS /MANIFEST:NO %LIBS%
if errorlevel 1 exit /b 1
type nul > portable\portable.flag

rem --- Setup (MpvLight.exe, README, CHANGELOG and licence are embedded as resources)
pushd packaging
rc /nologo /c65001 /fo ..\build\setup.res setup.rc
if errorlevel 1 (
  popd
  exit /b 1
)
popd
cl %CXXFLAGS% packaging\setup.cpp %LZMAOBJ% build\setup.res /Fe:"dist\MpvLight-Setup-%VERSION%-x64.exe" /Fo:build\setup.obj /link /SUBSYSTEM:WINDOWS /MANIFEST:NO %LIBS%
if errorlevel 1 exit /b 1

rem --- libmpv-2.dll for the portable folder: reuse a local copy if there is one.
rem     (Otherwise MpvLight offers "Scarica il motore" on its start screen.)
if not exist portable\libmpv-2.dll if exist libmpv-2.dll copy /y libmpv-2.dll portable\ >nul
if not exist portable\libmpv-2.dll if exist "%LOCALAPPDATA%\Programs\MpvLight\libmpv-2.dll" copy /y "%LOCALAPPDATA%\Programs\MpvLight\libmpv-2.dll" portable\ >nul

rem --- Release: portable ZIP (without libmpv, which MpvLight downloads by itself) and SHA-256 list.
set "STAGE=build\stage\MpvLight-%VERSION%"
if exist build\stage rmdir /s /q build\stage
mkdir "%STAGE%"
copy /y portable\MpvLight.exe "%STAGE%\" >nul
copy /y README.md "%STAGE%\" >nul
copy /y CHANGELOG.md "%STAGE%\" >nul
copy /y LICENSE-GPL.txt "%STAGE%\" >nul
type nul > "%STAGE%\portable.flag"
set "ZIP=dist\MpvLight-%VERSION%-portable-x64.zip"
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C build\stage "MpvLight-%VERSION%"
if errorlevel 1 (
  echo ERRORE: creazione di %ZIP% non riuscita ^(serve tar.exe di Windows 10 1803 o successivo^)
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-FileHash -Algorithm SHA256 -LiteralPath 'dist\MpvLight-Setup-%VERSION%-x64.exe','%ZIP%' | ForEach-Object { '{0}  {1}' -f $_.Hash.ToLower(), (Split-Path $_.Path -Leaf) } | Set-Content -Encoding ascii 'dist\SHA256SUMS.txt'"
if errorlevel 1 echo ATTENZIONE: SHA256SUMS.txt non generato

echo.
echo Fatto (MpvLight %VERSION%):
echo   portable\MpvLight.exe
echo   dist\MpvLight-Setup-%VERSION%-x64.exe
echo   %ZIP%
echo   dist\SHA256SUMS.txt
if not exist portable\libmpv-2.dll echo   (portable senza libmpv-2.dll: MpvLight la scarica da solo con "Scarica il motore")
exit /b 0
