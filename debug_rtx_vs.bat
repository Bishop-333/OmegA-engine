@echo off
setlocal EnableDelayedExpansion

echo === RTX Backend Validation Harness ===

set ROOT=%~dp0
set GAME_ROOT=%ROOT%src\project\msvc2017\output
set BASEQ3=%GAME_ROOT%\baseq3
set CI_DIR=%BASEQ3%\ci

if not exist "%GAME_ROOT%" (
    echo [ERROR] Build output directory not found: %GAME_ROOT%
    exit /b 1
)

if not exist "%BASEQ3%" (
    echo [ERROR] baseq3 assets not found in %GAME_ROOT%
    exit /b 1
)

if not exist "%CI_DIR%" (
    mkdir "%CI_DIR%"
)

set EXE=%GAME_ROOT%\quake3e.x64.exe
if not exist "%EXE%" set EXE=%GAME_ROOT%\quake3e-debug.x64.exe
if not exist "%EXE%" (
    echo [ERROR] Unable to locate quake3e executable in %GAME_ROOT%
    exit /b 1
)

if not defined RT_MAPS (
    set MAPS=q3dm1 q3dm7 q3dm17
) else (
    set MAPS=%RT_MAPS%
)

if defined RT_WAIT_COMPUTE (
    set WAIT_COMPUTE=%RT_WAIT_COMPUTE%
) else (
    set WAIT_COMPUTE=90
)
if defined RT_WAIT_SWITCH (
    set WAIT_SWITCH=%RT_WAIT_SWITCH%
) else (
    set WAIT_SWITCH=30
)
if defined RT_WAIT_HARDWARE (
    set WAIT_HARDWARE=%RT_WAIT_HARDWARE%
) else (
    set WAIT_HARDWARE=90
)

del "%CI_DIR%\rt_backend_metrics.csv" 2>nul
del "%CI_DIR%\rt_backend_hashes.csv" 2>nul
del "%CI_DIR%\rt_validation_summary.txt" 2>nul

for %%M in (%MAPS%) do (
    call :RunMap %%M
    if errorlevel 1 (
        echo [WARN] Validation run for %%M encountered issues.
    )
)

rem Consolidate per-map CSV files
powershell -NoProfile -Command ^
  "$dir = '%CI_DIR%'; $metrics = Get-ChildItem -Path $dir -Filter 'rt_*_metrics.csv'; if ($metrics) { $all = foreach ($m in $metrics) { Import-Csv $m }; $all | Export-Csv -Path (Join-Path $dir 'rt_backend_metrics.csv') -NoTypeInformation -Encoding ASCII }"
powershell -NoProfile -Command ^
  "$dir = '%CI_DIR%'; $hashes = Get-ChildItem -Path $dir -Filter 'rt_*_hashes.csv'; if ($hashes) { $all = foreach ($h in $hashes) { Import-Csv $h }; $all | Export-Csv -Path (Join-Path $dir 'rt_backend_hashes.csv') -NoTypeInformation -Encoding ASCII }"

echo.
echo Validation artifacts written to %CI_DIR%
echo   - rt_backend_metrics.csv (RMSE/max samples per backend)
echo   - rt_backend_hashes.csv  (frame hashes for CI comparisons)
echo   - rt_validation_summary.txt
exit /b 0

:RunMap
set MAP=%1
setlocal EnableDelayedExpansion

echo.
echo --- Running backend parity on !MAP! ---

set TEMP_CFG=%CI_DIR%\rt_ci_run.cfg

(
    echo seta developer 1
    echo seta logfile 2
    echo seta r_renderAPI 2
    echo seta rt_enable 1
    echo seta rt_mode all
    echo seta rt_gpuValidate 8
    echo seta rt_temporal 0
    echo seta rt_denoise 0
    echo seta vid_fullscreen 0
    echo seta r_fullscreen 0
    echo seta s_initsound 0
    echo seta in_nograb 1
    echo seta com_introplayed 1
    echo seta com_maxfps 0
    echo seta r_rt_backend software
    echo seta rtx_enable 0
    echo map !MAP!
    for /L %%W in (1,1,!WAIT_COMPUTE!) do echo wait
    echo seta r_rt_backend hardware
    echo seta rtx_enable 1
    for /L %%W in (1,1,!WAIT_SWITCH!) do echo wait
    for /L %%W in (1,1,!WAIT_HARDWARE!) do echo wait
    echo quit
) > "!TEMP_CFG!"

