@echo off
rem Run from an "x64 Native Tools Command Prompt for VS" (Visual Studio or Build Tools).
rem Produces build\InchesPropertyHandler.dll (64-bit, static CRT - no redistributable needed).

setlocal
cd /d "%~dp0"
if not exist build mkdir build

cl /nologo /LD /O2 /EHsc /MT /W4 /DUNICODE /D_UNICODE ^
   /Fobuild\ src\InchesHandler.cpp ^
   /link /DEF:src\InchesHandler.def /OUT:build\InchesPropertyHandler.dll ^
   propsys.lib ole32.lib advapi32.lib uuid.lib shlwapi.lib

if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo Built build\InchesPropertyHandler.dll
