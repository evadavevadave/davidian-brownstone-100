@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Davidian
rem Builds build\brownstone100.lv2 (plugin + GUI) with MSVC. "build.bat test" also builds and
rem runs the tests in build-test\. Sets up the Visual Studio environment if cl.exe is not on PATH.
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_cl

set "BUNDLE=%~dp0build\brownstone100.lv2"
set "TESTS=%~dp0build-test"
if not exist "%BUNDLE%" mkdir "%BUNDLE%"
if not exist "%TESTS%" mkdir "%TESTS%"

rem /MT links the C runtime statically, so hosts don't need a redistributable installed.
cl /nologo /O2 /fp:fast /MT /W3 /LD src\brownstone100.c /Fo"%TESTS%\\" /Fe"%BUNDLE%\brownstone100.dll" /link /NOLOGO /NOIMPLIB /NOEXP || exit /b 1

rem The GUI: a Win32 window drawn by its own software renderer.
cl /nologo /O2 /MT /W3 /LD src\ui\ui_lv2.c /Fo"%TESTS%\\" /Fe"%BUNDLE%\brownstone100_ui.dll" /link /NOLOGO /NOIMPLIB /NOEXP user32.lib gdi32.lib || exit /b 1

copy /y brownstone100.lv2\brownstone100.ttl "%BUNDLE%\" >nul
powershell -NoProfile -Command "(Get-Content brownstone100.lv2\manifest.ttl.in) -replace '@LIB_EXT@','.dll' -replace '@UI_CLASS@','ui:WindowsUI' | Set-Content -Encoding ascii '%BUNDLE%\manifest.ttl'"

if not "%1"=="test" goto :done
cl /nologo /O2 /fp:fast /MT /W3 test\render.c /Fo"%TESTS%\\" /Fe"%TESTS%\render.exe" /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W3 test\ui_render.c /Fo"%TESTS%\\" /Fe"%TESTS%\ui_render.exe" /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W3 test\ui_check.c /Fo"%TESTS%\\" /Fe"%TESTS%\ui_check.exe" /link /NOLOGO user32.lib gdi32.lib || exit /b 1
pushd "%TESTS%"
"%TESTS%\render.exe" || (popd & exit /b 1)
"%TESTS%\ui_render.exe" "%TESTS%\panel.bmp" || (popd & exit /b 1)
"%TESTS%\ui_check.exe" "%BUNDLE%" "%TESTS%\ui_window.bmp" || (popd & exit /b 1)
popd

:done
echo Built %BUNDLE%