del "%BASEQ3%\qconsole.log" 2>nul

pushd "%GAME_ROOT%"
"%EXE%" +set fs_basepath "%GAME_ROOT%" +set fs_homepath "%GAME_ROOT%" +set fs_game baseq3 +exec ci/rt_ci_run.cfg > "%CI_DIR%\rt_!MAP!_console.txt" 2>&1
set RUN_CODE=!ERRORLEVEL!
popd

if not exist "%BASEQ3%\qconsole.log" (
    echo [ERROR] qconsole.log missing for !MAP!
    del "!TEMP_CFG!" 2>nul
    endlocal & exit /b 1
)

copy "%BASEQ3%\qconsole.log" "%CI_DIR%\rt_!MAP!.log" >nul

call :ParseLog "!MAP!" "%CI_DIR%\rt_!MAP!.log"
set PARSE_CODE=!ERRORLEVEL!

del "!TEMP_CFG!" 2>nul

endlocal
exit /b %PARSE_CODE%

:ParseLog
set MAP=%~1
set LOG=%~2

powershell -NoProfile -Command ^
  "$log = Get-Content -Path '%LOG%';" ^
  "$backend = 'rt_gpuValidate \((?<backend>[^)]+)\): stride=(?<stride>\d+) RMSE=(?<rmse>[0-9eE\.\-]+) max=(?<max>[0-9eE\.\-]+) \((?<samples>\d+) samples\)';" ^
  "$parity = 'rt_gpuValidate parity (?<map>\S+): ΔRMSE=(?<delta>[0-9eE\.\-]+) ΔMax=(?<deltaMax>[0-9eE\.\-]+) \(RTX=(?<rtx>[0-9A-F]+), Compute=(?<compute>[0-9A-F]+)\)';" ^
  "$metrics = @();" ^
  "foreach ($line in $log) {" ^
  "  if ($line -match $backend) {" ^
  "    $metrics += [pscustomobject]@{ Map='%MAP%'; Backend=$Matches.backend; Stride=[int]$Matches.stride; RMSE=[double]$Matches.rmse; Max=[double]$Matches.max; Samples=[int]$Matches.samples; Hash=$null }" ^
  "  } elseif ($line -match $parity) {" ^
  "    foreach ($m in $metrics) {" ^
  "      if ($m.Backend -like 'RTX*') { $m.Hash = $Matches.rtx } elseif ($m.Backend -like 'Compute*') { $m.Hash = $Matches.compute }" ^
  "    }" ^
  "    $metrics += [pscustomobject]@{ Map=$Matches.map; Backend='Δ'; Stride=$null; RMSE=[double]$Matches.delta; Max=[double]$Matches.deltaMax; Samples=$null; Hash='RTX=' + $Matches.rtx + ';Compute=' + $Matches.compute }" ^
  "  }" ^
  "}" ^
  "if ($metrics.Count -gt 0) {" ^
  "  $metrics | Export-Csv -Path '%CI_DIR%\rt_%MAP%_metrics.csv' -NoTypeInformation -Encoding ASCII;" ^
  "  $metrics | Where-Object { $_.Backend -ne 'Δ' -and $_.Hash } | Select-Object Map,Backend,Hash | Export-Csv -Path '%CI_DIR%\rt_%MAP%_hashes.csv' -NoTypeInformation -Encoding ASCII;" ^
  "  $summaryLines = @('MAP: %MAP%');" ^
  "  $summaryLines += ($metrics | Format-Table -AutoSize | Out-String);" ^
  "  $summaryLines | Out-File -FilePath '%CI_DIR%\rt_%MAP%_summary.txt' -Encoding ASCII;" ^
  "  $summaryLines | Add-Content -Path '%CI_DIR%\rt_validation_summary.txt';" ^
  "} else {" ^
  "  Add-Content -Path '%CI_DIR%\rt_validation_summary.txt' -Value @('MAP: %MAP%','  [WARN] No validation data captured.');" ^
  "  exit 1;" ^
  "}"
if errorlevel 1 (
    exit /b 1
)
exit /b 0
