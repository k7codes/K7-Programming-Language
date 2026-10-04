@echo off
setlocal

set "K7_HOME=%~dp0"
set "K7_RUNTIME=%K7_HOME%build\k7.exe"

if not exist "%K7_RUNTIME%" (
  echo [HATA] K7 yorumlayicisi bulunamadi: "%K7_RUNTIME%"
  echo Once build.bat dosyasini calistir.
  exit /b 1
)

"%K7_RUNTIME%" %*
exit /b %errorlevel%
