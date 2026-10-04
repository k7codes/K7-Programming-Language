@echo off
setlocal enabledelayedexpansion
title K7 - Testler

cd /d "%~dp0.."
if not exist build\k7.exe (
  echo [HATA] build\k7.exe yok. Once build.bat calistir.
  exit /b 1
)

set "K7=build\k7.exe"
set "TOPLAM=0"
set "BASARISIZ=0"

echo.
echo  K7 testleri calistiriliyor...
echo.

for %%F in (tests\*.k7) do (
  set /a TOPLAM+=1
  echo --- %%F
  %K7% "%%F"
  if errorlevel 1 (
    set /a BASARISIZ+=1
    echo     [BASARISIZ] %%F
  ) else (
    echo     [OK] %%F
  )
  echo.
)

echo  ----------------------------------------
echo  Toplam: %TOPLAM%   Basarisiz: %BASARISIZ%
if not "%BASARISIZ%"=="0" (
  echo  [HATA] Basarisiz test var.
  exit /b 1
)
echo  Tum testler gecti.
endlocal