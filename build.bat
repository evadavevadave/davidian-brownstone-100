@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Copyright (C) 2026 Davidian
rem Builds build\brownstone100.lv2 (plugin + GUI) with MSVC (x64).
rem   build.bat         build the bundle
rem   build.bat test    also build and run the tests in build-test\
rem   build.bat dist    also zip the bundle with the docs into build\dist\ (set VERSION first)
rem Sets up the Visual Studio environment if cl.exe is not on PATH.
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
rem /fp:precise keeps the NaN guard and isfinite() checks that /fp:fast may remove.
cl /nologo /O2 /fp:precise /MT /W3 /LD src\brownstone100.c /Fo"%TESTS%\\" /Fe"%BUNDLE%\brownstone100.dll" /link /NOLOGO /NOIMPLIB /NOEXP || exit /b 1

rem The GUI: a Win32 window drawn by its own software renderer.
cl /nologo /O2 /MT /W3 /LD src\ui\ui_lv2.c /Fo"%TESTS%\\" /Fe"%BUNDLE%\brownstone100_ui.dll" /link /NOLOGO /NOIMPLIB /NOEXP user32.lib gdi32.lib || exit /b 1

copy /y brownstone100.lv2\brownstone100.ttl "%BUNDLE%\" >nul
powershell -NoProfile -Command "(Get-Content brownstone100.lv2\manifest.ttl.in) -replace '@LIB_EXT@','.dll' -replace '@UI_CLASS@','ui:WindowsUI' | Set-Content -Encoding ascii '%BUNDLE%\manifest.ttl'"

if "%1"=="test" goto :test
if "%1"=="dist" goto :dist
goto :done

:test
cl /nologo /O2 /fp:precise /MT /W3 test\render.c /Fo"%TESTS%\\" /Fe"%TESTS%\render.exe" /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W3 test\ui_render.c /Fo"%TESTS%\\" /Fe"%TESTS%\ui_render.exe" /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W3 test\ui_check.c /Fo"%TESTS%\\" /Fe"%TESTS%\ui_check.exe" /link /NOLOGO user32.lib gdi32.lib || exit /b 1
pushd "%TESTS%"
"%TESTS%\render.exe" || (popd & exit /b 1)
"%TESTS%\ui_render.exe" "%TESTS%\panel.bmp" || (popd & exit /b 1)
"%TESTS%\ui_check.exe" "%BUNDLE%" "%TESTS%\ui_window.bmp" || (popd & exit /b 1)
popd
goto :done

:dist
if "%VERSION%"=="" set "VERSION=dev"
set "DISTNAME=davidian-brownstone-100-%VERSION%-windows-x64"
set "DISTDIR=%~dp0build\dist"
if exist "%DISTDIR%\%DISTNAME%" rmdir /s /q "%DISTDIR%\%DISTNAME%"
mkdir "%DISTDIR%\%DISTNAME%" || exit /b 1
xcopy /e /i /q "%BUNDLE%" "%DISTDIR%\%DISTNAME%\brownstone100.lv2" >nul || exit /b 1
copy /y README.md "%DISTDIR%\%DISTNAME%\" >nul
copy /y LICENSE "%DISTDIR%\%DISTNAME%\" >nul
copy /y LICENSE-LV2 "%DISTDIR%\%DISTNAME%\" >nul
powershell -NoProfile -Command "Compress-Archive -Force -Path '%DISTDIR%\%DISTNAME%' -DestinationPath '%DISTDIR%\%DISTNAME%.zip'" || exit /b 1
echo Packaged %DISTDIR%\%DISTNAME%.zip

:done
echo Built %BUNDLE%
