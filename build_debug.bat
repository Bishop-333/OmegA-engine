@echo off
setlocal
REM Trim the PATH before invoking vcvars64 to avoid hitting the 8191-character limit.
set "PATH=%SystemRoot%\system32;%SystemRoot%;%SystemRoot%\System32\Wbem;%SystemRoot%\System32\WindowsPowerShell\v1.0;%SystemRoot%\System32\OpenSSH\"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
IF ERRORLEVEL 1 goto :end
msbuild src\project\msvc2017\quake3e.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Build /m
:end
endlocal
