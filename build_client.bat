@echo off
setlocal

REM Try to find Visual Studio 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
    echo Found Visual Studio 2022 Professional
    call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    goto :build
)

if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    echo Found Visual Studio 2022 Community
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    goto :build
)

if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" (
    echo Found Visual Studio 2022 Enterprise
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    goto :build
)

REM Try Visual Studio 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat" (
    echo Found Visual Studio 2019 Professional
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat"
    goto :build
)

if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat" (
    echo Found Visual Studio 2019 Community
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"
    goto :build
)

echo ERROR: Could not find Visual Studio installation
exit /b 1

:build
echo.
echo Building Quake3e-HD Client (Debug x64)...
echo ========================================

cd /d "%~dp0"

echo Building dependencies...
msbuild "src\project\msvc2017\libjpeg.vcxproj" /p:Configuration=Debug /p:Platform=x64 /v:minimal
if errorlevel 1 goto :error

msbuild "src\project\msvc2017\libogg.vcxproj" /p:Configuration=Debug /p:Platform=x64 /v:minimal
if errorlevel 1 goto :error

msbuild "src\project\msvc2017\libvorbis.vcxproj" /p:Configuration=Debug /p:Platform=x64 /v:minimal
if errorlevel 1 goto :error

echo.
echo Building client executable...
msbuild "src\project\msvc2017\quake3e.vcxproj" /p:Configuration=Debug /p:Platform=x64 /v:minimal
if errorlevel 1 goto :error

echo.
echo ========================================
echo Build completed successfully!
echo Client executable should be at: src\project\msvc2017\output\quake3e-debug.x64.exe
echo ========================================
goto :end

:error
echo.
echo ========================================
echo Build FAILED!
echo ========================================
exit /b 1

:end
endlocal