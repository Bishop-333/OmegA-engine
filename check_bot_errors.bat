@echo off
echo Checking bot system status...
echo.

cd /d F:\Development\Quake3e-HD

REM Create a simple config to test
echo set developer 2 > baseq3\test.cfg
echo set logfile 3 >> baseq3\test.cfg
echo set bot_enable 1 >> baseq3\test.cfg
echo set bot_debug 1 >> baseq3\test.cfg
echo set sv_pure 0 >> baseq3\test.cfg
echo set dedicated 1 >> baseq3\test.cfg
echo map q3dm1 >> baseq3\test.cfg
echo addbot sarge >> baseq3\test.cfg
echo wait 100 >> baseq3\test.cfg
echo quit >> baseq3\test.cfg

echo Running quick test...
timeout /t 1 >nul

REM Try to run and immediately quit
start /B /WAIT src\project\msvc2017\output\quake3e-debug.x64.exe +exec test.cfg 2>bot_error.txt

timeout /t 5 >nul

REM Kill the process if it's still running
taskkill /F /IM quake3e-debug.x64.exe 2>nul

echo.
if exist bot_error.txt (
    echo === Error Output ===
    type bot_error.txt
    echo.
)

echo Checking for crash dumps...
dir /b *.dmp 2>nul

echo.
echo Checking Windows Event Log for crashes...
powershell -Command "Get-EventLog -LogName Application -Newest 10 | Where-Object {$_.Message -like '*quake3e*'} | Select-Object TimeGenerated, Message | Format-List"

pause