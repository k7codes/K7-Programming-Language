@echo off
setlocal
title K7 - Tek dosya kontrolu

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>&1
cd /d "%~dp0"

set "FLAGS=/nologo /std:c++20 /O2 /W3 /EHsc /utf-8 /permissive- /D_CRT_SECURE_NO_WARNINGS /Iinclude /c /Fo:build\ /Fd:build\k7.pdb"

if not exist build mkdir build
cl %FLAGS% %1
endlocal