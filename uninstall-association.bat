@echo off
setlocal
reg delete "HKCU\Software\Classes\.k7" /ve /f >nul 2>&1
reg delete "HKCU\Software\Classes\K7.Source" /f >nul 2>&1
echo K7 dosya iliskilendirmesi kaldirildi.
endlocal
