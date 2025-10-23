@echo off
echo Copying shader files to output directory...

set SRC_DIR=F:\Development\Quake3e-HD\baseq3\shaders
set DST_DIR=F:\Development\Quake3e-HD\src\project\msvc2017\output\baseq3\shaders

:: Create directories
echo Creating directories...
mkdir "%DST_DIR%" 2>nul
mkdir "%DST_DIR%\compute" 2>nul
mkdir "%DST_DIR%\rtx" 2>nul
mkdir "%DST_DIR%\postprocess" 2>nul
mkdir "%DST_DIR%\glsl" 2>nul

:: Copy compute shaders
echo Copying compute shaders...
copy /Y "%SRC_DIR%\compute\*.spv" "%DST_DIR%\compute\" 2>nul

:: Copy RTX shaders
echo Copying RTX shaders...
copy /Y "%SRC_DIR%\rtx\*.spv" "%DST_DIR%\rtx\" 2>nul

:: Copy postprocess shaders
echo Copying postprocess shaders...
copy /Y "%SRC_DIR%\postprocess\*.spv" "%DST_DIR%\postprocess\" 2>nul

:: Copy GLSL shaders
echo Copying GLSL shaders...
copy /Y "%SRC_DIR%\glsl\*.spv" "%DST_DIR%\glsl\" 2>nul

echo.
echo Shader files copied successfully!
echo.

:: List what was copied
echo Compute shaders:
dir /b "%DST_DIR%\compute\*.spv" 2>nul
echo.
echo RTX shaders:
dir /b "%DST_DIR%\rtx\*.spv" 2>nul
echo.

pause