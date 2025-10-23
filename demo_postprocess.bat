@echo off
echo Recording and playing demo with post-processing effects...
echo.

cd /d F:\Development\Quake3e-HD

echo Step 1: Recording demo...
echo Starting game to record a demo on q3dm1
echo Commands to run in console:
echo   /map q3dm1
echo   /record testdemo
echo   (play for a few seconds)
echo   /stoprecord
echo   /quit
echo.

src\project\msvc2017\output\quake3e-debug.x64.exe ^
    +set fs_basepath "F:\Development\Quake3e-HD" ^
    +set bot_enable 1 ^
    +map q3dm1 ^
    +wait 100 ^
    +addbot sarge 3 ^
    +addbot visor 3 ^
    +wait 100 ^
    +record testdemo

echo.
echo Demo should be recorded as testdemo.dm_68 in baseq3\demos\
echo.
echo Step 2: Playing demo with post-processing effects...
pause

src\project\msvc2017\output\quake3e-debug.x64.exe ^
    +set fs_basepath "F:\Development\Quake3e-HD" ^
    +set developer 2 ^
    +set r_postProcess 1 ^
    +set r_postProcessDebug 1 ^
    +set r_chromaticAberration 1 ^
    +set r_vignette 1 ^
    +set r_dof 1 ^
    +set r_filmGrain 1 ^
    +demo testdemo

pause