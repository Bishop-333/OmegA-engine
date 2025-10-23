@echo off
echo Fixing AI includes to add qcommon.h...

REM Process each AI file that uses Z_Malloc
for %%f in (
    "src\game\ai\learning\rl_ppo.c"
    "src\game\ai\learning\skill_adaptation.c"
    "src\game\ai\character\bot_character.c"
    "src\game\ai\strategic\strategic_planning.c"
    "src\game\ai\team\team_coordination.c"
    "src\game\ai\tactical\cover_system.c"
    "src\game\ai\tactical\movement_tactics.c"
    "src\game\ai\tactical\tactical_combat.c"
    "src\game\ai\perception\ai_perception.c"
) do (
    echo Processing %%f
    powershell -Command "$content = Get-Content '%%f' -Raw; if ($content -notmatch 'qcommon\.h') { $lines = $content -split \"`r?`n\"; $inserted = $false; $newLines = @(); foreach ($line in $lines) { $newLines += $line; if (-not $inserted -and $line -match '^#include') { $newLines += '#include \"../../../engine/core/qcommon.h\"'; $inserted = $true } }; $newContent = $newLines -join \"`r`n\"; Set-Content -Path '%%f' -Value $newContent -NoNewline }"
)

echo Done!