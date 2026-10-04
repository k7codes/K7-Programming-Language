@echo off
setlocal
title K7 Language - Build

set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=* delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
  )
)

if not defined VCVARS (
  for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)

if not defined VCVARS (
  echo [HATA] MSVC x64 derleme araclari bulunamadi.
  echo Visual Studio 2022 veya Build Tools icinde "Desktop development with C++" is yukunu kurun.
  exit /b 1
)

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo [HATA] MSVC ortam yuklenemedi.
  exit /b 1
)

rem Proje dizinine gec: yol boslugu icermesin diye goreli yollar kullanilir.
cd /d "%~dp0"
if not exist build mkdir build
if not exist build\tools mkdir build\tools

set "FLAGS=/nologo /std:c++20 /O2 /GL /W3 /EHsc /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS"

echo.
echo  K7 derleniyor...
echo.

del /q build\*.obj 2>nul

cl %FLAGS% /Iinclude /c /Fo:build\ /Fd:build\k7.pdb ^
  src\main.cpp ^
  src\lexer.cpp ^
  src\parser.cpp ^
  src\value.cpp ^
  src\interp.cpp ^
  src\ops.cpp ^
  src\call.cpp ^
  src\builtins.cpp ^
  src\modules.cpp ^
  src\stdlib\mod_math.cpp ^
  src\stdlib\mod_time.cpp ^
  src\stdlib\mod_json.cpp ^
  src\stdlib\mod_fs.cpp ^
  src\stdlib\mod_random.cpp ^
  src\stdlib\mod_os.cpp ^
  src\stdlib\mod_str.cpp ^
  src\stdlib\mod_re.cpp

if errorlevel 1 (
  echo.
  echo [HATA] Derleme basarisiz.
  exit /b 1
)

link /nologo /LTCG /OUT:build\k7.exe /PDB:build\k7.pdb build\*.obj
if errorlevel 1 (
  echo.
  echo [HATA] Baglama basarisiz.
  exit /b 1
)

rem Lexer gelistirme araci (tests icin gerekli degil)
cl %FLAGS% /Iinclude /Fe:build\dump_tokens.exe /Fo:build\tools\ tools\dump_tokens.cpp src\lexer.cpp >nul 2>&1

echo.
echo  Tamam: build\k7.exe
endlocal
