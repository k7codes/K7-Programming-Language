@echo off
setlocal
set "K7_EXE=%~dp0build\k7.exe"
if not exist "%K7_EXE%" (
  echo [HATA] Yorumlayici bulunamadi: "%K7_EXE%"
  echo Once build.bat dosyasini calistirin.
  exit /b 1
)

set "OPEN_COMMAND=cmd.exe /k ""%K7_EXE%" "%%1"""
reg add "HKCU\Software\Classes\.k7" /ve /d "K7.Source" /f >nul
if errorlevel 1 exit /b 1
reg add "HKCU\Software\Classes\K7.Source" /ve /d "K7 kaynak dosyasi" /f >nul
if errorlevel 1 exit /b 1
reg add "HKCU\Software\Classes\K7.Source\DefaultIcon" /ve /d """%K7_EXE%"",0" /f >nul
reg add "HKCU\Software\Classes\K7.Source\shell\open\command" /ve /d "%OPEN_COMMAND%" /f >nul
if errorlevel 1 exit /b 1
echo .k7 dosyalari bu K7 klasorundeki yorumlayiciya baglandi.
echo Komut penceresi betik ciktisini gostermek icin acik kalir.
endlocal
