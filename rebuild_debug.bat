@echo off
echo Rebuilding Quake3e Debug...
cd src\project\msvc2017

echo Building Debug x64...
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
msbuild quake3e.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /m

echo Build complete!
pause