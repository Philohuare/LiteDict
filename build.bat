@echo off
rem ============================================================
rem  LiteDict build script
rem  usage: build.bat litedict        -> LiteDict.exe
rem         build.bat test_dict console -> console tests
rem
rem  vcvars64.bat shells out to reg.exe, which the security
rem  policy blocks here, so paths are wired up manually.
rem ============================================================
setlocal

set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Enterprise"
set "MSVCVER=14.51.36231"
set "SDKVER=10.0.26100.0"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "VC=%VSDIR%\VC\Tools\MSVC\%MSVCVER%"

set "PATH=%VC%\bin\Hostx64\x64;%SDK%\bin\%SDKVER%\x64;%PATH%"
set "INCLUDE=%VC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\winrt"
set "LIB=%VC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

cd /d "%~dp0"

set "LIBS=user32.lib gdi32.lib shell32.lib comdlg32.lib winhttp.lib winmm.lib shlwapi.lib advapi32.lib ole32.lib oleaut32.lib sapi.lib dwmapi.lib imm32.lib msimg32.lib"

if /I "%~1"=="litedict" goto :app

cl /nologo /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /I"src" ^
   src\%1.c src\dict.c ^
   /Fe:%1.exe ^
   /link /SUBSYSTEM:CONSOLE /MACHINE:X64 %LIBS%
if errorlevel 1 goto :fail
echo BUILD_OK
exit /b 0

:app
rc /nologo /fo res\app.res res\app.rc
if errorlevel 1 goto :fail
cl /nologo /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /I"src" ^
   src\app.c src\dict.c src\online.c src\store.c res\app.res ^
   /Fe:LiteDict.exe ^
   /link /SUBSYSTEM:WINDOWS /MACHINE:X64 %LIBS%
if errorlevel 1 goto :fail
echo BUILD_OK
exit /b 0

:fail
echo COMPILE_FAILED
exit /b 1
