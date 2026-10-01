@echo off
setlocal
rem call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
rem if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )
cd /d "%~dp0"

echo === building shim fjicnv.dll ===
cl /nologo /utf-8 /W3 /O2 /LD fjicnv_shim.c /link /DEF:fjicnv.def /OUT:fjicnv.dll
if errorlevel 1 goto :err

echo === building launcher.exe ===
cl /nologo /utf-8 /W3 /O2 launcher.c /link /OUT:launcher.exe shell32.lib user32.lib advapi32.lib
if errorlevel 1 goto :err

echo === done ===
exit /b 0

:err
echo BUILD FAILED
exit /b 1
