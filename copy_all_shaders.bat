@echo off
echo Copying all shaders to output directory...

rem Create shader directories if they don't exist
if not exist "src\project\msvc2017\output\baseq3\shaders" mkdir "src\project\msvc2017\output\baseq3\shaders"
if not exist "src\project\msvc2017\output\baseq3\shaders\compute" mkdir "src\project\msvc2017\output\baseq3\shaders\compute"
if not exist "src\project\msvc2017\output\baseq3\shaders\rtx" mkdir "src\project\msvc2017\output\baseq3\shaders\rtx"
if not exist "src\project\msvc2017\output\baseq3\shaders\postprocess" mkdir "src\project\msvc2017\output\baseq3\shaders\postprocess"
if not exist "src\project\msvc2017\output\baseq3\shaders\glsl" mkdir "src\project\msvc2017\output\baseq3\shaders\glsl"

rem Copy compute shaders
echo Copying compute shaders...
copy "baseq3\shaders\compute\*.spv" "src\project\msvc2017\output\baseq3\shaders\compute\" /Y

rem Copy RTX shaders
echo Copying RTX shaders...
copy "baseq3\shaders\rtx\*.spv" "src\project\msvc2017\output\baseq3\shaders\rtx\" /Y

rem Copy postprocess shaders
echo Copying postprocess shaders...
copy "baseq3\shaders\postprocess\*.spv" "src\project\msvc2017\output\baseq3\shaders\postprocess\" /Y

rem Copy GLSL shaders
echo Copying GLSL shaders...
copy "baseq3\shaders\glsl\*.spv" "src\project\msvc2017\output\baseq3\shaders\glsl\" /Y

echo Done copying all shaders.