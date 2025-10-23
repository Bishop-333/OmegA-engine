@echo off
echo Performing clean build of Quake3e...
echo.
cd src\project\msvc2017
echo Current directory: %CD%
echo.
echo Running MSBuild...
msbuild quake3e.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:normal
echo MSBuild exit code: %ERRORLEVEL%
if errorlevel 1 (
    echo Build failed with errors
    exit /b 1
)
echo Build completed successfully!