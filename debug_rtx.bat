@echo off
echo Starting Quake3e-HD with GDB debugger for RTX investigation...
cd /d "F:\Development\Quake3e-HD\src\project\msvc2017"

REM Set environment variables for better debugging
set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
set VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT
set VK_LOADER_DEBUG=all

echo.
echo Commands to use in GDB:
echo   break RTX_DispatchRaysVK
echo   break vkQueueSubmit
echo   break vkWaitForFences
echo   run +set r_rtx 1 +set rtx_debug 1 +set rtx_enabled 1 +set developer 1 +map q3dm17 +timedemo 1 +demo four
echo.
echo When breakpoint hits:
echo   info locals
echo   print vkrt.fence
echo   print *params
echo   backtrace
echo   continue
echo.

gdb.exe output\quake3e-debug.x64.exe