@echo off
echo Building Quake3e-HD with Uber Shader support...

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if not exist %MSBUILD% (
    echo MSBuild not found! Please install Visual Studio 2022
    exit /b 1
)

%MSBUILD% src\project\msvc2017\quake3e.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal

if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

echo Build successful!
echo.
echo Uber shader system has been integrated. Features:
echo - Unified shader system replacing multiple pipeline permutations
echo - Runtime configuration via push constants
echo - Controlled by r_useUberShader cvar (1=enabled, 0=disabled)
echo.
echo To test:
echo 1. Run quake3e.x64.exe
echo 2. Open console and type: r_useUberShader 1
echo 3. Restart renderer with: vid_restart